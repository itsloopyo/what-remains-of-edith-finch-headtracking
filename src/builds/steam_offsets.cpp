#include "build_profile.h"

// Steam Win64 build of What Remains of Edith Finch
// (FinchGame.exe, UE 4.16-era, PE build date 2017-06-21). RVAs derived in
// Ghidra via scripts/ghidra/*.java against the matching binary (.lab/ghidra
// project). Ghidra 12 dropped Jython, so the discovery scripts are Java.
//
// To add support for a new Steam build: do NOT edit kSteamProfile_<date> in
// place. Append a new `extern const BuildProfile kSteamProfile_YYYYMMDD = {...}`
// below, register it at the top of kKnownProfiles in build_registry.cpp, and
// keep older profiles forever (the PE fingerprint routes each user to theirs).

namespace finch_ht::builds
{
    extern const BuildProfile kSteamProfile_20170621;

    // ---- Steam Win64 build (PE TimeDateStamp 0x5949BA9D, 2017-06-21) ----
    const BuildProfile kSteamProfile_20170621 = {
        /* Name        */ "steam-win64-20170621",
        /* Fingerprint */ { 0x5949BA9Du, 0x02DBA000u, 0x02C3913Fu },
        /* Offsets     */ {
            // APlayerController::GetPlayerViewPoint @ RVA 0x01112680. The
            // shipping exe is SteamStub-packed (.text encrypted on disk), so
            // this was derived from a pe-sieve runtime dump of the DECRYPTED
            // module: GPV is the controller vtable[0x618] override. Confirmed
            // by decompile - reads this->PlayerCameraManager (+0x3D8), checks
            // the camera-cache timestamp (pcm+0x3b8 > 0), copies POV.Location
            // (pcm+0x3c0) and POV.Rotation (pcm+0x3cc) as 3 floats each (UE4 /
            // pre-LWC, 12-byte structs - see ue4_types.h FVector4f /
            // FRotator4f), else tail-falls to Super::GetPlayerViewPoint
            // (0x00ff2410). Signature (self, FVector* OutLoc, FRotator* OutRot).
            /* kGetPlayerViewPointRva */ 0x01112680ULL,
            // Decrypted prologue: mov [rsp+10],rbx / mov [rsp+18],rsi /
            // mov [rsp+20],rdi / push rbp / mov rbp,rsp. Read out of the
            // pe-sieve dump of the running (post-SteamStub) module.
            /* kGetPlayerViewPointPrologue */ {{
                0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74,
                0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20, 0x55,
            }},
            // GetPlayerViewPoint call sites, captured at runtime with inject
            // mode 0 (which logs every call CHAIN plus counts). Slot [0] is the
            // render path; the rest are kept so a single in-game session can
            // A/B them with Ctrl+Shift+U and so the next person can see what
            // was rejected and why.
            //
            //  [0] 0x010cfeef  ULocalPlayer::GetViewPoint. Identified by the
            //      FMinimalViewInfo it fills - Location +0x00, Rotation +0x0C,
            //      FOV +0x18 - and by copying the PlayerCameraManager POV
            //      before the virtual GetPlayerViewPoint call. The scene-view
            //      builder (0x010c8c90) reaches it both directly and through
            //      GetProjectionData (0x010ce500). THE RENDER PATH.
            //  [1] 0x0104396a  inside the viewport draw fn (0x01042370), which
            //      also drives the scene view; audio-listener placement.
            //  [2] 0x0110d073  another sub-call of the same draw fn.
            //  [3] 0x010d37b7  player-tick / camera-manager path (0x010d83a0).
            //  [4] 0x01242b70  sibling call from the same tick path.
            //  [5] 0x00183be3  line-of-sight / interaction check - it feeds a
            //      distance test whose result gates game logic. Injecting here
            //      would couple look into aim. NEVER inject.
            /* kKnownCallerRvas */ {{
                0x010cfeefULL, 0x0104396aULL, 0x0110d073ULL, 0x010d37b7ULL,
                0x01242b70ULL, 0x00183be3ULL, 0x0ULL, 0x0ULL,
                0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL,
            }},
            // 1 = inject only kKnownCallerRvas[0], the render path.
            /* kDefaultInjectMode     */ 1,
            /* kShowMouseCursorOffset */ 0x0,
            /* kShowMouseCursorMask   */ 0x1u,
            // ULocalPlayer::GetViewPoint (fn 0x010cfe60, call site +0x8f) builds
            // one FMinimalViewInfo and hands its fields straight to
            // GetPlayerViewPoint, so outLocation IS &viewInfo, outRotation is
            // +0x0c and FOV is +0x18. It fills FOV from the camera manager
            // BEFORE that call, so the engine's own FOV write has already
            // landed when our hook runs - ours goes on top of it and survives
            // to the projection matrix.
            /* kViewInfoCallerRva      */ 0x010cfeefULL,
            /* kViewInfoRotationOffset */ 0x0C,
            /* kViewInfoFovOffset      */ 0x18,
        },
    };
}
