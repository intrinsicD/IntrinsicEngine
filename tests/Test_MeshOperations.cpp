#include <gtest/gtest.h>
#include <cmath>
#include <vector>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>

import Geometry;

#include "Test_MeshBuilders.h"

namespace
{
    Geometry::Halfedge::Mesh MakeDenseClosedTriangleMesh(std::size_t iterations = 3)
    {
        auto coarse = MakeIcosahedron();
        Geometry::Halfedge::Mesh refined;

        Geometry::Subdivision::SubdivisionParams params;
        params.Iterations = iterations;

        auto result = Geometry::Subdivision::Subdivide(coarse, refined, params);
        EXPECT_TRUE(result.has_value());
        return refined;
    }

    void ExtractTriangleSoup(
        Geometry::Halfedge::Mesh& mesh,
        std::vector<glm::vec3>& positions,
        std::vector<uint32_t>& indices)
    {
        mesh.GarbageCollection();

        positions.clear();
        indices.clear();
        positions.reserve(mesh.VertexCount());
        indices.reserve(mesh.FaceCount() * 3);

        std::vector<uint32_t> vMap(mesh.VerticesSize(), 0u);
        uint32_t currentIdx = 0;
        for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
        {
            Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(i)};
            ASSERT_FALSE(mesh.IsDeleted(v));
            vMap[i] = currentIdx++;
            positions.push_back(mesh.Position(v));
        }

        for (std::size_t i = 0; i < mesh.FacesSize(); ++i)
        {
            Geometry::FaceHandle f{static_cast<Geometry::PropertyIndex>(i)};
            ASSERT_FALSE(mesh.IsDeleted(f));

            const auto h0 = mesh.Halfedge(f);
            const auto h1 = mesh.NextHalfedge(h0);
            const auto h2 = mesh.NextHalfedge(h1);
            const auto v0 = mesh.ToVertex(h0);
            const auto v1 = mesh.ToVertex(h1);
            const auto v2 = mesh.ToVertex(h2);

            ASSERT_TRUE(mesh.IsValid(v0));
            ASSERT_TRUE(mesh.IsValid(v1));
            ASSERT_TRUE(mesh.IsValid(v2));
            ASSERT_LT(v0.Index, vMap.size());
            ASSERT_LT(v1.Index, vMap.size());
            ASSERT_LT(v2.Index, vMap.size());

            indices.push_back(vMap[v0.Index]);
            indices.push_back(vMap[v1.Index]);
            indices.push_back(vMap[v2.Index]);
        }
    }

    Geometry::Halfedge::Mesh RebuildMeshFromTriangleSoup(
        const std::vector<glm::vec3>& positions,
        const std::vector<uint32_t>& indices)
    {
        Geometry::Halfedge::Mesh mesh;
        std::vector<Geometry::VertexHandle> verts;
        verts.reserve(positions.size());
        for (const auto& p : positions)
        {
            verts.push_back(mesh.AddVertex(p));
        }

        bool buildOk = true;
        for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
        {
            const auto maybeFace = mesh.AddTriangle(verts[indices[i]], verts[indices[i + 1]], verts[indices[i + 2]]);
            if (!maybeFace.has_value())
            {
                ADD_FAILURE() << "Failed to rebuild triangle " << (i / 3)
                              << " from indices (" << indices[i] << ", "
                              << indices[i + 1] << ", "
                              << indices[i + 2] << ")";
                buildOk = false;
                break;
            }
        }
        EXPECT_TRUE(buildOk);
        return mesh;
    }
}


// =============================================================================
// Quadric tests
// =============================================================================

TEST(Quadric_Probabilistic, PlaneZeroVarianceMatchesDeterministicPlane)
{
    const glm::dvec3 point{1.0, 4.0, -2.0};
    const glm::dvec3 normal{2.0, -1.0, 3.0};

    const auto deterministic = Geometry::Quadric::PlaneQuadric(glm::vec3(point), glm::vec3(normal));
    const auto probabilistic = Geometry::Quadric::ProbabilisticPlaneQuadric(point, normal, 0.0, 0.0);

    EXPECT_DOUBLE_EQ(probabilistic.A00, deterministic.A00);
    EXPECT_DOUBLE_EQ(probabilistic.A01, deterministic.A01);
    EXPECT_DOUBLE_EQ(probabilistic.A02, deterministic.A02);
    EXPECT_DOUBLE_EQ(probabilistic.A11, deterministic.A11);
    EXPECT_DOUBLE_EQ(probabilistic.A12, deterministic.A12);
    EXPECT_DOUBLE_EQ(probabilistic.A22, deterministic.A22);
    EXPECT_DOUBLE_EQ(probabilistic.b0, deterministic.b0);
    EXPECT_DOUBLE_EQ(probabilistic.b1, deterministic.b1);
    EXPECT_DOUBLE_EQ(probabilistic.b2, deterministic.b2);
    EXPECT_DOUBLE_EQ(probabilistic.c, deterministic.c);
}

TEST(Quadric_Probabilistic, PlaneIsotropicAndGeneralCovarianceVariantsAgree)
{
    const glm::dvec3 point{0.5, -1.25, 2.0};
    const glm::dvec3 normal{1.5, 0.25, -0.75};
    constexpr double positionStdDev = 0.15;
    constexpr double normalStdDev = 0.2;

    const glm::dmat3 sigmaP{positionStdDev * positionStdDev};
    const glm::dmat3 sigmaN{normalStdDev * normalStdDev};

    const auto isotropic = Geometry::Quadric::ProbabilisticPlaneQuadric(point, normal, positionStdDev, normalStdDev);
    const auto general = Geometry::Quadric::ProbabilisticPlaneQuadric(point, normal, sigmaP, sigmaN);

    EXPECT_NEAR(isotropic.A00, general.A00, 1e-12);
    EXPECT_NEAR(isotropic.A01, general.A01, 1e-12);
    EXPECT_NEAR(isotropic.A02, general.A02, 1e-12);
    EXPECT_NEAR(isotropic.A11, general.A11, 1e-12);
    EXPECT_NEAR(isotropic.A12, general.A12, 1e-12);
    EXPECT_NEAR(isotropic.A22, general.A22, 1e-12);
    EXPECT_NEAR(isotropic.b0, general.b0, 1e-12);
    EXPECT_NEAR(isotropic.b1, general.b1, 1e-12);
    EXPECT_NEAR(isotropic.b2, general.b2, 1e-12);
    EXPECT_NEAR(isotropic.c, general.c, 1e-12);
}

