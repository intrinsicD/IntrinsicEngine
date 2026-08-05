module;

module Extrinsic.Runtime.FramePacingDiagnostics;

namespace Extrinsic::Runtime
{
    void MirrorRenderGraphFramePacingDiagnostics(
        RuntimeFramePacingDiagnostics& pacing,
        const Graphics::RenderGraphFrameStats& stats) noexcept
    {
        pacing.RenderGraphCompileMicros = stats.Compile.TimeMicros;
        pacing.RenderGraphExecuteMicros = stats.Execute.TimeMicros;
    }
}
