module;

#include <cstddef>
#include <cstdint>
#include <span>

module Extrinsic.Graphics.Pass.VisualizationOverlay;

import Extrinsic.RHI.CommandContext;

namespace Extrinsic::Graphics
{
    void VisualizationOverlayPass::SetVectorFieldDepthTestedPipeline(const RHI::PipelineHandle pipeline) noexcept
    {
        m_VectorFieldDepthTestedPipeline = pipeline;
    }

    void VisualizationOverlayPass::SetVectorFieldAlwaysOnTopPipeline(const RHI::PipelineHandle pipeline) noexcept
    {
        m_VectorFieldAlwaysOnTopPipeline = pipeline;
    }

    void VisualizationOverlayPass::SetIsolineDepthTestedPipeline(const RHI::PipelineHandle pipeline) noexcept
    {
        m_IsolineDepthTestedPipeline = pipeline;
    }

    void VisualizationOverlayPass::SetIsolineAlwaysOnTopPipeline(const RHI::PipelineHandle pipeline) noexcept
    {
        m_IsolineAlwaysOnTopPipeline = pipeline;
    }

    void VisualizationOverlayPass::ExecuteVectorFields(
        RHI::ICommandContext& cmd,
        const std::span<const VectorFieldOverlayPacket> vectorFields,
        const VisualizationVectorFieldUploadResult& uploadResult,
        const std::uint64_t sceneTableBDA,
        VisualizationOverlayUploadDiagnostics& diagnostics)
    {
        diagnostics.VectorFieldRecordsSubmitted += static_cast<std::uint64_t>(vectorFields.size());

        if (vectorFields.empty() || !uploadResult.Uploaded || sceneTableBDA == 0u)
        {
            if (uploadResult.Overflow)
            {
                ++diagnostics.UploadOverflowCount;
            }
            diagnostics.VectorFieldPacketsSkipped += static_cast<std::uint64_t>(vectorFields.size());
            return;
        }

        // Consecutive packets sharing a variant bind the pipeline once.
        int lastDepthTested = -1;
        std::uint32_t recordedPackets = 0u;
        for (std::size_t packetIndex = 0; packetIndex < vectorFields.size(); ++packetIndex)
        {
            const VectorFieldOverlayPacket& packet = vectorFields[packetIndex];
            const std::uint32_t recordIndex =
                packetIndex < uploadResult.RecordIndexForPacket.size()
                    ? uploadResult.RecordIndexForPacket[packetIndex]
                    : VisualizationVectorFieldUploadResult::kInvalidRecord;
            const std::uint32_t glyphCount = VectorFieldGlyphCount(packet);
            if (recordIndex == VisualizationVectorFieldUploadResult::kInvalidRecord ||
                glyphCount == 0u)
            {
                ++diagnostics.VectorFieldPacketsSkipped;
                continue;
            }

            const int packetDepthTested = packet.DepthTested ? 1 : 0;
            if (packetDepthTested != lastDepthTested)
            {
                cmd.BindPipeline(packet.DepthTested
                    ? m_VectorFieldDepthTestedPipeline
                    : m_VectorFieldAlwaysOnTopPipeline);
                lastDepthTested = packetDepthTested;
            }

            const VisualizationVectorFieldPushConstants pc{
                .SceneTableBDA = sceneTableBDA,
                .RecordBufferBDA = uploadResult.RecordBufferBDA,
                .RecordIndex = recordIndex,
                .Reserved = 0u,
            };
            cmd.PushConstants(&pc, static_cast<std::uint32_t>(sizeof(pc)));
            cmd.Draw(kVisualizationVectorFieldGlyphVertexCount, glyphCount, 0u, 0u);
            diagnostics.VectorFieldGlyphsRecorded += glyphCount;
            ++recordedPackets;
        }

        diagnostics.VectorFieldRecordsRecorded += recordedPackets;
    }

    void VisualizationOverlayPass::ExecuteIsolines(
        RHI::ICommandContext& cmd,
        const std::span<const IsolineOverlayPacket> isolines,
        const VisualizationIsolineUploadResult& uploadResult,
        VisualizationOverlayUploadDiagnostics& diagnostics)
    {
        diagnostics.IsolineRecordsSubmitted += static_cast<std::uint64_t>(isolines.size());

        if (isolines.empty() || !uploadResult.Uploaded)
        {
            if (uploadResult.Overflow)
            {
                ++diagnostics.UploadOverflowCount;
            }
            return;
        }

        // Same per-packet pipeline-variant tracking as ExecuteVectorFields:
        // consecutive packets that share a `DepthTested` flag emit a
        // single `BindPipeline` followed by multiple
        // `PushConstants + Draw` pairs. First packet always forces a
        // bind (`lastDepthTested == -1` sentinel).
        int lastDepthTested = -1;
        std::uint32_t recordedPackets = 0u;
        std::uint32_t cumulativeFirstVertex = 0u;
        for (const IsolineOverlayPacket& packet : isolines)
        {
            const int packetDepthTested = packet.DepthTested ? 1 : 0;
            if (packetDepthTested != lastDepthTested)
            {
                const RHI::PipelineHandle pipeline = packet.DepthTested
                    ? m_IsolineDepthTestedPipeline
                    : m_IsolineAlwaysOnTopPipeline;
                cmd.BindPipeline(pipeline);
                lastDepthTested = packetDepthTested;
            }

            VisualizationIsolinePushConstants pc{};
            pc.VertexBufferBDA = uploadResult.VertexBufferBDA;
            pc.FirstVertex = cumulativeFirstVertex;
            pc.Reserved = 0u;
            cmd.PushConstants(&pc, static_cast<std::uint32_t>(sizeof(pc)));

            // `Draw(2 * IsoValueCount, 1, 0, 0)` per packet — each iso
            // value is a placeholder line segment (two vertices) on
            // the CPU/null path. Per-packet `FirstVertex` is carried
            // in the push block so the contract-test assertion can
            // pin the canonical 4-argument shape regardless of packet
            // count.
            const std::uint32_t packetVertexCount = packet.IsoValueCount * 2u;
            cmd.Draw(packetVertexCount, 1u, 0u, 0u);
            cumulativeFirstVertex += packetVertexCount;
            ++recordedPackets;
        }

        diagnostics.IsolineRecordsRecorded += recordedPackets;
    }
}