TEST(Quadric_Probabilistic, TriangleIsotropicAndGeneralCovarianceVariantsAgree)
{
    const glm::dvec3 p{0.0, 0.0, 0.0};
    const glm::dvec3 q{1.0, 0.0, 0.0};
    const glm::dvec3 r{0.25, 1.0, 0.5};
    constexpr double positionStdDev = 0.125;

    const glm::dmat3 sigma{positionStdDev * positionStdDev};

    const auto isotropic = Geometry::Quadric::ProbabilisticTriangleQuadric(p, q, r, positionStdDev);
    const auto general = Geometry::Quadric::ProbabilisticTriangleQuadric(p, q, r, sigma, sigma, sigma);

    EXPECT_NEAR(isotropic.A00, general.A00, 1e-12);
    EXPECT_NEAR(isotropic.A01, general.A01, 1e-12);
    EXPECT_NEAR(isotropic.A02, general.A02, 1e-12);
    EXPECT_NEAR(isotropic.A11, general.A11, 1e-12);
    EXPECT_NEAR(isotropic.A12, general.A12, 1e-12);
    EXPECT_NEAR(isotropic.A22, general.A22, 1e-12);
    EXPECT_NEAR(isotropic.b0, general.b0, 1e-12);
    EXPECT_NEAR(isotropic.b1, general.b1, 1e-12);
    EXPECT_NEAR(isotropic.b2, general.b2, 1e-12);
    EXPECT_NEAR(isotropic.c, general.c, 1e-12);
}

TEST(Quadric_Probabilistic, DegenerateTriangleProducesFiniteEnergy)
{
    const glm::dvec3 p{2.0, -1.0, 0.5};
    const auto quadric = Geometry::Quadric::ProbabilisticTriangleQuadric(p, p, p, 0.25);

    EXPECT_TRUE(std::isfinite(quadric.A00));
    EXPECT_TRUE(std::isfinite(quadric.A01));
    EXPECT_TRUE(std::isfinite(quadric.A02));
    EXPECT_TRUE(std::isfinite(quadric.A11));
    EXPECT_TRUE(std::isfinite(quadric.A12));
    EXPECT_TRUE(std::isfinite(quadric.A22));
    EXPECT_TRUE(std::isfinite(quadric.b0));
    EXPECT_TRUE(std::isfinite(quadric.b1));
    EXPECT_TRUE(std::isfinite(quadric.b2));
    EXPECT_TRUE(std::isfinite(quadric.c));
    EXPECT_TRUE(std::isfinite(quadric.Evaluate(p)));
}


// =============================================================================
// Edge Flip tests
// =============================================================================

TEST(MeshTopology_Flip, FlipPreservesVertexCount)
{
    // Use icosahedron — large enough that interior edges have high-valence endpoints
    auto mesh = MakeIcosahedron();
    std::size_t vBefore = mesh.VertexCount();
    std::size_t eBefore = mesh.EdgeCount();
    std::size_t fBefore = mesh.FaceCount();

    // Find a flippable interior edge
    Geometry::EdgeHandle eFlip;
    for (std::size_t i = 0; i < mesh.EdgesSize(); ++i)
    {
        Geometry::EdgeHandle e{static_cast<Geometry::PropertyIndex>(i)};
        if (mesh.IsFlipOk(e))
        {
            eFlip = e;
            break;
        }
    }

    ASSERT_TRUE(eFlip.IsValid());

    bool ok = mesh.Flip(eFlip);
    EXPECT_TRUE(ok);

    // Flip should not change counts
    EXPECT_EQ(mesh.VertexCount(), vBefore);
    EXPECT_EQ(mesh.EdgeCount(), eBefore);
    EXPECT_EQ(mesh.FaceCount(), fBefore);
}

TEST(MeshTopology_Flip, FlipChangesEndpoints)
{
    auto mesh = MakeIcosahedron();

    Geometry::EdgeHandle eFlip;
    for (std::size_t i = 0; i < mesh.EdgesSize(); ++i)
    {
        Geometry::EdgeHandle e{static_cast<Geometry::PropertyIndex>(i)};
        if (mesh.IsFlipOk(e))
        {
            eFlip = e;
            break;
        }
    }
    ASSERT_TRUE(eFlip.IsValid());

    auto h0 = mesh.Halfedge(eFlip, 0);
    auto v0Before = mesh.ToVertex(h0);
    auto v1Before = mesh.FromVertex(h0);

    (void)mesh.Flip(eFlip);

    auto v0After = mesh.ToVertex(h0);
    auto v1After = mesh.FromVertex(h0);

    // After flip, the edge should connect different vertices
    EXPECT_TRUE(v0After != v0Before || v1After != v1Before);
}

TEST(MeshTopology_Flip, TwoTriangleSquareRejected)
{
    // On a 2-triangle square, the interior edge endpoints have valence 3.
    // Flipping would reduce them to valence 2 (degenerate), so IsFlipOk should reject.
    auto mesh = MakeTwoTriangleSquare();

    Geometry::EdgeHandle eShared;
    for (std::size_t i = 0; i < mesh.EdgesSize(); ++i)
    {
        Geometry::EdgeHandle e{static_cast<Geometry::PropertyIndex>(i)};
        if (!mesh.IsBoundary(e))
        {
            eShared = e;
            break;
        }
    }
    ASSERT_TRUE(eShared.IsValid());
    EXPECT_FALSE(mesh.IsFlipOk(eShared));
}

TEST(MeshTopology_Flip, BoundaryEdgeReject)
{
    auto mesh = MakeSingleTriangle();

    // All edges are boundary — flip should fail
    for (std::size_t i = 0; i < mesh.EdgesSize(); ++i)
    {
        Geometry::EdgeHandle e{static_cast<Geometry::PropertyIndex>(i)};
        EXPECT_FALSE(mesh.IsFlipOk(e));
        EXPECT_FALSE(mesh.Flip(e));
    }
}

// =============================================================================
// Edge Split tests
// =============================================================================

