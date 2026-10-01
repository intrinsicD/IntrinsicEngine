// RUNTIME-296: the vertex-normals kernels' size contract on a mock device. A pass over more
// threads than one dispatch may cover is issued in chunks with a base index, every dispatch
// staying within the guaranteed 65535 workgroups; a bundle or a layout beyond the 32-bit
// index limits is refused before anything is allocated. The resident point workspaces
// (normals, scalar analysis, outliers) are reusable across runs: warm runs create no
// pipeline or scratch, only an undersized buffer is replaced, and each run resets its state.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
#include <gtest/gtest.h>
#include "MockRHI.hpp"
import Extrinsic.Graphics.VertexNormals;
import Extrinsic.Graphics.PointNormals;
import Extrinsic.Graphics.PointScalarAnalysis;
import Extrinsic.Graphics.OutlierAnalysis;
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

namespace
{
    template <class T>
    T PushField(const Extrinsic::Tests::MockCommandContext& commands, std::size_t offset)
    {
        T value{};
        const auto& payload = commands.PushConstantPayloads.back();
        EXPECT_GE(payload.size(), offset + sizeof(T));
        if (payload.size() >= offset + sizeof(T)) std::memcpy(&value, payload.data() + offset, sizeof(T));
        return value;
    }
    G::GpuPropertyView ResidentView(Extrinsic::Tests::MockDevice& device, std::uint32_t count)
    {
        const auto handle = device.CreateBuffer({.SizeBytes = std::uint64_t(count) * 12u, .Usage = Extrinsic::RHI::BufferUsage::Storage});
        return {.Buffer = handle, .Address = device.GetBufferDeviceAddress(handle), .Bytes = std::uint64_t(count) * 4u,
                .Layout = {.Count = count}};
    }
}

TEST(PointScalarWorkspace, WarmRunsKeepPipelineAndScratchAndGrowOnlyUndersizedBuffers)
{
    Extrinsic::Tests::MockDevice device;
    device.ShaderFloat64 = true;
    Extrinsic::Tests::MockCommandContext commands;
    G::PointScalarWorkspace workspace(device);
    const auto view = ResidentView(device, 4096u);
    const auto io = [&](std::uint32_t count) {
        return G::PointScalarResidentIo{.Positions = view, .Output = view, .Nodes = 16, .LiveSlots = 32, .LiveCount = count};
    };
    const int buffers = device.CreateBufferCount;
    // A refusal records and allocates nothing and leaves the workspace usable.
    EXPECT_FALSE(workspace.Record(commands, {.Method = 1, .K = 4}, {.Positions = view, .Output = view, .LiveCount = 100}).IsValid());
    EXPECT_TRUE(commands.DispatchRecords.empty());
    EXPECT_EQ(device.CreatePipelineCount, 0);
    EXPECT_EQ(device.CreateBufferCount, buffers);

    const auto stats = workspace.Record(commands, {.Method = 1, .K = 4}, io(100));
    ASSERT_TRUE(stats.IsValid());
    EXPECT_EQ(device.CreatePipelineCount, 1);
    EXPECT_EQ(device.CreateBufferCount - buffers, 3); // neighbors, nearest/means, stats
    EXPECT_EQ(commands.FillBufferCalls, 2);          // output and stats
    // Same capacity: nothing is created and the run's stats are zeroed again.
    EXPECT_EQ(workspace.Record(commands, {.Method = 1, .K = 4}, io(100)), stats);
    EXPECT_EQ(device.CreatePipelineCount, 1);
    EXPECT_EQ(device.CreateBufferCount - buffers, 3);
    EXPECT_EQ(device.DestroyBufferCount, 0);
    EXPECT_EQ(commands.FillBufferCalls, 4);
    // Smaller, different parameters: no allocation; the run addresses only its own rows.
    EXPECT_EQ(workspace.Record(commands, {.Method = 0, .K = 2}, io(10)), stats);
    EXPECT_EQ(device.CreateBufferCount - buffers, 3);
    EXPECT_EQ(commands.FillBufferCalls, 6);
    EXPECT_EQ(PushField<std::uint32_t>(commands, 64), 10u); // Count
    EXPECT_EQ(PushField<std::uint32_t>(commands, 68), 3u);  // Width
    EXPECT_EQ(PushField<std::uint32_t>(commands, 72), 0u);  // Method
    // A wider neighborhood replaces only the neighbor scratch.
    EXPECT_EQ(workspace.Record(commands, {.Method = 1, .K = 8}, io(100)), stats);
    EXPECT_EQ(device.CreateBufferCount - buffers, 4);
    EXPECT_EQ(device.DestroyBufferCount, 1);
    // More rows replace both row-sized buffers, never the stats.
    EXPECT_EQ(workspace.Record(commands, {.Method = 1, .K = 8}, io(200)), stats);
    EXPECT_EQ(device.CreateBufferCount - buffers, 6);
    EXPECT_EQ(device.DestroyBufferCount, 3);
    // A failed growth refuses before recording; the next run allocates it.
    const auto dispatches = commands.DispatchRecords.size();
    device.FailNextBufferCreate = true;
    EXPECT_FALSE(workspace.Record(commands, {.Method = 1, .K = 8}, io(400)).IsValid());
    EXPECT_EQ(commands.DispatchRecords.size(), dispatches);
    EXPECT_EQ(workspace.Record(commands, {.Method = 1, .K = 8}, io(400)), stats);
    EXPECT_EQ(device.CreatePipelineCount, 1);
}

