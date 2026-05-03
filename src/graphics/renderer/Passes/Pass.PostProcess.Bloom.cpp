module;

module Extrinsic.Graphics.Pass.PostProcess.Bloom;

namespace Extrinsic::Graphics
{
	void PostProcessBloomPass::SetPipeline(const RHI::PipelineHandle pipeline) noexcept
	{
		m_Pipeline = pipeline;
	}

	void PostProcessBloomPass::Execute(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera)
	{
		(void)camera;
		if (!m_PostProcessSystem.IsInitialized() || !m_Pipeline.IsValid() ||
		    !m_PostProcessSystem.IsStageEnabled(PostProcessStageKind::Bloom))
		{
			return;
		}

		const PostProcessPushConstants pc = m_PostProcessSystem.BuildPushConstants(PostProcessStageKind::Bloom);
		cmd.BindPipeline(m_Pipeline);
		cmd.PushConstants(&pc, sizeof(pc));
		cmd.Draw(3u, 1u, 0u, 0u);
	}
}

