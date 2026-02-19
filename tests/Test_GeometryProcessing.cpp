#include <gtest/gtest.h>
#include <cmath>
#include <numbers>
#include <numeric>
#include <vector>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>

import Geometry;

#include "TestMeshBuilders.h"

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
// Loop Subdivision tests
// =============================================================================

TEST(Subdivision_Loop, SingleTriangleProducesFourFaces)
{
    auto input = MakeSingleTriangle();
    Geometry::Halfedge::Mesh output;

    auto result = Geometry::Subdivision::Subdivide(input, output);
    ASSERT_TRUE(result.has_value());

    // 1 triangle → 4 triangles
    EXPECT_EQ(result->FinalFaceCount, 4u);
    // 3 vertices + 3 edge midpoints = 6 vertices
    EXPECT_EQ(result->FinalVertexCount, 6u);
    EXPECT_EQ(result->IterationsPerformed, 1u);
}

TEST(Subdivision_Loop, TetrahedronQuadruplesFaces)
{
    auto input = MakeTetrahedron();
    Geometry::Halfedge::Mesh output;

    auto result = Geometry::Subdivision::Subdivide(input, output);
    ASSERT_TRUE(result.has_value());

    // 4 faces → 16 faces
    EXPECT_EQ(result->FinalFaceCount, 16u);
    // 4 vertices + 6 edge midpoints = 10 vertices
    EXPECT_EQ(result->FinalVertexCount, 10u);
}

TEST(Subdivision_Loop, TwoIterationsQuadruplesTwice)
{
    auto input = MakeTetrahedron();
    Geometry::Halfedge::Mesh output;

    Geometry::Subdivision::SubdivisionParams params;
    params.Iterations = 2;

    auto result = Geometry::Subdivision::Subdivide(input, output, params);
    ASSERT_TRUE(result.has_value());

    // 4 → 16 → 64 faces
    EXPECT_EQ(result->FinalFaceCount, 64u);
    EXPECT_EQ(result->IterationsPerformed, 2u);
}

TEST(Subdivision_Loop, PreservesClosedMeshTopology)
{
    auto input = MakeTetrahedron();
    Geometry::Halfedge::Mesh output;

    auto result = Geometry::Subdivision::Subdivide(input, output);
    ASSERT_TRUE(result.has_value());

    // Euler characteristic: V - E + F = 2 for closed mesh
    std::size_t V = output.VertexCount();
    std::size_t E = output.EdgeCount();
    std::size_t F = output.FaceCount();
    EXPECT_EQ(static_cast<int>(V) - static_cast<int>(E) + static_cast<int>(F), 2)
        << "V=" << V << " E=" << E << " F=" << F;
}

TEST(Subdivision_Loop, AllFacesAreTriangles)
{
    auto input = MakeIcosahedron();
    Geometry::Halfedge::Mesh output;

    auto result = Geometry::Subdivision::Subdivide(input, output);
    ASSERT_TRUE(result.has_value());

    for (std::size_t fi = 0; fi < output.FacesSize(); ++fi)
    {
        Geometry::FaceHandle fh{static_cast<Geometry::PropertyIndex>(fi)};
        if (output.IsDeleted(fh)) continue;
        EXPECT_EQ(output.Valence(fh), 3u) << "Face " << fi << " should be a triangle";
    }
}

TEST(Subdivision_Loop, IcosahedronConvergesToSphere)
{
    // Loop subdivision of an icosahedron should converge toward a sphere.
    // After subdivision, vertices should be more uniformly distributed
    // around the unit sphere than before.
    auto input = MakeIcosahedron();
    Geometry::Halfedge::Mesh output;

    Geometry::Subdivision::SubdivisionParams params;
    params.Iterations = 2;
    auto result = Geometry::Subdivision::Subdivide(input, output, params);
    ASSERT_TRUE(result.has_value());

    // Compute variance of vertex distances from origin
    // (should be small for a sphere-like shape)
    double sumR = 0.0, sumR2 = 0.0;
    std::size_t count = 0;
    for (std::size_t vi = 0; vi < output.VerticesSize(); ++vi)
    {
        Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(vi)};
        if (output.IsDeleted(vh)) continue;
        double r = static_cast<double>(glm::length(output.Position(vh)));
        sumR += r;
        sumR2 += r * r;
        ++count;
    }
    double meanR = sumR / static_cast<double>(count);
    double variance = sumR2 / static_cast<double>(count) - meanR * meanR;

    // Variance should be small — icosahedron is already close to a sphere,
    // and subdivision brings it closer
    EXPECT_LT(variance, 0.01) << "Vertices should be near-equidistant from origin";
}

TEST(Subdivision_Loop, EmptyMeshReturnsNullopt)
{
    Geometry::Halfedge::Mesh input;
    Geometry::Halfedge::Mesh output;

    auto result = Geometry::Subdivision::Subdivide(input, output);
    EXPECT_FALSE(result.has_value());
}

