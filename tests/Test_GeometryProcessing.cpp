#include <gtest/gtest.h>
#include <cmath>
#include <numbers>
#include <numeric>
#include <unordered_set>
#include <vector>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>

import Geometry;

#include "Test_MeshBuilders.h"

// =============================================================================
// Helper: compute edge length statistics
// =============================================================================
static void EdgeLengthStats(const Geometry::Halfedge::Mesh& mesh,
                            double& minLen, double& maxLen, double& meanLen)
{
    minLen = 1e30;
    maxLen = 0.0;
    double sum = 0.0;
    std::size_t count = 0;
    for (std::size_t ei = 0; ei < mesh.EdgesSize(); ++ei)
    {
        Geometry::EdgeHandle e{static_cast<Geometry::PropertyIndex>(ei)};
        if (mesh.IsDeleted(e)) continue;
        auto h = mesh.Halfedge(e, 0);
        double len = static_cast<double>(glm::distance(
            mesh.Position(mesh.FromVertex(h)),
            mesh.Position(mesh.ToVertex(h))));
        if (len < minLen) minLen = len;
        if (len > maxLen) maxLen = len;
        sum += len;
        ++count;
    }
    meanLen = (count > 0) ? sum / static_cast<double>(count) : 0.0;
}

// =============================================================================
// CG Solver tests
// =============================================================================

TEST(CG_Solver, SolvesIdentitySystem)
{
    // A = Identity (3x3), b = [1, 2, 3], should give x = [1, 2, 3]
    Geometry::DEC::SparseMatrix A;
    A.Rows = 3;
    A.Cols = 3;
    A.RowOffsets = {0, 1, 2, 3};
    A.ColIndices = {0, 1, 2};
    A.Values = {1.0, 1.0, 1.0};

    std::vector<double> b = {1.0, 2.0, 3.0};
    std::vector<double> x(3, 0.0);

    auto result = Geometry::DEC::SolveCG(A, b, x);
    EXPECT_TRUE(result.Converged);
    EXPECT_NEAR(x[0], 1.0, 1e-6);
    EXPECT_NEAR(x[1], 2.0, 1e-6);
    EXPECT_NEAR(x[2], 3.0, 1e-6);
}

TEST(CG_Solver, SolvesDiagonalSystem)
{
    // A = diag(2, 3, 5), b = [4, 9, 25], x = [2, 3, 5]
    Geometry::DEC::SparseMatrix A;
    A.Rows = 3;
    A.Cols = 3;
    A.RowOffsets = {0, 1, 2, 3};
    A.ColIndices = {0, 1, 2};
    A.Values = {2.0, 3.0, 5.0};

    std::vector<double> b = {4.0, 9.0, 25.0};
    std::vector<double> x(3, 0.0);

    auto result = Geometry::DEC::SolveCG(A, b, x);
    EXPECT_TRUE(result.Converged);
    EXPECT_NEAR(x[0], 2.0, 1e-6);
    EXPECT_NEAR(x[1], 3.0, 1e-6);
    EXPECT_NEAR(x[2], 5.0, 1e-6);
}

TEST(CG_Solver, SolvesSPDTridiagonal)
{
    // 4x4 tridiagonal SPD:
    //  [ 4 -1  0  0]    [1]    [5]
    //  [-1  4 -1  0] x  [2] =  [5]
    //  [ 0 -1  4 -1]    [3]    [5]
    //  [ 0  0 -1  4]    [4]    [13]
    Geometry::DEC::SparseMatrix A;
    A.Rows = 4;
    A.Cols = 4;
    A.RowOffsets = {0, 2, 5, 8, 10};
    A.ColIndices = {0, 1,  0, 1, 2,  1, 2, 3,  2, 3};
    A.Values = {4, -1,  -1, 4, -1,  -1, 4, -1,  -1, 4};

    // b = A * [1,2,3,4]
    std::vector<double> xTrue = {1.0, 2.0, 3.0, 4.0};
    std::vector<double> b(4);
    A.Multiply(xTrue, b);

    std::vector<double> x(4, 0.0);
    auto result = Geometry::DEC::SolveCG(A, b, x);
    EXPECT_TRUE(result.Converged);
    for (std::size_t i = 0; i < 4; ++i)
        EXPECT_NEAR(x[i], xTrue[i], 1e-6) << "x[" << i << "]";
}

TEST(CG_Solver, SolvesLaplacianSystem)
{
    // Use the actual mesh Laplacian + small regularization
    auto mesh = MakeTetrahedron();
    auto ops = Geometry::DEC::BuildOperators(mesh);

    // Solve (L + epsilon*I) x = b
    // With regularization to make it SPD
    const std::size_t n = ops.Laplacian.Rows;
    Geometry::DEC::DiagonalMatrix reg;
    reg.Size = n;
    reg.Diagonal.assign(n, 0.001);

    // Create a known solution and compute rhs
    std::vector<double> xTrue(n);
    for (std::size_t i = 0; i < n; ++i)
        xTrue[i] = static_cast<double>(i) * 1.5;

    // Compute b = (L + 0.001*I) * xTrue
    std::vector<double> Lx(n), b(n);
    ops.Laplacian.Multiply(xTrue, Lx);
    for (std::size_t i = 0; i < n; ++i)
        b[i] = Lx[i] + 0.001 * xTrue[i];

    std::vector<double> x(n, 0.0);
    auto result = Geometry::DEC::SolveCGShifted(
        reg, 1.0, ops.Laplacian, 1.0, b, x);

    EXPECT_TRUE(result.Converged);
    for (std::size_t i = 0; i < n; ++i)
        EXPECT_NEAR(x[i], xTrue[i], 1e-4) << "x[" << i << "]";
}

