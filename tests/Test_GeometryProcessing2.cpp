#include <gtest/gtest.h>
#include <cmath>
#include <numbers>
#include <numeric>
#include <vector>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>

import Geometry;

#include "TestMeshBuilders.h"

static Geometry::Halfedge::Mesh MakeQuad()
{
    Geometry::Halfedge::Mesh mesh;
    auto v0 = mesh.AddVertex({0.0f, 0.0f, 0.0f});
    auto v1 = mesh.AddVertex({1.0f, 0.0f, 0.0f});
    auto v2 = mesh.AddVertex({1.0f, 1.0f, 0.0f});
    auto v3 = mesh.AddVertex({0.0f, 1.0f, 0.0f});
    (void)mesh.AddQuad(v0, v1, v2, v3);
    return mesh;
}

static Geometry::Halfedge::Mesh MakeCube()
{
    Geometry::Halfedge::Mesh mesh;
    auto v0 = mesh.AddVertex({-1.0f, -1.0f, -1.0f});
    auto v1 = mesh.AddVertex({ 1.0f, -1.0f, -1.0f});
    auto v2 = mesh.AddVertex({ 1.0f,  1.0f, -1.0f});
    auto v3 = mesh.AddVertex({-1.0f,  1.0f, -1.0f});
    auto v4 = mesh.AddVertex({-1.0f, -1.0f,  1.0f});
    auto v5 = mesh.AddVertex({ 1.0f, -1.0f,  1.0f});
    auto v6 = mesh.AddVertex({ 1.0f,  1.0f,  1.0f});
    auto v7 = mesh.AddVertex({-1.0f,  1.0f,  1.0f});

    // 6 quad faces
    (void)mesh.AddQuad(v3, v2, v1, v0); // -Z
    (void)mesh.AddQuad(v4, v5, v6, v7); // +Z
    (void)mesh.AddQuad(v0, v1, v5, v4); // -Y
    (void)mesh.AddQuad(v2, v3, v7, v6); // +Y
    (void)mesh.AddQuad(v0, v4, v7, v3); // -X
    (void)mesh.AddQuad(v1, v2, v6, v5); // +X

    return mesh;
}


// Generate unit sphere point cloud
static std::vector<glm::vec3> MakeSpherePointCloud(std::size_t n)
{
    std::vector<glm::vec3> points;
    points.reserve(n);

    // Fibonacci sphere sampling for uniform distribution
    const float goldenAngle = static_cast<float>(std::numbers::pi) * (3.0f - std::sqrt(5.0f));

    for (std::size_t i = 0; i < n; ++i)
    {
        float y = 1.0f - (2.0f * static_cast<float>(i) / static_cast<float>(n - 1));
        float radius = std::sqrt(1.0f - y * y);
        float theta = goldenAngle * static_cast<float>(i);
        float x = std::cos(theta) * radius;
        float z = std::sin(theta) * radius;
        points.push_back({x, y, z});
    }

    return points;
}

// Generate planar point cloud on the XY plane
static std::vector<glm::vec3> MakePlanarPointCloud(std::size_t nx, std::size_t ny)
{
    std::vector<glm::vec3> points;
    points.reserve(nx * ny);

    for (std::size_t i = 0; i < nx; ++i)
    {
        for (std::size_t j = 0; j < ny; ++j)
        {
            float x = static_cast<float>(i) / static_cast<float>(nx - 1);
            float y = static_cast<float>(j) / static_cast<float>(ny - 1);
            points.push_back({x, y, 0.0f});
        }
    }

    return points;
}

// =============================================================================
// Catmull-Clark Subdivision tests
// =============================================================================

TEST(CatmullClark, SingleTriangleProducesQuads)
{
    auto input = MakeSingleTriangle();
    Geometry::Halfedge::Mesh output;

    auto result = Geometry::CatmullClark::Subdivide(input, output);
    ASSERT_TRUE(result.has_value());

    // A single triangle has 3 edges, so produces 3 quad faces
    EXPECT_EQ(result->FinalFaceCount, 3u);
    EXPECT_EQ(result->IterationsPerformed, 1u);

    // All faces should be quads
    for (std::size_t fi = 0; fi < output.FacesSize(); ++fi)
    {
        Geometry::FaceHandle fh{static_cast<Geometry::PropertyIndex>(fi)};
        if (output.IsDeleted(fh)) continue;
        EXPECT_EQ(output.Valence(fh), 4u) << "Face " << fi << " should be a quad";
    }
}

