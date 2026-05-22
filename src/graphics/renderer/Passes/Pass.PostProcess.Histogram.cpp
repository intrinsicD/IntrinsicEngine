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

        const PostProcessHistogramPushConstants pc = BuildPostProcessHistogramPushConstants(
            m_PostProcessSystem.GetSettings(), m_ViewportWidth, m_ViewportHeight);

        const std::uint32_t groupsX = m_ViewportWidth  > 0u ? CeilDiv(m_ViewportWidth,  kHistogramTileSize) : 1u;
        const std::uint32_t groupsY = m_ViewportHeight > 0u ? CeilDiv(m_ViewportHeight, kHistogramTileSize) : 1u;

        cmd.BindPipeline(m_Pipeline);
        cmd.PushConstants(&pc, sizeof(pc));
        cmd.Dispatch(groupsX, groupsY, 1u);
    }
}

