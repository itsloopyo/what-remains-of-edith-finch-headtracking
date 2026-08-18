#include "steamstub.h"

#include <cstring>

#include <windows.h>

#include "logging.h"

namespace finch_ht
{
    namespace
    {
        constexpr int kPollMs = 20;
        constexpr int kTimeoutMs = 60000;
    }

    std::string HexBytes(const std::uint8_t* bytes, std::size_t count)
    {
        static const char* kHex = "0123456789ABCDEF";
        std::string out;
        out.reserve(count * 3);
        for (std::size_t i = 0; i < count; ++i) {
            out.push_back(kHex[bytes[i] >> 4]);
            out.push_back(kHex[bytes[i] & 0xF]);
            out.push_back(' ');
        }
        return out;
    }

    bool PageIsExecutable(const void* address)
    {
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(address, &mbi, sizeof(mbi)) == 0) return false;
        if (mbi.State != MEM_COMMIT) return false;
        constexpr DWORD kExec = PAGE_EXECUTE | PAGE_EXECUTE_READ
                              | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        return (mbi.Protect & kExec) != 0;
    }

    bool RangeIsReadable(const void* address, std::size_t bytes)
    {
        if (address == nullptr || bytes == 0) return false;

        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(address, &mbi, sizeof(mbi)) == 0) return false;
        if (mbi.State != MEM_COMMIT) return false;
        // PAGE_GUARD raises on the first touch even though the underlying
        // protection reads as readable, so it has to be excluded explicitly.
        if ((mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) return false;
        constexpr DWORD kReadable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY
                                  | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE
                                  | PAGE_EXECUTE_WRITECOPY;
        if ((mbi.Protect & kReadable) == 0) return false;

        // VirtualQuery answers for the region containing `address`; the range
        // has to fit inside it, or the tail lands in whatever follows.
        const auto start = reinterpret_cast<std::uintptr_t>(address);
        const auto regionEnd =
            reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        return start + bytes <= regionEnd;
    }

    bool WaitForDecryptedTarget(const void* target, const PrologueBytes& expectedPrologue,
                                const char* profileName)
    {
        const auto* got = static_cast<const std::uint8_t*>(target);
        const std::size_t size = expectedPrologue.size();

        bool announcedDecryption = false;
        bool announcedEntryBytes = false;
        for (int waited = 0; waited <= kTimeoutMs; waited += kPollMs) {
            // Re-checked every poll rather than once up front. The target is
            // only ever read after this returns, and SteamStub moves `.text`
            // protections underneath us while it decrypts - reading an address
            // that is not committed and readable at that instant faults the
            // bootstrap thread and takes the game down with it, which is the
            // opposite of the dormancy the profile check exists to guarantee.
            if (RangeIsReadable(target, size)) {
                if (!announcedEntryBytes) {
                    announcedEntryBytes = true;
                    Log::Line("steamstub: target bytes at entry: %s (exec=%d)",
                        HexBytes(got, size).c_str(), PageIsExecutable(target) ? 1 : 0);
                }
                const bool decrypted =
                    std::memcmp(got, expectedPrologue.data(), size) == 0;
                if (decrypted && !announcedDecryption) {
                    announcedDecryption = true;
                    Log::Line("steamstub: decrypted prologue present after %d ms", waited);
                }
                if (decrypted && PageIsExecutable(target)) {
                    Log::Line("steamstub: target executable after %d ms - hooking", waited);
                    return true;
                }
            }
            Sleep(kPollMs);
        }

        Log::Line("FATAL: GetPlayerViewPoint never became a decrypted executable target "
                  "for profile %s after %d ms.", profileName, kTimeoutMs);
        Log::Line("  expected: %s", HexBytes(expectedPrologue.data(), size).c_str());
        if (RangeIsReadable(target, size))
            Log::Line("  observed: %s (exec=%d)", HexBytes(got, size).c_str(),
                PageIsExecutable(target) ? 1 : 0);
        else
            Log::Line("  observed: address not committed and readable - the RVA does not "
                      "name code in this build.");
        Log::Line("  Staying dormant; game runs vanilla.");
        return false;
    }
}
