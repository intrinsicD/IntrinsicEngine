// The property-smoothing device workspaces on a mock device: a warm run keeps the pipeline and
// buffers, only an undersized buffer is replaced, every input a run reads is rewritten (nothing
// of an earlier run's weights, degree, row map, restore mask or reports survives), and a refused
// run leaves nothing recordable behind.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
#include <gtest/gtest.h>
#include "MockRHI.hpp"
import Extrinsic.Graphics.PropertyFilter;
import Extrinsic.Graphics.SparseConjugateGradient;
import Extrinsic.RHI.Handles;
namespace G = Extrinsic::Graphics;
using Extrinsic::Tests::MockCommandContext;
using Extrinsic::Tests::MockDevice;

namespace
{
    template <class T>
    T PushField(const MockCommandContext& commands, std::size_t offset, std::size_t fromBack = 0)
    {
        T value{};
        EXPECT_GT(commands.PushConstantPayloads.size(), fromBack);
        if (commands.PushConstantPayloads.size() <= fromBack) return value;
        const auto& payload = commands.PushConstantPayloads[commands.PushConstantPayloads.size() - 1 - fromBack];
        EXPECT_GE(payload.size(), offset + sizeof(T));
        if (payload.size() >= offset + sizeof(T)) std::memcpy(&value, payload.data() + offset, sizeof(T));
        return value;
    }
    template <class T>
    bool Wrote(const MockDevice& device, std::size_t since, const std::vector<T>& data, std::uint64_t offset = 0)
    {
        return std::any_of(device.BufferWrites.begin() + std::ptrdiff_t(since), device.BufferWrites.end(), [&](const auto& write) {
            return write.Offset == offset && write.Data.size() == data.size() * sizeof(T) &&
                   std::memcmp(write.Data.data(), data.data(), write.Data.size()) == 0;
        });
    }
    G::PropertyFilterResidentView View(MockDevice& device, bool isDouble = false)
    {
        const auto handle = device.CreateBuffer({.SizeBytes = 1024u, .Usage = Extrinsic::RHI::BufferUsage::Storage});
        return {.Buffer = handle, .Address = device.GetBufferDeviceAddress(handle), .Double = isDouble};
    }
    // PropertyFilter push layout (see Graphics.PropertyFilter.cpp).
    constexpr std::size_t kWeights = 24, kBaseWeights = 32, kDegree = 56, kFixed = 64, kSlots = 72, kRows = 88, kMode = 100;
    constexpr std::uint32_t kStore = 8;

    struct PathGraph
    {
        std::vector<double> Values{1, 2, 3, 4};
        std::vector<std::uint32_t> Edges{0, 1, 1, 2, 2, 3};
        std::vector<double> Weights{1, 1, 1}, Degree{2, 3, 3, 2};
        std::vector<std::uint32_t> Fixed{0, 0, 0, 0};
        G::PropertyFilterGpuInput Input(std::uint32_t channels = 1) const
        {
            return {.Values = Values, .Channels = channels, .Edges = Edges, .Weights = Weights, .Degree = Degree, .Fixed = Fixed};
        }
    };
}