TEST(CG_Solver, ShiftedSolverConverges)
{
    auto mesh = MakeIcosahedron();
    auto ops = Geometry::DEC::BuildOperators(mesh);

    const std::size_t n = ops.Laplacian.Rows;

    // Solve (M + t*L) u = delta
    // where M = Hodge0, t = 0.01
    std::vector<double> rhs(n, 0.0);
    rhs[0] = 1.0;

    std::vector<double> u(n, 0.0);
    auto result = Geometry::DEC::SolveCGShifted(
        ops.Hodge0, 1.0, ops.Laplacian, 0.01, rhs, u);

    EXPECT_TRUE(result.Converged);

    // Solution should be positive at source, decay away
    EXPECT_GT(u[0], 0.0);
}

// =============================================================================
// Isotropic Remeshing tests
// =============================================================================

TEST(Remeshing_Isotropic, ReducesEdgeLengthVariance)
{
    // Create a mesh with varied edge lengths by subdividing then simplifying
    auto mesh = MakeIcosahedron();

    double minBefore = 0, maxBefore = 0, meanBefore = 0;
    EdgeLengthStats(mesh, minBefore, maxBefore, meanBefore);

    // Perturb vertices to create non-uniform edge lengths
    for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
    {
        Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(i)};
        if (mesh.IsDeleted(vh)) continue;
        float noise = 0.15f * (static_cast<float>(i % 5) - 2.0f);
        glm::vec3 p = mesh.Position(vh);
        mesh.Position(vh) = p + glm::normalize(p) * noise;
    }

    double minPert = 0, maxPert = 0, meanPert = 0;
    EdgeLengthStats(mesh, minPert, maxPert, meanPert);

    Geometry::Remeshing::RemeshingParams params;
    params.TargetLength = meanPert;
    params.Iterations = 2;
    params.PreserveBoundary = false;

    auto result = Geometry::Remeshing::Remesh(mesh, params);
    ASSERT_TRUE(result.has_value());

    double minAfter = 0, maxAfter = 0, meanAfter = 0;
    EdgeLengthStats(mesh, minAfter, maxAfter, meanAfter);

    // The range (max - min) should be reduced
    double rangeBefore = maxPert - minPert;
    double rangeAfter = maxAfter - minAfter;
    EXPECT_LT(rangeAfter, rangeBefore)
        << "Edge length range should decrease: " << rangeBefore << " -> " << rangeAfter;
}

TEST(Remeshing_Isotropic, PerformsSplitsAndCollapses)
{
    auto mesh = MakeIcosahedron();

    Geometry::Remeshing::RemeshingParams params;
    params.Iterations = 3;
    params.PreserveBoundary = false;

    auto result = Geometry::Remeshing::Remesh(mesh, params);
    ASSERT_TRUE(result.has_value());

    // Should have performed some operations
    EXPECT_GT(result->IterationsPerformed, 0u);
    EXPECT_GT(result->FinalVertexCount, 0u);
    EXPECT_GT(result->FinalFaceCount, 0u);
}

TEST(Remeshing_Isotropic, ShorterTargetIncreasesVertexCount)
{
    // Use tetrahedron (small mesh) to avoid combinatorial explosion.
    // With target = 0.7 * meanLen, split threshold = (4/3)*0.7 = 0.933 * meanLen,
    // so all edges (at meanLen) exceed the threshold and get split.
    auto mesh = MakeTetrahedron();
    std::size_t vBefore = mesh.VertexCount();

    double minLen = 0, maxLen = 0, meanLen = 0;
    EdgeLengthStats(mesh, minLen, maxLen, meanLen);

    Geometry::Remeshing::RemeshingParams params;
    params.TargetLength = meanLen * 0.7;
    params.Iterations = 1;
    params.PreserveBoundary = false;

    auto result = Geometry::Remeshing::Remesh(mesh, params);
    ASSERT_TRUE(result.has_value());

    EXPECT_GT(result->FinalVertexCount, vBefore)
        << "Shorter target length should increase vertex count";
}

TEST(Remeshing_Isotropic, PreservesBoundaryVertices)
{
    auto mesh = MakeSubdividedTriangle();

    // Record boundary vertex positions
    std::vector<std::pair<Geometry::VertexHandle, glm::vec3>> boundaryBefore;
    for (std::size_t vi = 0; vi < mesh.VerticesSize(); ++vi)
    {
        Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(vi)};
        if (mesh.IsBoundary(vh))
            boundaryBefore.push_back({vh, mesh.Position(vh)});
    }

    Geometry::Remeshing::RemeshingParams params;
    params.Iterations = 3;
    params.PreserveBoundary = true;

    auto result = Geometry::Remeshing::Remesh(mesh, params);
    ASSERT_TRUE(result.has_value());

    // Original boundary vertices should still be at their positions
    for (const auto& [vh, pos] : boundaryBefore)
    {
        if (mesh.IsDeleted(vh)) continue;
        glm::vec3 after = mesh.Position(vh);
        EXPECT_NEAR(after.x, pos.x, 1e-5f);
        EXPECT_NEAR(after.y, pos.y, 1e-5f);
        EXPECT_NEAR(after.z, pos.z, 1e-5f);
    }
}

