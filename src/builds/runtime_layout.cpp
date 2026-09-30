// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#include "runtime_discovery.h"
#include "build_registry.h"
#include "logging.h"
#include <windows.h>
#include <cameraunlock/unreal/ue_runtime.h>
#include <atomic>
#include <set>
#include <stdexcept>
#include <cstring>
namespace finch_ht::builds {
namespace {
namespace ue=::cameraunlock::unreal;
class Unavailable: public std::runtime_error { public: using std::runtime_error::runtime_error; };
void Need(bool value,const char* why){if(!value)throw Unavailable(why);}
template<class T>T Read(std::uintptr_t at){T v{};Need(ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(at),&v,sizeof(v),nullptr),"live metadata is unreadable");return v;}
std::string Name(std::uint32_t id) {
    auto pool=Read<std::uintptr_t>(ue::ModuleBase()+RuntimeLayout().names);
    Need(pool!=0,"name table is not initialized");
    auto count=Read<std::uint32_t>(pool+0x400), chunks=Read<std::uint32_t>(pool+0x404);
    Need(count && count<=128*16384 && chunks && chunks<=128 && id<count && id/16384<chunks,"invalid name table bounds");
    auto chunk=Read<std::uintptr_t>(pool+(id/16384)*8);
    auto entry=Read<std::uintptr_t>(chunk+(id%16384)*8);
    auto header=Read<std::uint32_t>(entry);
    Need((header>>1)==id,"name entry index mismatch");
    std::string result;
    for(unsigned i=0;i<256;++i){
        auto ch=(header&1)?Read<std::uint16_t>(entry+16+i*2):Read<std::uint8_t>(entry+16+i);
        if(!ch)return result;
        Need(ch<128,"non-ASCII metadata name");result+=static_cast<char>(ch);
    }
    throw Unavailable("unterminated metadata name");
}
std::string ObjectName(std::uintptr_t p){Need(p!=0,"null metadata object");return Name(Read<std::uint32_t>(p+0x18));}
std::uintptr_t Class(std::uintptr_t p){return Read<std::uintptr_t>(p+0x10);}
bool NamedType(std::uintptr_t type,const char* name,const char* kind,const char* package) {
    return ObjectName(type)==name && ObjectName(Class(type))==kind &&
        ObjectName(Read<std::uintptr_t>(type+0x20))==package;
}
std::uintptr_t Owner(std::uintptr_t cls,const char* name) {
    std::set<std::uintptr_t> seen;
    for(unsigned i=0;cls && i<32;++i){
        Need(seen.insert(cls).second,"class inheritance cycle");
        if(NamedType(cls,name,"Class","/Script/Engine"))return cls;
        cls=Read<std::uintptr_t>(cls+0x30);
    }
    throw Unavailable("required Engine class missing");
}
struct Property { std::uint32_t offset,size; std::uintptr_t inner,address; };
Property Field(std::uintptr_t owner,const char* name,const char* kind,std::uint32_t width=0) {
    auto bound=Read<std::uint32_t>(owner+0x40);
    Need(bound && bound<0x100000,"invalid owner size");
    auto field=Read<std::uintptr_t>(owner+0x38);
    std::set<std::uintptr_t> seen;Property found{};unsigned count=0;
    for(unsigned i=0;field && i<4096;++i){
        Need(seen.insert(field).second,"property chain cycle");
        if(_stricmp(ObjectName(field).c_str(),name)==0){
            Need(ObjectName(Class(field))==kind,"property kind mismatch");
            Need(Read<std::uint32_t>(field+0x30)==1,"property array dimension mismatch");
            auto size=Read<std::uint32_t>(field+0x34), offset=Read<std::uint32_t>(field+0x4c);
            Need(size && (!width || size==width) && offset<bound && size<=bound-offset,"property width or bounds mismatch");
            found={offset,size,0,field};++count;
            if(std::strcmp(kind,"StructProperty")==0){
                found.inner=Read<std::uintptr_t>(field+0x70);
                Need(found.inner && Read<std::uint32_t>(found.inner+0x40)==size,"inner struct width mismatch");
            }
        }
        field=Read<std::uintptr_t>(field+0x28);
    }
    Need(!field && count==1,"required property absent or ambiguous");return found;
}
void Scalar(std::uintptr_t structure,const char* name,std::uint32_t offset) {
    Need(Field(structure,name,"FloatProperty",4).offset==offset,"camera scalar layout mismatch");
}
void ViewFields(std::uintptr_t view) {
    Need(NamedType(view,"MinimalViewInfo","ScriptStruct","/Script/Engine"),"unexpected camera view struct");
    auto loc=Field(view,"Location","StructProperty",12), rot=Field(view,"Rotation","StructProperty",12);
    Need(loc.offset==0 && rot.offset==12 && Field(view,"FOV","FloatProperty",4).offset==24,"camera view member layout mismatch");
    Need(NamedType(loc.inner,"Vector","ScriptStruct","/Script/CoreUObject") &&
         NamedType(rot.inner,"Rotator","ScriptStruct","/Script/CoreUObject"),"camera vector/rotator identity mismatch");
    Scalar(loc.inner,"X",0);Scalar(loc.inner,"Y",4);Scalar(loc.inner,"Z",8);
    Scalar(rot.inner,"Pitch",0);Scalar(rot.inner,"Yaw",4);Scalar(rot.inner,"Roll",8);
}
void ValidateLayout(std::uintptr_t controller) {
    const auto& layout=RuntimeLayout();
    auto pc=Owner(Class(controller),"PlayerController");
    auto manager=Field(pc,"PlayerCameraManager","ObjectProperty",8);
    Need(manager.offset==layout.manager,"camera manager field disagrees with render path");
    auto pcm=Read<std::uintptr_t>(controller+manager.offset);
    Need(pcm!=0,"camera manager not initialized");
    auto cache=Field(Owner(Class(pcm),"PlayerCameraManager"),"CameraCache","StructProperty");
    Need(NamedType(cache.inner,"CameraCacheEntry","ScriptStruct","/Script/Engine"),"camera cache type mismatch");
    auto pov=Field(cache.inner,"POV","StructProperty");
    auto view=Read<std::uintptr_t>(ue::ModuleBase()+layout.viewGlobal);
    Need(view==pov.inner && Read<std::uint32_t>(view+0x40)==layout.viewSize && cache.offset+pov.offset==layout.cache,"camera cache location mismatch");
    ViewFields(view);
    auto hit=Read<std::uintptr_t>(ue::ModuleBase()+layout.hitGlobal);
    Need(NamedType(hit,"HitResult","ScriptStruct","/Script/Engine") &&
         Read<std::uint32_t>(hit+0x40)==Offsets().kHitResultSize,"trace result type or size mismatch");
    auto block=Field(hit,"bBlockingHit","BoolProperty",1);
    Need(block.offset==Offsets().kHitResultBlockingHitOffset && Read<std::uint32_t>(block.address+0x70)==0x01010001,"blocking hit bit layout mismatch");
    auto loc=Field(hit,"Location","StructProperty",12);
    Need(loc.offset==Offsets().kHitResultLocationOffset && NamedType(loc.inner,"Vector_NetQuantize","ScriptStruct","/Script/Engine"),"trace location layout mismatch");
    auto vector=Field(view,"Location","StructProperty",12).inner;
    Need(Read<std::uintptr_t>(loc.inner+0x30)==vector,"trace location vector base mismatch");
}
}
bool ValidateController(std::uintptr_t controller){
    if(!UsesRuntimeDiscovery())return true;
    std::uintptr_t table=0,target=0;
    if(!ue::SafeReadPtr(controller,table) || !ue::SafeReadPtr(table+RuntimeLayout().slot,target) ||
       target!=ue::ModuleBase()+Offsets().kGetPlayerViewPointRva)return false;
    static std::atomic<bool> ready{false};if(ready.load(std::memory_order_acquire))return true;
    static std::uint64_t next=0,report=0;auto now=GetTickCount64();if(now<next)return false;next=now+1000;
    try{
        Need(Name(0)=="None","name table sentinel mismatch");ValidateLayout(controller);
        ready.store(true,std::memory_order_release);
        Log::Line("discovery: live 12-byte camera, controller dispatch and trace-result layouts validated");return true;
    }catch(const Unavailable& e){if(now>=report){report=now+5000;Log::Line("discovery: waiting for live layout validation: %s",e.what());}return false;}
}
}