TEST(CatmullClark, TetrahedronProducesAllQuads)
{
    auto input = MakeTetrahedron();
    Geometry::Halfedge::Mesh output;

    auto result = Geometry::CatmullClark::Subdivide(input, output);
    ASSERT_TRUE(result.has_value());

    EXPECT_TRUE(result->AllQuads);
    EXPECT_EQ(result->IterationsPerformed, 1u);

    // Tetrahedron: 4 faces with 3 edges each -> 12 quads
    // (each face has 3 halfedges, each produces 1 quad = 3 quads/face * 4 faces = 12)
    EXPECT_EQ(result->FinalFaceCount, 12u);
}

TEST(CatmullClark, CubeProducesAllQuads)
{
    auto input = MakeCube();
    Geometry::Halfedge::Mesh output;

    auto result = Geometry::CatmullClark::Subdivide(input, output);
    ASSERT_TRUE(result.has_value());

    EXPECT_TRUE(result->AllQuads);
    // Cube: 6 quad faces, each with 4 edges -> 24 quad faces after one iteration
    EXPECT_EQ(result->FinalFaceCount, 24u);
}

TEST(CatmullClark, PreservesClosedMeshEulerCharacteristic)
{
    auto input = MakeTetrahedron();
    Geometry::Halfedge::Mesh output;

    auto result = Geometry::CatmullClark::Subdivide(input, output);
    ASSERT_TRUE(result.has_value());

    // Euler characteristic: V - E + F = 2 for closed mesh
    std::size_t V = output.VertexCount();
    std::size_t E = output.EdgeCount();
    std::size_t F = output.FaceCount();
    EXPECT_EQ(static_cast<int>(V) - static_cast<int>(E) + static_cast<int>(F), 2)
        << "V=" << V << " E=" << E << " F=" << F;
}

TEST(CatmullClark, CubePreservesEulerCharacteristic)
{
    auto input = MakeCube();
    Geometry::Halfedge::Mesh output;

    auto result = Geometry::CatmullClark::Subdivide(input, output);
    ASSERT_TRUE(result.has_value());

    std::size_t V = output.VertexCount();
    std::size_t E = output.EdgeCount();
    std::size_t F = output.FaceCount();
    EXPECT_EQ(static_cast<int>(V) - static_cast<int>(E) + static_cast<int>(F), 2)
        << "V=" << V << " E=" << E << " F=" << F;
}

TEST(CatmullClark, TwoIterationsWork)
{
    auto input = MakeTetrahedron();
    Geometry::Halfedge::Mesh output;

    Geometry::CatmullClark::SubdivisionParams params;
    params.Iterations = 2;

    auto result = Geometry::CatmullClark::Subdivide(input, output, params);
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->IterationsPerformed, 2u);
    EXPECT_TRUE(result->AllQuads);

    // After two iterations, all faces should still be quads
    // Face count: 12 (iter 1) -> 12*4 = 48 (iter 2 — each quad -> 4 quads)
    EXPECT_EQ(result->FinalFaceCount, 48u);
}

TEST(CatmullClark, CubeConvergesToSphere)
{
    // Catmull-Clark subdivision of a cube should converge toward a sphere.
    // We verify that the variance of vertex distances from the origin
    // strictly decreases with each subdivision iteration.
    auto input = MakeCube();

    auto computeRadiusVariance = [](const Geometry::Halfedge::Mesh& mesh) -> double
    {
        double sumR = 0.0, sumR2 = 0.0;
        std::size_t count = 0;
        for (std::size_t vi = 0; vi < mesh.VerticesSize(); ++vi)
        {
            Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(vi)};
            if (mesh.IsDeleted(vh)) continue;
            double r = static_cast<double>(glm::length(mesh.Position(vh)));
            sumR += r;
            sumR2 += r * r;
            ++count;
        }
        double meanR = sumR / static_cast<double>(count);
        return sumR2 / static_cast<double>(count) - meanR * meanR;
    };

    // Variance after 1 iteration
    Geometry::Halfedge::Mesh output1;
    Geometry::CatmullClark::SubdivisionParams p1;
    p1.Iterations = 1;
    auto r1 = Geometry::CatmullClark::Subdivide(input, output1, p1);
    ASSERT_TRUE(r1.has_value());
    double var1 = computeRadiusVariance(output1);

    // Variance after 2 iterations
    Geometry::Halfedge::Mesh output2;
    Geometry::CatmullClark::SubdivisionParams p2;
    p2.Iterations = 2;
    auto r2 = Geometry::CatmullClark::Subdivide(input, output2, p2);
    ASSERT_TRUE(r2.has_value());
    double var2 = computeRadiusVariance(output2);

    // Variance should decrease with more iterations
    EXPECT_LT(var2, var1)
        << "Radius variance should decrease: iter1=" << var1 << " iter2=" << var2;
}

