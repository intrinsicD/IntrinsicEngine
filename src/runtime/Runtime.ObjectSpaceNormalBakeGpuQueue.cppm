module;

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

export module Extrinsic.Runtime.ObjectSpaceNormalBakeGpuQueue;

import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Graphics.ObjectSpaceNormalTextureBake;
import Extrinsic.RHI.CommandContext;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.ObjectSpaceNormalBakeBinding;
import Extrinsic.Runtime.ObjectSpaceNormalBakeQueue;
import Extrinsic.Runtime.ObjectSpaceNormalBakeSubmission;
import Extrinsic.Runtime.RenderExtraction;

export namespace Extrinsic::Runtime
{
    enum class RuntimeObjectSpaceNormalBakeGpuQueuePlanStatus : std::uint8_t
    {
        Ready,
        NotReady,
        Invalid,
    };

    struct RuntimeObjectSpaceNormalBakeGpuQueuePlanResult
    {
        RuntimeObjectSpaceNormalBakeGpuQueuePlanStatus Status{
            RuntimeObjectSpaceNormalBakeGpuQueuePlanStatus::NotReady};
        Graphics::ObjectSpaceNormalTextureBakePlan Plan{};
        std::string Diagnostic{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == RuntimeObjectSpaceNormalBakeGpuQueuePlanStatus::Ready &&
                   Plan.Succeeded();
        }
    };

    using RuntimeObjectSpaceNormalBakePlanProvider =
        std::function<RuntimeObjectSpaceNormalBakeGpuQueuePlanResult(
            const RuntimeObjectSpaceNormalBakeSubmission&)>;

    using RuntimeObjectSpaceNormalBakeReadyFrameProvider =
        std::function<std::uint64_t()>;

    struct RuntimeObjectSpaceNormalBakeGpuQueueDependencies
    {
        Graphics::GpuAssetCache* GpuAssets{};
        RenderExtractionCache* RenderExtraction{};
        RuntimeObjectSpaceNormalBakePlanProvider BuildPlan{};
        RuntimeObjectSpaceNormalBakeReadyFrameProvider ReadyFrame{};
        std::size_t MaxSubmissionsPerFrame{1u};
        std::size_t MaxBindingsPerDrain{8u};
    };

    struct RuntimeObjectSpaceNormalBakeGpuQueueDiagnostics
    {
        std::uint64_t PendingStaleDiscards{0u};
        std::uint64_t PlanNotReady{0u};
        std::uint64_t PlanRejected{0u};
        std::uint64_t CacheRejected{0u};
        std::uint64_t RecordedSubmissions{0u};
        std::uint64_t RecordFailures{0u};
        std::uint64_t ReadyFrameFailures{0u};
        std::uint64_t WaitingBindings{0u};
        std::uint64_t BoundCompletions{0u};
        std::uint64_t StaleCompletions{0u};
        std::uint64_t InvalidCompletions{0u};
        std::uint64_t ShutdownDiscards{0u};
        std::uint64_t LastRecordAttempted{0u};
        std::uint64_t LastRecordSubmitted{0u};
        std::uint64_t LastDrainProcessed{0u};
        std::uint64_t LastDrainBound{0u};
    };

    class RuntimeObjectSpaceNormalBakeGpuQueue
    {
    public:
        RuntimeObjectSpaceNormalBakeGpuQueue() = default;

        RuntimeObjectSpaceNormalBakeGpuQueue(
            const RuntimeObjectSpaceNormalBakeGpuQueue&) = delete;
        RuntimeObjectSpaceNormalBakeGpuQueue& operator=(
            const RuntimeObjectSpaceNormalBakeGpuQueue&) = delete;

        void SetDependencies(RuntimeObjectSpaceNormalBakeGpuQueueDependencies deps);

        [[nodiscard]] RuntimeObjectSpaceNormalBakeQueue& Queue() noexcept;
        [[nodiscard]] const RuntimeObjectSpaceNormalBakeQueue& Queue() const noexcept;
        [[nodiscard]] const RuntimeObjectSpaceNormalBakeGpuQueueDiagnostics&
            Diagnostics() const noexcept;

        [[nodiscard]] GpuQueueParticipantDesc MakeGpuQueueParticipantDesc();

        void RecordFrameCommands(RHI::ICommandContext& commandContext);
        [[nodiscard]] std::uint64_t DrainCompletedTransfers();
        [[nodiscard]] bool HasInFlightWork() const noexcept;
        void ShutdownAfterDeviceIdle();

    private:
        RuntimeObjectSpaceNormalBakeQueue m_Queue{};
        RuntimeObjectSpaceNormalBakeGpuQueueDependencies m_Dependencies{};
        std::vector<RuntimeObjectSpaceNormalBakeGpuSubmissionTicket> m_InFlight{};
        RuntimeObjectSpaceNormalBakeGpuQueueDiagnostics m_Diagnostics{};
    };
}
