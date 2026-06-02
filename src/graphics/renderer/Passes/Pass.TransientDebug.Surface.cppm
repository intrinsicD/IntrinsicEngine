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
    // GRAPHICS-077 Slices B + C — operational shell class for the
    // canonical default-recipe `TransientDebugSurfacePass`. Mirrors the
    // `PresentPass` shape: default-constructible (no system dependency),
    // per-lane pipeline accessors for fail-closed prerequisite checks, and
    // per-lane `Execute{Triangles,Lines,Points}(...)` bodies that iterate each
    // lane's packet span and record `BindPipeline + PushConstants(BDA)
    // + Draw(N, 1, 0, 0)` per packet (N = 3 / 2 / 1 for triangles /
    // lines / points). Each lane independently switches between the
    // depth-tested and always-on-top variants based on each packet's
    // `DepthTested` flag.
    //
    // Push constant layout: 16 bytes packing the helper's vertex buffer
    // BDA + a per-draw `FirstVertex` index so the BDA-fetch vertex
    // shader can address the right packet in the shared upload buffer.
    // All three lanes share the same 16-byte payload shape; separate
    // types per lane keep room for per-lane evolution (e.g. line width
    // or point radius push fields in a follow-up task).
    export struct TransientDebugTrianglePushConstants
    {
        std::uint64_t VertexBufferBDA;
        std::uint32_t FirstVertex;
        std::uint32_t Reserved;
    };

    export struct TransientDebugLinePushConstants
    {
        std::uint64_t VertexBufferBDA;
        std::uint32_t FirstVertex;
        std::uint32_t Reserved;
    };

    export struct TransientDebugPointPushConstants
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
        void SetLineDepthTestedPipeline(RHI::PipelineHandle pipeline) noexcept;
        void SetLineAlwaysOnTopPipeline(RHI::PipelineHandle pipeline) noexcept;
        void SetPointDepthTestedPipeline(RHI::PipelineHandle pipeline) noexcept;
        void SetPointAlwaysOnTopPipeline(RHI::PipelineHandle pipeline) noexcept;

        [[nodiscard]] RHI::PipelineHandle GetTriangleDepthTestedPipeline() const noexcept
        {
            return m_TriangleDepthTestedPipeline;
        }
        [[nodiscard]] RHI::PipelineHandle GetTriangleAlwaysOnTopPipeline() const noexcept
        {
            return m_TriangleAlwaysOnTopPipeline;
        }
        [[nodiscard]] RHI::PipelineHandle GetLineDepthTestedPipeline() const noexcept
        {
            return m_LineDepthTestedPipeline;
        }
        [[nodiscard]] RHI::PipelineHandle GetLineAlwaysOnTopPipeline() const noexcept
        {
            return m_LineAlwaysOnTopPipeline;
        }
        [[nodiscard]] RHI::PipelineHandle GetPointDepthTestedPipeline() const noexcept
        {
            return m_PointDepthTestedPipeline;
        }
        [[nodiscard]] RHI::PipelineHandle GetPointAlwaysOnTopPipeline() const noexcept
        {
            return m_PointAlwaysOnTopPipeline;
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

        // GRAPHICS-077 Slice C — line + point variants. Same shape as
        // `ExecuteTriangles`: per-packet `BindPipeline(variant) +
        // PushConstants(16) + Draw(N, 1, 0, 0)` (N = 2 / 1), per-
        // packet variant selection based on `DepthTested`, per-lane
        // diagnostic counter increments. The caller has already
        // validated the lane's pipeline handles via the renderer-side
        // gate in `RecordTransientDebugSurfacePass`.
        void ExecuteLines(RHI::ICommandContext& cmd,
                          std::span<const DebugLinePacket> lines,
                          const TransientDebugLineUploadResult& uploadResult,
                          TransientDebugUploadDiagnostics& diagnostics);

        void ExecutePoints(RHI::ICommandContext& cmd,
                           std::span<const DebugPointPacket> points,
                           const TransientDebugPointUploadResult& uploadResult,
                           TransientDebugUploadDiagnostics& diagnostics);

    private:
        RHI::PipelineHandle m_TriangleDepthTestedPipeline{};
        RHI::PipelineHandle m_TriangleAlwaysOnTopPipeline{};
        RHI::PipelineHandle m_LineDepthTestedPipeline{};
        RHI::PipelineHandle m_LineAlwaysOnTopPipeline{};
        RHI::PipelineHandle m_PointDepthTestedPipeline{};
        RHI::PipelineHandle m_PointAlwaysOnTopPipeline{};
    };
}
