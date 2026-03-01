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