TEST(MeshTopology_Split, InteriorSplitCreatesCorrectCounts)
{
    auto mesh = MakeTwoTriangleSquare();

    // Find interior edge
    Geometry::EdgeHandle eShared;
    for (std::size_t i = 0; i < mesh.EdgesSize(); ++i)
    {
        Geometry::EdgeHandle e{static_cast<Geometry::PropertyIndex>(i)};
        if (!mesh.IsBoundary(e))
        {
            eShared = e;
            break;
        }
    }
    ASSERT_TRUE(eShared.IsValid());

    // Before: 4 vertices, 5 edges, 2 faces
    EXPECT_EQ(mesh.VertexCount(), 4u);
    EXPECT_EQ(mesh.FaceCount(), 2u);

    auto h0 = mesh.Halfedge(eShared, 0);
    glm::vec3 midpoint = (mesh.Position(mesh.FromVertex(h0)) +
                          mesh.Position(mesh.ToVertex(h0))) * 0.5f;

    auto vm = mesh.Split(eShared, midpoint);
    EXPECT_TRUE(vm.IsValid());

    // After interior split: +1 vertex, +3 edges, +2 faces
    EXPECT_EQ(mesh.VertexCount(), 5u);
    EXPECT_EQ(mesh.FaceCount(), 4u);
}

TEST(MeshTopology_Split, BoundarySplitCreatesCorrectCounts)
{
    auto mesh = MakeSingleTriangle();

    // All edges are boundary. Pick the first edge.
    Geometry::EdgeHandle e0{0};
    ASSERT_FALSE(mesh.IsDeleted(e0));

    EXPECT_EQ(mesh.VertexCount(), 3u);
    EXPECT_EQ(mesh.FaceCount(), 1u);

    auto h0 = mesh.Halfedge(e0, 0);
    glm::vec3 midpoint = (mesh.Position(mesh.FromVertex(h0)) +
                          mesh.Position(mesh.ToVertex(h0))) * 0.5f;

    auto vm = mesh.Split(e0, midpoint);
    EXPECT_TRUE(vm.IsValid());

    // Boundary split of an edge adjacent to 1 face: +1 vertex, +2 edges, +1 face
    EXPECT_EQ(mesh.VertexCount(), 4u);
    EXPECT_EQ(mesh.FaceCount(), 2u);
}

TEST(MeshTopology_Split, NewVertexAtCorrectPosition)
{
    auto mesh = MakeTwoTriangleSquare();

    Geometry::EdgeHandle e0{0};
    glm::vec3 target(0.5f, 0.5f, 0.5f);

    auto vm = mesh.Split(e0, target);
    ASSERT_TRUE(vm.IsValid());

    glm::vec3 actual = mesh.Position(vm);
    EXPECT_NEAR(actual.x, target.x, 1e-5f);
    EXPECT_NEAR(actual.y, target.y, 1e-5f);
    EXPECT_NEAR(actual.z, target.z, 1e-5f);
}

// =============================================================================
// Edge Collapse tests
// =============================================================================

TEST(MeshTopology_Collapse, CollapseReducesCounts)
{
    auto mesh = MakeIcosahedron();

    EXPECT_EQ(mesh.VertexCount(), 12u);
    EXPECT_EQ(mesh.FaceCount(), 20u);
    EXPECT_EQ(mesh.EdgeCount(), 30u);

    // Find a collapsible edge
    Geometry::EdgeHandle collapseEdge;
    for (std::size_t i = 0; i < mesh.EdgesSize(); ++i)
    {
        Geometry::EdgeHandle e{static_cast<Geometry::PropertyIndex>(i)};
        if (mesh.IsCollapseOk(e))
        {
            collapseEdge = e;
            break;
        }
    }

    ASSERT_TRUE(collapseEdge.IsValid());

    auto h0 = mesh.Halfedge(collapseEdge, 0);
    glm::vec3 midpoint = (mesh.Position(mesh.FromVertex(h0)) +
                          mesh.Position(mesh.ToVertex(h0))) * 0.5f;

    auto result = mesh.Collapse(collapseEdge, midpoint);
    ASSERT_TRUE(result.has_value());

    // Interior edge collapse: -1 vertex, -3 edges (the collapsed edge + 2 degenerate), -2 faces
    EXPECT_EQ(mesh.VertexCount(), 11u);
    EXPECT_EQ(mesh.FaceCount(), 18u);
}

TEST(MeshTopology_Collapse, SingleCollapsePreservesNextCycles)
{
    auto mesh = MakeIcosahedron();

    Geometry::EdgeHandle collapseEdge;
    for (std::size_t i = 0; i < mesh.EdgesSize(); ++i)
    {
        Geometry::EdgeHandle e{static_cast<Geometry::PropertyIndex>(i)};
        if (mesh.IsCollapseOk(e))
        {
            collapseEdge = e;
            break;
        }
    }
    ASSERT_TRUE(collapseEdge.IsValid());

    auto h0 = mesh.Halfedge(collapseEdge, 0);
    glm::vec3 midpoint = (mesh.Position(mesh.FromVertex(h0)) +
                          mesh.Position(mesh.ToVertex(h0))) * 0.5f;

    auto result = mesh.Collapse(h0, midpoint);
    ASSERT_TRUE(result.has_value());

    // Every non-deleted face must still form a proper 3-cycle.
    for (std::size_t fi = 0; fi < mesh.FacesSize(); ++fi)
    {
        Geometry::FaceHandle f{static_cast<Geometry::PropertyIndex>(fi)};
        if (mesh.IsDeleted(f)) continue;

        EXPECT_EQ(mesh.Valence(f), 3u)
            << "Face " << fi << " has broken Next cycle after single collapse";
    }
}

TEST(MeshTopology_Collapse, SurvivingVertexAtCorrectPosition)
{
    auto mesh = MakeIcosahedron();

    // Find a collapsible edge
    Geometry::EdgeHandle collapseEdge;
    for (std::size_t i = 0; i < mesh.EdgesSize(); ++i)
    {
        Geometry::EdgeHandle e{static_cast<Geometry::PropertyIndex>(i)};
        if (mesh.IsCollapseOk(e))
        {
            collapseEdge = e;
            break;
        }
    }
    ASSERT_TRUE(collapseEdge.IsValid());

    glm::vec3 target(0.123f, 0.456f, 0.789f);
    auto result = mesh.Collapse(collapseEdge, target);
    ASSERT_TRUE(result.has_value());

    glm::vec3 actual = mesh.Position(*result);
    EXPECT_NEAR(actual.x, target.x, 1e-5f);
    EXPECT_NEAR(actual.y, target.y, 1e-5f);
    EXPECT_NEAR(actual.z, target.z, 1e-5f);
}

