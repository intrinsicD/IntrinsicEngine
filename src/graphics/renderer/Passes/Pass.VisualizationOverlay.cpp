module;

#include <cstdint>

module Extrinsic.Graphics.Pass.VisualizationOverlay;

namespace Extrinsic::Graphics
{
    void VisualizationOverlayPass::SetVectorFieldDepthTestedPipeline(const RHI::PipelineHandle pipeline) noexcept
    {
        m_VectorFieldDepthTestedPipeline = pipeline;
    }

    void VisualizationOverlayPass::SetVectorFieldAlwaysOnTopPipeline(const RHI::PipelineHandle pipeline) noexcept
    {
        m_VectorFieldAlwaysOnTopPipeline = pipeline;
    }

    void VisualizationOverlayPass::SetIsolineDepthTestedPipeline(const RHI::PipelineHandle pipeline) noexcept
    {
        m_IsolineDepthTestedPipeline = pipeline;
    }

    void VisualizationOverlayPass::SetIsolineAlwaysOnTopPipeline(const RHI::PipelineHandle pipeline) noexcept
    {
        m_IsolineAlwaysOnTopPipeline = pipeline;
    }
}