TEST(Remeshing_Isotropic, MaintainsValidMesh)
{
    auto mesh = MakeIcosahedron();

    Geometry::Remeshing::RemeshingParams params;
    params.Iterations = 2;
    params.PreserveBoundary = false;

    auto result = Geometry::Remeshing::Remesh(mesh, params);
    ASSERT_TRUE(result.has_value());

    mesh.GarbageCollection();

    // All faces should be triangles
    for (std::size_t fi = 0; fi < mesh.FacesSize(); ++fi)
    {
        Geometry::FaceHandle fh{static_cast<Geometry::PropertyIndex>(fi)};
        if (mesh.IsDeleted(fh)) continue;
        EXPECT_EQ(mesh.Valence(fh), 3u) << "Face " << fi;
    }

    // All vertices should have valence >= 3
    for (std::size_t vi = 0; vi < mesh.VerticesSize(); ++vi)
    {
        Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(vi)};
        if (mesh.IsDeleted(vh) || mesh.IsIsolated(vh)) continue;
        EXPECT_GE(mesh.Valence(vh), 3u) << "Vertex " << vi;
    }

    // Euler characteristic should be preserved for closed mesh
    std::size_t V = mesh.VertexCount();
    std::size_t E = mesh.EdgeCount();
    std::size_t F = mesh.FaceCount();
    EXPECT_EQ(static_cast<int>(V) - static_cast<int>(E) + static_cast<int>(F), 2)
        << "Euler characteristic V-E+F=" << V << "-" << E << "+" << F;
}

TEST(Remeshing_Isotropic, TooFewFacesReturnsNullopt)
{
    auto mesh = MakeSingleTriangle();
    auto result = Geometry::Remeshing::Remesh(mesh);
    EXPECT_FALSE(result.has_value());
}

TEST(Remeshing_Isotropic, EmptyMeshReturnsNullopt)
{
    Geometry::Halfedge::Mesh mesh;
    auto result = Geometry::Remeshing::Remesh(mesh);
    EXPECT_FALSE(result.has_value());
}

// =============================================================================
// MeshUtils::ComputeCotanLaplacian
// =============================================================================

TEST(MeshUtils_CotanLaplacian, EmptyMeshReturnsEmptyVector)
{
    Geometry::Halfedge::Mesh mesh;
    auto laplacian = Geometry::MeshUtils::ComputeCotanLaplacian(mesh);
    EXPECT_TRUE(laplacian.empty());
}

TEST(MeshUtils_CotanLaplacian, SingleTriangleNonZero)
{
    auto mesh = MakeSingleTriangle();
    auto laplacian = Geometry::MeshUtils::ComputeCotanLaplacian(mesh);

    ASSERT_EQ(laplacian.size(), mesh.VerticesSize());

    // On a single triangle, each vertex should have a non-trivial Laplacian
    // (boundary edges have only one adjacent face, so cotSum has one term).
    double totalMag = 0.0;
    for (const auto& L : laplacian)
        totalMag += glm::length(L);
    EXPECT_GT(totalMag, 0.0);
}

TEST(MeshUtils_EdgeCotanWeight, EquilateralTriangleBoundaryWeight)
{
    auto mesh = MakeSingleTriangle();

    const double expected = 1.0 / (2.0 * std::sqrt(3.0));
    for (std::size_t ei = 0; ei < mesh.EdgesSize(); ++ei)
    {
        const Geometry::EdgeHandle e{static_cast<Geometry::PropertyIndex>(ei)};
        EXPECT_NEAR(Geometry::MeshUtils::EdgeCotanWeight(mesh, e), expected, 1e-6)
            << "Edge " << ei;
    }
}

TEST(MeshUtils_CotanLaplacian, EquilateralTriangleSymmetry)
{
    // Equilateral triangle: all vertices should have equal Laplacian magnitude.
    constexpr double kSymmetryTolerance = 1e-8;

    auto mesh = MakeSingleTriangle();
    auto laplacian = Geometry::MeshUtils::ComputeCotanLaplacian(mesh);

    ASSERT_EQ(laplacian.size(), 3u);
    double m0 = glm::length(laplacian[0]);
    double m1 = glm::length(laplacian[1]);
    double m2 = glm::length(laplacian[2]);
    EXPECT_NEAR(m0, m1, kSymmetryTolerance);
    EXPECT_NEAR(m1, m2, kSymmetryTolerance);
}

TEST(MeshUtils_CotanLaplacian, ClampedWeightsNonNegative)
{
    // With clamped weights, the result should differ from unclamped only when
    // negative cotan weights are present (obtuse triangles). For a regular
    // icosahedron (all equilateral faces), both should be identical.
    auto mesh = MakeIcosahedron();
    auto unclamped = Geometry::MeshUtils::ComputeCotanLaplacian(mesh, false);
    auto clamped = Geometry::MeshUtils::ComputeCotanLaplacian(mesh, true);

    ASSERT_EQ(unclamped.size(), clamped.size());
    for (std::size_t i = 0; i < unclamped.size(); ++i)
    {
        EXPECT_NEAR(unclamped[i].x, clamped[i].x, 1e-10) << "Index " << i;
        EXPECT_NEAR(unclamped[i].y, clamped[i].y, 1e-10) << "Index " << i;
        EXPECT_NEAR(unclamped[i].z, clamped[i].z, 1e-10) << "Index " << i;
    }
}

