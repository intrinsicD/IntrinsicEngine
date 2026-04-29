module;

#include <cstdint>
#include <expected>
#include <functional>
#include <string>
#include <vector>
#include <variant>

export module Extrinsic.Runtime.StreamingExecutor;

import Extrinsic.Core.Error;
import Extrinsic.Core.Dag.Scheduler;
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

    struct StreamingCpuPayloadReady
    {
        std::uint64_t PayloadToken = 0;
    };

    struct StreamingGpuUploadRequest
    {
        std::uint64_t PayloadToken = 0;
        std::uint64_t ByteSize = 0;
    };

    using StreamingTaskValue = std::variant<Core::Unit, StreamingCpuPayloadReady, StreamingGpuUploadRequest>;
    using StreamingResult = std::expected<StreamingTaskValue, Core::ErrorCode>;

    struct StreamingTaskDesc
    {
        std::string Name{};
        Core::Dag::TaskKind Kind = Core::Dag::TaskKind::Generic;
        Core::Dag::TaskPriority Priority = Core::Dag::TaskPriority::Normal;
        std::uint32_t EstimatedCost = 1;
        std::uint64_t CancellationGeneration = 0;
        std::vector<StreamingTaskHandle> DependsOn{};
        std::move_only_function<StreamingResult()> Execute{};
        std::move_only_function<void(StreamingResult&&)> ApplyOnMainThread{};
    };

    class StreamingExecutor
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
