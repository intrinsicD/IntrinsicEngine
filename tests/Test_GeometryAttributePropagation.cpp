#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <span>
#include <vector>
#include <glm/glm.hpp>

import Geometry;

#include "TestMeshBuilders.h"

namespace
{
    constexpr float kEps = 1e-6f;

    void EnableTexcoordTransfer(Geometry::Halfedge::Mesh& mesh)
    {
        Geometry::Halfedge::Mesh::VertexAttributeTransfer rule;
        rule.Name = "v:texcoord";
        rule.Rule = Geometry::Halfedge::Mesh::VertexAttributeTransfer::Policy::Average;
        mesh.SetVertexAttributeTransferRules(std::span<const Geometry::Halfedge::Mesh::VertexAttributeTransfer>(&rule, 1));
    }

    void AssignPlanarTexcoords(Geometry::Halfedge::Mesh& mesh)
    {
        auto uv = Geometry::VertexProperty<glm::vec2>(
            mesh.VertexProperties().GetOrAdd<glm::vec2>("v:texcoord", glm::vec2(0.0f)));

        for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
        {
            Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(i)};
            if (!mesh.IsValid(v) || mesh.IsDeleted(v))
            {
                continue;
            }

            const glm::vec3 p = mesh.Position(v);
            uv[v] = glm::vec2(0.5f * p.x + 0.73f, 0.5f * p.y + 0.41f);
        }
    }

    void ExpectFiniteTexcoords(const Geometry::Halfedge::Mesh& mesh)
    {
        const auto uv = mesh.VertexProperties().Get<glm::vec2>("v:texcoord");
        ASSERT_TRUE(static_cast<bool>(uv));

        for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
        {
            Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(i)};
            if (!mesh.IsValid(v) || mesh.IsDeleted(v))
            {
                continue;
            }

            EXPECT_TRUE(std::isfinite(uv[i].x));
            EXPECT_TRUE(std::isfinite(uv[i].y));
        }
    }

    Geometry::VertexHandle FindVertexByPosition(const Geometry::Halfedge::Mesh& mesh, const glm::vec3& position)
    {
        for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
        {
            Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(i)};
            if (!mesh.IsValid(v) || mesh.IsDeleted(v))
            {
                continue;
            }
            if (glm::length(mesh.Position(v) - position) <= kEps)
            {
                return v;
            }
        }
        return {};
    }
}

// These tests enforce a PMP-style contract:
// Topology edits (Split/Collapse) must keep user vertex properties consistent.
// We test with a simple per-vertex "texcoord" property stored in the mesh.

TEST(Attributes_TopologyEdits, SplitInterpolatesVertexPropertyAtRequestedPosition)
{
    using namespace Geometry;

    Halfedge::Mesh mesh;
    auto v0 = mesh.AddVertex(glm::vec3(0, 0, 0));
    auto v1 = mesh.AddVertex(glm::vec3(1, 0, 0));
    auto v2 = mesh.AddVertex(glm::vec3(0, 1, 0));
    ASSERT_TRUE(mesh.AddTriangle(v0, v1, v2).has_value());

    // Add a "texcoord" property.
    auto uv = VertexProperty<glm::vec2>(mesh.VertexProperties().GetOrAdd<glm::vec2>("v:texcoord", glm::vec2(0.0f)));
    uv[v0] = glm::vec2(0.0f, 0.0f);
    uv[v1] = glm::vec2(1.0f, 0.0f);
    uv[v2] = glm::vec2(0.0f, 1.0f);

    EnableTexcoordTransfer(mesh);

    // Split edge (v0, v1).
    auto eOpt = mesh.FindEdge(v0, v1);
    ASSERT_TRUE(eOpt.has_value());

    constexpr float t = 0.25f;
    VertexHandle vm = mesh.Split(*eOpt, glm::vec3(t, 0.0f, 0.0f));
    ASSERT_TRUE(vm.IsValid());

    const glm::vec2 expected = (1.0f - t) * uv[v0] + t * uv[v1];
    EXPECT_NEAR(uv[vm].x, expected.x, 1e-6f);
    EXPECT_NEAR(uv[vm].y, expected.y, 1e-6f);
}

