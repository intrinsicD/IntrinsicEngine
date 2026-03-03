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

TEST(ComponentMigration, GraphData_CreatesLineAndPoint)
{
    entt::registry reg;
    auto e = reg.create();

    auto& gd = reg.emplace<Graph::Data>(e);
    gd.DefaultNodeColor = {0.9f, 0.1f, 0.0f, 1.0f};
    gd.DefaultEdgeColor = {0.0f, 0.0f, 1.0f, 1.0f};
    gd.DefaultNodeRadius = 0.02f;
    gd.EdgeWidth = 2.5f;
    gd.EdgesOverlay = true;
    gd.Visible = true;

    Graphics::Systems::ComponentMigration::OnUpdate(reg);

    // Line from graph edges.
    ASSERT_TRUE(reg.all_of<Line::Component>(e));
    auto& line = reg.get<Line::Component>(e);
    EXPECT_EQ(line.Color, glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));
    EXPECT_FLOAT_EQ(line.Width, 2.5f);
    EXPECT_TRUE(line.Overlay);

    // Point from graph nodes.
    ASSERT_TRUE(reg.all_of<Point::Component>(e));
    auto& pt = reg.get<Point::Component>(e);
    EXPECT_EQ(pt.Color, glm::vec4(0.9f, 0.1f, 0.0f, 1.0f));
    EXPECT_FLOAT_EQ(pt.Size, 0.02f);
}

TEST(ComponentMigration, GraphData_Invisible_SkipsCreation)
{
    entt::registry reg;
    auto e = reg.create();

    auto& gd = reg.emplace<Graph::Data>(e);
    gd.Visible = false;

    Graphics::Systems::ComponentMigration::OnUpdate(reg);

    EXPECT_FALSE(reg.all_of<Line::Component>(e));
    EXPECT_FALSE(reg.all_of<Point::Component>(e));
}

TEST(ComponentMigration, Idempotent_SecondRunProducesSameResult)
{
    entt::registry reg;
    auto e = reg.create();

    auto& gd = reg.emplace<Graph::Data>(e);
    gd.Visible = true;
    gd.DefaultEdgeColor = {0.0f, 0.0f, 1.0f, 1.0f};

    Graphics::Systems::ComponentMigration::OnUpdate(reg);
    auto lineColor1 = reg.get<Line::Component>(e).Color;

    // Second run should not change anything.
    Graphics::Systems::ComponentMigration::OnUpdate(reg);
    auto lineColor2 = reg.get<Line::Component>(e).Color;

    EXPECT_EQ(lineColor1, lineColor2);
    EXPECT_EQ(lineColor2, glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));
}