TEST(PropertyFilterWorkspace, WarmRunsKeepPipelineAndBuffersAndRewriteEveryInput)
{
    MockDevice device;
    device.ShaderFloat64 = true;
    MockCommandContext commands;
    G::PropertyFilterWorkspace workspace(device);
    const PathGraph graph;
    const G::PropertyFilterGpuParams averaging{.Iterations = 2, .Step = 0.5};
    const G::PropertyFilterGpuParams bilateral{.Method = G::PropertyFilterGpuMethod::Bilateral, .Iterations = 2, .Step = 0.5};
    const int created = device.CreateBufferCount;
    const auto newBuffers = [&] { return device.CreateBufferCount - created; };

    const auto writesBefore = device.BufferWrites.size();
    const auto first = workspace.Record(commands, graph.Input(), averaging);
    ASSERT_TRUE(first.IsValid());
    EXPECT_EQ(device.CreatePipelineCount, 1);
    // Four value buffers, edges, base weights, offsets, incidences, degree, fixed; averaging reads
    // the base weights directly, so no second weight buffer is uploaded.
    EXPECT_EQ(newBuffers(), 10);
    EXPECT_EQ(PushField<std::uint64_t>(commands, kWeights), PushField<std::uint64_t>(commands, kBaseWeights));
    const auto writesPerRun = device.BufferWrites.size() - writesBefore;
    EXPECT_EQ(writesPerRun, 7u);

    // Warm repeat: nothing created or destroyed; every input is written again.
    auto since = device.BufferWrites.size();
    EXPECT_TRUE(workspace.Record(commands, graph.Input(), averaging).IsValid());
    EXPECT_EQ(device.CreatePipelineCount, 1);
    EXPECT_EQ(newBuffers(), 10);
    EXPECT_EQ(device.DestroyBufferCount, 0);
    EXPECT_EQ(device.BufferWrites.size() - since, writesPerRun);

    // Bilateral rebuilds weights and degree on the device: one weight buffer is added, the
    // degree is not uploaded, and the kernels read the rebuilt weights.
    since = device.BufferWrites.size();
    EXPECT_TRUE(workspace.Record(commands, graph.Input(), bilateral).IsValid());
    EXPECT_EQ(newBuffers(), 11);
    EXPECT_EQ(device.BufferWrites.size() - since, writesPerRun - 1);
    EXPECT_FALSE(Wrote(device, since, graph.Degree));
    EXPECT_NE(PushField<std::uint64_t>(commands, kWeights), PushField<std::uint64_t>(commands, kBaseWeights));

    // The next averaging run restores the uploaded degree the bilateral passes overwrote.
    since = device.BufferWrites.size();
    EXPECT_TRUE(workspace.Record(commands, graph.Input(), averaging).IsValid());
    EXPECT_TRUE(Wrote(device, since, graph.Degree));
    EXPECT_EQ(PushField<std::uint64_t>(commands, kWeights), PushField<std::uint64_t>(commands, kBaseWeights));
    EXPECT_EQ(newBuffers(), 11);

    // Smaller: no allocation, the run addresses only its own rows.
    const PathGraph small{.Values = {5, 6}, .Edges = {0, 1}, .Weights = {2}, .Degree = {2, 2}, .Fixed = {0, 1}};
    EXPECT_TRUE(workspace.Record(commands, small.Input(), averaging).IsValid());
    EXPECT_EQ(newBuffers(), 11);
    EXPECT_EQ(PushField<std::uint32_t>(commands, kRows), 2u);

    // More channels outgrow only the four value buffers.
    PathGraph wide;
    wide.Values.assign(16, 1.0);
    EXPECT_TRUE(workspace.Record(commands, wide.Input(4), averaging).IsValid());
    EXPECT_EQ(newBuffers(), 15);
    EXPECT_EQ(device.DestroyBufferCount, 4);
    EXPECT_EQ(device.CreatePipelineCount, 1);

    // A failed growth refuses before recording; the next run allocates it.
    PathGraph wider; // one more row
    wider.Values.assign(5 * 4, 1.0);
    wider.Degree.push_back(1);
    wider.Fixed.push_back(0);
    const auto dispatches = commands.DispatchRecords.size();
    device.FailNextBufferCreate = true;
    EXPECT_FALSE(workspace.Record(commands, wider.Input(4), averaging).IsValid());
    EXPECT_EQ(commands.DispatchRecords.size(), dispatches);
    EXPECT_TRUE(workspace.Record(commands, wider.Input(4), averaging).IsValid());
    EXPECT_EQ(device.CreatePipelineCount, 1);
}

