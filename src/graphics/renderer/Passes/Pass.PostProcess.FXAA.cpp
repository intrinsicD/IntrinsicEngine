module;

module Extrinsic.Graphics.Pass.PostProcess.FXAA;

namespace Extrinsic::Graphics
{
	void PostProcessFXAAPass::SetPipeline(const RHI::PipelineHandle pipeline) noexcept
	{
		m_Pipeline = pipeline;
	}

	void PostProcessFXAAPass::Execute(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera)
	{
		(void)camera;
		if (!m_PostProcessSystem.IsInitialized() || !m_Pipeline.IsValid() ||
		    !m_PostProcessSystem.IsStageEnabled(PostProcessStageKind::FXAA))
		{
			return;
		}

		const PostProcessPushConstants pc = m_PostProcessSystem.BuildPushConstants(PostProcessStageKind::FXAA);
		cmd.BindPipeline(m_Pipeline);
		cmd.PushConstants(&pc, sizeof(pc));
		cmd.Draw(3u, 1u, 0u, 0u);
	}
}

