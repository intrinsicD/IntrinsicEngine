#pragma once

// Include-only Sandbox editor module glue. Include after the public job DTOs
// and required RHI/runtime module imports in Runtime.SandboxEditorFacades.cppm.

namespace Extrinsic::Runtime
{
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