TEST(OutlierWorkspace, WarmRunsKeepPipelineAndScratchAndGrowOnlyUndersizedBuffers)
{
    Extrinsic::Tests::MockDevice device;
    device.ShaderFloat64 = true;
    Extrinsic::Tests::MockCommandContext commands;
    G::OutlierWorkspace workspace(device);
    const auto io = [&](std::uint32_t count) {
        const auto view = ResidentView(device, count);
        return G::OutlierResidentIo{.Positions = view, .Score = view, .Mask = view, .Presentation = view,
                                    .Nodes = 16, .LiveSlots = 32, .LiveCount = count};
    };
    const auto small = io(100), large = io(300);
    const int buffers = device.CreateBufferCount;
    const auto stats = workspace.Record(commands, {.Method = 0, .K = 4}, small);
    ASSERT_TRUE(stats.IsValid());
    EXPECT_EQ(device.CreatePipelineCount, 1);
    EXPECT_EQ(device.CreateBufferCount - buffers, 3);
    // Same capacity, then a smaller radius run: nothing is created.
    EXPECT_EQ(workspace.Record(commands, {.Method = 0, .K = 4}, small), stats);
    EXPECT_EQ(workspace.Record(commands, {.Method = 1, .MinimumNeighbors = 2, .Radius = 1}, small), stats);
    EXPECT_EQ(device.CreatePipelineCount, 1);
    EXPECT_EQ(device.CreateBufferCount - buffers, 3);
    EXPECT_EQ(PushField<std::uint32_t>(commands, 76), 1u); // Width of the radius run
    EXPECT_EQ(PushField<std::uint32_t>(commands, 80), 1u); // Method
    // A wider neighborhood replaces only the neighbor scratch; more slots also the means.
    EXPECT_EQ(workspace.Record(commands, {.Method = 2, .K = 16}, small), stats);
    EXPECT_EQ(device.CreateBufferCount - buffers, 4);
    EXPECT_EQ(device.DestroyBufferCount, 1);
    EXPECT_EQ(workspace.Record(commands, {.Method = 2, .K = 16}, large), stats);
    EXPECT_EQ(device.CreateBufferCount - buffers, 6);
    EXPECT_EQ(device.DestroyBufferCount, 3);
    EXPECT_EQ(device.CreatePipelineCount, 1);
}

TEST(PointNormalsWorkspace, NewRunAfterCompletionReusesScratchAndRefusesOversizedLaterPages)
{
    Extrinsic::Tests::MockDevice device;
    device.ShaderFloat64 = true;
    Extrinsic::Tests::MockCommandContext commands;
    G::PointNormalsWorkspace workspace(device);
    const auto view = ResidentView(device, 2048u);
    const auto io = [&](std::uint32_t count) {
        return G::PointNormalsResidentIo{.Positions = view, .Output = view, .Nodes = 16, .LiveSlots = 32, .LiveCount = count};
    };
    const int buffers = device.CreateBufferCount;
    const G::PointNormalsGpuParams wide{.K = 15, .BatchSize = 64};
    const auto stats = workspace.Record(commands, wide, io(128));
    ASSERT_TRUE(stats.IsValid());
    ASSERT_EQ(workspace.Record(commands, wide, io(128), 64), stats);
    EXPECT_EQ(commands.FillBufferCalls, 2);
    // Page 0 of the next run starts over on the kept pipeline and scratch.
    const G::PointNormalsGpuParams narrow{.K = 7, .BatchSize = 64};
    EXPECT_EQ(workspace.Record(commands, narrow, io(64)), stats);
    EXPECT_EQ(device.CreatePipelineCount, 1);
    EXPECT_EQ(device.CreateBufferCount - buffers, 2);
    EXPECT_EQ(commands.FillBufferCalls, 4); // output and stats reset again
    // A later page needing more scratch than its run's page 0 prepared is refused unrecorded.
    const auto dispatches = commands.DispatchRecords.size();
    EXPECT_FALSE(workspace.Record(commands, wide, io(128), 64).IsValid());
    EXPECT_EQ(commands.DispatchRecords.size(), dispatches);
    // A larger run replaces only the neighbor scratch.
    EXPECT_EQ(workspace.Record(commands, {.K = 31, .BatchSize = 64}, io(128)), stats);
    EXPECT_EQ(device.CreateBufferCount - buffers, 3);
    EXPECT_EQ(device.DestroyBufferCount, 1);
    EXPECT_EQ(device.CreatePipelineCount, 1);
}
