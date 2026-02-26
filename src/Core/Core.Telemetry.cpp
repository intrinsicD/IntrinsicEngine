module;

#include <chrono>
#include <cstdint>
#include <algorithm>
#include <atomic>
#include <vector>

module Core.Telemetry;

namespace Core::Telemetry
{
    void TimingCategory::AddSample(uint64_t durationNs)
    {
        TotalTimeNs += durationNs;
        CallCount++;
        MinTimeNs = std::min(MinTimeNs, durationNs);
        MaxTimeNs = std::max(MaxTimeNs, durationNs);
    }

    double TimingCategory::AverageMs() const
    {
        if (CallCount == 0) return 0.0;
        return static_cast<double>(TotalTimeNs) / static_cast<double>(CallCount) / 1'000'000.0;
    }

    double TimingCategory::TotalMs() const
    {
        return static_cast<double>(TotalTimeNs) / 1'000'000.0;
    }

    void TimingCategory::Reset()
    {
        TotalTimeNs = 0;
        CallCount = 0;
        MinTimeNs = UINT64_MAX;
        MaxTimeNs = 0;
    }

    void TelemetrySystem::BeginFrame()
    {
        m_FrameStartTime = std::chrono::high_resolution_clock::now();
        m_CurrentFrameSampleCount.store(0, std::memory_order_relaxed);

        for (auto& cat : m_Categories)
        {
            cat.Reset();
        }
    }

    void TelemetrySystem::EndFrame()
    {
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(
            endTime - m_FrameStartTime).count();

        size_t idx = m_CurrentFrame % MAX_FRAME_HISTORY;
        m_FrameHistory[idx].FrameNumber = m_CurrentFrame;
        m_FrameHistory[idx].FrameTimeNs = duration;
        m_FrameHistory[idx].CpuTimeNs = duration;
        m_FrameHistory[idx].SampleCount = m_CurrentFrameSampleCount.load(std::memory_order_relaxed);
        m_FrameHistory[idx].DrawCalls = m_DrawCallCount.load(std::memory_order_relaxed);
        m_FrameHistory[idx].TriangleCount = m_TriangleCount.load(std::memory_order_relaxed);
        m_FrameHistory[idx].TaskParkCount = m_TaskParkCount.load(std::memory_order_relaxed);
        m_FrameHistory[idx].TaskUnparkCount = m_TaskUnparkCount.load(std::memory_order_relaxed);
        m_FrameHistory[idx].TaskParkP50Ns = m_TaskParkP50Ns.load(std::memory_order_relaxed);
        m_FrameHistory[idx].TaskParkP95Ns = m_TaskParkP95Ns.load(std::memory_order_relaxed);
        m_FrameHistory[idx].TaskParkP99Ns = m_TaskParkP99Ns.load(std::memory_order_relaxed);
        m_FrameHistory[idx].TaskUnparkP50Ns = m_TaskUnparkP50Ns.load(std::memory_order_relaxed);
        m_FrameHistory[idx].TaskUnparkP95Ns = m_TaskUnparkP95Ns.load(std::memory_order_relaxed);
        m_FrameHistory[idx].TaskUnparkP99Ns = m_TaskUnparkP99Ns.load(std::memory_order_relaxed);
        m_FrameHistory[idx].TaskIdleWaitCount = m_TaskIdleWaitCount.load(std::memory_order_relaxed);
        m_FrameHistory[idx].TaskIdleWaitTotalNs = m_TaskIdleWaitTotalNs.load(std::memory_order_relaxed);
        m_FrameHistory[idx].TaskQueueContentionCount = m_TaskQueueContentionCount.load(std::memory_order_relaxed);
        m_FrameHistory[idx].TaskStealSuccessRatio = m_TaskStealSuccessRatio.load(std::memory_order_relaxed);
        m_FrameHistory[idx].FrameGraphCompileTimeNs = m_FrameGraphCompileTimeNs.load(std::memory_order_relaxed);
        m_FrameHistory[idx].FrameGraphExecuteTimeNs = m_FrameGraphExecuteTimeNs.load(std::memory_order_relaxed);
        m_FrameHistory[idx].FrameGraphCriticalPathTimeNs = m_FrameGraphCriticalPathTimeNs.load(std::memory_order_relaxed);

        m_CurrentFrame++;
        m_DrawCallCount.store(0, std::memory_order_relaxed);
        m_TriangleCount.store(0, std::memory_order_relaxed);
    }

    void TelemetrySystem::RecordSample(uint32_t nameHash, const char* name, uint64_t durationNs, uint16_t depth)
    {
        size_t catIdx = FindOrCreateCategory(nameHash, name);
        if (catIdx < MAX_CATEGORIES)
        {
            m_Categories[catIdx].AddSample(durationNs);
        }

        uint32_t sampleIdx = m_CurrentFrameSampleCount.fetch_add(1, std::memory_order_relaxed);
        if (sampleIdx < MAX_SAMPLES_PER_FRAME)
        {
            size_t frameIdx = m_CurrentFrame % MAX_FRAME_HISTORY;
            auto& sample = m_SampleBuffer[frameIdx * MAX_SAMPLES_PER_FRAME + sampleIdx];
            sample.NameHash = nameHash;
            sample.DurationNs = durationNs;
            sample.Depth = depth;
        }
    }

