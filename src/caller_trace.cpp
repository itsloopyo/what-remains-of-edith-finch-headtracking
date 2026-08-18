#include "caller_trace.h"

#include <cstdio>

#include <windows.h>

#include <cameraunlock/unreal/ue_runtime.h>

#include "logging.h"

namespace finch_ht
{
    namespace ue = ::cameraunlock::unreal;

    std::string FormatChain(const CallChain& chain)
    {
        std::string out;
        for (std::size_t i = 0; i < kChainDepth && chain[i] != 0; ++i) {
            char frame[24];
            std::snprintf(frame, sizeof(frame), "0x%08llx",
                static_cast<unsigned long long>(chain[i]));
            if (i != 0) out += " <- ";
            out += frame;
        }
        return out;
    }

    CallChain CaptureChain()
    {
        void* frames[kChainDepth + 2] = {};
        const USHORT n = RtlCaptureStackBackTrace(1, kChainDepth + 1, frames, nullptr);
        const std::uintptr_t base = ue::ModuleBase();
        CallChain chain{};
        std::size_t out = 0;
        for (USHORT i = 0; i < n && out < kChainDepth; ++i) {
            const auto address = reinterpret_cast<std::uintptr_t>(frames[i]);
            if (address < base || address >= ue::ModuleEnd()) continue;  // skip our own .asi frames
            chain[out++] = address - base;
        }
        return chain;
    }

    void CallerCensus::RecordAndMaybeDump(std::uint64_t callCount)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ++m_counts[CaptureChain()];
        if (callCount - m_lastSummary < kSummaryEvery) return;
        m_lastSummary = callCount;
        Dump(callCount);
    }

    // Caller holds m_mutex.
    void CallerCensus::Dump(std::uint64_t total)
    {
        Log::Line("caller-summary @%llu calls: %zu unique call chains:",
            static_cast<unsigned long long>(total), m_counts.size());
        for (const auto& entry : m_counts) {
            Log::Line("  count=%-8llu %s",
                static_cast<unsigned long long>(entry.second),
                FormatChain(entry.first).c_str());
        }
    }
}
