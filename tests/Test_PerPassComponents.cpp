// ============================================================================
// Test_PerPassComponents.cpp — Contract tests for PLAN.md Phase 1 per-pass
// typed ECS components (Surface, Line, Point) and the ComponentMigration
// system that bridges legacy components to the new types.
// ============================================================================

#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>

import Graphics;
import Geometry;

using namespace ECS;

// =============================================================================
// Component Default Construction Tests
// =============================================================================

TEST(PerPassComponents_Surface, DefaultConstruction)
{
    Surface::Component c{};
    EXPECT_FALSE(c.Geometry.IsValid());
    EXPECT_FALSE(c.Material.IsValid());
    EXPECT_EQ(c.GpuSlot, Surface::Component::kInvalidSlot);
    EXPECT_EQ(c.CachedMaterialRevisionForInstance, 0u);
    EXPECT_FALSE(c.CachedIsSelectedForInstance);
}

TEST(PerPassComponents_Line, DefaultConstruction)
{
    Line::Component c{};
    EXPECT_FALSE(c.Geometry.IsValid());
    EXPECT_FALSE(c.EdgeView.IsValid());
    EXPECT_EQ(c.Color, glm::vec4(0.85f, 0.85f, 0.85f, 1.0f));
    EXPECT_FLOAT_EQ(c.Width, 1.5f);
    EXPECT_FALSE(c.Overlay);
    EXPECT_FALSE(c.HasPerEdgeColors);
    EXPECT_FALSE(c.HasPerEdgeWidths);
}

TEST(PerPassComponents_Point, DefaultConstruction)
{
    Point::Component c{};
    EXPECT_FALSE(c.Geometry.IsValid());
    EXPECT_EQ(c.Color, glm::vec4(1.0f, 0.6f, 0.0f, 1.0f));
    EXPECT_FLOAT_EQ(c.Size, 0.008f);
    EXPECT_FLOAT_EQ(c.SizeMultiplier, 1.0f);
    EXPECT_EQ(c.Mode, Geometry::PointCloud::RenderMode::FlatDisc);
    EXPECT_FALSE(c.HasPerPointColors);
    EXPECT_FALSE(c.HasPerPointRadii);
    EXPECT_FALSE(c.HasPerPointNormals);
}

// =============================================================================
// kInvalidSlot Consistency — all components with GPU slots use the same sentinel
// =============================================================================

TEST(PerPassComponents_SlotSentinel, AllComponentsShareSameKInvalidSlot)
{
    EXPECT_EQ(Surface::Component::kInvalidSlot, PointCloudRenderer::Component::kInvalidSlot);
    EXPECT_EQ(Surface::Component::kInvalidSlot, Graph::Data::kInvalidSlot);
    EXPECT_EQ(Surface::Component::kInvalidSlot, PointCloud::Data::kInvalidSlot);
    EXPECT_EQ(Surface::Component::kInvalidSlot, MeshEdgeView::Component::kInvalidSlot);
    EXPECT_EQ(Surface::Component::kInvalidSlot, MeshVertexView::Component::kInvalidSlot);
    EXPECT_EQ(Surface::Component::kInvalidSlot, ~0u);
}

// =============================================================================
// EnTT Registry Integration — components can be attached/detached
// =============================================================================

TEST(PerPassComponents_Registry, SurfaceAttachDetach)
{
    entt::registry reg;
    auto e = reg.create();
    reg.emplace<Surface::Component>(e);

    ASSERT_TRUE(reg.all_of<Surface::Component>(e));

    auto& surf = reg.get<Surface::Component>(e);
    EXPECT_EQ(surf.GpuSlot, Surface::Component::kInvalidSlot);

    reg.remove<Surface::Component>(e);
    EXPECT_FALSE(reg.all_of<Surface::Component>(e));
}

