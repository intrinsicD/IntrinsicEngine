module;

#include <cstdint>

module Extrinsic.Graphics.Pass.Selection.EntityId;

namespace Extrinsic::Graphics
{
    void EntityIdPass::SetPipeline(const RHI::PipelineHandle pipeline) noexcept
    {
        m_Pipeline = pipeline;
    }

    void EntityIdPass::Execute(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera)
    {
        (void)cmd;
        (void)camera;
    }

    void EntityIdPass::Execute(RHI::ICommandContext& cmd,
                               const RHI::CameraUBO& camera,
                               const GpuWorld&       gpuWorld,
                               const CullingSystem&  culling,
                               const std::uint32_t   frameIndex)
    {
        (void)camera;
        if (!m_SelectionSystem.IsInitialized() || !m_Pipeline.IsValid())
        {
            return;
        }

        RecordOpaqueSurfaceBucket(
            cmd, m_Pipeline, gpuWorld,
            culling.GetBucket(RHI::GpuDrawBucketKind::SurfaceOpaque),
            frameIndex);
    }
}