TEST(Attributes_TopologyEdits, CollapseInterpolatesVertexPropertyAtRequestedPosition)
{
    using namespace Geometry;

    Halfedge::Mesh mesh;
    auto v0 = mesh.AddVertex(glm::vec3(0, 0, 0));
    auto v1 = mesh.AddVertex(glm::vec3(1, 0, 0));
    auto v2 = mesh.AddVertex(glm::vec3(0, 1, 0));
    auto v3 = mesh.AddVertex(glm::vec3(1, 1, 0));
    ASSERT_TRUE(mesh.AddTriangle(v0, v1, v2).has_value());
    ASSERT_TRUE(mesh.AddTriangle(v2, v1, v3).has_value());

    auto uv = VertexProperty<glm::vec2>(mesh.VertexProperties().GetOrAdd<glm::vec2>("v:texcoord", glm::vec2(0.0f)));
    uv[v0] = glm::vec2(0.0f, 0.0f);
    uv[v1] = glm::vec2(1.0f, 0.0f);
    uv[v2] = glm::vec2(0.0f, 1.0f);
    uv[v3] = glm::vec2(1.0f, 1.0f);

    EnableTexcoordTransfer(mesh);

    // Collapse the shared diagonal edge (v1, v2). This is interior here.
    auto eOpt = mesh.FindEdge(v1, v2);
    ASSERT_TRUE(eOpt.has_value());

    // Verify topology is sane before collapse.
    EXPECT_FALSE(mesh.IsBoundary(*eOpt)) << "edge (v1,v2) should be interior";
    EXPECT_FALSE(mesh.IsIsolated(v1));
    EXPECT_FALSE(mesh.IsIsolated(v2));
    EXPECT_TRUE(mesh.IsCollapseOk(*eOpt)) << "link condition should pass for interior edge in a 2-tri quad";

    // Put survivor at a non-midpoint position so the test catches hardcoded 50/50 averaging.
    auto h = mesh.Halfedge(*eOpt, 0);
    const VertexHandle a = mesh.FromVertex(h);
    const VertexHandle b = mesh.ToVertex(h);
    constexpr float t = 0.25f;
    const glm::vec3 target = (1.0f - t) * mesh.Position(a) + t * mesh.Position(b);

    // Capture expected UV average BEFORE collapse — the survivor's UV is overwritten.
    const glm::vec2 expectedAvg = (1.0f - t) * uv[a] + t * uv[b];

    auto vSurvivorOpt = mesh.Collapse(*eOpt, target);
    ASSERT_TRUE(vSurvivorOpt.has_value());

    const VertexHandle vSurvivor = *vSurvivorOpt;

    EXPECT_NEAR(uv[vSurvivor].x, expectedAvg.x, 1e-6f);
    EXPECT_NEAR(uv[vSurvivor].y, expectedAvg.y, 1e-6f);
}