TEST(PropertyFilterWorkspace, EachRunBindsOnlyItsOwnRowMapAndRestoreMask)
{
    MockDevice device;
    device.ShaderFloat64 = true;
    MockCommandContext commands;
    G::PropertyFilterWorkspace workspace(device);
    PathGraph graph;
    graph.Values.clear(); // gathered from the resident input
    const G::PropertyFilterGpuParams averaging{.Iterations = 1, .Step = 0.5};
    const std::vector<std::uint32_t> slots{3, 1, 0, 2}, otherSlots{2, 3, 1, 0}, restore{0, 1, 0, 0};
    const auto input = View(device), output = View(device);
    G::PropertyFilterResidentIo io{.Input = input, .Output = output, .OutputBytes = 16, .Slots = slots, .RestoreMask = restore};

    auto since = device.BufferWrites.size();
    ASSERT_TRUE(workspace.Record(commands, graph.Input(), averaging, &io).IsValid());
    EXPECT_TRUE(Wrote(device, since, slots));
    EXPECT_TRUE(Wrote(device, since, restore));
    EXPECT_EQ(PushField<std::uint32_t>(commands, kMode), kStore);
    const auto slotAddress = PushField<std::uint64_t>(commands, kSlots);
    EXPECT_NE(slotAddress, 0u);
    EXPECT_NE(PushField<std::uint64_t>(commands, kFixed), 0u);
    EXPECT_EQ(PushField<std::uint64_t>(commands, kDegree), input.Address); // restored rows read the input
    // Later stores of the run reuse its map without uploading it again.
    since = device.BufferWrites.size();
    EXPECT_TRUE(workspace.RecordStore(commands, io, 1, output.Buffer, output.Address, 1, 4));
    EXPECT_EQ(device.BufferWrites.size(), since);
    EXPECT_EQ(PushField<std::uint64_t>(commands, kSlots), slotAddress);

    // A new run of the same length but different rows uploads its own map; without a restore
    // mask nothing of the previous one is bound.
    G::PropertyFilterResidentIo plain{.Input = input, .Output = output, .OutputBytes = 16, .Slots = otherSlots};
    since = device.BufferWrites.size();
    ASSERT_TRUE(workspace.Record(commands, graph.Input(), averaging, &plain).IsValid());
    EXPECT_TRUE(Wrote(device, since, otherSlots));
    EXPECT_EQ(PushField<std::uint32_t>(commands, kMode), kStore);
    EXPECT_EQ(PushField<std::uint64_t>(commands, kFixed), 0u);
    EXPECT_EQ(PushField<std::uint64_t>(commands, kDegree), 0u);
    // A store whose endpoints are not this run's is refused unrecorded.
    const auto dispatches = commands.DispatchRecords.size();
    EXPECT_FALSE(workspace.RecordStore(commands, io, 1, output.Buffer, output.Address, 1, 4));
    EXPECT_EQ(commands.DispatchRecords.size(), dispatches);

    // RecordLoad starts a run (a solver's seeds), whose stores then succeed.
    EXPECT_TRUE(workspace.RecordLoad(commands, io, 1, output.Buffer, output.Address, 1, 4));
    EXPECT_TRUE(workspace.RecordStore(commands, io, 1, output.Buffer, output.Address, 1, 4));
    EXPECT_NE(PushField<std::uint64_t>(commands, kFixed), 0u);

    // A refused run ends the previous one: no store reuses its map.
    PathGraph bad;
    bad.Values.clear();
    bad.Degree.pop_back();
    EXPECT_FALSE(workspace.Record(commands, bad.Input(), averaging, &io).IsValid());
    EXPECT_FALSE(workspace.RecordStore(commands, io, 1, output.Buffer, output.Address, 1, 4));
    EXPECT_EQ(device.CreatePipelineCount, 1);
}

namespace
{
    // SparseConjugateGradient push layout (see Graphics.SparseConjugateGradient.cpp).
    constexpr std::size_t kCgRows = 88;
    struct Tridiagonal
    {
        std::vector<std::uint32_t> Offsets{0, 2, 5, 7}, Columns{0, 1, 0, 1, 2, 1, 2};
        std::vector<double> Values{4, 1, 1, 4, 1, 1, 4};
        G::SparseCgMatrix Matrix() const { return {.Rows = 3, .RowOffsets = Offsets, .Columns = Columns, .Values = Values}; }
    };
}

