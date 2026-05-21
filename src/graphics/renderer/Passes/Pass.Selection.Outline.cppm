module;

#include <cstdint>

export module Extrinsic.Graphics.Pass.Selection.Outline;

import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Types;
import Extrinsic.Graphics.RenderWorld;
import Extrinsic.Graphics.SelectionSystem;

namespace Extrinsic::Graphics
{
	// GRAPHICS-074 Slice D.4 — exported `selection_outline.frag` push block.
	// Layout mirrors `assets/shaders/selection_outline.frag`'s
	// `layout(push_constant) uniform Push { ... }` byte-for-byte under the
	// Vulkan std430 default layout (vec4 OutlineColor + vec4 HoverColor +
	// 12 floats/uints + uint[16] SelectedIds = 144 bytes). The pass and the
	// helper below were anonymous-namespace types in `Pass.Selection.Outline.cpp`
	// during Slice C; promoting them to the module interface lets the
	// renderer source the push payload from `RenderWorld::Selection` and
	// lets contract tests assert the recorded bytes match a seeded snapshot
	// without poking at `Pass.Selection.Outline.cpp` internals.
	export struct alignas(16) SelectionOutlinePushConstants
	{
		float         OutlineColor[4]      {0.f, 0.f, 0.f, 0.f};
		float         HoverColor[4]        {0.f, 0.f, 0.f, 0.f};
		float         OutlineWidth         {0.f};
		std::uint32_t SelectedCount        {0u};
		std::uint32_t HoveredId            {0u};
		std::uint32_t OutlineMode          {0u};
		float         SelectionFillAlpha   {0.f};
		float         HoverFillAlpha       {0.f};
		float         PulsePhase           {0.f};
		float         PulseMin             {0.f};
		float         PulseMax             {0.f};
		float         GlowFalloff          {0.f};
		std::uint32_t _pad0                {0u};
		std::uint32_t _pad1                {0u};
		std::uint32_t SelectedIds[16]      {};
	};

	// Capacity of the `SelectedIds[]` tail in the push block. Snapshots with
	// more selected ids than this are truncated; the renderer-side follow-up
	// is to move overflow into a UBO or bindless buffer (see the
	// `PushConstantSize > 128` portability note in `BuildSelectionOutlinePipelineDesc`).
	export inline constexpr std::uint32_t kSelectionOutlineMaxSelectedIds = 16u;

	// Build the push payload that `SelectionOutlinePass::Execute` will push
	// from the runtime-extracted selection snapshot. `HoveredId` is zeroed
	// when `HasHovered` is false so the shader's hover early-out fires;
	// `SelectedIds` is truncated to `kSelectionOutlineMaxSelectedIds` and
	// `SelectedCount` matches the truncated tail. Pulse/glow scalars are
	// passed through verbatim and only consumed by the shader when
	// `OutlineMode` selects them.
	export [[nodiscard]] SelectionOutlinePushConstants BuildSelectionOutlinePushConstants(
		const SelectionSnapshot& selection) noexcept;

	export class SelectionOutlinePass
	{
	public:
		explicit SelectionOutlinePass(SelectionSystem& selection) : m_SelectionSystem(selection) {}

		SelectionOutlinePass(const SelectionOutlinePass&)            = delete;
		SelectionOutlinePass& operator=(const SelectionOutlinePass&) = delete;

		void SetPipeline(RHI::PipelineHandle pipeline) noexcept;
		void Execute(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera);
		void Execute(RHI::ICommandContext& cmd,
		             const RHI::CameraUBO& camera,
		             std::uint32_t         frameIndex);
		// GRAPHICS-074 Slice D.4 — push runtime-driven outline state. The
		// renderer's `RecordSelectionOutlinePass` calls this overload with
		// `BuildSelectionOutlinePushConstants(renderWorld.Selection)` so the
		// shader sees the seeded selection / hover ids and visual style
		// instead of the all-zero Slice C placeholder.
		void Execute(RHI::ICommandContext& cmd,
		             const RHI::CameraUBO& camera,
		             std::uint32_t         frameIndex,
		             const SelectionOutlinePushConstants& pushConstants);

	private:
		SelectionSystem&    m_SelectionSystem;
		RHI::PipelineHandle m_Pipeline{};
	};
}
