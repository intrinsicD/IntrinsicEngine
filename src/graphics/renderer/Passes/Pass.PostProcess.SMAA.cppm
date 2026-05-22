module;

#include <cstddef>
#include <cstdint>

export module Extrinsic.Graphics.Pass.PostProcess.SMAA;

import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Types;
import Extrinsic.Graphics.PostProcessSystem;

namespace Extrinsic::Graphics
{
	// GRAPHICS-075 Slice D.1 — pass-local push-constant blocks mirroring
	// the three `assets/shaders/post_smaa_{edge,blend,resolve}.frag`
	// `layout(push_constant) Push` declarations byte-for-byte under
	// Vulkan std430. Each block is 16 bytes: `vec2` is 8-byte aligned
	// (size 8) so the trailing two scalars pack directly behind it. Like
	// the FXAA push block in Slice C, the canonical 20-byte
	// `PostProcessPushConstants` is intentionally not reused — under
	// std430 it would alias `Exposure`/`Gamma`/`BloomIntensity`/etc. onto
	// the SMAA shaders' `InvResolution.x`/`InvResolution.y`/threshold
	// scalars and produce visually-meaningless SMAA output. The retained
	// `AreaTex` / `SearchTex` LUT textures sampled by the blend pipeline
	// are owned by `PostProcessSystem` and land in Slice D.2 alongside the
	// recipe-side `PostProcess.AATemp.{Edges,Weights}` split; Slice D.1
	// only adds the pipeline shapes + per-shader push blocks + executor
	// fan-out, keeping the pass body's bind/push/draw triples
	// deterministic against the contract recorder.
	export struct PostProcessSMAAEdgePushConstants
	{
		float InvResolution[2]{0.0f, 0.0f}; // 1 / viewport dimensions
		float EdgeThreshold{0.1f};           // SMAA reference luma contrast threshold
		float Pad0{0.0f};
	};

	static_assert(sizeof(PostProcessSMAAEdgePushConstants) == 16u,
		"PostProcessSMAAEdgePushConstants must mirror post_smaa_edge.frag std430 push block (16 bytes).");
	static_assert(offsetof(PostProcessSMAAEdgePushConstants, EdgeThreshold) == 8u,
		"EdgeThreshold must follow vec2 InvResolution per post_smaa_edge.frag.");
	static_assert(offsetof(PostProcessSMAAEdgePushConstants, Pad0) == 12u,
		"_pad0 must follow float EdgeThreshold per post_smaa_edge.frag.");

	export struct PostProcessSMAABlendPushConstants
	{
		float InvResolution[2]{0.0f, 0.0f}; // 1 / viewport dimensions
		std::int32_t MaxSearchSteps{16};     // SMAA reference horizontal/vertical search distance
		std::int32_t MaxSearchStepsDiag{8};  // SMAA reference diagonal search distance
	};

	static_assert(sizeof(PostProcessSMAABlendPushConstants) == 16u,
		"PostProcessSMAABlendPushConstants must mirror post_smaa_blend.frag std430 push block (16 bytes).");
	static_assert(offsetof(PostProcessSMAABlendPushConstants, MaxSearchSteps) == 8u,
		"MaxSearchSteps must follow vec2 InvResolution per post_smaa_blend.frag.");
	static_assert(offsetof(PostProcessSMAABlendPushConstants, MaxSearchStepsDiag) == 12u,
		"MaxSearchStepsDiag must follow int MaxSearchSteps per post_smaa_blend.frag.");

	export struct PostProcessSMAAResolvePushConstants
	{
		float InvResolution[2]{0.0f, 0.0f}; // 1 / viewport dimensions
		float Pad0{0.0f};
		float Pad1{0.0f};
	};

	static_assert(sizeof(PostProcessSMAAResolvePushConstants) == 16u,
		"PostProcessSMAAResolvePushConstants must mirror post_smaa_resolve.frag std430 push block (16 bytes).");
	static_assert(offsetof(PostProcessSMAAResolvePushConstants, Pad0) == 8u,
		"_pad0 must follow vec2 InvResolution per post_smaa_resolve.frag.");
	static_assert(offsetof(PostProcessSMAAResolvePushConstants, Pad1) == 12u,
		"_pad1 must follow float _pad0 per post_smaa_resolve.frag.");

	// Builds each SMAA push payload from the system's settings + the
	// current viewport extent. `viewportWidth`/`viewportHeight` come from
	// `RHI::CameraUBO::Viewport{Width,Height}` (the same source Slice C's
	// FXAA builder consumes); a zero or negative extent maps to a zero
	// inverse so the shaders' `1.0 / pc.InvResolution` neighbour offsets
	// degenerate gracefully rather than diverging. The remaining knobs
	// (`EdgeThreshold` / `MaxSearchSteps` / `MaxSearchStepsDiag`) take the
	// SMAA reference defaults documented in `assets/shaders/post_smaa_*`;
	// future `PostProcessSettings::SMAA*` fields flow through these
	// builders so the pass body and pipeline descs stay unchanged.
	export PostProcessSMAAEdgePushConstants BuildPostProcessSMAAEdgePushConstants(
		const PostProcessSettings& settings,
		float viewportWidth,
		float viewportHeight) noexcept;

	export PostProcessSMAABlendPushConstants BuildPostProcessSMAABlendPushConstants(
		const PostProcessSettings& settings,
		float viewportWidth,
		float viewportHeight) noexcept;

	export PostProcessSMAAResolvePushConstants BuildPostProcessSMAAResolvePushConstants(
		const PostProcessSettings& settings,
		float viewportWidth,
		float viewportHeight) noexcept;

	// GRAPHICS-075 Slice D.2a — `PostProcessSMAAPass` exposes per-stage
	// `Execute{Edge,Blend,Resolve}(...)` so the renderer can fan each
	// stage out under its own ordered graph pass
	// (`"PostProcessAA{Edge,Blend,Resolve}Pass"`), with edge / blend /
	// resolve targeting format-incompatible color attachments
	// (`RG8_UNORM` / `RGBA8_UNORM` / backbuffer format). The pass body
	// is the SMAA analogue of the bloom helper's "structurally-recorded
	// no-op" taxonomy: when AA is gated off (`None` or `FXAA`) every
	// per-stage Execute emits no bind/push/draw, but the umbrella
	// helpers still return `Recorded` under their accumulators. Retained
	// `AreaTex` / `SearchTex` LUTs (sampled by the blend pipeline) land
	// in Slice D.2b alongside the device-aware `Initialize(device)`
	// overload.
	export class PostProcessSMAAPass
	{
	public:
		explicit PostProcessSMAAPass(PostProcessSystem& postProcess) : m_PostProcessSystem(postProcess) {}

		PostProcessSMAAPass(const PostProcessSMAAPass&)            = delete;
		PostProcessSMAAPass& operator=(const PostProcessSMAAPass&) = delete;

		void SetEdgePipeline(RHI::PipelineHandle pipeline) noexcept;
		void SetBlendPipeline(RHI::PipelineHandle pipeline) noexcept;
		void SetResolvePipeline(RHI::PipelineHandle pipeline) noexcept;

		void ExecuteEdge(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera);
		void ExecuteBlend(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera);
		void ExecuteResolve(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera);

	private:
		PostProcessSystem& m_PostProcessSystem;
		RHI::PipelineHandle m_EdgePipeline{};
		RHI::PipelineHandle m_BlendPipeline{};
		RHI::PipelineHandle m_ResolvePipeline{};
	};
}
