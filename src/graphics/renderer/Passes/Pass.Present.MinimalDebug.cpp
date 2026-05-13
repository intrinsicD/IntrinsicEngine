module;

module Extrinsic.Graphics.Pass.Present.MinimalDebug;

import Extrinsic.RHI.CommandContext;

namespace Extrinsic::Graphics
{
    void MinimalDebugPresentPass::SetPipeline(const RHI::PipelineHandle pipeline) noexcept
    {
        m_Pipeline = pipeline;
    }

    void MinimalDebugPresentPass::Execute(RHI::ICommandContext& cmd)
    {
        if (!m_Pipeline.IsValid())
        {
            return;
        }

        cmd.BindPipeline(m_Pipeline);
        cmd.Draw(3u, 1u, 0u, 0u);
    }
}
