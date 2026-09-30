// RUNTIME-296: the vertex-normals kernels' size contract on a mock device. A pass over more
// threads than one dispatch may cover is issued in chunks with a base index, every dispatch
// staying within the guaranteed 65535 workgroups; a bundle or a layout beyond the 32-bit
// index limits is refused before anything is allocated.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
#include <gtest/gtest.h>
#include "MockRHI.hpp"
import Extrinsic.Graphics.VertexNormals;
import Extrinsic.Graphics.PointNormals;
import Extrinsic.Graphics.GpuPropertyResidency;
import Extrinsic.RHI.Handles;
namespace G = Extrinsic::Graphics;

namespace
{
    struct PushWords
    {
        std::uint64_t Addresses[9];
        std::uint32_t Mode, Count, Weighting, First;
    };
}

TEST(VertexNormalsWorkspace, PassesLargerThanOneDispatchAreChunkedWithABaseIndex)
{
    Extrinsic::Tests::MockDevice device;
    device.ShaderFloat64 = true;
    Extrinsic::Tests::MockCommandContext commands;
    G::VertexNormalsWorkspace workspace(device);
    const auto buffer = [&](const char* name) {
        const auto handle = device.CreateBuffer({.SizeBytes = 64u, .Usage = Extrinsic::RHI::BufferUsage::Storage, .DebugName = name});
        return G::VertexNormalsResidentView{.Buffer = handle, .Address = device.GetBufferDeviceAddress(handle)};
    };
    // One face, and one live row more than a single dispatch covers.
    const std::uint32_t liveRows = G::VertexNormalsMaxDispatchThreads + 1u;
    const auto layout = G::UnpackVertexNormalsTopologyLayout(1u, liveRows, liveRows, 2u + 3u + (liveRows + 1u) + 6u + liveRows);
    ASSERT_NE(layout.Words, 0u);
    EXPECT_EQ(layout.Corners, 3u);
    const G::VertexNormalsResidentIo io{.Positions = buffer("positions"), .Topology = buffer("topology"), .Output = buffer("output"),
                                        .OutputBytes = std::uint64_t(liveRows) * 12u, .Layout = layout};
    ASSERT_TRUE(workspace.Record(commands, {}, io).IsValid());
    ASSERT_EQ(commands.DispatchRecords.size(), 3u) << "one face dispatch, two vertex chunks";
    for (const auto& dispatch : commands.DispatchRecords) EXPECT_LE(dispatch.X, 65535u);
    EXPECT_EQ(commands.DispatchRecords[0].X, 1u);
    EXPECT_EQ(commands.DispatchRecords[1].X, 65535u);
    EXPECT_EQ(commands.DispatchRecords[2].X, 1u);
    ASSERT_EQ(commands.PushConstantPayloads.size(), 3u);
    std::vector<PushWords> pushes(3);
    for (std::size_t i = 0; i < 3; ++i)
    {
        ASSERT_GE(commands.PushConstantPayloads[i].size(), sizeof(PushWords));
        std::memcpy(&pushes[i], commands.PushConstantPayloads[i].data(), sizeof(PushWords));
    }
    EXPECT_EQ(pushes[0].Mode, 0u);
    EXPECT_EQ(pushes[0].First, 0u);
    EXPECT_EQ(pushes[0].Count, 1u);
    EXPECT_EQ(pushes[1].Mode, 1u);
    EXPECT_EQ(pushes[1].First, 0u);
    EXPECT_EQ(pushes[1].Count, G::VertexNormalsMaxDispatchThreads);
    EXPECT_EQ(pushes[2].Mode, 1u);
    EXPECT_EQ(pushes[2].First, G::VertexNormalsMaxDispatchThreads);
    EXPECT_EQ(pushes[2].Count, 1u);
}