TEST(MeshUtils_CotanLaplacian, ClosedMeshLaplacianSumsToZero)
{
    // On a closed manifold, the sum of all Laplacian vectors should be zero
    // (conservation: total displacement is zero).
    auto mesh = MakeTetrahedron();
    auto laplacian = Geometry::MeshUtils::ComputeCotanLaplacian(mesh);

    glm::dvec3 sum(0.0);
    for (const auto& L : laplacian)
        sum += L;
    EXPECT_NEAR(sum.x, 0.0, 1e-10);
    EXPECT_NEAR(sum.y, 0.0, 1e-10);
    EXPECT_NEAR(sum.z, 0.0, 1e-10);
}

// =============================================================================
// MeshUtils::ComputeOneRingCentroid
// =============================================================================

TEST(MeshUtils_OneRingCentroid, IsolatedVertexReturnsSelf)
{
    Geometry::Halfedge::Mesh mesh;
    auto v = mesh.AddVertex(glm::vec3(1.0f, 2.0f, 3.0f));

    auto centroid = Geometry::MeshUtils::ComputeOneRingCentroid(mesh, v);
    EXPECT_NEAR(centroid.x, 1.0, 1e-10);
    EXPECT_NEAR(centroid.y, 2.0, 1e-10);
    EXPECT_NEAR(centroid.z, 3.0, 1e-10);
}

TEST(MeshUtils_OneRingCentroid, EquilateralTriangleCentroids)
{
    // For an equilateral triangle, centroid of neighbors of vertex 0 is the
    // midpoint of the opposite edge.
    auto mesh = MakeSingleTriangle();

    // Vertex 0's neighbors are vertices 1 and 2
    Geometry::VertexHandle v0{0};
    auto centroid = Geometry::MeshUtils::ComputeOneRingCentroid(mesh, v0);

    glm::vec3 p1 = mesh.Position(Geometry::VertexHandle{1});
    glm::vec3 p2 = mesh.Position(Geometry::VertexHandle{2});
    glm::dvec3 expected = (glm::dvec3(p1) + glm::dvec3(p2)) / 2.0;

    EXPECT_NEAR(centroid.x, expected.x, 1e-10);
    EXPECT_NEAR(centroid.y, expected.y, 1e-10);
    EXPECT_NEAR(centroid.z, expected.z, 1e-10);
}

TEST(MeshUtils_OneRingCentroid, RegularTetrahedronSymmetry)
{
    // All vertices in a regular tetrahedron have the same centroid distance
    // from their position (by symmetry).
    auto mesh = MakeTetrahedron();

    double dist0 = glm::length(
        Geometry::MeshUtils::ComputeOneRingCentroid(mesh, Geometry::VertexHandle{0})
        - glm::dvec3(mesh.Position(Geometry::VertexHandle{0})));
    double dist1 = glm::length(
        Geometry::MeshUtils::ComputeOneRingCentroid(mesh, Geometry::VertexHandle{1})
        - glm::dvec3(mesh.Position(Geometry::VertexHandle{1})));
    double dist2 = glm::length(
        Geometry::MeshUtils::ComputeOneRingCentroid(mesh, Geometry::VertexHandle{2})
        - glm::dvec3(mesh.Position(Geometry::VertexHandle{2})));
    double dist3 = glm::length(
        Geometry::MeshUtils::ComputeOneRingCentroid(mesh, Geometry::VertexHandle{3})
        - glm::dvec3(mesh.Position(Geometry::VertexHandle{3})));

    EXPECT_NEAR(dist0, dist1, 1e-6);
    EXPECT_NEAR(dist1, dist2, 1e-6);
    EXPECT_NEAR(dist2, dist3, 1e-6);
}

// =============================================================================
// Edge Loop / Edge Ring selection tests
// =============================================================================

// Helper: find the edge index connecting two vertices (or return UINT32_MAX).
static uint32_t FindEdgeBetween(const Geometry::Halfedge::Mesh& mesh,
                                Geometry::VertexHandle v0, Geometry::VertexHandle v1)
{
    for (auto he : mesh.HalfedgesAroundVertex(v0))
    {
        if (mesh.ToVertex(he).Index == v1.Index)
            return mesh.Edge(he).Index;
    }
    return UINT32_MAX;
}

// Helper: check that a vector contains no duplicate values.
static bool HasNoDuplicates(const std::vector<uint32_t>& v)
{
    std::unordered_set<uint32_t> seen(v.begin(), v.end());
    return seen.size() == v.size();
}

// --- Edge Loop (vertex-walking) tests ---

TEST(EdgeLoop, InvalidEdgeReturnsEmpty)
{
    auto mesh = MakeSingleTriangle();
    auto loop = Geometry::MeshUtils::CollectEdgeLoop(mesh, Geometry::EdgeHandle{999});
    EXPECT_TRUE(loop.empty());
}

TEST(EdgeLoop, SingleTriangleProducesPath)
{
    auto mesh = MakeSingleTriangle();
    // A single triangle has 3 vertices each with valence 2.
    // The loop walk at a valence-2 vertex picks rotation by 1 (valence/2=1),
    // which continues to the adjacent edge.
    auto loop = Geometry::MeshUtils::CollectEdgeLoop(mesh, Geometry::EdgeHandle{0});
    ASSERT_GE(loop.size(), 1u);
    ASSERT_LE(loop.size(), 3u);
    EXPECT_TRUE(HasNoDuplicates(loop));
}

