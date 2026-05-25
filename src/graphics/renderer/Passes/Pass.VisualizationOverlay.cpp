module;

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
        VisualizationOverlayUploadDiagnostics& diagnostics)
    {
        diagnostics.VectorFieldRecordsSubmitted += static_cast<std::uint64_t>(vectorFields.size());

        if (vectorFields.empty() || !uploadResult.Uploaded)
        {
            if (uploadResult.Overflow)
            {
                ++diagnostics.UploadOverflowCount;
            }
            return;
        }

        // Track the last-bound variant so consecutive packets that
        // share a `DepthTested` flag emit a single `BindPipeline`
        // followed by multiple `PushConstants + Draw` pairs. The first
        // packet always forces a bind (`lastDepthTested == -1`
        // sentinel).
        int lastDepthTested = -1;
        std::uint32_t recordedPackets = 0u;
        std::uint32_t cumulativeFirstVertex = 0u;
        for (const VectorFieldOverlayPacket& packet : vectorFields)
        {
            const int packetDepthTested = packet.DepthTested ? 1 : 0;
            if (packetDepthTested != lastDepthTested)
            {
                const RHI::PipelineHandle pipeline = packet.DepthTested
                    ? m_VectorFieldDepthTestedPipeline
                    : m_VectorFieldAlwaysOnTopPipeline;
                cmd.BindPipeline(pipeline);
                lastDepthTested = packetDepthTested;
            }

            VisualizationVectorFieldPushConstants pc{};
            pc.VertexBufferBDA = uploadResult.VertexBufferBDA;
            pc.FirstVertex = cumulativeFirstVertex;
            pc.Reserved = 0u;
            cmd.PushConstants(&pc, static_cast<std::uint32_t>(sizeof(pc)));

            // `Draw(2 * ElementCount, 1, 0, 0)` per packet — each glyph
            // is two vertices (anchor + tip) fetched via BDA at
            // `FirstVertex + gl_VertexIndex`. Per-packet `FirstVertex`
            // is carried in the push block so the contract-test
            // assertion can pin the canonical 4-argument shape
            // regardless of packet count.
            const std::uint32_t packetVertexCount = packet.ElementCount * 2u;
            cmd.Draw(packetVertexCount, 1u, 0u, 0u);
            cumulativeFirstVertex += packetVertexCount;
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
