module;

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

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

    struct TaskKind
    {
        uint8_t Value = 0;

        constexpr TaskKind() noexcept = default;
        constexpr explicit TaskKind(const uint8_t value) noexcept
            : Value(value) {}

        [[nodiscard]] friend constexpr bool operator==(TaskKind, TaskKind) noexcept = default;
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
        WeakRead,
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
        std::string_view debugName{};
        TaskKind kind{};
        TaskPriority priority = TaskPriority::Normal;
        uint32_t estimatedCost = 1;
        uint32_t cancellationGeneration = 0;
        std::span<const TaskId> dependsOn{};
        std::span<const ResourceAccess> resources{};
    };

    struct PlanTask
    {
        TaskId id{};
        uint32_t topoOrder = 0;
        uint32_t batch = 0;
    };

    struct ScheduleStats
    {
        uint32_t producerCount = 0;
        uint32_t taskCount = 0;
        uint32_t explicitEdgeCount = 0;
        uint32_t hazardEdgeCount = 0;
        uint32_t edgeCount = 0;
        uint32_t layerCount = 0;
        uint32_t criticalPathCost = 0;
        uint32_t maxReadyQueueDepth = 0;
        std::string lastDiagnostic{};
    };
}
