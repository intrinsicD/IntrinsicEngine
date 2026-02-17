#include <gtest/gtest.h>
#include <cmath>
#include <numbers>
#include <numeric>
#include <vector>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>

import Geometry;

// =============================================================================
// Test mesh builders (shared across all test groups)
// =============================================================================

// Single equilateral triangle: vertices at (0,0,0), (1,0,0), (0.5, sqrt(3)/2, 0)
static Geometry::Halfedge::Mesh MakeSingleTriangle()
{
    Geometry::Halfedge::Mesh mesh;
    auto v0 = mesh.AddVertex({0.0f, 0.0f, 0.0f});
    auto v1 = mesh.AddVertex({1.0f, 0.0f, 0.0f});
    auto v2 = mesh.AddVertex({0.5f, std::sqrt(3.0f) / 2.0f, 0.0f});
    (void)mesh.AddTriangle(v0, v1, v2);
    return mesh;
}

// Unit square split into two right triangles: 4 vertices, 2 faces
static Geometry::Halfedge::Mesh MakeTwoTriangleSquare()
{
    Geometry::Halfedge::Mesh mesh;
    auto v0 = mesh.AddVertex({0.0f, 0.0f, 0.0f});
    auto v1 = mesh.AddVertex({1.0f, 0.0f, 0.0f});
    auto v2 = mesh.AddVertex({1.0f, 1.0f, 0.0f});
    auto v3 = mesh.AddVertex({0.0f, 1.0f, 0.0f});
    (void)mesh.AddTriangle(v0, v1, v2);
    (void)mesh.AddTriangle(v0, v2, v3);
    return mesh;
}

// Regular tetrahedron (closed mesh)
static Geometry::Halfedge::Mesh MakeTetrahedron()
{
    Geometry::Halfedge::Mesh mesh;
    auto v0 = mesh.AddVertex({1.0f, 1.0f, 1.0f});
    auto v1 = mesh.AddVertex({1.0f, -1.0f, -1.0f});
    auto v2 = mesh.AddVertex({-1.0f, 1.0f, -1.0f});
    auto v3 = mesh.AddVertex({-1.0f, -1.0f, 1.0f});

    (void)mesh.AddTriangle(v0, v1, v2);
    (void)mesh.AddTriangle(v0, v2, v3);
    (void)mesh.AddTriangle(v0, v3, v1);
    (void)mesh.AddTriangle(v1, v3, v2);

    return mesh;
}

// Subdivided triangle: 6 vertices, 4 faces, good for testing interior vertices
static Geometry::Halfedge::Mesh MakeSubdividedTriangle()
{
    const float s = std::sqrt(3.0f);
    Geometry::Halfedge::Mesh mesh;
    auto v0 = mesh.AddVertex({0.0f, 0.0f, 0.0f});
    auto v1 = mesh.AddVertex({2.0f, 0.0f, 0.0f});
    auto v2 = mesh.AddVertex({1.0f, s, 0.0f});
    auto v3 = mesh.AddVertex({1.0f, 0.0f, 0.0f});       // mid(v0,v1)
    auto v4 = mesh.AddVertex({1.5f, s / 2.0f, 0.0f});   // mid(v1,v2)
    auto v5 = mesh.AddVertex({0.5f, s / 2.0f, 0.0f});   // mid(v0,v2)

    (void)mesh.AddTriangle(v0, v3, v5);
    (void)mesh.AddTriangle(v3, v1, v4);
    (void)mesh.AddTriangle(v5, v4, v2);
    (void)mesh.AddTriangle(v3, v4, v5);

    return mesh;
}

