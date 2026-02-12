module;

#include <chrono>
#include <cstdint>
#include <string_view>
#include <array>
#include <atomic>
#include <vector>

export module Core:Telemetry;

export namespace Core::Telemetry
{
    // -------------------------------------------------------------------------
    // Lock-Free Ring Buffer for Frame Timing Data
    // -------------------------------------------------------------------------

    struct TimingSample
    {
        uint64_t StartTimeNs = 0;
        uint64_t DurationNs = 0;
        uint32_t NameHash = 0;      // FNV-1a hash of the scope name
        uint16_t ThreadId = 0;       // Which thread recorded this
        uint16_t Depth = 0;          // Nesting depth for hierarchical display
    };

    struct FrameStats
    {
        uint64_t FrameNumber = 0;
        uint64_t FrameTimeNs = 0;
        uint64_t CpuTimeNs = 0;
        uint64_t GpuTimeNs = 0;
        uint32_t DrawCalls = 0;
        uint32_t TriangleCount = 0;
        uint32_t SampleCount = 0;
    };

    struct TimingCategory
    {
        uint32_t NameHash = 0;
        const char* Name = nullptr;
        uint64_t TotalTimeNs = 0;
        uint32_t CallCount = 0;
        uint64_t MinTimeNs = UINT64_MAX;
        uint64_t MaxTimeNs = 0;

        void AddSample(uint64_t durationNs);
        [[nodiscard]] double AverageMs() const;
        [[nodiscard]] double TotalMs() const;
        void Reset();
    };

    // -------------------------------------------------------------------------
    // Telemetry System - Thread-Safe Singleton
    // -------------------------------------------------------------------------
    class TelemetrySystem
    {
    public:
        static constexpr size_t MAX_SAMPLES_PER_FRAME = 4096;
        static constexpr size_t MAX_FRAME_HISTORY = 120;
        static constexpr size_t MAX_CATEGORIES = 256;

        static TelemetrySystem& Get()
        {
            static TelemetrySystem instance;
            return instance;
        }

        void BeginFrame();
        void EndFrame();
        void RecordSample(uint32_t nameHash, const char* name, uint64_t durationNs, uint16_t depth = 0);
        void RecordDrawCall(uint32_t triangles = 0);
        void SetGpuFrameTimeNs(uint64_t gpuTimeNs);

        [[nodiscard]] const FrameStats& GetFrameStats(size_t framesAgo = 0) const
        {
            size_t idx = (m_CurrentFrame - 1 - framesAgo) % MAX_FRAME_HISTORY;
            return m_FrameHistory[idx];
        }

        [[nodiscard]] double GetAverageFrameTimeMs(size_t frameCount = 60) const;
        [[nodiscard]] double GetAverageFPS(size_t frameCount = 60) const;

        [[nodiscard]] const TimingCategory* GetCategories() const { return m_Categories.data(); }
        [[nodiscard]] size_t GetCategoryCount() const { return m_CategoryCount.load(std::memory_order_relaxed); }

        [[nodiscard]] std::vector<const TimingCategory*> GetCategoriesSortedByTime() const;

    private:
        TelemetrySystem() = default;

        size_t FindOrCreateCategory(uint32_t nameHash, const char* name);

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
#define INTRINSIC_PROFILE_SCOPE(name) \
    static constexpr uint32_t _profileHash##__LINE__ = Core::Telemetry::HashString(name); \
    Core::Telemetry::ScopedTimer _profileTimer##__LINE__(name, _profileHash##__LINE__)

#define INTRINSIC_PROFILE_FUNCTION() INTRINSIC_PROFILE_SCOPE(__func__)

#define PROFILE_SCOPE(name) INTRINSIC_PROFILE_SCOPE(name)
#define PROFILE_FUNCTION() INTRINSIC_PROFILE_FUNCTION()
