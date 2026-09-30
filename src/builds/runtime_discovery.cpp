// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "runtime_discovery.h"


#include <algorithm>
#include <cstring>
#include <initializer_list>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>

namespace finch_ht::builds {
namespace {

class Rejected : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void Require(bool condition, const char* reason) {
    if (!condition) throw Rejected(reason);
}

struct Range {
    std::uint32_t begin;
    std::uint32_t size;

    bool Contains(std::uint64_t at, std::size_t length = 1) const {
        return at >= begin && length <= size && at - begin <= size - length;
    }
};

class Image {
public:
    explicit Image(ImageView view) : view_(view) {
        Require(Read<std::uint16_t>(0) == 0x5a4d, "missing DOS header");
        const auto nt = Read<std::uint32_t>(0x3c);
        Require(Read<std::uint32_t>(nt) == 0x4550 &&
                Read<std::uint16_t>(nt + 4) == 0x8664 &&
                Read<std::uint16_t>(nt + 24) == 0x20b, "expected an x64 PE image");
        Require(Read<std::uint32_t>(nt + 24 + 56) == view.size,
                "PE image size disagrees with the captured module");
        const auto count = Read<std::uint16_t>(nt + 6);
        const auto optionalSize = Read<std::uint16_t>(nt + 20);
        Require(optionalSize >= 160 && count > 0 && count <= 96, "invalid PE section table");
        const std::uint64_t sections = static_cast<std::uint64_t>(nt) + 24 + optionalSize;
        for (unsigned i = 0; i < count; ++i) {
            const auto header = sections + i * 40;
            Bounds(header, 40);
            char name[9]{};
            std::memcpy(name, view.data + header, 8);
            Range range{Read<std::uint32_t>(header + 12), Read<std::uint32_t>(header + 8)};
            Bounds(range.begin, range.size);
            Require(sections_.emplace(name, range).second, "duplicate PE section name");
            flags_.emplace(name, Read<std::uint32_t>(header + 36));
        }
        text = Section(".text", 0x20000000);
        rdata = Section(".rdata", 0x40000000);
        writable = Section(".data", 0x80000000);
        const auto pdata = Section(".pdata", 0x40000000);
        const auto exceptionRva = Read<std::uint32_t>(nt + 24 + 112 + 3 * 8);
        const auto exceptionSize = Read<std::uint32_t>(nt + 24 + 112 + 3 * 8 + 4);
        Require(exceptionSize && exceptionSize % 12 == 0 &&
                pdata.Contains(exceptionRva, exceptionSize), "invalid PE exception directory");
        for (std::uint32_t i = 0; i < exceptionSize; i += 12) {
            const auto begin = Read<std::uint32_t>(exceptionRva + i);
            const auto end = Read<std::uint32_t>(exceptionRva + i + 4);
            Require(end > begin && end <= view.size, "invalid function extent");
            Require(functions_.empty() || functions_.back().begin < begin,
                    "unsorted PE exception directory");
            functions_.push_back({begin, end - begin});
            unwind_[begin] = Read<std::uint32_t>(exceptionRva + i + 8);
        }
    }

    template<class T> T Read(std::uint64_t at) const {
        Bounds(at, sizeof(T));
        T value;
        std::memcpy(&value, view_.data + at, sizeof(T));
        return value;
    }

    std::uint32_t Relative(std::uint32_t displacement) const {
        const auto value = static_cast<std::int64_t>(displacement) + 4 + Read<std::int32_t>(displacement);
        Require(value >= 0 && static_cast<std::uint64_t>(value) < view_.size,
                "relative instruction target is outside the module");
        return static_cast<std::uint32_t>(value);
    }

    Range Function(std::uint32_t at) const {
        auto it = std::upper_bound(functions_.begin(), functions_.end(), at,
            [](std::uint32_t address, Range range) { return address < range.begin; });
        if (it == functions_.begin() || !(--it)->Contains(at)) return {};
        return *it;
    }

    std::uint32_t Root(std::uint32_t at) const {
        auto fn = Function(at);
        Require(fn.size != 0, "instruction has no unwind function");
        std::set<std::uint32_t> visited;
        for (;;) {
            Require(visited.insert(fn.begin).second, "cyclic chained unwind data");
            const auto u = unwind_.at(fn.begin);
            Require((Read<std::uint8_t>(u) & 7) == 1 || (Read<std::uint8_t>(u) & 7) == 2,
                    "unsupported unwind information version");
            const auto flags = Read<std::uint8_t>(u) >> 3;
            if (!(flags & 4)) return fn.begin;
            Require((flags & 3) == 0, "invalid chained unwind flags");
            const auto count = Read<std::uint8_t>(u + 2);
            const auto parent = Read<std::uint32_t>(u + 4 + ((count + 1u) & ~1u) * 2);
            fn = Function(parent);
            Require(fn.size && fn.begin == parent, "invalid chained unwind parent");
        }
    }

