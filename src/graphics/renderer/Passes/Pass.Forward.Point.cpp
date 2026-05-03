module;

#include <cstdint>

module Extrinsic.Graphics.Pass.Forward.Point;

namespace Extrinsic::Graphics
{
    void ForwardPointPass::SetPipeline(const RHI::PipelineHandle pipeline) noexcept
    {
        m_Pipeline = pipeline;
    }

    void ForwardPointPass::Execute(RHI::ICommandContext& cmd,
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
        if (bucket.Indexed || !bucket.NonIndexedArgsBuffer.IsValid() ||
            !bucket.CountBuffer.IsValid() || bucket.Capacity == 0u)
        {
            return;
        }

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