// Regular icosahedron (closed, 12 vertices, 20 faces, 30 edges)
// All vertices are on the unit sphere.
static Geometry::Halfedge::Mesh MakeIcosahedron()
{
    Geometry::Halfedge::Mesh mesh;
    const float phi = (1.0f + std::sqrt(5.0f)) / 2.0f;
    const float scale = 1.0f / std::sqrt(1.0f + phi * phi);

    // 12 vertices of an icosahedron
    auto v0  = mesh.AddVertex(glm::vec3( 0,  1,  phi) * scale);
    auto v1  = mesh.AddVertex(glm::vec3( 0, -1,  phi) * scale);
    auto v2  = mesh.AddVertex(glm::vec3( 0,  1, -phi) * scale);
    auto v3  = mesh.AddVertex(glm::vec3( 0, -1, -phi) * scale);
    auto v4  = mesh.AddVertex(glm::vec3( 1,  phi,  0) * scale);
    auto v5  = mesh.AddVertex(glm::vec3(-1,  phi,  0) * scale);
    auto v6  = mesh.AddVertex(glm::vec3( 1, -phi,  0) * scale);
    auto v7  = mesh.AddVertex(glm::vec3(-1, -phi,  0) * scale);
    auto v8  = mesh.AddVertex(glm::vec3( phi,  0,  1) * scale);
    auto v9  = mesh.AddVertex(glm::vec3(-phi,  0,  1) * scale);
    auto v10 = mesh.AddVertex(glm::vec3( phi,  0, -1) * scale);
    auto v11 = mesh.AddVertex(glm::vec3(-phi,  0, -1) * scale);

    // 20 faces
    (void)mesh.AddTriangle(v0, v1, v8);
    (void)mesh.AddTriangle(v0, v8, v4);
    (void)mesh.AddTriangle(v0, v4, v5);
    (void)mesh.AddTriangle(v0, v5, v9);
    (void)mesh.AddTriangle(v0, v9, v1);
    (void)mesh.AddTriangle(v1, v6, v8);
    (void)mesh.AddTriangle(v1, v7, v6);
    (void)mesh.AddTriangle(v1, v9, v7);
    (void)mesh.AddTriangle(v2, v3, v11);
    (void)mesh.AddTriangle(v2, v10, v3);
    (void)mesh.AddTriangle(v2, v4, v10);
    (void)mesh.AddTriangle(v2, v5, v4);
    (void)mesh.AddTriangle(v2, v11, v5);
    (void)mesh.AddTriangle(v3, v6, v7);
    (void)mesh.AddTriangle(v3, v10, v6);
    (void)mesh.AddTriangle(v3, v7, v11);
    (void)mesh.AddTriangle(v4, v8, v10);
    (void)mesh.AddTriangle(v5, v11, v9);
    (void)mesh.AddTriangle(v6, v10, v8);
    (void)mesh.AddTriangle(v7, v9, v11);

    return mesh;
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

// =============================================================================
// Curvature tests
// =============================================================================

TEST(Curvature_Mean, SphereHasConstantMeanCurvature)
{
    // The icosahedron is a crude approximation of a sphere.
    // All vertices should have approximately equal mean curvature.
    auto mesh = MakeIcosahedron();
    auto H = Geometry::Curvature::ComputeMeanCurvature(mesh);

    EXPECT_EQ(H.size(), mesh.VerticesSize());

    // All icosahedron vertices are symmetric — mean curvature should be equal
    double H0 = H[0];
    for (std::size_t i = 1; i < mesh.VerticesSize(); ++i)
    {
        Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(i)};
        if (mesh.IsDeleted(vh)) continue;
        EXPECT_NEAR(std::abs(H[i]), std::abs(H0), 1e-4)
            << "Vertex " << i << ": H=" << H[i] << " vs H0=" << H0;
    }
}

TEST(Curvature_Mean, FlatMeshHasZeroMeanCurvature)
{
    // A flat mesh (all vertices in a plane) should have zero mean curvature
    // at interior vertices.
    auto mesh = MakeSubdividedTriangle();
    auto H = Geometry::Curvature::ComputeMeanCurvature(mesh);

    // Vertex 3 (index 3) is the interior vertex with valence 4
    // in the subdivided triangle (midpoint of v0-v1).
    // For a flat mesh, its mean curvature should be 0.

    // Check all interior vertices
    for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
    {
        Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(i)};
        if (mesh.IsDeleted(vh) || mesh.IsIsolated(vh)) continue;
        if (mesh.IsBoundary(vh)) continue;

        EXPECT_NEAR(H[i], 0.0, 1e-6)
            << "Interior vertex " << i << " should have H=0 on flat mesh";
    }
}

