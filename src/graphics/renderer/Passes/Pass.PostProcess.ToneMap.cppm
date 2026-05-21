module;

#include <cstddef>
#include <cstdint>

export module Extrinsic.Graphics.Pass.PostProcess.ToneMap;

import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Types;
import Extrinsic.Graphics.PostProcessSystem;

namespace Extrinsic::Graphics
{
	// GRAPHICS-075 Slice A — pass-local push-constant block mirroring the
	// `assets/shaders/post_tonemap.frag` `layout(push_constant)` declaration
	// byte-for-byte under Vulkan std430. The default Vulkan push-constant
	// layout for a `uniform Push { ... }` block is std430, under which
	// `vec3` has 16-byte alignment (12 bytes of data + 4 bytes padding); the
	// explicit `_pad{0,1,2}` fields in the shader match the natural std430
	// padding so the C++ struct here can lay the same fields out under the
	// standard C++ float-array alignment rules and still produce a byte-
	// identical wire shape. The canonical `PostProcessPushConstants` block
	// in `Graphics.PostProcessSystem.cppm` is a different 20-byte shape
	// shared by stages that don't need grading state (Histogram / Bloom /
	// FXAA / SMAA) and is intentionally not used as the wire format here:
	// pushing the canonical 20-byte block into this shader silently aliases
	// `HistogramBinCount` onto `ColorGradingOn` (256 -> grading on) and
	// `StageKind` onto `Saturation` (`bit_cast<float>(2)` ~ 0 -> grayscale),
	// with the remaining 60 bytes of `Lift`/`Gamma`/`Gain` reading
	// implementation-defined memory.
	export struct PostProcessToneMapPushConstants
	{
		float Exposure{1.0f};
		std::int32_t Operator{0};          // 0 = ACES, 1 = Reinhard, 2 = Uncharted2
		float BloomIntensity{0.0f};
		std::int32_t ColorGradingOn{0};    // 1 = apply color grading, 0 = bypass

		float Saturation{1.0f};
		float Contrast{1.0f};
		float ColorTempOffset{0.0f};
		float TintOffset{0.0f};

		float Lift[3]{0.0f, 0.0f, 0.0f};
		float _pad0{0.0f};
		float Gamma[3]{1.0f, 1.0f, 1.0f};
		float _pad1{0.0f};
		float Gain[3]{1.0f, 1.0f, 1.0f};
		float _pad2{0.0f};
	};

	static_assert(sizeof(PostProcessToneMapPushConstants) == 80u,
		"PostProcessToneMapPushConstants must mirror post_tonemap.frag std430 push block (80 bytes).");
	static_assert(offsetof(PostProcessToneMapPushConstants, Saturation) == 16u,
		"Saturation must follow the first four 4-byte fields per post_tonemap.frag.");
	static_assert(offsetof(PostProcessToneMapPushConstants, Lift) == 32u,
		"Lift must start on a 16-byte boundary per std430 vec3 alignment.");
	static_assert(offsetof(PostProcessToneMapPushConstants, Gamma) == 48u,
		"Gamma must start on a 16-byte boundary per std430 vec3 alignment.");
	static_assert(offsetof(PostProcessToneMapPushConstants, Gain) == 64u,
		"Gain must start on a 16-byte boundary per std430 vec3 alignment.");

	// Builds the push payload from the system's settings. Exposure and
	// BloomIntensity come directly from `PostProcessSettings`; the remaining
	// tonemap-specific fields (operator selection, color grading) take
	// deterministic defaults that produce neutral ACES with grading
	// disabled. Future settings extensions / Slice E histogram-driven
	// adaptation can extend this mapping without touching the pass body or
	// the canonical `PostProcessPushConstants` block.
	export PostProcessToneMapPushConstants BuildPostProcessToneMapPushConstants(
		const PostProcessSettings& settings) noexcept;

	export class PostProcessToneMapPass
	{
	public:
		explicit PostProcessToneMapPass(PostProcessSystem& postProcess) : m_PostProcessSystem(postProcess) {}

		PostProcessToneMapPass(const PostProcessToneMapPass&)            = delete;
		PostProcessToneMapPass& operator=(const PostProcessToneMapPass&) = delete;

		void SetPipeline(RHI::PipelineHandle pipeline) noexcept;
		void Execute(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera);

	private:
		PostProcessSystem& m_PostProcessSystem;
		RHI::PipelineHandle m_Pipeline{};
	};
}

