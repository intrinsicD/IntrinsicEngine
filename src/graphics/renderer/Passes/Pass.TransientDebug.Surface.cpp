module;

#include <cstdint>
#include <span>

module Extrinsic.Graphics.Pass.TransientDebug.Surface;

import Extrinsic.RHI.CommandContext;

namespace Extrinsic::Graphics
{
    void TransientDebugSurfacePass::SetTriangleDepthTestedPipeline(const RHI::PipelineHandle pipeline) noexcept
    {
        m_TriangleDepthTestedPipeline = pipeline;
    }

    void TransientDebugSurfacePass::SetTriangleAlwaysOnTopPipeline(const RHI::PipelineHandle pipeline) noexcept
    {
        m_TriangleAlwaysOnTopPipeline = pipeline;
    }

    void TransientDebugSurfacePass::ExecuteTriangles(
        RHI::ICommandContext& cmd,
        const std::span<const DebugTrianglePacket> triangles,
        const TransientDebugTriangleUploadResult& uploadResult,
        TransientDebugUploadDiagnostics& diagnostics)
    {
        diagnostics.TriangleRecordsSubmitted += static_cast<std::uint64_t>(triangles.size());

        if (triangles.empty() || !uploadResult.Uploaded)
        {
            if (uploadResult.Overflow)
            {
                ++diagnostics.UploadOverflowCount;
            }
            return;
        }

        // Track the last-bound variant so consecutive packets that share
        // a `DepthTested` flag emit a single `BindPipeline` followed by
        // multiple `PushConstants + Draw` pairs. The first packet always
        // forces a bind (`lastDepthTested == -1` sentinel).
        int lastDepthTested = -1;
        std::uint32_t recordedPackets = 0u;
        for (std::size_t packetIndex = 0; packetIndex < triangles.size(); ++packetIndex)
        {
            const DebugTrianglePacket& packet = triangles[packetIndex];
            const int packetDepthTested = packet.DepthTested ? 1 : 0;
            if (packetDepthTested != lastDepthTested)
            {
                const RHI::PipelineHandle pipeline = packet.DepthTested
                    ? m_TriangleDepthTestedPipeline
                    : m_TriangleAlwaysOnTopPipeline;
                cmd.BindPipeline(pipeline);
                lastDepthTested = packetDepthTested;
            }

            TransientDebugTrianglePushConstants pc{};
            pc.VertexBufferBDA = uploadResult.VertexBufferBDA;
            pc.FirstVertex = static_cast<std::uint32_t>(packetIndex * 3u);
            pc.Reserved = 0u;
            cmd.PushConstants(&pc, static_cast<std::uint32_t>(sizeof(pc)));

            // `Draw(3, 1, 0, 0)` per packet — the per-packet `FirstVertex`
            // is carried in the push block rather than `gl_VertexIndex`
            // base so the contract-test assertion can pin the canonical
            // 4-argument shape regardless of packet count.
            cmd.Draw(3u, 1u, 0u, 0u);
            ++recordedPackets;
        }

        diagnostics.TriangleRecordsRecorded += recordedPackets;
    }
}
