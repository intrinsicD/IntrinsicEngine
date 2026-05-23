module;

module Extrinsic.Graphics.Pass.TransientDebug.Surface;

import Extrinsic.RHI.CommandContext;

namespace Extrinsic::Graphics
{
    void TransientDebugSurfacePass::SetPipeline(const RHI::PipelineHandle pipeline) noexcept
    {
        m_Pipeline = pipeline;
    }

    void TransientDebugSurfacePass::Execute(RHI::ICommandContext& cmd)
    {
        // GRAPHICS-077 Slice A — scaffold-only body. The pass holds no
        // pipelines yet, so `m_Pipeline` is always invalid and the
        // executor never reaches this point with a valid bind/draw shape.
        // Slice B introduces the triangle pipelines + upload helper +
        // per-packet `BindPipeline + BindVertexBuffer + Draw(...)`
        // recording; Slice C extends to line + point lanes.
        (void)cmd;
        if (!m_Pipeline.IsValid())
        {
            return;
        }
    }
}
