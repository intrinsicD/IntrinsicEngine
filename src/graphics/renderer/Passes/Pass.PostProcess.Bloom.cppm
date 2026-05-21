module;

#include <cstddef>
#include <cstdint>

export module Extrinsic.Graphics.Pass.PostProcess.Bloom;

import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Types;
import Extrinsic.Graphics.PostProcessSystem;

namespace Extrinsic::Graphics
{
	// GRAPHICS-075 Slice B.2 — canonical bloom mip-chain depth shared by the
	// recipe-side `PostProcess.BloomScratch` declaration and the per-mip
	// iteration in `PostProcessBloomPass::Execute`. The cap of six mips
	// matches `docs/architecture/rendering-three-pass.md` ("capped at six
	// mips, truncating at extents below 8x8"); centralising it keeps the
	// recipe's `RHI::TextureDesc::MipLevels` and the pass's iteration count
	// in lock-step so a future cap change touches one site instead of two.
	export inline constexpr std::uint32_t kBloomMipChainLevels = 6u;

	// GRAPHICS-075 Slice B.1 — pass-local push-constant block mirroring the
	// `assets/shaders/post_bloom_downsample.frag` `layout(push_constant) Push`
	// declaration byte-for-byte under Vulkan std430. `vec2` is 8-byte aligned
	// (size 8) so the trailing `float Threshold` + `int IsFirstMip` pack
	// directly behind it for a total of 16 bytes. The canonical 20-byte
	// `PostProcessPushConstants` block shared by stages that don't need
	// per-mip state is intentionally not used here: under std430 it aliases
	// `Gamma` onto `Threshold` (2.2 -> threshold) and `BloomIntensity` onto
	// `IsFirstMip` (`bit_cast<int>(0.05f)` ≈ 1.04e9 → always-first-mip), with
	// the remaining bytes reading past the shader's declared block.
	export struct PostProcessBloomDownsamplePushConstants
	{
		float InvSrcResolution[2]{0.0f, 0.0f}; // 1 / source mip dimensions
		float Threshold{1.0f};                 // brightness threshold (mip 0 only)
		std::int32_t IsFirstMip{0};            // 1 = apply threshold, 0 = pass-through
	};

	static_assert(sizeof(PostProcessBloomDownsamplePushConstants) == 16u,
		"PostProcessBloomDownsamplePushConstants must mirror post_bloom_downsample.frag std430 push block (16 bytes).");
	static_assert(offsetof(PostProcessBloomDownsamplePushConstants, Threshold) == 8u,
		"Threshold must follow vec2 InvSrcResolution per post_bloom_downsample.frag.");
	static_assert(offsetof(PostProcessBloomDownsamplePushConstants, IsFirstMip) == 12u,
		"IsFirstMip must follow float Threshold per post_bloom_downsample.frag.");

	// GRAPHICS-075 Slice B.1 — pass-local push-constant block mirroring the
	// `assets/shaders/post_bloom_upsample.frag` `layout(push_constant) Push`
	// declaration byte-for-byte under Vulkan std430. Same 16-byte shape as
	// the downsample block, but the field semantics differ (`FilterRadius`
	// is a tent-filter radius multiplier rather than a brightness threshold,
	// and the trailing scalar is a float pad rather than an int flag).
	export struct PostProcessBloomUpsamplePushConstants
	{
		float InvCoarserResolution[2]{0.0f, 0.0f}; // 1 / coarser mip dimensions
		float FilterRadius{1.0f};                  // tent-filter radius multiplier
		float _pad0{0.0f};
	};

	static_assert(sizeof(PostProcessBloomUpsamplePushConstants) == 16u,
		"PostProcessBloomUpsamplePushConstants must mirror post_bloom_upsample.frag std430 push block (16 bytes).");
	static_assert(offsetof(PostProcessBloomUpsamplePushConstants, FilterRadius) == 8u,
		"FilterRadius must follow vec2 InvCoarserResolution per post_bloom_upsample.frag.");

	// Builds the downsample push payload for a given mip's source resolution.
	// Slice B.1 records a single placeholder downsample with `IsFirstMip = 1`
	// so the first-mip thresholding code path is exercised; Slice B.2's
	// per-mip iteration calls this once per downsample step with the matching
	// `IsFirstMip` flag (1 for mip 0 → mip 1, 0 for the rest).
	export PostProcessBloomDownsamplePushConstants BuildPostProcessBloomDownsamplePushConstants(
		const PostProcessSettings& settings,
		std::uint32_t srcWidth,
		std::uint32_t srcHeight,
		bool isFirstMip) noexcept;

	// Builds the upsample push payload for a given coarser mip's resolution.
	// Slice B.1 records a single placeholder upsample with `FilterRadius =
	// 1.0`; Slice B.2's per-mip iteration calls this once per upsample step
	// with the coarser mip's inverse dimensions.
	export PostProcessBloomUpsamplePushConstants BuildPostProcessBloomUpsamplePushConstants(
		const PostProcessSettings& settings,
		std::uint32_t coarserWidth,
		std::uint32_t coarserHeight) noexcept;

	export class PostProcessBloomPass
	{
	public:
		explicit PostProcessBloomPass(PostProcessSystem& postProcess) : m_PostProcessSystem(postProcess) {}

		PostProcessBloomPass(const PostProcessBloomPass&)            = delete;
		PostProcessBloomPass& operator=(const PostProcessBloomPass&) = delete;

		void SetDownsamplePipeline(RHI::PipelineHandle pipeline) noexcept;
		void SetUpsamplePipeline(RHI::PipelineHandle pipeline) noexcept;
		// GRAPHICS-075 Slice B.2 — receives the per-frame `PostProcess.BloomScratch`
		// transient handle so `Execute(...)` can emit the inline
		// `ColorAttachment ↔ ShaderRead` barriers between mip iterations.
		// The handle is transient (re-published each frame by the renderer
		// after framegraph compile). When unset (default-constructed) the
		// pass still records the per-mip bind/push/draw sequence but skips
		// the barriers — the contract test sets a synthetic handle to drive
		// the barrier-sequence assertion, and the renderer publishes the
		// real transient handle just before invoking `Execute(...)`.
		void SetBloomScratch(RHI::TextureHandle texture) noexcept;
		void Execute(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera);

	private:
		PostProcessSystem& m_PostProcessSystem;
		RHI::PipelineHandle m_DownsamplePipeline{};
		RHI::PipelineHandle m_UpsamplePipeline{};
		RHI::TextureHandle m_BloomScratch{};
	};
}
