module;

module Extrinsic.Graphics.Pass.PostProcess.FXAA;

namespace Extrinsic::Graphics
{
	PostProcessFXAAPushConstants BuildPostProcessFXAAPushConstants(
		const PostProcessSettings& settings,
		const float viewportWidth,
		const float viewportHeight) noexcept
	{
		PostProcessFXAAPushConstants pc{};
		pc.InvResolution[0] = viewportWidth > 0.0f ? 1.0f / viewportWidth : 0.0f;
		pc.InvResolution[1] = viewportHeight > 0.0f ? 1.0f / viewportHeight : 0.0f;
		// FXAA 3.11 quality defaults documented in `assets/shaders/post_fxaa.frag`.
		// Future `PostProcessSettings::FXAA*` fields can plug in here without
		// touching the pass body or pipeline desc.
		pc.ContrastThreshold = 0.0312f;
		pc.RelativeThreshold = 0.063f;
		pc.SubpixelBlending = 0.75f;
		(void)settings;
		return pc;
	}

	void PostProcessFXAAPass::SetPipeline(const RHI::PipelineHandle pipeline) noexcept
	{
		m_Pipeline = pipeline;
	}

	void PostProcessFXAAPass::Execute(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera)
	{
		if (!m_PostProcessSystem.IsInitialized() || !m_Pipeline.IsValid() ||
		    !m_PostProcessSystem.IsStageEnabled(PostProcessStageKind::FXAA))
		{
			return;
		}

		const PostProcessFXAAPushConstants pc =
			BuildPostProcessFXAAPushConstants(m_PostProcessSystem.GetSettings(),
			                                  camera.ViewportWidth,
			                                  camera.ViewportHeight);
		cmd.BindPipeline(m_Pipeline);
		cmd.PushConstants(&pc, sizeof(pc));
		cmd.Draw(3u, 1u, 0u, 0u);
	}
}