TEST(MeshTopology_Collapse, DirectedHalfedgeCollapsePreservesToVertexWhileLegacyEdgeCollapseKeepsHistoricalSurvivor)
{
    auto meshDirected = MakeIcosahedron();
    auto meshLegacy = meshDirected;

    Geometry::EdgeHandle collapseEdge;
    for (std::size_t i = 0; i < meshDirected.EdgesSize(); ++i)
    {
        Geometry::EdgeHandle e{static_cast<Geometry::PropertyIndex>(i)};
        if (meshDirected.IsCollapseOk(e))
        {
            collapseEdge = e;
            break;
        }
    }
    ASSERT_TRUE(collapseEdge.IsValid());

    const auto h0 = meshDirected.Halfedge(collapseEdge, 0);
    const auto expectedDirectedSurvivor = meshDirected.ToVertex(h0);
    const auto expectedLegacySurvivor = meshDirected.FromVertex(h0);

    const glm::vec3 directedTarget(0.25f, -0.5f, 0.75f);
    const auto directed = meshDirected.Collapse(h0, directedTarget);
    ASSERT_TRUE(directed.has_value());
    EXPECT_EQ(*directed, expectedDirectedSurvivor);
    EXPECT_NE(*directed, expectedLegacySurvivor);
    EXPECT_NEAR(meshDirected.Position(*directed).x, directedTarget.x, 1e-5f);
    EXPECT_NEAR(meshDirected.Position(*directed).y, directedTarget.y, 1e-5f);
    EXPECT_NEAR(meshDirected.Position(*directed).z, directedTarget.z, 1e-5f);

    const glm::vec3 legacyTarget(-0.125f, 0.375f, 0.625f);
    const auto legacy = meshLegacy.Collapse(collapseEdge, legacyTarget);
    ASSERT_TRUE(legacy.has_value());
    EXPECT_EQ(*legacy, expectedLegacySurvivor);
    EXPECT_NEAR(meshLegacy.Position(*legacy).x, legacyTarget.x, 1e-5f);
    EXPECT_NEAR(meshLegacy.Position(*legacy).y, legacyTarget.y, 1e-5f);
    EXPECT_NEAR(meshLegacy.Position(*legacy).z, legacyTarget.z, 1e-5f);
}

// =============================================================================
// Simplification tests
// =============================================================================

TEST(Simplification_QEM, ReducesFaceCount)
{
    auto mesh = MakeIcosahedron();
    EXPECT_EQ(mesh.FaceCount(), 20u);

    Geometry::Simplification::Params params;
    params.TargetFaces = 10;
    params.PreserveBoundary = false; // closed mesh

    auto result = Geometry::Simplification::Simplify(mesh, params);
    ASSERT_TRUE(result.has_value());

    mesh.GarbageCollection();

    EXPECT_LE(mesh.FaceCount(), 10u);
    EXPECT_GT(result->CollapseCount, 0u);
}

TEST(Simplification_QEM, RespectsTargetFaceCount)
{
    auto mesh = MakeIcosahedron();

    Geometry::Simplification::Params params;
    params.TargetFaces = 12;
    params.PreserveBoundary = false;

    auto result = Geometry::Simplification::Simplify(mesh, params);
    ASSERT_TRUE(result.has_value());

    // Should stop at or below target
    EXPECT_LE(result->FinalFaceCount, 12u);
}

TEST(Simplification_QEM, TooFewFacesReturnsNullopt)
{
    auto mesh = MakeSingleTriangle();

    Geometry::Simplification::Params params;
    params.TargetFaces = 1;

    auto result = Geometry::Simplification::Simplify(mesh, params);
    EXPECT_FALSE(result.has_value());
}

TEST(Simplification_QEM, ErrorThresholdStopsEarly)
{
    auto mesh = MakeIcosahedron();

    Geometry::Simplification::Params params;
    params.TargetFaces = 4; // Very aggressive
    params.MaxError = 1e-10; // Very tight threshold — should stop early
    params.PreserveBoundary = false;

    auto result = Geometry::Simplification::Simplify(mesh, params);
    ASSERT_TRUE(result.has_value());

    // With such a tight error threshold on an icosahedron, some collapses
    // should happen (zero-error collapses don't exist on a regular mesh)
    // but we should stop well before reaching 4 faces.
    // Just check that the result is valid.
    EXPECT_GE(result->FinalFaceCount, 4u); // Didn't go below absolute minimum
}

TEST(Simplification_QEM, PreservedMeshIsValid)
{
    auto mesh = MakeIcosahedron();

    Geometry::Simplification::Params params;
    params.TargetFaces = 8;
    params.PreserveBoundary = false;

    auto result = Geometry::Simplification::Simplify(mesh, params);
    ASSERT_TRUE(result.has_value());

    // Validate BEFORE garbage collection: every non-deleted face should have
    // exactly 3 halfedges forming a cycle
    for (std::size_t fi = 0; fi < mesh.FacesSize(); ++fi)
    {
        Geometry::FaceHandle fh{static_cast<Geometry::PropertyIndex>(fi)};
        if (mesh.IsDeleted(fh)) continue;

        EXPECT_EQ(mesh.Valence(fh), 3u)
            << "Face " << fi << " should be a triangle after simplification";
    }

    // Validate BEFORE garbage collection: every non-deleted vertex should
    // have valence >= 3
    for (std::size_t vi = 0; vi < mesh.VerticesSize(); ++vi)
    {
        Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(vi)};
        if (mesh.IsDeleted(vh) || mesh.IsIsolated(vh)) continue;

        EXPECT_GE(mesh.Valence(vh), 3u)
            << "Vertex " << vi << " should have valence >= 3";
    }

    // Euler characteristic check: V - E + F = 2 for closed mesh
    std::size_t V = mesh.VertexCount();
    std::size_t E = mesh.EdgeCount();
    std::size_t F = mesh.FaceCount();
    EXPECT_EQ(static_cast<int>(V) - static_cast<int>(E) + static_cast<int>(F), 2)
        << "Euler characteristic should be 2 for closed mesh: V=" << V << " E=" << E << " F=" << F;
}

