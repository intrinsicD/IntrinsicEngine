// ============================================================================
// Test_PropertySetDirtySync.cpp — Contract tests for the PropertySet
// dirty-domain sync system (TODO §1). Validates:
//
//   - Dirty tag components are empty ECS tags (1 byte in C++), used only as
//     presence markers.
//   - Tag attachment/detachment on entities.
//   - Domain independence: attribute tags don't trigger full re-upload.
//   - Position/topology tags escalate to GpuDirty.
//   - Attribute-only tags re-extract cached vectors from PropertySets.
//   - Count divergence safety: escalates to GpuDirty.
//   - Tags cleared after sync.
//   - Multiple simultaneous dirty domains handled independently.
//
// No GPU device is needed — these are pure contract/ECS tests.
// ============================================================================

#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>

import Graphics;
import Geometry;

using namespace ECS;

// =============================================================================
// Section 1: Dirty Tag Component Properties
// =============================================================================

TEST(DirtyTag, ZeroSizeTags)
{
    // All dirty tags must be empty tag components (presence-only markers).
    EXPECT_EQ(sizeof(DirtyTag::VertexPositions), 1u);  // Empty struct = 1 byte in C++
    EXPECT_EQ(sizeof(DirtyTag::VertexAttributes), 1u);
    EXPECT_EQ(sizeof(DirtyTag::EdgeTopology), 1u);
    EXPECT_EQ(sizeof(DirtyTag::EdgeAttributes), 1u);
    EXPECT_EQ(sizeof(DirtyTag::FaceTopology), 1u);
    EXPECT_EQ(sizeof(DirtyTag::FaceAttributes), 1u);
}

TEST(DirtyTag, AttachDetach)
{
    entt::registry reg;
    auto e = reg.create();

    EXPECT_FALSE(reg.all_of<DirtyTag::VertexPositions>(e));

    reg.emplace<DirtyTag::VertexPositions>(e);
    EXPECT_TRUE(reg.all_of<DirtyTag::VertexPositions>(e));

    reg.remove<DirtyTag::VertexPositions>(e);
    EXPECT_FALSE(reg.all_of<DirtyTag::VertexPositions>(e));
}

TEST(DirtyTag, MultipleTagsCoexist)
{
    entt::registry reg;
    auto e = reg.create();

    reg.emplace<DirtyTag::VertexAttributes>(e);
    reg.emplace<DirtyTag::EdgeAttributes>(e);
    reg.emplace<DirtyTag::FaceAttributes>(e);

    EXPECT_TRUE(reg.all_of<DirtyTag::VertexAttributes>(e));
    EXPECT_TRUE(reg.all_of<DirtyTag::EdgeAttributes>(e));
    EXPECT_TRUE(reg.all_of<DirtyTag::FaceAttributes>(e));
    EXPECT_FALSE(reg.all_of<DirtyTag::VertexPositions>(e));
}

TEST(DirtyTag, FaceAttributes_ViewDoesNotAliasMeshOrSurfaceStorage)
{
    entt::registry reg;
    auto e = reg.create();

    reg.emplace<Mesh::Data>(e);
    reg.emplace<Surface::Component>(e);
    reg.emplace<DirtyTag::FaceAttributes>(e);

    auto view = reg.view<DirtyTag::FaceAttributes, Mesh::Data, Surface::Component>();

    std::size_t count = 0;
    for (auto [entity, meshData, surf] : view.each())
    {
        (void)meshData;
        (void)surf;
        EXPECT_EQ(entity, e);
        ++count;
    }

    EXPECT_EQ(count, 1u);
}

TEST(DirtyTag, BulkClear)
{
    entt::registry reg;
    auto e1 = reg.create();
    auto e2 = reg.create();

    reg.emplace<DirtyTag::VertexAttributes>(e1);
    reg.emplace<DirtyTag::VertexAttributes>(e2);

    EXPECT_TRUE(reg.all_of<DirtyTag::VertexAttributes>(e1));
    EXPECT_TRUE(reg.all_of<DirtyTag::VertexAttributes>(e2));

    reg.clear<DirtyTag::VertexAttributes>();

    EXPECT_FALSE(reg.all_of<DirtyTag::VertexAttributes>(e1));
    EXPECT_FALSE(reg.all_of<DirtyTag::VertexAttributes>(e2));
}

// =============================================================================
// Section 2: VertexPositionsDirty → GpuDirty Escalation
// =============================================================================