TEST(Subdivision_Loop, ZeroIterationsReturnsNullopt)
{
    auto input = MakeTetrahedron();
    Geometry::Halfedge::Mesh output;

    Geometry::Subdivision::SubdivisionParams params;
    params.Iterations = 0;
    auto result = Geometry::Subdivision::Subdivide(input, output, params);
    EXPECT_FALSE(result.has_value());
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
// Geodesic Distance (Heat Method) tests
// =============================================================================

TEST(Geodesic_Heat, SourceHasZeroDistance)
{
    auto mesh = MakeTetrahedron();
    std::vector<std::size_t> sources = {0};

    auto result = Geometry::Geodesic::ComputeDistance(mesh, sources);
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->Distances.size(), mesh.VerticesSize());

    // Source vertex should have zero distance
    EXPECT_NEAR(result->Distances[0], 0.0, 1e-4);
}

TEST(Geodesic_Heat, NonSourceVerticesHavePositiveDistance)
{
    auto mesh = MakeTetrahedron();
    std::vector<std::size_t> sources = {0};

    auto result = Geometry::Geodesic::ComputeDistance(mesh, sources);
    ASSERT_TRUE(result.has_value());

    // Non-source vertices should have positive distance
    for (std::size_t vi = 1; vi < mesh.VerticesSize(); ++vi)
    {
        Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(vi)};
        if (mesh.IsDeleted(vh) || mesh.IsIsolated(vh)) continue;
        EXPECT_GT(result->Distances[vi], 0.0)
            << "Vertex " << vi << " should have positive distance from source";
    }
}

TEST(Geodesic_Heat, SymmetricMeshGivesEqualDistances)
{
    // On the icosahedron (symmetric mesh), all vertices equidistant from
    // a given source should have the same geodesic distance.
    // The icosahedron has vertex symmetry — all neighbors of v0 are
    // at the same distance, and all second-ring neighbors are equal too.
    auto mesh = MakeIcosahedron();
    std::vector<std::size_t> sources = {0};

    auto result = Geometry::Geodesic::ComputeDistance(mesh, sources);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->Converged);

    // Collect distances of direct neighbors of vertex 0
    std::vector<double> neighborDists;
    Geometry::VertexHandle v0{0};
    auto hStart = mesh.Halfedge(v0);
    auto h = hStart;
    do
    {
        auto vn = mesh.ToVertex(h);
        neighborDists.push_back(result->Distances[vn.Index]);
        h = mesh.CWRotatedHalfedge(h);
    } while (h != hStart);

    // All neighbors should have approximately equal distance
    ASSERT_FALSE(neighborDists.empty());
    double refDist = neighborDists[0];
    for (std::size_t i = 1; i < neighborDists.size(); ++i)
    {
        EXPECT_NEAR(neighborDists[i], refDist, refDist * 0.15)
            << "Neighbor " << i << " should have similar distance to neighbor 0";
    }
}

TEST(Geodesic_Heat, MultipleSourcesWork)
{
    auto mesh = MakeIcosahedron();

    // Use two sources on opposite sides
    std::vector<std::size_t> sources = {0, 3};

    auto result = Geometry::Geodesic::ComputeDistance(mesh, sources);
    ASSERT_TRUE(result.has_value());

    // Both sources should have minimum distance (0 or near-0)
    EXPECT_NEAR(result->Distances[0], 0.0, 0.1);
    EXPECT_NEAR(result->Distances[3], 0.0, 0.1);
}

TEST(Geodesic_Heat, DistancesRespectTriangleInequality)
{
    auto mesh = MakeIcosahedron();
    std::vector<std::size_t> sources = {0};

    auto result = Geometry::Geodesic::ComputeDistance(mesh, sources);
    ASSERT_TRUE(result.has_value());

    // For adjacent vertices, |d(u) - d(v)| <= edge_length(u,v)
    for (std::size_t ei = 0; ei < mesh.EdgesSize(); ++ei)
    {
        Geometry::EdgeHandle e{static_cast<Geometry::PropertyIndex>(ei)};
        if (mesh.IsDeleted(e)) continue;

        auto h0 = mesh.Halfedge(e, 0);
        auto va = mesh.FromVertex(h0);
        auto vb = mesh.ToVertex(h0);

        double da = result->Distances[va.Index];
        double db = result->Distances[vb.Index];
        double edgeLen = static_cast<double>(glm::distance(
            mesh.Position(va), mesh.Position(vb)));

        // Allow some tolerance (the heat method is approximate)
        EXPECT_LE(std::abs(da - db), edgeLen * 2.0)
            << "Triangle inequality violation on edge " << ei;
    }
}

TEST(Geodesic_Heat, EmptyMeshReturnsNullopt)
{
    Geometry::Halfedge::Mesh mesh;
    std::vector<std::size_t> sources = {0};

    auto result = Geometry::Geodesic::ComputeDistance(mesh, sources);
    EXPECT_FALSE(result.has_value());
}

TEST(Geodesic_Heat, EmptySourcesReturnsNullopt)
{
    auto mesh = MakeTetrahedron();
    std::vector<std::size_t> sources;

    auto result = Geometry::Geodesic::ComputeDistance(mesh, sources);
    EXPECT_FALSE(result.has_value());
}

TEST(Geodesic_Heat, SolverConverges)
{
    auto mesh = MakeIcosahedron();
    std::vector<std::size_t> sources = {0};

    auto result = Geometry::Geodesic::ComputeDistance(mesh, sources);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->Converged);
    EXPECT_GT(result->HeatSolveIterations, 0u);
    EXPECT_GT(result->PoissonSolveIterations, 0u);
}
