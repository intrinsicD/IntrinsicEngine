module;

#include <cstdint>

export module Extrinsic.Graphics.Pass.Forward.Point;

import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Types;
import Extrinsic.Graphics.CullingSystem;
import Extrinsic.Graphics.ForwardSystem;
import Extrinsic.Graphics.GpuWorld;

namespace Extrinsic::Graphics
{
    export class PointPass
    {
    public:
        explicit PointPass(ForwardSystem& forward) : m_ForwardSystem(forward) {}

        PointPass(const PointPass&)            = delete;
        PointPass& operator=(const PointPass&) = delete;

        void SetPipeline(RHI::PipelineHandle pipeline) noexcept;
        void Execute(RHI::ICommandContext& cmd,
                     const RHI::CameraUBO& camera,
                     const GpuWorld&       gpuWorld,
                     const CullingSystem&  culling,
                     std::uint32_t         frameIndex);

    private:
        ForwardSystem&      m_ForwardSystem;
        RHI::PipelineHandle m_Pipeline{};
    };
}
