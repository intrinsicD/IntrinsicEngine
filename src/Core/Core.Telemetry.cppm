gmodule;

#include <chrono>
#include <cstdint>
#include <string_view>
#include <array>
#include <atomic>
#include <mutex>
#include <vector>
#include <algorithm>

export module Core:Telemetry;

export namespace Core::Telemetry
{
    // -------------------------------------------------------------------------
    // Lock-Free Ring Buffer for Frame Timing Data
    // -------------------------------------------------------------------------
    // Design: Each frame pushes timing samples into a ring buffer.
    // UI/Debug tools can read the last N frames without blocking the main thread.
    // -------------------------------------------------------------------------

    struct TimingSample
    {
        uint64_t StartTimeNs = 0;
        uint64_t DurationNs = 0;
        uint32_t NameHash = 0;      // FNV-1a hash of the scope name
        uint16_t ThreadId = 0;       // Which thread recorded this
        uint16_t Depth = 0;          // Nesting depth for hierarchical display
    };

    // Per-frame statistics
    struct FrameStats
    {
        uint64_t FrameNumber = 0;
        uint64_t FrameTimeNs = 0;
        uint64_t CpuTimeNs = 0;
        uint64_t GpuTimeNs = 0;      // Placeholder for GPU timing
        uint32_t DrawCalls = 0;
        uint32_t TriangleCount = 0;
        uint32_t SampleCount = 0;    // Number of timing samples this frame
    };

    // Named timing category for aggregation
    struct TimingCategory
    {
        uint32_t NameHash = 0;
        const char* Name = nullptr;
        uint64_t TotalTimeNs = 0;
        uint32_t CallCount = 0;
        uint64_t MinTimeNs = UINT64_MAX;
        uint64_t MaxTimeNs = 0;

        void AddSample(uint64_t durationNs)
        {
            TotalTimeNs += durationNs;
            CallCount++;
            MinTimeNs = std::min(MinTimeNs, durationNs);
            MaxTimeNs = std::max(MaxTimeNs, durationNs);
        }

        [[nodiscard]] double AverageMs() const
        {
            if (CallCount == 0) return 0.0;
            return static_cast<double>(TotalTimeNs) / static_cast<double>(CallCount) / 1'000'000.0;
        }

        [[nodiscard]] double TotalMs() const
        {
            return static_cast<double>(TotalTimeNs) / 1'000'000.0;
        }

        void Reset()
        {
            TotalTimeNs = 0;
            CallCount = 0;
            MinTimeNs = UINT64_MAX;
            MaxTimeNs = 0;
        }
    };

    // -------------------------------------------------------------------------
    // Telemetry System - Thread-Safe Singleton
    // -------------------------------------------------------------------------
    class TelemetrySystem
    {
    public:
        static constexpr size_t MAX_SAMPLES_PER_FRAME = 4096;
        static constexpr size_t MAX_FRAME_HISTORY = 120;  // ~2 seconds at 60fps
        static constexpr size_t MAX_CATEGORIES = 256;

        static TelemetrySystem& Get()
        {
            static TelemetrySystem instance;
            return instance;
        }

        // Called at start of frame
        void BeginFrame()
        {
            m_FrameStartTime = std::chrono::high_resolution_clock::now();
            m_CurrentFrameSampleCount.store(0, std::memory_order_relaxed);

            // Reset per-frame category stats
            for (auto& cat : m_Categories)
            {
                cat.Reset();
            }
        }

        // Called at end of frame
        void EndFrame()
        {
            auto endTime = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(
                endTime - m_FrameStartTime).count();

            // Store frame stats
            size_t idx = m_CurrentFrame % MAX_FRAME_HISTORY;
            m_FrameHistory[idx].FrameNumber = m_CurrentFrame;
            m_FrameHistory[idx].FrameTimeNs = duration;
            m_FrameHistory[idx].CpuTimeNs = duration; // TODO: Separate CPU/GPU
            m_FrameHistory[idx].SampleCount = m_CurrentFrameSampleCount.load(std::memory_order_relaxed);
            m_FrameHistory[idx].DrawCalls = m_DrawCallCount.load(std::memory_order_relaxed);
            m_FrameHistory[idx].TriangleCount = m_TriangleCount.load(std::memory_order_relaxed);

            m_CurrentFrame++;
            m_DrawCallCount.store(0, std::memory_order_relaxed);
            m_TriangleCount.store(0, std::memory_order_relaxed);
        }

        // Record a timing sample (called from ScopedTimer destructor)
        void RecordSample(uint32_t nameHash, const char* name, uint64_t durationNs, uint16_t depth = 0)
        {
            // Find or create category
            size_t catIdx = FindOrCreateCategory(nameHash, name);
            if (catIdx < MAX_CATEGORIES)
            {
                m_Categories[catIdx].AddSample(durationNs);
            }

            // Store in ring buffer for detailed analysis
            uint32_t sampleIdx = m_CurrentFrameSampleCount.fetch_add(1, std::memory_order_relaxed);
            if (sampleIdx < MAX_SAMPLES_PER_FRAME)
            {
                size_t frameIdx = m_CurrentFrame % MAX_FRAME_HISTORY;
                auto& sample = m_SampleBuffer[frameIdx * MAX_SAMPLES_PER_FRAME + sampleIdx];
                sample.NameHash = nameHash;
                sample.DurationNs = durationNs;
                sample.Depth = depth;
                // ThreadId could be set here if needed
            }
        }

