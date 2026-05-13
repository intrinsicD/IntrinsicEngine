module;

#include <cstdint>

export module Extrinsic.Graphics.Pass.Surface.MinimalDebug;

import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Types;
import Extrinsic.Graphics.CullingSystem;
import Extrinsic.Graphics.GpuWorld;

namespace Extrinsic::Graphics
{
    // GRAPHICS-032B — CPU-mock command body for the minimal-debug-surface
    // recipe. Mirrors the ForwardSurfacePass call shape (BindPipeline,
    // BindIndexBuffer, PushConstants, DrawIndexedIndirectCount over the
    // SurfaceOpaque bucket) but uses the GRAPHICS-031A slot-0
    // default-debug-surface pipeline rather than a forward-system-owned
    // pipeline. Removed by GRAPHICS-081 once the canonical default recipe
    // records every pass body operationally.
    export class MinimalDebugSurfacePass
    {
    public:
        MinimalDebugSurfacePass() = default;

        MinimalDebugSurfacePass(const MinimalDebugSurfacePass&)            = delete;
        MinimalDebugSurfacePass& operator=(const MinimalDebugSurfacePass&) = delete;

        void SetPipeline(RHI::PipelineHandle pipeline) noexcept;

        [[nodiscard]] RHI::PipelineHandle GetPipeline() const noexcept { return m_Pipeline; }

        void Execute(RHI::ICommandContext& cmd,
                     const RHI::CameraUBO& camera,
                     const GpuWorld&       gpuWorld,
                     const CullingSystem&  culling,
                     std::uint32_t         frameIndex);

    private:
        RHI::PipelineHandle m_Pipeline{};
    };
}
