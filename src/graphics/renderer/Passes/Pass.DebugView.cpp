module;

module Extrinsic.Graphics.Pass.DebugView;

namespace Extrinsic::Graphics
{
    void DebugViewPass::SetPipeline(const RHI::PipelineHandle pipeline) noexcept
    {
        m_Pipeline = pipeline;
    }

    void DebugViewPass::Execute(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera)
    {
        (void)camera;
        const DebugViewResolvedSelection selection = m_DebugViewSystem.GetResolvedSelection();
        if (!m_DebugViewSystem.IsInitialized() || !m_Pipeline.IsValid() || !selection.Enabled)
        {
            return;
        }

        const DebugViewPushConstants pc = m_DebugViewSystem.BuildPushConstants();
        cmd.BindPipeline(m_Pipeline);
        cmd.PushConstants(&pc, sizeof(pc));
        cmd.Draw(3u, 1u, 0u, 0u);
    }
}

