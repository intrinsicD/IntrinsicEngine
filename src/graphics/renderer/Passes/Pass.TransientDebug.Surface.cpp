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

    void TransientDebugSurfacePass::SetLineDepthTestedPipeline(const RHI::PipelineHandle pipeline) noexcept
    {
        m_LineDepthTestedPipeline = pipeline;
    }

    void TransientDebugSurfacePass::SetLineAlwaysOnTopPipeline(const RHI::PipelineHandle pipeline) noexcept
    {
        m_LineAlwaysOnTopPipeline = pipeline;
    }

    void TransientDebugSurfacePass::SetPointDepthTestedPipeline(const RHI::PipelineHandle pipeline) noexcept
    {
        m_PointDepthTestedPipeline = pipeline;
    }

    void TransientDebugSurfacePass::SetPointAlwaysOnTopPipeline(const RHI::PipelineHandle pipeline) noexcept
    {
        m_PointAlwaysOnTopPipeline = pipeline;
    }

    void TransientDebugSurfacePass::SetSphereDepthTestedPipeline(const RHI::PipelineHandle pipeline) noexcept
    {
        m_SphereDepthTestedPipeline = pipeline;
    }

    void TransientDebugSurfacePass::SetSphereAlwaysOnTopPipeline(const RHI::PipelineHandle pipeline) noexcept
    {
        m_SphereAlwaysOnTopPipeline = pipeline;
    }

    void TransientDebugSurfacePass::ExecuteTriangles(
        RHI::ICommandContext& cmd,
        const std::span<const DebugTrianglePacket> triangles,
        const TransientDebugTriangleUploadResult& uploadResult,
        TransientDebugUploadDiagnostics& diagnostics,
        const TransientDebugView& view)
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
            pc.ViewProjection = view.ViewProjection;
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

    void TransientDebugSurfacePass::ExecuteLines(
        RHI::ICommandContext& cmd,
        const std::span<const DebugLinePacket> lines,
        const TransientDebugLineUploadResult& uploadResult,
        TransientDebugUploadDiagnostics& diagnostics,
        const TransientDebugView& view)
    {
        diagnostics.LineRecordsSubmitted += static_cast<std::uint64_t>(lines.size());

        if (lines.empty() || !uploadResult.Uploaded)
        {
            if (uploadResult.Overflow)
            {
                ++diagnostics.UploadOverflowCount;
            }
            return;
        }

        int lastDepthTested = -1;
        std::uint32_t recordedPackets = 0u;
        for (std::size_t packetIndex = 0; packetIndex < lines.size(); ++packetIndex)
        {
            const DebugLinePacket& packet = lines[packetIndex];
            const int packetDepthTested = packet.DepthTested ? 1 : 0;
            if (packetDepthTested != lastDepthTested)
            {
                const RHI::PipelineHandle pipeline = packet.DepthTested
                    ? m_LineDepthTestedPipeline
                    : m_LineAlwaysOnTopPipeline;
                cmd.BindPipeline(pipeline);
                lastDepthTested = packetDepthTested;
            }

            TransientDebugLinePushConstants pc{};
            pc.VertexBufferBDA = uploadResult.VertexBufferBDA;
            pc.FirstVertex = static_cast<std::uint32_t>(packetIndex * 2u);
            pc.Reserved = 0u;
            pc.ViewProjection = view.ViewProjection;
            cmd.PushConstants(&pc, static_cast<std::uint32_t>(sizeof(pc)));

            // `Draw(2, 1, 0, 0)` per packet — one line segment is two
            // vertices fetched via BDA at `FirstVertex + gl_VertexIndex`.
            cmd.Draw(2u, 1u, 0u, 0u);
            ++recordedPackets;
        }

        diagnostics.LineRecordsRecorded += recordedPackets;
    }

    void TransientDebugSurfacePass::ExecutePoints(
        RHI::ICommandContext& cmd,
        const std::span<const DebugPointPacket> points,
        const TransientDebugPointUploadResult& uploadResult,
        TransientDebugUploadDiagnostics& diagnostics,
        const TransientDebugView& view)
    {
        diagnostics.PointRecordsSubmitted += static_cast<std::uint64_t>(points.size());

        if (points.empty() || !uploadResult.Uploaded)
        {
            if (uploadResult.Overflow)
            {
                ++diagnostics.UploadOverflowCount;
            }
            return;
        }

        // Runs of consecutive packets sharing depth mode, radius and shape are one draw each
        // (UI-067): flat sprites Draw(n) point vertices, spheres Draw(6 n) billboard vertices.
        const bool spheresAvailable =
            view.HasCamera && m_SphereDepthTestedPipeline.IsValid() && m_SphereAlwaysOnTopPipeline.IsValid();
        const auto sphere = [&](const DebugPointPacket& packet) { return packet.Sphere && spheresAvailable; };
        RHI::PipelineHandle bound{};
        std::uint32_t recordedPackets = 0u;
        for (std::size_t first = 0; first < points.size();)
        {
            const DebugPointPacket& packet = points[first];
            std::size_t end = first + 1u;
            while (end < points.size() && points[end].DepthTested == packet.DepthTested &&
                   points[end].Radius == packet.Radius && sphere(points[end]) == sphere(packet))
                ++end;
            const auto count = static_cast<std::uint32_t>(end - first);
            const RHI::PipelineHandle pipeline = sphere(packet)
                ? (packet.DepthTested ? m_SphereDepthTestedPipeline : m_SphereAlwaysOnTopPipeline)
                : (packet.DepthTested ? m_PointDepthTestedPipeline : m_PointAlwaysOnTopPipeline);
            if (pipeline != bound)
            {
                cmd.BindPipeline(pipeline);
                bound = pipeline;
            }
            if (sphere(packet))
            {
                TransientDebugSpherePushConstants pc{};
                pc.VertexBufferBDA = uploadResult.VertexBufferBDA;
                pc.FirstVertex = static_cast<std::uint32_t>(first);
                pc.Radius = packet.Radius;
                for (int c = 0; c < 4; ++c)
                    for (int r = 0; r < 3; ++r) pc.View[c * 3 + r] = view.View[c][r];
                pc.Projection = view.Projection;
                cmd.PushConstants(&pc, static_cast<std::uint32_t>(sizeof(pc)));
                cmd.Draw(6u * count, 1u, 0u, 0u);
            }
            else
            {
                TransientDebugPointPushConstants pc{};
                pc.VertexBufferBDA = uploadResult.VertexBufferBDA;
                pc.FirstVertex = static_cast<std::uint32_t>(first);
                pc.Radius = packet.Radius;
                pc.ViewProjection = view.ViewProjection;
                pc.PixelsPerUnit = view.PixelsPerUnit;
                cmd.PushConstants(&pc, static_cast<std::uint32_t>(sizeof(pc)));
                // `FirstVertex + gl_VertexIndex` addresses each point of the run.
                cmd.Draw(count, 1u, 0u, 0u);
            }
            recordedPackets += count;
            first = end;
        }

        diagnostics.PointRecordsRecorded += recordedPackets;
    }
}