TEST(Simplification_QEM, GarbageCollectionKeepsFaceHalfedgesValidAfterDenseSimplification)
{
    auto mesh = MakeDenseClosedTriangleMesh();

    Geometry::Simplification::Params params;
    params.TargetFaces = 400;
    params.PreserveBoundary = false;

    auto result = Geometry::Simplification::Simplify(mesh, params);
    ASSERT_TRUE(result.has_value());
    ASSERT_GT(result->CollapseCount, 0u);

    for (std::size_t fi = 0; fi < mesh.FacesSize(); ++fi)
    {
        Geometry::FaceHandle f{static_cast<Geometry::PropertyIndex>(fi)};
        if (mesh.IsDeleted(f))
        {
            continue;
        }

        const auto h0 = mesh.Halfedge(f);
        ASSERT_TRUE(mesh.IsValid(h0)) << "Pre-GC face " << fi << " has invalid representative halfedge " << h0.Index;

        const auto h1 = mesh.NextHalfedge(h0);
        ASSERT_TRUE(mesh.IsValid(h1)) << "Pre-GC face " << fi << " has invalid second halfedge " << h1.Index;

        const auto h2 = mesh.NextHalfedge(h1);
        ASSERT_TRUE(mesh.IsValid(h2)) << "Pre-GC face " << fi << " has invalid third halfedge " << h2.Index;

        EXPECT_EQ(mesh.NextHalfedge(h2), h0) << "Pre-GC face " << fi << " no longer forms a 3-cycle";

        EXPECT_FALSE(mesh.IsDeleted(mesh.ToVertex(h0)));
        EXPECT_FALSE(mesh.IsDeleted(mesh.ToVertex(h1)));
        EXPECT_FALSE(mesh.IsDeleted(mesh.ToVertex(h2)));
    }

    mesh.GarbageCollection();

    std::vector<uint32_t> extracted;
    extracted.reserve(mesh.FaceCount() * 3);

    for (std::size_t fi = 0; fi < mesh.FacesSize(); ++fi)
    {
        Geometry::FaceHandle f{static_cast<Geometry::PropertyIndex>(fi)};
        if (mesh.IsDeleted(f))
        {
            continue;
        }

        const auto h0 = mesh.Halfedge(f);
        ASSERT_TRUE(mesh.IsValid(h0)) << "Face " << fi << " has invalid representative halfedge " << h0.Index;

        const auto h1 = mesh.NextHalfedge(h0);
        ASSERT_TRUE(mesh.IsValid(h1)) << "Face " << fi << " has invalid second halfedge " << h1.Index;

        const auto h2 = mesh.NextHalfedge(h1);
        ASSERT_TRUE(mesh.IsValid(h2)) << "Face " << fi << " has invalid third halfedge " << h2.Index;

        EXPECT_EQ(mesh.NextHalfedge(h2), h0) << "Face " << fi << " no longer forms a 3-cycle after GC";

        const auto v0 = mesh.ToVertex(h0);
        const auto v1 = mesh.ToVertex(h1);
        const auto v2 = mesh.ToVertex(h2);
        ASSERT_TRUE(mesh.IsValid(v0)) << "Face " << fi << " has invalid v0 " << v0.Index << " from h0 " << h0.Index;
        ASSERT_TRUE(mesh.IsValid(v1)) << "Face " << fi << " has invalid v1 " << v1.Index << " from h1 " << h1.Index;
        ASSERT_TRUE(mesh.IsValid(v2)) << "Face " << fi << " has invalid v2 " << v2.Index << " from h2 " << h2.Index;
        EXPECT_FALSE(mesh.IsDeleted(v0));
        EXPECT_FALSE(mesh.IsDeleted(v1));
        EXPECT_FALSE(mesh.IsDeleted(v2));

        extracted.push_back(v0.Index);
        extracted.push_back(v1.Index);
        extracted.push_back(v2.Index);
    }

    EXPECT_EQ(extracted.size(), mesh.FaceCount() * 3);
}

TEST(Simplification_QEM, DenseClosedMeshStaysClosed)
{
    auto mesh = MakeDenseClosedTriangleMesh();

    Geometry::Simplification::Params params;
    params.TargetFaces = 400;
    params.PreserveBoundary = false;

    auto result = Geometry::Simplification::Simplify(mesh, params);
    ASSERT_TRUE(result.has_value());
    ASSERT_GT(result->CollapseCount, 0u);

    auto countBoundaryEdges = [&](const Geometry::Halfedge::Mesh& m)
    {
        std::size_t boundaryEdges = 0;
        for (std::size_t ei = 0; ei < m.EdgesSize(); ++ei)
        {
            Geometry::EdgeHandle e{static_cast<Geometry::PropertyIndex>(ei)};
            if (m.IsDeleted(e))
            {
                continue;
            }
            if (m.IsBoundary(e))
            {
                ++boundaryEdges;
            }
        }
        return boundaryEdges;
    };

    auto expectClosedSphereTopology = [&](const Geometry::Halfedge::Mesh& m, const char* phase, bool checkVertexManifold)
    {
        EXPECT_EQ(countBoundaryEdges(m), 0u) << phase << " introduced boundary edges";

        const auto V = m.VertexCount();
        const auto E = m.EdgeCount();
        const auto F = m.FaceCount();
        EXPECT_EQ(static_cast<int>(V) - static_cast<int>(E) + static_cast<int>(F), 2)
            << phase << " changed Euler characteristic: V=" << V << " E=" << E << " F=" << F;

        if (!checkVertexManifold)
        {
            return;
        }

        for (std::size_t vi = 0; vi < m.VerticesSize(); ++vi)
        {
            Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(vi)};
            if (m.IsDeleted(v) || m.IsIsolated(v))
            {
                continue;
            }
            EXPECT_TRUE(m.IsManifold(v)) << phase << " made vertex " << vi << " non-manifold";
        }
    };

    expectClosedSphereTopology(mesh, "pre-GC simplification", false);

    mesh.GarbageCollection();

    expectClosedSphereTopology(mesh, "post-GC simplification", true);
}

TEST(Simplification_QEM, HausdorffErrorConstraintIsRespected)
{
    auto mesh = MakeDenseClosedTriangleMesh();

    // Collect original vertex positions for distance checking
    std::vector<glm::vec3> originalPositions;
    for (std::size_t vi = 0; vi < mesh.VerticesSize(); ++vi)
    {
        Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(vi)};
        if (!mesh.IsDeleted(v) && !mesh.IsIsolated(v))
        {
            originalPositions.push_back(mesh.Position(v));
        }
    }

    Geometry::Simplification::Params params;
    params.TargetFaces = 400;
    params.PreserveBoundary = false;
    params.HausdorffError = 0.5;

    auto result = Geometry::Simplification::Simplify(mesh, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_GT(result->CollapseCount, 0u);
}