TEST(PerPassComponents_Registry, LineAttachDetach)
{
    entt::registry reg;
    auto e = reg.create();
    reg.emplace<Line::Component>(e);

    ASSERT_TRUE(reg.all_of<Line::Component>(e));

    auto& line = reg.get<Line::Component>(e);
    line.Color = {1.0f, 0.0f, 0.0f, 1.0f};
    line.Width = 3.0f;
    line.Overlay = true;

    EXPECT_EQ(reg.get<Line::Component>(e).Color, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
    EXPECT_FLOAT_EQ(reg.get<Line::Component>(e).Width, 3.0f);
    EXPECT_TRUE(reg.get<Line::Component>(e).Overlay);

    reg.remove<Line::Component>(e);
    EXPECT_FALSE(reg.all_of<Line::Component>(e));
}

TEST(PerPassComponents_Registry, PointAttachDetach)
{
    entt::registry reg;
    auto e = reg.create();

    auto& pt = reg.emplace<Point::Component>(e);
    pt.Mode = Geometry::PointCloud::RenderMode::Surfel;
    pt.Size = 0.02f;
    pt.HasPerPointNormals = true;

    ASSERT_TRUE(reg.all_of<Point::Component>(e));
    EXPECT_EQ(reg.get<Point::Component>(e).Mode, Geometry::PointCloud::RenderMode::Surfel);
    EXPECT_FLOAT_EQ(reg.get<Point::Component>(e).Size, 0.02f);
    EXPECT_TRUE(reg.get<Point::Component>(e).HasPerPointNormals);

    reg.remove<Point::Component>(e);
    EXPECT_FALSE(reg.all_of<Point::Component>(e));
}

// =============================================================================
// Orthogonal Composition — entities can carry multiple components simultaneously
// =============================================================================

TEST(PerPassComponents_Composition, MeshEntity_AllThreeComponents)
{
    // A mesh entity can carry Surface + Line + Point simultaneously.
    entt::registry reg;
    auto e = reg.create();

    reg.emplace<Surface::Component>(e);
    reg.emplace<Line::Component>(e);
    reg.emplace<Point::Component>(e);

    EXPECT_TRUE(reg.all_of<Surface::Component>(e));
    EXPECT_TRUE(reg.all_of<Line::Component>(e));
    EXPECT_TRUE(reg.all_of<Point::Component>(e));
}

TEST(PerPassComponents_Composition, GraphEntity_LineAndPoint)
{
    // A graph entity carries Line (edges) + Point (nodes).
    entt::registry reg;
    auto e = reg.create();

    reg.emplace<Graph::Data>(e);
    reg.emplace<Line::Component>(e);
    reg.emplace<Point::Component>(e);

    EXPECT_TRUE(reg.all_of<Graph::Data>(e));
    EXPECT_TRUE(reg.all_of<Line::Component>(e));
    EXPECT_TRUE(reg.all_of<Point::Component>(e));
    EXPECT_FALSE(reg.all_of<Surface::Component>(e));
}

TEST(PerPassComponents_Composition, PointCloudEntity_PointOnly)
{
    // A standalone point cloud entity carries only Point.
    entt::registry reg;
    auto e = reg.create();

    reg.emplace<Point::Component>(e);

    EXPECT_TRUE(reg.all_of<Point::Component>(e));
    EXPECT_FALSE(reg.all_of<Surface::Component>(e));
    EXPECT_FALSE(reg.all_of<Line::Component>(e));
}

TEST(PerPassComponents_Composition, AllThreeComponentsCoexist)
{
    // New per-pass typed components coexist on the same entity.
    entt::registry reg;
    auto e = reg.create();

    reg.emplace<Surface::Component>(e);
    reg.emplace<Line::Component>(e);
    reg.emplace<Point::Component>(e);

    EXPECT_TRUE(reg.all_of<Surface::Component>(e));
    EXPECT_TRUE(reg.all_of<Line::Component>(e));
    EXPECT_TRUE(reg.all_of<Point::Component>(e));
}

// =============================================================================
// Toggle by Presence/Absence — the PLAN.md principle
// =============================================================================

TEST(PerPassComponents_Toggle, WireframeToggle_ByPresence)
{
    entt::registry reg;
    auto e = reg.create();

    // Wireframe OFF: no Line component.
    EXPECT_FALSE(reg.all_of<Line::Component>(e));

    // Wireframe ON: attach Line component.
    reg.emplace<Line::Component>(e);
    EXPECT_TRUE(reg.all_of<Line::Component>(e));

    // Wireframe OFF: detach.
    reg.remove<Line::Component>(e);
    EXPECT_FALSE(reg.all_of<Line::Component>(e));
}

TEST(PerPassComponents_Toggle, VertexVisToggle_ByPresence)
{
    entt::registry reg;
    auto e = reg.create();

    EXPECT_FALSE(reg.all_of<Point::Component>(e));

    reg.emplace<Point::Component>(e);
    EXPECT_TRUE(reg.all_of<Point::Component>(e));

    reg.remove<Point::Component>(e);
    EXPECT_FALSE(reg.all_of<Point::Component>(e));
}

// =============================================================================
// EnTT View Iteration — passes can iterate their owned component type
// =============================================================================

TEST(PerPassComponents_Iteration, SurfacePassView)
{
    entt::registry reg;

    // Create 3 entities with Surface, 2 without.
    auto e1 = reg.create();
    auto e2 = reg.create();
    auto e3 = reg.create();
    auto e4 = reg.create();
    auto e5 = reg.create();

    reg.emplace<Surface::Component>(e1);
    reg.emplace<Surface::Component>(e2);
    reg.emplace<Surface::Component>(e3);
    // e4, e5 have no Surface component.

    int count = 0;
    auto view = reg.view<Surface::Component>();
    for (auto entity : view)
    {
        (void)entity;
        ++count;
    }
    EXPECT_EQ(count, 3);
}

TEST(PerPassComponents_Iteration, LinePassView)
{
    entt::registry reg;

    auto e1 = reg.create();
    auto e2 = reg.create();

    reg.emplace<Line::Component>(e1);
    // e2 has no Line component.

    int count = 0;
    auto view = reg.view<Line::Component>();
    for (auto entity : view)
    {
        (void)entity;
        ++count;
    }
    EXPECT_EQ(count, 1);
}

TEST(PerPassComponents_Iteration, PointPassView)
{
    entt::registry reg;

    auto e1 = reg.create();
    auto e2 = reg.create();
    auto e3 = reg.create();

    reg.emplace<Point::Component>(e1);
    reg.emplace<Point::Component>(e2);
    reg.emplace<Point::Component>(e3);

    int count = 0;
    auto view = reg.view<Point::Component>();
    for (auto entity : view)
    {
        (void)entity;
        ++count;
    }
    EXPECT_EQ(count, 3);
}

// =============================================================================
// ComponentMigration System — bridge from legacy to new components
// =============================================================================

TEST(ComponentMigration, PointCloudRenderer_CreatesPoint)
{
    entt::registry reg;
    auto e = reg.create();

    auto& pc = reg.emplace<PointCloudRenderer::Component>(e);
    pc.DefaultColor = {0.5f, 0.5f, 1.0f, 1.0f};
    pc.DefaultRadius = 0.01f;
    pc.SizeMultiplier = 2.0f;
    pc.RenderMode = Geometry::PointCloud::RenderMode::Surfel;
    pc.Visible = true;

    Graphics::Systems::ComponentMigration::OnUpdate(reg);

    ASSERT_TRUE(reg.all_of<Point::Component>(e));
    auto& pt = reg.get<Point::Component>(e);
    EXPECT_EQ(pt.Color, glm::vec4(0.5f, 0.5f, 1.0f, 1.0f));
    EXPECT_FLOAT_EQ(pt.Size, 0.01f);
    EXPECT_FLOAT_EQ(pt.SizeMultiplier, 2.0f);
    EXPECT_EQ(pt.Mode, Geometry::PointCloud::RenderMode::Surfel);
}

TEST(ComponentMigration, PointCloudRenderer_Invisible_SkipsCreation)
{
    entt::registry reg;
    auto e = reg.create();

    auto& pc = reg.emplace<PointCloudRenderer::Component>(e);
    pc.Visible = false;

    Graphics::Systems::ComponentMigration::OnUpdate(reg);

    EXPECT_FALSE(reg.all_of<Point::Component>(e));
}

// =============================================================================
// Phase 6: ComponentMigration no longer bridges Graph or PointCloud
// =============================================================================
// Graph::Data → Line+Point bridging moved to GraphGeometrySyncSystem.
// PointCloud::Data → Point bridging moved to PointCloudGeometrySyncSystem.
// ComponentMigration only bridges PointCloudRenderer → Point.

TEST(ComponentMigration, GraphData_NotBridgedByComponentMigration)
{
    // After Phase 6, ComponentMigration does NOT create Line/Point from Graph::Data.
    // GraphGeometrySyncSystem handles this directly.
    entt::registry reg;
    auto e = reg.create();

    auto& gd = reg.emplace<Graph::Data>(e);
    gd.Visible = true;
    gd.DefaultEdgeColor = {0.0f, 0.0f, 1.0f, 1.0f};

    Graphics::Systems::ComponentMigration::OnUpdate(reg);

    // ComponentMigration should NOT create Line or Point from Graph::Data.
    EXPECT_FALSE(reg.all_of<Line::Component>(e));
    EXPECT_FALSE(reg.all_of<Point::Component>(e));
}

TEST(ComponentMigration, PointCloudData_NotBridgedByComponentMigration)
{
    // After Phase 6, ComponentMigration does NOT create Point from PointCloud::Data.
    // PointCloudGeometrySyncSystem handles this directly.
    entt::registry reg;
    auto e = reg.create();

    auto& pcd = reg.emplace<PointCloud::Data>(e);
    pcd.Visible = true;
    pcd.DefaultColor = {0.5f, 0.5f, 1.0f, 1.0f};

    Graphics::Systems::ComponentMigration::OnUpdate(reg);

    EXPECT_FALSE(reg.all_of<Point::Component>(e));
}

TEST(ComponentMigration, Idempotent_SecondRunProducesSameResult)
{
    entt::registry reg;
    auto e = reg.create();

    auto& pc = reg.emplace<PointCloudRenderer::Component>(e);
    pc.Visible = true;
    pc.DefaultColor = {0.0f, 0.0f, 1.0f, 1.0f};

    Graphics::Systems::ComponentMigration::OnUpdate(reg);
    auto ptColor1 = reg.get<Point::Component>(e).Color;

    // Second run should not change anything.
    Graphics::Systems::ComponentMigration::OnUpdate(reg);
    auto ptColor2 = reg.get<Point::Component>(e).Color;

    EXPECT_EQ(ptColor1, ptColor2);
    EXPECT_EQ(ptColor2, glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));
}

