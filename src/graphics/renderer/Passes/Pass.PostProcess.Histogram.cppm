module;

#include <cstddef>
#include <cstdint>

export module Extrinsic.Graphics.Pass.PostProcess.Histogram;

import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Types;
import Extrinsic.Graphics.PostProcessSystem;

namespace Extrinsic::Graphics
{
	// GRAPHICS-075 Slice E.1 — pass-local push-constant block mirroring the
	// `assets/shaders/post_histogram.comp` `layout(push_constant) PushConstants`
	// declaration byte-for-byte under Vulkan std430. The shader's block is
	// `uint Width + uint Height + float MinLogLum + float RangeLogLum`,
	// each 4 bytes, packed tightly for a total of 16 bytes. The canonical
	// 20-byte `PostProcessPushConstants` block shared by stages that don't
	// need per-shader state is intentionally not used here per the standing
	// shader-push-constant compatibility policy: under std430 it would alias
	// `Exposure` (1.0) onto `Width` (`bit_cast<uint>(1.0f)` = 0x3F800000 ≈
	// 1.07e9 pixels wide), `Gamma` (2.2) onto `Height`, and `BloomIntensity`
	// (0.05) onto `MinLogLum`, producing a degenerate out-of-bounds dispatch
	// shape and a meaningless luminance histogram.
	export struct PostProcessHistogramPushConstants
	{
		std::uint32_t Width{0u};        // viewport width in pixels
		std::uint32_t Height{0u};       // viewport height in pixels
		float MinLogLum{-10.0f};        // log2(min luminance)
		float RangeLogLum{1.0f / 20.0f}; // 1.0 / (log2(maxLum) - log2(minLum))
	};

	static_assert(sizeof(PostProcessHistogramPushConstants) == 16u,
		"PostProcessHistogramPushConstants must mirror post_histogram.comp std430 push block (16 bytes).");
	static_assert(offsetof(PostProcessHistogramPushConstants, Height) == 4u,
		"Height must follow Width per post_histogram.comp.");
	static_assert(offsetof(PostProcessHistogramPushConstants, MinLogLum) == 8u,
		"MinLogLum must follow Height per post_histogram.comp.");
	static_assert(offsetof(PostProcessHistogramPushConstants, RangeLogLum) == 12u,
		"RangeLogLum must follow MinLogLum per post_histogram.comp.");

	// Builds the histogram push payload from the system's settings and the
	// runtime viewport extent. `MinLogLum` / `RangeLogLum` default to the
	// canonical `[-10, +10]` log-luminance range per `GRAPHICS-013AQ`; Slice
	// E.2's exposure adaptation overrides those bounds from the retained
	// `PostProcessExposureHistory` storage buffer once the readback drain
	// publishes the prior frame's average log luminance.
	export PostProcessHistogramPushConstants BuildPostProcessHistogramPushConstants(
		const PostProcessSettings& settings,
		std::uint32_t viewportWidth,
		std::uint32_t viewportHeight) noexcept;

	export class PostProcessHistogramPass
	{
	public:
		explicit PostProcessHistogramPass(PostProcessSystem& postProcess) : m_PostProcessSystem(postProcess) {}

		PostProcessHistogramPass(const PostProcessHistogramPass&)            = delete;
		PostProcessHistogramPass& operator=(const PostProcessHistogramPass&) = delete;

		void SetPipeline(RHI::PipelineHandle pipeline) noexcept;

		// GRAPHICS-075 Slice E.1 — published by the executor before
		// `Execute(...)` so the compute dispatch shape (`ceil(W/16) x
		// ceil(H/16) x 1`, matching the shader's `local_size_x =
		// local_size_y = 16`) tracks the runtime backbuffer extent. When
		// the executor never publishes a viewport (e.g. headless contract
		// tests that drive `Execute` directly without going through the
		// renderer), the dispatch falls back to `(1, 1, 1)` so the recorded
		// event count is still observable but no useful luminance is
		// accumulated.
		void SetViewport(std::uint32_t width, std::uint32_t height) noexcept;

		void Execute(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera);

	private:
		PostProcessSystem& m_PostProcessSystem;
		RHI::PipelineHandle m_Pipeline{};
		std::uint32_t m_ViewportWidth{0u};
		std::uint32_t m_ViewportHeight{0u};
	};
}

