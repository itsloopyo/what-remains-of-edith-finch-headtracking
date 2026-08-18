#pragma once
#include <windows.h>

namespace finch_ht {

// Entry points called from DllMain. Initialize spins up a bootstrap thread so
// the heavy work (config, fingerprinting, UDP, MinHook) never runs under the
// loader lock.
void Initialize(HMODULE self);
void Shutdown();

}
