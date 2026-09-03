#include "lean_trace.h"

#include <atomic>
#include <cmath>
#include <cstring>

#include <cameraunlock/unreal/ue_runtime.h>

#include "builds/build_registry.h"
#include "logging.h"
#include "ue4_types.h"

namespace finch_ht::lean_trace {

namespace {

namespace ue = ::cameraunlock::unreal;
using cameraunlock::camera::LeanObstruction;
using cameraunlock::math::Vec3;

// The shipping signature, read off the exec thunk rather than the UE4 headers -
// see the comment on kSphereTraceSingleRva. The colour and duration arguments
// the headers carry are compiled out of a shipping build.
using SphereTraceSingle_t = bool(__fastcall*)(void* worldContextObject,
                                              const FVector4f* start,
                                              const FVector4f* end,
                                              float radius,
                                              int traceChannel,
                                              bool traceComplex,
                                              const void* actorsToIgnore,
                                              int drawDebugType,
                                              void* outHit,
                                              bool ignoreSelf);

// UE4's TArray is {allocator data pointer, int32 Num, int32 Max}. The sweep
// only reads it, and ours is always empty, so an empty one satisfies the
// parameter with no allocator to imitate.
struct ActorArray {
    void* data;
    std::int32_t num;
    std::int32_t max;
};

// Larger than any FHitResult the profile can name, so the engine's write always
// lands inside our own frame. The profile's size is what gets zeroed, and it is
// checked against this before the first call.
constexpr std::size_t kHitResultCapacity = 256;

SphereTraceSingle_t g_sphereTrace = nullptr;
std::atomic<float> g_radius{10.0f};
std::atomic<int> g_channel{0};
std::atomic<bool> g_resolved{false};
std::atomic<bool> g_failed{false};

}  // namespace

void SetRadius(float centimetres) {
    g_radius.store(centimetres, std::memory_order_relaxed);
}

void SetChannel(int traceTypeQuery) {
    g_channel.store(traceTypeQuery, std::memory_order_relaxed);
}

bool Ready() {
    if (g_resolved.load(std::memory_order_relaxed)) return true;
    if (g_failed.load(std::memory_order_relaxed)) return false;

    const auto& offsets = Offsets();
    if (offsets.kSphereTraceSingleRva == 0) {
        Log::Line("lean-trace: this build profile carries no SphereTraceSingle - collision clamp unavailable");
        g_failed.store(true, std::memory_order_relaxed);
        return false;
    }
    if (offsets.kHitResultSize == 0 || offsets.kHitResultSize > kHitResultCapacity) {
        Log::Line("lean-trace: FHitResult size 0x%zx does not fit our buffer - collision clamp unavailable",
                  offsets.kHitResultSize);
        g_failed.store(true, std::memory_order_relaxed);
        return false;
    }
    if (offsets.kHitResultLocationOffset + sizeof(FVector4f) > offsets.kHitResultSize) {
        Log::Line("lean-trace: FHitResult::Location at 0x%zx runs past the struct - collision clamp unavailable",
                  offsets.kHitResultLocationOffset);
        g_failed.store(true, std::memory_order_relaxed);
        return false;
    }

    g_sphereTrace = reinterpret_cast<SphereTraceSingle_t>(
        ue::ModuleBase() + offsets.kSphereTraceSingleRva);
    g_resolved.store(true, std::memory_order_relaxed);
    Log::Line("lean-trace: SphereTraceSingle at RVA 0x%08llx, FHitResult 0x%zx bytes",
              static_cast<unsigned long long>(offsets.kSphereTraceSingleRva), offsets.kHitResultSize);
    return true;
}

LeanObstruction Query(void* context, const Vec3& start, const Vec3& direction, float maxDistance) {
    LeanObstruction out;

    if (!Ready() || context == nullptr) return out;

    const FVector4f from{start.x, start.y, start.z};
    const FVector4f to{start.x + direction.x * maxDistance,
                       start.y + direction.y * maxDistance,
                       start.z + direction.z * maxDistance};

    const ActorArray ignoreList{nullptr, 0, 0};

    const auto& offsets = Offsets();
    alignas(16) unsigned char hit[kHitResultCapacity];
    std::memset(hit, 0, offsets.kHitResultSize);

    // The return value is discarded on purpose: the engine fills bBlockingHit
    // whatever it returns, and reading the struct keeps this independent of
    // how the shipping build passes a bool back.
    g_sphereTrace(context, &from, &to, g_radius.load(std::memory_order_relaxed),
                  g_channel.load(std::memory_order_relaxed),
                  /*traceComplex*/ false, &ignoreList, /*drawDebugType*/ 0, hit,
                  /*ignoreSelf*/ false);

    out.queried = true;
    out.blocked = (hit[offsets.kHitResultBlockingHitOffset] & 1u) != 0;
    if (out.blocked) {
        FVector4f location{};
        std::memcpy(&location, hit + offsets.kHitResultLocationOffset, sizeof(location));
        const float dx = location.X - start.x;
        const float dy = location.Y - start.y;
        const float dz = location.Z - start.z;
        out.distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    }
    return out;
}

}  // namespace finch_ht::lean_trace
