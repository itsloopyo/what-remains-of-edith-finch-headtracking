#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

#include <cameraunlock/memory/pe_fingerprint.h>

// One BuildProfile describes a single shipped build of What Remains of Edith
// Finch: the PE-header fingerprint that uniquely identifies it, plus every
// per-build RVA / field offset the camera hook needs. The registry holds one
// profile per supported build; at startup the mod fingerprints the live module
// and selects the matching profile. No match leaves the mod fully dormant (no
// hooks installed, game runs vanilla) - see AGENTS.md "Maintain compatibility
// across new patches": never edit an existing profile's RVAs in place, ADD a
// new one.
//
// Edith Finch is UE 4.x (2017 build). UE4 predates Large World Coordinates, so
// FVector / FRotator are 3-float structs (12 bytes each), NOT the 3-double
// FVector3d / FRotator3d of UE5. The hook reads/writes the GetPlayerViewPoint
// out-params as floats - see ue4_types.h.
//
// This is a first-person narrative walking-sim: no weapons, no crosshair, no
// HUD overlay, so the mod only needs to inject the head pose into the
// render-path view and leave every other GetPlayerViewPoint caller clean (the
// interaction-trace / audio-listener decoupling).

namespace finch_ht
{
    // PE-header build fingerprint (TimeDateStamp + SizeOfImage + CheckSum);
    // the shared type keeps reading/matching/classification in core.
    using PeFingerprint = ::cameraunlock::memory::PeFingerprint;

    // Call-site slots a profile can pin. The inject-mode numbering is derived
    // from this (see view_injection.h), so widening the table widens the modes
    // the dev hotkeys cycle through with no other edit.
    inline constexpr std::size_t kMaxKnownCallers = 16;
    using CallerRvaTable = std::array<std::uintptr_t, kMaxKnownCallers>;

    // Bytes compared against the hook target to prove SteamStub has decrypted
    // it - see kGetPlayerViewPointPrologue below.
    inline constexpr std::size_t kPrologueBytes = 16;
    using PrologueBytes = std::array<std::uint8_t, kPrologueBytes>;

    struct OffsetTable
    {
        // Hook target: APlayerController::GetPlayerViewPoint. RVA from the
        // module base. Zero = profile incomplete (mod stays dormant).
        std::uintptr_t kGetPlayerViewPointRva;

        // First bytes of the DECRYPTED GetPlayerViewPoint. FinchGame.exe ships
        // SteamStub-packed: `.text` is ciphertext on disk and the stub decrypts
        // it in-place at the entry point, which races our bootstrap thread.
        // Hooking before the stub finishes makes MinHook build its trampoline
        // out of ciphertext, and the first call executes that garbage. The
        // bootstrap waits for these bytes to appear before touching the target.
        PrologueBytes kGetPlayerViewPointPrologue;

        // Return-address RVAs of the distinct GetPlayerViewPoint call sites.
        // Head tracking is injected ONLY for callers flagged here per the
        // active inject mode; every other caller reads the clean (mouse/pad)
        // rotation. That per-caller gate IS the look/aim decoupling.
        // 0-valued trailing entries are unused padding.
        CallerRvaTable kKnownCallerRvas;

        // Default inject mode at startup. 0 = all callers (diagnostic only),
        // 1..kMaxKnownCallers = inject only for kKnownCallerRvas[mode-1] (the
        // render-path caller / FMinimalViewInfo builder), and one past that =
        // none. Ctrl+Shift+U / J cycle this live so the render caller can be
        // re-confirmed in game after a patch without a rebuild.
        int kDefaultInjectMode;

        // APlayerController::bShowMouseCursor bitfield, for the InGameplay
        // gate (cursor visible == menu/cutscene -> suppress tracking). Both
        // zero = gate disabled (always treat as gameplay).
        std::size_t   kShowMouseCursorOffset;
        std::uint32_t kShowMouseCursorMask;

        // The render-path caller passes its two out-params as fields of ONE
        // FMinimalViewInfo - &OutViewInfo.Location and &OutViewInfo.Rotation -
        // and has already filled OutViewInfo.FOV from the camera manager by the
        // time GetPlayerViewPoint runs. So the hook reaches the view's field of
        // view through its own out-params, with no extra pointer chain: read it
        // at kViewInfoFovOffset, and write there to change what this frame
        // renders with.
        //
        // Pinned separately from kKnownCallerRvas[0] even though it is the same
        // call site: the inject mode is cycled live for diagnostics, and a stray
        // FOV write through a caller that passes two unrelated stack locals
        // would corrupt its frame. kViewInfoRotationOffset is the check that the
        // two out-params really are one struct. Zero = FOV unavailable on this
        // build (read and override both stay off).
        std::uintptr_t kViewInfoCallerRva;
        std::size_t    kViewInfoRotationOffset;
        std::size_t    kViewInfoFovOffset;
    };

    struct BuildProfile
    {
        const char*   Name;
        PeFingerprint Fingerprint;
        OffsetTable   Offsets;
    };
}
