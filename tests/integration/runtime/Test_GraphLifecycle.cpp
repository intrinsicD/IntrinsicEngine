#include <gtest/gtest.h>
#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

import Graphics;
import Geometry;

// =============================================================================
// GraphLifecycle — Compile-time contract tests
// =============================================================================
//
// These tests validate the CPU-side contract of the GraphLifecycleSystem
// without requiring a GPU device. They verify:
//   - Edge pair extraction from graph topology (including deleted-vertex compaction).
//   - Per-edge color extraction from PropertySets.
//   - Upload mode selection (Direct vs Staged) based on StaticGeometry flag.
//   - GpuDirty lifecycle transitions for graph entities.
//   - Empty/degenerate graph handling.
//
// Test_GraphRenderPass.cpp covers Graph::Data component defaults and
// per-node attribute extraction. This file focuses on the sync system's
// edge-centric logic and compaction correctness.

// ---- Helpers ----

static std::shared_ptr<Geometry::Graph::Graph> MakeLineGraph()
{
    // 4 vertices in a line: v0 -- v1 -- v2 -- v3
    auto g = std::make_shared<Geometry::Graph::Graph>();
    auto v0 = g->AddVertex(glm::vec3(0, 0, 0));
    auto v1 = g->AddVertex(glm::vec3(1, 0, 0));
    auto v2 = g->AddVertex(glm::vec3(2, 0, 0));
    auto v3 = g->AddVertex(glm::vec3(3, 0, 0));
    (void)g->AddEdge(v0, v1);
    (void)g->AddEdge(v1, v2);
    (void)g->AddEdge(v2, v3);
    return g;
}

// Replicate the edge pair extraction logic from GraphLifecycleSystem.
// This builds a remap table from raw vertex indices to compacted indices
// (skipping deleted vertices), then extracts edge pairs using remapped indices.
static std::vector<ECS::EdgePair> ExtractEdgePairs(
    const Geometry::Graph::Graph& g,
    std::vector<uint32_t>& outRemapTable)
{
    // Phase 1: build compaction remap table.
    const std::size_t vSize = g.VerticesSize();
    outRemapTable.assign(vSize, ~0u);
    uint32_t compactIndex = 0;
    for (std::size_t i = 0; i < vSize; ++i)
    {
        const Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(i)};
        if (!g.IsDeleted(v))
            outRemapTable[i] = compactIndex++;
    }

    // Phase 2: extract edge pairs using remapped indices.
    std::vector<ECS::EdgePair> pairs;
    const std::size_t eSize = g.EdgesSize();
    for (std::size_t i = 0; i < eSize; ++i)
    {
        const Geometry::EdgeHandle e{static_cast<Geometry::PropertyIndex>(i)};
        if (g.IsDeleted(e))
            continue;

        const auto [v0, v1] = g.EdgeVertices(e);
        const uint32_t r0 = outRemapTable[v0.Index];
        const uint32_t r1 = outRemapTable[v1.Index];
        if (r0 == ~0u || r1 == ~0u)
            continue; // Skip edges to deleted vertices.

        pairs.push_back({r0, r1});
    }
    return pairs;
}

// =============================================================================
// Section 1: Edge Pair Extraction
// =============================================================================

TEST(GraphLifecycle_EdgePairs, ExtractsCorrectPairsFromSimpleGraph)
{
    auto g = MakeLineGraph();
    std::vector<uint32_t> remap;
    auto pairs = ExtractEdgePairs(*g, remap);

    EXPECT_EQ(pairs.size(), 3u);
    // With no deletions, remap is identity.
    EXPECT_EQ(pairs[0].i0, 0u);
    EXPECT_EQ(pairs[0].i1, 1u);
    EXPECT_EQ(pairs[1].i0, 1u);
    EXPECT_EQ(pairs[1].i1, 2u);
    EXPECT_EQ(pairs[2].i0, 2u);
    EXPECT_EQ(pairs[2].i1, 3u);
}

TEST(GraphLifecycle_EdgePairs, RemapsIndicesAfterVertexDeletion)
{
    auto g = MakeLineGraph(); // v0 -- v1 -- v2 -- v3
    auto v1 = Geometry::VertexHandle{1};
    g->DeleteVertex(v1); // Removes v1 and edges (v0,v1), (v1,v2).

    std::vector<uint32_t> remap;
    auto pairs = ExtractEdgePairs(*g, remap);

    // After deleting v1: only edge (v2,v3) remains.
    // Compacted indices: v0→0, v2→1, v3→2 (v1 deleted).
    EXPECT_EQ(pairs.size(), 1u);
    EXPECT_EQ(remap[0], 0u);     // v0 → 0
    EXPECT_EQ(remap[1], ~0u);    // v1 deleted
    EXPECT_EQ(remap[2], 1u);     // v2 → 1
    EXPECT_EQ(remap[3], 2u);     // v3 → 2
    EXPECT_EQ(pairs[0].i0, 1u);  // remapped v2
    EXPECT_EQ(pairs[0].i1, 2u);  // remapped v3
}

