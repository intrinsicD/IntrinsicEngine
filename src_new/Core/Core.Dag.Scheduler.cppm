module;

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <memory>

export module Extrinsic.Core.DagScheduler;

import Extrinsic.Core.Error;
import Extrinsic.Core.StrongHandle;

export namespace Extrinsic::Core::Dag
{
    struct ProducerTag;
    struct TaskTag;
    struct ResourceTag;

    using ProducerId = StrongHandle<ProducerTag>;
    using TaskId = StrongHandle<TaskTag>;
    using ResourceId = StrongHandle<ResourceTag>;

    enum class QueueDomain : uint8_t
    {
        Cpu = 0,
        Gpu = 1,
        Streaming = 2,
    };

    enum class TaskKind : uint8_t
    {
        Generic = 0,
        AssetIO,
        AssetDecode,
        AssetUpload,
        GeometryProcess,
        PhysicsStep,
        RenderPass,
    };

    enum class TaskPriority : uint8_t
    {
        Critical = 0,
        High,
        Normal,
        Low,
        Background,
    };

    enum class ResourceAccessMode : uint8_t
    {
        Read = 0,
        Write,
        ReadWrite,
    };

    struct ResourceAccess
    {
        ResourceId resource{};
        ResourceAccessMode mode = ResourceAccessMode::Read;
    };

    // Compact descriptor emitted by producer systems.
    // This is immutable once emitted for the current scheduling epoch.
    struct PendingTaskDesc
    {
        TaskId id{};
        QueueDomain domain = QueueDomain::Cpu;
        TaskKind kind = TaskKind::Generic;
        TaskPriority priority = TaskPriority::Normal;
        uint32_t estimatedCost = 1; // Relative cost units for balancing.
        uint32_t cancellationGeneration = 0;
        std::span<const TaskId> dependsOn{};
        std::span<const ResourceAccess> resources{};
    };

    struct ProducerInfo
    {
        std::string_view name{};
        uint32_t subsystemId = 0;
        QueueDomain preferredDomain = QueueDomain::Cpu;
    };

    struct BuildConfig
    {
        uint32_t workerCountCpu = 0;
        uint32_t queueBudgetCpu = 0;
        uint32_t queueBudgetGpu = 0;
        uint32_t queueBudgetStreaming = 0;
        uint64_t frameIndex = 0;
    };

    struct PlanTask
    {
        TaskId id{};
        QueueDomain domain = QueueDomain::Cpu;
        uint32_t lane = 0;
        uint32_t topoOrder = 0;
        uint32_t batch = 0;
    };

    struct SchedulePlanView
    {
        std::span<const PlanTask> orderedTasks{};
    };

    struct ScheduleStats
    {
        uint32_t producerCount = 0;
        uint32_t taskCount = 0;
        uint32_t edgeCount = 0;
        uint32_t criticalPathCost = 0;
        uint32_t maxReadyQueueDepth = 0;
    };

    // Query callback:
    //  - producerCtx is owned by subsystem
    //  - emitCtx is scheduler-owned temporary context
    //  - emit should be called once per pending task
    using EmitPendingTaskFn = bool(*)(void* emitCtx, const PendingTaskDesc&);
    using QueryPendingTasksFn = Result(*)(void* producerCtx, void* emitCtx, EmitPendingTaskFn emit);

    class DagScheduler
    {
    public:
        DagScheduler() = default;
        DagScheduler(const DagScheduler&) = delete;
        DagScheduler& operator=(const DagScheduler&) = delete;

        [[nodiscard]] virtual Expected<ProducerId> RegisterProducer(
            ProducerInfo info,
            void* producerCtx,
            QueryPendingTasksFn queryFn) = 0;

        virtual Result UnregisterProducer(ProducerId producer) = 0;

        // Calls every registered producer query callback and caches a snapshot
        // of all pending tasks for the current epoch.
        virtual Result QueryAllPending() = 0;

        // Builds a complete topological schedule over the currently cached
        // pending-task snapshot.
        [[nodiscard]] virtual Expected<SchedulePlanView> BuildSchedule(const BuildConfig& config) = 0;

        [[nodiscard]] virtual ScheduleStats GetLastStats() const = 0;

        virtual void ResetEpoch() = 0;

        virtual ~DagScheduler() = default;
    };

    class CpuTaskGraph
    {
    public:
        virtual Result Submit(const PendingTaskDesc& task) = 0;
        [[nodiscard]] virtual Expected<SchedulePlanView> BuildPlan(const BuildConfig& config) = 0;
        virtual void Reset() = 0;
        virtual ~CpuTaskGraph() = default;
    };

    class GpuFrameGraph
    {
    public:
        virtual Result Submit(const PendingTaskDesc& pass) = 0;
        [[nodiscard]] virtual Expected<SchedulePlanView> BuildPlan(const BuildConfig& config) = 0;
        virtual void Reset() = 0;
        virtual ~GpuFrameGraph() = default;
    };

    class AsyncStreamingGraph
    {
    public:
        virtual Result Submit(const PendingTaskDesc& task) = 0;
        [[nodiscard]] virtual Expected<SchedulePlanView> BuildPlan(const BuildConfig& config) = 0;
        virtual void Reset() = 0;
        virtual ~AsyncStreamingGraph() = default;
    };

    [[nodiscard]] std::unique_ptr<DagScheduler> CreateDagScheduler();
    [[nodiscard]] std::unique_ptr<CpuTaskGraph> CreateCpuTaskGraph();
    [[nodiscard]] std::unique_ptr<GpuFrameGraph> CreateGpuFrameGraph();
    [[nodiscard]] std::unique_ptr<AsyncStreamingGraph> CreateAsyncStreamingGraph();
}