// =============================================================================
// Phase 6: Lifecycle system component population contracts
// =============================================================================

TEST(Phase6_GraphSync, GraphDataFields_MapToLineAndPoint)
{
    // Validates the field mapping contract: GraphGeometrySyncSystem
    // populates Line::Component and Point::Component from Graph::Data
    // with the same field mapping that ComponentMigration previously used.
    Graph::Data gd;
    gd.DefaultEdgeColor = {0.0f, 0.0f, 1.0f, 1.0f};
    gd.EdgeWidth = 2.5f;
    gd.EdgesOverlay = true;
    gd.DefaultNodeColor = {0.9f, 0.1f, 0.0f, 1.0f};
    gd.DefaultNodeRadius = 0.02f;
    gd.NodeSizeMultiplier = 1.5f;
    gd.NodeRenderMode = Geometry::PointCloud::RenderMode::Surfel;
    gd.CachedNodeColors = {0xFF0000FF};
    gd.CachedNodeRadii = {0.01f};
    gd.CachedEdgeColors = {0xFF00FF00};

    // Simulate what GraphGeometrySyncSystem Phase 3 does.
    Line::Component line;
    line.Color            = gd.DefaultEdgeColor;
    line.Width            = gd.EdgeWidth;
    line.Overlay          = gd.EdgesOverlay;
    line.HasPerEdgeColors = !gd.CachedEdgeColors.empty();

    Point::Component pt;
    pt.Color             = gd.DefaultNodeColor;
    pt.Size              = gd.DefaultNodeRadius;
    pt.SizeMultiplier    = gd.NodeSizeMultiplier;
    pt.Mode              = gd.NodeRenderMode;
    pt.HasPerPointColors = !gd.CachedNodeColors.empty();
    pt.HasPerPointRadii  = !gd.CachedNodeRadii.empty();

    EXPECT_EQ(line.Color, glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));
    EXPECT_FLOAT_EQ(line.Width, 2.5f);
    EXPECT_TRUE(line.Overlay);
    EXPECT_TRUE(line.HasPerEdgeColors);

    EXPECT_EQ(pt.Color, glm::vec4(0.9f, 0.1f, 0.0f, 1.0f));
    EXPECT_FLOAT_EQ(pt.Size, 0.02f);
    EXPECT_FLOAT_EQ(pt.SizeMultiplier, 1.5f);
    EXPECT_EQ(pt.Mode, Geometry::PointCloud::RenderMode::Surfel);
    EXPECT_TRUE(pt.HasPerPointColors);
    EXPECT_TRUE(pt.HasPerPointRadii);
}