TEST(VertexNormalsWorkspace, BundlesAndLayoutsBeyondTheIndexLimitsAreRefusedBeforeAllocation)
{
    // A consistent small bundle packs and unpacks to the same layout.
    const std::vector<std::uint32_t> faceOffsets{0u, 3u, 6u}, corners{0u, 1u, 2u, 2u, 1u, 3u}, live{0u, 1u, 2u, 3u};
    std::vector<std::uint32_t> bundle;
    const auto packed = G::PackVertexNormalsTopology(faceOffsets, corners, 4u, live, bundle);
    ASSERT_EQ(packed.Words, 30u);
    const auto unpacked = G::UnpackVertexNormalsTopologyLayout(2u, 4u, 4u, packed.Words);
    EXPECT_EQ(unpacked.Corners, packed.Corners);
    EXPECT_EQ(unpacked.IncidenceAt, packed.IncidenceAt);
    EXPECT_EQ(unpacked.LiveRowsAt, packed.LiveRowsAt);
    // Vertex 1 is on both faces: incidences (0, 1) then (1, 1), ascending face order.
    EXPECT_EQ(bundle[packed.VertexOffsetsAt + 1u], 1u);
    EXPECT_EQ(bundle[packed.VertexOffsetsAt + 2u], 3u);
    EXPECT_EQ(bundle[packed.IncidenceAt + 2u], 0u);
    EXPECT_EQ(bundle[packed.IncidenceAt + 3u], 1u);
    EXPECT_EQ(bundle[packed.IncidenceAt + 4u], 1u);
    EXPECT_EQ(bundle[packed.IncidenceAt + 5u], 1u);

    // Too many vertices: refused before the per-vertex arrays would be sized.
    EXPECT_EQ(G::PackVertexNormalsTopology(faceOffsets, corners, G::VertexNormalsMaxVertices + 1u, live, bundle).Words, 0u);
    EXPECT_TRUE(bundle.empty());
    // More live rows than vertices, or a corner outside the vertices: refused.
    EXPECT_EQ(G::PackVertexNormalsTopology(faceOffsets, corners, 4u, std::vector<std::uint32_t>(5u, 0u), bundle).Words, 0u);
    EXPECT_EQ(G::PackVertexNormalsTopology(faceOffsets, corners, 3u, live, bundle).Words, 0u);
    // Layouts: faces, vertices, live rows and the derived corner count each have a limit.
    EXPECT_EQ(G::UnpackVertexNormalsTopologyLayout(G::VertexNormalsMaxFaces + 1u, 4u, 4u, 1u << 30).Words, 0u);
    EXPECT_EQ(G::UnpackVertexNormalsTopologyLayout(2u, G::VertexNormalsMaxVertices + 1u, 4u, 1u << 30).Words, 0u);
    EXPECT_EQ(G::UnpackVertexNormalsTopologyLayout(2u, 4u, 5u, 30u).Words, 0u);
    const std::uint32_t fixed = 3u + 5u + 4u;
    EXPECT_EQ(G::UnpackVertexNormalsTopologyLayout(2u, 4u, 4u, fixed + 3u * (G::VertexNormalsMaxCorners + 1u)).Words, 0u);
    EXPECT_NE(G::UnpackVertexNormalsTopologyLayout(2u, 4u, 4u, fixed + 3u * G::VertexNormalsMaxCorners).Words, 0u);
    // A layout that claims more than the limits is refused by Record too.
    Extrinsic::Tests::MockDevice device;
    device.ShaderFloat64 = true;
    Extrinsic::Tests::MockCommandContext commands;
    G::VertexNormalsWorkspace workspace(device);
    const auto handle = device.CreateBuffer({.SizeBytes = 64u, .Usage = Extrinsic::RHI::BufferUsage::Storage});
    const G::VertexNormalsResidentView view{.Buffer = handle, .Address = device.GetBufferDeviceAddress(handle)};
    auto layout = packed;
    layout.LiveRows = layout.Vertices + 1u;
    const G::VertexNormalsResidentIo io{.Positions = view, .Topology = view, .Output = view, .OutputBytes = 48u, .Layout = layout};
    EXPECT_FALSE(workspace.Record(commands, {}, io).IsValid());
    EXPECT_TRUE(commands.DispatchRecords.empty());
}

