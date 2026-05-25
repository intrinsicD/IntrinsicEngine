module;

#include <cstdint>
#include <span>

export module Extrinsic.Graphics.Pass.VisualizationOverlay;

import Extrinsic.Graphics.VisualizationOverlayUploadHelper;
import Extrinsic.Graphics.VisualizationPackets;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Handles;

namespace Extrinsic::Graphics
{
    // GRAPHICS-078 Slice B — operational shell class for the canonical
    // default-recipe `VisualizationOverlayPass`. Mirrors the
    // `TransientDebugSurfacePass` shape: default-constructible (no
    // system dependency), per-kind pipeline accessors for fail-closed
    // prerequisite checks, and (Slice B) an `ExecuteVectorFields(...)`
    // body that iterates the vector-field packet span and records
    // `BindPipeline + PushConstants(BDA) + Draw(2 * ElementCount, 1, 0, 0)`
    // per packet. Each packet independently switches between the
    // depth-tested and always-on-top variants based on its
    // `DepthTested` flag. Slice C adds the matching `ExecuteIsolines`
    // body.
    //
    // Push constant layout: 16 bytes packing the helper's vertex buffer
    // BDA + a per-draw `FirstVertex` index so the BDA-fetch vertex
    // shader can address the right packet in the shared upload buffer.
    // Both kinds share the same 16-byte payload shape; separate types
    // per kind keep room for per-kind evolution (e.g. per-glyph width
    // or per-iso polyline expansion push fields in a follow-up task).
    export struct VisualizationVectorFieldPushConstants
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

        // GRAPHICS-078 Slice B — records the per-packet `BindPipeline +
        // PushConstants + Draw(2 * ElementCount, 1, 0, 0)` shape
        // against `cmd`. `uploadResult` carries the helper's vertex
        // buffer handle/BDA and total endpoint count for the frame.
        // The caller has already validated that both pipeline handles
        // are valid (see `Graphics.Renderer.cpp`'s
        // `RecordVisualizationOverlayPass` gate). Increments
        // `diagnostics.VectorFieldRecordsSubmitted` per submitted
        // packet and `diagnostics.VectorFieldRecordsRecorded` per
        // packet whose draw record actually lands; sets
        // `diagnostics.UploadOverflowCount` when the upload helper
        // reported an overflow.
        void ExecuteVectorFields(RHI::ICommandContext& cmd,
                                 std::span<const VectorFieldOverlayPacket> vectorFields,
                                 const VisualizationVectorFieldUploadResult& uploadResult,
                                 VisualizationOverlayUploadDiagnostics& diagnostics);

    private:
        RHI::PipelineHandle m_VectorFieldDepthTestedPipeline{};
        RHI::PipelineHandle m_VectorFieldAlwaysOnTopPipeline{};
        RHI::PipelineHandle m_IsolineDepthTestedPipeline{};
        RHI::PipelineHandle m_IsolineAlwaysOnTopPipeline{};
    };
}
