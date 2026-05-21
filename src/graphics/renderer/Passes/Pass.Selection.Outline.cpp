module;

#include <algorithm>
#include <cstddef>
#include <cstdint>

module Extrinsic.Graphics.Pass.Selection.Outline;

namespace Extrinsic::Graphics
{
    // GRAPHICS-074 Slice C/D.4 — push-constant block byte layout (Vulkan
    // std430 default). Offsets (verified against the GLSL declaration in
    // `assets/shaders/selection_outline.frag`):
    //
    //   0   vec4   OutlineColor          (16)
    //   16  vec4   HoverColor            (16)
    //   32  float  OutlineWidth          ( 4)
    //   36  uint   SelectedCount         ( 4)
    //   40  uint   HoveredId             ( 4)
    //   44  uint   OutlineMode           ( 4)
    //   48  float  SelectionFillAlpha    ( 4)
    //   52  float  HoverFillAlpha        ( 4)
    //   56  float  PulsePhase            ( 4)
    //   60  float  PulseMin              ( 4)
    //   64  float  PulseMax              ( 4)
    //   68  float  GlowFalloff           ( 4)
    //   72  uint   _pad0                 ( 4)
    //   76  uint   _pad1                 ( 4)
    //   80  uint   SelectedIds[16]       (64)
    //  144  total
    //
    // Slice C introduced this block and pushed a zero-initialised instance so
    // the shader saw defined values rather than stale push-constant memory
    // left by an earlier draw. Slice D.4 keeps the same layout but the
    // renderer's `RecordSelectionOutlinePass` now sources the contents from
    // `RenderWorld::Selection` via `BuildSelectionOutlinePushConstants`, so
    // a runtime hover/selection actually drives the outline instead of
    // remaining a no-op overlay.
    //
    // Size note: 144 bytes exceeds the Vulkan-guaranteed
    // `maxPushConstantsSize` minimum of 128. Hosts that report exactly 128
    // will reject pipeline-layout creation; the CPU/null gate the renderer
    // runs against does not enforce this. Reducing the block below 128
    // (e.g. moving `SelectedIds[16]` to a UBO or a small bindless
    // structured buffer) is tracked as the portability follow-up under the
    // outline-state plumbing; until then this pipeline is opt-in on hosts
    // that expose >=144 push bytes (which covers all current desktop
    // Vulkan implementations).
    static_assert(sizeof(SelectionOutlinePushConstants) == 144);

    SelectionOutlinePushConstants BuildSelectionOutlinePushConstants(
        const SelectionSnapshot& selection) noexcept
    {
        SelectionOutlinePushConstants pc{};
        pc.OutlineColor[0] = selection.OutlineColor.r;
        pc.OutlineColor[1] = selection.OutlineColor.g;
        pc.OutlineColor[2] = selection.OutlineColor.b;
        pc.OutlineColor[3] = selection.OutlineColor.a;
        pc.HoverColor[0]   = selection.HoverColor.r;
        pc.HoverColor[1]   = selection.HoverColor.g;
        pc.HoverColor[2]   = selection.HoverColor.b;
        pc.HoverColor[3]   = selection.HoverColor.a;
        pc.OutlineWidth        = selection.OutlineWidth;
        // The shader's center/neighbour-hover test compares `pc.HoveredId`
        // to texelFetch results that are already 0 for "no entity"; gating
        // on `HasHovered` keeps the implicit-zero case explicit so the
        // shader's early-out (`pc.HoveredId == 0u` branch) fires
        // deterministically when the runtime did not publish a hover.
        pc.HoveredId           = selection.HasHovered ? selection.HoveredStableId : 0u;
        pc.OutlineMode         = selection.OutlineMode;
        pc.SelectionFillAlpha  = selection.SelectionFillAlpha;
        pc.HoverFillAlpha      = selection.HoverFillAlpha;
        pc.PulsePhase          = selection.PulsePhase;
        pc.PulseMin            = selection.PulseMin;
        pc.PulseMax            = selection.PulseMax;
        pc.GlowFalloff         = selection.GlowFalloff;

        const std::size_t capacity = static_cast<std::size_t>(kSelectionOutlineMaxSelectedIds);
        const std::size_t available = selection.SelectedStableIds.size();
        const std::size_t copyCount = std::min(available, capacity);
        for (std::size_t i = 0; i < copyCount; ++i)
        {
            pc.SelectedIds[i] = selection.SelectedStableIds[i];
        }
        pc.SelectedCount = static_cast<std::uint32_t>(copyCount);
        return pc;
    }

    void SelectionOutlinePass::SetPipeline(const RHI::PipelineHandle pipeline) noexcept
    {
        m_Pipeline = pipeline;
    }

    void SelectionOutlinePass::Execute(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera)
    {
        Execute(cmd, camera, 0u);
    }

    void SelectionOutlinePass::Execute(RHI::ICommandContext& cmd,
                                       const RHI::CameraUBO& camera,
                                       const std::uint32_t frameIndex)
    {
        // GRAPHICS-074 Slice C compatibility — the no-snapshot overload
        // continues to push a zero-initialised payload so the shader still
        // sees defined values when a caller has no `SelectionSnapshot` to
        // hand (e.g. the pre-Slice-D.4 isolated pass contract test).
        Execute(cmd, camera, frameIndex, SelectionOutlinePushConstants{});
    }

    void SelectionOutlinePass::Execute(RHI::ICommandContext& cmd,
                                       const RHI::CameraUBO& camera,
                                       const std::uint32_t frameIndex,
                                       const SelectionOutlinePushConstants& pushConstants)
    {
        (void)camera;
        (void)frameIndex;
        if (!m_SelectionSystem.IsInitialized() || !m_Pipeline.IsValid())
        {
            return;
        }
        cmd.BindPipeline(m_Pipeline);
        cmd.PushConstants(&pushConstants, sizeof(pushConstants));
        cmd.Draw(3u, 1u, 0u, 0u);
    }
}