TEST(PropertySetDirtySync, VertexPositions_Graph_EscalatesToGpuDirty)
{
    entt::registry reg;
    auto e = reg.create();

    auto& gd = reg.emplace<Graph::Data>(e);
    gd.GpuDirty = false; // Simulate already-uploaded state.

    reg.emplace<DirtyTag::VertexPositions>(e);

    Graphics::Systems::PropertySetDirtySync::OnUpdate(reg);

    // Position changes must escalate to full re-upload.
    EXPECT_TRUE(gd.GpuDirty);

    // Tag must be cleared.
    EXPECT_FALSE(reg.all_of<DirtyTag::VertexPositions>(e));
}

TEST(PropertySetDirtySync, VertexPositions_PointCloud_EscalatesToGpuDirty)
{
    entt::registry reg;
    auto e = reg.create();

    auto& pcd = reg.emplace<PointCloud::Data>(e);
    pcd.GpuDirty = false;
    pcd.PositionRevision = 7;

    reg.emplace<DirtyTag::VertexPositions>(e);

    Graphics::Systems::PropertySetDirtySync::OnUpdate(reg);

    EXPECT_TRUE(pcd.GpuDirty);
    EXPECT_EQ(pcd.PositionRevision, 8u);
    EXPECT_FALSE(reg.all_of<DirtyTag::VertexPositions>(e));
}

// =============================================================================
// Section 3: EdgeTopologyDirty → GpuDirty Escalation
// =============================================================================

TEST(PropertySetDirtySync, EdgeTopology_EscalatesToGpuDirty)
{
    entt::registry reg;
    auto e = reg.create();

    auto& gd = reg.emplace<Graph::Data>(e);
    gd.GpuDirty = false;

    reg.emplace<DirtyTag::EdgeTopology>(e);

    Graphics::Systems::PropertySetDirtySync::OnUpdate(reg);

    EXPECT_TRUE(gd.GpuDirty);
    EXPECT_FALSE(reg.all_of<DirtyTag::EdgeTopology>(e));
}

// =============================================================================
// Section 4: VertexAttributesDirty — Incremental Re-extraction (Graph)
// =============================================================================

TEST(PropertySetDirtySync, VertexAttributes_Graph_ReExtractsColors)
{
    entt::registry reg;
    auto e = reg.create();

    auto graph = std::make_shared<Geometry::Graph::Graph>();
    auto v0 = graph->AddVertex({0, 0, 0});
    auto v1 = graph->AddVertex({1, 0, 0});

    // Add color property.
    auto colors = graph->GetOrAddVertexProperty<glm::vec4>("v:color", {1, 1, 1, 1});
    colors[v0] = {1.0f, 0.0f, 0.0f, 1.0f}; // Red
    colors[v1] = {0.0f, 1.0f, 0.0f, 1.0f}; // Green

    auto& gd = reg.emplace<Graph::Data>(e);
    gd.GraphRef = graph;
    gd.GpuDirty = false;
    gd.GpuGeometry = Geometry::GeometryHandle(0, 1); // Simulate valid handle.
    gd.GpuVertexCount = 2; // Matches graph vertex count.

    // Old cached colors (stale).
    gd.CachedNodeColors = {0xFFFFFFFF, 0xFFFFFFFF};

    reg.emplace<DirtyTag::VertexAttributes>(e);

    Graphics::Systems::PropertySetDirtySync::OnUpdate(reg);

    // GpuDirty should NOT be set (attribute-only change).
    EXPECT_FALSE(gd.GpuDirty);

    // Colors must be re-extracted.
    ASSERT_EQ(gd.CachedNodeColors.size(), 2u);

    // Verify red (R=255 in low byte, ABGR packing).
    uint32_t red = gd.CachedNodeColors[0];
    EXPECT_EQ((red >> 0) & 0xFF, 255u); // R
    EXPECT_EQ((red >> 8) & 0xFF, 0u);   // G
    EXPECT_EQ((red >> 16) & 0xFF, 0u);  // B

    // Verify green.
    uint32_t green = gd.CachedNodeColors[1];
    EXPECT_EQ((green >> 0) & 0xFF, 0u);   // R
    EXPECT_EQ((green >> 8) & 0xFF, 255u); // G

    // Tag cleared.
    EXPECT_FALSE(reg.all_of<DirtyTag::VertexAttributes>(e));
}

