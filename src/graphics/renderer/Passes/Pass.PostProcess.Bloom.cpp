module;

#include <cstdint>

module Extrinsic.Graphics.Pass.PostProcess.Bloom;

namespace Extrinsic::Graphics
{
	namespace
	{
		[[nodiscard]] float SafeInverse(const std::uint32_t dim) noexcept
		{
			return dim > 0u ? 1.0f / static_cast<float>(dim) : 0.0f;
		}
	}

	PostProcessBloomDownsamplePushConstants BuildPostProcessBloomDownsamplePushConstants(
		const PostProcessSettings& settings,
		const std::uint32_t srcWidth,
		const std::uint32_t srcHeight,
		const bool isFirstMip) noexcept
	{
		PostProcessBloomDownsamplePushConstants pc{};
		pc.InvSrcResolution[0] = SafeInverse(srcWidth);
		pc.InvSrcResolution[1] = SafeInverse(srcHeight);
		// `BloomIntensity` doubles as the soft-threshold control in the
		// downsample shader: a non-zero intensity implies the chain is
		// active and the first mip should clip near-black highlights so the
		// upsample tent filter doesn't smear noise across the pyramid. The
		// canonical default of 0.05 maps to a near-pass-through threshold
		// of 1.0 (the shader's `SoftThreshold` knee is gentle around 1.0)
		// which keeps Slice B.1's CPU contract gate deterministic while
		// leaving headroom for Slice B.2's per-mip iteration to drive the
		// threshold from a future `PostProcessSettings::BloomThreshold`
		// field.
		pc.Threshold = 1.0f;
		pc.IsFirstMip = isFirstMip ? 1 : 0;
		(void)settings;
		return pc;
	}

	PostProcessBloomUpsamplePushConstants BuildPostProcessBloomUpsamplePushConstants(
		const PostProcessSettings& settings,
		const std::uint32_t coarserWidth,
		const std::uint32_t coarserHeight) noexcept
	{
		PostProcessBloomUpsamplePushConstants pc{};
		pc.InvCoarserResolution[0] = SafeInverse(coarserWidth);
		pc.InvCoarserResolution[1] = SafeInverse(coarserHeight);
		pc.FilterRadius = 1.0f;
		(void)settings;
		return pc;
	}

	void PostProcessBloomPass::SetDownsamplePipeline(const RHI::PipelineHandle pipeline) noexcept
	{
		m_DownsamplePipeline = pipeline;
	}

	void PostProcessBloomPass::SetUpsamplePipeline(const RHI::PipelineHandle pipeline) noexcept
	{
		m_UpsamplePipeline = pipeline;
	}

	void PostProcessBloomPass::Execute(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera)
	{
		(void)camera;
		if (!m_PostProcessSystem.IsInitialized() ||
		    !m_PostProcessSystem.IsStageEnabled(PostProcessStageKind::Bloom))
		{
			return;
		}

		// GRAPHICS-075 Slice B.1 — placeholder per-stage recording. The pass
		// records one downsample bind/push/draw (when the downsample
		// pipeline is available) and one upsample bind/push/draw (when the
		// upsample pipeline is available). Both stages early-skip
		// independently so a partially-published lease pair (e.g. only the
		// downsample pipeline created) still surfaces structurally without
		// faulting. Slice B.2 replaces the single-step recording with
		// per-mip iteration over `BloomScratch.MipLevels` mips, threading
		// the matching inverse-resolution and `IsFirstMip` flag through the
		// push payload and emitting the
		// `ColorAttachment → ShaderRead → ColorAttachment` barriers between
		// mips.
		const PostProcessSettings& settings = m_PostProcessSystem.GetSettings();
		if (m_DownsamplePipeline.IsValid())
		{
			const PostProcessBloomDownsamplePushConstants downPc =
				BuildPostProcessBloomDownsamplePushConstants(settings, 0u, 0u, true);
			cmd.BindPipeline(m_DownsamplePipeline);
			cmd.PushConstants(&downPc, sizeof(downPc));
			cmd.Draw(3u, 1u, 0u, 0u);
		}
		if (m_UpsamplePipeline.IsValid())
		{
			const PostProcessBloomUpsamplePushConstants upPc =
				BuildPostProcessBloomUpsamplePushConstants(settings, 0u, 0u);
			cmd.BindPipeline(m_UpsamplePipeline);
			cmd.PushConstants(&upPc, sizeof(upPc));
			cmd.Draw(3u, 1u, 0u, 0u);
		}
	}
}
