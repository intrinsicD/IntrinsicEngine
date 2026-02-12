#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

import Core;

using namespace Core;

// -------------------------------------------------------------------------
// Helpers
// -------------------------------------------------------------------------
namespace
{
    // Verify that node 'before' appears in an earlier layer than node 'after'.
    void ExpectLayerOrder(const std::vector<std::vector<uint32_t>>& layers,
                          uint32_t before, uint32_t after)
    {
        int beforeLayer = -1, afterLayer = -1;
        for (size_t l = 0; l < layers.size(); ++l)
        {
            for (uint32_t n : layers[l])
            {
                if (n == before) beforeLayer = static_cast<int>(l);
                if (n == after) afterLayer = static_cast<int>(l);
            }
        }
        ASSERT_GE(beforeLayer, 0) << "Node " << before << " not found in layers";
        ASSERT_GE(afterLayer, 0) << "Node " << after << " not found in layers";
        EXPECT_LT(beforeLayer, afterLayer)
            << "Node " << before << " (layer " << beforeLayer
            << ") should execute before node " << after << " (layer " << afterLayer << ")";
    }

    bool InSameLayer(const std::vector<std::vector<uint32_t>>& layers,
                     uint32_t a, uint32_t b)
    {
        for (const auto& layer : layers)
        {
            bool hasA = std::find(layer.begin(), layer.end(), a) != layer.end();
            bool hasB = std::find(layer.begin(), layer.end(), b) != layer.end();
            if (hasA && hasB) return true;
        }
        return false;
    }

    // Resource keys for testing (arbitrary distinct values).
    constexpr size_t kResA = 100;
    constexpr size_t kResB = 200;
    constexpr size_t kResC = 300;
    constexpr size_t kResD = 400;
}

// =========================================================================
// Test: Empty graph compiles cleanly
// =========================================================================
TEST(DAGScheduler, EmptyGraph)
{
    DAGScheduler sched;
    auto result = sched.Compile();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(sched.GetNodeCount(), 0u);
    EXPECT_TRUE(sched.GetExecutionLayers().empty());
}

// =========================================================================
// Test: Single node
// =========================================================================
TEST(DAGScheduler, SingleNode)
{
    DAGScheduler sched;
    uint32_t n = sched.AddNode();
    EXPECT_EQ(n, 0u);
    EXPECT_EQ(sched.GetNodeCount(), 1u);

    auto result = sched.Compile();
    ASSERT_TRUE(result.has_value());

    const auto& layers = sched.GetExecutionLayers();
    ASSERT_EQ(layers.size(), 1u);
    ASSERT_EQ(layers[0].size(), 1u);
    EXPECT_EQ(layers[0][0], 0u);
}

// =========================================================================
// Test: RAW dependency (Read-after-Write)
// =========================================================================
TEST(DAGScheduler, RAW_LinearChain)
{
    // A writes R, B reads R -> A before B
    DAGScheduler sched;
    uint32_t a = sched.AddNode();
    uint32_t b = sched.AddNode();

    sched.DeclareWrite(a, kResA);
    sched.DeclareRead(b, kResA);

    auto result = sched.Compile();
    ASSERT_TRUE(result.has_value());

    const auto& layers = sched.GetExecutionLayers();
    ASSERT_EQ(layers.size(), 2u);
    ExpectLayerOrder(layers, a, b);
}

// =========================================================================
// Test: WAW dependency (Write-after-Write)
// =========================================================================
TEST(DAGScheduler, WAW_Ordering)
{
    // A writes R, B writes R -> A before B
    DAGScheduler sched;
    uint32_t a = sched.AddNode();
    uint32_t b = sched.AddNode();

    sched.DeclareWrite(a, kResA);
    sched.DeclareWrite(b, kResA);

    auto result = sched.Compile();
    ASSERT_TRUE(result.has_value());

    const auto& layers = sched.GetExecutionLayers();
    ASSERT_EQ(layers.size(), 2u);
    ExpectLayerOrder(layers, a, b);
}