TEST(PropertySetDirtySync, VertexAttributes_Graph_ReExtractsRadii)
{
    entt::registry reg;
    auto e = reg.create();

    auto graph = std::make_shared<Geometry::Graph::Graph>();
    auto v0 = graph->AddVertex({0, 0, 0});
    auto v1 = graph->AddVertex({1, 0, 0});

    auto radii = graph->GetOrAddVertexProperty<float>("v:radius", 0.01f);
    radii[v0] = 0.05f;
    radii[v1] = 0.10f;

    auto& gd = reg.emplace<Graph::Data>(e);
    gd.GraphRef = graph;
    gd.GpuDirty = false;
    gd.GpuGeometry = Geometry::GeometryHandle(0, 1);
    gd.GpuVertexCount = 2;

    reg.emplace<DirtyTag::VertexAttributes>(e);

    Graphics::Systems::PropertySetDirtySync::OnUpdate(reg);

    EXPECT_FALSE(gd.GpuDirty);
    ASSERT_EQ(gd.CachedNodeRadii.size(), 2u);
    EXPECT_FLOAT_EQ(gd.CachedNodeRadii[0], 0.05f);
    EXPECT_FLOAT_EQ(gd.CachedNodeRadii[1], 0.10f);
}

TEST(PropertySetDirtySync, VertexAttributes_Graph_UpdatesPointComponentFlags)
{
    entt::registry reg;
    auto e = reg.create();

    auto graph = std::make_shared<Geometry::Graph::Graph>();
    (void)graph->AddVertex({0, 0, 0});
    (void)graph->GetOrAddVertexProperty<glm::vec4>("v:color", {1, 1, 1, 1});

    auto& gd = reg.emplace<Graph::Data>(e);
    gd.GraphRef = graph;
    gd.GpuDirty = false;
    gd.GpuGeometry = Geometry::GeometryHandle(0, 1);
    gd.GpuVertexCount = 1;

    auto& pt = reg.emplace<Point::Component>(e);
    pt.HasPerPointColors = false;

    reg.emplace<DirtyTag::VertexAttributes>(e);

    Graphics::Systems::PropertySetDirtySync::OnUpdate(reg);

    EXPECT_TRUE(pt.HasPerPointColors);
}

// =============================================================================
// Section 5: VertexAttributesDirty — Count Divergence Safety
// =============================================================================

TEST(PropertySetDirtySync, VertexAttributes_Graph_CountDivergence_Escalates)
{
    entt::registry reg;
    auto e = reg.create();

    auto graph = std::make_shared<Geometry::Graph::Graph>();
    graph->AddVertex({0, 0, 0});
    graph->AddVertex({1, 0, 0});
    graph->AddVertex({0, 1, 0}); // 3 vertices now

    auto& gd = reg.emplace<Graph::Data>(e);
    gd.GraphRef = graph;
    gd.GpuDirty = false;
    gd.GpuGeometry = Geometry::GeometryHandle(0, 1);
    gd.GpuVertexCount = 2; // Stale: last upload had 2 vertices.

    reg.emplace<DirtyTag::VertexAttributes>(e);

    Graphics::Systems::PropertySetDirtySync::OnUpdate(reg);

    // Count divergence → escalate to full re-upload.
    EXPECT_TRUE(gd.GpuDirty);
}

TEST(PropertySetDirtySync, VertexAttributes_PointCloud_CountDivergence_Escalates)
{
    entt::registry reg;
    auto e = reg.create();

    auto cloud = std::make_shared<Geometry::PointCloud::Cloud>();
    cloud->AddPoint({0, 0, 0});
    cloud->AddPoint({1, 0, 0});

    auto& pcd = reg.emplace<PointCloud::Data>(e);
    pcd.CloudRef = cloud;
    pcd.GpuDirty = false;
    pcd.GpuGeometry = Geometry::GeometryHandle(0, 1);
    pcd.GpuPointCount = 1; // Stale: last upload had 1 point.

    reg.emplace<DirtyTag::VertexAttributes>(e);

    Graphics::Systems::PropertySetDirtySync::OnUpdate(reg);

    EXPECT_TRUE(pcd.GpuDirty);
}

// =============================================================================
// Section 6: EdgeAttributesDirty — Incremental Re-extraction
// =============================================================================