    std::vector<Range> Parts(std::uint32_t root) const {
        if (!partsIndexed_) {
            for (auto fn : functions_) parts_[Root(fn.begin)].push_back(fn);
            partsIndexed_ = true;
        }
        const auto found = parts_.find(root);
        return found == parts_.end() ? std::vector<Range>{} : found->second;
    }

    std::vector<std::uint32_t> Find(Range range, std::initializer_list<int> pattern) const {
        std::vector<std::uint32_t> hits;
        if (pattern.size() > range.size) return hits;
        const auto last = range.begin + range.size - pattern.size();
        for (std::uint32_t at = range.begin; at <= last; ++at) {
            std::size_t index = 0;
            for (const int byte : pattern) {
                if (byte >= 0 && view_.data[at + index] != byte) break;
                ++index;
            }
            if (index == pattern.size()) hits.push_back(at);
        }
        return hits;
    }

    std::vector<std::uint32_t> Strings(Range range, const char* name, bool wide = false) const {
        std::vector<std::uint8_t> bytes;
        do {
            bytes.push_back(static_cast<std::uint8_t>(*name));
            if (wide) bytes.push_back(0);
        } while (*name++);
        std::vector<std::uint32_t> hits;
        auto cursor = view_.data + range.begin;
        const auto end = cursor + range.size;
        while (cursor < end) {
            const auto found = std::search(cursor, end, bytes.begin(), bytes.end());
            if (found == end) break;
            const auto at = static_cast<std::uint32_t>(found - view_.data);
            if (at == range.begin || Read<std::uint8_t>(at - 1) == 0) hits.push_back(at);
            cursor = found + 1;
        }
        return hits;
    }

    Range text{}, rdata{}, writable{};

    std::vector<std::uint32_t> NamedEntries(const char* name, bool wide = false) const {
        const auto strings = Strings(rdata, name, wide);
        const std::set<std::uint32_t> names(strings.begin(), strings.end());
        std::vector<std::uint32_t> entries;
        for (auto at = rdata.begin; rdata.Contains(at, 16); at += 8) {
            const auto pointer = Read<std::uint64_t>(at);
            if (pointer >= view_.base && pointer - view_.base <= UINT32_MAX &&
                names.count(static_cast<std::uint32_t>(pointer - view_.base))) entries.push_back(at);
        }
        return entries;
    }

    bool Starts(std::uint32_t at, std::initializer_list<int> pattern) const {
        return text.Contains(at, pattern.size()) &&
               !Find({at, static_cast<std::uint32_t>(pattern.size())}, pattern).empty();
    }

private:
    void Bounds(std::uint64_t at, std::size_t length) const {
        Require(at <= view_.size && length <= view_.size - at, "read extends beyond the captured image");
    }

    Range Section(const char* name, std::uint32_t requiredFlag) const {
        const auto found = sections_.find(name);
        Require(found != sections_.end() && found->second.size != 0, "required PE section is missing");
        Require((flags_.at(name) & requiredFlag) != 0, "unexpected PE section protection");
        return found->second;
    }