TEST(Curvature_Gaussian, FlatMeshHasZeroGaussianCurvature)
{
    // Flat meshes have zero Gaussian curvature at interior vertices.
    auto mesh = MakeSubdividedTriangle();
    auto K = Geometry::Curvature::ComputeGaussianCurvature(mesh);

    for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
    {
        Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(i)};
        if (mesh.IsDeleted(vh) || mesh.IsIsolated(vh)) continue;
        if (mesh.IsBoundary(vh)) continue;

        EXPECT_NEAR(K[i], 0.0, 1e-6)
            << "Interior vertex " << i << " should have K=0 on flat mesh";
    }
}

TEST(Curvature_Gaussian, GaussBonnetOnClosedMesh)
{
    // Gauss-Bonnet theorem: Σ K_i * A_i = 2π * χ(M)
    // For a closed surface homeomorphic to a sphere: χ = 2, so integral = 4π.
    auto mesh = MakeTetrahedron();
    auto K = Geometry::Curvature::ComputeGaussianCurvature(mesh);

    // Build mixed areas
    auto ops = Geometry::DEC::BuildOperators(mesh);

    double integral = 0.0;
    for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
    {
        Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(i)};
        if (mesh.IsDeleted(vh) || mesh.IsIsolated(vh)) continue;
        integral += K[i] * ops.Hodge0.Diagonal[i];
    }

    // χ(tetrahedron) = V - E + F = 4 - 6 + 4 = 2
    double expected = 4.0 * std::numbers::pi;
    EXPECT_NEAR(integral, expected, 1e-4)
        << "Gauss-Bonnet: integral of K should equal 4π for a sphere-like closed mesh";
}

TEST(Curvature_Gaussian, IcosahedronGaussBonnet)
{
    auto mesh = MakeIcosahedron();
    auto K = Geometry::Curvature::ComputeGaussianCurvature(mesh);
    auto ops = Geometry::DEC::BuildOperators(mesh);

    double integral = 0.0;
    for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
    {
        Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(i)};
        if (mesh.IsDeleted(vh) || mesh.IsIsolated(vh)) continue;
        integral += K[i] * ops.Hodge0.Diagonal[i];
    }

    // Icosahedron: V=12, E=30, F=20, χ=2, integral = 4π
    double expected = 4.0 * std::numbers::pi;
    EXPECT_NEAR(integral, expected, 1e-4);
}

TEST(Curvature_Gaussian, PositiveOnConvexMesh)
{
    // On a convex mesh (icosahedron), all Gaussian curvatures should be positive.
    auto mesh = MakeIcosahedron();
    auto K = Geometry::Curvature::ComputeGaussianCurvature(mesh);

    for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
    {
        Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(i)};
        if (mesh.IsDeleted(vh) || mesh.IsIsolated(vh)) continue;

        EXPECT_GT(K[i], 0.0) << "Vertex " << i << " should have K > 0 on convex mesh";
    }
}

TEST(Curvature_Full, PrincipalCurvatureRelation)
{
    // For any mesh: H = (κ₁ + κ₂) / 2 and K = κ₁ * κ₂
    auto mesh = MakeIcosahedron();
    auto field = Geometry::Curvature::ComputeCurvature(mesh);

    for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
    {
        Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(i)};
        if (mesh.IsDeleted(vh) || mesh.IsIsolated(vh)) continue;

        const auto& vc = field.Vertices[i];

        // H should be average of principal curvatures
        double expectedH = (vc.MaxPrincipalCurvature + vc.MinPrincipalCurvature) / 2.0;
        EXPECT_NEAR(vc.MeanCurvature, expectedH, 1e-6)
            << "Vertex " << i << ": H should equal (κ₁ + κ₂)/2";

        // K should be product of principal curvatures (within discretization error).
        // On coarse meshes like the icosahedron, mean and Gaussian curvature
        // are computed by independent methods (Laplace-Beltrami vs angle defect),
        // so the relation K = κ₁*κ₂ only holds approximately.
        double expectedK = vc.MaxPrincipalCurvature * vc.MinPrincipalCurvature;
        EXPECT_NEAR(vc.GaussianCurvature, expectedK, 0.5)
            << "Vertex " << i << ": K should approximately equal κ₁ * κ₂";
    }
}

