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

    void DeferredLightingPass::Execute(RHI::ICommandContext& cmd,
                                       const RHI::CameraUBO& camera,
                                       const GpuWorld& gpuWorld)
    {
        if (!m_DeferredSystem.IsInitialized())
        {
            return;
        }

        DeferredLightingPushConstants pc{};
        pc.SceneTableBDA = gpuWorld.GetSceneTableBDA();
        cmd.PushConstants(&pc, sizeof(pc));

        (void)camera;
    }
}