TEST(EdgeLoop, QuadStripVerticalEdgeSelectsColumn)
{
    // 3-quad strip: bot v0..v3, top v4..v7
    // Vertical edges: v0-v4, v1-v5, v2-v6, v3-v7
    // A loop from vertical edge v1-v5 should walk through vertices v1 and v5
    // to select a continuous path of edges.
    auto mesh = MakeQuadStrip(3);
    uint32_t edgeIdx = FindEdgeBetween(mesh,
        Geometry::VertexHandle{1}, Geometry::VertexHandle{5});
    ASSERT_NE(edgeIdx, UINT32_MAX);

    auto loop = Geometry::MeshUtils::CollectEdgeLoop(mesh, Geometry::EdgeHandle{edgeIdx});
    EXPECT_GT(loop.size(), 1u) << "Loop should extend beyond the starting edge";
    EXPECT_TRUE(HasNoDuplicates(loop));
}

TEST(EdgeLoop, QuadStripHorizontalEdgeSelectsRow)
{
    auto mesh = MakeQuadStrip(3);
    // Pick a horizontal bottom edge: v0-v1
    uint32_t edgeIdx = FindEdgeBetween(mesh,
        Geometry::VertexHandle{0}, Geometry::VertexHandle{1});
    ASSERT_NE(edgeIdx, UINT32_MAX);

    auto loop = Geometry::MeshUtils::CollectEdgeLoop(mesh, Geometry::EdgeHandle{edgeIdx});
    // Walking through vertices along the bottom row should select at least
    // v0-v1 and one continuation edge.
    EXPECT_GE(loop.size(), 2u);
    EXPECT_TRUE(HasNoDuplicates(loop));
}

TEST(EdgeLoop, ClosedMeshNoDuplicatesOrOverflow)
{
    auto mesh = MakeIcosahedron();
    for (std::size_t i = 0; i < mesh.EdgesSize(); ++i)
    {
        auto eh = Geometry::EdgeHandle{static_cast<Geometry::PropertyIndex>(i)};
        if (mesh.IsDeleted(eh)) continue;
        auto loop = Geometry::MeshUtils::CollectEdgeLoop(mesh, eh);
        EXPECT_LE(loop.size(), mesh.EdgeCount());
        EXPECT_TRUE(HasNoDuplicates(loop));
    }
}

// --- Edge Ring (face-crossing) tests ---

TEST(EdgeRing, InvalidEdgeReturnsEmpty)
{
    auto mesh = MakeSingleTriangle();
    auto ring = Geometry::MeshUtils::CollectEdgeRing(mesh, Geometry::EdgeHandle{999});
    EXPECT_TRUE(ring.empty());
}

TEST(EdgeRing, SingleTriangleBoundaryStops)
{
    auto mesh = MakeSingleTriangle();
    // In a single triangle, one halfedge side faces the triangle, the other is
    // boundary.  The ring walk enters the face, finds the opposite edge, but
    // that edge's other side is boundary → stop after 1–2 edges.
    auto ring = Geometry::MeshUtils::CollectEdgeRing(mesh, Geometry::EdgeHandle{0});
    ASSERT_GE(ring.size(), 1u);
    ASSERT_LE(ring.size(), 3u);
    EXPECT_TRUE(HasNoDuplicates(ring));
}

TEST(EdgeRing, QuadStripVerticalEdgeSelectsAllVerticalEdges)
{
    // 3-quad strip: v0-v1-v2-v3 (bot), v4-v5-v6-v7 (top)
    // Vertical edges: v0-v4, v1-v5, v2-v6, v3-v7
    // Ring from v1-v5 should cross quads selecting opposite vertical edges.
    auto mesh = MakeQuadStrip(3);
    uint32_t edgeIdx = FindEdgeBetween(mesh,
        Geometry::VertexHandle{1}, Geometry::VertexHandle{5});
    ASSERT_NE(edgeIdx, UINT32_MAX);

    auto ring = Geometry::MeshUtils::CollectEdgeRing(mesh, Geometry::EdgeHandle{edgeIdx});

    // On a 3-quad strip, the ring from an interior vertical edge should select
    // all 4 vertical edges (the start + 3 via face-crossing).
    EXPECT_EQ(ring.size(), 4u) << "Ring should select all vertical edges in the strip";
    EXPECT_TRUE(HasNoDuplicates(ring));

    // Verify all vertical edges are present
    for (int col = 0; col <= 3; ++col)
    {
        uint32_t ve = FindEdgeBetween(mesh,
            Geometry::VertexHandle{static_cast<Geometry::PropertyIndex>(col)},
            Geometry::VertexHandle{static_cast<Geometry::PropertyIndex>(col + 4)});
        ASSERT_NE(ve, UINT32_MAX);
        bool found = false;
        for (auto ei : ring)
            if (ei == ve) found = true;
        EXPECT_TRUE(found) << "Vertical edge at column " << col << " should be in ring";
    }
}

TEST(EdgeRing, QuadStripHorizontalEdgeCrossesStripHeight)
{
    auto mesh = MakeQuadStrip(3);
    // Pick horizontal bottom edge v0-v1
    uint32_t edgeIdx = FindEdgeBetween(mesh,
        Geometry::VertexHandle{0}, Geometry::VertexHandle{1});
    ASSERT_NE(edgeIdx, UINT32_MAX);

    auto ring = Geometry::MeshUtils::CollectEdgeRing(mesh, Geometry::EdgeHandle{edgeIdx});
    // A horizontal edge's opposite in a quad is the other horizontal edge (top).
    // After crossing, we reach the boundary → ring has exactly 2 edges.
    EXPECT_EQ(ring.size(), 2u) << "Horizontal ring crosses one quad to top edge";
    EXPECT_TRUE(HasNoDuplicates(ring));
}

