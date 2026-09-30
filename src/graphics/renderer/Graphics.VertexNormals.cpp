module;
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>
module Extrinsic.Graphics.VertexNormals;

import Extrinsic.Graphics.ComputeParallelPrimitives;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Types;

namespace Extrinsic::Graphics
{
    namespace
    {
        enum Mode : std::uint32_t { Faces, Vertices, FaceNormals };
        struct Push
        {
            std::uint64_t Positions{}, FaceOffsets{}, Corners{}, VertexOffsets{}, Incidence{}, LiveRows{}, Areas{}, Result{}, Stats{};
            std::uint32_t Mode{}, Count{}, Weighting{}, First{};
            double Epsilon{};
            float FallbackX{}, FallbackY{}, FallbackZ{}, Pad1{};
        };
        static_assert(sizeof(Push) == 112 && offsetof(Push, Epsilon) == 88 && offsetof(Push, FallbackX) == 96);
        constexpr auto kShaderAccess = RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::ShaderWrite;
        constexpr std::uint64_t kAreaBytesPerFace = 6u * sizeof(double);
    }

    VertexNormalsTopologyLayout PackVertexNormalsTopology(
        const std::span<const std::uint32_t> faceOffsets, const std::span<const std::uint32_t> corners,
        const std::uint32_t vertices, const std::span<const std::uint32_t> liveRows, std::vector<std::uint32_t>& out,
        const VertexNormalsBundleKind kind)
    {
        VertexNormalsTopologyLayout layout{};
        layout.Kind = kind;
        out.clear();
        const bool faceRows = kind == VertexNormalsBundleKind::FaceNormals;
        // Limits first, before anything sized by the inputs is allocated.
        if (faceOffsets.empty() || faceOffsets.size() - 1u > VertexNormalsMaxFaces || vertices == 0u ||
            vertices > VertexNormalsMaxVertices || corners.size() > VertexNormalsMaxCorners)
            return {};
        const auto faces = std::uint32_t(faceOffsets.size() - 1u);
        const std::uint32_t rowBound = faceRows ? faces : vertices;
        if (liveRows.size() > rowBound) return {};
        if (faceOffsets.front() != 0u || faceOffsets.back() != corners.size() ||
            !std::is_sorted(faceOffsets.begin(), faceOffsets.end()))
            return {};
        for (const auto corner : corners)
            if (corner >= vertices && !(faceRows && corner == VertexNormalsDeletedCorner)) return {};
        for (const auto row : liveRows)
            if (row >= rowBound) return {};
        // Incidences per vertex, then their (face, corner) pairs in ascending face order: the
        // reference visits faces in index order and each face's corners in ring order. The
        // face-normals kernel walks rings only, so its bundle carries none.
        std::vector<std::uint32_t> vertexOffsets(std::size_t(vertices) + 1u, 0u);
        std::vector<std::uint32_t> incidence;
        if (!faceRows)
        {
            for (const auto corner : corners) ++vertexOffsets[std::size_t(corner) + 1u];
            for (std::size_t v = 0; v < vertices; ++v) vertexOffsets[v + 1u] += vertexOffsets[v];
            incidence.resize(std::size_t(corners.size()) * 2u);
            std::vector<std::uint32_t> cursor(vertexOffsets.begin(), vertexOffsets.end() - 1);
            for (std::uint32_t f = 0; f < faces; ++f)
                for (std::uint32_t c = faceOffsets[f]; c < faceOffsets[f + 1u]; ++c)
                {
                    const auto slot = cursor[corners[c]]++;
                    incidence[std::size_t(slot) * 2u] = f;
                    incidence[std::size_t(slot) * 2u + 1u] = c - faceOffsets[f];
                }
        }
        layout.Faces = faces;
        layout.Vertices = vertices;
        layout.LiveRows = std::uint32_t(liveRows.size());
        layout.Corners = std::uint32_t(corners.size());
        layout.Incidences = std::uint32_t(incidence.size() / 2u);
        const auto append = [&](const std::span<const std::uint32_t> words) {
            const auto at = std::uint32_t(out.size());
            out.insert(out.end(), words.begin(), words.end());
            return at;
        };
        layout.FaceOffsetsAt = append(faceOffsets);
        layout.CornersAt = append(corners);
        layout.VertexOffsetsAt = append(vertexOffsets);
        layout.IncidenceAt = append(incidence);
        layout.LiveRowsAt = append(liveRows);
        layout.Words = std::uint32_t(out.size());
        return layout;
    }