// =========================================================================
// Test: WAR dependency (Write-after-Read)
// =========================================================================
TEST(DAGScheduler, WAR_Ordering)
{
    // A writes R, B reads R, C reads R, D writes R
    // D must wait for both B and C
    DAGScheduler sched;
    uint32_t a = sched.AddNode();
    uint32_t b = sched.AddNode();
    uint32_t c = sched.AddNode();
    uint32_t d = sched.AddNode();

    sched.DeclareWrite(a, kResA);
    sched.DeclareRead(b, kResA);
    sched.DeclareRead(c, kResA);
    sched.DeclareWrite(d, kResA);

    auto result = sched.Compile();
    ASSERT_TRUE(result.has_value());

    const auto& layers = sched.GetExecutionLayers();
    ASSERT_EQ(layers.size(), 3u);

    // Layer 0: A (writer)
    // Layer 1: B, C (parallel readers)
    // Layer 2: D (next writer, waits for readers)
    EXPECT_EQ(layers[0].size(), 1u);
    EXPECT_EQ(layers[1].size(), 2u);
    EXPECT_EQ(layers[2].size(), 1u);

    ExpectLayerOrder(layers, a, b);
    ExpectLayerOrder(layers, a, c);
    ExpectLayerOrder(layers, b, d);
    ExpectLayerOrder(layers, c, d);
}

// =========================================================================
// Test: Parallel readers (RAR) in same layer
// =========================================================================
TEST(DAGScheduler, ParallelReaders)
{
    DAGScheduler sched;
    uint32_t writer = sched.AddNode();
    uint32_t r1 = sched.AddNode();
    uint32_t r2 = sched.AddNode();
    uint32_t r3 = sched.AddNode();

    sched.DeclareWrite(writer, kResA);
    sched.DeclareRead(r1, kResA);
    sched.DeclareRead(r2, kResA);
    sched.DeclareRead(r3, kResA);

    auto result = sched.Compile();
    ASSERT_TRUE(result.has_value());

    const auto& layers = sched.GetExecutionLayers();
    ASSERT_EQ(layers.size(), 2u);
    EXPECT_EQ(layers[0].size(), 1u);
    EXPECT_EQ(layers[1].size(), 3u);
}

// =========================================================================
// Test: Diamond dependency
// =========================================================================
TEST(DAGScheduler, Diamond)
{
    //      0 (writes A)
    //     / \
    //    1   2  (read A, write B / write C)
    //     \ /
    //      3  (reads B and C)

    DAGScheduler sched;
    uint32_t n0 = sched.AddNode();
    uint32_t n1 = sched.AddNode();
    uint32_t n2 = sched.AddNode();
    uint32_t n3 = sched.AddNode();

    sched.DeclareWrite(n0, kResA);
    sched.DeclareRead(n1, kResA);
    sched.DeclareWrite(n1, kResB);
    sched.DeclareRead(n2, kResA);
    sched.DeclareWrite(n2, kResC);
    sched.DeclareRead(n3, kResB);
    sched.DeclareRead(n3, kResC);

    auto result = sched.Compile();
    ASSERT_TRUE(result.has_value());

    const auto& layers = sched.GetExecutionLayers();
    ASSERT_EQ(layers.size(), 3u);
    EXPECT_EQ(layers[0].size(), 1u);  // n0
    EXPECT_EQ(layers[1].size(), 2u);  // n1, n2 (parallel)
    EXPECT_EQ(layers[2].size(), 1u);  // n3

    EXPECT_TRUE(InSameLayer(layers, n1, n2));
}

// =========================================================================
// Test: Independent nodes parallelize
// =========================================================================
TEST(DAGScheduler, IndependentNodesParallel)
{
    DAGScheduler sched;
    uint32_t a = sched.AddNode();
    uint32_t b = sched.AddNode();
    uint32_t c = sched.AddNode();

    // Each writes to a different resource — no dependencies.
    sched.DeclareWrite(a, kResA);
    sched.DeclareWrite(b, kResB);
    sched.DeclareWrite(c, kResC);

    auto result = sched.Compile();
    ASSERT_TRUE(result.has_value());

    const auto& layers = sched.GetExecutionLayers();
    ASSERT_EQ(layers.size(), 1u);
    EXPECT_EQ(layers[0].size(), 3u);
}

// =========================================================================
// Test: Direct edge (AddEdge)
// =========================================================================
TEST(DAGScheduler, DirectEdge)
{
    DAGScheduler sched;
    uint32_t a = sched.AddNode();
    uint32_t b = sched.AddNode();

    // No resource dependencies, but explicit edge.
    sched.AddEdge(a, b);

    auto result = sched.Compile();
    ASSERT_TRUE(result.has_value());

    const auto& layers = sched.GetExecutionLayers();
    ASSERT_EQ(layers.size(), 2u);
    ExpectLayerOrder(layers, a, b);
}

