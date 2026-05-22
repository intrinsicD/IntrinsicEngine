module;

#include <cstdint>

module Extrinsic.Graphics.Pass.PostProcess.Histogram;

namespace Extrinsic::Graphics
{
    namespace
    {
        // GRAPHICS-075 Slice E.1 — workgroup tile size shared with the
        // `post_histogram.comp` shader (`layout(local_size_x = 16,
        // local_size_y = 16)`). The dispatch shape is the ceiling of the
        // viewport extent divided by the tile size.
        constexpr std::uint32_t kHistogramTileSize = 16u;

        // GRAPHICS-075 Slice E.1 — recipe-side `PostProcess.Histogram`
        // buffer is sized for 256 uint32 bins (`R32_UINT` ABI), matching
        // the `bins[256]` declaration in `post_histogram.comp`. The
        // per-frame zero-fill writes the full 256 * 4 = 1024 bytes.
        constexpr std::uint64_t kHistogramBufferSizeBytes =
            256ull * sizeof(std::uint32_t);

        // GRAPHICS-075 Slice E.1 — canonical log-luminance bounds per
        // `GRAPHICS-013AQ`. The histogram covers `[-10, +10]` log2 stops
        // (i.e. `[~1/1024, ~1024]` linear luminance). Slice E.2's exposure
        // adaptation overrides these bounds once the readback drain
        // publishes the prior frame's average log luminance.
        constexpr float kHistogramMinLogLum = -10.0f;
        constexpr float kHistogramMaxLogLum =  10.0f;

        [[nodiscard]] constexpr std::uint32_t CeilDiv(std::uint32_t value, std::uint32_t divisor) noexcept
        {
            if (divisor == 0u) { return 0u; }
            return (value + divisor - 1u) / divisor;
        }
    }

    PostProcessHistogramPushConstants BuildPostProcessHistogramPushConstants(
        const PostProcessSettings& settings,
        const std::uint32_t viewportWidth,
        const std::uint32_t viewportHeight) noexcept
    {
        (void)settings; // exposure / gamma plumbed in Slice E.2 once the
                        // readback drain feeds the adaptation history.
        PostProcessHistogramPushConstants pc{};
        pc.Width = viewportWidth;
        pc.Height = viewportHeight;
        pc.MinLogLum = kHistogramMinLogLum;
        pc.RangeLogLum = 1.0f / (kHistogramMaxLogLum - kHistogramMinLogLum);
        return pc;
    }

    void PostProcessHistogramPass::SetPipeline(const RHI::PipelineHandle pipeline) noexcept
    {
        m_Pipeline = pipeline;
    }

    void PostProcessHistogramPass::SetHistogramBuffer(const RHI::BufferHandle buffer) noexcept
    {
        m_HistogramBuffer = buffer;
    }

    void PostProcessHistogramPass::SetViewport(const std::uint32_t width, const std::uint32_t height) noexcept
    {
        m_ViewportWidth = width;
        m_ViewportHeight = height;
    }

    void PostProcessHistogramPass::Execute(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera)
    {
        (void)camera;
        if (!m_PostProcessSystem.IsInitialized() || !m_Pipeline.IsValid() ||
            !m_PostProcessSystem.IsStageEnabled(PostProcessStageKind::Histogram))
        {
            return;
        }

        // GRAPHICS-075 Slice E.1 — zero the 256 uint32 bins before
        // dispatching. The compute shader accumulates samples through
        // `atomicAdd(histogram.bins[binIdx], 1)`, so any non-zero
        // contents (transient-allocator reuse from a prior frame, or
        // the very first frame's undefined memory) would be summed
        // into the next frame's luminance distribution and corrupt the
        // downstream exposure-adaptation readback Slice E.2 wires.
        // Mirrors the `CullingSystem::ResetCounters` pattern:
        // `FillBuffer` writes through `MemoryAccess::TransferWrite`
        // and the matching `BufferBarrier(TransferWrite → ShaderWrite)`
        // makes the zero-fill visible to the histogram dispatch's
        // atomic accumulations. The `TransferDst` usage bit needed by
        // `vkCmdFillBuffer` is declared on the recipe-side
        // `PostProcess.Histogram` buffer creation. The fill is gated
        // on a valid published handle so headless contract tests that
        // drive `Execute` directly (without going through the
        // executor) still observe the original bind/push/dispatch
        // event triple — the bug-fix is opt-in for callers that wire
        // the handle.
        if (m_HistogramBuffer.IsValid())
        {
            cmd.FillBuffer(m_HistogramBuffer, 0u, kHistogramBufferSizeBytes, 0u);
            cmd.BufferBarrier(m_HistogramBuffer,
                              RHI::MemoryAccess::TransferWrite,
                              RHI::MemoryAccess::ShaderWrite);
        }

        const PostProcessHistogramPushConstants pc = BuildPostProcessHistogramPushConstants(
            m_PostProcessSystem.GetSettings(), m_ViewportWidth, m_ViewportHeight);

        const std::uint32_t groupsX = m_ViewportWidth  > 0u ? CeilDiv(m_ViewportWidth,  kHistogramTileSize) : 1u;
        const std::uint32_t groupsY = m_ViewportHeight > 0u ? CeilDiv(m_ViewportHeight, kHistogramTileSize) : 1u;

        cmd.BindPipeline(m_Pipeline);
        cmd.PushConstants(&pc, sizeof(pc));
        cmd.Dispatch(groupsX, groupsY, 1u);
    }
}

