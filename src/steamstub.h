#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "builds/build_profile.h"

// FinchGame.exe is SteamStub-packed: `.text` arrives as ciphertext under
// writable protection, and the stub decrypts it in place at the entry point
// before restoring PAGE_EXECUTE_READ. Our bootstrap thread is released by the
// loader at roughly the same moment, so hooking immediately captures ciphertext
// into MinHook's trampoline - the first hooked call then executes garbage and
// faults inside what looks like the target's own prologue.

namespace finch_ht
{
    // "48 89 5C 24 ", uppercase, one trailing space per byte.
    std::string HexBytes(const std::uint8_t* bytes, std::size_t count);

    bool PageIsExecutable(const void* address);

    // True only if every byte of [address, address+bytes) is committed and
    // readable right now. Nothing may dereference a per-build RVA without this
    // first: a stale offset, or a page SteamStub has protections off mid-flight,
    // otherwise faults the bootstrap thread and kills the game.
    bool RangeIsReadable(const void* address, std::size_t bytes);

    // Blocks until the target is both decrypted (expectedPrologue present) and
    // executable again, logging what it saw either way. False = gave up; the
    // caller must then stay dormant. profileName appears in the failure log.
    bool WaitForDecryptedTarget(const void* target, const PrologueBytes& expectedPrologue,
                                const char* profileName);
}
