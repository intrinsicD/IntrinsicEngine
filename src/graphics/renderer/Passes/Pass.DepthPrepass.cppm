//
// Created by alex on 22.04.26.
//

module;

#include <cstdint>

export module Extrinsic.Graphics.Pass.DepthPrepass;

import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Types;
import Extrinsic.Graphics.CullingSystem;
import Extrinsic.Graphics.GpuWorld;

namespace Extrinsic::Graphics
{
    export class DepthPrepassPass
    {
    public:
        DepthPrepassPass() = default;

        DepthPrepassPass(const DepthPrepassPass&)            = delete;
        DepthPrepassPass& operator=(const DepthPrepassPass&) = delete;

        void SetPipeline(RHI::PipelineHandle pipeline) noexcept;
        void Execute(RHI::ICommandContext& cmd,
                     const RHI::CameraUBO& camera,
                     const GpuWorld&       gpuWorld,
                     const CullingSystem&  culling,
                     std::uint32_t         frameIndex);

    private:
        RHI::PipelineHandle m_Pipeline{};
    };

    export using DepthPrePass = DepthPrepassPass;
}