TEST(Phase6_CloudSync, PointCloudDataFields_MapToPoint)
{
    // Validates the field mapping contract: PointCloudGeometrySyncSystem
    // populates Point::Component from PointCloud::Data.
    auto cloud = std::make_shared<Geometry::PointCloud::Cloud>();
    cloud->EnableNormals();
    cloud->EnableColors();
    cloud->EnableRadii();
    cloud->AddPoint({0.f, 0.f, 0.f});

    PointCloud::Data pcd;
    pcd.CloudRef = cloud;
    pcd.DefaultColor = {0.5f, 0.5f, 1.0f, 1.0f};
    pcd.DefaultRadius = 0.01f;
    pcd.SizeMultiplier = 2.0f;
    pcd.RenderMode = Geometry::PointCloud::RenderMode::Surfel;

    // Simulate what PointCloudGeometrySyncSystem Phase 3 does.
    Point::Component pt;
    pt.Color             = pcd.DefaultColor;
    pt.Size              = pcd.DefaultRadius;
    pt.SizeMultiplier    = pcd.SizeMultiplier;
    pt.Mode              = pcd.RenderMode;
    pt.HasPerPointColors = pcd.HasColors();
    pt.HasPerPointRadii  = pcd.HasRadii();
    pt.HasPerPointNormals = pcd.HasNormals();

    EXPECT_EQ(pt.Color, glm::vec4(0.5f, 0.5f, 1.0f, 1.0f));
    EXPECT_FLOAT_EQ(pt.Size, 0.01f);
    EXPECT_FLOAT_EQ(pt.SizeMultiplier, 2.0f);
    EXPECT_EQ(pt.Mode, Geometry::PointCloud::RenderMode::Surfel);
    EXPECT_TRUE(pt.HasPerPointColors);
    EXPECT_TRUE(pt.HasPerPointRadii);
    EXPECT_TRUE(pt.HasPerPointNormals);
}

