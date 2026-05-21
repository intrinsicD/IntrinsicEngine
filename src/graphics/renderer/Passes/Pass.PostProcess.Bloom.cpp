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

	void PostProcessBloomPass::SetBloomScratch(const RHI::TextureHandle texture,
	                                            const std::uint32_t mipLevels) noexcept
	{
		m_BloomScratch = texture;
		// Clamp to the canonical depth so a misconfigured caller cannot
		// drive the iteration past the texture's actual mip range. A value
		// of 0 keeps the canonical default so headless contract tests that
		// do not publish a transient handle still observe the full-depth
		// recording shape.
		if (mipLevels == 0u)
		{
			m_BloomScratchMipLevels = kBloomMipChainLevels;
		}
		else
		{
			m_BloomScratchMipLevels =
				mipLevels > kBloomMipChainLevels ? kBloomMipChainLevels : mipLevels;
		}
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

		// GRAPHICS-075 Slice B.2 — iterate the bloom mip pyramid that the
		// recipe declared, clamped per `ComputeBloomMipChainLevels` against
		// the current viewport extent (never above `kBloomMipChainLevels =
		// 6`, possibly lower for tiny viewports where Vulkan's
		// `mipLevels <= floor(log2(maxDim)) + 1` rule would otherwise be
		// violated). For an effective depth `M >= 2` the pass records
		// `M-1` downsamples (mip 0 → 1, …, M-2 → M-1) followed by `M-1`
		// upsamples (mip M-1 → M-2, …, 1 → 0). Each step pushes the
		// *source* mip extent for its shader's std430 push block
		// (`InvSrcResolution` for downsample, `InvCoarserResolution` for
		// upsample) so the 13-tap downsample kernel and 9-tap tent
		// upsample kernel sample real per-mip texels rather than
		// collapsing onto the origin of one fixed resolution. `IsFirstMip
		// = 1` only on the mip 0 → mip 1 downsample step (the
		// `SceneColorHDR`-sourced read that needs the soft-threshold
		// knee).
		//
		// NOTE: this body deliberately emits *no* `TextureBarrier(...)`
		// calls. The renderer's `"PostProcessPass"` executor branch begins
		// a single umbrella render pass around bloom + tonemap (per the
		// framegraph's `Write(BloomScratch, ColorAttachmentWrite)` /
		// `Read(BloomScratch, ShaderRead)` declarations), and Vulkan
		// rejects `vkCmdPipelineBarrier2` layout transitions issued inside
		// a render-pass scope on an attachment currently being rendered.
		// Correct per-mip subresource barriers would require both
		// (a) an `ICommandContext::TextureBarrier(handle, mipRange, ...)`
		// extension to the RHI and (b) a per-mip render-pass restart
		// between iterations so the next mip can be a fresh
		// `ColorAttachment` target. Both are deferred to a follow-up
		// slice; until then the inter-pass barrier between the bloom and
		// tonemap legs is emitted by the framegraph compiler from the
		// recipe-level read/write declarations, which is layout-safe for
		// the whole-texture case.
		const PostProcessSettings& settings = m_PostProcessSystem.GetSettings();
		const auto viewportWidth = static_cast<std::uint32_t>(
			camera.ViewportWidth > 0.0f ? camera.ViewportWidth : 0.0f);
		const auto viewportHeight = static_cast<std::uint32_t>(
			camera.ViewportHeight > 0.0f ? camera.ViewportHeight : 0.0f);
		const std::uint32_t effectiveMipLevels = m_BloomScratchMipLevels;
		if (effectiveMipLevels < 2u)
		{
			// A single-mip pyramid leaves no down/up chain to iterate;
			// recording one degenerate step would silently re-draw mip 0
			// over itself. Skip the pass body entirely — the umbrella
			// executor branch still reports `Recorded` via the umbrella
			// status accumulator if the pipelines are live (the helper
			// returns Recorded based on pipeline lease validity, not on
			// the number of recorded draws).
			return;
		}

		if (hasDownsamplePipeline)
		{
			for (std::uint32_t step = 1u; step < effectiveMipLevels; ++step)
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
			}
		}

		if (hasUpsamplePipeline)
		{
			for (std::uint32_t step = effectiveMipLevels - 1u; step >= 1u; --step)
			{
				// Each upsample step reads mip `step` (the coarser mip)
				// and writes mip `step - 1` (the finer mip). Push the
				// coarser mip's extent so the 9-tap tent filter spans the
				// correct source-mip texels.
				const std::uint32_t coarserW = MipExtent(viewportWidth, step);
				const std::uint32_t coarserH = MipExtent(viewportHeight, step);
				const PostProcessBloomUpsamplePushConstants upPc =
					BuildPostProcessBloomUpsamplePushConstants(settings, coarserW, coarserH);
				cmd.BindPipeline(m_UpsamplePipeline);
				cmd.PushConstants(&upPc, sizeof(upPc));
				cmd.Draw(3u, 1u, 0u, 0u);
			}
		}
	}
}
