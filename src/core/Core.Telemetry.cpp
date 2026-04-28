module;

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

module Extrinsic.Core.Telemetry;

namespace Extrinsic::Core::Telemetry
{
    // -----------------------------------------------------------------------
    // Alloc — global lock-free allocation counters
    // -----------------------------------------------------------------------
    namespace
    {
        std::atomic<uint64_t> g_AllocBytes{0};
        std::atomic<uint64_t> g_AllocCount{0};
    }

    namespace Alloc
    {
        void RecordAlloc(std::size_t bytes) noexcept
        {
            g_AllocBytes.fetch_add(bytes,        std::memory_order_relaxed);
            g_AllocCount.fetch_add(1,            std::memory_order_relaxed);
        }

        uint64_t SnapshotBytes() noexcept { return g_AllocBytes.load(std::memory_order_relaxed); }
        uint64_t SnapshotCount() noexcept { return g_AllocCount.load(std::memory_order_relaxed); }

        void Reset() noexcept
        {
            g_AllocBytes.store(0, std::memory_order_relaxed);
            g_AllocCount.store(0, std::memory_order_relaxed);
        }
    }

    // -----------------------------------------------------------------------
    // TimingCategory
    // -----------------------------------------------------------------------
    void TimingCategory::AddSample(uint64_t ns) noexcept
    {
        TotalTimeNs += ns;
        ++CallCount;
        if (ns < MinTimeNs) MinTimeNs = ns;
        if (ns > MaxTimeNs) MaxTimeNs = ns;
    }

    double TimingCategory::AverageMs() const noexcept
    {
        if (CallCount == 0) return 0.0;
        return static_cast<double>(TotalTimeNs) /
               static_cast<double>(CallCount) / 1'000'000.0;
    }

    double TimingCategory::TotalMs() const noexcept
    {
        return static_cast<double>(TotalTimeNs) / 1'000'000.0;
    }

    void TimingCategory::Reset() noexcept
    {
        TotalTimeNs = 0;
        CallCount   = 0;
        MinTimeNs   = UINT64_MAX;
        MaxTimeNs   = 0;
    }

    // -----------------------------------------------------------------------
    // TelemetrySystem
    // -----------------------------------------------------------------------
    void TelemetrySystem::BeginFrame()
    {
        m_FrameStart = std::chrono::high_resolution_clock::now();
        m_SampleCount.store(0,  std::memory_order_relaxed);
        m_DrawCalls.store(0,    std::memory_order_relaxed);
        m_Triangles.store(0,    std::memory_order_relaxed);
        m_FenceWaitNs.store(0,  std::memory_order_relaxed);
        m_AcquireNs.store(0,    std::memory_order_relaxed);
        m_PresentNs.store(0,    std::memory_order_relaxed);
        m_SimTickCount.store(0, std::memory_order_relaxed);
        m_SimClampCount.store(0,std::memory_order_relaxed);
        m_SimCpuNs.store(0,     std::memory_order_relaxed);

        const std::size_t count = m_CategoryCount.load(std::memory_order_relaxed);
        for (std::size_t i = 0; i < count; ++i)
            m_Categories[i].Reset();
    }

