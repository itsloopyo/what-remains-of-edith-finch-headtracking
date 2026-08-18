#include "fov_override.h"

#include <atomic>
#include <cmath>

#include "builds/build_registry.h"
#include "logging.h"
#include "runtime_state.h"
#include "view_injection.h"

namespace finch_ht
{
    namespace
    {
        // A game FOV this far from the last one is a camera change rather than a
        // blend step, which is what the log is there to record.
        constexpr float kFovChangeLogThreshold = 0.5f;

        // Non-null only for the one caller whose out-params are proven to be
        // fields of the same FMinimalViewInfo.
        float* ViewInfoFovPtr(std::uintptr_t retRva, FVector4f* outLocation,
                              const FRotator4f* outRotation)
        {
            const OffsetTable& offsets = Offsets();
            if (offsets.kViewInfoCallerRva == 0 || retRva != offsets.kViewInfoCallerRva)
                return nullptr;

            auto* base = reinterpret_cast<char*>(outLocation);
            if (reinterpret_cast<const char*>(outRotation) != base + offsets.kViewInfoRotationOffset) {
                static std::atomic<bool> s_warned{false};
                if (!s_warned.exchange(true, std::memory_order_relaxed))
                    Log::Line("FOV: caller 0x%08llx does not pass one FMinimalViewInfo "
                              "(rotation is %+lld bytes from location, expected +%zu) - FOV "
                              "read and override disabled for profile %s.",
                        static_cast<unsigned long long>(retRva),
                        static_cast<long long>(reinterpret_cast<const char*>(outRotation) - base),
                        offsets.kViewInfoRotationOffset, builds::ActiveProfile().Name);
                return nullptr;
            }
            return reinterpret_cast<float*>(base + offsets.kViewInfoFovOffset);
        }
    }

    void ApplyFovOverride(std::uintptr_t retRva, FVector4f* outLocation,
                          const FRotator4f* outRotation)
    {
        float* fov = ViewInfoFovPtr(retRva, outLocation, outRotation);
        if (fov == nullptr) return;

        const float gameFov = *fov;
        if (gameFov <= 0.0f) return;

        // Logged on change, not sampled: the game authors FOV per camera and
        // blends between them, and that is the record of which values a
        // playthrough actually asks for.
        const float previous = Runtime().gameFov.exchange(gameFov, std::memory_order_relaxed);
        if (previous != 0.0f && std::fabs(gameFov - previous) >= kFovChangeLogThreshold)
            Log::Line("fov: game changed %.2f -> %.2f", previous, gameFov);

        const float offset = Runtime().fovOffset.load(std::memory_order_relaxed);
        if (offset != 0.0f)
            *fov = ClampFov(gameFov + offset);
    }
}