TEST(EdgeRing, TriangleStripProducesPath)
{
    auto mesh = MakeTriangleStrip(4);
    uint32_t edgeIdx = FindEdgeBetween(mesh,
        Geometry::VertexHandle{1}, Geometry::VertexHandle{6});
    ASSERT_NE(edgeIdx, UINT32_MAX);

    auto ring = Geometry::MeshUtils::CollectEdgeRing(mesh, Geometry::EdgeHandle{edgeIdx});
    EXPECT_GT(ring.size(), 1u) << "Ring should traverse at least one triangle";
    EXPECT_TRUE(HasNoDuplicates(ring));
}

TEST(EdgeRing, ClosedMeshNoDuplicatesOrOverflow)
{
    auto mesh = MakeIcosahedron();
    for (std::size_t i = 0; i < mesh.EdgesSize(); ++i)
    {
        auto eh = Geometry::EdgeHandle{static_cast<Geometry::PropertyIndex>(i)};
        if (mesh.IsDeleted(eh)) continue;
        auto ring = Geometry::MeshUtils::CollectEdgeRing(mesh, eh);
        EXPECT_LE(ring.size(), mesh.EdgeCount());
        EXPECT_TRUE(HasNoDuplicates(ring));
    }
}

// =============================================================================
// Edge Loop / Edge Ring — extended tests for E3e hardening
// =============================================================================

// Helper: build a mixed tri/quad fan around a central vertex.
// Creates a central vertex (v0) surrounded by `numSectors` sectors.
// Even-indexed sectors are quads, odd-indexed are triangles, producing
// an extraordinary vertex with mixed face types.
static Geometry::Halfedge::Mesh MakeMixedFanAroundVertex(int numSectors)
{
    Geometry::Halfedge::Mesh mesh;
    auto center = mesh.AddVertex({0.0f, 0.0f, 0.0f});
    const float pi2 = 2.0f * 3.14159265f;
    // Create ring vertices
    std::vector<Geometry::VertexHandle> ring;
    for (int i = 0; i < numSectors; ++i)
    {
        float angle = pi2 * static_cast<float>(i) / static_cast<float>(numSectors);
        ring.push_back(mesh.AddVertex({std::cos(angle), std::sin(angle), 0.0f}));
    }
    // Create faces: even sectors = quads (need an extra midpoint vertex), odd = tris
    for (int i = 0; i < numSectors; ++i)
    {
        int next = (i + 1) % numSectors;
        if (i % 2 == 0)
        {
            // Quad: center, ring[i], midpoint, ring[next] — but midpoint must exist
            float midAngle = pi2 * (static_cast<float>(i) + 0.5f) / static_cast<float>(numSectors);
            auto mid = mesh.AddVertex({1.5f * std::cos(midAngle), 1.5f * std::sin(midAngle), 0.0f});
            (void)mesh.AddQuad(center, ring[i], mid, ring[next]);
        }
        else
        {
            (void)mesh.AddTriangle(center, ring[i], ring[next]);
        }
    }
    return mesh;
}

// --- EdgeTraversalStrategy tests ---

TEST(EdgeLoop, StrictQuadStopsAtNonQuadVertex)
{
    // On a triangle mesh (icosahedron), all vertices have valence 5.
    // StrictQuad mode should only return the start edge.
    auto mesh = MakeIcosahedron();
    auto eh = Geometry::EdgeHandle{0};
    auto loop = Geometry::MeshUtils::CollectEdgeLoop(
        mesh, eh, Geometry::MeshUtils::EdgeTraversalStrategy::StrictQuad);
    EXPECT_EQ(loop.size(), 1u) << "StrictQuad should stop at non-valence-4 vertices";
    EXPECT_EQ(loop[0], 0u);
}

TEST(EdgeLoop, StrictQuadTraversesQuadStripInterior)
{
    // On a quad strip, boundary vertices have valence 2-3 (not 4).
    // StrictQuad stops at non-valence-4 vertices, so it produces shorter
    // loops than Permissive on boundary meshes. For an interior vertical
    // edge (v2-v8 in a 5-col strip), the endpoints are boundary vertices
    // (valence 3), so StrictQuad returns only the start edge.
    // This test verifies StrictQuad correctly restricts to valence-4 only.
    auto mesh = MakeQuadStrip(5);
    uint32_t edgeIdx = FindEdgeBetween(mesh,
        Geometry::VertexHandle{2}, Geometry::VertexHandle{8});
    ASSERT_NE(edgeIdx, UINT32_MAX);

    auto loopStrict = Geometry::MeshUtils::CollectEdgeLoop(
        mesh, Geometry::EdgeHandle{edgeIdx},
        Geometry::MeshUtils::EdgeTraversalStrategy::StrictQuad);

    // Boundary vertices have valence 3, not 4, so StrictQuad stops immediately
    EXPECT_EQ(loopStrict.size(), 1u)
        << "StrictQuad should stop at valence-3 boundary vertices";

    auto loopPermissive = Geometry::MeshUtils::CollectEdgeLoop(
        mesh, Geometry::EdgeHandle{edgeIdx},
        Geometry::MeshUtils::EdgeTraversalStrategy::Permissive);
    EXPECT_GT(loopPermissive.size(), loopStrict.size())
        << "Permissive should traverse further than StrictQuad on boundary mesh";
}

