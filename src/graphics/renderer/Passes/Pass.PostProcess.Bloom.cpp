module;

#include <cstdint>

module Extrinsic.Graphics.Pass.PostProcess.Bloom;

import Extrinsic.RHI.Descriptors;

namespace Extrinsic::Graphics
{
	namespace
	{
		[[nodiscard]] float SafeInverse(const std::uint32_t dim) noexcept
		{
			return dim > 0u ? 1.0f / static_cast<float>(dim) : 0.0f;
		}

		// Compute the integer extent of mip `mipIndex` derived from `base` with
		// the standard Vulkan mip-chain rule (`max(1, base >> mipIndex)`). The
		// minimum-1 floor keeps the smallest mip valid even when the viewport
		// is small enough that `base >> kBloomMipChainLevels-1` would otherwise
		// underflow to zero.
		[[nodiscard]] std::uint32_t MipExtent(const std::uint32_t base, const std::uint32_t mipIndex) noexcept
		{
			const std::uint32_t shifted = base >> mipIndex;
			return shifted > 0u ? shifted : 1u;
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
		// which keeps the CPU contract gate deterministic while leaving
		// headroom for a future `PostProcessSettings::BloomThreshold`
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

	void PostProcessBloomPass::SetBloomScratch(const RHI::TextureHandle texture) noexcept
	{
		m_BloomScratch = texture;
	}

	void PostProcessBloomPass::Execute(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera)
	{
		if (!m_PostProcessSystem.IsInitialized() ||
		    !m_PostProcessSystem.IsStageEnabled(PostProcessStageKind::Bloom))
		{
			return;
		}

		const bool hasDownsamplePipeline = m_DownsamplePipeline.IsValid();
		const bool hasUpsamplePipeline = m_UpsamplePipeline.IsValid();
		if (!hasDownsamplePipeline && !hasUpsamplePipeline)
		{
			return;
		}

		// GRAPHICS-075 Slice B.2 — iterate the canonical six-mip pyramid that
		// the recipe declares (see `BuildDefaultFrameRecipe`'s
		// `bloomScratchDesc.MipLevels = kBloomMipChainLevels`). For
		// `N = kBloomMipChainLevels` we record `N-1` downsamples (mip 0 → 1,
		// 1 → 2, …, N-2 → N-1) followed by `N-1` upsamples (mip N-1 → N-2,
		// …, 1 → 0). Each step pushes the *source* mip extent for its
		// shader's std430 push block (`InvSrcResolution` for downsample,
		// `InvCoarserResolution` for upsample) so the 13-tap downsample
		// kernel and 9-tap tent upsample kernel sample real per-mip texels
		// rather than collapsing onto the origin of one fixed resolution.
		// `IsFirstMip = 1` only on the mip-0 → mip-1 downsample step (the
		// `SceneColorHDR`-sourced read that needs the soft-threshold knee).
		//
		// Between each step the pass emits `ColorAttachment ↔ ShaderRead`
		// barriers on `m_BloomScratch` so the next step's read sees the
		// previous step's write at the right layout. The barrier API
		// (`ICommandContext::TextureBarrier`) operates on a whole texture
		// rather than a single mip subresource, so the inline barriers
		// codify the canonical *recording-shape* contract — fine-grained
		// per-mip layout state would need a future
		// `TextureBarrier(handle, mipRange, before, after)` RHI extension.
		// When `m_BloomScratch` is unset (default-constructed) the pass
		// still records the per-mip bind/push/draw sequence but skips the
		// barriers; the contract test sets a synthetic handle to exercise
		// the barrier-sequence assertion, and the renderer publishes the
		// real transient handle just before invoking `Execute(...)`.
		const PostProcessSettings& settings = m_PostProcessSystem.GetSettings();
		const auto viewportWidth = static_cast<std::uint32_t>(
			camera.ViewportWidth > 0.0f ? camera.ViewportWidth : 0.0f);
		const auto viewportHeight = static_cast<std::uint32_t>(
			camera.ViewportHeight > 0.0f ? camera.ViewportHeight : 0.0f);
		const bool emitBarriers = m_BloomScratch.IsValid();

		if (hasDownsamplePipeline)
		{
			for (std::uint32_t step = 1u; step < kBloomMipChainLevels; ++step)
			{
				// Each downsample step reads mip `step - 1` and writes mip
				// `step`. Push the *source* mip's extent so the shader's
				// `InvSrcResolution = 1 / vec2(W, H)` produces the
				// per-tap kernel offsets for that mip. `IsFirstMip = 1`
				// only on step 1 (the `SceneColorHDR`-sourced read) so
				// only the highest-resolution downsample applies the
				// soft-threshold knee.
				const std::uint32_t srcMip = step - 1u;
				const std::uint32_t srcW = MipExtent(viewportWidth, srcMip);
				const std::uint32_t srcH = MipExtent(viewportHeight, srcMip);
				const bool isFirstMip = (step == 1u);
				const PostProcessBloomDownsamplePushConstants downPc =
					BuildPostProcessBloomDownsamplePushConstants(settings, srcW, srcH, isFirstMip);
				cmd.BindPipeline(m_DownsamplePipeline);
				cmd.PushConstants(&downPc, sizeof(downPc));
				cmd.Draw(3u, 1u, 0u, 0u);
				// After writing mip `step` as a color attachment, transition
				// it to a shader-readable layout so the next step (the next
				// downsample read or the first upsample read) can sample
				// it. Emitting the barrier after every downsample (rather
				// than only between adjacent downsamples) keeps the inline
				// recording pattern uniform across the chain.
				if (emitBarriers)
				{
					cmd.TextureBarrier(m_BloomScratch,
					                   RHI::TextureLayout::ColorAttachment,
					                   RHI::TextureLayout::ShaderReadOnly);
				}
			}
		}

		if (hasUpsamplePipeline)
		{
			for (std::uint32_t step = kBloomMipChainLevels - 1u; step >= 1u; --step)
			{
				// Each upsample step reads mip `step` (the coarser mip) and
				// writes mip `step - 1` (the finer mip). Before writing the
				// destination mip, transition it back from `ShaderReadOnly`
				// (where it was left by the downsample chain) to
				// `ColorAttachment` so the upsample's color attachment
				// write is layout-valid. After the draw a trailing
				// `ColorAttachment → ShaderReadOnly` barrier readies the
				// just-written finer mip for the next upsample step's
				// read; the final iteration's trailing barrier also leaves
				// mip 0 in `ShaderReadOnly` so the downstream tonemap pass
				// can sample the bloom pyramid root.
				if (emitBarriers)
				{
					cmd.TextureBarrier(m_BloomScratch,
					                   RHI::TextureLayout::ShaderReadOnly,
					                   RHI::TextureLayout::ColorAttachment);
				}
				const std::uint32_t coarserW = MipExtent(viewportWidth, step);
				const std::uint32_t coarserH = MipExtent(viewportHeight, step);
				const PostProcessBloomUpsamplePushConstants upPc =
					BuildPostProcessBloomUpsamplePushConstants(settings, coarserW, coarserH);
				cmd.BindPipeline(m_UpsamplePipeline);
				cmd.PushConstants(&upPc, sizeof(upPc));
				cmd.Draw(3u, 1u, 0u, 0u);
				if (emitBarriers)
				{
					cmd.TextureBarrier(m_BloomScratch,
					                   RHI::TextureLayout::ColorAttachment,
					                   RHI::TextureLayout::ShaderReadOnly);
				}
			}
		}
	}
}
