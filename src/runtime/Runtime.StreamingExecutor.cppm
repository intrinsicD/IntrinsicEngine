module;

#include <cstdint>
#include <expected>
#include <functional>
#include <span>
#include <string>
#include <vector>
#include <variant>

export module Extrinsic.Runtime.StreamingExecutor;

import Extrinsic.Core.Error;
import Extrinsic.Core.Dag.Scheduler;
import Extrinsic.Core.StrongHandle;
// RUNTIME-194 moved RuntimeTaskKinds to the surviving execution surface;
// re-exported so existing consumers keep compiling until this module retires.
export import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.WorldHandle;

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
        WaitingForReadback,
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

    struct StreamingReadbackRequest
    {
        std::uint64_t PayloadToken = 0;
        std::uint64_t ByteSize = 0;
    };

    using StreamingTaskValue = std::variant<Core::Unit,
                                            StreamingCpuPayloadReady,
                                            StreamingGpuUploadRequest,
                                            StreamingReadbackRequest>;
    using StreamingResult = std::expected<StreamingTaskValue, Core::ErrorCode>;

    struct StreamingTaskDesc
    {
        std::string Name{};
        Core::Dag::TaskKind Kind = RuntimeTaskKinds::Generic;
        Core::Dag::TaskPriority Priority = Core::Dag::TaskPriority::Normal;
        std::uint32_t EstimatedCost = 1;
        std::uint64_t CancellationGeneration = 0;
        WorldHandle Scope{DefaultWorldHandle};
        std::vector<StreamingTaskHandle> DependsOn{};
        std::move_only_function<StreamingResult()> Execute{};
        std::move_only_function<void(StreamingResult&&)> ApplyOnMainThread{};
        // Optional control-state reconciliation for cancellation. The
        // executor never routes a cancelled result through the normal apply
        // callback; instead it invokes this finalizer exactly once from
        // ApplyMainThreadResults(), outside the executor lock.
        std::move_only_function<void()> FinalizeCancellationOnMainThread{};
    };

    struct StreamingExecutorDiagnostics
    {
        std::uint32_t SlotCount = 0;
        std::uint32_t ActiveSlotCount = 0;
        std::uint32_t FreeSlotCount = 0;
        std::uint32_t ReadyTaskCount = 0;
        std::uint32_t ReadyForApplyCount = 0;
        std::uint32_t RunningCount = 0;
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
        [[nodiscard]] std::uint32_t RetireWorld(WorldHandle world);
        [[nodiscard]] bool IsWorldRetired(WorldHandle world) const;
        void PumpBackground(std::uint32_t maxLaunches);
        void DrainCompletions();
        [[nodiscard]] bool ResumeReadback(StreamingTaskHandle handle);
        void ApplyMainThreadResults();
        [[nodiscard]] std::uint32_t ApplyMainThreadResults(std::uint32_t maxApplyCount);
        void ShutdownAndDrain();

        [[nodiscard]] StreamingTaskState GetState(StreamingTaskHandle handle) const;
        [[nodiscard]] std::vector<StreamingTaskState>
            GetStates(std::span<const StreamingTaskHandle> handles) const;
        [[nodiscard]] StreamingExecutorDiagnostics GetDiagnostics() const;

    private:
        struct Impl;
        Impl* m_Impl{nullptr};
    };
}
