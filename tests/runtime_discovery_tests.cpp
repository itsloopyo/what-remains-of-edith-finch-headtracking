// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#include "builds/runtime_discovery.h"
#ifdef _WIN32
#include "builds/build_registry.h"
#endif
#include <algorithm>
#include <cstring>
#include <vector>
#include <initializer_list>
#include <iostream>
int checks=0,failures=0;
#define CHECK(x) do { ++checks; if(!(x)){++failures;std::cerr<<__LINE__<<": "<<#x<<"\n";} } while(false)
namespace {

struct Fixture {
    std::uint32_t shift;
    std::uintptr_t base;
    std::vector<std::uint8_t> bytes;

    template<class T> void Write(std::uint32_t at, T value) {
        std::memcpy(bytes.data() + at, &value, sizeof(value));
    }

    void Code(std::uint32_t at, std::initializer_list<std::uint8_t> code) {
        std::copy(code.begin(), code.end(), bytes.begin() + at + shift);
    }

    void String(std::uint32_t at, const char* value, bool wide = false) {
        do {
            bytes[at++ + shift] = static_cast<std::uint8_t>(*value);
            if (wide) bytes[at++ + shift] = 0;
        } while (*value++);
    }

    void Pointer(std::uint32_t at, std::uint32_t target) {
        Write<std::uint64_t>(at + shift, base + shift + target);
    }

    void Relative(std::uint32_t at, std::uint32_t target) {
        Write<std::int32_t>(at + shift, static_cast<std::int32_t>(target) - static_cast<std::int32_t>(at) - 4);
    }

