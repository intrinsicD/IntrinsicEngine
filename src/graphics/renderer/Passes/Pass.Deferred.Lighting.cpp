module;

#include <cstdint>

module Extrinsic.Graphics.Pass.Deferred.Lighting;

namespace Extrinsic::Graphics
{
    namespace
    {
        struct alignas(16) DeferredLightingPushConstants
        {
            std::uint64_t SceneTableBDA = 0;
            std::uint32_t _pad0 = 0;
            std::uint32_t _pad1 = 0;
        };

        static_assert(sizeof(DeferredLightingPushConstants) <= 128);
    }

    void DeferredLightingPass::SetPipeline(const RHI::PipelineHandle pipeline) noexcept
    {
        m_Pipeline = pipeline;
    }

    void DeferredLightingPass::Execute(RHI::ICommandContext& cmd,
                                       const RHI::CameraUBO& camera,
                                       const GpuWorld& gpuWorld)
    {
        if (!m_DeferredSystem.IsInitialized() || !m_Pipeline.IsValid())
        {
            return;
        }

        DeferredLightingPushConstants pc{};
        pc.SceneTableBDA = gpuWorld.GetSceneTableBDA();
        cmd.BindPipeline(m_Pipeline);
        cmd.PushConstants(&pc, sizeof(pc));
        cmd.Draw(3u, 1u, 0u, 0u);

        (void)camera;
    }
}