TEST(Attributes_TriangleSoupBridge, BuildAndExtractPreserveTexcoords)
{
    using namespace Geometry;

    const std::vector<glm::vec3> positions{{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};
    const std::vector<uint32_t> indices{0, 1, 2};
    const std::vector<glm::vec4> aux{{0.0f, 0.0f, 7.0f, 9.0f}, {1.0f, 0.0f, 3.0f, 4.0f}, {0.0f, 1.0f, 5.0f, 6.0f}};

    auto mesh = MeshUtils::BuildHalfedgeMeshFromIndexedTriangles(positions, indices, aux);
    ASSERT_TRUE(mesh.has_value());

    std::vector<glm::vec3> extractedPositions;
    std::vector<uint32_t> extractedIndices;
    std::vector<glm::vec4> extractedAux;
    MeshUtils::ExtractIndexedTriangles(*mesh, extractedPositions, extractedIndices, &extractedAux);

    ASSERT_EQ(extractedPositions.size(), positions.size());
    ASSERT_EQ(extractedIndices.size(), indices.size());
    ASSERT_EQ(extractedAux.size(), aux.size());
    EXPECT_EQ(extractedIndices, indices);

    for (std::size_t i = 0; i < aux.size(); ++i)
    {
        EXPECT_NEAR(extractedAux[i].x, aux[i].x, kEps);
        EXPECT_NEAR(extractedAux[i].y, aux[i].y, kEps);
    }
}

TEST(Attributes_TriangleSoupBridge, WeldSkipsUVSeams)
{
    using namespace Geometry;

    const std::vector<glm::vec3> positions{
        {0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {1.0f, 1.0f, 0.0f},
        {1.0f, 0.0f, 0.0f}, // same position as vertex 1, different UV seam
    };
    const std::vector<uint32_t> indices{0, 1, 2, 2, 4, 3};
    const std::vector<glm::vec4> aux{
        {0.0f, 0.0f, 0.0f, 0.0f},
        {0.25f, 0.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f, 0.0f},
        {1.0f, 1.0f, 0.0f, 0.0f},
        {0.75f, 0.0f, 0.0f, 0.0f},
    };

    MeshUtils::TriangleSoupBuildParams params;
    params.WeldVertices = true;
    params.WeldEpsilon = 1e-6f;

    auto mesh = MeshUtils::BuildHalfedgeMeshFromIndexedTriangles(positions, indices, aux, params);
    ASSERT_TRUE(mesh.has_value());
    EXPECT_EQ(mesh->VertexCount(), positions.size());
}

TEST(Attributes_Subdivision, LoopSubdivisionPreservesTexcoords)
{
    using namespace Geometry;

    Halfedge::Mesh mesh;
    auto v0 = mesh.AddVertex(glm::vec3(0, 0, 0));
    auto v1 = mesh.AddVertex(glm::vec3(1, 0, 0));
    auto v2 = mesh.AddVertex(glm::vec3(0, 1, 0));
    ASSERT_TRUE(mesh.AddTriangle(v0, v1, v2).has_value());

    auto uv = VertexProperty<glm::vec2>(mesh.VertexProperties().GetOrAdd<glm::vec2>("v:texcoord", glm::vec2(0.0f)));
    uv[v0] = glm::vec2(0.0f, 0.0f);
    uv[v1] = glm::vec2(1.0f, 0.0f);
    uv[v2] = glm::vec2(0.0f, 1.0f);

    Halfedge::Mesh out;
    auto result = Subdivision::Subdivide(mesh, out, {.Iterations = 1});
    ASSERT_TRUE(result.has_value());

    const auto outUv = out.VertexProperties().Get<glm::vec2>("v:texcoord");
    ASSERT_TRUE(static_cast<bool>(outUv));

    const VertexHandle midpoint = FindVertexByPosition(out, glm::vec3(0.5f, 0.0f, 0.0f));
    ASSERT_TRUE(midpoint.IsValid());
    EXPECT_NEAR(outUv[midpoint.Index].x, 0.5f, kEps);
    EXPECT_NEAR(outUv[midpoint.Index].y, 0.0f, kEps);
}

TEST(Attributes_Subdivision, CatmullClarkPreservesTexcoords)
{
    using namespace Geometry;

    Halfedge::Mesh mesh;
    auto v0 = mesh.AddVertex(glm::vec3(0, 0, 0));
    auto v1 = mesh.AddVertex(glm::vec3(1, 0, 0));
    auto v2 = mesh.AddVertex(glm::vec3(1, 1, 0));
    auto v3 = mesh.AddVertex(glm::vec3(0, 1, 0));
    ASSERT_TRUE(mesh.AddQuad(v0, v1, v2, v3).has_value());

    auto uv = VertexProperty<glm::vec2>(mesh.VertexProperties().GetOrAdd<glm::vec2>("v:texcoord", glm::vec2(0.0f)));
    uv[v0] = glm::vec2(0.0f, 0.0f);
    uv[v1] = glm::vec2(1.0f, 0.0f);
    uv[v2] = glm::vec2(1.0f, 1.0f);
    uv[v3] = glm::vec2(0.0f, 1.0f);

    Halfedge::Mesh out;
    auto result = CatmullClark::Subdivide(mesh, out, {.Iterations = 1});
    ASSERT_TRUE(result.has_value());

    const auto outUv = out.VertexProperties().Get<glm::vec2>("v:texcoord");
    ASSERT_TRUE(static_cast<bool>(outUv));

    const VertexHandle center = FindVertexByPosition(out, glm::vec3(0.5f, 0.5f, 0.0f));
    ASSERT_TRUE(center.IsValid());
    EXPECT_NEAR(outUv[center.Index].x, 0.5f, kEps);
    EXPECT_NEAR(outUv[center.Index].y, 0.5f, kEps);

    std::vector<glm::vec3> extractedPositions;
    std::vector<uint32_t> extractedIndices;
    std::vector<glm::vec4> extractedAux;
    MeshUtils::ExtractIndexedTriangles(out, extractedPositions, extractedIndices, &extractedAux);

    EXPECT_EQ(extractedPositions.size(), out.VertexCount());
    EXPECT_EQ(extractedAux.size(), out.VertexCount());
    EXPECT_EQ(extractedIndices.size(), out.FaceCount() * 6u);

    bool foundCenterAux = false;
    for (std::size_t i = 0; i < extractedPositions.size(); ++i)
    {
        if (glm::length(extractedPositions[i] - glm::vec3(0.5f, 0.5f, 0.0f)) <= kEps)
        {
            foundCenterAux = true;
            EXPECT_NEAR(extractedAux[i].x, 0.5f, kEps);
            EXPECT_NEAR(extractedAux[i].y, 0.5f, kEps);
        }
    }
    EXPECT_TRUE(foundCenterAux);
}

TEST(Attributes_TopologyOperators, SimplificationPreservesTexcoords)
{
    using namespace Geometry;

    auto mesh = MakeIcosahedron();
    AssignPlanarTexcoords(mesh);
    EnableTexcoordTransfer(mesh);

    const std::size_t initialFaces = mesh.FaceCount();

    Simplification::SimplificationParams params;
    params.TargetFaces = initialFaces / 2u;
    params.PreserveBoundary = false;

    auto result = Simplification::Simplify(mesh, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_GT(result->CollapseCount, 0u);
    EXPECT_LT(mesh.FaceCount(), initialFaces);
    ExpectFiniteTexcoords(mesh);
}

TEST(Attributes_TopologyOperators, SimplificationWithConfigurableQuadricsPreservesTexcoords)
{
    using namespace Geometry;

    auto mesh = MakeIcosahedron();
    AssignPlanarTexcoords(mesh);
    EnableTexcoordTransfer(mesh);

    const std::size_t initialFaces = mesh.FaceCount();

    Simplification::SimplificationParams params;
    params.TargetFaces = initialFaces / 2u;
    params.PreserveBoundary = false;
    params.Quadric.Type = Simplification::QuadricType::Triangle;
    params.Quadric.Residence = Simplification::QuadricResidence::VerticesAndFaces;
    params.Quadric.PlacementPolicy = Simplification::CollapsePlacementPolicy::BestOfEndpointsAndMinimizer;
    params.Quadric.ProbabilisticMode = Simplification::QuadricProbabilisticMode::Isotropic;
    params.Quadric.PositionStdDev = 0.01;

    auto result = Simplification::Simplify(mesh, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_GT(result->CollapseCount, 0u);
    EXPECT_LT(mesh.FaceCount(), initialFaces);
    ExpectFiniteTexcoords(mesh);
}

TEST(Attributes_TopologyOperators, IsotropicRemeshingPreservesTexcoords)
{
    using namespace Geometry;

    auto mesh = MakeIcosahedron();
    AssignPlanarTexcoords(mesh);
    EnableTexcoordTransfer(mesh);

    const std::size_t initialVertices = mesh.VertexCount();

    Remeshing::RemeshingParams params;
    params.TargetLength = 0.35;
    params.Iterations = 1;
    params.PreserveBoundary = false;

    auto result = Remeshing::Remesh(mesh, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_GT(result->SplitCount, 0u);
    EXPECT_GT(mesh.VertexCount(), initialVertices);
    ExpectFiniteTexcoords(mesh);
}

TEST(Attributes_TopologyOperators, AdaptiveRemeshingPreservesTexcoords)
{
    using namespace Geometry;

    auto mesh = MakeIcosahedron();
    AssignPlanarTexcoords(mesh);
    EnableTexcoordTransfer(mesh);

    const std::size_t initialVertices = mesh.VertexCount();

    AdaptiveRemeshing::AdaptiveRemeshingParams params;
    params.MinEdgeLength = 0.2;
    params.MaxEdgeLength = 0.3;
    params.CurvatureAdaptation = 0.0;
    params.Iterations = 1;
    params.PreserveBoundary = false;
    params.MaxOpsPerIteration = 0;

    auto result = AdaptiveRemeshing::AdaptiveRemesh(mesh, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_GT(result->SplitCount, 0u);
    EXPECT_GT(mesh.VertexCount(), initialVertices);
    ExpectFiniteTexcoords(mesh);
}