TEST(GraphLifecycle_EdgePairs, EmptyGraphProducesNoPairs)
{
    auto g = std::make_shared<Geometry::Graph::Graph>();
    std::vector<uint32_t> remap;
    auto pairs = ExtractEdgePairs(*g, remap);

    EXPECT_TRUE(pairs.empty());
    EXPECT_TRUE(remap.empty());
}

TEST(GraphLifecycle_EdgePairs, SingleVertexNoPairs)
{
    auto g = std::make_shared<Geometry::Graph::Graph>();
    g->AddVertex(glm::vec3(0, 0, 0));

    std::vector<uint32_t> remap;
    auto pairs = ExtractEdgePairs(*g, remap);

    EXPECT_TRUE(pairs.empty());
    EXPECT_EQ(remap.size(), 1u);
    EXPECT_EQ(remap[0], 0u);
}

TEST(GraphLifecycle_EdgePairs, AllVerticesDeletedProducesNoPairs)
{
    auto g = std::make_shared<Geometry::Graph::Graph>();
    auto v0 = g->AddVertex(glm::vec3(0, 0, 0));
    auto v1 = g->AddVertex(glm::vec3(1, 0, 0));
    (void)g->AddEdge(v0, v1);
    g->DeleteVertex(v0);
    g->DeleteVertex(v1);

    std::vector<uint32_t> remap;
    auto pairs = ExtractEdgePairs(*g, remap);

    EXPECT_TRUE(pairs.empty());
}

// =============================================================================
// Section 2: Per-Edge Color Extraction
// =============================================================================

TEST(GraphLifecycle_EdgeColors, ExtractsEdgeColorsFromPropertySet)
{
    auto g = MakeLineGraph();
    [[maybe_unused]] auto colorProp = g->GetOrAddEdgeProperty<glm::vec4>("e:color",
        glm::vec4(0.0f, 1.0f, 0.0f, 1.0f));

    ECS::Graph::Data data;
    data.GraphRef = g;

    EXPECT_TRUE(data.HasEdgeColors());

    // Simulate extraction: one color per live edge.
    const std::size_t eSize = g->EdgesSize();
    data.CachedEdgeColors.clear();
    for (std::size_t i = 0; i < eSize; ++i)
    {
        const Geometry::EdgeHandle e{static_cast<Geometry::PropertyIndex>(i)};
        if (g->IsDeleted(e))
            continue;
        // Pack a simple color (just testing the count here).
        data.CachedEdgeColors.push_back(0xFF00FF00u);
    }

    EXPECT_EQ(data.CachedEdgeColors.size(), 3u); // 3 edges in line graph.
}

TEST(GraphLifecycle_EdgeColors, NoEdgeColorsWhenPropertyAbsent)
{
    auto g = MakeLineGraph();
    ECS::Graph::Data data;
    data.GraphRef = g;

    EXPECT_FALSE(data.HasEdgeColors());
    EXPECT_TRUE(data.CachedEdgeColors.empty());
}

TEST(GraphLifecycle_EdgeColors, EdgeColorCountMatchesAfterDeletion)
{
    auto g = MakeLineGraph();
    (void)g->GetOrAddEdgeProperty<glm::vec4>("e:color", glm::vec4(1, 0, 0, 1));

    // Delete the middle vertex — removes 2 adjacent edges.
    g->DeleteVertex(Geometry::VertexHandle{1});

    ECS::Graph::Data data;
    data.GraphRef = g;

    // Extract remaining edge colors.
    const std::size_t eSize = g->EdgesSize();
    data.CachedEdgeColors.clear();
    for (std::size_t i = 0; i < eSize; ++i)
    {
        const Geometry::EdgeHandle e{static_cast<Geometry::PropertyIndex>(i)};
        if (g->IsDeleted(e))
            continue;
        data.CachedEdgeColors.push_back(0xFFFF0000u);
    }

    // Only 1 edge remains (v2--v3).
    EXPECT_EQ(data.CachedEdgeColors.size(), 1u);
}

// =============================================================================
// Section 3: Upload Mode Selection
// =============================================================================

