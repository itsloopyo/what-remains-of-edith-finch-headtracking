#include "build_profile.h"


namespace finch_ht::builds
{
    extern const BuildProfile kSteamProfile_20170621;

    const BuildProfile kSteamProfile_20170621 = {
        /* Name        */ "steam-win64-20170621",
        /* Fingerprint */ { 0x5949BA9Du, 0x02DBA000u, 0x02C3913Fu },
        /* Offsets     */ {
            /* kGetPlayerViewPointRva */ 0x01112680ULL,
            /* kGetPlayerViewPointPrologueHash */ 0xA4E5BECDF1756227ULL,
            /* kKnownCallerRvas */ {{
                0x010cfeefULL, 0x0104396aULL, 0x0110d073ULL, 0x010d37b7ULL,
                0x01242b70ULL, 0x00183be3ULL, 0x0ULL, 0x0ULL,
                0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL,
            }},
            /* kDefaultInjectMode     */ 1,
            /* kShowMouseCursorOffset */ 0x0,
            /* kShowMouseCursorMask   */ 0x1u,
            /* kViewInfoCallerRva      */ 0x010cfeefULL,
            /* kViewInfoRotationOffset */ 0x0C,
            /* kViewInfoFovOffset      */ 0x18,
            /* kSphereTraceSingleRva */ 0x010c10f0ULL,
            /* kHitResultSize              */ 0x80,
            /* kHitResultBlockingHitOffset */ 0x00,
            /* kHitResultLocationOffset    */ 0x0C,
        },
    };
}