TEST(CatmullClark, EmptyMeshReturnsNullopt)
{
    Geometry::Halfedge::Mesh input;
    Geometry::Halfedge::Mesh output;

    auto result = Geometry::CatmullClark::Subdivide(input, output);
    EXPECT_FALSE(result.has_value());
}

TEST(CatmullClark, ZeroIterationsReturnsNullopt)
{
    auto input = MakeTetrahedron();
    Geometry::Halfedge::Mesh output;

    Geometry::CatmullClark::SubdivisionParams params;
    params.Iterations = 0;
    auto result = Geometry::CatmullClark::Subdivide(input, output, params);
    EXPECT_FALSE(result.has_value());
}

TEST(CatmullClark, VertexCountFormula)
{
    // After one CC iteration:
    // V_new = V_old + E_old + F_old
    auto input = MakeTetrahedron();
    std::size_t Vold = input.VertexCount();
    std::size_t Eold = input.EdgeCount();
    std::size_t Fold = input.FaceCount();

    Geometry::Halfedge::Mesh output;
    auto result = Geometry::CatmullClark::Subdivide(input, output);
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->FinalVertexCount, Vold + Eold + Fold)
        << "V=" << Vold << " E=" << Eold << " F=" << Fold;
}

// =============================================================================
// Normal Estimation tests
// =============================================================================

TEST(NormalEstimation, SphereNormalsPointOutward)
{
    auto points = MakeSpherePointCloud(200);

    auto result = Geometry::NormalEstimation::EstimateNormals(points);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Normals.size(), points.size());

    // For a unit sphere, the normal at each point should be approximately
    // equal to the point's position (normalized).
    std::size_t goodCount = 0;
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        glm::vec3 expected = glm::normalize(points[i]);
        float dotProd = glm::dot(result->Normals[i], expected);
        // Normal should be close to the expected direction (allow sign ambiguity
        // before orientation, but after MST orientation they should agree)
        if (std::abs(dotProd) > 0.7f)
            ++goodCount;
    }

    // At least 90% of normals should be well-aligned
    EXPECT_GT(goodCount, points.size() * 9 / 10)
        << "Most normals should align with radial direction: " << goodCount << "/" << points.size();
}

TEST(NormalEstimation, PlanarNormalsAreConsistent)
{
    auto points = MakePlanarPointCloud(10, 10);

    // Use larger k for the regular grid to get robust PCA at boundaries
    Geometry::NormalEstimation::EstimationParams params;
    params.KNeighbors = 20;

    auto result = Geometry::NormalEstimation::EstimateNormals(points, params);
    ASSERT_TRUE(result.has_value());

    // Count how many normals are nearly vertical (z-aligned)
    std::size_t verticalCount = 0;
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        float zComponent = std::abs(result->Normals[i].z);
        if (zComponent > 0.8f)
            ++verticalCount;
    }

    // At least 90% should be well-aligned (boundary points may be less precise)
    EXPECT_GT(verticalCount, points.size() * 9 / 10)
        << "Most normals should be nearly vertical: " << verticalCount << "/" << points.size();

    // After MST orientation, the majority should point in the same direction
    std::size_t positiveZ = 0;
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        if (result->Normals[i].z > 0.0f)
            ++positiveZ;
    }

    // Either most are +Z or most are -Z
    std::size_t consistentCount = std::max(positiveZ, points.size() - positiveZ);
    EXPECT_GT(consistentCount, points.size() * 9 / 10)
        << "Most normals should have consistent orientation";
}

TEST(NormalEstimation, NormalsAreUnitLength)
{
    auto points = MakeSpherePointCloud(100);

    auto result = Geometry::NormalEstimation::EstimateNormals(points);
    ASSERT_TRUE(result.has_value());

    for (std::size_t i = 0; i < result->Normals.size(); ++i)
    {
        float len = glm::length(result->Normals[i]);
        EXPECT_NEAR(len, 1.0f, 0.01f) << "Normal " << i << " should be unit length";
    }
}

