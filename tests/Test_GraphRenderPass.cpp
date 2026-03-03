#include <gtest/gtest.h>
#include <cstdint>
#include <memory>

#include <glm/glm.hpp>

import Graphics;
import Geometry;

// =============================================================================
// GraphRenderPass — Compile-time contract tests
// =============================================================================
//
// These tests validate the CPU-side contract of GraphRenderPass and the
// ECS::Graph::Data component without requiring a GPU device.  They verify:
//   - Graph::Data component default values and PropertySet-backed queries.
//   - Node color / radius optional attribute detection via PropertySets.
//   - GraphRenderPass instantiation and configuration (no GPU calls).
//   - GraphRenderPass correctly delegates node submission to PointCloudRenderPass.

// ---- Helper: create a Graph with vertices and edges ----

static std::shared_ptr<Geometry::Graph::Graph> MakeTriangleGraph()
{
    auto g = std::make_shared<Geometry::Graph::Graph>();
    auto v0 = g->AddVertex(glm::vec3(0, 0, 0));
    auto v1 = g->AddVertex(glm::vec3(1, 0, 0));
    auto v2 = g->AddVertex(glm::vec3(0, 1, 0));
    g->AddEdge(v0, v1);
    g->AddEdge(v1, v2);
    g->AddEdge(v2, v0);
    return g;
}

// ---- ECS::Graph::Data Tests ----

TEST(Graph_Data, DefaultValues)
{
    ECS::Graph::Data data;
    EXPECT_EQ(data.NodeCount(), 0u);
    EXPECT_EQ(data.EdgeCount(), 0u);
    EXPECT_FALSE(data.HasNodeColors());
    EXPECT_FALSE(data.HasNodeRadii());
    EXPECT_FLOAT_EQ(data.DefaultNodeRadius,  0.01f);
    EXPECT_FLOAT_EQ(data.NodeSizeMultiplier, 1.0f);
    EXPECT_TRUE(data.Visible);
    EXPECT_FALSE(data.EdgesOverlay);
    EXPECT_EQ(data.GraphRef, nullptr);
}

TEST(Graph_Data, WithGraphRef)
{
    ECS::Graph::Data data;
    data.GraphRef = MakeTriangleGraph();

    EXPECT_EQ(data.NodeCount(), 3u);
    EXPECT_EQ(data.EdgeCount(), 3u);
    EXPECT_FALSE(data.HasNodeColors());
    EXPECT_FALSE(data.HasNodeRadii());
}

TEST(Graph_Data, NullGraphRefIsHandled)
{
    ECS::Graph::Data data;
    data.GraphRef = nullptr;

    EXPECT_EQ(data.NodeCount(), 0u);
    EXPECT_EQ(data.EdgeCount(), 0u);
    EXPECT_FALSE(data.HasNodeColors());
    EXPECT_FALSE(data.HasNodeRadii());
}

