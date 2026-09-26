#include "hotkeys.h"

#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

#include <cameraunlock/input/chord_hotkeys.h>
#include <cameraunlock/input/key_binding_registration.h>
#include <cameraunlock/input/key_bindings.h>

#include "builds/build_registry.h"
#include "logging.h"
#include "view_injection.h"

#ifndef EDITHFINCH_DEV_HOTKEYS
#define EDITHFINCH_DEV_HOTKEYS 0
#endif

namespace finch_ht
{
    namespace
    {
        using cameraunlock::TrackingMode;
        using cameraunlock::input::KeyBinding;
#if EDITHFINCH_DEV_HOTKEYS
        using cameraunlock::input::ChordGuarded;

        constexpr int kVkU = 0x55;
        constexpr int kVkJ = 0x4A;
#endif

        constexpr int kPollIntervalMs = 16;

        // End changes this session only; EnableOnStartup decides the next one.
        void ToggleTracking()
        {
            const bool enabled = !Runtime().trackingEnabled.load();
            Runtime().trackingEnabled.store(enabled);
            Log::Line("hotkey: tracking %s", enabled ? "ON" : "OFF");
        }

        // The session's mode is an atomic the hook reads each frame, so the
        // cycle applies it here and then saves it.
        void CycleTrackingMode(Session& session)
        {
            const TrackingMode mode = session.CycleMode();
            const char* name = "normal (rotation + position)";
            switch (mode) {
                case TrackingMode::RotationOnly: name = "rotation only (position off)"; break;
                case TrackingMode::PositionOnly: name = "position only (rotation off)"; break;
                case TrackingMode::RotationAndPosition: break;
            }
            Log::Line("hotkey: tracking mode -> %s", name);
            config::SaveTrackingMode(mode);
        }

        void ToggleYawMode()
        {
            const bool worldSpace = !Runtime().worldSpaceYaw.load();
            Runtime().worldSpaceYaw.store(worldSpace);
            Log::Line("hotkey: yaw mode %s", worldSpace ? "world" : "local");
            config::SaveWorldSpaceYaw(worldSpace);
        }

        // The table's hotkey codec only lets through a list this parser reads.
        std::vector<KeyBinding> Bindings(const char* key, const std::string& list)
        {
            const cameraunlock::input::KeyBindingsParseResult parsed = cameraunlock::input::ParseKeyBindings(list);
            if (!parsed.ok()) throw std::logic_error(std::string(key) + "='" + list + "': " + parsed.error);
            return parsed.bindings;
        }

#if EDITHFINCH_DEV_HOTKEYS
        void CycleInject(int direction)
        {
            const int mode = CycleInjectMode(Runtime().injectMode.load(), direction);
            Runtime().injectMode.store(mode);
            Log::Line("hotkey: inject mode -> %d (caller RVA 0x%08llx)", mode,
                static_cast<unsigned long long>(
                    CallerRvaForMode(mode, Offsets().kKnownCallerRvas)));
        }
#endif
    }

    std::unique_ptr<cameraunlock::input::HotkeyPoller> StartHotkeys(Session& session, const Config& config)
    {
        auto poller = std::make_unique<cameraunlock::input::HotkeyPoller>();

        // Each list holds every key that fires its action, the Ctrl+Shift chord
        // included. A key without modifiers stays silent while Ctrl and Shift
        // are both held, so Ctrl+Shift+<key> reaches only a binding that names
        // the chord.
        cameraunlock::input::RegisterKeyBindings(*poller, Bindings("ToggleKey", config.toggle_key),
                                                 [] { ToggleTracking(); });
        cameraunlock::input::RegisterKeyBindings(*poller,
                                                 Bindings("CycleTrackingModeKey", config.cycle_tracking_mode_key),
                                                 [&session] { CycleTrackingMode(session); });
        cameraunlock::input::RegisterKeyBindings(*poller, Bindings("YawModeKey", config.yaw_mode_key),
                                                 [] { ToggleYawMode(); });
        Log::Line("hotkey: toggle=[%s] cycle tracking mode=[%s] yaw mode=[%s]", config.toggle_key.c_str(),
                  config.cycle_tracking_mode_key.c_str(), config.yaw_mode_key.c_str());

#if EDITHFINCH_DEV_HOTKEYS
        // Dev: re-confirm the render caller in-game (cycle which GPV caller is
        // injected) without a rebuild. Ctrl+Shift+U next / Ctrl+Shift+J prev.
        poller->AddHotkey(kVkU, ChordGuarded([] { CycleInject(+1); }));
        poller->AddHotkey(kVkJ, ChordGuarded([] { CycleInject(-1); }));
        Log::Line("dev: inject-mode hotkeys enabled (Ctrl+Shift+U / Ctrl+Shift+J)");
#endif

        poller->Start(kPollIntervalMs);
        return poller;
    }
}