TEST(SparseConjugateGradientWorkspace, WarmRunsKeepBuffersAndZeroTheirReports)
{
    MockDevice device;
    device.ShaderFloat64 = true;
    MockCommandContext commands;
    G::SparseConjugateGradientWorkspace workspace(device);
    const int created = device.CreateBufferCount;
    const Tridiagonal a;
    const std::vector<double> rhs{1, 2, 3}, guess{0.5, 0.5, 0.5};
    const std::vector<std::byte> zeroReport(sizeof(G::SparseCgReport), std::byte{0});
    const G::SparseCgProblem problem{.Matrix = a.Matrix(), .RightHandSides = rhs, .InitialGuesses = guess, .MaxIterations = 3};

    ASSERT_TRUE(workspace.Begin(problem));
    auto since = device.BufferWrites.size();
    const auto result = workspace.RecordNext(commands);
    ASSERT_TRUE(result.IsValid());
    EXPECT_TRUE(workspace.Finished());
    EXPECT_EQ(workspace.RecordFinal(commands), result);
    EXPECT_EQ(device.CreatePipelineCount, 1);
    EXPECT_EQ(device.CreateBufferCount - created, 9);
    EXPECT_TRUE(Wrote(device, since, zeroReport));
    EXPECT_TRUE(Wrote(device, since, guess, sizeof(G::SparseCgReport)));
    EXPECT_TRUE(Wrote(device, since, rhs));

    // Warm repeat: same buffers, reports zeroed again, operator rewritten.
    ASSERT_TRUE(workspace.Begin(problem));
    since = device.BufferWrites.size();
    EXPECT_EQ(workspace.RecordNext(commands), result);
    EXPECT_EQ(device.CreateBufferCount - created, 9);
    EXPECT_EQ(device.DestroyBufferCount, 0);
    EXPECT_TRUE(Wrote(device, since, zeroReport));
    EXPECT_TRUE(Wrote(device, since, a.Values));

    // Smaller: nothing is created; the run addresses its own rows.
    const std::vector<std::uint32_t> unitOffsets{0, 1}, unitColumns{0};
    const std::vector<double> unitValues{2}, unitRhs{1}, unitGuess{0};
    ASSERT_TRUE(workspace.Begin({.Matrix = {.Rows = 1, .RowOffsets = unitOffsets, .Columns = unitColumns, .Values = unitValues},
                                 .RightHandSides = unitRhs, .InitialGuesses = unitGuess, .MaxIterations = 3}));
    EXPECT_EQ(workspace.RecordNext(commands), result);
    EXPECT_EQ(device.CreateBufferCount - created, 9);
    EXPECT_EQ(PushField<std::uint32_t>(commands, kCgRows), 1u);

    // Device-seeded chained solves: more solves and the chain coefficients outgrow the
    // right-hand sides, both coefficient buffers and the result; no right-hand side or guess is
    // uploaded (ChainRhs forms every one on the device).
    const std::vector<double> diagonal(6, 1.0), constant(6, 0.0);
    ASSERT_TRUE(workspace.Begin({.Matrix = a.Matrix(), .Solves = 4, .RhsDiagonal = diagonal, .RhsConstant = constant,
                                 .ChainStride = 2, .SeedsOnDevice = true, .MaxIterations = 3}));
    since = device.BufferWrites.size();
    ASSERT_TRUE(workspace.RecordUpload(commands));
    EXPECT_EQ(device.CreateBufferCount - created, 13);
    EXPECT_EQ(device.DestroyBufferCount, 4);
    EXPECT_EQ(device.BufferWrites.size() - since, 6u); // offsets, columns, values, diagonal, constant, reports
    EXPECT_TRUE(Wrote(device, since, std::vector<std::byte>(4 * sizeof(G::SparseCgReport), std::byte{0})));
    EXPECT_NE(workspace.SolutionsAddress(), 0u);
    EXPECT_TRUE(workspace.RecordNext(commands).IsValid());
    EXPECT_EQ(device.CreatePipelineCount, 1);
}

TEST(SparseConjugateGradientWorkspace, RefusedProblemOrGrowthLeavesNoEarlierRunRecordable)
{
    MockDevice device;
    device.ShaderFloat64 = true;
    MockCommandContext commands;
    G::SparseConjugateGradientWorkspace workspace(device);
    const Tridiagonal a;
    const std::vector<double> rhs{1, 2, 3}, guess{0, 0, 0};
    ASSERT_TRUE(workspace.Begin({.Matrix = a.Matrix(), .RightHandSides = rhs, .InitialGuesses = guess, .MaxIterations = 2000}));
    ASSERT_TRUE(workspace.RecordNext(commands).IsValid());
    EXPECT_FALSE(workspace.Finished());

    // A malformed problem ends the unfinished run.
    Tridiagonal broken;
    broken.Columns[1] = 7;
    EXPECT_FALSE(workspace.Begin({.Matrix = broken.Matrix(), .RightHandSides = rhs, .InitialGuesses = guess}));
    const auto dispatches = commands.DispatchRecords.size();
    EXPECT_FALSE(workspace.RecordNext(commands).IsValid());
    EXPECT_FALSE(workspace.RecordFinal(commands).IsValid());
    EXPECT_FALSE(workspace.ResultBuffer().IsValid());
    EXPECT_EQ(workspace.SolutionsAddress(), 0u);
    EXPECT_EQ(commands.DispatchRecords.size(), dispatches);

    // A failed growth refuses the run; the next run allocates it.
    std::vector<double> many(3 * 64, 1.0), seeds(3 * 64, 0.0);
    const G::SparseCgProblem batch{.Matrix = a.Matrix(), .Solves = 64, .RightHandSides = many, .InitialGuesses = seeds, .MaxIterations = 1};
    ASSERT_TRUE(workspace.Begin(batch));
    device.FailNextBufferCreate = true;
    EXPECT_FALSE(workspace.RecordNext(commands).IsValid());
    EXPECT_EQ(commands.DispatchRecords.size(), dispatches);
    ASSERT_TRUE(workspace.Begin(batch));
    EXPECT_TRUE(workspace.RecordNext(commands).IsValid());
    EXPECT_EQ(device.CreatePipelineCount, 1);
}
