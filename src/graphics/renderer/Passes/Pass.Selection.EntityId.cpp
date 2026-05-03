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

        const auto& bucket = culling.GetBucket(RHI::GpuDrawBucketKind::SurfaceOpaque);
        if (!bucket.Indexed || !bucket.IndexedArgsBuffer.IsValid() ||
            !bucket.CountBuffer.IsValid() || bucket.Capacity == 0u)
        {
            return;
        }

        cmd.BindPipeline(m_Pipeline);
        cmd.BindIndexBuffer(gpuWorld.GetManagedIndexBuffer(), 0, RHI::IndexType::Uint32);

        RHI::GpuScenePushConstants pc{};
        pc.SceneTableBDA = gpuWorld.GetSceneTableBDA();
        pc.FrameIndex    = frameIndex;
        pc.DrawBucket    = static_cast<std::uint32_t>(RHI::GpuDrawBucketKind::SurfaceOpaque);
        cmd.PushConstants(&pc, sizeof(pc));

        cmd.DrawIndexedIndirectCount(bucket.IndexedArgsBuffer, 0, bucket.CountBuffer, 0, bucket.Capacity);
    }
}
