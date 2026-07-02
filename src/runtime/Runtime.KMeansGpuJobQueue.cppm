module;

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

export module Extrinsic.Runtime.KMeansGpuJobQueue;

import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.TransferQueue;
import Extrinsic.Runtime.KMeansGpuBackend;
import Geometry.KMeans;

export namespace Extrinsic::Runtime
{
    enum class RuntimeKMeansGpuJobStatus : std::uint8_t
    {
        Idle,
        Accepted,
        Busy,
        InvalidInput,
        GpuUnavailable,
        PipelineUnavailable,
        RecordFailed,
        ReadbackPending,
        ReadbackFailed,
        Completed,
    };

    struct RuntimeKMeansGpuJobRequest
    {
        std::uint64_t Sequence{0u}; // 0 means "assign a sequence on submit".
        std::uint32_t StableEntityId{0u};
        std::uint32_t DomainTag{0u};
        std::vector<glm::vec3> Points{};
        std::vector<glm::vec3> InitialCentroids{};
        Geometry::KMeans::KMeansParams Params{};
    };

    struct RuntimeKMeansGpuJobSubmission
    {
        RuntimeKMeansGpuJobStatus Status{RuntimeKMeansGpuJobStatus::Idle};
        std::uint64_t Sequence{0u};
        KMeansGpuStatus GpuStatus{KMeansGpuStatus::Success};
        std::string Diagnostic{};

        [[nodiscard]] bool Accepted() const noexcept
        {
            return Status == RuntimeKMeansGpuJobStatus::Accepted;
        }
    };

    struct RuntimeKMeansGpuJobResult
    {
        RuntimeKMeansGpuJobStatus Status{RuntimeKMeansGpuJobStatus::Idle};
        std::uint64_t Sequence{0u};
        std::uint32_t StableEntityId{0u};
        std::uint32_t DomainTag{0u};
        KMeansGpuStatus GpuStatus{KMeansGpuStatus::Success};
        Geometry::KMeans::KMeansResult Result{};
        std::string Diagnostic{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == RuntimeKMeansGpuJobStatus::Completed;
        }
    };

    [[nodiscard]] const char* DebugNameForRuntimeKMeansGpuJobStatus(
        RuntimeKMeansGpuJobStatus status) noexcept;

    class RuntimeKMeansGpuJobQueue
    {
    public:
        RuntimeKMeansGpuJobQueue(RHI::IDevice& device,
                                 RHI::BufferManager& buffers,
                                 RHI::ITransferQueue& transferQueue);
        ~RuntimeKMeansGpuJobQueue();

        RuntimeKMeansGpuJobQueue(const RuntimeKMeansGpuJobQueue&) = delete;
        RuntimeKMeansGpuJobQueue& operator=(const RuntimeKMeansGpuJobQueue&) = delete;

        [[nodiscard]] RuntimeKMeansGpuJobSubmission Submit(
            RuntimeKMeansGpuJobRequest request);

        // Called from the renderer's already-open frame command context.
        // Advances at most one GPU phase so the editor command stays nonblocking
        // and no extra swapchain present is created.
        void AdvanceGpuWork(RHI::ICommandContext& commandContext);

        // Called after the owner drains ITransferQueue::CollectCompleted().
        void DrainCompletedTransfers();

        [[nodiscard]] std::optional<RuntimeKMeansGpuJobResult> ConsumeCompleted();
        [[nodiscard]] bool HasInFlightJob() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