// =========================================================================
// Test: DeclareWeakRead (ordering without WAR)
// =========================================================================
TEST(DAGScheduler, WeakRead_NoWAR)
{
    // A writes R (Signal), B weak-reads R (WaitFor), C writes R (Signal).
    // B depends on A (RAW from weak read).
    // C depends on A (WAW).
    // C does NOT depend on B (because weak read doesn't register as reader).
    // So B and C should be in the same layer.

    DAGScheduler sched;
    uint32_t a = sched.AddNode();
    uint32_t b = sched.AddNode();
    uint32_t c = sched.AddNode();

    sched.DeclareWrite(a, kResA);     // A signals
    sched.DeclareWeakRead(b, kResA);  // B waits (weak)
    sched.DeclareWrite(c, kResA);     // C signals (WAW with A, but no WAR with B)

    auto result = sched.Compile();
    ASSERT_TRUE(result.has_value());

    const auto& layers = sched.GetExecutionLayers();
    ASSERT_EQ(layers.size(), 2u);
    EXPECT_EQ(layers[0].size(), 1u);  // A
    EXPECT_EQ(layers[1].size(), 2u);  // B and C (parallel)
    EXPECT_TRUE(InSameLayer(layers, b, c));
}

// =========================================================================
// Test: DeclareWeakRead with regular Read (mixed)
// =========================================================================
TEST(DAGScheduler, WeakRead_MixedWithRegularRead)
{
    // A writes R, B reads R (strong), C weak-reads R, D writes R.
    // D must wait for B (WAR from strong read) but NOT for C (weak read).
    // B and C should be in the same layer (both depend on A).
    // D should be in a later layer than B.

    DAGScheduler sched;
    uint32_t a = sched.AddNode();
    uint32_t b = sched.AddNode();
    uint32_t c = sched.AddNode();
    uint32_t d = sched.AddNode();

    sched.DeclareWrite(a, kResA);
    sched.DeclareRead(b, kResA);       // Strong read
    sched.DeclareWeakRead(c, kResA);   // Weak read
    sched.DeclareWrite(d, kResA);

    auto result = sched.Compile();
    ASSERT_TRUE(result.has_value());

    const auto& layers = sched.GetExecutionLayers();

    ExpectLayerOrder(layers, a, b);
    ExpectLayerOrder(layers, a, c);
    ExpectLayerOrder(layers, b, d);

    // C and B should be in the same layer
    EXPECT_TRUE(InSameLayer(layers, b, c));
}

// =========================================================================
// Test: Edge deduplication
// =========================================================================
TEST(DAGScheduler, EdgeDeduplication)
{
    // Adding the same edge twice should not corrupt the graph.
    DAGScheduler sched;
    uint32_t a = sched.AddNode();
    uint32_t b = sched.AddNode();

    sched.AddEdge(a, b);
    sched.AddEdge(a, b); // duplicate

    auto result = sched.Compile();
    ASSERT_TRUE(result.has_value());

    const auto& layers = sched.GetExecutionLayers();
    ASSERT_EQ(layers.size(), 2u);
}

// =========================================================================
// Test: Self-edge is ignored
// =========================================================================
TEST(DAGScheduler, SelfEdgeIgnored)
{
    DAGScheduler sched;
    uint32_t a = sched.AddNode();

    sched.AddEdge(a, a); // self-edge

    auto result = sched.Compile();
    ASSERT_TRUE(result.has_value());

    const auto& layers = sched.GetExecutionLayers();
    ASSERT_EQ(layers.size(), 1u);
}

// =========================================================================
// Test: Reset and rebuild across frames
// =========================================================================
TEST(DAGScheduler, MultiFrameReset)
{
    DAGScheduler sched;

    for (int frame = 0; frame < 3; ++frame)
    {
        sched.Reset();

        uint32_t a = sched.AddNode();
        uint32_t b = sched.AddNode();
        sched.DeclareWrite(a, kResA);
        sched.DeclareRead(b, kResA);

        auto result = sched.Compile();
        ASSERT_TRUE(result.has_value()) << "Frame " << frame;

        const auto& layers = sched.GetExecutionLayers();
        ASSERT_EQ(layers.size(), 2u) << "Frame " << frame;
    }
}

