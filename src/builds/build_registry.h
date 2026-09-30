#pragma once
#include <windows.h>
#include "build_profile.h"

namespace finch_ht
{
    namespace builds
    {
        enum class MatchResult
        {
            Matched,     // Active profile set; mod can run.
            ReadFailed,  // Could not read the PE header.
            DiscoveryFailed,
        };

        MatchResult SelectProfile(HMODULE host, bool waitForStartup = true);
        const BuildProfile& ActiveProfile();
        bool                HasActiveProfile();
    }

    // Accessor for the active profile's offset table. Must run after
    // SelectProfile() returns Matched.
    inline const OffsetTable& Offsets()
    {
        return builds::ActiveProfile().Offsets;
    }
}
