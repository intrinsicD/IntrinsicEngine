module;

#include <cstdint>
#include <span>

export module Extrinsic.Graphics.Pass.TransientDebug.Surface;

import Extrinsic.Graphics.RenderWorld;
import Extrinsic.Graphics.TransientDebugUploadHelper;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Handles;

namespace Extrinsic::Graphics
{
    // GRAPHICS-077 Slice B — operational shell class for the canonical
    // default-recipe `TransientDebugSurfacePass`. Mirrors the
    // `PresentPass` / `MinimalDebugPresentPass` shape: default-
    // constructible (no system dependency), per-lane pipeline accessors
    // for fail-closed prerequisite checks, and an `ExecuteTriangles(...)`
    // body that iterates triangle packets and records `BindPipeline +
    // PushConstants(BDA) + Draw(3, 1, firstVertex, 0)` per packet,
    // switching between the depth-tested and always-on-top variants
    // based on each packet's `DepthTested` flag.
    //
    // Slice C extends this to the line + point lanes.
    //
    // Push constant layout: 16 bytes packing the helper's vertex buffer
    // BDA + a per-draw `FirstVertex` index so the BDA-fetch vertex
    // shader can address the right triangle in the shared upload buffer.
    export struct TransientDebugTrianglePushConstants
    {
        std::uint64_t VertexBufferBDA;
        std::uint32_t FirstVertex;
        std::uint32_t Reserved;
    };

    export class TransientDebugSurfacePass
    {
    public:
        TransientDebugSurfacePass() = default;

        TransientDebugSurfacePass(const TransientDebugSurfacePass&)            = delete;
        TransientDebugSurfacePass& operator=(const TransientDebugSurfacePass&) = delete;

        void SetTriangleDepthTestedPipeline(RHI::PipelineHandle pipeline) noexcept;
        void SetTriangleAlwaysOnTopPipeline(RHI::PipelineHandle pipeline) noexcept;

        [[nodiscard]] RHI::PipelineHandle GetTriangleDepthTestedPipeline() const noexcept
        {
            return m_TriangleDepthTestedPipeline;
        }
        [[nodiscard]] RHI::PipelineHandle GetTriangleAlwaysOnTopPipeline() const noexcept
        {
            return m_TriangleAlwaysOnTopPipeline;
        }

        // Records the per-packet `BindPipeline + PushConstants + Draw(3, 1, ...)`
        // shape against `cmd`. `uploadResult` carries the helper's vertex
        // buffer handle/BDA and total vertex count for the frame. The
        // caller has already validated that both pipeline handles are
        // valid (see `Graphics.Renderer.cpp`'s
        // `RecordTransientDebugSurfacePass` gate). Increments
        // `diagnostics.TriangleRecordsSubmitted` per submitted packet and
        // `diagnostics.TriangleRecordsRecorded` per packet whose draw
        // record actually lands; sets `diagnostics.UploadOverflowCount`
        // when the upload helper reported an overflow.
        void ExecuteTriangles(RHI::ICommandContext& cmd,
                              std::span<const DebugTrianglePacket> triangles,
                              const TransientDebugTriangleUploadResult& uploadResult,
                              TransientDebugUploadDiagnostics& diagnostics);

    private:
        RHI::PipelineHandle m_TriangleDepthTestedPipeline{};
        RHI::PipelineHandle m_TriangleAlwaysOnTopPipeline{};
    };
}
