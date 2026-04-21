module;

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

export module Extrinsic.Core.Telemetry;

import Extrinsic.Core.Hash;

// -----------------------------------------------------------------------
// Extrinsic::Core::Telemetry
// -----------------------------------------------------------------------
// Centralised engine telemetry: frame timing, GPU memory budgets, per-pass
// timings, allocation counters, and ScopedTimer RAII helpers.
//
// Memory allocation tracking (previously Core.Memory:Telemetry) lives here
// as Core::Telemetry::Alloc so that allocators do not need to reach back into
// the Memory module and create a circular dependency.
//
// TelemetrySystem is a process-global singleton (accessed via Get()). This is
// an intentional exception to the "no global state" policy — instrumentation
// is a cross-cutting concern with no meaningful owning subsystem, and the
// write path is lock-free (atomics) to avoid hot-loop overhead.
// -----------------------------------------------------------------------

export namespace Extrinsic::Core::Telemetry
{
    // -----------------------------------------------------------------------
    // Allocation telemetry (absorbs former Memory:Telemetry partition)
    // Allocators call RecordAlloc() on every allocation. Main thread reads via
    // Snapshot() once per frame into FrameStats::AllocBytes / AllocCount.
    // -----------------------------------------------------------------------
    namespace Alloc
    {
        void     RecordAlloc(std::size_t bytes) noexcept;
        [[nodiscard]] uint64_t SnapshotBytes()  noexcept;
        [[nodiscard]] uint64_t SnapshotCount()  noexcept;
        void     Reset()                        noexcept;
    }

    // -----------------------------------------------------------------------
    // GPU memory budget snapshot (populated once per frame by RHI)
    // -----------------------------------------------------------------------
    inline constexpr std::size_t kMaxMemoryHeaps = 16;
    inline constexpr uint32_t    kHeapFlagDeviceLocal = 0x01;

    struct GpuHeapBudget
    {
        uint64_t UsageBytes  = 0;
        uint64_t BudgetBytes = 0;
        uint32_t Flags       = 0;
    };

    struct GpuMemorySnapshot
    {
        uint32_t HeapCount = 0;
        std::array<GpuHeapBudget, kMaxMemoryHeaps> Heaps{};
        bool HasBudgetExtension = false;
    };

    // -----------------------------------------------------------------------
    // Per-scope timing sample (ring-buffered)
    // -----------------------------------------------------------------------
    struct TimingSample
    {
        uint64_t StartTimeNs  = 0;
        uint64_t DurationNs   = 0;
        uint32_t NameHash     = 0;
        uint16_t ThreadId     = 0;
        uint16_t Depth        = 0;
    };

    struct TimingCategory
    {
        uint32_t NameHash    = 0;
        const char* Name     = nullptr;
        uint64_t TotalTimeNs = 0;
        uint32_t CallCount   = 0;
        uint64_t MinTimeNs   = UINT64_MAX;
        uint64_t MaxTimeNs   = 0;

        void AddSample(uint64_t durationNs) noexcept;
        [[nodiscard]] double AverageMs() const noexcept;
        [[nodiscard]] double TotalMs()   const noexcept;
        void Reset() noexcept;
    };

    // Per-pass CPU+GPU combined timing (populated by render orchestrator)
    struct PassTimingEntry
    {
        std::string Name{};
        uint64_t GpuTimeNs = 0;
        uint64_t CpuTimeNs = 0;
    };

    // -----------------------------------------------------------------------
    // Task scheduler stats — defined here so Core.Tasks can report without
    // importing Core.Telemetry creating a cycle via Core.Memory.
    // Fields mirror Tasks::Scheduler::Stats (kept in sync manually).
    // -----------------------------------------------------------------------
    struct TaskSchedulerStats
    {
        uint64_t InFlightTasks         = 0;
        uint64_t QueuedTasks           = 0;
        uint64_t ParkCount             = 0;
        uint64_t UnparkCount           = 0;
        uint64_t ParkLatencyP50Ns      = 0;
        uint64_t ParkLatencyP95Ns      = 0;
        uint64_t ParkLatencyP99Ns      = 0;
        uint64_t UnparkLatencyP50Ns    = 0;
        uint64_t UnparkLatencyP95Ns    = 0;
        uint64_t UnparkLatencyP99Ns    = 0;
        uint64_t UnparkLatencyTailSpreadNs = 0;
        uint64_t IdleWaitCount         = 0;
        uint64_t IdleWaitTotalNs       = 0;
        uint64_t QueueContentionCount  = 0;
        double   StealSuccessRatio     = 0.0;
    };