    VertexNormalsTopologyLayout UnpackVertexNormalsTopologyLayout(
        const std::uint32_t faces, const std::uint32_t vertices, const std::uint32_t liveRows, const std::uint32_t words,
        const VertexNormalsBundleKind kind)
    {
        const bool faceRows = kind == VertexNormalsBundleKind::FaceNormals;
        const std::uint64_t fixed = std::uint64_t(faces) + 1u + std::uint64_t(vertices) + 1u + liveRows;
        // Corners take one word each, plus two incidence words for VertexNormals.
        const std::uint64_t perCorner = faceRows ? 1u : 3u;
        if (vertices == 0u || faces > VertexNormalsMaxFaces || vertices > VertexNormalsMaxVertices ||
            liveRows > (faceRows ? faces : vertices) || words < fixed || (words - fixed) % perCorner != 0u ||
            (words - fixed) / perCorner > VertexNormalsMaxCorners)
            return {};
        VertexNormalsTopologyLayout layout{};
        layout.Kind = kind;
        layout.Faces = faces;
        layout.Vertices = vertices;
        layout.LiveRows = liveRows;
        layout.Corners = std::uint32_t((words - fixed) / perCorner);
        layout.Incidences = faceRows ? 0u : layout.Corners;
        layout.FaceOffsetsAt = 0u;
        layout.CornersAt = faces + 1u;
        layout.VertexOffsetsAt = layout.CornersAt + layout.Corners;
        layout.IncidenceAt = layout.VertexOffsetsAt + vertices + 1u;
        layout.LiveRowsAt = layout.IncidenceAt + 2u * layout.Incidences;
        layout.Words = words;
        return layout;
    }

    struct VertexNormalsWorkspace::Impl
    {
        RHI::IDevice& Device;
        RHI::PipelineHandle Pipeline{};
        RHI::BufferHandle Areas{}, Stats{};
        std::uint64_t AreaBytes{};
        explicit Impl(RHI::IDevice& device) : Device(device) {}
        ~Impl()
        {
            for (auto buffer : {Areas, Stats})
                if (buffer.IsValid()) Device.DestroyBuffer(buffer);
            if (Pipeline.IsValid()) Device.DestroyPipeline(Pipeline);
        }
        bool EnsurePipeline()
        {
            if (!Pipeline.IsValid())
                Pipeline = CreateComputePipeline(Device, "shaders/vertex_normals.comp.spv", sizeof(Push), "VertexNormals");
            return Pipeline.IsValid();
        }
        bool EnsureBuffers(const std::uint64_t areaBytes)
        {
            if (!Areas.IsValid() || AreaBytes < areaBytes)
            {
                if (Areas.IsValid()) Device.DestroyBuffer(Areas);
                Areas = Device.CreateBuffer({.SizeBytes = std::max<std::uint64_t>(areaBytes, kAreaBytesPerFace),
                                             .Usage = RHI::BufferUsage::Storage, .DebugName = "VertexNormals.Areas"});
                AreaBytes = areaBytes;
            }
            if (!Stats.IsValid())
                Stats = Device.CreateBuffer({.SizeBytes = StatsReadbackBytes,
                                             .Usage = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferSrc | RHI::BufferUsage::TransferDst,
                                             .DebugName = "VertexNormals.Stats"});
            return Areas.IsValid() && Stats.IsValid();
        }
    };

    VertexNormalsWorkspace::VertexNormalsWorkspace(RHI::IDevice& device) : m_Impl(std::make_unique<Impl>(device)) {}
    VertexNormalsWorkspace::~VertexNormalsWorkspace() = default;

