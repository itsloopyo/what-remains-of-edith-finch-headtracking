// Characterization tests for the two diagnostic renderings the mod's log is
// read through: the call-chain census line that identifies the render-path
// caller after a patch, and the byte dump that proves whether SteamStub has
// decrypted the hook target.

#include "caller_trace.h"
#include "exe_paths.h"
#include "steamstub.h"

#include <cstdint>

#include <windows.h>

#include "test_support.h"

namespace {

using finch_tests::Check;

void FormatChainTests(int& failures)
{
    finch_ht::CallChain chain{};
    chain[0] = 0x010cfeef;
    chain[1] = 0x010c8c90;
    Check(failures, finch_ht::FormatChain(chain) == "0x010cfeef <- 0x010c8c90",
          "a chain renders newest-frame-first, 8 hex digits, arrow separated");

    Check(failures, finch_ht::FormatChain(finch_ht::CallChain{}) == "",
          "an empty chain renders as nothing");

    finch_ht::CallChain full{};
    for (std::size_t i = 0; i < finch_ht::kChainDepth; ++i) full[i] = 0x1000 + i;
    Check(failures, finch_ht::FormatChain(full) ==
            "0x00001000 <- 0x00001001 <- 0x00001002 <- 0x00001003 <- 0x00001004",
          "a full-depth chain renders every frame");

    // A zero frame terminates the chain: CaptureChain leaves the tail zeroed
    // when the stack is shallower than kChainDepth.
    finch_ht::CallChain gap{};
    gap[0] = 0x1000;
    gap[2] = 0x3000;
    Check(failures, finch_ht::FormatChain(gap) == "0x00001000",
          "rendering stops at the first empty frame");
}

void HexBytesTests(int& failures)
{
    const std::uint8_t prologue[] = {0x48, 0x89, 0x5C, 0x24, 0x10};
    Check(failures, finch_ht::HexBytes(prologue, sizeof(prologue)) == "48 89 5C 24 10 ",
          "bytes render uppercase, space separated, with a trailing space");
    Check(failures, finch_ht::HexBytes(prologue, 0) == "",
          "an empty range renders as nothing");

    const std::uint8_t edges[] = {0x00, 0x0F, 0xF0, 0xFF};
    Check(failures, finch_ht::HexBytes(edges, sizeof(edges)) == "00 0F F0 FF ",
          "both nibbles are always printed");
}

void DirectoryOfTests(int& failures)
{
    Check(failures, finch_ht::DirectoryOf(std::string("C:\\Games\\Finch\\FinchGame.exe"))
            == "C:\\Games\\Finch",
          "the EXE directory drops the file name");
    Check(failures, finch_ht::DirectoryOf(std::string("C:/Games/Finch/FinchGame.exe"))
            == "C:/Games/Finch",
          "forward slashes separate too");
    Check(failures, finch_ht::DirectoryOf(std::string("FinchGame.exe")) == ".",
          "a bare file name resolves to the current directory");
    Check(failures, finch_ht::DirectoryOf(std::wstring(L"C:\\Games\\Finch\\FinchGame.exe"))
            == L"C:\\Games\\Finch",
          "the wide overload behaves identically");
}

// The gate that stands between a per-build RVA and a dereference. Without it a
// stale offset - or a page SteamStub currently has protections off - faults the
// bootstrap thread and takes the game with it, which is exactly the crash the
// dormancy contract exists to prevent.
void RangeIsReadableTests(int& failures)
{
    const std::uint8_t buffer[16] = {};
    Check(failures, finch_ht::RangeIsReadable(buffer, sizeof(buffer)),
          "committed readable memory passes");

    Check(failures, !finch_ht::RangeIsReadable(nullptr, 16), "a null address is rejected");
    Check(failures, !finch_ht::RangeIsReadable(buffer, 0), "an empty range is rejected");

    // Reserved but never committed: VirtualQuery answers, the read would fault.
    void* reserved = VirtualAlloc(nullptr, 0x1000, MEM_RESERVE, PAGE_READWRITE);
    Check(failures, reserved != nullptr && !finch_ht::RangeIsReadable(reserved, 16),
          "reserved-but-uncommitted memory is rejected");

    void* noAccess = VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_NOACCESS);
    Check(failures, noAccess != nullptr && !finch_ht::RangeIsReadable(noAccess, 16),
          "a PAGE_NOACCESS page is rejected");

    void* guarded = VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE,
                                 PAGE_READWRITE | PAGE_GUARD);
    Check(failures, guarded != nullptr && !finch_ht::RangeIsReadable(guarded, 16),
          "a guard page is rejected even though its protection reads as readable");

    // A range that starts inside a committed page but runs past its end: only
    // the first byte is covered by the region VirtualQuery reports.
    void* committed = VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (committed != nullptr) {
        auto* lastByte = static_cast<std::uint8_t*>(committed) + 0x1000 - 1;
        Check(failures, finch_ht::RangeIsReadable(lastByte, 1),
              "the final byte of a committed region passes");
        Check(failures, !finch_ht::RangeIsReadable(lastByte, 16),
              "a range running past the end of its region is rejected");
        VirtualFree(committed, 0, MEM_RELEASE);
    }

    if (reserved) VirtualFree(reserved, 0, MEM_RELEASE);
    if (noAccess) VirtualFree(noAccess, 0, MEM_RELEASE);
    if (guarded) VirtualFree(guarded, 0, MEM_RELEASE);
}

}  // namespace

int RunDiagnosticsTests()
{
    int failures = 0;
    std::cout << "Diagnostics tests\n";
    FormatChainTests(failures);
    HexBytesTests(failures);
    DirectoryOfTests(failures);
    RangeIsReadableTests(failures);
    return finch_tests::Report("Diagnostics tests", failures);
}