TEST(PointNormalsWorkspace, RadiusPagesBoundWorkAndKeepOutputAndStatsAcrossSubmissions)
{
    Extrinsic::Tests::MockDevice device;
    device.ShaderFloat64 = true;
    Extrinsic::Tests::MockCommandContext commands;
    G::PointNormalsWorkspace workspace(device);
    const auto buffer = device.CreateBuffer({.SizeBytes = 1026u * 12u, .Usage = Extrinsic::RHI::BufferUsage::Storage});
    const G::GpuPropertyView view{.Buffer = buffer, .Address = device.GetBufferDeviceAddress(buffer), .Bytes = 1026u * 12u};
    const G::PointNormalsResidentIo io{.Positions = view, .Output = view, .Nodes = 16, .LiveSlots = 32, .LiveCount = 1026};
    const G::PointNormalsGpuParams params{.BatchSize = 1, .RadiusSearch = true, .Radius = 1};
    const auto stats = workspace.Record(commands, params, io);
    ASSERT_TRUE(stats.IsValid());
    ASSERT_EQ(commands.DispatchRecords.size(), 1u);
    EXPECT_EQ(commands.DispatchRecords.back().X, 1u);
    EXPECT_EQ(commands.FillBufferCalls, 2); // output and aggregate stats, first page only
    const auto checkPage = [&](std::uint32_t first, std::uint32_t rows) {
        std::uint32_t words[2]{};
        EXPECT_EQ(commands.PushConstantPayloads.back().size(), 104u);
        std::memcpy(words, commands.PushConstantPayloads.back().data() + 96, sizeof(words));
        EXPECT_EQ(words[0], first);
        EXPECT_EQ(words[1], rows);
    };
    checkPage(0, 64);
    for (std::uint32_t first = 64; first < io.LiveCount; first += 64)
    {
        ASSERT_EQ(workspace.Record(commands, params, io, first), stats);
        checkPage(first, std::min(64u, io.LiveCount - first));
    }
    EXPECT_EQ(commands.DispatchRecords.size(), 17u);
    EXPECT_EQ(commands.FillBufferCalls, 2);
    EXPECT_EQ(device.CreatePipelineCount, 1);
    EXPECT_EQ(G::PointNormalsWorkspace::RowsPerSubmission(false, 1), 64u);
    EXPECT_EQ(G::PointNormalsWorkspace::RowsPerSubmission(false, 16384), 4096u);
    // Radius rows exit early on overflow, so radius pages follow the same batch clamp.
    EXPECT_EQ(G::PointNormalsWorkspace::RowsPerSubmission(true, 1), 64u);
    EXPECT_EQ(G::PointNormalsWorkspace::RowsPerSubmission(true, 16384), 4096u);
}

TEST(PointNormalsWorkspace, InputBeyondTheLbvhLimitIsRefusedBeforeRecording)
{
    Extrinsic::Tests::MockDevice device;
    device.ShaderFloat64 = true;
    Extrinsic::Tests::MockCommandContext commands;
    G::PointNormalsWorkspace workspace(device);
    for (auto count : {(1u << 20) + 1u, 1u << 21})
    {
        const G::PointNormalsResidentIo io{.Positions = {.Address = 8}, .Output = {.Address = 16},
                                          .Nodes = 32, .LiveSlots = 64, .LiveCount = count};
        EXPECT_FALSE(workspace.Record(commands, {.RadiusSearch = true}, io).IsValid());
    }
    EXPECT_TRUE(commands.DispatchRecords.empty());
    EXPECT_EQ(device.CreatePipelineCount, 0);
}
