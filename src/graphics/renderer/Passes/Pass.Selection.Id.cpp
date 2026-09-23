module;

#include <cstdint>

module Extrinsic.Graphics.Pass.Selection.Id;

namespace Extrinsic::Graphics
{
    namespace
    {
        // Byte layout matches the `selection/*_id.vert` push blocks.
        void PushSceneConstants(RHI::ICommandContext& cmd, const GpuWorld& gpuWorld,
                                const std::uint32_t frameIndex, const RHI::GpuDrawBucketKind bucket)
        {
            RHI::GpuScenePushConstants pc{};
            pc.SceneTableBDA = gpuWorld.GetSceneTableBDA();
            pc.FrameIndex    = frameIndex;
            pc.DrawBucket    = static_cast<std::uint32_t>(bucket);
            cmd.PushConstants(&pc, sizeof(pc));
        }
    }

    // Entity and face IDs share the surface draw; their pipelines choose the ID payload.
    void EntityIdPass::Execute(RHI::ICommandContext& cmd, const RHI::CameraUBO&, const GpuWorld& gpuWorld,
                               const CullingSystem& culling, const std::uint32_t frameIndex)
    {
        if (!Ready())
        {
            return;
        }
        RecordOpaqueSurfaceBucket(cmd, m_Pipeline, gpuWorld,
                                  culling.GetBucket(RHI::GpuDrawBucketKind::SurfaceOpaque), frameIndex);
    }

    void FaceIdPass::Execute(RHI::ICommandContext& cmd, const RHI::CameraUBO&, const GpuWorld& gpuWorld,
                             const CullingSystem& culling, const std::uint32_t frameIndex)
    {
        if (!Ready())
        {
            return;
        }
        RecordOpaqueSurfaceBucket(cmd, m_Pipeline, gpuWorld,
                                  culling.GetBucket(RHI::GpuDrawBucketKind::SurfaceOpaque), frameIndex);
    }

    void EdgeIdPass::Execute(RHI::ICommandContext& cmd, const RHI::CameraUBO&, const GpuWorld& gpuWorld,
                             const CullingSystem& culling, const std::uint32_t frameIndex)
    {
        if (!Ready())
        {
            return;
        }
        const auto& bucket = culling.GetBucket(RHI::GpuDrawBucketKind::Lines);
        if (!bucket.Indexed || !bucket.IndexedArgsBuffer.IsValid() ||
            !bucket.CountBuffer.IsValid() || bucket.Capacity == 0u)
        {
            return;
        }

        cmd.BindPipeline(m_Pipeline);
        cmd.BindIndexBuffer(gpuWorld.GetManagedIndexBuffer(), 0, RHI::IndexType::Uint32);
        PushSceneConstants(cmd, gpuWorld, frameIndex, RHI::GpuDrawBucketKind::Lines);
        cmd.DrawIndexedIndirectCount(bucket.IndexedArgsBuffer, 0, bucket.CountBuffer, 0, bucket.Capacity);
    }

    void PointIdPass::Execute(RHI::ICommandContext& cmd, const RHI::CameraUBO&, const GpuWorld& gpuWorld,
                              const CullingSystem& culling, const std::uint32_t frameIndex)
    {
        if (!Ready())
        {
            return;
        }
        const auto& bucket = culling.GetBucket(RHI::GpuDrawBucketKind::SelectionPoints);
        if (bucket.Indexed || !bucket.NonIndexedArgsBuffer.IsValid() ||
            !bucket.CountBuffer.IsValid() || bucket.Capacity == 0u)
        {
            return;
        }

        cmd.BindPipeline(m_Pipeline);
        PushSceneConstants(cmd, gpuWorld, frameIndex, RHI::GpuDrawBucketKind::SelectionPoints);
        cmd.DrawIndirectCount(bucket.NonIndexedArgsBuffer, 0, bucket.CountBuffer, 0, bucket.Capacity);
    }
}