    mutable bool partsIndexed_ = false;
    mutable std::map<std::uint32_t, std::vector<Range>> parts_;
    ImageView view_;
    std::map<std::string, Range> sections_;
    std::map<std::string, std::uint32_t> flags_;
    std::vector<Range> functions_;
    std::map<std::uint32_t, std::uint32_t> unwind_;
};

std::uint32_t Unique(const std::set<std::uint32_t>& values, const char* reason) {
    Require(values.size() == 1, reason);
    return *values.begin();
}

std::map<std::uint32_t,std::set<std::uint32_t>> References(const Image& image) {
    std::map<std::uint32_t,std::set<std::uint32_t>> refs;
    for(auto prefix:{0x48,0x4c}) for(auto reg:{0x05,0x0d,0x15,0x1d,0x25,0x2d,0x35,0x3d})
        for(auto at:image.Find(image.text,{prefix,0x8d,reg,-1,-1,-1,-1}))
        {
            const auto target=static_cast<std::int64_t>(at)+7+image.Read<std::int32_t>(at+3);
            if(target>=0 && target<=UINT32_MAX)refs[static_cast<std::uint32_t>(target)].insert(at);
        }
    return refs;
}
std::pair<std::uint32_t,std::uint32_t> StructGlobal(const Image& image,const char* name,
    const std::map<std::uint32_t,std::set<std::uint32_t>>& refs) {
    std::set<std::uint32_t> slots;std::uint32_t size=0;
    for(auto str:image.Strings(image.rdata,name,true)) {
        auto it=refs.find(str);if(it==refs.end())continue;
        for(auto at:it->second){
            if(at<6 || !image.Function(at).Contains(at-6,37) ||
               !image.Starts(at-6,{0x41,0xb9,-1,-1,-1,-1,0x4c,0x8d,0x05,-1,-1,-1,-1,
                    0x48,0x8d,0x0d,-1,-1,-1,-1,0xe8,-1,-1,-1,-1,0x48,0x8b,0x5c,0x24,-1,
                    0x48,0x89,0x05,-1,-1,-1,-1}))continue;
            auto target=image.Relative(at+27);
            if(!image.writable.Contains(target,8))continue;
            slots.insert(target);size=image.Read<std::uint32_t>(at-4);
        }
    }
    auto slot=Unique(slots,"named struct registration absent or ambiguous");
    Require(size>0 && size<0x10000,"invalid reflected struct size");return {slot,size};
}
std::uint32_t Native(const Image& image,const char* name,
    const std::map<std::uint32_t,std::set<std::uint32_t>>& refs){
    std::set<std::uint32_t> functions;
    for(auto str:image.Strings(image.rdata,name)){
        auto it=refs.find(str);if(it==refs.end())continue;
        for(auto at:it->second){
            if(at<7 || !image.Starts(at-7,{0x4c,0x8d,0x05,-1,-1,-1,-1,0x48,0x8d,0x15,-1,-1,-1,-1,0xe8}))continue;
            auto fn=image.Relative(at-4);if(image.Function(fn).begin==fn && image.text.Contains(fn))functions.insert(fn);
        }
    }
    return Unique(functions,"native registration absent or ambiguous");
}
void Resolve(const Image& image,OffsetTable& out,DiscoveryLayout& layout){
    auto refs=References(image);std::map<std::uint32_t,unsigned> constructors;
    for(auto name:{"None","ByteProperty","IntProperty","BoolProperty","FloatProperty","ObjectProperty","NameProperty"}){
        std::set<std::uint32_t> roots;
        for(auto str:image.Strings(image.rdata,name,true))for(auto at:refs[str])if(image.Function(at).size)roots.insert(image.Root(at));
        for(auto root:roots)++constructors[root];
    }
    std::set<std::uint32_t> roots,names;
    for(auto item:constructors)if(item.second==7)roots.insert(item.first);
    auto constructor=Unique(roots,"name initializer absent or ambiguous");
    for(auto part:image.Parts(constructor))for(auto at:image.Find(part,{0x48,0x8b,0x3d,-1,-1,-1,-1})){
        auto target=image.Relative(at+3);if(image.writable.Contains(target,8))names.insert(target);
    }
    layout.names=Unique(names,"name storage absent or ambiguous");
    auto view=StructGlobal(image,"MinimalViewInfo",refs),hit=StructGlobal(image,"HitResult",refs);
    layout.viewGlobal=view.first;layout.viewSize=view.second;layout.hitGlobal=hit.first;layout.hitSize=hit.second;
    std::set<std::uint32_t> views;
    for(auto base:{0x81,0x83})for(auto at:image.Find(image.text,{0xf2,0x0f,0x10,base,-1,-1,-1,-1,
        0xf2,0x0f,0x11,0x06,0x8b,base,-1,-1,-1,-1,0x89,0x46,8,
        0xf2,0x0f,0x10,base,-1,-1,-1,-1,0xf2,0x0f,0x11,0x07,0x8b,base,-1,-1,-1,-1,0x89,0x47,8})){
        if(!image.Function(at).Contains(at,42))continue;
        auto loc=image.Read<std::uint32_t>(at+4);
        if(image.Read<std::uint32_t>(at+14)!=loc+8 || image.Read<std::uint32_t>(at+25)!=loc+12 || image.Read<std::uint32_t>(at+35)!=loc+20)continue;
        views.insert(image.Root(at));
        if(base==0x81)layout.cache=loc;
    }
    out.kGetPlayerViewPointRva=Unique(views,"12-byte view output copies absent or ambiguous");
    std::set<std::uint32_t> renders;
    for(auto at:image.Find(image.text,{0x48,0x8b,0x88,-1,-1,-1,-1,0x48,0x8b,0x01,0xff,0x90,-1,-1,-1,-1,
        0xf3,0x0f,0x11,0x07,0x48,0x8b,0x4e,0x30,0x48,0x8b,0x01,0x4c,0x8d,0x43,0x0c,0x48,0x8b,0xd3,0xff,0x90,-1,-1,-1,-1})){
        if(!image.Function(at).Contains(at,40))continue;
        bool output=false;
        for(auto part:image.Parts(image.Root(at)))output|=!image.Find(part,{0x48,0x8d,0x7a,0x18,0x48,0x8b,0xda}).empty();
        if(output){renders.insert(at+40);layout.manager=image.Read<std::uint32_t>(at+3);}
    }
    out.kViewInfoCallerRva=Unique(renders,"render output arguments and FOV store absent or ambiguous");
    layout.slot=image.Read<std::uint32_t>(out.kViewInfoCallerRva-4);
    Require(layout.slot>=0x100 && layout.slot<0x2000 && layout.slot%8==0,"invalid camera virtual slot");
    auto exec=Native(image,"GetPlayerViewPoint",refs);bool slotMatches=false;
    for(auto part:image.Parts(exec))for(auto at:image.Find(part,{0x48,0x89,0x7b,0x20,0xe8,-1,-1,-1,-1})) {
        auto bridge=image.Relative(at+5);
        if(image.Function(bridge).begin!=bridge)continue;
        for(auto body:image.Parts(bridge))for(auto call:image.Find(body,{0x4c,0x8b,0x08,0x4c,0x8b,0xc3,
            0x48,0x8b,0xd7,0x48,0x8b,0xc8,0x41,0xff,0x91,-1,-1,-1,-1}))
            slotMatches|=image.Read<std::uint32_t>(call+15)==layout.slot;
    }
    Require(slotMatches,"view native and render slots disagree");
    out.kKnownCallerRvas[0]=out.kViewInfoCallerRva;out.kDefaultInjectMode=1;out.kViewInfoRotationOffset=12;out.kViewInfoFovOffset=24;
    const auto trace=Native(image,"SphereTraceSingle_NEW",refs);
    std::set<std::uint32_t> traces;bool hitSize=false,radius=false,args=false,outputs=false,array=false;
    for(auto part:image.Parts(trace)){
        for(auto at:image.Find(part,{0x33,0xd2,0x48,0x8d,0x4d,-1,0x41,0xb8,-1,-1,-1,-1,0xe8}))
            hitSize|=image.Read<std::uint32_t>(at+8)==layout.hitSize;
        radius|=!image.Find(part,{0xf3,0x0f,0x10,0x5c,0x24,-1,0x88,0x54,0x24,0x48}).empty();
        args|=!image.Find(part,{0x44,0x88,0x7c,0x24,0x28,0x89,0x4c,0x24,0x20,0x48,0x8b,0x4c,0x24,-1,
            0x48,0x89,0x7b,0x20,0xf2,0x0f,0x11,0x45,-1,0xe8}).empty();
        outputs|=!image.Find(part,{0x48,0x89,0x74,0x24,0x40}).empty();
        array|=!image.Find(part,{0x89,0x44,0x24,0x38,0x4c,0x89,0x74,0x24,0x30}).empty();
        for(auto at:image.Find(part,{0xe8,-1,-1,-1,-1,0x48,0x8b,0x4c,0x24,-1,0x41,0x88,0x04,0x24})){
            auto target=image.Relative(at+1);if(image.Function(target).begin==target)traces.insert(target);
        }
    }
    Require(hitSize && radius && args && outputs && array && layout.hitSize<=256,"trace result size or native argument ABI not established");
    out.kSphereTraceSingleRva=Unique(traces,"native sphere trace target absent or ambiguous");
    out.kHitResultSize=layout.hitSize;out.kHitResultBlockingHitOffset=0;out.kHitResultLocationOffset=12;
}
}
bool DiscoverOffsets(ImageView view,OffsetTable& out,DiscoveryLayout& layout,std::string& reason){
    out={};layout={};reason.clear();try{Resolve(Image(view),out,layout);return true;}
    catch(const Rejected& e){out={};layout={};reason=e.what();return false;}
}
}
