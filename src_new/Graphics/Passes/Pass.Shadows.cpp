module;

#include <cstdint>

module Extrinsic.Graphics.Pass.Shadows;

namespace Extrinsic::Graphics
{
    void ShadowPass::SetPipeline(const RHI::PipelineHandle pipeline) noexcept
    {
        m_Pipeline = pipeline;
    }

    void ShadowPass::Execute(RHI::ICommandContext& cmd,
                             const RHI::CameraUBO& camera,
                             const GpuWorld&       gpuWorld,
                             const CullingSystem&  culling,
                             const std::uint32_t   frameIndex)
    {
        (void)camera;
        if (!m_ShadowSystem.IsInitialized() || !m_Pipeline.IsValid())
        {
            return;
        }

        const auto& bucket = culling.GetBucket(RHI::GpuDrawBucketKind::ShadowOpaque);

        cmd.BindPipeline(m_Pipeline);
        cmd.BindIndexBuffer(gpuWorld.GetManagedIndexBuffer(), 0, RHI::IndexType::Uint32);

        RHI::GpuScenePushConstants pc{};
        pc.SceneTableBDA = gpuWorld.GetSceneTableBDA();
        pc.FrameIndex    = frameIndex;
        pc.DrawBucket    = static_cast<std::uint32_t>(RHI::GpuDrawBucketKind::ShadowOpaque);

        cmd.PushConstants(&pc, sizeof(pc));

        cmd.DrawIndexedIndirectCount(
            bucket.IndexedArgsBuffer,
            0,
            bucket.CountBuffer,
            0,
            bucket.Capacity);
    }
}