    Fixture(std::uint32_t displacement = 0, std::uintptr_t imageBase = 0x140000000)
        : shift(displacement), base(imageBase), bytes(0x30000 + shift) {
        if (!base) base=reinterpret_cast<std::uintptr_t>(bytes.data());
        Write<std::uint16_t>(0, 0x5a4d);
        Write<std::uint32_t>(0x3c, 0x80);
        Write<std::uint32_t>(0x80, 0x4550);
        Write<std::uint16_t>(0x84, 0x8664);
        Write<std::uint16_t>(0x86, 4);
        Write<std::uint16_t>(0x94, 240);
        Write<std::uint16_t>(0x98, 0x20b);
        Write<std::uint32_t>(0x98 + 56, static_cast<std::uint32_t>(bytes.size()));
        struct Section { const char* name; std::uint32_t rva, size, flags; };
        const Section sections[] = {
            {".text", 0x1000, 0x2000, 0x60000000},
            {".rdata", 0x4000, 0x3000, 0x40000000},
            {".data", 0x8000, 0x20000, 0xc0000000},
            {".pdata", 0x29000, 0x1000, 0x40000000},
        };
        std::uint32_t header = 0x188;
        for (const auto& section : sections) {
            std::memcpy(bytes.data() + header, section.name, std::strlen(section.name));
            Write<std::uint32_t>(header + 8, section.size);
            Write<std::uint32_t>(header + 12, section.rva + shift);
            Write<std::uint32_t>(header + 36, section.flags);
            header += 40;
        }
        const std::uint32_t functions[][2] = {
            {0x1100,0x1180},{0x1200,0x1280},{0x1300,0x1400},{0x1400,0x1500},
            {0x1500,0x1580},{0x1600,0x1700},{0x1800,0x1880},{0x1900,0x1980},
            {0x1a00,0x1a80},{0x1b00,0x1c00},{0x1d00,0x1d80},{0x1e00,0x1e80},
        };
        std::uint32_t record = 0x29000 + shift;
        for (const auto& fn : functions) {
            Write<std::uint32_t>(record, fn[0] + shift);
            Write<std::uint32_t>(record + 4, fn[1] + shift);
            Write<std::uint32_t>(record + 8, 0x6800 + shift);
            record += 12;
        }
        Code(0x6800, {1,0,0,0});
        Write<std::uint32_t>(0x29000 + 12 + 8 + shift, 0x6810 + shift);
        Code(0x6810, {0x21,0,0,0});
        Write<std::uint32_t>(0x6814 + shift, 0x1100 + shift);
        Write<std::uint32_t>(0x98 + 112 + 24, 0x29000 + shift);
        Write<std::uint32_t>(0x98 + 112 + 28, record - 0x29000 - shift);
        unsigned n=0;
        for(auto name:{"None","ByteProperty","IntProperty","BoolProperty","FloatProperty","ObjectProperty","NameProperty"}){
            String(0x4000+n*40,name,true);Code(0x1600+n*16,{0x48,0x8d,0x15,0,0,0,0});Relative(0x1603+n*16,0x4000+n*40);++n;
        }
        Code(0x1680,{0x48,0x8b,0x3d,0,0,0,0});Relative(0x1683,0x8000);
        String(0x4400,"MinimalViewInfo",true);String(0x4500,"HitResult",true);
        for(unsigned i=0;i<2;++i){
            auto at=0x1400+i*0x100;
            Code(at,{0x41,0xb9,0,0,0,0,0x4c,0x8d,0x05,0,0,0,0,0x48,0x8d,0x0d,0,0,0,0,
                0xe8,0,0,0,0,0x48,0x8b,0x5c,0x24,0x30,0x48,0x89,0x05,0,0,0,0});
            Write<std::uint32_t>(at+2+shift,i?0x80:0x3d8);Relative(at+9,0x4400+i*0x100);
            Relative(at+16,0x1d00);Relative(at+21,0x1d00);Relative(at+33,0x8010+i*8);
        }
        Code(0x1100,{0xf2,0x0f,0x10,0x81,0,3,0,0,0xf2,0x0f,0x11,0x06,0x8b,0x81,8,3,0,0,0x89,0x46,8,
            0xf2,0x0f,0x10,0x81,12,3,0,0,0xf2,0x0f,0x11,0x07,0x8b,0x81,20,3,0,0,0x89,0x47,8});
        Code(0x1300,{0x48,0x8b,0x88,0xd8,3,0,0,0x48,0x8b,0x01,0xff,0x90,0x90,5,0,0,
            0xf3,0x0f,0x11,0x07,0x48,0x8b,0x4e,0x30,0x48,0x8b,0x01,0x4c,0x8d,0x43,0x0c,
            0x48,0x8b,0xd3,0xff,0x90,0x18,6,0,0});
        Code(0x1340,{0x48,0x8d,0x7a,0x18,0x48,0x8b,0xda});
        String(0x4600,"GetPlayerViewPoint");String(0x4640,"SphereTraceSingle_NEW");
        for(unsigned i=0;i<2;++i){auto at=0x1800+i*32;
            Code(at,{0x4c,0x8d,0x05,0,0,0,0,0x48,0x8d,0x15,0,0,0,0,0xe8});
            Relative(at+3,i?0x1b00:0x1900);Relative(at+10,0x4600+i*0x40);
        }
        Code(0x1900,{0x48,0x89,0x7b,0x20,0xe8,0,0,0,0});Relative(0x1905,0x1a00);
        Code(0x1a00,{0x4c,0x8b,0x08,0x4c,0x8b,0xc3,0x48,0x8b,0xd7,0x48,0x8b,0xc8,0x41,0xff,0x91,0x18,6,0,0});
        Code(0x1b00,{0x33,0xd2,0x48,0x8d,0x4d,0xb0,0x41,0xb8,0x80,0,0,0,0xe8});
        Code(0x1b20,{0xf3,0x0f,0x10,0x5c,0x24,0x54,0x88,0x54,0x24,0x48});
        Code(0x1b40,{0x44,0x88,0x7c,0x24,0x28,0x89,0x4c,0x24,0x20,0x48,0x8b,0x4c,0x24,0x58,
            0x48,0x89,0x7b,0x20,0xf2,0x0f,0x11,0x45,0xa0,0xe8,0,0,0,0,0x48,0x8b,0x4c,0x24,0x60,0x41,0x88,0x04,0x24});
        Relative(0x1b58,0x1d00);
        Code(0x1b80,{0x48,0x89,0x74,0x24,0x40,0x89,0x44,0x24,0x38,0x4c,0x89,0x74,0x24,0x30});
    }
    finch_ht::builds::ImageView View(){return {bytes.data(),bytes.size(),base};}
};
}
int main(){
    using namespace finch_ht;using namespace finch_ht::builds;
#ifdef _WIN32
    {
        Fixture f(0,0);
        CHECK(SelectProfile(reinterpret_cast<HMODULE>(f.bytes.data()),false)==MatchResult::Matched);
        CHECK(UsesRuntimeDiscovery());CHECK(RuntimeLayout().slot==0x618);
        f.bytes[0x1300]=0;
        CHECK(SelectProfile(reinterpret_cast<HMODULE>(f.bytes.data()),false)==MatchResult::DiscoveryFailed);
        CHECK(!HasActiveProfile());CHECK(!UsesRuntimeDiscovery());
    }
#endif
    for(auto shift:{0u,0x1000u}){
        Fixture f(shift);OffsetTable o{};DiscoveryLayout l{};std::string reason;
        CHECK(DiscoverOffsets(f.View(),o,l,reason));if(!reason.empty())std::cerr<<reason<<"\n";
        CHECK(o.kGetPlayerViewPointRva==0x1100+shift);CHECK(o.kViewInfoCallerRva==0x1328+shift);
        CHECK(o.kSphereTraceSingleRva==0x1d00+shift);CHECK(l.names==0x8000+shift);
        CHECK(l.viewGlobal==0x8010+shift);CHECK(l.hitGlobal==0x8018+shift);
        CHECK(l.slot==0x618);CHECK(l.cache==0x300);CHECK(l.manager==0x3d8);CHECK(o.kHitResultSize==0x80);
    }
    for(auto at:{0u,0x84u,0x98u,0x4000u,0x1683u,0x4400u,0x4500u,0x141eu,0x1502u,
                 0x110eu,0x1119u,0x1123u,0x1312u,0x1343u,0x4600u,0x1900u,0x1a0fu,
                 0x4640u,0x1b08u,0x1b23u,0x1b44u,0x1b61u,0x1b84u,0x1b8du}){
        Fixture f;f.bytes[at]^=1;OffsetTable o{};DiscoveryLayout l{};std::string reason;
        CHECK(!DiscoverOffsets(f.View(),o,l,reason));CHECK(o.kGetPlayerViewPointRva==0 && l.names==0);
    }
    {
        Fixture f;std::copy_n(f.bytes.begin()+0x1100,42,f.bytes.begin()+0x1e00);
        OffsetTable o{};DiscoveryLayout l{};std::string reason;CHECK(!DiscoverOffsets(f.View(),o,l,reason));
    }
    for(auto size:{0u,64u,0x100u,0x188u,0x29004u}){
        Fixture f;f.bytes.resize(size);OffsetTable o{};DiscoveryLayout l{};std::string reason;CHECK(!DiscoverOffsets(f.View(),o,l,reason));
    }
    std::cout<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;
}