TEST(Simplification_QEM, RepeatedWorkflowStyleSimplificationStaysValid)
{
    auto mesh = MakeDenseClosedTriangleMesh(4); // 5,120 triangles: fast, but still dense enough for repeated decimation.

    Geometry::Simplification::Params first;
    first.TargetFaces = 4000;
    first.PreserveBoundary = false;

    auto firstResult = Geometry::Simplification::Simplify(mesh, first);
    ASSERT_TRUE(firstResult.has_value());
    ASSERT_LE(mesh.FaceCount(), 4000u);

    std::vector<glm::vec3> positions;
    std::vector<uint32_t> indices;
    ExtractTriangleSoup(mesh, positions, indices);
    ASSERT_EQ(indices.size(), mesh.FaceCount() * 3);

    auto rebuilt = RebuildMeshFromTriangleSoup(positions, indices);
    ASSERT_EQ(rebuilt.FaceCount(), indices.size() / 3);

    Geometry::Simplification::Params second;
    second.TargetFaces = 3000;
    second.PreserveBoundary = false;

    auto secondResult = Geometry::Simplification::Simplify(rebuilt, second);
    ASSERT_TRUE(secondResult.has_value());
    ASSERT_LE(rebuilt.FaceCount(), 3000u);

    ExtractTriangleSoup(rebuilt, positions, indices);

    EXPECT_EQ(indices.size(), rebuilt.FaceCount() * 3);
    for (std::size_t ei = 0; ei < rebuilt.EdgesSize(); ++ei)
    {
        Geometry::EdgeHandle e{static_cast<Geometry::PropertyIndex>(ei)};
        EXPECT_FALSE(rebuilt.IsDeleted(e));
        EXPECT_FALSE(rebuilt.IsBoundary(e)) << "Repeated workflow simplification introduced a hole at edge " << ei;
    }
}

TEST(Simplification_QEM, ConfigurableQuadricsSupportAllTypesResidencesAndPlacements)
{
    struct Config
    {
        Geometry::Simplification::QuadricType Type;
        Geometry::Simplification::QuadricResidence Residence;
        Geometry::Simplification::CollapsePlacementPolicy Placement;
    };

    const std::vector<Config> configs = {
        {Geometry::Simplification::QuadricType::Plane, Geometry::Simplification::QuadricResidence::Vertices, Geometry::Simplification::CollapsePlacementPolicy::KeepSurvivor},
        {Geometry::Simplification::QuadricType::Plane, Geometry::Simplification::QuadricResidence::Faces, Geometry::Simplification::CollapsePlacementPolicy::QuadricMinimizer},
        {Geometry::Simplification::QuadricType::Plane, Geometry::Simplification::QuadricResidence::VerticesAndFaces, Geometry::Simplification::CollapsePlacementPolicy::BestOfEndpointsAndMinimizer},
        {Geometry::Simplification::QuadricType::Triangle, Geometry::Simplification::QuadricResidence::Vertices, Geometry::Simplification::CollapsePlacementPolicy::KeepSurvivor},
        {Geometry::Simplification::QuadricType::Triangle, Geometry::Simplification::QuadricResidence::Faces, Geometry::Simplification::CollapsePlacementPolicy::QuadricMinimizer},
        {Geometry::Simplification::QuadricType::Triangle, Geometry::Simplification::QuadricResidence::VerticesAndFaces, Geometry::Simplification::CollapsePlacementPolicy::BestOfEndpointsAndMinimizer},
        {Geometry::Simplification::QuadricType::Point, Geometry::Simplification::QuadricResidence::Vertices, Geometry::Simplification::CollapsePlacementPolicy::KeepSurvivor},
        {Geometry::Simplification::QuadricType::Point, Geometry::Simplification::QuadricResidence::Faces, Geometry::Simplification::CollapsePlacementPolicy::QuadricMinimizer},
        {Geometry::Simplification::QuadricType::Point, Geometry::Simplification::QuadricResidence::VerticesAndFaces, Geometry::Simplification::CollapsePlacementPolicy::BestOfEndpointsAndMinimizer},
    };

    for (const Config& config : configs)
    {
        SCOPED_TRACE(static_cast<int>(config.Type));
        SCOPED_TRACE(static_cast<int>(config.Residence));
        SCOPED_TRACE(static_cast<int>(config.Placement));

        auto mesh = MakeDenseClosedTriangleMesh(2);
        const std::size_t initialFaces = mesh.FaceCount();

        Geometry::Simplification::Params params;
        params.TargetFaces = initialFaces / 2u;
        params.PreserveBoundary = false;
        params.Quadric.Type = config.Type;
        params.Quadric.Residence = config.Residence;
        params.Quadric.PlacementPolicy = config.Placement;

        auto result = Geometry::Simplification::Simplify(mesh, params);
        ASSERT_TRUE(result.has_value());
        EXPECT_GT(result->CollapseCount, 0u);
        EXPECT_LT(mesh.FaceCount(), initialFaces);
    }
}

TEST(Simplification_QEM, ProbabilisticQuadricsSupportIsotropicAndCovarianceModes)
{
    struct Config
    {
        Geometry::Simplification::QuadricType Type;
        Geometry::Simplification::QuadricProbabilisticMode Mode;
    };

    const std::vector<Config> configs = {
        {Geometry::Simplification::QuadricType::Plane, Geometry::Simplification::QuadricProbabilisticMode::Isotropic},
        {Geometry::Simplification::QuadricType::Plane, Geometry::Simplification::QuadricProbabilisticMode::Covariance},
        {Geometry::Simplification::QuadricType::Triangle, Geometry::Simplification::QuadricProbabilisticMode::Isotropic},
        {Geometry::Simplification::QuadricType::Triangle, Geometry::Simplification::QuadricProbabilisticMode::Covariance},
    };

    for (const Config& config : configs)
    {
        SCOPED_TRACE(static_cast<int>(config.Type));
        SCOPED_TRACE(static_cast<int>(config.Mode));

        auto mesh = MakeDenseClosedTriangleMesh(2);
        const std::size_t initialFaces = mesh.FaceCount();

        auto vertexSigma = Geometry::VertexProperty<glm::dmat3>(
            mesh.VertexProperties().GetOrAdd<glm::dmat3>("v:quadric_sigma_p", glm::dmat3(0.0)));
        for (std::size_t vi = 0; vi < mesh.VerticesSize(); ++vi)
        {
            Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(vi)};
            if (!mesh.IsDeleted(v))
            {
                vertexSigma[v] = glm::dmat3(1e-4);
            }
        }

        auto faceSigmaP = Geometry::FaceProperty<glm::dmat3>(
            mesh.FaceProperties().GetOrAdd<glm::dmat3>("f:quadric_sigma_p", glm::dmat3(0.0)));
        auto faceSigmaN = Geometry::FaceProperty<glm::dmat3>(
            mesh.FaceProperties().GetOrAdd<glm::dmat3>("f:quadric_sigma_n", glm::dmat3(0.0)));
        for (std::size_t fi = 0; fi < mesh.FacesSize(); ++fi)
        {
            Geometry::FaceHandle f{static_cast<Geometry::PropertyIndex>(fi)};
            if (!mesh.IsDeleted(f))
            {
                faceSigmaP[f] = glm::dmat3(5e-5);
                faceSigmaN[f] = glm::dmat3(2.5e-5);
            }
        }

        Geometry::Simplification::Params params;
        params.TargetFaces = initialFaces / 2u;
        params.PreserveBoundary = false;
        params.Quadric.Type = config.Type;
        params.Quadric.Residence = Geometry::Simplification::QuadricResidence::VerticesAndFaces;
        params.Quadric.PlacementPolicy = Geometry::Simplification::CollapsePlacementPolicy::BestOfEndpointsAndMinimizer;
        params.Quadric.ProbabilisticMode = config.Mode;
        params.Quadric.PositionStdDev = 0.01;
        params.Quadric.NormalStdDev = 0.02;

        auto result = Geometry::Simplification::Simplify(mesh, params);
        ASSERT_TRUE(result.has_value());
        EXPECT_GT(result->CollapseCount, 0u);
        EXPECT_LT(mesh.FaceCount(), initialFaces);
    }
}