    void TelemetrySystem::EndFrame()
    {
        const auto end      = std::chrono::high_resolution_clock::now();
        const uint64_t ns   = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - m_FrameStart).count());

        const std::size_t idx = m_CurrentFrame % kMaxFrameHistory;
        auto& f = m_FrameHistory[idx];
        f.FrameNumber         = m_CurrentFrame;
        f.FrameTimeNs         = ns;
        f.CpuTimeNs           = ns;
        f.FenceWaitTimeNs     = m_FenceWaitNs.load(std::memory_order_relaxed);
        f.AcquireTimeNs       = m_AcquireNs.load(std::memory_order_relaxed);
        f.PresentTimeNs       = m_PresentNs.load(std::memory_order_relaxed);
        f.FramesInFlightCount = m_FramesInFlight.load(std::memory_order_relaxed);
        f.DrawCalls           = m_DrawCalls.load(std::memory_order_relaxed);
        f.TriangleCount       = m_Triangles.load(std::memory_order_relaxed);
        f.SampleCount         = m_SampleCount.load(std::memory_order_relaxed);
        f.SimulationTickCount = m_SimTickCount.load(std::memory_order_relaxed);
        f.SimulationClampHitCount = m_SimClampCount.load(std::memory_order_relaxed);
        f.SimulationCpuTimeNs = m_SimCpuNs.load(std::memory_order_relaxed);
        f.AllocBytes          = Alloc::SnapshotBytes();
        f.AllocCount          = Alloc::SnapshotCount();
        f.FrameGraphCompileTimeNs     = m_FGCompileNs.load(std::memory_order_relaxed);
        f.FrameGraphExecuteTimeNs     = m_FGExecuteNs.load(std::memory_order_relaxed);
        f.FrameGraphCriticalPathTimeNs = m_FGCriticalPathNs.load(std::memory_order_relaxed);
        // Task stats
        f.Tasks.ParkCount          = m_TaskParkCount.load(std::memory_order_relaxed);
        f.Tasks.UnparkCount        = m_TaskUnparkCount.load(std::memory_order_relaxed);
        f.Tasks.ParkLatencyP50Ns   = m_TaskParkP50Ns.load(std::memory_order_relaxed);
        f.Tasks.ParkLatencyP95Ns   = m_TaskParkP95Ns.load(std::memory_order_relaxed);
        f.Tasks.ParkLatencyP99Ns   = m_TaskParkP99Ns.load(std::memory_order_relaxed);
        f.Tasks.UnparkLatencyP50Ns = m_TaskUnparkP50Ns.load(std::memory_order_relaxed);
        f.Tasks.UnparkLatencyP95Ns = m_TaskUnparkP95Ns.load(std::memory_order_relaxed);
        f.Tasks.UnparkLatencyP99Ns = m_TaskUnparkP99Ns.load(std::memory_order_relaxed);
        f.Tasks.UnparkLatencyTailSpreadNs = m_TaskUnparkTailNs.load(std::memory_order_relaxed);
        f.Tasks.IdleWaitCount      = m_TaskIdleWaitCount.load(std::memory_order_relaxed);
        f.Tasks.IdleWaitTotalNs    = m_TaskIdleWaitNs.load(std::memory_order_relaxed);
        f.Tasks.QueueContentionCount = m_TaskContentionCount.load(std::memory_order_relaxed);
        f.Tasks.StealSuccessRatio  = m_TaskStealRatio.load(std::memory_order_relaxed);

        Alloc::Reset();
        ++m_CurrentFrame;
    }

    void TelemetrySystem::RecordSample(uint32_t nameHash, const char* name,
                                       uint64_t durationNs, uint16_t depth)
    {
        const std::size_t catIdx = FindOrCreateCategory(nameHash, name);
        if (catIdx < kMaxCategories)
            m_Categories[catIdx].AddSample(durationNs);

        const uint32_t si = m_SampleCount.fetch_add(1, std::memory_order_relaxed);
        if (si < kMaxSamplesPerFrame)
        {
            const std::size_t fi = m_CurrentFrame % kMaxFrameHistory;
            auto& s = m_SampleBuffer[fi * kMaxSamplesPerFrame + si];
            s.NameHash   = nameHash;
            s.DurationNs = durationNs;
            s.Depth      = depth;
        }
    }

    void TelemetrySystem::RecordDrawCall(uint32_t triangles) noexcept
    {
        m_DrawCalls.fetch_add(1,         std::memory_order_relaxed);
        m_Triangles.fetch_add(triangles, std::memory_order_relaxed);
    }

    void TelemetrySystem::SetGpuFrameTimeNs(uint64_t ns) noexcept
    {
        const std::size_t idx = (m_CurrentFrame > 0 ? m_CurrentFrame - 1 : 0) % kMaxFrameHistory;
        m_FrameHistory[idx].GpuTimeNs = ns;
    }

    void TelemetrySystem::SetPresentTimings(uint64_t fenceNs,
                                            uint64_t acquireNs,
                                            uint64_t presentNs) noexcept
    {
        m_FenceWaitNs.store(fenceNs,   std::memory_order_relaxed);
        m_AcquireNs.store(acquireNs,   std::memory_order_relaxed);
        m_PresentNs.store(presentNs,   std::memory_order_relaxed);
    }

    void TelemetrySystem::SetFramesInFlightCount(uint32_t count) noexcept
    {
        m_FramesInFlight.store(count, std::memory_order_relaxed);
    }

    void TelemetrySystem::SetSimulationStats(uint32_t tickCount,
                                             uint32_t clampCount,
                                             uint64_t cpuNs) noexcept
    {
        m_SimTickCount.store(tickCount, std::memory_order_relaxed);
        m_SimClampCount.store(clampCount, std::memory_order_relaxed);
        m_SimCpuNs.store(cpuNs,          std::memory_order_relaxed);
    }

    void TelemetrySystem::SetTaskStats(const TaskSchedulerStats& s) noexcept
    {
        m_TaskParkCount.store(s.ParkCount,               std::memory_order_relaxed);
        m_TaskUnparkCount.store(s.UnparkCount,           std::memory_order_relaxed);
        m_TaskParkP50Ns.store(s.ParkLatencyP50Ns,        std::memory_order_relaxed);
        m_TaskParkP95Ns.store(s.ParkLatencyP95Ns,        std::memory_order_relaxed);
        m_TaskParkP99Ns.store(s.ParkLatencyP99Ns,        std::memory_order_relaxed);
        m_TaskUnparkP50Ns.store(s.UnparkLatencyP50Ns,    std::memory_order_relaxed);
        m_TaskUnparkP95Ns.store(s.UnparkLatencyP95Ns,    std::memory_order_relaxed);
        m_TaskUnparkP99Ns.store(s.UnparkLatencyP99Ns,    std::memory_order_relaxed);
        m_TaskUnparkTailNs.store(s.UnparkLatencyTailSpreadNs, std::memory_order_relaxed);
        m_TaskIdleWaitCount.store(s.IdleWaitCount,       std::memory_order_relaxed);
        m_TaskIdleWaitNs.store(s.IdleWaitTotalNs,        std::memory_order_relaxed);
        m_TaskContentionCount.store(s.QueueContentionCount, std::memory_order_relaxed);
        m_TaskStealRatio.store(s.StealSuccessRatio,      std::memory_order_relaxed);
    }

    void TelemetrySystem::SetFrameGraphTimings(uint64_t compileNs,
                                               uint64_t executeNs,
                                               uint64_t criticalPathNs) noexcept
    {
        m_FGCompileNs.store(compileNs,       std::memory_order_relaxed);
        m_FGExecuteNs.store(executeNs,       std::memory_order_relaxed);
        m_FGCriticalPathNs.store(criticalPathNs, std::memory_order_relaxed);
    }

    void TelemetrySystem::SetGpuMemoryBudgets(const GpuMemorySnapshot& snapshot)
    {
        m_GpuMemory = snapshot;
    }

    void TelemetrySystem::SetPassGpuTimings(std::vector<PassTimingEntry> timings)
    {
        m_PassTimings = std::move(timings);
    }

    void TelemetrySystem::MergePassCpuTimings(
        const std::vector<std::pair<std::string, uint64_t>>& cpu)
    {
        for (const auto& [name, ns] : cpu)
        {
            bool found = false;
            for (auto& e : m_PassTimings)
            {
                if (e.Name == name) { e.CpuTimeNs = ns; found = true; break; }
            }
            if (!found)
                m_PassTimings.push_back(PassTimingEntry{name, 0, ns});
        }
    }

    const FrameStats& TelemetrySystem::GetFrameStats(std::size_t framesAgo) const noexcept
    {
        const std::size_t idx = (m_CurrentFrame > 0
            ? (m_CurrentFrame - 1 - framesAgo) % kMaxFrameHistory
            : 0);
        return m_FrameHistory[idx];
    }

    double TelemetrySystem::GetAverageFrameTimeMs(std::size_t frameCount) const
    {
        const std::size_t count = std::min(frameCount, static_cast<std::size_t>(m_CurrentFrame));
        if (count == 0) return 0.0;
        uint64_t total = 0;
        for (std::size_t i = 0; i < count; ++i)
            total += GetFrameStats(i).FrameTimeNs;
        return static_cast<double>(total) / static_cast<double>(count) / 1'000'000.0;
    }

    double TelemetrySystem::GetAverageFPS(std::size_t frameCount) const
    {
        const double ms = GetAverageFrameTimeMs(frameCount);
        return ms > 0.0 ? 1000.0 / ms : 0.0;
    }

    std::vector<const TimingCategory*> TelemetrySystem::GetCategoriesSortedByTime() const
    {
        const std::size_t count = m_CategoryCount.load(std::memory_order_relaxed);
        std::vector<const TimingCategory*> result;
        result.reserve(count);
        for (std::size_t i = 0; i < count; ++i)
            if (m_Categories[i].CallCount > 0)
                result.push_back(&m_Categories[i]);
        std::sort(result.begin(), result.end(),
            [](const TimingCategory* a, const TimingCategory* b)
            { return a->TotalTimeNs > b->TotalTimeNs; });
        return result;
    }

    std::size_t TelemetrySystem::FindOrCreateCategory(uint32_t nameHash, const char* name)
    {
        const std::size_t count = m_CategoryCount.load(std::memory_order_relaxed);
        for (std::size_t i = 0; i < count; ++i)
            if (m_Categories[i].NameHash == nameHash)
                return i;
        if (count < kMaxCategories)
        {
            const std::size_t newIdx =
                m_CategoryCount.fetch_add(1, std::memory_order_relaxed);
            if (newIdx < kMaxCategories)
            {
                m_Categories[newIdx].NameHash = nameHash;
                m_Categories[newIdx].Name     = name;
                return newIdx;
            }
        }
        return kMaxCategories; // dropped
    }
}