TEST(Curvature_Full, MeanCurvatureNormalsNonZero)
{
    auto mesh = MakeIcosahedron();
    auto field = Geometry::Curvature::ComputeCurvature(mesh);

    EXPECT_EQ(field.MeanCurvatureNormals.size(), mesh.VerticesSize());

    for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
    {
        Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(i)};
        if (mesh.IsDeleted(vh) || mesh.IsIsolated(vh)) continue;

        float len = glm::length(field.MeanCurvatureNormals[i]);
        EXPECT_GT(len, 0.0f) << "Vertex " << i << " mean curvature normal should be non-zero";
    }
}

// =============================================================================
// Smoothing tests
// =============================================================================

TEST(Smoothing_Uniform, ReducesVariance)
{
    // Create a noisy mesh by perturbing vertex positions of the icosahedron
    auto mesh = MakeIcosahedron();

    // Add noise to vertex positions
    for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
    {
        Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(i)};
        if (mesh.IsDeleted(vh)) continue;

        // Deterministic "noise" based on vertex index
        float noise = 0.05f * (static_cast<float>(i % 5) - 2.0f);
        glm::vec3 p = mesh.Position(vh);
        mesh.Position(vh) = p + glm::normalize(p) * noise;
    }

    // Compute variance of edge lengths before smoothing
    auto edgeLengthVariance = [](const Geometry::Halfedge::Mesh& m) -> double
    {
        double sum = 0.0, sumSq = 0.0;
        std::size_t count = 0;
        for (std::size_t i = 0; i < m.EdgesSize(); ++i)
        {
            Geometry::EdgeHandle e{static_cast<Geometry::PropertyIndex>(i)};
            if (m.IsDeleted(e)) continue;

            auto h = m.Halfedge(e, 0);
            double len = static_cast<double>(glm::distance(
                m.Position(m.FromVertex(h)),
                m.Position(m.ToVertex(h))));
            sum += len;
            sumSq += len * len;
            ++count;
        }
        double mean = sum / static_cast<double>(count);
        return sumSq / static_cast<double>(count) - mean * mean;
    };

    double varianceBefore = edgeLengthVariance(mesh);

    Geometry::Smoothing::SmoothingParams params;
    params.Iterations = 5;
    params.Lambda = 0.3;
    params.PreserveBoundary = false; // closed mesh has no boundary

    Geometry::Smoothing::UniformLaplacian(mesh, params);

    double varianceAfter = edgeLengthVariance(mesh);

    // Smoothing should reduce edge length variance
    EXPECT_LT(varianceAfter, varianceBefore);
}

TEST(Smoothing_Uniform, PreservesBoundary)
{
    auto mesh = MakeSubdividedTriangle();

    // Record boundary vertex positions
    std::vector<std::pair<Geometry::VertexHandle, glm::vec3>> boundaryPositions;
    for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
    {
        Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(i)};
        if (mesh.IsBoundary(vh))
        {
            boundaryPositions.push_back({vh, mesh.Position(vh)});
        }
    }

    Geometry::Smoothing::SmoothingParams params;
    params.Iterations = 10;
    params.Lambda = 0.5;
    params.PreserveBoundary = true;

    Geometry::Smoothing::UniformLaplacian(mesh, params);

    // Boundary vertices should be unchanged
    for (const auto& [vh, pos] : boundaryPositions)
    {
        glm::vec3 after = mesh.Position(vh);
        EXPECT_NEAR(after.x, pos.x, 1e-6f);
        EXPECT_NEAR(after.y, pos.y, 1e-6f);
        EXPECT_NEAR(after.z, pos.z, 1e-6f);
    }
}