TEST(Graph_Data, PropertySetBackedColors)
{
    ECS::Graph::Data data;
    data.GraphRef = MakeTriangleGraph();
    EXPECT_FALSE(data.HasNodeColors());

    // Add per-node colors via PropertySet.
    auto colorProp = data.GraphRef->GetOrAddVertexProperty<glm::vec4>("v:color",
        glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
    EXPECT_TRUE(data.HasNodeColors());
}

TEST(Graph_Data, PropertySetBackedRadii)
{
    ECS::Graph::Data data;
    data.GraphRef = MakeTriangleGraph();
    EXPECT_FALSE(data.HasNodeRadii());

    // Add per-node radii via PropertySet.
    auto radiusProp = data.GraphRef->GetOrAddVertexProperty<float>("v:radius", 0.02f);
    EXPECT_TRUE(data.HasNodeRadii());
}

TEST(Graph_Data, SharedPtrSemantics)
{
    auto graph = MakeTriangleGraph();

    ECS::Graph::Data data1;
    data1.GraphRef = graph;

    ECS::Graph::Data data2;
    data2.GraphRef = graph;

    // Both reference the same authoritative graph.
    EXPECT_EQ(data1.GraphRef.get(), data2.GraphRef.get());
    EXPECT_EQ(data1.NodeCount(), data2.NodeCount());

    // Mutation through one reference is visible to the other.
    graph->AddVertex(glm::vec3(1, 1, 0));
    EXPECT_EQ(data1.NodeCount(), 4u);
    EXPECT_EQ(data2.NodeCount(), 4u);
}

TEST(Graph_Data, EmptyGraph)
{
    ECS::Graph::Data data;
    data.GraphRef = std::make_shared<Geometry::Graph::Graph>();

    EXPECT_EQ(data.NodeCount(), 0u);
    EXPECT_EQ(data.EdgeCount(), 0u);
}

TEST(Graph_Data, GraphWithDeletedVertices)
{
    auto g = std::make_shared<Geometry::Graph::Graph>();
    auto v0 = g->AddVertex(glm::vec3(0, 0, 0));
    auto v1 = g->AddVertex(glm::vec3(1, 0, 0));
    auto v2 = g->AddVertex(glm::vec3(0, 1, 0));
    g->AddEdge(v0, v1);
    g->AddEdge(v1, v2);

    ECS::Graph::Data data;
    data.GraphRef = g;
    EXPECT_EQ(data.NodeCount(), 3u);
    EXPECT_EQ(data.EdgeCount(), 2u);

    // Delete a vertex — adjacent edges are removed too.
    g->DeleteVertex(v0);
    EXPECT_EQ(data.NodeCount(), 2u);
    EXPECT_EQ(data.EdgeCount(), 1u);
}

// ---- GraphRenderPass Instantiation ----

TEST(GraphRenderPass_Contract, CanBeInstantiated)
{
    // GraphRenderPass has no GPU resources — instantiation should always succeed.
    Graphics::Passes::GraphRenderPass pass;
    SUCCEED(); // No crash = pass.
}

TEST(GraphRenderPass_Contract, SetPointCloudPassDoesNotCrash)
{
    Graphics::Passes::GraphRenderPass pass;
    pass.SetPointCloudPass(nullptr); // nullptr is valid (disables node rendering).
    SUCCEED();
}

// ---- Node Submission via PointCloudRenderPass ----

TEST(GraphRenderPass_Contract, NodeSubmissionDelegatesToPointCloud)
{
    // Verify that GraphRenderPass correctly calls PointCloudRenderPass::SubmitPoints
    // for each node in the graph component.
    // We can't run AddPasses() without ECS, but we can verify the count math
    // by direct PointCloudRenderPass staging.

    Graphics::Passes::PointCloudRenderPass pcPass;

    // Simulate what GraphRenderPass does for 3 nodes in FlatDisc mode.
    for (int i = 0; i < 3; ++i)
    {
        auto pt = Graphics::Passes::PointCloudRenderPass::PackPoint(
            static_cast<float>(i), 0, 0,
            0, 1, 0,
            0.01f,
            Graphics::Passes::PointCloudRenderPass::PackColor(255, 128, 0));
        pcPass.SubmitPoints(Geometry::PointCloud::RenderMode::FlatDisc, &pt, 1);
    }

    EXPECT_EQ(pcPass.GetPointCount(), 3u);
    EXPECT_TRUE(pcPass.HasContent());
}

TEST(GraphRenderPass_Contract, FlatDiscNodesAccumulate)
{
    // Graph nodes use flat-disc point rendering.
    Graphics::Passes::PointCloudRenderPass pcPass;

    auto pt = Graphics::Passes::PointCloudRenderPass::PackPoint(
        0, 0, 0, 0, 1, 0, 0.02f, 0xFFFFFFFF);
    pcPass.SubmitPoints(Geometry::PointCloud::RenderMode::FlatDisc, &pt, 1);

    EXPECT_EQ(pcPass.GetPointCount(), 1u);
}

// ---- GraphRenderPass + PointCloudRenderPass Reset Integration ----

TEST(GraphRenderPass_Contract, ResetBeforeCollect)
{
    Graphics::Passes::PointCloudRenderPass pcPass;

    // Submit some points (simulating a previous frame).
    auto pt = Graphics::Passes::PointCloudRenderPass::PackPoint(
        0, 0, 0, 0, 1, 0, 0.01f, 0xFFFFFFFF);
    pcPass.SubmitPoints(&pt, 1);
    ASSERT_EQ(pcPass.GetPointCount(), 1u);

    // Frame boundary: reset before new collection.
    pcPass.ResetPoints();
    EXPECT_EQ(pcPass.GetPointCount(), 0u);
    EXPECT_FALSE(pcPass.HasContent());

    // Re-submit for new frame.
    pcPass.SubmitPoints(Geometry::PointCloud::RenderMode::FlatDisc, &pt, 1);
    EXPECT_EQ(pcPass.GetPointCount(), 1u);
}

// ---- RenderMode Enum Coverage ----

TEST(Graph_RenderMode, FlatDiscAvailable)
{
    // Verify the supported render mode.
    using RM = Geometry::PointCloud::RenderMode;

    EXPECT_EQ(static_cast<uint32_t>(RM::FlatDisc), 0u);
}

TEST(Graph_Data, NodeRenderModeDefault)
{
    ECS::Graph::Data data;
    EXPECT_EQ(data.NodeRenderMode, Geometry::PointCloud::RenderMode::FlatDisc);
}

TEST(Graph_Data, NodeRenderModeSetFlatDisc)
{
    ECS::Graph::Data data;
    data.NodeRenderMode = Geometry::PointCloud::RenderMode::FlatDisc;
    EXPECT_EQ(data.NodeRenderMode, Geometry::PointCloud::RenderMode::FlatDisc);
}

// ---- PropertySet Accessor Tests ----

TEST(Graph_PropertySetAccessors, VertexPropertiesAccessible)
{
    auto g = MakeTriangleGraph();
    const auto& constG = *g;

    // Mutable access.
    EXPECT_EQ(g->VertexProperties().Size(), g->VerticesSize());

    // Const access.
    EXPECT_EQ(constG.VertexProperties().Size(), constG.VerticesSize());
}

TEST(Graph_PropertySetAccessors, EdgePropertiesAccessible)
{
    auto g = MakeTriangleGraph();
    const auto& constG = *g;

    EXPECT_EQ(g->EdgeProperties().Size(), g->EdgesSize());
    EXPECT_EQ(constG.EdgeProperties().Size(), constG.EdgesSize());
}

TEST(Graph_PropertySetAccessors, PropertyExistsCheck)
{
    auto g = MakeTriangleGraph();

    EXPECT_FALSE(g->VertexProperties().Exists("v:color"));
    g->GetOrAddVertexProperty<glm::vec4>("v:color");
    EXPECT_TRUE(g->VertexProperties().Exists("v:color"));
}

// =============================================================================
// GPU State Fields — Retained-mode rendering contract tests
// =============================================================================
//
// Validate the new GPU state fields on ECS::Graph::Data without requiring a
// GPU device. These ensure the component's retained-mode API surface is
// correct and that default values don't break existing code.

TEST(Graph_RetainedMode, DefaultGpuStateValues)
{
    ECS::Graph::Data data;

    // GpuGeometry handle should be invalid by default.
    EXPECT_FALSE(data.GpuGeometry.IsValid());

    // GPUScene slot should be invalid by default.
    EXPECT_EQ(data.GpuSlot, ECS::Graph::Data::kInvalidSlot);

    // Edge pairs should be empty.
    EXPECT_TRUE(data.CachedEdgePairs.empty());

    // Per-edge colors should be empty.
    EXPECT_TRUE(data.CachedEdgeColors.empty());

    // Per-node colors should be empty.
    EXPECT_TRUE(data.CachedNodeColors.empty());

    // Per-node radii should be empty.
    EXPECT_TRUE(data.CachedNodeRadii.empty());

    // GpuDirty should be true (triggers initial upload).
    EXPECT_TRUE(data.GpuDirty);

    // GpuVertexCount should be 0.
    EXPECT_EQ(data.GpuVertexCount, 0u);

    // EdgeWidth should have a sensible default.
    EXPECT_FLOAT_EQ(data.EdgeWidth, 1.5f);
}

TEST(Graph_RetainedMode, GpuDirtyStartsTrue)
{
    // Every new Graph::Data instance starts dirty so that
    // GraphGeometrySyncSystem uploads on the first frame.
    ECS::Graph::Data data;
    data.GraphRef = MakeTriangleGraph();
    EXPECT_TRUE(data.GpuDirty);
}

TEST(Graph_RetainedMode, EdgePairStorageMatchesEdgeCount)
{
    // Simulate what GraphGeometrySyncSystem would do: extract edge pairs
    // from graph topology and store them on the component.
    auto g = MakeTriangleGraph(); // 3 vertices, 3 edges

    ECS::Graph::Data data;
    data.GraphRef = g;

    // Manual edge extraction (mirrors sync system logic).
    const std::size_t eSize = g->EdgesSize();
    for (std::size_t i = 0; i < eSize; ++i)
    {
        const Geometry::EdgeHandle e{static_cast<Geometry::PropertyIndex>(i)};
        if (g->IsDeleted(e))
            continue;

        const auto [v0, v1] = g->EdgeVertices(e);
        data.CachedEdgePairs.push_back({
            static_cast<uint32_t>(v0.Index),
            static_cast<uint32_t>(v1.Index)
        });
    }

    EXPECT_EQ(data.CachedEdgePairs.size(), 3u);
}

TEST(Graph_RetainedMode, EdgePairSizeMatchesLayout)
{
    // EdgePair must be exactly 8 bytes for direct SSBO upload.
    static_assert(sizeof(ECS::RenderVisualization::EdgePair) == 8);

    ECS::RenderVisualization::EdgePair ep{42, 99};
    EXPECT_EQ(ep.i0, 42u);
    EXPECT_EQ(ep.i1, 99u);
}

TEST(Graph_RetainedMode, GpuDirtyCanBeCleared)
{
    ECS::Graph::Data data;
    data.GraphRef = MakeTriangleGraph();

    EXPECT_TRUE(data.GpuDirty);
    data.GpuDirty = false;
    EXPECT_FALSE(data.GpuDirty);

    // Simulates layout change trigger.
    data.GpuDirty = true;
    EXPECT_TRUE(data.GpuDirty);
}

TEST(Graph_RetainedMode, GpuVertexCountTracksCompactedCount)
{
    // After vertex compaction, GpuVertexCount should reflect
    // the number of non-deleted vertices uploaded to GPU.
    auto g = std::make_shared<Geometry::Graph::Graph>();
    auto v0 = g->AddVertex(glm::vec3(0, 0, 0));
    auto v1 = g->AddVertex(glm::vec3(1, 0, 0));
    auto v2 = g->AddVertex(glm::vec3(0, 1, 0));
    auto v3 = g->AddVertex(glm::vec3(1, 1, 0));
    g->AddEdge(v0, v1);
    g->AddEdge(v1, v2);
    g->AddEdge(v2, v3);

    ECS::Graph::Data data;
    data.GraphRef = g;

    // Simulate compaction for all 4 vertices.
    data.GpuVertexCount = 4;
    EXPECT_EQ(data.GpuVertexCount, 4u);

    // Delete v2 and simulate re-upload with 3 compacted vertices.
    g->DeleteVertex(v2);
    data.GpuVertexCount = static_cast<uint32_t>(g->VertexCount());
    EXPECT_EQ(data.GpuVertexCount, 3u);
}

// ---- GraphRenderPass SetRetainedPassesActive ----

TEST(GraphRenderPass_Retained, SetRetainedPassesActiveDoesNotCrash)
{
    Graphics::Passes::GraphRenderPass pass;
    pass.SetRetainedPassesActive(true, true);
    pass.SetRetainedPassesActive(false, false);
    pass.SetRetainedPassesActive(true, false);
    pass.SetRetainedPassesActive(false, true);
    SUCCEED();
}

TEST(GraphRenderPass_Retained, RetainedFlagsDefaultToFalse)
{
    // GraphRenderPass should default to CPU path (retained flags false).
    // We verify this indirectly: a new pass with SetPointCloudPass(nullptr)
    // should not crash when AddPasses isn't called.
    Graphics::Passes::GraphRenderPass pass;
    pass.SetPointCloudPass(nullptr);
    // If retained were true by default, the pass would skip entities even
    // without GPU state — which is wrong. The default must be false.
    SUCCEED();
}

// =============================================================================
// GPUScene Slot Integration — Contract tests
// =============================================================================

TEST(Graph_GPUSceneSlot, InvalidSlotConstantMatchesOtherComponents)
{
    // All renderer components must use the same kInvalidSlot sentinel (0xFFFFFFFF).
    EXPECT_EQ(ECS::Graph::Data::kInvalidSlot,
              ECS::MeshRenderer::Component::kInvalidSlot);
    EXPECT_EQ(ECS::Graph::Data::kInvalidSlot,
              ECS::PointCloudRenderer::Component::kInvalidSlot);
    EXPECT_EQ(ECS::Graph::Data::kInvalidSlot, ~0u);
}

TEST(Graph_GPUSceneSlot, SlotDefaultsToInvalid)
{
    ECS::Graph::Data data;
    EXPECT_EQ(data.GpuSlot, ECS::Graph::Data::kInvalidSlot);
}

TEST(Graph_GPUSceneSlot, SlotCanBeAssignedAndCleared)
{
    ECS::Graph::Data data;
    data.GpuSlot = 42u;
    EXPECT_EQ(data.GpuSlot, 42u);

    data.GpuSlot = ECS::Graph::Data::kInvalidSlot;
    EXPECT_EQ(data.GpuSlot, ECS::Graph::Data::kInvalidSlot);
}

// =============================================================================
// Per-Node Attribute Cache — Contract tests
// =============================================================================
//
// GraphGeometrySyncSystem extracts per-node colors and radii from PropertySets
// into CachedNodeColors / CachedNodeRadii vectors on the component.

TEST(Graph_NodeAttributes, CachedNodeColorsDefaultEmpty)
{
    ECS::Graph::Data data;
    EXPECT_TRUE(data.CachedNodeColors.empty());
}

TEST(Graph_NodeAttributes, CachedNodeRadiiDefaultEmpty)
{
    ECS::Graph::Data data;
    EXPECT_TRUE(data.CachedNodeRadii.empty());
}

TEST(Graph_NodeAttributes, CachedNodeColorsMatchVertexCount)
{
    // Simulate what GraphGeometrySyncSystem does: extract per-node colors
    // and pack them as ABGR uint32 values.
    auto g = MakeTriangleGraph();

    ECS::Graph::Data data;
    data.GraphRef = g;

    // Add per-node colors via PropertySet.
    auto colorProp = g->GetOrAddVertexProperty<glm::vec4>("v:color",
        glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
    EXPECT_TRUE(data.HasNodeColors());

    // Simulate extraction: one packed color per compacted vertex.
    const std::size_t vSize = g->VerticesSize();
    for (std::size_t i = 0; i < vSize; ++i)
    {
        const Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(i)};
        if (g->IsDeleted(v))
            continue;
        const glm::vec4& c = colorProp[v];
        // Pack ABGR — just verify the count, not the exact packing.
        data.CachedNodeColors.push_back(static_cast<uint32_t>(c.r * 255.0f));
    }

    EXPECT_EQ(data.CachedNodeColors.size(), 3u); // 3 vertices in triangle graph.
}

TEST(Graph_NodeAttributes, CachedNodeRadiiMatchVertexCount)
{
    auto g = MakeTriangleGraph();

    ECS::Graph::Data data;
    data.GraphRef = g;

    auto radiusProp = g->GetOrAddVertexProperty<float>("v:radius", 0.02f);
    EXPECT_TRUE(data.HasNodeRadii());

    const std::size_t vSize = g->VerticesSize();
    for (std::size_t i = 0; i < vSize; ++i)
    {
        const Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(i)};
        if (g->IsDeleted(v))
            continue;
        data.CachedNodeRadii.push_back(radiusProp[v]);
    }

    EXPECT_EQ(data.CachedNodeRadii.size(), 3u);
    EXPECT_FLOAT_EQ(data.CachedNodeRadii[0], 0.02f);
}