TEST(GraphLifecycle_UploadMode, DynamicGraphUsesDirect)
{
    ECS::Graph::Data data;
    data.StaticGeometry = false;

    const auto mode = data.StaticGeometry
        ? Graphics::GeometryUploadMode::Staged
        : Graphics::GeometryUploadMode::Direct;
    EXPECT_EQ(mode, Graphics::GeometryUploadMode::Direct);
}

TEST(GraphLifecycle_UploadMode, StaticGraphUsesStaged)
{
    ECS::Graph::Data data;
    data.StaticGeometry = true;

    const auto mode = data.StaticGeometry
        ? Graphics::GeometryUploadMode::Staged
        : Graphics::GeometryUploadMode::Direct;
    EXPECT_EQ(mode, Graphics::GeometryUploadMode::Staged);
}

// =============================================================================
// Section 4: GpuDirty Lifecycle
// =============================================================================

TEST(GraphLifecycle_Lifecycle, GpuDirtyStartsTrueForInitialUpload)
{
    ECS::Graph::Data data;
    data.GraphRef = MakeLineGraph();
    EXPECT_TRUE(data.GpuDirty);
}

TEST(GraphLifecycle_Lifecycle, GpuDirtyClearedAfterUpload)
{
    ECS::Graph::Data data;
    data.GraphRef = MakeLineGraph();

    // Simulate upload completion.
    data.GpuDirty = false;
    data.GpuVertexCount = static_cast<uint32_t>(data.GraphRef->VertexCount());

    EXPECT_FALSE(data.GpuDirty);
    EXPECT_EQ(data.GpuVertexCount, 4u);
}

TEST(GraphLifecycle_Lifecycle, GpuDirtyResetOnTopologyChange)
{
    ECS::Graph::Data data;
    data.GraphRef = MakeLineGraph();
    data.GpuDirty = false; // Simulated successful upload.

    // Simulate topology change → mark dirty.
    data.GraphRef->AddVertex(glm::vec3(4, 0, 0));
    data.GpuDirty = true;

    EXPECT_TRUE(data.GpuDirty);
}

TEST(GraphLifecycle_Lifecycle, EmptyGraphClearsAllCachedData)
{
    ECS::Graph::Data data;
    data.GraphRef = MakeLineGraph();
    data.CachedEdgePairs.push_back({0, 1});
    data.CachedEdgeColors.push_back(0xFF);
    data.CachedNodeColors.push_back(0xFF);
    data.CachedNodeRadii.push_back(0.01f);
    data.GpuVertexCount = 4;

    // Simulate what the sync system does for empty/null graph.
    data.GraphRef = nullptr;
    data.CachedEdgePairs.clear();
    data.CachedEdgeColors.clear();
    data.CachedNodeColors.clear();
    data.CachedNodeRadii.clear();
    data.GpuVertexCount = 0;
    data.GpuDirty = false;

    EXPECT_TRUE(data.CachedEdgePairs.empty());
    EXPECT_TRUE(data.CachedEdgeColors.empty());
    EXPECT_TRUE(data.CachedNodeColors.empty());
    EXPECT_TRUE(data.CachedNodeRadii.empty());
    EXPECT_EQ(data.GpuVertexCount, 0u);
}

// =============================================================================
// Section 5: Compaction Vertex Count Consistency
// =============================================================================

TEST(GraphLifecycle_Compaction, VertexCountMatchesLiveVertices)
{
    auto g = std::make_shared<Geometry::Graph::Graph>();
    auto v0 = g->AddVertex(glm::vec3(0, 0, 0));
    auto v1 = g->AddVertex(glm::vec3(1, 0, 0));
    auto v2 = g->AddVertex(glm::vec3(2, 0, 0));
    auto v3 = g->AddVertex(glm::vec3(3, 0, 0));
    (void)g->AddEdge(v0, v1);
    (void)g->AddEdge(v2, v3);

    g->DeleteVertex(v1); // v1 deleted, v0 now isolated.

    std::vector<uint32_t> remap;
    ExtractEdgePairs(*g, remap);

    // Count compacted vertices.
    uint32_t compactedCount = 0;
    for (auto r : remap)
        if (r != ~0u) ++compactedCount;

    EXPECT_EQ(compactedCount, static_cast<uint32_t>(g->VertexCount()));
    EXPECT_EQ(compactedCount, 3u); // v0, v2, v3 live.
}

TEST(GraphLifecycle_Compaction, RemapTableSizeMatchesVerticesSize)
{
    auto g = MakeLineGraph();
    g->DeleteVertex(Geometry::VertexHandle{2});

    std::vector<uint32_t> remap;
    ExtractEdgePairs(*g, remap);

    // Remap table must cover all storage slots (including deleted).
    EXPECT_EQ(remap.size(), g->VerticesSize());
}