    // -----------------------------------------------------------------------
    // Per-frame aggregated statistics
    // -----------------------------------------------------------------------
    struct FrameStats
    {
        uint64_t FrameNumber             = 0;
        uint64_t FrameTimeNs             = 0;
        uint64_t CpuTimeNs               = 0;
        uint64_t GpuTimeNs               = 0;
        uint64_t FenceWaitTimeNs         = 0;
        uint64_t AcquireTimeNs           = 0;
        uint64_t PresentTimeNs           = 0;
        uint32_t FramesInFlightCount     = 0;
        uint32_t DrawCalls               = 0;
        uint32_t TriangleCount           = 0;
        uint32_t SampleCount             = 0;
        uint32_t SimulationTickCount     = 0;
        uint32_t SimulationClampHitCount = 0;
        uint64_t SimulationCpuTimeNs     = 0;
        // Allocation snapshot for the frame
        uint64_t AllocBytes              = 0;
        uint64_t AllocCount              = 0;
        // Task scheduler
        TaskSchedulerStats Tasks{};
        // FrameGraph compile/execute times
        uint64_t FrameGraphCompileTimeNs    = 0;
        uint64_t FrameGraphExecuteTimeNs    = 0;
        uint64_t FrameGraphCriticalPathTimeNs = 0;
    };

    // -----------------------------------------------------------------------
    // TelemetrySystem — process-global singleton
    // -----------------------------------------------------------------------
    class TelemetrySystem
    {
    public:
        static constexpr std::size_t kMaxSamplesPerFrame = 4096;
        static constexpr std::size_t kMaxFrameHistory    = 120;
        static constexpr std::size_t kMaxCategories      = 256;
        static constexpr std::size_t kMaxPassTimings     = 32;

        [[nodiscard]] static TelemetrySystem& Get() noexcept
        {
            static TelemetrySystem s_Instance;
            return s_Instance;
        }

        // --- Frame lifecycle (main thread) ---
        void BeginFrame();
        void EndFrame();

        // --- Incremental writes (any thread, lock-free) ---
        void RecordSample(uint32_t nameHash, const char* name,
                          uint64_t durationNs, uint16_t depth = 0);
        void RecordDrawCall(uint32_t triangles = 0) noexcept;
        void SetGpuFrameTimeNs(uint64_t gpuTimeNs) noexcept;
        void SetPresentTimings(uint64_t fenceWaitNs,
                               uint64_t acquireNs,
                               uint64_t presentNs) noexcept;
        void SetFramesInFlightCount(uint32_t count) noexcept;
        void SetSimulationStats(uint32_t tickCount,
                                uint32_t clampHitCount,
                                uint64_t cpuTimeNs) noexcept;
        void SetTaskStats(const TaskSchedulerStats& stats) noexcept;
        void SetFrameGraphTimings(uint64_t compileNs,
                                  uint64_t executeNs,
                                  uint64_t criticalPathNs) noexcept;

        // --- GPU memory budget (main thread) ---
        void SetGpuMemoryBudgets(const GpuMemorySnapshot& snapshot);
        [[nodiscard]] const GpuMemorySnapshot& GetGpuMemorySnapshot() const noexcept
        { return m_GpuMemory; }

        // --- Per-pass timings (main thread) ---
        void SetPassGpuTimings(std::vector<PassTimingEntry> timings);
        void MergePassCpuTimings(const std::vector<std::pair<std::string, uint64_t>>& cpu);
        [[nodiscard]] const std::vector<PassTimingEntry>& GetPassTimings() const noexcept
        { return m_PassTimings; }

        // --- Queries ---
        [[nodiscard]] const FrameStats& GetFrameStats(std::size_t framesAgo = 0) const noexcept;
        [[nodiscard]] double GetAverageFrameTimeMs(std::size_t frameCount = 60) const;
        [[nodiscard]] double GetAverageFPS(std::size_t frameCount = 60) const;
        [[nodiscard]] const TimingCategory* GetCategories() const noexcept
        { return m_Categories.data(); }
        [[nodiscard]] std::size_t GetCategoryCount() const noexcept
        { return m_CategoryCount.load(std::memory_order_relaxed); }
        [[nodiscard]] std::vector<const TimingCategory*> GetCategoriesSortedByTime() const;

    private:
        TelemetrySystem() = default;
        TelemetrySystem(const TelemetrySystem&) = delete;
        TelemetrySystem& operator=(const TelemetrySystem&) = delete;