TEST(PropertySetDirtySync, EdgeAttributes_ReExtractsEdgeColors)
{
    entt::registry reg;
    auto e = reg.create();

    auto graph = std::make_shared<Geometry::Graph::Graph>();
    auto v0 = graph->AddVertex({0, 0, 0});
    auto v1 = graph->AddVertex({1, 0, 0});
    (void)graph->AddEdge(v0, v1);

    [[maybe_unused]] auto edgeColors = graph->GetOrAddEdgeProperty<glm::vec4>(
        "e:color", {1, 1, 1, 1});

    auto& gd = reg.emplace<Graph::Data>(e);
    gd.GraphRef = graph;
    gd.GpuDirty = false;
    gd.GpuGeometry = Geometry::GeometryHandle(0, 1);
    gd.GpuEdgeCount = 1;

    auto& line = reg.emplace<Line::Component>(e);
    line.HasPerEdgeColors = false;

    reg.emplace<DirtyTag::EdgeAttributes>(e);

    Graphics::Systems::PropertySetDirtySync::OnUpdate(reg);

    EXPECT_FALSE(gd.GpuDirty);
    ASSERT_EQ(gd.CachedEdgeColors.size(), 1u);
    EXPECT_TRUE(line.HasPerEdgeColors);
    EXPECT_FALSE(reg.all_of<DirtyTag::EdgeAttributes>(e));
}

TEST(PropertySetDirtySync, EdgeAttributes_CountDivergence_Escalates)
{
    entt::registry reg;
    auto e = reg.create();

    auto graph = std::make_shared<Geometry::Graph::Graph>();
    auto v0 = graph->AddVertex({0, 0, 0});
    auto v1 = graph->AddVertex({1, 0, 0});
    auto v2 = graph->AddVertex({0, 1, 0});
    (void)graph->AddEdge(v0, v1);
    (void)graph->AddEdge(v1, v2); // 2 edges

    auto& gd = reg.emplace<Graph::Data>(e);
    gd.GraphRef = graph;
    gd.GpuDirty = false;
    gd.GpuGeometry = Geometry::GeometryHandle(0, 1);
    gd.GpuEdgeCount = 1; // Stale

    reg.emplace<DirtyTag::EdgeAttributes>(e);

    Graphics::Systems::PropertySetDirtySync::OnUpdate(reg);

    EXPECT_TRUE(gd.GpuDirty);
}

// =============================================================================
// Section 7: FaceAttributesDirty → Surface FaceColorsDirty
// =============================================================================

TEST(PropertySetDirtySync, FaceAttributes_SetsFaceColorsDirty)
{
    entt::registry reg;
    auto e = reg.create();

    auto& surf = reg.emplace<Surface::Component>(e);
    surf.FaceColorsDirty = false;

    reg.emplace<DirtyTag::FaceAttributes>(e);

    Graphics::Systems::PropertySetDirtySync::OnUpdate(reg);

    EXPECT_TRUE(surf.FaceColorsDirty);
    EXPECT_FALSE(reg.all_of<DirtyTag::FaceAttributes>(e));
}

TEST(PropertySetDirtySync, FaceTopology_SetsFaceColorsDirty)
{
    entt::registry reg;
    auto e = reg.create();

    auto& surf = reg.emplace<Surface::Component>(e);
    surf.FaceColorsDirty = false;

    reg.emplace<DirtyTag::FaceTopology>(e);

    Graphics::Systems::PropertySetDirtySync::OnUpdate(reg);

    EXPECT_TRUE(surf.FaceColorsDirty);
    EXPECT_FALSE(reg.all_of<DirtyTag::FaceTopology>(e));
}

// =============================================================================
// Section 8: PointCloud VertexAttributes — Incremental Re-extraction
// =============================================================================

TEST(PropertySetDirtySync, VertexAttributes_PointCloud_ReExtractsColors)
{
    entt::registry reg;
    auto e = reg.create();

    auto cloud = std::make_shared<Geometry::PointCloud::Cloud>();
    cloud->EnableColors();
    cloud->AddPoint({0, 0, 0});
    cloud->AddPoint({1, 0, 0});

    // Set specific colors.
    auto colorSpan = cloud->Colors();
    colorSpan[0] = {1.0f, 0.0f, 0.0f, 1.0f};
    colorSpan[1] = {0.0f, 0.0f, 1.0f, 1.0f};

    auto& pcd = reg.emplace<PointCloud::Data>(e);
    pcd.CloudRef = cloud;
    pcd.GpuDirty = false;
    pcd.GpuGeometry = Geometry::GeometryHandle(0, 1);
    pcd.GpuPointCount = 2;

    reg.emplace<DirtyTag::VertexAttributes>(e);

    Graphics::Systems::PropertySetDirtySync::OnUpdate(reg);

    EXPECT_FALSE(pcd.GpuDirty);
    ASSERT_EQ(pcd.CachedColors.size(), 2u);

    // Verify red (R in low byte).
    EXPECT_EQ((pcd.CachedColors[0] >> 0) & 0xFF, 255u);
    EXPECT_EQ((pcd.CachedColors[0] >> 8) & 0xFF, 0u);

    // Verify blue.
    EXPECT_EQ((pcd.CachedColors[1] >> 0) & 0xFF, 0u);
    EXPECT_EQ((pcd.CachedColors[1] >> 16) & 0xFF, 255u);
}