// =========================================================================
// Test: Complex realistic scenario
// =========================================================================
TEST(DAGScheduler, RealisticFrame)
{
    // Simulates a realistic game frame:
    //   Input        -> writes A (Velocity)
    //   AI           -> reads D (Health), writes A (Velocity) [WAW after Input]
    //   Physics      -> reads A (Velocity), writes B (Transform), writes C (Collider)
    //   Collision    -> reads C (Collider), writes D (Health)
    //   Animation    -> reads B (Transform)
    //   RenderPrep   -> reads B (Transform), reads D (Health)

    DAGScheduler sched;
    uint32_t input     = sched.AddNode(); // 0
    uint32_t ai        = sched.AddNode(); // 1
    uint32_t physics   = sched.AddNode(); // 2
    uint32_t collision = sched.AddNode(); // 3
    uint32_t animation = sched.AddNode(); // 4
    uint32_t render    = sched.AddNode(); // 5

    sched.DeclareWrite(input, kResA);                       // Input writes Velocity
    sched.DeclareRead(ai, kResD); sched.DeclareWrite(ai, kResA); // AI reads Health, writes Velocity
    sched.DeclareRead(physics, kResA); sched.DeclareWrite(physics, kResB); sched.DeclareWrite(physics, kResC);
    sched.DeclareRead(collision, kResC); sched.DeclareWrite(collision, kResD);
    sched.DeclareRead(animation, kResB);
    sched.DeclareRead(render, kResB); sched.DeclareRead(render, kResD);

    auto result = sched.Compile();
    ASSERT_TRUE(result.has_value());

    // Expected: Input -> AI -> Physics -> {Collision, Animation} -> RenderPrep
    const auto& layers = sched.GetExecutionLayers();
    ASSERT_EQ(layers.size(), 5u);
    EXPECT_EQ(layers[0].size(), 1u);  // Input
    EXPECT_EQ(layers[1].size(), 1u);  // AI
    EXPECT_EQ(layers[2].size(), 1u);  // Physics
    EXPECT_EQ(layers[3].size(), 2u);  // Collision + Animation
    EXPECT_EQ(layers[4].size(), 1u);  // RenderPrep

    ExpectLayerOrder(layers, input, ai);
    ExpectLayerOrder(layers, ai, physics);
    ExpectLayerOrder(layers, physics, collision);
    ExpectLayerOrder(layers, physics, animation);
    ExpectLayerOrder(layers, collision, render);
}

// =========================================================================
// Test: Wide fan-out stress
// =========================================================================
TEST(DAGScheduler, WideFanout)
{
    constexpr uint32_t kReaderCount = 64;

    DAGScheduler sched;
    uint32_t producer = sched.AddNode();
    sched.DeclareWrite(producer, kResA);

    for (uint32_t i = 0; i < kReaderCount; ++i)
    {
        uint32_t reader = sched.AddNode();
        sched.DeclareRead(reader, kResA);
    }

    auto result = sched.Compile();
    ASSERT_TRUE(result.has_value());

    const auto& layers = sched.GetExecutionLayers();
    ASSERT_EQ(layers.size(), 2u);
    EXPECT_EQ(layers[0].size(), 1u);
    EXPECT_EQ(layers[1].size(), kReaderCount);
}

// =========================================================================
// Test: Multiple resources with interleaved access
// =========================================================================
TEST(DAGScheduler, MultipleResources)
{
    // Node 0 writes A, Node 1 writes B, Node 2 reads A+B, Node 3 writes A
    DAGScheduler sched;
    uint32_t n0 = sched.AddNode();
    uint32_t n1 = sched.AddNode();
    uint32_t n2 = sched.AddNode();
    uint32_t n3 = sched.AddNode();

    sched.DeclareWrite(n0, kResA);
    sched.DeclareWrite(n1, kResB);
    sched.DeclareRead(n2, kResA);
    sched.DeclareRead(n2, kResB);
    sched.DeclareWrite(n3, kResA);

    auto result = sched.Compile();
    ASSERT_TRUE(result.has_value());

    const auto& layers = sched.GetExecutionLayers();
    // n0 and n1 are independent (different resources) -> same layer
    // n2 depends on both n0 and n1 -> next layer
    // n3 depends on n0 (WAW) and n2 (WAR) -> next layer
    ASSERT_EQ(layers.size(), 3u);
    EXPECT_EQ(layers[0].size(), 2u);  // n0, n1
    EXPECT_EQ(layers[1].size(), 1u);  // n2
    EXPECT_EQ(layers[2].size(), 1u);  // n3
}

// =========================================================================
// Test: Read-then-write on same resource by same node
// =========================================================================
TEST(DAGScheduler, ReadWriteSameResourceSameNode)
{
    // Node 0: reads and writes A (in-place update).
    // Node 1: reads A.
    DAGScheduler sched;
    uint32_t n0 = sched.AddNode();
    uint32_t n1 = sched.AddNode();

    sched.DeclareRead(n0, kResA);
    sched.DeclareWrite(n0, kResA);
    sched.DeclareRead(n1, kResA);

    auto result = sched.Compile();
    ASSERT_TRUE(result.has_value());

    const auto& layers = sched.GetExecutionLayers();
    ASSERT_EQ(layers.size(), 2u);
    ExpectLayerOrder(layers, n0, n1);
}