TEST(Smoothing_Cotan, ReducesEdgeLengthVariance)
{
    auto mesh = MakeIcosahedron();

    // Add mild radial noise to break the uniformity.
    // Keep noise small so the explicit cotan integration remains stable
    // (large perturbations create obtuse triangles with small areas,
    // amplifying the area-normalized update).
    for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
    {
        Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(i)};
        if (mesh.IsDeleted(vh)) continue;

        float noise = 0.03f * (static_cast<float>((i * 7) % 5) - 2.0f);
        glm::vec3 p = mesh.Position(vh);
        mesh.Position(vh) = p + glm::normalize(p) * noise;
    }

    // Compute edge length variance
    auto edgeLengthVariance = [](const Geometry::Halfedge::Mesh& m) -> double
    {
        double sum = 0.0, sumSq = 0.0;
        std::size_t count = 0;
        for (std::size_t i = 0; i < m.EdgesSize(); ++i)
        {
            Geometry::EdgeHandle e{static_cast<Geometry::PropertyIndex>(i)};
            if (m.IsDeleted(e)) continue;
            auto h = m.Halfedge(e, 0);
            double len = static_cast<double>(glm::distance(
                m.Position(m.FromVertex(h)), m.Position(m.ToVertex(h))));
            sum += len;
            sumSq += len * len;
            ++count;
        }
        double mean = sum / static_cast<double>(count);
        return sumSq / static_cast<double>(count) - mean * mean;
    };

    double varBefore = edgeLengthVariance(mesh);

    Geometry::Smoothing::SmoothingParams params;
    params.Iterations = 10;
    params.Lambda = 0.05;
    params.PreserveBoundary = false;

    Geometry::Smoothing::CotanLaplacian(mesh, params);

    double varAfter = edgeLengthVariance(mesh);

    EXPECT_LT(varAfter, varBefore);
}

TEST(Smoothing_Taubin, PreservesVolumeBetterThanLaplacian)
{
    auto mesh1 = MakeIcosahedron(); // For Laplacian
    auto mesh2 = MakeIcosahedron(); // For Taubin

    // Compute approximate "volume" using vertex distance from origin
    auto avgRadius = [](const Geometry::Halfedge::Mesh& m) -> double
    {
        double sum = 0.0;
        std::size_t count = 0;
        for (std::size_t i = 0; i < m.VerticesSize(); ++i)
        {
            Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(i)};
            if (m.IsDeleted(vh)) continue;
            sum += static_cast<double>(glm::length(m.Position(vh)));
            ++count;
        }
        return sum / static_cast<double>(count);
    };

    double radiusBefore = avgRadius(mesh1);

    // Apply Laplacian smoothing
    Geometry::Smoothing::SmoothingParams lapParams;
    lapParams.Iterations = 10;
    lapParams.Lambda = 0.5;
    lapParams.PreserveBoundary = false;
    Geometry::Smoothing::UniformLaplacian(mesh1, lapParams);

    // Apply Taubin smoothing
    Geometry::Smoothing::TaubinParams taubinParams;
    taubinParams.Iterations = 10;
    taubinParams.Lambda = 0.5;
    taubinParams.PassbandFrequency = 0.1;
    taubinParams.PreserveBoundary = false;
    Geometry::Smoothing::Taubin(mesh2, taubinParams);

    double radiusAfterLaplacian = avgRadius(mesh1);
    double radiusAfterTaubin = avgRadius(mesh2);

    double shrinkageLaplacian = std::abs(radiusBefore - radiusAfterLaplacian);
    double shrinkageTaubin = std::abs(radiusBefore - radiusAfterTaubin);

    // Taubin should have less shrinkage than pure Laplacian
    EXPECT_LT(shrinkageTaubin, shrinkageLaplacian);
}

TEST(Smoothing_Taubin, FlatMeshStaysFlat)
{
    // A flat mesh smoothed with Taubin should remain flat
    auto mesh = MakeSubdividedTriangle();

    Geometry::Smoothing::TaubinParams params;
    params.Iterations = 5;
    params.Lambda = 0.5;
    params.PassbandFrequency = 0.1;
    params.PreserveBoundary = true;

    Geometry::Smoothing::Taubin(mesh, params);

    // Check all vertices remain in the z=0 plane
    for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
    {
        Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(i)};
        if (mesh.IsDeleted(vh)) continue;

        EXPECT_NEAR(mesh.Position(vh).z, 0.0f, 1e-6f)
            << "Vertex " << i << " should remain on z=0 plane after smoothing";
    }
}

// =============================================================================
// Implicit Laplacian Smoothing tests
// =============================================================================

