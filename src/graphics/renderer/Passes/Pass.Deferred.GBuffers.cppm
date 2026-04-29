module;

#include <cstdint>

export module Extrinsic.Graphics.Pass.Deferred.GBuffers;

import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Types;
import Extrinsic.Graphics.CullingSystem;
import Extrinsic.Graphics.DeferredSystem;
import Extrinsic.Graphics.GpuWorld;

namespace Extrinsic::Graphics
{
    export class GBufferPass
    {
    public:
        explicit GBufferPass(DeferredSystem& deferred) : m_DeferredSystem(deferred) {}

        GBufferPass(const GBufferPass&)            = delete;
        GBufferPass& operator=(const GBufferPass&) = delete;

        void SetPipeline(RHI::PipelineHandle pipeline) noexcept;
        void Execute(RHI::ICommandContext& cmd,
                     const RHI::CameraUBO& camera,
                     const GpuWorld&       gpuWorld,
                     const CullingSystem&  culling,
                     std::uint32_t         frameIndex);

    private:
        DeferredSystem&      m_DeferredSystem;
        RHI::PipelineHandle  m_Pipeline{};
    };
}