TEST(EdgeRing, StrictQuadStopsAtTriangleFace)
{
    // On a pure triangle mesh, StrictQuad ring should only return start edge.
    auto mesh = MakeIcosahedron();
    auto eh = Geometry::EdgeHandle{0};
    auto ring = Geometry::MeshUtils::CollectEdgeRing(
        mesh, eh, Geometry::MeshUtils::EdgeTraversalStrategy::StrictQuad);
    EXPECT_EQ(ring.size(), 1u) << "StrictQuad ring should stop at triangle faces";
}

TEST(EdgeRing, StrictQuadTraversesQuadStrip)
{
    auto mesh = MakeQuadStrip(4);
    uint32_t edgeIdx = FindEdgeBetween(mesh,
        Geometry::VertexHandle{2}, Geometry::VertexHandle{7});
    ASSERT_NE(edgeIdx, UINT32_MAX);

    auto ringPermissive = Geometry::MeshUtils::CollectEdgeRing(
        mesh, Geometry::EdgeHandle{edgeIdx},
        Geometry::MeshUtils::EdgeTraversalStrategy::Permissive);
    auto ringStrict = Geometry::MeshUtils::CollectEdgeRing(
        mesh, Geometry::EdgeHandle{edgeIdx},
        Geometry::MeshUtils::EdgeTraversalStrategy::StrictQuad);

    EXPECT_EQ(ringPermissive, ringStrict);
}

// --- Odd-valence / extraordinary vertex tests ---

TEST(EdgeLoop, OddValenceVertexProducesDeterministicResult)
{
    // Icosahedron vertices all have valence 5 (odd).
    // Run CollectEdgeLoop 10 times from the same edge and verify identical results.
    auto mesh = MakeIcosahedron();
    auto eh = Geometry::EdgeHandle{5};

    auto reference = Geometry::MeshUtils::CollectEdgeLoop(mesh, eh);
    ASSERT_FALSE(reference.empty());

    for (int trial = 0; trial < 10; ++trial)
    {
        auto loop = Geometry::MeshUtils::CollectEdgeLoop(mesh, eh);
        EXPECT_EQ(loop, reference) << "Trial " << trial << " diverged from reference";
    }
}

TEST(EdgeLoop, MixedTriQuadFanNoDuplicatesOrOverflow)
{
    // Mixed fan: extraordinary vertex surrounded by alternating tri/quad faces.
    auto mesh = MakeMixedFanAroundVertex(6);
    for (std::size_t i = 0; i < mesh.EdgesSize(); ++i)
    {
        auto eh = Geometry::EdgeHandle{static_cast<Geometry::PropertyIndex>(i)};
        if (mesh.IsDeleted(eh)) continue;
        auto loop = Geometry::MeshUtils::CollectEdgeLoop(mesh, eh);
        EXPECT_LE(loop.size(), mesh.EdgeCount());
        EXPECT_TRUE(HasNoDuplicates(loop));
    }
}

TEST(EdgeRing, MixedTriQuadFanNoDuplicatesOrOverflow)
{
    auto mesh = MakeMixedFanAroundVertex(6);
    for (std::size_t i = 0; i < mesh.EdgesSize(); ++i)
    {
        auto eh = Geometry::EdgeHandle{static_cast<Geometry::PropertyIndex>(i)};
        if (mesh.IsDeleted(eh)) continue;
        auto ring = Geometry::MeshUtils::CollectEdgeRing(mesh, eh);
        EXPECT_LE(ring.size(), mesh.EdgeCount());
        EXPECT_TRUE(HasNoDuplicates(ring));
    }
}

// --- Boundary-rich mesh tests ---

TEST(EdgeLoop, BoundaryMeshStopsAtBoundary)
{
    // A single quad has all boundary edges and valence-2 vertices.
    // Edge loop should be short (boundary vertices have valence 2).
    auto mesh = MakeSingleQuad();
    auto loop = Geometry::MeshUtils::CollectEdgeLoop(mesh, Geometry::EdgeHandle{0});
    ASSERT_GE(loop.size(), 1u);
    ASSERT_LE(loop.size(), mesh.EdgeCount());
    EXPECT_TRUE(HasNoDuplicates(loop));
}

TEST(EdgeRing, BoundaryMeshStopsAtBoundary)
{
    // Single quad: the ring enters the face, finds opposite edge, but other
    // side is boundary → stops after at most 2 edges.
    auto mesh = MakeSingleQuad();
    auto ring = Geometry::MeshUtils::CollectEdgeRing(mesh, Geometry::EdgeHandle{0});
    ASSERT_GE(ring.size(), 1u);
    ASSERT_LE(ring.size(), 2u);
    EXPECT_TRUE(HasNoDuplicates(ring));
}

TEST(EdgeLoop, TwoTriangleDiamondBoundary)
{
    // Diamond: 4 vertices, 2 faces. Boundary-heavy with one shared interior edge.
    auto mesh = MakeTwoTriangleDiamond();
    for (std::size_t i = 0; i < mesh.EdgesSize(); ++i)
    {
        auto eh = Geometry::EdgeHandle{static_cast<Geometry::PropertyIndex>(i)};
        if (mesh.IsDeleted(eh)) continue;
        auto loop = Geometry::MeshUtils::CollectEdgeLoop(mesh, eh);
        EXPECT_GE(loop.size(), 1u);
        EXPECT_LE(loop.size(), mesh.EdgeCount());
        EXPECT_TRUE(HasNoDuplicates(loop));
    }
}