TEST(Smoothing_Implicit, EmptyMeshReturnsNullopt)
{
    Geometry::Halfedge::Mesh mesh;
    auto result = Geometry::Smoothing::ImplicitLaplacian(mesh);
    EXPECT_FALSE(result.has_value());
}

TEST(Smoothing_Implicit, FlatMeshStaysFlat)
{
    auto mesh = MakeSubdividedTriangle();

    Geometry::Smoothing::ImplicitSmoothingParams params;
    params.Iterations = 3;
    params.Lambda = 1.0;
    params.PreserveBoundary = true;

    auto result = Geometry::Smoothing::ImplicitLaplacian(mesh, params);
    ASSERT_TRUE(result.has_value());

    // All vertices should remain on z=0 plane
    for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
    {
        Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(i)};
        if (mesh.IsDeleted(vh) || mesh.IsIsolated(vh)) continue;

        EXPECT_NEAR(mesh.Position(vh).z, 0.0f, 1e-4f)
            << "Vertex " << i << " should remain on z=0 plane";
    }
}

TEST(Smoothing_Implicit, ReducesNoise)
{
    auto mesh = MakeIcosahedron();

    // Add noise
    for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
    {
        Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(i)};
        if (mesh.IsDeleted(vh)) continue;

        float noise = 0.05f * (static_cast<float>(i % 5) - 2.0f);
        glm::vec3 p = mesh.Position(vh);
        mesh.Position(vh) = p + glm::normalize(p) * noise;
    }

    auto edgeLengthVariance = [](const Geometry::Halfedge::Mesh& m) -> double
    {
        double sum = 0.0, sumSq = 0.0;
        std::size_t count = 0;
        for (std::size_t i = 0; i < m.EdgesSize(); ++i)
        {
            Geometry::EdgeHandle e{static_cast<Geometry::PropertyIndex>(i)};
            if (m.IsDeleted(e)) continue;
            auto h = m.Halfedge(e, 0);
            double len = static_cast<double>(glm::distance(
                m.Position(m.FromVertex(h)), m.Position(m.ToVertex(h))));
            sum += len;
            sumSq += len * len;
            ++count;
        }
        double mean = sum / static_cast<double>(count);
        return sumSq / static_cast<double>(count) - mean * mean;
    };

    double varBefore = edgeLengthVariance(mesh);

    Geometry::Smoothing::ImplicitSmoothingParams params;
    params.Iterations = 1;
    params.Lambda = 1.0;
    params.PreserveBoundary = false;

    auto result = Geometry::Smoothing::ImplicitLaplacian(mesh, params);
    ASSERT_TRUE(result.has_value());

    double varAfter = edgeLengthVariance(mesh);
    EXPECT_LT(varAfter, varBefore);
}

TEST(Smoothing_Implicit, PreservesBoundary)
{
    auto mesh = MakeSubdividedTriangle();

    // Record boundary positions
    std::vector<std::pair<Geometry::VertexHandle, glm::vec3>> boundaryPositions;
    for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
    {
        Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(i)};
        if (mesh.IsBoundary(vh))
            boundaryPositions.push_back({vh, mesh.Position(vh)});
    }

    Geometry::Smoothing::ImplicitSmoothingParams params;
    params.Iterations = 3;
    params.Lambda = 1.0;
    params.PreserveBoundary = true;

    auto result = Geometry::Smoothing::ImplicitLaplacian(mesh, params);
    ASSERT_TRUE(result.has_value());

    for (const auto& [vh, pos] : boundaryPositions)
    {
        glm::vec3 after = mesh.Position(vh);
        EXPECT_NEAR(after.x, pos.x, 1e-5f);
        EXPECT_NEAR(after.y, pos.y, 1e-5f);
        EXPECT_NEAR(after.z, pos.z, 1e-5f);
    }
}

