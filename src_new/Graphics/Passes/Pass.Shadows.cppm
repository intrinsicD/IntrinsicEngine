module;

#include <cstdint>

export module Extrinsic.Graphics.Pass.Shadows;

import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Types;
import Extrinsic.Graphics.CullingSystem;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.ShadowSystem;

namespace Extrinsic::Graphics
{
    export class ShadowPass
    {
    public:
        explicit ShadowPass(ShadowSystem& shadows) : m_ShadowSystem(shadows) {}

        ShadowPass(const ShadowPass&)            = delete;
        ShadowPass& operator=(const ShadowPass&) = delete;

        void SetPipeline(RHI::PipelineHandle pipeline) noexcept;
        void Execute(RHI::ICommandContext& cmd,
                     const RHI::CameraUBO& camera,
                     const GpuWorld&       gpuWorld,
                     const CullingSystem&  culling,
                     std::uint32_t         frameIndex);

    private:
        ShadowSystem&       m_ShadowSystem;
        RHI::PipelineHandle m_Pipeline{};
    };
}
