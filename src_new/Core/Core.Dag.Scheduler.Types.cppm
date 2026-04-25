module;

#include <cstddef>
#include <cstdint>
#include <span>

export module Extrinsic.Core.Dag.Scheduler:Types;

import Extrinsic.Core.StrongHandle;

export namespace Extrinsic::Core::Dag
{
    struct ProducerTag;
    struct TaskTag;
    struct ResourceTag;
    struct LabelTag;

    using ProducerId = StrongHandle<ProducerTag>;
    using TaskId = StrongHandle<TaskTag>;
    using ResourceId = StrongHandle<ResourceTag>;
    using LabelId = StrongHandle<LabelTag>;

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

    struct PendingTaskDesc
    {
        TaskId id{};
        QueueDomain domain = QueueDomain::Cpu;
        TaskKind kind = TaskKind::Generic;
        TaskPriority priority = TaskPriority::Normal;
        uint32_t estimatedCost = 1;
        uint32_t cancellationGeneration = 0;
        std::span<const TaskId> dependsOn{};
        std::span<const ResourceAccess> resources{};
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

    struct ScheduleStats
    {
        uint32_t producerCount = 0;
        uint32_t taskCount = 0;
        uint32_t edgeCount = 0;
        uint32_t criticalPathCost = 0;
        uint32_t maxReadyQueueDepth = 0;
    };
}