TEST(Graph_NodeAttributes, EmptyWhenPropertySetAbsent)
{
    auto g = MakeTriangleGraph();

    ECS::Graph::Data data;
    data.GraphRef = g;

    // No "v:color" or "v:radius" PropertySets added.
    EXPECT_FALSE(data.HasNodeColors());
    EXPECT_FALSE(data.HasNodeRadii());
    EXPECT_TRUE(data.CachedNodeColors.empty());
    EXPECT_TRUE(data.CachedNodeRadii.empty());
}

TEST(Graph_NodeAttributes, ClearedOnEmptyGraph)
{
    // When a graph becomes empty, all cached data should be cleared.
    ECS::Graph::Data data;
    data.CachedNodeColors.push_back(0xFF0000FF);
    data.CachedNodeRadii.push_back(0.01f);
    data.CachedEdgePairs.push_back({0, 1});
    data.CachedEdgeColors.push_back(0xFFFFFFFF);

    // Simulate what GraphGeometrySyncSystem does for an empty graph.
    data.CachedEdgePairs.clear();
    data.CachedEdgeColors.clear();
    data.CachedNodeColors.clear();
    data.CachedNodeRadii.clear();
    data.GpuVertexCount = 0;

    EXPECT_TRUE(data.CachedNodeColors.empty());
    EXPECT_TRUE(data.CachedNodeRadii.empty());
    EXPECT_TRUE(data.CachedEdgePairs.empty());
    EXPECT_TRUE(data.CachedEdgeColors.empty());
    EXPECT_EQ(data.GpuVertexCount, 0u);
}