    RHI::BufferHandle VertexNormalsWorkspace::Record(RHI::ICommandContext& commands, const VertexNormalsGpuParams& params,
                                                     const VertexNormalsResidentIo& io)
    {
        auto& s = *m_Impl;
        const auto& layout = io.Layout;
        const bool faceRows = layout.Kind == VertexNormalsBundleKind::FaceNormals;
        const std::uint32_t outputRows = faceRows ? layout.Faces : layout.Vertices;
        if (!io.Positions.Valid() || !io.Topology.Valid() || !io.Output.Valid() || layout.Words == 0u ||
            layout.Vertices == 0u || layout.Vertices > VertexNormalsMaxVertices || layout.Faces > VertexNormalsMaxFaces ||
            layout.Corners > VertexNormalsMaxCorners || layout.LiveRows > outputRows ||
            io.OutputBytes < std::uint64_t(outputRows) * 3u * sizeof(float) ||
            !(params.Epsilon > 0.0) || !s.Device.IsOperational() || !s.Device.SupportsShaderFloat64() ||
            !s.EnsurePipeline() || !s.EnsureBuffers(faceRows ? kAreaBytesPerFace : std::uint64_t(std::max(layout.Faces, 1u)) * kAreaBytesPerFace))
            return {};
        const auto word = [&](const std::uint32_t at) { return io.Topology.Address + std::uint64_t(at) * 4u; };
        // Inputs uploaded by the transfer queue (positions, bundle) become shader-readable;
        // the stats start at zero; the output starts as the property's published bytes.
        commands.BufferBarrier(io.Positions.Buffer, RHI::MemoryAccess::TransferWrite | kShaderAccess, RHI::MemoryAccess::ShaderRead);
        commands.BufferBarrier(io.Topology.Buffer, RHI::MemoryAccess::TransferWrite | kShaderAccess, RHI::MemoryAccess::ShaderRead);
        commands.BufferBarrier(s.Stats, kShaderAccess | RHI::MemoryAccess::TransferRead, RHI::MemoryAccess::TransferWrite);
        commands.FillBuffer(s.Stats, 0u, StatsReadbackBytes, 0u);
        commands.BufferBarrier(s.Stats, RHI::MemoryAccess::TransferWrite, kShaderAccess);
        commands.BufferBarrier(io.Output.Buffer, kShaderAccess | RHI::MemoryAccess::TransferRead, RHI::MemoryAccess::TransferWrite);
        if (io.Base.Valid() && io.Base.Buffer != io.Output.Buffer)
        {
            commands.BufferBarrier(io.Base.Buffer, RHI::MemoryAccess::TransferWrite | kShaderAccess, RHI::MemoryAccess::TransferRead);
            commands.CopyBuffer(io.Base.Buffer, io.Output.Buffer, 0u, 0u, io.OutputBytes);
        }
        else
            commands.FillBuffer(io.Output.Buffer, 0u, io.OutputBytes, 0u);
        commands.BufferBarrier(io.Output.Buffer, RHI::MemoryAccess::TransferWrite, RHI::MemoryAccess::ShaderWrite);
        commands.BufferBarrier(s.Areas, kShaderAccess, RHI::MemoryAccess::ShaderWrite);
        commands.BindPipeline(s.Pipeline);
        Push push{.Positions = io.Positions.Address, .FaceOffsets = word(layout.FaceOffsetsAt), .Corners = word(layout.CornersAt),
                  .VertexOffsets = word(layout.VertexOffsetsAt), .Incidence = word(layout.IncidenceAt),
                  .LiveRows = word(layout.LiveRowsAt), .Areas = s.Device.GetBufferDeviceAddress(s.Areas),
                  .Result = io.Output.Address, .Stats = s.Device.GetBufferDeviceAddress(s.Stats),
                  .Weighting = std::uint32_t(params.Weighting), .Epsilon = params.Epsilon,
                  .FallbackX = params.Fallback[0], .FallbackY = params.Fallback[1], .FallbackZ = params.Fallback[2]};
        // A pass over more threads than one dispatch may cover (65535 workgroups of 64) is
        // issued in chunks; the shader adds the chunk's first index.
        const auto dispatch = [&](const std::uint32_t mode, const std::uint32_t threads) {
            push.Mode = mode;
            for (std::uint32_t first = 0; first < threads; first += VertexNormalsMaxDispatchThreads)
            {
                push.First = first;
                push.Count = std::min(threads - first, VertexNormalsMaxDispatchThreads);
                commands.PushConstants(&push, sizeof(push), 0);
                commands.Dispatch((push.Count + 63u) / 64u, 1, 1);
            }
        };
        if (faceRows)
            dispatch(Mode::FaceNormals, layout.LiveRows); // one pass: a Newell normal per processed face
        else
        {
            dispatch(Mode::Faces, layout.Faces);
            commands.BufferBarrier(s.Areas, RHI::MemoryAccess::ShaderWrite, RHI::MemoryAccess::ShaderRead);
            dispatch(Mode::Vertices, layout.LiveRows);
        }
        // Ready for the Accept readback (and any observer) and the stats copy.
        commands.BufferBarrier(io.Output.Buffer, RHI::MemoryAccess::ShaderWrite, RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::TransferRead);
        commands.BufferBarrier(s.Stats, kShaderAccess, RHI::MemoryAccess::TransferRead);
        return s.Stats;
    }
}
