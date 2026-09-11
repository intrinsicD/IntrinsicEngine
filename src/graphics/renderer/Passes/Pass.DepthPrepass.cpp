module;

#include <cstdint>

module Extrinsic.Graphics.Pass.DepthPrepass;

namespace Extrinsic::Graphics
{
    void DepthPrepassPass::SetPipeline(const RHI::PipelineHandle pipeline) noexcept
    {
        m_Pipeline = pipeline;
    }

    void DepthPrepassPass::Execute(RHI::ICommandContext& cmd,
                                   const RHI::CameraUBO& camera,
                                   const GpuWorld&       gpuWorld,
                                   const CullingSystem&  culling,
                                   const std::uint32_t   frameIndex)
    {
        (void)camera;
        if (!m_Pipeline.IsValid())
        {
            return;
        }

        RecordOpaqueSurfaceBucket(
            cmd, m_Pipeline, gpuWorld,
            culling.GetBucket(RHI::GpuDrawBucketKind::SurfaceOpaque),
            frameIndex, RHI::kMaxIndirectDrawCount);
    }
}