        // Increment draw call counter (called from render system)
        void RecordDrawCall(uint32_t triangles = 0)
        {
            m_DrawCallCount.fetch_add(1, std::memory_order_relaxed);
            m_TriangleCount.fetch_add(triangles, std::memory_order_relaxed);
        }

        // Get frame statistics for UI display
        [[nodiscard]] const FrameStats& GetFrameStats(size_t framesAgo = 0) const
        {
            size_t idx = (m_CurrentFrame - 1 - framesAgo) % MAX_FRAME_HISTORY;
            return m_FrameHistory[idx];
        }

        [[nodiscard]] double GetAverageFrameTimeMs(size_t frameCount = 60) const
        {
            uint64_t total = 0;
            size_t count = std::min(frameCount, static_cast<size_t>(m_CurrentFrame));
            for (size_t i = 0; i < count; ++i)
            {
                total += GetFrameStats(i).FrameTimeNs;
            }
            return count > 0 ? static_cast<double>(total) / static_cast<double>(count) / 1'000'000.0 : 0.0;
        }

        [[nodiscard]] double GetAverageFPS(size_t frameCount = 60) const
        {
            double avgMs = GetAverageFrameTimeMs(frameCount);
            return avgMs > 0.0 ? 1000.0 / avgMs : 0.0;
        }

        // Get category data for profiling UI
        [[nodiscard]] const TimingCategory* GetCategories() const { return m_Categories.data(); }
        [[nodiscard]] size_t GetCategoryCount() const { return m_CategoryCount.load(std::memory_order_relaxed); }

        // Get sorted categories by total time (for UI)
        std::vector<const TimingCategory*> GetCategoriesSortedByTime() const
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

    private:
        TelemetrySystem() = default;

        size_t FindOrCreateCategory(uint32_t nameHash, const char* name)
        {
            // Linear search for existing (fast for small N)
            size_t count = m_CategoryCount.load(std::memory_order_relaxed);
            for (size_t i = 0; i < count; ++i)
            {
                if (m_Categories[i].NameHash == nameHash)
                {
                    return i;
                }
            }

            // Create new category
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
            return MAX_CATEGORIES; // Failed
        }

        std::chrono::high_resolution_clock::time_point m_FrameStartTime;
        uint64_t m_CurrentFrame = 0;

        std::atomic<uint32_t> m_CurrentFrameSampleCount{0};
        std::atomic<uint32_t> m_DrawCallCount{0};
        std::atomic<uint32_t> m_TriangleCount{0};
        std::atomic<size_t> m_CategoryCount{0};

        std::array<FrameStats, MAX_FRAME_HISTORY> m_FrameHistory{};
        std::array<TimingCategory, MAX_CATEGORIES> m_Categories{};
        std::array<TimingSample, MAX_SAMPLES_PER_FRAME * MAX_FRAME_HISTORY> m_SampleBuffer{};
    };

    // -------------------------------------------------------------------------
    // Scoped Timer - RAII timing with automatic telemetry submission
    // -------------------------------------------------------------------------
    class ScopedTimer
    {
    public:
        ScopedTimer(const char* name, uint32_t nameHash)
            : m_Name(name)
            , m_NameHash(nameHash)
            , m_Start(std::chrono::high_resolution_clock::now())
        {
        }

        ~ScopedTimer()
        {
            auto end = std::chrono::high_resolution_clock::now();
            auto durationNs = std::chrono::duration_cast<std::chrono::nanoseconds>(end - m_Start).count();
            TelemetrySystem::Get().RecordSample(m_NameHash, m_Name, durationNs);
        }

        // Non-copyable
        ScopedTimer(const ScopedTimer&) = delete;
        ScopedTimer& operator=(const ScopedTimer&) = delete;

    private:
        const char* m_Name;
        uint32_t m_NameHash;
        std::chrono::high_resolution_clock::time_point m_Start;
    };

    // -------------------------------------------------------------------------
    // Compile-time FNV-1a hash for scope names
    // -------------------------------------------------------------------------
    constexpr uint32_t HashString(const char* str)
    {
        uint32_t hash = 2166136261u;
        while (*str)
        {
            hash ^= static_cast<uint8_t>(*str++);
            hash *= 16777619u;
        }
        return hash;
    }
}

// -------------------------------------------------------------------------
// Macros for easy instrumentation
// -------------------------------------------------------------------------
// PROFILE_SCOPE("MyFunction") - Named scope
// PROFILE_FUNCTION() - Uses __func__ automatically
// -------------------------------------------------------------------------

#define INTRINSIC_PROFILE_SCOPE(name) \
    static constexpr uint32_t _profileHash##__LINE__ = Core::Telemetry::HashString(name); \
    Core::Telemetry::ScopedTimer _profileTimer##__LINE__(name, _profileHash##__LINE__)

#define INTRINSIC_PROFILE_FUNCTION() INTRINSIC_PROFILE_SCOPE(__func__)

// Legacy compatibility
#define PROFILE_SCOPE(name) INTRINSIC_PROFILE_SCOPE(name)
#define PROFILE_FUNCTION() INTRINSIC_PROFILE_FUNCTION()