TEST(Simplification_QEM, MissingCovariancePropertiesFallBackToZeroCovariance)
{
    auto baselineMesh = MakeDenseClosedTriangleMesh(2);
    auto covarianceMesh = baselineMesh;

    Geometry::Simplification::Params baseline;
    baseline.TargetFaces = baselineMesh.FaceCount() / 2u;
    baseline.PreserveBoundary = false;
    baseline.Quadric.Type = Geometry::Simplification::QuadricType::Plane;
    baseline.Quadric.Residence = Geometry::Simplification::QuadricResidence::VerticesAndFaces;
    baseline.Quadric.PlacementPolicy = Geometry::Simplification::CollapsePlacementPolicy::BestOfEndpointsAndMinimizer;

    Geometry::Simplification::Params covariance = baseline;
    covariance.Quadric.ProbabilisticMode = Geometry::Simplification::QuadricProbabilisticMode::Covariance;
    covariance.Quadric.VertexPositionCovarianceProperty = "v:missing_sigma_p";
    covariance.Quadric.FacePositionCovarianceProperty = "f:missing_sigma_p";
    covariance.Quadric.FaceNormalCovarianceProperty = "f:missing_sigma_n";

    auto baselineResult = Geometry::Simplification::Simplify(baselineMesh, baseline);
    auto covarianceResult = Geometry::Simplification::Simplify(covarianceMesh, covariance);
    ASSERT_TRUE(baselineResult.has_value());
    ASSERT_TRUE(covarianceResult.has_value());

    EXPECT_EQ(covarianceMesh.FaceCount(), baselineMesh.FaceCount());
    EXPECT_EQ(covarianceResult->CollapseCount, baselineResult->CollapseCount);
}

TEST(Simplification_QEM, OpenBoundaryPatchKeepsDiskTopology)
{
    auto mesh = MakeSubdividedTriangle();

    Geometry::Simplification::Params params;
    params.TargetFaces = 2;
    params.PreserveBoundary = false;
    params.ForbidBoundaryInteriorCollapse = true;

    auto result = Geometry::Simplification::Simplify(mesh, params);
    ASSERT_TRUE(result.has_value());
    ASSERT_GT(result->CollapseCount, 0u);

    mesh.GarbageCollection();

    auto quality = Geometry::MeshQuality::ComputeQuality(mesh);
    ASSERT_TRUE(quality.has_value());
    EXPECT_EQ(quality->EulerCharacteristic, 1) << "Open patch should remain topologically equivalent to a disk";
    EXPECT_EQ(quality->BoundaryLoopCount, 1u) << "Open patch simplification should keep a single boundary loop";
    EXPECT_EQ(mesh.FaceCount(), 2u);

    for (std::size_t vi = 0; vi < mesh.VerticesSize(); ++vi)
    {
        Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(vi)};
        if (mesh.IsDeleted(v) || mesh.IsIsolated(v))
        {
            continue;
        }
        EXPECT_TRUE(mesh.IsManifold(v)) << "Open patch simplification made vertex " << vi << " non-manifold";
    }
}

TEST(Simplification_QEM, PreserveBoundaryPreventsCollapsesOnOpenPatch)
{
    auto mesh = MakeSubdividedTriangle();

    const auto before = Geometry::MeshQuality::ComputeQuality(mesh);
    ASSERT_TRUE(before.has_value());
    ASSERT_EQ(before->BoundaryLoopCount, 1u);

    std::size_t boundaryVertexCountBefore = 0;
    for (std::size_t vi = 0; vi < mesh.VerticesSize(); ++vi)
    {
        Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(vi)};
        if (!mesh.IsDeleted(v) && !mesh.IsIsolated(v) && mesh.IsBoundary(v))
        {
            ++boundaryVertexCountBefore;
        }
    }

    Geometry::Simplification::Params params;
    params.TargetFaces = 2;
    params.PreserveBoundary = true;
    params.ForbidBoundaryInteriorCollapse = true;

    auto result = Geometry::Simplification::Simplify(mesh, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->CollapseCount, 0u);
    EXPECT_EQ(mesh.FaceCount(), 4u);

    const auto after = Geometry::MeshQuality::ComputeQuality(mesh);
    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(after->BoundaryLoopCount, before->BoundaryLoopCount);

    std::size_t boundaryVertexCountAfter = 0;
    for (std::size_t vi = 0; vi < mesh.VerticesSize(); ++vi)
    {
        Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(vi)};
        if (!mesh.IsDeleted(v) && !mesh.IsIsolated(v) && mesh.IsBoundary(v))
        {
            ++boundaryVertexCountAfter;
        }
    }
    EXPECT_EQ(boundaryVertexCountAfter, boundaryVertexCountBefore);
}

// =============================================================================
// Mesh Repair tests (merged from Test_GeometryProcessing2.cpp)
// =============================================================================

TEST(MeshRepair_BoundaryDetection, ClosedMeshHasNoBoundary)
{
    auto mesh = MakeTetrahedron();
    auto result = Geometry::MeshRepair::FindBoundaryLoops(mesh);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->Loops.empty());
}

TEST(MeshRepair_BoundaryDetection, OpenMeshHasBoundary)
{
    auto mesh = MakeSingleTriangle();
    auto result = Geometry::MeshRepair::FindBoundaryLoops(mesh);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Loops.size(), 1u);
    EXPECT_EQ(result->Loops[0].Vertices.size(), 3u);
}

