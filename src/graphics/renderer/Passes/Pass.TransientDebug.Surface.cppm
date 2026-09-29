// Records transient debug lanes through RHI using renderer-provided pipelines.
module;

#include <cstdint>
#include <span>

#include <glm/mat4x4.hpp>

export module Extrinsic.Graphics.Pass.TransientDebug.Surface;

import Extrinsic.Graphics.RenderDiagnostics;
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
    // Push constant layout: the helper's vertex buffer BDA and a per-draw `FirstVertex`
    // index (so the BDA-fetch vertex shader addresses the right packet in the shared upload
    // buffer), then the camera's view-projection: packet coordinates are world space.
    // Points add their world-space radius and the projection's pixels per unit at unit depth
    // (Projection[1][1] * viewport height / 2), so the shader sizes them in pixels.
    export struct TransientDebugTrianglePushConstants
    {
        std::uint64_t VertexBufferBDA;
        std::uint32_t FirstVertex;
        std::uint32_t Reserved;
        glm::mat4 ViewProjection;
    };

    export struct TransientDebugLinePushConstants
    {
        std::uint64_t VertexBufferBDA;
        std::uint32_t FirstVertex;
        std::uint32_t Reserved;
        glm::mat4 ViewProjection;
    };

    export struct TransientDebugPointPushConstants
    {
        std::uint64_t VertexBufferBDA;
        std::uint32_t FirstVertex;
        float Radius;
        glm::mat4 ViewProjection;
        float PixelsPerUnit;
        float Reserved[3];
    };

    // UI-067: sphere points. One draw per run of packets sharing depth mode and radius, six
    // billboard vertices per point; View is world -> view as a 4x3 (scalar layout) so the block
    // fits the guaranteed 128 bytes.
    export struct TransientDebugSpherePushConstants
    {
        std::uint64_t VertexBufferBDA;
        std::uint32_t FirstVertex;
        float Radius;
        float View[12]; // glm::mat4x3, column-major
        glm::mat4 Projection;
    };
    static_assert(sizeof(TransientDebugSpherePushConstants) == 128);

    // Camera for one frame's transient debug primitives. The default (identity, no point
    // scaling) makes packet coordinates clip-space, which the pass contract tests use.
    export struct TransientDebugView
    {
        glm::mat4 ViewProjection{1.0f};
        float PixelsPerUnit{0.0f};
        // Sphere points need the view and projection separately; without a camera they are
        // drawn as flat sprites.
        glm::mat4 View{1.0f};
        glm::mat4 Projection{1.0f};
        bool HasCamera{false};
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
        // Optional (UI-067): without them sphere packets fall back to flat sprites.
        void SetSphereDepthTestedPipeline(RHI::PipelineHandle pipeline) noexcept;
        void SetSphereAlwaysOnTopPipeline(RHI::PipelineHandle pipeline) noexcept;

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
                              TransientDebugUploadDiagnostics& diagnostics,
                              const TransientDebugView& view = {});

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
                          TransientDebugUploadDiagnostics& diagnostics,
                          const TransientDebugView& view = {});

        void ExecutePoints(RHI::ICommandContext& cmd,
                           std::span<const DebugPointPacket> points,
                           const TransientDebugPointUploadResult& uploadResult,
                           TransientDebugUploadDiagnostics& diagnostics,
                           const TransientDebugView& view = {});

    private:
        RHI::PipelineHandle m_TriangleDepthTestedPipeline{};
        RHI::PipelineHandle m_TriangleAlwaysOnTopPipeline{};
        RHI::PipelineHandle m_LineDepthTestedPipeline{};
        RHI::PipelineHandle m_LineAlwaysOnTopPipeline{};
        RHI::PipelineHandle m_PointDepthTestedPipeline{};
        RHI::PipelineHandle m_PointAlwaysOnTopPipeline{};
        RHI::PipelineHandle m_SphereDepthTestedPipeline{};
        RHI::PipelineHandle m_SphereAlwaysOnTopPipeline{};
    };
}
