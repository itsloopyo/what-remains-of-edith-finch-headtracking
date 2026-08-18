#pragma once

#include <array>
#include <cstdint>

#include <cameraunlock/unreal/ue_math.h>

#include "builds/build_profile.h"
#include "ue4_types.h"

// What the hook decides and what it writes, with no state of its own: which
// GetPlayerViewPoint caller may be injected, what the tracked rotation and the
// world-space position offset come out as, and which views are held off.
// Everything here is a pure function of its arguments so it can be exercised
// without a game process - see tests/view_injection_tests.cpp.

namespace finch_ht
{
    namespace ue = ::cameraunlock::unreal;

    // ---- inject-mode caller gate (the look/aim decoupling) ---------------
    // 0                        = all callers (entangles aim with view - diagnostic only)
    // 1..kMaxKnownCallers      = inject only for kKnownCallerRvas[mode-1]
    // kInjectModeNone          = none (tracking disabled at the hook)
    inline constexpr int kInjectModeAllCallers = 0;
    inline constexpr int kInjectModeFirstCaller = 1;
    inline constexpr int kInjectModeLastCaller = static_cast<int>(kMaxKnownCallers);
    inline constexpr int kInjectModeNone = kInjectModeLastCaller + 1;
    inline constexpr int kInjectModeCount = kInjectModeNone + 1;

    // 0 for a mode that pins no single caller (all-callers, none, out of range).
    inline std::uintptr_t CallerRvaForMode(int mode, const CallerRvaTable& callers)
    {
        if (mode < kInjectModeFirstCaller || mode > kInjectModeLastCaller) return 0;
        return callers[static_cast<std::size_t>(mode - kInjectModeFirstCaller)];
    }

    inline bool ShouldInjectForCaller(std::uintptr_t retRva, int mode,
                                      const CallerRvaTable& callers)
    {
        if (mode == kInjectModeAllCallers) return true;
        const std::uintptr_t rva = CallerRvaForMode(mode, callers);
        return rva != 0 && retRva == rva;
    }

    // Wraps at both ends so the dev hotkeys cycle the whole range in either
    // direction.
    inline int CycleInjectMode(int mode, int direction)
    {
        return (mode + direction + kInjectModeCount) % kInjectModeCount;
    }

    // ---- suppressed views ------------------------------------------------
    // Two chapters put the player camera on a mount pointing dead down, and in
    // both of them that camera is not the thing the player is looking at:
    //
    //   Lewis' cannery chapter (pitch -90.00) hands it to the daydream's top-down
    //   2D map, while the cannery fills the screen.
    //   Barbara's comic chapter (pitch -89.50) parks it above the open comic book;
    //   the panels are masks cut through the page onto scenes behind it, and the
    //   character is moved through those scenes rather than followed by a camera.
    //
    // Neither chapter exposes the view the player cares about. A census of every
    // GetPlayerViewPoint call site in each one returns the pinned camera and
    // nothing else - 165 of 180 samples across Barbara's whole comic, every bucket
    // across Lewis' - so there is no second camera to switch to. Driving the pinned
    // one instead swings the daydream map or the whole comic book around, which is
    // worse than leaving it be.
    //
    // So: hold still on any view pinned this far past the horizon. The player's own
    // look tops out around 78 degrees, well clear of the threshold, and the gate
    // releases itself as soon as the game hands back a camera with a horizon.
    inline constexpr float kPinnedDownPitch = 89.0f;

    inline bool IsPinnedDownView(float pitch)
    {
        return pitch <= -kPinnedDownPitch || pitch >= kPinnedDownPitch;
    }

    // ---- field of view ---------------------------------------------------
    // Past these the projection matrix degenerates, so they bound the user's
    // offset rather than trusting an INI to stay sane.
    inline constexpr float kMinFov = 10.0f;
    inline constexpr float kMaxFov = 170.0f;

    inline float ClampFov(float fov)
    {
        return fov < kMinFov ? kMinFov : (fov > kMaxFov ? kMaxFov : fov);
    }

    // ---- pose composition ------------------------------------------------
    inline ue::FQuat4d ViewQuat(const FRotator4f& rotation)
    {
        return ue::QuatFromEulerDeg(rotation.Pitch, rotation.Yaw, rotation.Roll);
    }

    // baseQ must be ViewQuat(clean); it is passed in because the caller also
    // needs it for the position offset and the conversion is not free.
    inline FRotator4f ComposeTrackedRotation(const FRotator4f& clean, const ue::FQuat4d& baseQ,
                                             float yaw, float pitch, float roll,
                                             bool worldSpaceYaw)
    {
        if (worldSpaceYaw) {
            // Horizon-locked: FRotator addition about the world up-axis.
            return FRotator4f{clean.Pitch + pitch, clean.Yaw + yaw, clean.Roll - roll};
        }
        // Camera-local: quaternion post-multiply, which leans on pitched turns.
        const ue::FQuat4d headLocalQ = ue::QuatFromEulerDeg(
            static_cast<double>(pitch), static_cast<double>(yaw), -static_cast<double>(roll));
        const ue::FRotator composed = ue::QuatToRotator(ue::QuatMul(baseQ, headLocalQ));
        return FRotator4f{
            static_cast<float>(composed.Pitch),
            static_cast<float>(composed.Yaw),
            static_cast<float>(composed.Roll),
        };
    }

    // Build a world-space camera-location offset (UE units = cm) from the
    // session's processed offset (meters) in the CLEAN camera frame, so head
    // sway follows the body, not the head-rotated view.
    inline ue::FVector PositionOffsetUE(const ue::FQuat4d& baseQ, float offX, float offY, float offZ)
    {
        const ue::FVector camFwd   = ue::QuatRotateVec(baseQ, ue::FVector{1.0, 0.0, 0.0});
        const ue::FVector camRight = ue::QuatRotateVec(baseQ, ue::FVector{0.0, 1.0, 0.0});
        const ue::FVector camUp    = ue::QuatRotateVec(baseQ, ue::FVector{0.0, 0.0, 1.0});
        constexpr double kMetersToUE = 100.0;
        // Sign flips are the core-to-engine convention boundary, not user inversion:
        // the processor's forward lean is NEGATIVE z (that's the axis carrying the
        // generous limit_z), and its sway runs opposite UE's camera-right.
        const double s = -static_cast<double>(offZ) * kMetersToUE;  // surge -> forward
        const double r = -static_cast<double>(offX) * kMetersToUE;  // sway  -> right
        const double u =  static_cast<double>(offY) * kMetersToUE;  // heave -> up
        return ue::FVector{
            camFwd.X * s + camRight.X * r + camUp.X * u,
            camFwd.Y * s + camRight.Y * r + camUp.Y * u,
            camFwd.Z * s + camRight.Z * r + camUp.Z * u,
        };
    }
}