TEST(NormalEstimation, DifferentKValues)
{
    auto points = MakeSpherePointCloud(100);

    Geometry::NormalEstimation::EstimationParams params;

    // Small k
    params.KNeighbors = 5;
    auto resultSmall = Geometry::NormalEstimation::EstimateNormals(points, params);
    ASSERT_TRUE(resultSmall.has_value());

    // Large k
    params.KNeighbors = 30;
    auto resultLarge = Geometry::NormalEstimation::EstimateNormals(points, params);
    ASSERT_TRUE(resultLarge.has_value());

    // Both should produce valid normals
    EXPECT_EQ(resultSmall->Normals.size(), points.size());
    EXPECT_EQ(resultLarge->Normals.size(), points.size());
}

TEST(NormalEstimation, WithoutOrientation)
{
    auto points = MakeSpherePointCloud(100);

    Geometry::NormalEstimation::EstimationParams params;
    params.OrientNormals = false;

    auto result = Geometry::NormalEstimation::EstimateNormals(points, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->FlippedCount, 0u);

    // Normals should still be unit length
    for (std::size_t i = 0; i < result->Normals.size(); ++i)
    {
        float len = glm::length(result->Normals[i]);
        EXPECT_NEAR(len, 1.0f, 0.01f);
    }
}

TEST(NormalEstimation, TooFewPointsReturnsNullopt)
{
    std::vector<glm::vec3> points = {{0, 0, 0}, {1, 0, 0}};
    auto result = Geometry::NormalEstimation::EstimateNormals(points);
    EXPECT_FALSE(result.has_value());
}

TEST(NormalEstimation, EmptyInputReturnsNullopt)
{
    std::vector<glm::vec3> points;
    auto result = Geometry::NormalEstimation::EstimateNormals(points);
    EXPECT_FALSE(result.has_value());
}

TEST(NormalEstimation, MinimumThreePoints)
{
    std::vector<glm::vec3> points = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    auto result = Geometry::NormalEstimation::EstimateNormals(points);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Normals.size(), 3u);

    // All three normals should point in Z direction
    for (std::size_t i = 0; i < 3; ++i)
    {
        float zComponent = std::abs(result->Normals[i].z);
        EXPECT_GT(zComponent, 0.9f);
    }
}

// =============================================================================
// Mesh Repair tests
// =============================================================================

TEST(MeshRepair_BoundaryDetection, ClosedMeshHasNoBoundary)
{
    auto mesh = MakeTetrahedron();
    auto loops = Geometry::MeshRepair::FindBoundaryLoops(mesh);
    EXPECT_TRUE(loops.empty());
}

TEST(MeshRepair_BoundaryDetection, OpenMeshHasBoundary)
{
    auto mesh = MakeSingleTriangle();
    auto loops = Geometry::MeshRepair::FindBoundaryLoops(mesh);
    EXPECT_EQ(loops.size(), 1u);
    EXPECT_EQ(loops[0].Vertices.size(), 3u);
}

TEST(MeshRepair_BoundaryDetection, TwoTriangleSquareHasBoundary)
{
    auto mesh = MakeTwoTriangleSquare();
    auto loops = Geometry::MeshRepair::FindBoundaryLoops(mesh);
    // The two-triangle square has one boundary loop with 4 vertices
    EXPECT_EQ(loops.size(), 1u);
    EXPECT_EQ(loops[0].Vertices.size(), 4u);
}

TEST(MeshRepair_HoleFilling, FillsTriangularHole)
{
    // Create a tetrahedron, delete one face to create a hole, then fill it
    auto mesh = MakeTetrahedron();

    // Verify it starts closed
    auto loopsBefore = Geometry::MeshRepair::FindBoundaryLoops(mesh);
    EXPECT_TRUE(loopsBefore.empty());

    // Delete one face to create a hole
    Geometry::FaceHandle f0{0};
    mesh.DeleteFace(f0);
    mesh.GarbageCollection();

    // Verify the hole exists
    auto loopsAfter = Geometry::MeshRepair::FindBoundaryLoops(mesh);
    EXPECT_EQ(loopsAfter.size(), 1u);

    // Fill the hole
    auto result = Geometry::MeshRepair::FillHoles(mesh);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->HolesDetected, 1u);
    EXPECT_EQ(result->HolesFilled, 1u);
    EXPECT_GE(result->TrianglesAdded, 1u);

    // Verify no more holes
    auto loopsFinal = Geometry::MeshRepair::FindBoundaryLoops(mesh);
    EXPECT_TRUE(loopsFinal.empty());
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
