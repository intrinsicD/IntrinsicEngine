module;

#include <cstdint>

module Extrinsic.Graphics.Pass.Selection.PointId;

namespace Extrinsic::Graphics
{
    void PointIdPass::SetPipeline(const RHI::PipelineHandle pipeline) noexcept
    {
        m_Pipeline = pipeline;
    }

    void PointIdPass::Execute(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera)
    {
        (void)cmd;
        (void)camera;
    }

    void PointIdPass::Execute(RHI::ICommandContext& cmd,
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

        const auto& bucket = culling.GetBucket(RHI::GpuDrawBucketKind::SelectionPoints);
        if (bucket.Indexed || !bucket.NonIndexedArgsBuffer.IsValid() ||
            !bucket.CountBuffer.IsValid() || bucket.Capacity == 0u)
        {
            return;
        }

        cmd.BindPipeline(m_Pipeline);

        RHI::GpuScenePushConstants pc{};
        pc.SceneTableBDA = gpuWorld.GetSceneTableBDA();
        pc.FrameIndex    = frameIndex;
        pc.DrawBucket    = static_cast<std::uint32_t>(RHI::GpuDrawBucketKind::SelectionPoints);
        cmd.PushConstants(&pc, sizeof(pc));

        cmd.DrawIndirectCount(bucket.NonIndexedArgsBuffer, 0, bucket.CountBuffer, 0, bucket.Capacity);
    }
}
