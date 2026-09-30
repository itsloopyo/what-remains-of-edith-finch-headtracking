// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#include "build_registry.h"
#include "runtime_discovery.h"
#include "logging.h"
#include <vector>
namespace finch_ht::builds {
extern const BuildProfile kSteamProfile_20170621;
namespace {
const BuildProfile* active=nullptr;
BuildProfile discovered{};
DiscoveryLayout layout{};
}
MatchResult SelectProfile(HMODULE host,bool waitForStartup){
    active=nullptr;layout={};PeFingerprint running{};
    if(!cameraunlock::memory::ReadPeFingerprint(host,running))return MatchResult::ReadFailed;
    const bool known=running.Matches(kSteamProfile_20170621.Fingerprint);
    std::vector<std::uint8_t> bytes(running.SizeOfImage);
    const auto start=GetTickCount64();std::string reason;
    for(;;){
        SIZE_T copied=0;
        if(ReadProcessMemory(GetCurrentProcess(),host,bytes.data(),bytes.size(),&copied) && copied==bytes.size()){
            OffsetTable found{};DiscoveryLayout candidate{};
            if(DiscoverOffsets({bytes.data(),bytes.size(),reinterpret_cast<std::uintptr_t>(host)},found,candidate,reason)){
                if(found.kGetPlayerViewPointRva+kPrologueBytes>bytes.size())return MatchResult::DiscoveryFailed;
                auto hash=0xcbf29ce484222325ULL;
                for(std::size_t i=0;i<kPrologueBytes;++i){hash^=bytes[found.kGetPlayerViewPointRva+i];hash*=0x100000001b3ULL;}
                found.kGetPlayerViewPointPrologueHash=hash;
                if(known){
                    const auto& expected=kSteamProfile_20170621.Offsets;
                    if(found.kGetPlayerViewPointRva!=expected.kGetPlayerViewPointRva || hash!=expected.kGetPlayerViewPointPrologueHash ||
                       found.kKnownCallerRvas[0]!=expected.kKnownCallerRvas[0] || found.kViewInfoCallerRva!=expected.kViewInfoCallerRva ||
                       found.kViewInfoRotationOffset!=expected.kViewInfoRotationOffset || found.kViewInfoFovOffset!=expected.kViewInfoFovOffset ||
                       found.kSphereTraceSingleRva!=expected.kSphereTraceSingleRva || found.kHitResultSize!=expected.kHitResultSize ||
                       found.kHitResultBlockingHitOffset!=expected.kHitResultBlockingHitOffset || found.kHitResultLocationOffset!=expected.kHitResultLocationOffset){
                        Log::Line("discovery: values disagree with historical profile; staying dormant");return MatchResult::DiscoveryFailed;
                    }
                }
                discovered={"runtime-discovered",running,found};layout=candidate;active=&discovered;
                Log::Line("discovery: view=0x%llx render=0x%llx trace=0x%llx names=0x%x slot=0x%x after %llu ms; awaiting live validation",
                    static_cast<unsigned long long>(found.kGetPlayerViewPointRva),static_cast<unsigned long long>(found.kViewInfoCallerRva),
                    static_cast<unsigned long long>(found.kSphereTraceSingleRva),layout.names,layout.slot,GetTickCount64()-start);
                return MatchResult::Matched;
            }
        }else{reason="executable snapshot not yet readable";}
        if(!waitForStartup || GetTickCount64()-start>=60000)break;
        Sleep(250);
    }
    Log::Line("discovery: %s",reason.c_str());
    if(known){active=&kSteamProfile_20170621;Log::Line("build-check: using exact historical profile %s",active->Name);return MatchResult::Matched;}
    return MatchResult::DiscoveryFailed;
}
const BuildProfile& ActiveProfile(){return *active;}
bool HasActiveProfile(){return active!=nullptr;}
bool UsesRuntimeDiscovery(){return active==&discovered;}
const DiscoveryLayout& RuntimeLayout(){return layout;}
}