    void TelemetrySystem::RecordDrawCall(uint32_t triangles)
    {
        m_DrawCallCount.fetch_add(1, std::memory_order_relaxed);
        m_TriangleCount.fetch_add(triangles, std::memory_order_relaxed);
    }

    void TelemetrySystem::SetGpuFrameTimeNs(uint64_t gpuTimeNs)
    {
        size_t idx = (m_CurrentFrame - 1) % MAX_FRAME_HISTORY;
        m_FrameHistory[idx].GpuTimeNs = gpuTimeNs;
    }

    void TelemetrySystem::SetTaskSchedulerStats(const Core::Tasks::Scheduler::Stats& stats)
    {
        m_TaskParkCount.store(stats.ParkCount, std::memory_order_relaxed);
        m_TaskUnparkCount.store(stats.UnparkCount, std::memory_order_relaxed);
        m_TaskParkP50Ns.store(stats.ParkLatencyP50Ns, std::memory_order_relaxed);
        m_TaskParkP95Ns.store(stats.ParkLatencyP95Ns, std::memory_order_relaxed);
        m_TaskParkP99Ns.store(stats.ParkLatencyP99Ns, std::memory_order_relaxed);
        m_TaskUnparkP50Ns.store(stats.UnparkLatencyP50Ns, std::memory_order_relaxed);
        m_TaskUnparkP95Ns.store(stats.UnparkLatencyP95Ns, std::memory_order_relaxed);
        m_TaskUnparkP99Ns.store(stats.UnparkLatencyP99Ns, std::memory_order_relaxed);
        m_TaskIdleWaitCount.store(stats.IdleWaitCount, std::memory_order_relaxed);
        m_TaskIdleWaitTotalNs.store(stats.IdleWaitTotalNs, std::memory_order_relaxed);
        m_TaskQueueContentionCount.store(stats.QueueContentionCount, std::memory_order_relaxed);
        m_TaskStealSuccessRatio.store(stats.StealSuccessRatio, std::memory_order_relaxed);
    }

    void TelemetrySystem::SetFrameGraphTimings(uint64_t compileTimeNs, uint64_t executeTimeNs, uint64_t criticalPathTimeNs)
    {
        m_FrameGraphCompileTimeNs.store(compileTimeNs, std::memory_order_relaxed);
        m_FrameGraphExecuteTimeNs.store(executeTimeNs, std::memory_order_relaxed);
        m_FrameGraphCriticalPathTimeNs.store(criticalPathTimeNs, std::memory_order_relaxed);
    }

    double TelemetrySystem::GetAverageFrameTimeMs(size_t frameCount) const
    {
        uint64_t total = 0;
        size_t count = std::min(frameCount, static_cast<size_t>(m_CurrentFrame));
        for (size_t i = 0; i < count; ++i)
        {
            total += GetFrameStats(i).FrameTimeNs;
        }
        return count > 0 ? static_cast<double>(total) / static_cast<double>(count) / 1'000'000.0 : 0.0;
    }

    double TelemetrySystem::GetAverageFPS(size_t frameCount) const
    {
        double avgMs = GetAverageFrameTimeMs(frameCount);
        return avgMs > 0.0 ? 1000.0 / avgMs : 0.0;
    }

    std::vector<const TimingCategory*> TelemetrySystem::GetCategoriesSortedByTime() const
    {
        std::vector<const TimingCategory*> result;
        size_t count = m_CategoryCount.load(std::memory_order_relaxed);
        result.reserve(count);

        for (size_t i = 0; i < count; ++i)
        {
            if (m_Categories[i].CallCount > 0)
            {
                result.push_back(&m_Categories[i]);
            }
        }

        std::sort(result.begin(), result.end(),
            [](const TimingCategory* a, const TimingCategory* b) {
                return a->TotalTimeNs > b->TotalTimeNs;
            });

        return result;
    }

    size_t TelemetrySystem::FindOrCreateCategory(uint32_t nameHash, const char* name)
    {
        size_t count = m_CategoryCount.load(std::memory_order_relaxed);
        for (size_t i = 0; i < count; ++i)
        {
            if (m_Categories[i].NameHash == nameHash)
            {
                return i;
            }
        }

        if (count < MAX_CATEGORIES)
        {
            size_t newIdx = m_CategoryCount.fetch_add(1, std::memory_order_relaxed);
            if (newIdx < MAX_CATEGORIES)
            {
                m_Categories[newIdx].NameHash = nameHash;
                m_Categories[newIdx].Name = name;
                return newIdx;
            }
        }
        return MAX_CATEGORIES;
    }
}
