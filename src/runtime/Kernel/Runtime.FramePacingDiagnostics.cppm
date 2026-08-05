module;

#include <cstdint>

export module Extrinsic.Runtime.FramePacingDiagnostics;

import Extrinsic.Graphics.Renderer;

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
        std::uint64_t SelectionReadbackMicros{0u};
        std::uint64_t ReleaseRenderWorldMicros{0u};
    };

    export void MirrorRenderGraphFramePacingDiagnostics(
        RuntimeFramePacingDiagnostics& pacing,
        const Graphics::RenderGraphFrameStats& stats) noexcept;
}