TEST(PropertySetDirtySync, VertexAttributes_PointCloud_ReExtractsRadii)
{
    entt::registry reg;
    auto e = reg.create();

    auto cloud = std::make_shared<Geometry::PointCloud::Cloud>();
    cloud->EnableRadii();
    cloud->AddPoint({0, 0, 0});

    auto radiiSpan = cloud->Radii();
    radiiSpan[0] = 0.05f;

    auto& pcd = reg.emplace<PointCloud::Data>(e);
    pcd.CloudRef = cloud;
    pcd.GpuDirty = false;
    pcd.GpuGeometry = Geometry::GeometryHandle(0, 1);
    pcd.GpuPointCount = 1;

    reg.emplace<DirtyTag::VertexAttributes>(e);

    Graphics::Systems::PropertySetDirtySync::OnUpdate(reg);

    EXPECT_FALSE(pcd.GpuDirty);
    ASSERT_EQ(pcd.CachedRadii.size(), 1u);
    EXPECT_FLOAT_EQ(pcd.CachedRadii[0], 0.05f);
}

// =============================================================================
// Section 9: Domain Independence — Key Invariant
// =============================================================================

TEST(PropertySetDirtySync, FaceColorChange_DoesNotTriggerVertexReUpload)
{
    // THE key contract: face color dirty does NOT trigger vertex buffer re-upload.
    entt::registry reg;
    auto e = reg.create();

    auto& gd = reg.emplace<Graph::Data>(e);
    gd.GpuDirty = false;

    auto& surf = reg.emplace<Surface::Component>(e);
    surf.FaceColorsDirty = false;

    // Only face attributes dirty — NOT vertex positions.
    reg.emplace<DirtyTag::FaceAttributes>(e);

    Graphics::Systems::PropertySetDirtySync::OnUpdate(reg);

    // Graph::Data must NOT be dirty (no vertex re-upload).
    EXPECT_FALSE(gd.GpuDirty);

    // Only face colors dirty.
    EXPECT_TRUE(surf.FaceColorsDirty);
}

TEST(PropertySetDirtySync, VertexAttributeChange_DoesNotTriggerEdgeRebuild)
{
    // Vertex attribute dirty does NOT trigger edge index buffer rebuild.
    entt::registry reg;
    auto e = reg.create();

    auto graph = std::make_shared<Geometry::Graph::Graph>();
    (void)graph->AddVertex({0, 0, 0});
    (void)graph->GetOrAddVertexProperty<glm::vec4>("v:color", {1, 1, 1, 1});

    auto& gd = reg.emplace<Graph::Data>(e);
    gd.GraphRef = graph;
    gd.GpuDirty = false;
    gd.GpuGeometry = Geometry::GeometryHandle(0, 1);
    gd.GpuVertexCount = 1;

    // Only vertex attributes dirty.
    reg.emplace<DirtyTag::VertexAttributes>(e);

    Graphics::Systems::PropertySetDirtySync::OnUpdate(reg);

    // Must NOT trigger full re-upload (which includes edge buffer rebuild).
    EXPECT_FALSE(gd.GpuDirty);
}

// =============================================================================
// Section 10: Multiple Dirty Domains Simultaneously
// =============================================================================