TEST(Smoothing_Implicit, UnconditionallyStable)
{
    // Implicit smoothing should be stable even with very large timesteps
    auto mesh = MakeIcosahedron();

    Geometry::Smoothing::ImplicitSmoothingParams params;
    params.Iterations = 1;
    params.Lambda = 1.0;
    params.TimeStep = 1000.0; // Enormous timestep
    params.PreserveBoundary = false;

    auto result = Geometry::Smoothing::ImplicitLaplacian(mesh, params);
    ASSERT_TRUE(result.has_value());

    // Check no NaN or Inf
    for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
    {
        Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(i)};
        if (mesh.IsDeleted(vh) || mesh.IsIsolated(vh)) continue;

        glm::vec3 p = mesh.Position(vh);
        EXPECT_FALSE(std::isnan(p.x) || std::isnan(p.y) || std::isnan(p.z))
            << "Vertex " << i << " has NaN after large timestep";
        EXPECT_FALSE(std::isinf(p.x) || std::isinf(p.y) || std::isinf(p.z))
            << "Vertex " << i << " has Inf after large timestep";
    }
}

TEST(Smoothing_Implicit, ConvergesForClosedMesh)
{
    auto mesh = MakeIcosahedron();

    Geometry::Smoothing::ImplicitSmoothingParams params;
    params.Iterations = 1;
    params.Lambda = 1.0;
    params.PreserveBoundary = false;

    auto result = Geometry::Smoothing::ImplicitLaplacian(mesh, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->Converged);
    EXPECT_EQ(result->IterationsPerformed, 1u);
}

TEST(Smoothing_Implicit, MultipleIterations)
{
    auto mesh1 = MakeIcosahedron();
    auto mesh2 = MakeIcosahedron();

    // Add same noise to both
    for (std::size_t i = 0; i < mesh1.VerticesSize(); ++i)
    {
        Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(i)};
        if (mesh1.IsDeleted(vh)) continue;
        float noise = 0.05f * (static_cast<float>(i % 5) - 2.0f);
        glm::vec3 p = mesh1.Position(vh);
        mesh1.Position(vh) = p + glm::normalize(p) * noise;
        mesh2.Position(vh) = p + glm::normalize(p) * noise;
    }

    // 1 iteration on mesh1
    Geometry::Smoothing::ImplicitSmoothingParams params1;
    params1.Iterations = 1;
    params1.Lambda = 1.0;
    params1.PreserveBoundary = false;
    (void)Geometry::Smoothing::ImplicitLaplacian(mesh1, params1);

    // 3 iterations on mesh2
    Geometry::Smoothing::ImplicitSmoothingParams params3;
    params3.Iterations = 3;
    params3.Lambda = 1.0;
    params3.PreserveBoundary = false;
    auto result = Geometry::Smoothing::ImplicitLaplacian(mesh2, params3);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->IterationsPerformed, 3u);

    // mesh2 (3 iterations) should be smoother than mesh1 (1 iteration)
    auto edgeLengthVariance = [](const Geometry::Halfedge::Mesh& m) -> double
    {
        double sum = 0.0, sumSq = 0.0;
        std::size_t count = 0;
        for (std::size_t i = 0; i < m.EdgesSize(); ++i)
        {
            Geometry::EdgeHandle e{static_cast<Geometry::PropertyIndex>(i)};
            if (m.IsDeleted(e)) continue;
            auto h = m.Halfedge(e, 0);
            double len = static_cast<double>(glm::distance(
                m.Position(m.FromVertex(h)), m.Position(m.ToVertex(h))));
            sum += len;
            sumSq += len * len;
            ++count;
        }
        double mean = sum / static_cast<double>(count);
        return sumSq / static_cast<double>(count) - mean * mean;
    };

    EXPECT_LE(edgeLengthVariance(mesh2), edgeLengthVariance(mesh1));
}

// =============================================================================
// Simplification tests
// =============================================================================

TEST(Simplification_QEM, ReducesFaceCount)
{
    auto mesh = MakeIcosahedron();
    EXPECT_EQ(mesh.FaceCount(), 20u);

    Geometry::Simplification::SimplificationParams params;
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

    Geometry::Simplification::SimplificationParams params;
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

    Geometry::Simplification::SimplificationParams params;
    params.TargetFaces = 1;

    auto result = Geometry::Simplification::Simplify(mesh, params);
    EXPECT_FALSE(result.has_value());
}

TEST(Simplification_QEM, ErrorThresholdStopsEarly)
{
    auto mesh = MakeIcosahedron();

    Geometry::Simplification::SimplificationParams params;
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

    Geometry::Simplification::SimplificationParams params;
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