        std::size_t FindOrCreateCategory(uint32_t nameHash, const char* name);

        std::chrono::high_resolution_clock::time_point m_FrameStart{};
        uint64_t m_CurrentFrame = 0;

        std::atomic<uint32_t> m_SampleCount{0};
        std::atomic<uint32_t> m_DrawCalls{0};
        std::atomic<uint32_t> m_Triangles{0};
        std::atomic<uint64_t> m_FenceWaitNs{0};
        std::atomic<uint64_t> m_AcquireNs{0};
        std::atomic<uint64_t> m_PresentNs{0};
        std::atomic<uint32_t> m_FramesInFlight{0};
        std::atomic<uint32_t> m_SimTickCount{0};
        std::atomic<uint32_t> m_SimClampCount{0};
        std::atomic<uint64_t> m_SimCpuNs{0};
        std::atomic<uint64_t> m_FGCompileNs{0};
        std::atomic<uint64_t> m_FGExecuteNs{0};
        std::atomic<uint64_t> m_FGCriticalPathNs{0};
        std::atomic<std::size_t> m_CategoryCount{0};

        // Task stats — written atomically field by field from any thread.
        std::atomic<uint64_t> m_TaskParkCount{0};
        std::atomic<uint64_t> m_TaskUnparkCount{0};
        std::atomic<uint64_t> m_TaskParkP50Ns{0};
        std::atomic<uint64_t> m_TaskParkP95Ns{0};
        std::atomic<uint64_t> m_TaskParkP99Ns{0};
        std::atomic<uint64_t> m_TaskUnparkP50Ns{0};
        std::atomic<uint64_t> m_TaskUnparkP95Ns{0};
        std::atomic<uint64_t> m_TaskUnparkP99Ns{0};
        std::atomic<uint64_t> m_TaskUnparkTailNs{0};
        std::atomic<uint64_t> m_TaskIdleWaitCount{0};
        std::atomic<uint64_t> m_TaskIdleWaitNs{0};
        std::atomic<uint64_t> m_TaskContentionCount{0};
        std::atomic<double>   m_TaskStealRatio{0.0};

        std::array<FrameStats, kMaxFrameHistory>     m_FrameHistory{};
        std::array<TimingCategory, kMaxCategories>   m_Categories{};
        std::array<TimingSample,
            kMaxSamplesPerFrame * kMaxFrameHistory>  m_SampleBuffer{};

        std::vector<PassTimingEntry> m_PassTimings;
        GpuMemorySnapshot m_GpuMemory{};
    };

    // -----------------------------------------------------------------------
    // RAII scoped timer — submits duration to TelemetrySystem on destruction.
    // -----------------------------------------------------------------------
    class ScopedTimer
    {
    public:
        ScopedTimer(const char* name, uint32_t nameHash) noexcept
            : m_Name(name)
            , m_NameHash(nameHash)
            , m_Start(std::chrono::high_resolution_clock::now())
        {}

        ~ScopedTimer()
        {
            const auto end = std::chrono::high_resolution_clock::now();
            const uint64_t ns = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(end - m_Start).count());
            TelemetrySystem::Get().RecordSample(m_NameHash, m_Name, ns);
        }

        ScopedTimer(const ScopedTimer&) = delete;
        ScopedTimer& operator=(const ScopedTimer&) = delete;

    private:
        const char* m_Name;
        uint32_t    m_NameHash;
        std::chrono::high_resolution_clock::time_point m_Start;
    };

    // Alias for macro consumers
    using Core::Hash::HashString;
}

// ---------------------------------------------------------------------------
// Profiling macros
// ---------------------------------------------------------------------------
#define EXTRINSIC_PROFILE_SCOPE(name) \
    static constexpr uint32_t _profileHash_##__LINE__ = \
        Extrinsic::Core::Telemetry::HashString(name); \
    Extrinsic::Core::Telemetry::ScopedTimer \
        _profileTimer_##__LINE__(name, _profileHash_##__LINE__)

#define EXTRINSIC_PROFILE_FUNCTION() EXTRINSIC_PROFILE_SCOPE(__func__)

// Back-compat aliases used throughout legacy src/
#define INTRINSIC_PROFILE_SCOPE  EXTRINSIC_PROFILE_SCOPE
#define INTRINSIC_PROFILE_FUNCTION EXTRINSIC_PROFILE_FUNCTION
#define PROFILE_SCOPE            EXTRINSIC_PROFILE_SCOPE
#define PROFILE_FUNCTION         EXTRINSIC_PROFILE_FUNCTION