TEST(PropertySetDirtySync, MultipleDomains_AllProcessed)
{
    entt::registry reg;
    auto e = reg.create();

    auto graph = std::make_shared<Geometry::Graph::Graph>();
    (void)graph->AddVertex({0, 0, 0});
    (void)graph->GetOrAddVertexProperty<glm::vec4>("v:color", {1, 0, 0, 1});

    auto& gd = reg.emplace<Graph::Data>(e);
    gd.GraphRef = graph;
    gd.GpuDirty = false;
    gd.GpuGeometry = Geometry::GeometryHandle(0, 1);
    gd.GpuVertexCount = 1;

    auto& surf = reg.emplace<Surface::Component>(e);
    surf.FaceColorsDirty = false;

    // Tag both vertex attributes AND face attributes.
    reg.emplace<DirtyTag::VertexAttributes>(e);
    reg.emplace<DirtyTag::FaceAttributes>(e);

    Graphics::Systems::PropertySetDirtySync::OnUpdate(reg);

    // Vertex attributes re-extracted.
    EXPECT_FALSE(gd.GpuDirty);
    EXPECT_EQ(gd.CachedNodeColors.size(), 1u);

    // Face colors also flagged dirty.
    EXPECT_TRUE(surf.FaceColorsDirty);

    // Both tags cleared.
    EXPECT_FALSE(reg.all_of<DirtyTag::VertexAttributes>(e));
    EXPECT_FALSE(reg.all_of<DirtyTag::FaceAttributes>(e));
}

TEST(PropertySetDirtySync, PositionsAndAttributes_PositionWins)
{
    // When both VertexPositions and VertexAttributes are tagged,
    // position escalation to GpuDirty takes precedence. The lifecycle
    // system's full re-upload will re-extract attributes anyway.
    entt::registry reg;
    auto e = reg.create();

    auto graph = std::make_shared<Geometry::Graph::Graph>();
    graph->AddVertex({0, 0, 0});

    auto& gd = reg.emplace<Graph::Data>(e);
    gd.GraphRef = graph;
    gd.GpuDirty = false;
    gd.GpuGeometry = Geometry::GeometryHandle(0, 1);
    gd.GpuVertexCount = 1;

    reg.emplace<DirtyTag::VertexPositions>(e);
    reg.emplace<DirtyTag::VertexAttributes>(e);

    Graphics::Systems::PropertySetDirtySync::OnUpdate(reg);

    // Position dirty → full re-upload wins.
    EXPECT_TRUE(gd.GpuDirty);

    // Both tags cleared.
    EXPECT_FALSE(reg.all_of<DirtyTag::VertexPositions>(e));
    EXPECT_FALSE(reg.all_of<DirtyTag::VertexAttributes>(e));
}

// =============================================================================
// Section 11: No-Op on Clean Entities
// =============================================================================

TEST(PropertySetDirtySync, NoTags_NoSideEffects)
{
    entt::registry reg;
    auto e = reg.create();

    auto& gd = reg.emplace<Graph::Data>(e);
    gd.GpuDirty = false;

    auto& pcd = reg.emplace<PointCloud::Data>(e);
    pcd.GpuDirty = false;

    auto& surf = reg.emplace<Surface::Component>(e);
    surf.FaceColorsDirty = false;

    // No dirty tags attached.
    Graphics::Systems::PropertySetDirtySync::OnUpdate(reg);

    // Nothing should change.
    EXPECT_FALSE(gd.GpuDirty);
    EXPECT_FALSE(pcd.GpuDirty);
    EXPECT_FALSE(surf.FaceColorsDirty);
}

// =============================================================================
// Section 12: Null/Invalid Data Safety
// =============================================================================

TEST(PropertySetDirtySync, VertexAttributes_NullGraph_EscalatesToGpuDirty)
{
    entt::registry reg;
    auto e = reg.create();

    auto& gd = reg.emplace<Graph::Data>(e);
    gd.GraphRef = nullptr; // Null graph.
    gd.GpuDirty = false;

    reg.emplace<DirtyTag::VertexAttributes>(e);

    Graphics::Systems::PropertySetDirtySync::OnUpdate(reg);

    // Null graph → escalate to GpuDirty.
    EXPECT_TRUE(gd.GpuDirty);
}

TEST(PropertySetDirtySync, VertexAttributes_InvalidGeometry_EscalatesToGpuDirty)
{
    entt::registry reg;
    auto e = reg.create();

    auto graph = std::make_shared<Geometry::Graph::Graph>();
    graph->AddVertex({0, 0, 0});

    auto& gd = reg.emplace<Graph::Data>(e);
    gd.GraphRef = graph;
    gd.GpuDirty = false;
    gd.GpuGeometry = {}; // Invalid handle — no GPU buffer yet.

    reg.emplace<DirtyTag::VertexAttributes>(e);

    Graphics::Systems::PropertySetDirtySync::OnUpdate(reg);

    // Invalid GPU geometry → escalate to GpuDirty for initial upload.
    EXPECT_TRUE(gd.GpuDirty);
}
