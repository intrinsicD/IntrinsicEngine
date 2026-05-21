module;

#include <cstddef>

export module Extrinsic.Graphics.Pass.PostProcess.FXAA;

import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Types;
import Extrinsic.Graphics.PostProcessSystem;

namespace Extrinsic::Graphics
{
	// GRAPHICS-075 Slice C — pass-local push-constant block mirroring the
	// `assets/shaders/post_fxaa.frag` `layout(push_constant) Push`
	// declaration byte-for-byte under Vulkan std430. `vec2` is 8-byte
	// aligned (size 8) so the trailing three floats pack directly behind
	// it for a total of 20 bytes. The canonical 20-byte
	// `PostProcessPushConstants` block in `Graphics.PostProcessSystem.cppm`
	// happens to share the same byte size but a completely different field
	// layout: under std430 it would alias `Exposure` (1.0) onto
	// `InvResolution.x` (sane only at 1px screens), `Gamma` (2.2) onto
	// `InvResolution.y`, `BloomIntensity` (0.05) onto `ContrastThreshold`,
	// `HistogramBinCount` (256) onto `RelativeThreshold`
	// (`bit_cast<float>(256)` ≈ 3.6e-43 → always-pass), and `StageKind`
	// onto `SubpixelBlending` (`bit_cast<float>(3)` ≈ 4.2e-45 →
	// effectively-off). The same "Shader push-constant compatibility
	// policy" hard gate the tonemap and bloom slices follow applies here.
	export struct PostProcessFXAAPushConstants
	{
		float InvResolution[2]{0.0f, 0.0f}; // 1 / viewport dimensions
		float ContrastThreshold{0.0312f};    // FXAA 3.11 edge-detection threshold
		float RelativeThreshold{0.063f};     // FXAA 3.11 relative-contrast threshold
		float SubpixelBlending{0.75f};       // 0.0 = off, 0.75 = default, 1.0 = max
	};

	static_assert(sizeof(PostProcessFXAAPushConstants) == 20u,
		"PostProcessFXAAPushConstants must mirror post_fxaa.frag std430 push block (20 bytes).");
	static_assert(offsetof(PostProcessFXAAPushConstants, ContrastThreshold) == 8u,
		"ContrastThreshold must follow vec2 InvResolution per post_fxaa.frag.");
	static_assert(offsetof(PostProcessFXAAPushConstants, RelativeThreshold) == 12u,
		"RelativeThreshold must follow float ContrastThreshold per post_fxaa.frag.");
	static_assert(offsetof(PostProcessFXAAPushConstants, SubpixelBlending) == 16u,
		"SubpixelBlending must follow float RelativeThreshold per post_fxaa.frag.");

	// Builds the FXAA push payload from the system's settings + the
	// current viewport extent. `viewportWidth`/`viewportHeight` come from
	// `RHI::CameraUBO::Viewport{Width,Height}` (the same source Slice B.2's
	// bloom builder consumes); a zero or negative extent maps to a zero
	// inverse so the shader's `1.0 / pc.InvResolution` neighbour offsets
	// degenerate gracefully rather than diverging. The remaining knobs
	// (`ContrastThreshold`/`RelativeThreshold`/`SubpixelBlending`) take the
	// FXAA 3.11 quality defaults documented in `post_fxaa.frag`; future
	// `PostProcessSettings::FXAA*` fields flow through this builder so the
	// pass body and pipeline desc stay unchanged.
	export PostProcessFXAAPushConstants BuildPostProcessFXAAPushConstants(
		const PostProcessSettings& settings,
		float viewportWidth,
		float viewportHeight) noexcept;

	export class PostProcessFXAAPass
	{
	public:
		explicit PostProcessFXAAPass(PostProcessSystem& postProcess) : m_PostProcessSystem(postProcess) {}

		PostProcessFXAAPass(const PostProcessFXAAPass&)            = delete;
		PostProcessFXAAPass& operator=(const PostProcessFXAAPass&) = delete;

		void SetPipeline(RHI::PipelineHandle pipeline) noexcept;
		void Execute(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera);

	private:
		PostProcessSystem& m_PostProcessSystem;
		RHI::PipelineHandle m_Pipeline{};
	};
}