// --- Valence-3 extraordinary vertex (subdivided triangle) ---

TEST(EdgeLoop, SubdividedTriangleValence6Interior)
{
    // The subdivided triangle has an interior vertex (v3, the midpoint)
    // with valence 6 (even). The loop should traverse cleanly through it.
    auto mesh = MakeSubdividedTriangle();
    for (std::size_t i = 0; i < mesh.EdgesSize(); ++i)
    {
        auto eh = Geometry::EdgeHandle{static_cast<Geometry::PropertyIndex>(i)};
        if (mesh.IsDeleted(eh)) continue;
        auto loop = Geometry::MeshUtils::CollectEdgeLoop(mesh, eh);
        EXPECT_GE(loop.size(), 1u);
        EXPECT_LE(loop.size(), mesh.EdgeCount());
        EXPECT_TRUE(HasNoDuplicates(loop));
    }
}

// --- Deterministic ordering guarantees ---

TEST(EdgeLoop, OrderingIsConsistentAcrossRuns)
{
    // Build a non-trivial mesh and verify that edge loop ordering is identical
    // across 20 invocations (catches any non-determinism from hash sets, etc.).
    auto mesh = MakeQuadStrip(5);
    uint32_t edgeIdx = FindEdgeBetween(mesh,
        Geometry::VertexHandle{1}, Geometry::VertexHandle{7});
    ASSERT_NE(edgeIdx, UINT32_MAX);

    auto reference = Geometry::MeshUtils::CollectEdgeLoop(
        mesh, Geometry::EdgeHandle{edgeIdx});
    for (int trial = 0; trial < 20; ++trial)
    {
        auto loop = Geometry::MeshUtils::CollectEdgeLoop(
            mesh, Geometry::EdgeHandle{edgeIdx});
        EXPECT_EQ(loop, reference) << "Ordering diverged on trial " << trial;
    }
}

TEST(EdgeRing, OrderingIsConsistentAcrossRuns)
{
    auto mesh = MakeQuadStrip(5);
    uint32_t edgeIdx = FindEdgeBetween(mesh,
        Geometry::VertexHandle{2}, Geometry::VertexHandle{8});
    ASSERT_NE(edgeIdx, UINT32_MAX);

    auto reference = Geometry::MeshUtils::CollectEdgeRing(
        mesh, Geometry::EdgeHandle{edgeIdx});
    for (int trial = 0; trial < 20; ++trial)
    {
        auto ring = Geometry::MeshUtils::CollectEdgeRing(
            mesh, Geometry::EdgeHandle{edgeIdx});
        EXPECT_EQ(ring, reference) << "Ordering diverged on trial " << trial;
    }
}

// --- Start edge is always included in result ---

TEST(EdgeLoop, AlwaysContainsStartEdge)
{
    auto mesh = MakeQuadStrip(3);
    for (std::size_t i = 0; i < mesh.EdgesSize(); ++i)
    {
        auto eh = Geometry::EdgeHandle{static_cast<Geometry::PropertyIndex>(i)};
        if (mesh.IsDeleted(eh)) continue;
        auto loop = Geometry::MeshUtils::CollectEdgeLoop(mesh, eh);
        ASSERT_FALSE(loop.empty());
        bool found = false;
        for (auto ei : loop)
            if (ei == eh.Index) found = true;
        EXPECT_TRUE(found) << "Start edge " << eh.Index << " missing from loop";
    }
}

TEST(EdgeRing, AlwaysContainsStartEdge)
{
    auto mesh = MakeQuadStrip(3);
    for (std::size_t i = 0; i < mesh.EdgesSize(); ++i)
    {
        auto eh = Geometry::EdgeHandle{static_cast<Geometry::PropertyIndex>(i)};
        if (mesh.IsDeleted(eh)) continue;
        auto ring = Geometry::MeshUtils::CollectEdgeRing(mesh, eh);
        ASSERT_FALSE(ring.empty());
        bool found = false;
        for (auto ei : ring)
            if (ei == eh.Index) found = true;
        EXPECT_TRUE(found) << "Start edge " << eh.Index << " missing from ring";
    }
}

// --- Degenerate edge (zero-length / coincident vertices) ---

TEST(EdgeLoop, DegenerateZeroLengthEdgeDoesNotCrash)
{
    // Build a triangle with two coincident vertices (zero-length edge).
    // The loop should not crash or produce NaN; it should gracefully
    // fall back to floor-rotation for the degenerate incoming direction.
    Geometry::Halfedge::Mesh mesh;
    auto v0 = mesh.AddVertex({0.0f, 0.0f, 0.0f});
    auto v1 = mesh.AddVertex({0.0f, 0.0f, 0.0f}); // coincident with v0
    auto v2 = mesh.AddVertex({1.0f, 1.0f, 0.0f});
    (void)mesh.AddTriangle(v0, v1, v2);

    for (std::size_t i = 0; i < mesh.EdgesSize(); ++i)
    {
        auto eh = Geometry::EdgeHandle{static_cast<Geometry::PropertyIndex>(i)};
        if (mesh.IsDeleted(eh)) continue;
        auto loop = Geometry::MeshUtils::CollectEdgeLoop(mesh, eh);
        EXPECT_GE(loop.size(), 1u);
        EXPECT_TRUE(HasNoDuplicates(loop));
    }
}
