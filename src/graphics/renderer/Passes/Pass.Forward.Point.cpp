module;

#include <cstdint>

module Extrinsic.Graphics.Pass.Forward.Point;

namespace Extrinsic::Graphics
{
    void PointPass::SetPipeline(const RHI::PipelineHandle pipeline) noexcept
    {
        m_Pipeline = pipeline;
    }

    void PointPass::Execute(RHI::ICommandContext& cmd,
                            const RHI::CameraUBO& camera,
                            const GpuWorld&       gpuWorld,
                            const CullingSystem&  culling,
                            const std::uint32_t   frameIndex)
    {
        (void)camera;
        if (!m_ForwardSystem.IsInitialized() || !m_Pipeline.IsValid())
        {
            return;
        }

        const auto& bucket = culling.GetBucket(RHI::GpuDrawBucketKind::Points);

        cmd.BindPipeline(m_Pipeline);

        RHI::GpuScenePushConstants pc{};
        pc.SceneTableBDA = gpuWorld.GetSceneTableBDA();
        pc.FrameIndex    = frameIndex;
        pc.DrawBucket    = static_cast<std::uint32_t>(RHI::GpuDrawBucketKind::Points);

        cmd.PushConstants(&pc, sizeof(pc));

        cmd.DrawIndirectCount(
            bucket.NonIndexedArgsBuffer,
            0,
            bucket.CountBuffer,
            0,
            bucket.Capacity);
    }
}
