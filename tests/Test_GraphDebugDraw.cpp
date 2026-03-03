#include <gtest/gtest.h>
#include <memory>

#include <glm/glm.hpp>

import Graphics; // DebugDraw
import ECS;
import Geometry;

// =============================================================================
// Graph Debug Draw — Contract tests
// =============================================================================
//
// Graph edge rendering is handled by LinePass reading ECS::Line::Component
// (populated by ComponentMigration from Graph::Data). This test validates
// the DebugDraw API for transient graph debug overlays.

TEST(Graphics_GraphDebugDraw, GraphEntityWithDebugDraw)
{
    ECS::Scene scene;

    // Create one graph entity using PropertySet-backed ECS::Graph::Data.
    auto& reg = scene.GetRegistry();
    const entt::entity e = reg.create();

    auto graph = std::make_shared<Geometry::Graph::Graph>();
    auto v0 = graph->AddVertex(glm::vec3(0.0f));
    auto v1 = graph->AddVertex(glm::vec3(1.0f, 0.0f, 0.0f));
    graph->AddEdge(v0, v1);

    ECS::Graph::Data graphData{};
    graphData.Visible = true;
    graphData.GraphRef = graph;
    reg.emplace<ECS::Graph::Data>(e, graphData);

    Graphics::DebugDraw dd;

    // DebugDraw starts empty — transient overlays are added per-frame.
    EXPECT_EQ(dd.GetLineCount(), 0u);
    EXPECT_FALSE(dd.HasContent());
}
