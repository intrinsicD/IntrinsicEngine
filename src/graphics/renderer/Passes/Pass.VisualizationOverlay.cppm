// Records visualization overlays from copied packets and prepared GPU uploads.
module;

#include <cstdint>
#include <span>

export module Extrinsic.Graphics.Pass.VisualizationOverlay;

import Extrinsic.Graphics.RenderDiagnostics;
import Extrinsic.Graphics.VisualizationOverlayUploadHelper;
import Extrinsic.Graphics.VisualizationPackets;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Handles;

namespace Extrinsic::Graphics
{
    // Records the canonical default-recipe `VisualizationOverlayPass` from
    // copied packets. Each packet independently selects the depth-tested or
    // always-on-top pipeline variant through its `DepthTested` flag.
    //
    // Vector fields draw one instanced arrow glyph per sampled source row:
    // `Draw(kVisualizationVectorFieldGlyphVertexCount, glyphCount, 0, 0)`.
    // The push block names the GpuScene table (camera and viewport) and the
    // packet's draw record; the vertex shader reads anchors, vectors and live
    // rows through the record's buffer addresses. Isolines keep the packed
    // fixture-vertex shape: `Draw(2 * IsoValueCount, 1, 0, 0)` per packet.
    export struct VisualizationVectorFieldPushConstants
    {
        std::uint64_t SceneTableBDA;
        std::uint64_t RecordBufferBDA;
        std::uint32_t RecordIndex;
        std::uint32_t Reserved;
    };
    static_assert(sizeof(VisualizationVectorFieldPushConstants) == 24u);

    export struct VisualizationIsolinePushConstants
    {
        std::uint64_t VertexBufferBDA;
        std::uint32_t FirstVertex;
        std::uint32_t Reserved;
    };

    export class VisualizationOverlayPass
    {
    public:
        VisualizationOverlayPass() = default;

        VisualizationOverlayPass(const VisualizationOverlayPass&)            = delete;
        VisualizationOverlayPass& operator=(const VisualizationOverlayPass&) = delete;

        void SetVectorFieldDepthTestedPipeline(RHI::PipelineHandle pipeline) noexcept;
        void SetVectorFieldAlwaysOnTopPipeline(RHI::PipelineHandle pipeline) noexcept;
        void SetIsolineDepthTestedPipeline(RHI::PipelineHandle pipeline) noexcept;
        void SetIsolineAlwaysOnTopPipeline(RHI::PipelineHandle pipeline) noexcept;

        [[nodiscard]] RHI::PipelineHandle GetVectorFieldDepthTestedPipeline() const noexcept
        {
            return m_VectorFieldDepthTestedPipeline;
        }
        [[nodiscard]] RHI::PipelineHandle GetVectorFieldAlwaysOnTopPipeline() const noexcept
        {
            return m_VectorFieldAlwaysOnTopPipeline;
        }
        [[nodiscard]] RHI::PipelineHandle GetIsolineDepthTestedPipeline() const noexcept
        {
            return m_IsolineDepthTestedPipeline;
        }
        [[nodiscard]] RHI::PipelineHandle GetIsolineAlwaysOnTopPipeline() const noexcept
        {
            return m_IsolineAlwaysOnTopPipeline;
        }

        // Records every packet that received a draw record. `sceneTableBDA`
        // supplies camera matrices and viewport; the caller has already
        // validated both pipeline handles. Packets without a record count as
        // skipped; an upload overflow ticks `UploadOverflowCount`.
        void ExecuteVectorFields(RHI::ICommandContext& cmd,
                                 std::span<const VectorFieldOverlayPacket> vectorFields,
                                 const VisualizationVectorFieldUploadResult& uploadResult,
                                 std::uint64_t sceneTableBDA,
                                 VisualizationOverlayUploadDiagnostics& diagnostics);

        // GRAPHICS-078 Slice C — records the per-packet `BindPipeline +
        // PushConstants + Draw(2 * IsoValueCount, 1, 0, 0)` shape
        // against `cmd` for the isoline lane. Same per-packet variant
        // selection (`DepthTested`) and per-lane diagnostic counter
        // semantics as `ExecuteVectorFields`. `uploadResult` carries
        // the isoline-lane vertex buffer handle/BDA and total endpoint
        // count for the frame. The caller has already validated that
        // both pipeline handles are valid (see the renderer-side gate
        // in `RecordVisualizationOverlayPass`).
        void ExecuteIsolines(RHI::ICommandContext& cmd,
                             std::span<const IsolineOverlayPacket> isolines,
                             const VisualizationIsolineUploadResult& uploadResult,
                             VisualizationOverlayUploadDiagnostics& diagnostics);

    private:
        RHI::PipelineHandle m_VectorFieldDepthTestedPipeline{};
        RHI::PipelineHandle m_VectorFieldAlwaysOnTopPipeline{};
        RHI::PipelineHandle m_IsolineDepthTestedPipeline{};
        RHI::PipelineHandle m_IsolineAlwaysOnTopPipeline{};
    };
}
