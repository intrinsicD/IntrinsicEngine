module;

#include <cstdint>

module Extrinsic.Graphics.Pass.Selection.Outline;

namespace Extrinsic::Graphics
{
    namespace
    {
        // GRAPHICS-074 Slice C — deterministic push-constant block matching
        // `assets/shaders/selection_outline.frag`'s `layout(push_constant)
        // uniform Push { ... }` byte-for-byte under the Vulkan std430 default
        // layout. Offsets (verified against the GLSL declaration):
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
        // The pass body pushes a zero-initialised instance so the shader sees
        // defined values rather than stale push-constant memory left by an
        // earlier draw. With `SelectedCount == 0u`, `HoveredId == 0u`, and the
        // outline/hover colours fully transparent, the fragment shader's
        // early-out (`if (!centerSelected && !centerHovered &&
        // pc.SelectedCount == 0u && pc.HoveredId == 0u) { outColor = vec4(0);
        // return; }`) fires on every pixel, so the pass remains a deterministic
        // no-op overlay until the runtime-driven outline state is plumbed
        // through (Slice D scope alongside the `Picking.Readback` drain).
        // Crucially `OutlineWidth` is 0 (not garbage), so the inner
        // neighbour-sampling loop's `for (r = 1; r <= max(width, 1); ...)`
        // executes exactly once instead of running for the worst case of
        // whatever stale bytes happened to be in push memory.
        //
        // Size note: 144 bytes exceeds the Vulkan-guaranteed
        // `maxPushConstantsSize` minimum of 128. Hosts that report exactly
        // 128 will reject pipeline-layout creation; the CPU/null gate the
        // renderer runs against does not enforce this. Reducing the block
        // below 128 (e.g. moving `SelectedIds` to a UBO or a small bindless
        // structured buffer) is tracked as part of the Slice D outline
        // wiring follow-up; until then this pipeline is opt-in on hosts that
        // expose >=144 push bytes (which covers all current desktop Vulkan
        // implementations).
        struct alignas(16) SelectionOutlinePushConstants
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

        static_assert(sizeof(SelectionOutlinePushConstants) == 144);
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
        (void)camera;
        (void)frameIndex;
        if (!m_SelectionSystem.IsInitialized() || !m_Pipeline.IsValid())
        {
            return;
        }

        // GRAPHICS-074 Slice C — push deterministic zero defaults before the
        // fullscreen draw so the fragment shader does not read stale push
        // memory left by a previous pass. Runtime-driven outline state
        // (selected entity IDs, hover ID, width/colour/animation) is wired
        // in alongside the Slice D `Picking.Readback` drain.
        const SelectionOutlinePushConstants pc{};
        cmd.BindPipeline(m_Pipeline);
        cmd.PushConstants(&pc, sizeof(pc));
        cmd.Draw(3u, 1u, 0u, 0u);
    }
}
