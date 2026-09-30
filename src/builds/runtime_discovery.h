// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#pragma once
#include "build_profile.h"
#include <string>
namespace finch_ht::builds {
struct ImageView { const std::uint8_t* data;std::size_t size;std::uintptr_t base; };
struct DiscoveryLayout {std::uint32_t names,viewGlobal,hitGlobal,viewSize,hitSize,slot,manager,cache;};
bool DiscoverOffsets(ImageView,OffsetTable&,DiscoveryLayout&,std::string&);
bool UsesRuntimeDiscovery();
const DiscoveryLayout& RuntimeLayout();
bool ValidateController(std::uintptr_t);
}
