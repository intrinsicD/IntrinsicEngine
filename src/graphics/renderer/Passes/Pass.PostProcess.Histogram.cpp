module;

module Extrinsic.Graphics.Pass.PostProcess.Histogram;

namespace Extrinsic::Graphics
{
    void PostProcessHistogramPass::SetPipeline(const RHI::PipelineHandle pipeline) noexcept
    {
        m_Pipeline = pipeline;
    }

    void PostProcessHistogramPass::Execute(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera)
    {
        (void)camera;
        if (!m_PostProcessSystem.IsInitialized() || !m_Pipeline.IsValid() ||
            !m_PostProcessSystem.IsStageEnabled(PostProcessStageKind::Histogram))
        {
            return;
        }

        const PostProcessPushConstants pc = m_PostProcessSystem.BuildPushConstants(PostProcessStageKind::Histogram);
        cmd.BindPipeline(m_Pipeline);
        cmd.PushConstants(&pc, sizeof(pc));
        cmd.Dispatch(1u, 1u, 1u);
    }
}

