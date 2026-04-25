module;

#include <cstdint>
#include <expected>
#include <functional>
#include <string>
#include <vector>

export module Extrinsic.Runtime.StreamingExecutor;

import Extrinsic.Core.Error;
import Extrinsic.Core.Dag.Scheduler:Types;
import Extrinsic.Core.StrongHandle;

export namespace Extrinsic::Runtime
{
    struct StreamingTaskTag;
    using StreamingTaskHandle = Core::StrongHandle<StreamingTaskTag>;

    enum class StreamingTaskState : std::uint8_t
    {
        Pending,
        Ready,
        Running,
        WaitingForMainThreadApply,
        WaitingForGpuUpload,
        Complete,
        Failed,
        Cancelled
    };

    using StreamingResult = std::expected<Core::Unit, Core::ErrorCode>;

    struct StreamingTaskDesc
    {
        std::string Name{};
        Core::Dag::TaskKind Kind = Core::Dag::TaskKind::Generic;
        Core::Dag::TaskPriority Priority = Core::Dag::TaskPriority::Normal;
        std::uint32_t EstimatedCost = 1;
        std::vector<StreamingTaskHandle> DependsOn{};
        std::move_only_function<StreamingResult()> Execute{};
        std::move_only_function<void(StreamingResult&&)> ApplyOnMainThread{};
    };

    export class StreamingExecutor
    {
    public:
        StreamingExecutor();
        ~StreamingExecutor();

        StreamingExecutor(const StreamingExecutor&) = delete;
        StreamingExecutor& operator=(const StreamingExecutor&) = delete;

        [[nodiscard]] StreamingTaskHandle Submit(StreamingTaskDesc desc);
        void Cancel(StreamingTaskHandle handle);
        void PumpBackground(std::uint32_t maxLaunches);
        void DrainCompletions();
        void ApplyMainThreadResults();
        void ShutdownAndDrain();

        [[nodiscard]] StreamingTaskState GetState(StreamingTaskHandle handle) const;

    private:
        struct Impl;
        Impl* m_Impl{nullptr};
    };
}
