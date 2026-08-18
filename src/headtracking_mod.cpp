#include "headtracking_mod.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include <windows.h>
#include <psapi.h>
#include <intrin.h>

#include "builds/build_registry.h"
#include "caller_trace.h"
#include "config.h"
#include "exe_paths.h"
#include "fov_override.h"
#include "hotkeys.h"
#include "logging.h"
#include "runtime_state.h"
#include "steamstub.h"
#include "ue4_types.h"
#include "view_injection.h"

#include "cameraunlock/diagnostics/crash_handler.h"
#include "cameraunlock/hooks/hook_manager.h"
#include "cameraunlock/time/frame_clock.h"
#include "cameraunlock/unreal/ue_runtime.h"

namespace finch_ht {

namespace {

using cameraunlock::TrackingMode;
using cameraunlock::time::FrameClock;
namespace hooks = cameraunlock::hooks;

HANDLE g_bootstrapThread = nullptr;

Config g_config;

std::unique_ptr<cameraunlock::UdpReceiver> g_receiver;
std::unique_ptr<Session> g_session;
std::unique_ptr<cameraunlock::input::HotkeyPoller> g_hotkeys;

GetPlayerViewPoint_t g_origGetPlayerViewPoint = nullptr;
std::atomic<std::uint64_t> g_hookCallCount{0};

// Ticked only by the injected render-path caller, so the session sees one dt
// per rendered frame.
FrameClock g_frameClock;

CallerCensus g_callerCensus;

// ---- the hook ------------------------------------------------------------

// The detour must never re-enter itself: the trampoline runs the engine's own
// GetPlayerViewPoint, and any path from there back to the hooked address would
// recurse until the stack faults. A re-entrant call is passed straight through
// to the original. The first entries are also written to the emergency log,
// which is what distinguishes "hooked and running" from a fault inside the
// target's own prologue (the signature of a trampoline built from bytes
// SteamStub had not decrypted yet - see steamstub.h).
class ReentrancyGuard {
public:
    ReentrancyGuard() : m_reentered(s_inside) { s_inside = true; }
    ~ReentrancyGuard() {
        if (!m_reentered) s_inside = false;
    }
    ReentrancyGuard(const ReentrancyGuard&) = delete;
    ReentrancyGuard& operator=(const ReentrancyGuard&) = delete;

