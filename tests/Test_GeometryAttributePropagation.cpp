#include <gtest/gtest.h>

#include <glm/glm.hpp>

import Geometry;

// These tests enforce a PMP-style contract:
// Topology edits (Split/Collapse) must keep user vertex properties consistent.
// We test with a simple per-vertex "texcoord" property stored in the mesh.

TEST(Attributes_TopologyEdits, SplitInterpolatesVertexProperty)
{
    using namespace Geometry;

    Halfedge::Mesh mesh;
    auto v0 = mesh.AddVertex(glm::vec3(0, 0, 0));
    auto v1 = mesh.AddVertex(glm::vec3(1, 0, 0));
    auto v2 = mesh.AddVertex(glm::vec3(0, 1, 0));
    ASSERT_TRUE(mesh.AddTriangle(v0, v1, v2).has_value());

    // Add a "texcoord" property.
    auto uv = mesh.VertexProperties().GetOrAdd<glm::vec2>("v:texcoord", glm::vec2(0.0f));
    uv[v0] = glm::vec2(0.0f, 0.0f);
    uv[v1] = glm::vec2(1.0f, 0.0f);
    uv[v2] = glm::vec2(0.0f, 1.0f);

    Halfedge::Mesh::VertexAttributeTransfer rule;
    rule.Name = "v:texcoord";
    rule.Rule = Halfedge::Mesh::VertexAttributeTransfer::Policy::Average;
    mesh.SetVertexAttributeTransferRules(std::span<const Halfedge::Mesh::VertexAttributeTransfer>(&rule, 1));

    // Split edge (v0, v1).
    auto eOpt = mesh.FindEdge(v0, v1);
    ASSERT_TRUE(eOpt.has_value());

    VertexHandle vm = mesh.Split(*eOpt, glm::vec3(0.5f, 0.0f, 0.0f));
    ASSERT_TRUE(vm.IsValid());

    const glm::vec2 expected = 0.5f * (uv[v0] + uv[v1]);
    EXPECT_NEAR(uv[vm].x, expected.x, 1e-6f);
    EXPECT_NEAR(uv[vm].y, expected.y, 1e-6f);
}

TEST(Attributes_TopologyEdits, CollapseMergesVertexProperty)
{
    using namespace Geometry;

    Halfedge::Mesh mesh;
    auto v0 = mesh.AddVertex(glm::vec3(0, 0, 0));
    auto v1 = mesh.AddVertex(glm::vec3(1, 0, 0));
    auto v2 = mesh.AddVertex(glm::vec3(0, 1, 0));
    auto v3 = mesh.AddVertex(glm::vec3(1, 1, 0));
    ASSERT_TRUE(mesh.AddTriangle(v0, v1, v2).has_value());
    ASSERT_TRUE(mesh.AddTriangle(v2, v1, v3).has_value());

    auto uv = mesh.VertexProperties().GetOrAdd<glm::vec2>("v:texcoord", glm::vec2(0.0f));
    uv[v0] = glm::vec2(0.0f, 0.0f);
    uv[v1] = glm::vec2(1.0f, 0.0f);
    uv[v2] = glm::vec2(0.0f, 1.0f);
    uv[v3] = glm::vec2(1.0f, 1.0f);

    Halfedge::Mesh::VertexAttributeTransfer rule;
    rule.Name = "v:texcoord";
    rule.Rule = Halfedge::Mesh::VertexAttributeTransfer::Policy::Average;
    mesh.SetVertexAttributeTransferRules(std::span<const Halfedge::Mesh::VertexAttributeTransfer>(&rule, 1));

    // Collapse the shared diagonal edge (v1, v2). This is interior here.
    auto eOpt = mesh.FindEdge(v1, v2);
    ASSERT_TRUE(eOpt.has_value());

    // Put survivor at midpoint.
    auto h = mesh.Halfedge(*eOpt, 0);
    const VertexHandle a = mesh.FromVertex(h);
    const VertexHandle b = mesh.ToVertex(h);
    const glm::vec3 mid = 0.5f * (mesh.Position(a) + mesh.Position(b));

    auto vSurvivorOpt = mesh.Collapse(*eOpt, mid);
    ASSERT_TRUE(vSurvivorOpt.has_value());

    // The current implementation keeps `FromVertex(h0)` as survivor.
    const VertexHandle vSurvivor = *vSurvivorOpt;

    const glm::vec2 expectedAvg = 0.5f * (uv[a] + uv[b]);
    EXPECT_NEAR(uv[vSurvivor].x, expectedAvg.x, 1e-6f);
    EXPECT_NEAR(uv[vSurvivor].y, expectedAvg.y, 1e-6f);
}

