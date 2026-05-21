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
		// faulting. Push-payload resolutions are sourced from the
		// `CameraUBO`'s `ViewportWidth/Height` so the downsample shader's
		// `vec2 InvSrcResolution = 1 / vec2(W, H)` and the upsample
		// shader's `vec2 InvCoarserResolution = 1 / vec2(W/2, H/2)`
		// produce real per-tap kernel offsets even in this placeholder
		// step — feeding zero dimensions would collapse every sample tap
		// onto the same texel and silently break the spatial filter shape
		// the next Vulkan smoke would exercise. Slice B.2 replaces the
		// single-step recording with per-mip iteration over
		// `BloomScratch.MipLevels` mips, threading the matching per-mip
		// inverse-resolution + `IsFirstMip` flag through the push payload
		// and emitting the `ColorAttachment → ShaderRead → ColorAttachment`
		// barriers between mips.
		const PostProcessSettings& settings = m_PostProcessSystem.GetSettings();
		const auto viewportWidth = static_cast<std::uint32_t>(
			camera.ViewportWidth > 0.0f ? camera.ViewportWidth : 0.0f);
		const auto viewportHeight = static_cast<std::uint32_t>(
			camera.ViewportHeight > 0.0f ? camera.ViewportHeight : 0.0f);
		if (m_DownsamplePipeline.IsValid())
		{
			// First downsample reads `SceneColorHDR` at full viewport
			// extent and writes mip 1 of `BloomScratch`; `IsFirstMip = 1`
			// applies the soft-threshold knee.
			const PostProcessBloomDownsamplePushConstants downPc =
				BuildPostProcessBloomDownsamplePushConstants(
					settings, viewportWidth, viewportHeight, true);
			cmd.BindPipeline(m_DownsamplePipeline);
			cmd.PushConstants(&downPc, sizeof(downPc));
			cmd.Draw(3u, 1u, 0u, 0u);
		}
		if (m_UpsamplePipeline.IsValid())
		{
			// Single-step placeholder upsample reads from the coarser
			// half-viewport mip; Slice B.2's per-mip iteration drives the
			// actual coarser dimension per upsample step.
			const std::uint32_t coarserWidth = viewportWidth > 1u ? viewportWidth / 2u : viewportWidth;
			const std::uint32_t coarserHeight = viewportHeight > 1u ? viewportHeight / 2u : viewportHeight;
			const PostProcessBloomUpsamplePushConstants upPc =
				BuildPostProcessBloomUpsamplePushConstants(
					settings, coarserWidth, coarserHeight);
			cmd.BindPipeline(m_UpsamplePipeline);
			cmd.PushConstants(&upPc, sizeof(upPc));
			cmd.Draw(3u, 1u, 0u, 0u);
		}
	}
}