    bool Reentered() const { return m_reentered; }

private:
    static thread_local bool s_inside;
    bool m_reentered;
};
thread_local bool ReentrancyGuard::s_inside = false;

constexpr std::uint64_t kLoggedEntries = 8;
constexpr std::uint64_t kLoggedReentries = 64;
constexpr std::uint64_t kHeartbeatIntervalMs = 30000;
constexpr std::uint64_t kPoseSampleIntervalMs = 2000;

struct HeadPose { float yaw, pitch, roll; };

// Cursor visible -> menu / cutscene / pause -> suppress tracking. When the
// offset isn't known for this build the gate is disabled (always gameplay).
bool InGameplay(std::uintptr_t controller) {
    const auto offset = Offsets().kShowMouseCursorOffset;
    if (offset == 0) return true;
    std::uint32_t bits = 0;
    if (!ue::SafeReadU32(controller + offset, bits)) return false;
    return (bits & Offsets().kShowMouseCursorMask) == 0;
}

// Distinguishes "hook never fired" from "no tracker data".
void LogHeartbeat(std::uint64_t call, std::uintptr_t retRva, int mode) {
    static std::atomic<std::uint64_t> s_lastTick{0};
    const std::uint64_t now = GetTickCount64();
    if (call != 1 && (now - s_lastTick.load(std::memory_order_relaxed)) < kHeartbeatIntervalMs)
        return;
    s_lastTick.store(now, std::memory_order_relaxed);

    float yaw = 0, pitch = 0, roll = 0;
    const bool haveData = g_receiver && g_receiver->GetRotation(yaw, pitch, roll);
    Log::Line("heartbeat hook=%llu retRVA=0x%08llx enabled=%s udpData=%s raw=(Y=%.2f P=%.2f R=%.2f) yawMode=%s injectMode=%d gameFov=%.2f fovOffset=%+.2f",
        static_cast<unsigned long long>(call),
        static_cast<unsigned long long>(retRva),
        Runtime().trackingEnabled.load() ? "ON" : "OFF",
        haveData ? "YES" : "NO", yaw, pitch, roll,
        Runtime().worldSpaceYaw.load() ? "world" : "local", mode,
        Runtime().gameFov.load(std::memory_order_relaxed),
        Runtime().fovOffset.load(std::memory_order_relaxed));
}

// Time-based sampling, not every-Nth-call: the hook runs for several distinct
// callers at different rates, so a call-count stride samples them unevenly and
// makes the log impossible to line up against wall-clock events (screenshots,
// tracker steps).
void LogPoseSample(std::uint64_t call, std::uintptr_t retRva,
                   const FRotator4f& cleanRotation, const FVector4f& cleanLocation,
                   const HeadPose& pose, const FRotator4f& trackedRotation,
                   const ue::FVector& positionOffset) {
    static std::atomic<std::uint64_t> s_lastSample{0};
    const std::uint64_t now = GetTickCount64();
    if (now - s_lastSample.load(std::memory_order_relaxed) < kPoseSampleIntervalMs) return;
    s_lastSample.store(now, std::memory_order_relaxed);

    Log::Line("hook #%llu retRVA=0x%08llx fov=%.2f clean_rot=(Y=%.2f P=%.2f R=%.2f) clean_loc=(%.0f,%.0f,%.0f) tracker=(Y=%.2f P=%.2f R=%.2f) result=(Y=%.2f P=%.2f R=%.2f) posOff=(%.1f,%.1f,%.1f)",
        static_cast<unsigned long long>(call),
        static_cast<unsigned long long>(retRva),
        Runtime().gameFov.load(std::memory_order_relaxed),
        cleanRotation.Yaw, cleanRotation.Pitch, cleanRotation.Roll,
        cleanLocation.X, cleanLocation.Y, cleanLocation.Z,
        pose.yaw, pose.pitch, pose.roll,
        trackedRotation.Yaw, trackedRotation.Pitch, trackedRotation.Roll,
        positionOffset.X, positionOffset.Y, positionOffset.Z);
}

// True while the game is on a camera the player is not looking through, logging
// each transition. See IsPinnedDownView for which chapters do this and why.
bool SuppressedForPinnedView(float cleanPitch) {
    static std::atomic<bool> s_pinned{false};
    const bool pinned = IsPinnedDownView(cleanPitch);
    if (pinned != s_pinned.exchange(pinned, std::memory_order_relaxed))
        Log::Line("view: camera pitch %.2f - tracking %s", cleanPitch,
            pinned ? "suppressed (pinned straight-down view)" : "resumed");
    return pinned;
}

ue::FVector ApplyPositionOffset(const ue::FQuat4d& baseQuat, FVector4f* outLocation) {
    float offsetX = 0.0f, offsetY = 0.0f, offsetZ = 0.0f;
    if (!g_session->GetPositionOffset(offsetX, offsetY, offsetZ))
        return ue::FVector{0.0, 0.0, 0.0};

    const ue::FVector offset = PositionOffsetUE(baseQuat, offsetX, offsetY, offsetZ);
    outLocation->X += static_cast<float>(offset.X);
    outLocation->Y += static_cast<float>(offset.Y);
    outLocation->Z += static_cast<float>(offset.Z);
    return offset;
}

void __fastcall GetPlayerViewPoint_Hook(void* self, FVector4f* outLocation, FRotator4f* outRotation) {
    const void* returnAddress = _ReturnAddress();
    const std::uintptr_t retRva = ue::ModuleBase() != 0
        ? reinterpret_cast<std::uintptr_t>(returnAddress) - ue::ModuleBase()
        : reinterpret_cast<std::uintptr_t>(returnAddress);

    static std::atomic<std::uint64_t> s_entries{0};
    const std::uint64_t entryNo = s_entries.fetch_add(1, std::memory_order_relaxed) + 1;
    ReentrancyGuard guard;
    if (entryNo <= kLoggedEntries)
        Log::EmergencyLine("GPV-ENTRY #%llu reentered=%d retRVA=0x%08llx",
            static_cast<unsigned long long>(entryNo), guard.Reentered() ? 1 : 0,
            static_cast<unsigned long long>(retRva));
    if (guard.Reentered()) {
        if (entryNo <= kLoggedReentries)
            Log::EmergencyLine("GPV-REENTRY retRVA=0x%08llx",
                static_cast<unsigned long long>(retRva));
        g_origGetPlayerViewPoint(self, outLocation, outRotation);
        return;
    }

    const bool inGameplay = InGameplay(reinterpret_cast<std::uintptr_t>(self));

    g_origGetPlayerViewPoint(self, outLocation, outRotation);
    const FRotator4f cleanRotation = *outRotation;
    const FVector4f  cleanLocation = *outLocation;

    const std::uint64_t call = g_hookCallCount.fetch_add(1, std::memory_order_relaxed) + 1;
    const int mode = Runtime().injectMode.load(std::memory_order_relaxed);

    if (mode == kInjectModeAllCallers)
        g_callerCensus.RecordAndMaybeDump(call);

    // FOV first, and outside every tracking gate: it is a view preference, not
    // part of the head pose, so it holds through menus and through the toggle.
    // The caller reassigns FOV from the camera manager ahead of every one of
    // these calls, so the offset applies to a fresh value each frame and never
    // compounds.
    ApplyFovOverride(retRva, outLocation, outRotation);

    LogHeartbeat(call, retRva, mode);

    if (!Runtime().trackingEnabled.load(std::memory_order_relaxed) || !g_session || !inGameplay)
        return;

    // Decoupling: only the render-path caller(s) get the head pose written back.
    // Every other GetPlayerViewPoint caller (interaction traces, audio listener,
    // AI perception, replication) keeps the clean mouse/pad rotation.
    if (!ShouldInjectForCaller(retRva, mode, Offsets().kKnownCallerRvas))
        return;

    if (!g_session->Update(g_frameClock.Tick()))
        return;

    HeadPose pose{};
    if (!g_session->GetRotation(pose.yaw, pose.pitch, pose.roll))
        return;

    // Checked after the session update so smoothing keeps running while
    // suppressed - the pose is already current when the gate releases.
    if (SuppressedForPinnedView(cleanRotation.Pitch))
        return;

    const ue::FQuat4d baseQuat = ViewQuat(cleanRotation);
    *outRotation = ComposeTrackedRotation(cleanRotation, baseQuat, pose.yaw, pose.pitch, pose.roll,
        Runtime().worldSpaceYaw.load(std::memory_order_relaxed));
    const ue::FVector positionOffset = ApplyPositionOffset(baseQuat, outLocation);

    LogPoseSample(call, retRva, cleanRotation, cleanLocation, pose, *outRotation, positionOffset);
}

// ---- bootstrap -----------------------------------------------------------
void ApplyConfigToSession() {
    Runtime().trackingEnabled.store(g_config.enable_on_startup);
    Runtime().worldSpaceYaw.store(g_config.world_space_yaw);
    Runtime().fovOffset.store(g_config.fov_offset);

    cameraunlock::SensitivitySettings sens;
    sens.yaw          = g_config.yaw_sensitivity;
    sens.pitch        = g_config.pitch_sensitivity;
    sens.roll         = g_config.roll_sensitivity;
    sens.invert_yaw   = g_config.invert_yaw;
    sens.invert_pitch = g_config.invert_pitch;
    sens.invert_roll  = g_config.invert_roll;
    g_session->GetProcessor().SetSensitivity(sens);
    // Both smoothing parameters cover rotation and position; the session picks
    // between them per connection from the receiver's source-address check, so
    // a switch from a local OpenTrack instance to a phone on WiFi mid-session
    // needs no restart.
    g_session->SetLocalSmoothing(g_config.local_smoothing);
    g_session->SetRemoteSmoothing(g_config.remote_smoothing);

    auto& position = g_session->GetPositionProcessor().GetSettings();
    position.sensitivity_x = g_config.position_sensitivity_x;
    position.sensitivity_y = g_config.position_sensitivity_y;
    position.sensitivity_z = g_config.position_sensitivity_z;
    position.limit_x       = g_config.limit_x;
    // The INI exposes one vertical limit, so it has to reach both sides of the
    // clamp - the processor's is [-limit_y_down, +limit_y], and leaving the
    // down side at its struct default silently caps a raised LimitY at 0.20m
    // downward.
    position.limit_y       = g_config.limit_y;
    position.limit_y_down  = g_config.limit_y;
    position.limit_z       = g_config.limit_z;
    position.limit_z_back  = g_config.limit_z_back;

    g_session->SetMode(g_config.position_enabled
        ? TrackingMode::RotationAndPosition
        : TrackingMode::RotationOnly);
}

void LoadAndLogConfig() {
    const std::string exeDir = ExeDirectoryNarrow();
    WriteDefaultConfigIfMissing(exeDir);
    LoadConfig(exeDir, g_config);
    Log::Line("config: udp_port=%d enable=%d yaw_sens=%.2f local_smoothing=%.2f remote_smoothing=%.2f position=%d yaw_mode=%s yaw_mode_key=0x%02X fov_offset=%+.2f",
        g_config.udp_port, g_config.enable_on_startup ? 1 : 0,
        g_config.yaw_sensitivity, g_config.local_smoothing, g_config.remote_smoothing,
        g_config.position_enabled ? 1 : 0,
        g_config.world_space_yaw ? "world" : "local", g_config.yaw_mode_key,
        g_config.fov_offset);
}

// False = this build is not one the mod knows how to touch. The caller must
// then install nothing at all, leaving the game vanilla.
bool SelectBuildProfile(HMODULE host) {
    const auto match = builds::SelectProfile(host);
    switch (match) {
        case builds::MatchResult::Matched:
            return true;
        case builds::MatchResult::HostNewer:
            Log::Line("build-check: this game build is NEWER than any profile this "
                      "mod knows about - check the releases page for an update. "
                      "Staying dormant; game runs vanilla.");
            return false;
        case builds::MatchResult::HostOlder:
            Log::Line("build-check: this game build is OLDER than the profile - let "
                      "Steam finish updating. Staying dormant; game runs vanilla.");
            return false;
        default:
            Log::Line("build-check: no matching/complete profile - staying dormant; "
                      "game runs vanilla.");
            return false;
    }
}

// Publishes the module range that every RVA in this mod is relative to.
bool PublishModuleRange(HMODULE host) {
    MODULEINFO info{};
    if (!GetModuleInformation(GetCurrentProcess(), host, &info, sizeof(info))) {
        Log::Line("FATAL: GetModuleInformation failed - cannot resolve RVAs");
        return false;
    }
    const auto base = reinterpret_cast<std::uintptr_t>(info.lpBaseOfDll);
    // The lean hook does no UObject reflection, so the globals layout is
    // left zeroed - only the module range and SafeRead* guards are used.
    ue::SetRuntime(base, base + info.SizeOfImage, ue::UObjectGlobalsLayout{});
    Log::Line("module base=0x%llx size=0x%x",
        static_cast<unsigned long long>(base), info.SizeOfImage);
    return true;
}

void StartTracking() {
    g_receiver = std::make_unique<cameraunlock::UdpReceiver>();
    g_receiver->SetLog([](const std::string& message) { Log::Line("udp: %s", message.c_str()); });
    if (!g_receiver->Start(static_cast<uint16_t>(g_config.udp_port)))
        Log::Line("udp: port %d busy - retrying in background", g_config.udp_port);

    g_session = std::make_unique<Session>(*g_receiver);
    ApplyConfigToSession();
}

bool InstallViewPointHook() {
    auto& manager = hooks::HookManager::Instance();
    if (auto status = manager.Initialize(); status != hooks::HookStatus::Ok) {
        Log::Line("FATAL: MinHook init failed: %s", hooks::HookStatusToString(status));
        return false;
    }

    const std::uintptr_t targetAddress = ue::ModuleBase() + Offsets().kGetPlayerViewPointRva;
    // An RVA past the end of the module does not name code in this build, and
    // in a loaded process it can land inside an unrelated DLL that would be
    // hooked instead. Checked before the address is read or written to.
    if (targetAddress < ue::ModuleBase()
        || targetAddress + Offsets().kGetPlayerViewPointPrologue.size() > ue::ModuleEnd()) {
        Log::Line("FATAL: profile %s puts GetPlayerViewPoint at RVA 0x%08llx, outside the "
                  "module - staying dormant; game runs vanilla.",
            builds::ActiveProfile().Name,
            static_cast<unsigned long long>(Offsets().kGetPlayerViewPointRva));
        return false;
    }

    void* target = reinterpret_cast<void*>(targetAddress);
    if (!WaitForDecryptedTarget(target, Offsets().kGetPlayerViewPointPrologue,
                                builds::ActiveProfile().Name))
        return false;

    if (auto status = manager.CreateHook(target, reinterpret_cast<void*>(&GetPlayerViewPoint_Hook),
                                         reinterpret_cast<void**>(&g_origGetPlayerViewPoint));
        status != hooks::HookStatus::Ok) {
        Log::Line("FATAL: CreateHook(GetPlayerViewPoint) failed: %s", hooks::HookStatusToString(status));
        return false;
    }
    if (auto status = manager.EnableHook(target); status != hooks::HookStatus::Ok) {
        Log::Line("FATAL: EnableHook failed: %s", hooks::HookStatusToString(status));
        return false;
    }
    Log::Line("GetPlayerViewPoint hooked at RVA 0x%08llx (default inject mode %d)",
        static_cast<unsigned long long>(Offsets().kGetPlayerViewPointRva),
        Runtime().injectMode.load());
    return true;
}

DWORD WINAPI BootstrapThread(LPVOID) {
    Log::Open(ExeDirectory() + L"\\HeadTracking.log");
    Log::Line("=== What Remains of Edith Finch Head Tracking (UE4) ===");
    cameraunlock::diagnostics::InstallCrashHandler();

    LoadAndLogConfig();

    HMODULE host = GetModuleHandleW(nullptr);
    if (!SelectBuildProfile(host)) return 0;

    Runtime().injectMode.store(Offsets().kDefaultInjectMode);

    if (!PublishModuleRange(host)) return 0;

    StartTracking();

    if (!InstallViewPointHook()) return 0;

    g_hotkeys = StartHotkeys(*g_session, g_config.yaw_mode_key);
    Log::Line("init complete. Home=recenter End=toggle PageUp=cycle tracking mode "
              "PageDown=yawmode (chords Ctrl+Shift+T/Y/G/H). Waiting for OpenTrack on UDP %d.",
        g_config.udp_port);
    return 0;
}

}  // namespace

void Initialize(HMODULE) {
    g_bootstrapThread = CreateThread(nullptr, 0, BootstrapThread, nullptr, 0, nullptr);
}

void Shutdown() {
    if (g_hotkeys) g_hotkeys->Stop();
    if (g_receiver) g_receiver->Stop();
    hooks::HookManager::Instance().Shutdown();
    Log::Line("shutdown");
    Log::Close();
    if (g_bootstrapThread) { CloseHandle(g_bootstrapThread); g_bootstrapThread = nullptr; }
}

}  // namespace finch_ht