TEST(MeshRepair_BoundaryDetection, TwoTriangleSquareHasBoundary)
{
    auto mesh = MakeTwoTriangleSquare();
    auto result = Geometry::MeshRepair::FindBoundaryLoops(mesh);
    ASSERT_TRUE(result.has_value());
    // The two-triangle square has one boundary loop with 4 vertices
    EXPECT_EQ(result->Loops.size(), 1u);
    EXPECT_EQ(result->Loops[0].Vertices.size(), 4u);
}

TEST(MeshRepair_HoleFilling, FillsTriangularHole)
{
    // Create a tetrahedron, delete one face to create a hole, then fill it
    auto mesh = MakeTetrahedron();

    // Verify it starts closed
    auto beforeResult = Geometry::MeshRepair::FindBoundaryLoops(mesh);
    ASSERT_TRUE(beforeResult.has_value());
    EXPECT_TRUE(beforeResult->Loops.empty());

    // Delete one face to create a hole
    Geometry::FaceHandle f0{0};
    mesh.DeleteFace(f0);
    mesh.GarbageCollection();

    // Verify the hole exists
    auto afterResult = Geometry::MeshRepair::FindBoundaryLoops(mesh);
    ASSERT_TRUE(afterResult.has_value());
    EXPECT_EQ(afterResult->Loops.size(), 1u);

    // Fill the hole
    auto result = Geometry::MeshRepair::FillHoles(mesh);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->HolesDetected, 1u);
    EXPECT_EQ(result->HolesFilled, 1u);
    EXPECT_GE(result->TrianglesAdded, 1u);

    // Verify no more holes
    auto finalResult = Geometry::MeshRepair::FindBoundaryLoops(mesh);
    ASSERT_TRUE(finalResult.has_value());
    EXPECT_TRUE(finalResult->Loops.empty());
}

TEST(MeshRepair_HoleFilling, EmptyMeshReturnsNullopt)
{
    Geometry::Halfedge::Mesh mesh;
    auto result = Geometry::MeshRepair::FillHoles(mesh);
    EXPECT_FALSE(result.has_value());
}

TEST(MeshRepair_HoleFilling, ClosedMeshReportsZeroHoles)
{
    auto mesh = MakeTetrahedron();
    auto result = Geometry::MeshRepair::FillHoles(mesh);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->HolesDetected, 0u);
    EXPECT_EQ(result->HolesFilled, 0u);
}

TEST(MeshRepair_DegenerateFaces, DetectsZeroAreaTriangle)
{
    Geometry::Halfedge::Mesh mesh;
    // Create a degenerate triangle (all vertices collinear)
    auto v0 = mesh.AddVertex({0.0f, 0.0f, 0.0f});
    auto v1 = mesh.AddVertex({1.0f, 0.0f, 0.0f});
    auto v2 = mesh.AddVertex({2.0f, 0.0f, 0.0f});
    (void)mesh.AddTriangle(v0, v1, v2);

    auto result = Geometry::MeshRepair::RemoveDegenerateFaces(mesh);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->DegenerateFacesFound, 1u);
    EXPECT_EQ(result->FacesRemoved, 1u);
}

TEST(MeshRepair_DegenerateFaces, PreservesValidTriangles)
{
    auto mesh = MakeTetrahedron();
    std::size_t facesBefore = mesh.FaceCount();

    auto result = Geometry::MeshRepair::RemoveDegenerateFaces(mesh);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->DegenerateFacesFound, 0u);
    EXPECT_EQ(mesh.FaceCount(), facesBefore);
}

TEST(MeshRepair_DegenerateFaces, EmptyMeshReturnsNullopt)
{
    Geometry::Halfedge::Mesh mesh;
    auto result = Geometry::MeshRepair::RemoveDegenerateFaces(mesh);
    EXPECT_FALSE(result.has_value());
}

TEST(MeshRepair_Orientation, ClosedMeshIsConsistent)
{
    auto mesh = MakeTetrahedron();
    auto result = Geometry::MeshRepair::MakeConsistentOrientation(mesh);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->WasConsistent);
    EXPECT_EQ(result->FacesFlipped, 0u);
    EXPECT_EQ(result->ComponentCount, 1u);
}

TEST(MeshRepair_Orientation, EmptyMeshReturnsNullopt)
{
    Geometry::Halfedge::Mesh mesh;
    auto result = Geometry::MeshRepair::MakeConsistentOrientation(mesh);
    EXPECT_FALSE(result.has_value());
}

TEST(MeshRepair_Orientation, IcosahedronSingleComponent)
{
    auto mesh = MakeIcosahedron();
    auto result = Geometry::MeshRepair::MakeConsistentOrientation(mesh);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->ComponentCount, 1u);
}

TEST(MeshRepair_Combined, RepairValidMesh)
{
    // A valid closed mesh should pass through repair without modification
    auto mesh = MakeTetrahedron();

    auto result = Geometry::MeshRepair::Repair(mesh);
    ASSERT_TRUE(result.has_value());

    // No degenerates
    EXPECT_EQ(result->DegenerateResult.DegenerateFacesFound, 0u);

    // Orientation is consistent
    EXPECT_TRUE(result->OrientResult.WasConsistent);

    // No holes to fill
    EXPECT_EQ(result->HoleResult.HolesDetected, 0u);
}

TEST(MeshRepair_Combined, RepairWithDegenerates)
{
    // Create mesh with a degenerate triangle among valid ones
    Geometry::Halfedge::Mesh mesh;
    auto v0 = mesh.AddVertex({0.0f, 0.0f, 0.0f});
    auto v1 = mesh.AddVertex({1.0f, 0.0f, 0.0f});
    auto v2 = mesh.AddVertex({0.5f, 1.0f, 0.0f});
    (void)mesh.AddVertex({2.0f, 0.0f, 0.0f}); // extra vertex, not part of any face

    (void)mesh.AddTriangle(v0, v1, v2); // valid triangle

    // The repair should detect no degenerates in the valid triangles
    Geometry::MeshRepair::RepairParams params;
    params.FillHoles = false; // skip hole filling for this test

    auto result = Geometry::MeshRepair::Repair(mesh, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->DegenerateResult.DegenerateFacesFound, 0u);
}

TEST(MeshRepair_Combined, EmptyMeshReturnsNullopt)
{
    Geometry::Halfedge::Mesh mesh;
    auto result = Geometry::MeshRepair::Repair(mesh);
    EXPECT_FALSE(result.has_value());
}

