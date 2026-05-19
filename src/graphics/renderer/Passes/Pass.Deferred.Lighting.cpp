module;

#include <cstdint>

module Extrinsic.Graphics.Pass.Deferred.Lighting;

import Extrinsic.RHI.Bindless;

namespace Extrinsic::Graphics
{
    namespace
    {
        // GRAPHICS-072 Slice C — `ShadowAtlasBindlessIndex` is the third
        // word, replacing the Slice B `_pad0` field byte-identically (16-byte
        // struct stays 16 bytes). The fragment shader reads this index out of
        // `PushConstants` and uses it to sample the `ShadowSystem`-owned
        // shadow atlas through the global bindless heap. The legacy
        // `assets/shaders/deferred_lighting.frag` `set 1, binding 1`
        // `sampler2DShadow` cannot be honored on the promoted Vulkan pipeline
        // layout, which declares only the bindless set at `set = 0`, so the
        // engine substitutes a bindless-index push-constant for the same
        // logical "binding". See `src/graphics/renderer/README.md` ("Shader
        // push-constant compatibility policy" + "Slice C: shadow-atlas
        // binding") for the durable rule.
        struct alignas(16) DeferredLightingPushConstants
        {
            std::uint64_t SceneTableBDA            = 0;
            std::uint32_t ShadowAtlasBindlessIndex = RHI::kInvalidBindlessIndex;
            std::uint32_t _pad0                    = 0;
        };

        static_assert(sizeof(DeferredLightingPushConstants) == 16);
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
        // GRAPHICS-072 Slice C — sourced from `ShadowSystem::GetAtlasBindlessIndex()`.
        // The accessor returns `kInvalidBindlessIndex` when the atlas has not
        // been lazily allocated yet (shadows disabled or `SetParams(...)` has
        // not enabled them). The default initial value of the push-constant
        // field already matches that sentinel; reading it here keeps the
        // wiring contract explicit so the shader can branch on validity.
        pc.ShadowAtlasBindlessIndex = m_ShadowSystem.GetAtlasBindlessIndex();
        cmd.BindPipeline(m_Pipeline);
        cmd.PushConstants(&pc, sizeof(pc));
        cmd.Draw(3u, 1u, 0u, 0u);

        (void)camera;
    }
}
