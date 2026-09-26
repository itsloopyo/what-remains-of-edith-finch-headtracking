#pragma once

#include <memory>

#include <cameraunlock/input/hotkey_poller.h>

#include "config.h"
#include "runtime_state.h"

namespace finch_ht
{
    // Registers the ToggleKey, CycleTrackingModeKey and YawModeKey lists from
    // the config, and the dev inject-mode cycling in a build that asks for it,
    // then starts polling. The returned poller owns the polling thread; Stop()
    // it before the session goes away.
    std::unique_ptr<cameraunlock::input::HotkeyPoller> StartHotkeys(Session& session, const Config& config);
}
