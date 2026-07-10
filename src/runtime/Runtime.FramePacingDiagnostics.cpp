module;

module Extrinsic.Runtime.FramePacingDiagnostics;

namespace Extrinsic::Runtime
{
    void MirrorImGuiFramePacingDiagnostics(
        RuntimeFramePacingDiagnostics& pacing,
        const ImGuiAdapterDiagnostics& imgui) noexcept
    {
        pacing.ImGuiEditorCallbackMicros = imgui.LastEditorCallbackMicros;
        pacing.ImGuiDrawDataCopyMicros = imgui.LastDrawDataCopyMicros;
        pacing.ImGuiDrawListCount = imgui.LastDrawListCount;
        pacing.ImGuiVertexCount = imgui.LastVertexCount;
        pacing.ImGuiIndexCount = imgui.LastIndexCount;
        pacing.ImGuiCommandCount = imgui.LastCommandCount;
        pacing.ImGuiFontAtlasCopyCount = imgui.FontAtlasCopyCount;
        pacing.ImGuiFontAtlasReuseCount = imgui.FontAtlasReuseCount;
        pacing.ImGuiFontAtlasCopied = imgui.LastFrameFontAtlasCopied;
        pacing.ImGuiFrameUsedUserTexture = imgui.LastFrameUsedUserTexture;
        pacing.ImGuiFontAtlasByteCount = imgui.LastFontAtlasByteCount;
        pacing.ImGuiFontAtlasCopyBytes = imgui.LastFrameFontAtlasCopyBytes;
        pacing.ImGuiVertexCopyBytes = imgui.LastFrameVertexCopyBytes;
        pacing.ImGuiIndexCopyBytes = imgui.LastFrameIndexCopyBytes;
        pacing.ImGuiCommandCopyBytes = imgui.LastFrameCommandCopyBytes;
        pacing.ImGuiOverlayCopyBytes = imgui.LastFrameOverlayCopyBytes;
    }

    void MirrorRenderGraphFramePacingDiagnostics(
        RuntimeFramePacingDiagnostics& pacing,
        const Graphics::RenderGraphFrameStats& stats) noexcept
    {
        pacing.RenderGraphCompileMicros = stats.Compile.TimeMicros;
        pacing.RenderGraphExecuteMicros = stats.Execute.TimeMicros;
    }
}