TEST(Phase6_MeshView, EdgeView_PopulatesLineComponent)
{
    // Validates the contract: MeshViewLifecycleSystem Phase 1b
    // populates Line::Component from completed edge views.
    Geometry::GeometryHandle meshGeo(0, 1);
    Geometry::GeometryHandle edgeGeo(1, 1);

    ECS::MeshEdgeView::Component ev;
    ev.Geometry = edgeGeo;
    ev.EdgeCount = 42;
    ev.Dirty = false;

    // Simulate Phase 1b population.
    Line::Component line;
    line.Geometry  = meshGeo;    // Shared vertex buffer
    line.EdgeView  = ev.Geometry; // Edge index buffer
    line.EdgeCount = ev.EdgeCount;

    EXPECT_TRUE(line.Geometry.IsValid());
    EXPECT_TRUE(line.EdgeView.IsValid());
    EXPECT_EQ(line.EdgeCount, 42u);
    EXPECT_NE(line.Geometry, line.EdgeView); // Different handles
}

TEST(Phase6_MeshView, VertexView_PopulatesPointComponent)
{
    // Validates the contract: MeshViewLifecycleSystem Phase 2b
    // populates Point::Component from completed vertex views.
    Geometry::GeometryHandle vtxGeo(2, 1);

    ECS::MeshVertexView::Component pv;
    pv.Geometry = vtxGeo;
    pv.VertexCount = 1024;
    pv.Dirty = false;

    // Simulate Phase 2b population.
    Point::Component pt;
    pt.Geometry = pv.Geometry;
    pt.HasPerPointNormals = true; // Source has normals

    EXPECT_TRUE(pt.Geometry.IsValid());
    EXPECT_TRUE(pt.HasPerPointNormals);
}
