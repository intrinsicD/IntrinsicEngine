module;

module Extrinsic.Graphics.Pass.PostProcess.SMAA;

namespace Extrinsic::Graphics
{
    void PostProcessSMAAPass::SetPipeline(const RHI::PipelineHandle pipeline) noexcept
    {
        m_Pipeline = pipeline;
    }

    void PostProcessSMAAPass::Execute(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera)
    {
        (void)camera;
        if (!m_PostProcessSystem.IsInitialized() || !m_Pipeline.IsValid() ||
            !m_PostProcessSystem.IsStageEnabled(PostProcessStageKind::SMAA))
        {
            return;
        }

        const PostProcessPushConstants pc = m_PostProcessSystem.BuildPushConstants(PostProcessStageKind::SMAA);
        cmd.BindPipeline(m_Pipeline);
        cmd.PushConstants(&pc, sizeof(pc));
        cmd.Draw(3u, 1u, 0u, 0u);
    }
}

