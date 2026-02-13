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
