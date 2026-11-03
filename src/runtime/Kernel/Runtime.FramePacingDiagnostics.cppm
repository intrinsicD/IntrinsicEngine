// Per-frame timings and counters shared by runtime phases and diagnostic readers.
module;

#include <cstdint>

export module Extrinsic.Runtime.FramePacingDiagnostics;

namespace Extrinsic::Runtime
{
    export struct RuntimeFramePacingDiagnostics
    {
        bool Valid{false};
        bool PlatformContinueFrame{false};
        bool RendererBeganFrame{false};
        bool RendererCompletedFrame{false};
        std::uint64_t FrameIndex{0u};
        std::uint64_t TotalMicros{0u};
        std::uint64_t PlatformBeginMicros{0u};
        std::uint64_t ResizeMicros{0u};
        std::uint64_t OperationalTransitionMicros{0u};
        std::uint64_t FixedStepMicros{0u};
        std::uint64_t ImGuiBeginMicros{0u};
        std::uint64_t VariableTickMicros{0u};
        std::uint64_t ImGuiEndMicros{0u};
        std::uint64_t ImGuiEditorCallbackMicros{0u};
        std::uint64_t ImGuiDrawDataCopyMicros{0u};
        std::uint32_t ImGuiDrawListCount{0u};
        std::uint32_t ImGuiVertexCount{0u};
        std::uint32_t ImGuiIndexCount{0u};
        std::uint32_t ImGuiCommandCount{0u};
        std::uint32_t ImGuiFontAtlasCopyCount{0u};
        std::uint32_t ImGuiFontAtlasReuseCount{0u};
        bool          ImGuiFontAtlasCopied{false};
        bool          ImGuiFrameUsedUserTexture{false};
        std::uint64_t ImGuiFontAtlasByteCount{0u};
        std::uint64_t ImGuiFontAtlasCopyBytes{0u};
        std::uint64_t ImGuiVertexCopyBytes{0u};
        std::uint64_t ImGuiIndexCopyBytes{0u};
        std::uint64_t ImGuiCommandCopyBytes{0u};
        std::uint64_t ImGuiOverlayCopyBytes{0u};
        std::uint64_t PreRenderSetupMicros{0u};
        std::uint64_t PreRenderTransformFlushMicros{0u};
        bool          PreRenderTransformFlushRan{false};
        std::uint32_t PreRenderTransformWorldUpdatedObserved{0u};
        std::uint32_t PreRenderTransformDirtyTransformStamped{0u};
        std::uint32_t PreRenderTransformWorldUpdatedCleared{0u};
        std::uint64_t SelectionPickDrainMicros{0u};
        std::uint64_t RenderContractMicros{0u};
        std::uint64_t RenderBeginFrameMicros{0u};
        std::uint64_t RenderExtractionMicros{0u};
        std::uint64_t RenderPrepareMicros{0u};
        std::uint64_t RenderExecuteMicros{0u};
        std::uint64_t RenderEndFrameMicros{0u};
        std::uint64_t RenderGraphCompileMicros{0u};
        std::uint64_t RenderGraphExecuteMicros{0u};
        std::uint64_t PresentMicros{0u};
        std::uint64_t MaintenanceMicros{0u};
        // ARCH-022 R7: the main-thread intervals where algorithm runs do their frame work, each one call of the
        // frame: the command drain, the job completion drain (step-run pumps and job publications), the transfer
        // queue's readback deliveries, the GPU queue participants' transfer drain, and their frame recording.
        std::uint64_t CommandDrainMicros{0u};
        std::uint64_t JobCompletionDrainMicros{0u};
        std::uint64_t TransferCollectMicros{0u};
        std::uint64_t GpuTransferDrainMicros{0u};
        std::uint64_t GpuQueueRecordMicros{0u};
        std::uint64_t SelectionReadbackMicros{0u};
        std::uint64_t ReleaseRenderWorldMicros{0u};
    };

}
