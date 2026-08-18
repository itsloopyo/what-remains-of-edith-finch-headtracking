#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

// Caller distribution accounting for GetPlayerViewPoint, only meaningful in
// inject mode 0 - that's how the render caller gets (re-)confirmed after a
// patch. Off the hot path otherwise.
//
// The immediate return address alone does not say WHICH path reached the call
// site. GetPlayerViewPoint is reached through ULocalPlayer::GetViewPoint, which
// is itself called both from the scene-view builder AND from
// ULocalPlayer::GetProjectionData - and GetProjectionData is what deprojection
// / world-to-screen game logic runs through. So the census records whole call
// CHAINS: one return address collapses those two into a single bucket, and the
// chain is what tells them apart in the log.
//
// The inject gate itself (ShouldInjectForCaller) still matches ONE frame, the
// immediate return address, so injecting on ULocalPlayer::GetViewPoint covers
// the GetProjectionData path too. Edith Finch draws no crosshair and does no
// world-to-screen gameplay work, so nothing reads that path; a game that does
// would need the gate widened to match the chain the census already captures.

namespace finch_ht
{
    inline constexpr std::size_t kChainDepth = 5;
    using CallChain = std::array<std::uintptr_t, kChainDepth>;

    // "0x00010cfe <- 0x0010c8c9 <- ...", stopping at the first empty frame.
    std::string FormatChain(const CallChain& chain);

    // Frames 0..kChainDepth-1 above the caller, as module RVAs. Frame 0 is the
    // GetPlayerViewPoint call site itself; our own .asi frames are skipped.
    CallChain CaptureChain();

    class CallerCensus
    {
    public:
        // Counts the current call chain and dumps the tally to the log once
        // every kSummaryEvery hook calls. callCount is the hook's running call
        // counter, which paces the dump.
        void RecordAndMaybeDump(std::uint64_t callCount);

    private:
        struct ChainHash
        {
            std::size_t operator()(const CallChain& c) const noexcept
            {
                std::size_t h = 1469598103934665603ULL;
                for (auto v : c) { h ^= v; h *= 1099511628211ULL; }
                return h;
            }
        };

        static constexpr std::uint64_t kSummaryEvery = 3600;

        void Dump(std::uint64_t total);

        std::mutex m_mutex;
        std::unordered_map<CallChain, std::uint64_t, ChainHash> m_counts;
        std::uint64_t m_lastSummary = 0;
    };
}
