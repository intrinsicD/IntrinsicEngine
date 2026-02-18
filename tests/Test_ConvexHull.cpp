#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <numeric>
#include <random>
#include <vector>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>

import Geometry;

// =============================================================================
// Helpers
// =============================================================================

// Verify all input points are on or inside the hull (non-positive signed
// distance from every face plane).
static bool AllPointsInsideOrOn(
    const std::vector<glm::vec3>& points,
    const Geometry::ConvexHull& hull,
    double tolerance = 1e-6)
{
    for (const auto& p : points)
    {
        for (const auto& plane : hull.Planes)
        {
            double dist = static_cast<double>(glm::dot(plane.Normal, p)) -
                          static_cast<double>(plane.Distance);
            if (dist > tolerance)
                return false;
        }
    }
    return true;
}

// Verify that the hull satisfies Euler's formula: V - E + F = 2
static bool SatisfiesEuler(std::size_t V, std::size_t E, std::size_t F)
{
    return (static_cast<int>(V) - static_cast<int>(E) + static_cast<int>(F)) == 2;
}

// Compute the volume of a convex hull via the divergence theorem.
// Assumes triangulated faces with outward normals.
static double ConvexHullVolume(const Geometry::ConvexHull& hull,
                               const std::vector<std::array<uint32_t, 3>>& faces)
{
    // V = (1/6) * sum_faces |det(v0, v1, v2)|
    // = (1/6) * sum_faces dot(v0, cross(v1, v2))
    double volume = 0.0;
    for (const auto& face : faces)
    {
        glm::dvec3 v0 = hull.Vertices[face[0]];
        glm::dvec3 v1 = hull.Vertices[face[1]];
        glm::dvec3 v2 = hull.Vertices[face[2]];
        volume += glm::dot(v0, glm::cross(v1, v2));
    }
    return std::abs(volume) / 6.0;
}

// Generate points on a unit sphere (icosahedron-like distribution)
static std::vector<glm::vec3> MakeSpherePoints(std::size_t n, float radius = 1.0f)
{
    std::vector<glm::vec3> points;
    points.reserve(n);

    // Fibonacci sphere sampling for approximately uniform distribution
    const double goldenRatio = (1.0 + std::sqrt(5.0)) / 2.0;
    for (std::size_t i = 0; i < n; ++i)
    {
        double theta = 2.0 * std::numbers::pi * static_cast<double>(i) / goldenRatio;
        double phi = std::acos(1.0 - 2.0 * (static_cast<double>(i) + 0.5) / static_cast<double>(n));
        float x = static_cast<float>(std::cos(theta) * std::sin(phi)) * radius;
        float y = static_cast<float>(std::sin(theta) * std::sin(phi)) * radius;
        float z = static_cast<float>(std::cos(phi)) * radius;
        points.push_back({x, y, z});
    }
    return points;
}

// Generate a unit cube's 8 vertices
static std::vector<glm::vec3> MakeCubePoints()
{
    return {
        {-1.0f, -1.0f, -1.0f}, {1.0f, -1.0f, -1.0f},
        {1.0f, 1.0f, -1.0f},   {-1.0f, 1.0f, -1.0f},
        {-1.0f, -1.0f, 1.0f},  {1.0f, -1.0f, 1.0f},
        {1.0f, 1.0f, 1.0f},    {-1.0f, 1.0f, 1.0f}
    };
}

// Generate a regular tetrahedron
static std::vector<glm::vec3> MakeTetrahedronPoints()
{
    // Regular tetrahedron inscribed in unit sphere
    float a = 1.0f / 3.0f;
    float b = std::sqrt(8.0f / 9.0f);
    float c = std::sqrt(2.0f / 9.0f);
    float d = std::sqrt(2.0f / 3.0f);
    return {
        {0.0f, 0.0f, 1.0f},
        {-c, d, -a},
        {-c, -d, -a},
        {b, 0.0f, -a}
    };
}

// Generate an octahedron (6 vertices)
static std::vector<glm::vec3> MakeOctahedronPoints()
{
    return {
        {1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}
    };
}

// =============================================================================
// Degenerate / Edge Case Tests
// =============================================================================

TEST(ConvexHull_Degenerate, EmptyInputReturnsNullopt)
{
    std::vector<glm::vec3> empty;
    auto result = Geometry::ConvexHullBuilder::Build(empty);
    EXPECT_FALSE(result.has_value());
}

TEST(ConvexHull_Degenerate, SinglePointReturnsNullopt)
{
    std::vector<glm::vec3> points = {{1.0f, 2.0f, 3.0f}};
    auto result = Geometry::ConvexHullBuilder::Build(points);
    EXPECT_FALSE(result.has_value());
}

TEST(ConvexHull_Degenerate, TwoPointsReturnsNullopt)
{
    std::vector<glm::vec3> points = {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}};
    auto result = Geometry::ConvexHullBuilder::Build(points);
    EXPECT_FALSE(result.has_value());
}

TEST(ConvexHull_Degenerate, ThreePointsReturnsNullopt)
{
    std::vector<glm::vec3> points = {
        {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.5f, 1.0f, 0.0f}
    };
    auto result = Geometry::ConvexHullBuilder::Build(points);
    EXPECT_FALSE(result.has_value());
}

TEST(ConvexHull_Degenerate, CoincidentPointsReturnsNullopt)
{
    std::vector<glm::vec3> points(10, {5.0f, 5.0f, 5.0f});
    auto result = Geometry::ConvexHullBuilder::Build(points);
    EXPECT_FALSE(result.has_value());
}

TEST(ConvexHull_Degenerate, CollinearPointsReturnsNullopt)
{
    std::vector<glm::vec3> points;
    for (int i = 0; i < 10; ++i)
        points.push_back({static_cast<float>(i), 0.0f, 0.0f});
    auto result = Geometry::ConvexHullBuilder::Build(points);
    EXPECT_FALSE(result.has_value());
}

TEST(ConvexHull_Degenerate, CoplanarPointsReturnsNullopt)
{
    std::vector<glm::vec3> points;
    for (int i = 0; i < 5; ++i)
        for (int j = 0; j < 5; ++j)
            points.push_back({static_cast<float>(i), static_cast<float>(j), 0.0f});
    auto result = Geometry::ConvexHullBuilder::Build(points);
    EXPECT_FALSE(result.has_value());
}

// =============================================================================
// Basic Shape Tests
// =============================================================================

TEST(ConvexHull_Tetrahedron, FourPointsProduceTetrahedron)
{
    auto points = MakeTetrahedronPoints();
    auto result = Geometry::ConvexHullBuilder::Build(points);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->HullVertexCount, 4u);
    EXPECT_EQ(result->HullFaceCount, 4u);
    EXPECT_EQ(result->HullEdgeCount, 6u);
    EXPECT_EQ(result->InteriorPointCount, 0u);
    EXPECT_EQ(result->InputPointCount, 4u);

    EXPECT_TRUE(SatisfiesEuler(result->HullVertexCount,
                               result->HullEdgeCount,
                               result->HullFaceCount));
    EXPECT_TRUE(AllPointsInsideOrOn(points, result->Hull));
}

TEST(ConvexHull_Tetrahedron, PlanesHaveOutwardNormals)
{
    auto points = MakeTetrahedronPoints();
    auto result = Geometry::ConvexHullBuilder::Build(points);
    ASSERT_TRUE(result.has_value());

    // Centroid should be strictly inside all planes
    glm::vec3 centroid{0.0f, 0.0f, 0.0f};
    for (const auto& p : points)
        centroid += p;
    centroid /= static_cast<float>(points.size());

    for (const auto& plane : result->Hull.Planes)
    {
        double dist = static_cast<double>(glm::dot(plane.Normal, centroid)) -
                      static_cast<double>(plane.Distance);
        EXPECT_LT(dist, 0.0) << "Centroid should be inside all hull planes";
    }
}

TEST(ConvexHull_Cube, EightVerticesTwelveFaces)
{
    auto points = MakeCubePoints();
    auto result = Geometry::ConvexHullBuilder::Build(points);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->HullVertexCount, 8u);
    // Cube triangulated: 6 quads × 2 triangles = 12 triangles
    EXPECT_EQ(result->HullFaceCount, 12u);
    EXPECT_EQ(result->InteriorPointCount, 0u);

    EXPECT_TRUE(SatisfiesEuler(result->HullVertexCount,
                               result->HullEdgeCount,
                               result->HullFaceCount));
    EXPECT_TRUE(AllPointsInsideOrOn(points, result->Hull));
}

TEST(ConvexHull_Octahedron, SixVerticesEightFaces)
{
    auto points = MakeOctahedronPoints();
    auto result = Geometry::ConvexHullBuilder::Build(points);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->HullVertexCount, 6u);
    EXPECT_EQ(result->HullFaceCount, 8u);
    EXPECT_EQ(result->HullEdgeCount, 12u);
    EXPECT_EQ(result->InteriorPointCount, 0u);

    EXPECT_TRUE(SatisfiesEuler(result->HullVertexCount,
                               result->HullEdgeCount,
                               result->HullFaceCount));
    EXPECT_TRUE(AllPointsInsideOrOn(points, result->Hull));
}

// =============================================================================
// Interior Points Tests
// =============================================================================

TEST(ConvexHull_Interior, CubeWithInteriorPoints)
{
    auto points = MakeCubePoints();

    // Add some interior points
    points.push_back({0.0f, 0.0f, 0.0f});   // centroid
    points.push_back({0.5f, 0.5f, 0.5f});
    points.push_back({-0.5f, -0.5f, -0.5f});
    points.push_back({0.1f, -0.3f, 0.7f});

    auto result = Geometry::ConvexHullBuilder::Build(points);
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->HullVertexCount, 8u);
    EXPECT_EQ(result->InteriorPointCount, 4u);
    EXPECT_TRUE(AllPointsInsideOrOn(points, result->Hull));
}

TEST(ConvexHull_Interior, SphereWithCentroid)
{
    auto points = MakeSpherePoints(50);
    points.push_back({0.0f, 0.0f, 0.0f}); // centroid is interior

    auto result = Geometry::ConvexHullBuilder::Build(points);
    ASSERT_TRUE(result.has_value());

    EXPECT_TRUE(AllPointsInsideOrOn(points, result->Hull));
    EXPECT_GT(result->InteriorPointCount, 0u);
}

// =============================================================================
// Sphere Tests
// =============================================================================

TEST(ConvexHull_Sphere, AllSurfacePointsOnHull)
{
    // All points on a sphere should be hull vertices (for small n)
    auto points = MakeSpherePoints(20);
    auto result = Geometry::ConvexHullBuilder::Build(points);

    ASSERT_TRUE(result.has_value());
    // All 20 points should be on the hull since they're on a sphere
    EXPECT_EQ(result->HullVertexCount, 20u);
    EXPECT_EQ(result->InteriorPointCount, 0u);
    EXPECT_TRUE(SatisfiesEuler(result->HullVertexCount,
                               result->HullEdgeCount,
                               result->HullFaceCount));
    EXPECT_TRUE(AllPointsInsideOrOn(points, result->Hull));
}

TEST(ConvexHull_Sphere, LargerSphereConverges)
{
    auto points = MakeSpherePoints(200);
    auto result = Geometry::ConvexHullBuilder::Build(points);

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(AllPointsInsideOrOn(points, result->Hull));
    EXPECT_TRUE(SatisfiesEuler(result->HullVertexCount,
                               result->HullEdgeCount,
                               result->HullFaceCount));
}

// =============================================================================
// Random Point Cloud Tests
// =============================================================================

TEST(ConvexHull_Random, UniformCubeDistribution)
{
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(-10.0f, 10.0f);

    std::vector<glm::vec3> points(100);
    for (auto& p : points)
        p = {dist(rng), dist(rng), dist(rng)};

    auto result = Geometry::ConvexHullBuilder::Build(points);
    ASSERT_TRUE(result.has_value());

    EXPECT_GT(result->HullVertexCount, 4u);
    EXPECT_GT(result->HullFaceCount, 4u);
    EXPECT_GT(result->InteriorPointCount, 0u);
    EXPECT_TRUE(SatisfiesEuler(result->HullVertexCount,
                               result->HullEdgeCount,
                               result->HullFaceCount));
    EXPECT_TRUE(AllPointsInsideOrOn(points, result->Hull));
}

TEST(ConvexHull_Random, GaussianDistribution)
{
    std::mt19937 rng(123);
    std::normal_distribution<float> dist(0.0f, 5.0f);

    std::vector<glm::vec3> points(150);
    for (auto& p : points)
        p = {dist(rng), dist(rng), dist(rng)};

    auto result = Geometry::ConvexHullBuilder::Build(points);
    ASSERT_TRUE(result.has_value());

    EXPECT_TRUE(AllPointsInsideOrOn(points, result->Hull));
    EXPECT_TRUE(SatisfiesEuler(result->HullVertexCount,
                               result->HullEdgeCount,
                               result->HullFaceCount));
}

TEST(ConvexHull_Random, LargePointCloud)
{
    std::mt19937 rng(999);
    std::uniform_real_distribution<float> dist(-100.0f, 100.0f);

    std::vector<glm::vec3> points(1000);
    for (auto& p : points)
        p = {dist(rng), dist(rng), dist(rng)};

    auto result = Geometry::ConvexHullBuilder::Build(points);
    ASSERT_TRUE(result.has_value());

    EXPECT_TRUE(AllPointsInsideOrOn(points, result->Hull));
    EXPECT_TRUE(SatisfiesEuler(result->HullVertexCount,
                               result->HullEdgeCount,
                               result->HullFaceCount));
}

// =============================================================================
// H-Rep (Planes) Tests
// =============================================================================

TEST(ConvexHull_HRep, PlaneCountMatchesFaceCount)
{
    auto points = MakeCubePoints();
    auto result = Geometry::ConvexHullBuilder::Build(points);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Hull.Planes.size(), result->HullFaceCount);
}

TEST(ConvexHull_HRep, PlanesAreNormalized)
{
    auto points = MakeOctahedronPoints();
    auto result = Geometry::ConvexHullBuilder::Build(points);

    ASSERT_TRUE(result.has_value());
    for (const auto& plane : result->Hull.Planes)
    {
        float len = glm::length(plane.Normal);
        EXPECT_NEAR(len, 1.0f, 1e-5f) << "Plane normal should be unit length";
    }
}

TEST(ConvexHull_HRep, SkipPlanesWhenDisabled)
{
    auto points = MakeTetrahedronPoints();
    Geometry::ConvexHullBuilder::ConvexHullParams params;
    params.ComputePlanes = false;

    auto result = Geometry::ConvexHullBuilder::Build(points, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->Hull.Planes.empty());
}

// =============================================================================
// Mesh Output Tests
// =============================================================================

TEST(ConvexHull_Mesh, BuildMeshProducesValidHalfedgeMesh)
{
    auto points = MakeTetrahedronPoints();
    Geometry::ConvexHullBuilder::ConvexHullParams params;
    params.BuildMesh = true;

    auto result = Geometry::ConvexHullBuilder::Build(points, params);
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->Mesh.VertexCount(), 4u);
    EXPECT_EQ(result->Mesh.FaceCount(), 4u);
    EXPECT_FALSE(result->Mesh.IsEmpty());
}

TEST(ConvexHull_Mesh, CubeMeshTopology)
{
    auto points = MakeCubePoints();
    Geometry::ConvexHullBuilder::ConvexHullParams params;
    params.BuildMesh = true;

    auto result = Geometry::ConvexHullBuilder::Build(points, params);
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->Mesh.VertexCount(), 8u);
    EXPECT_EQ(result->Mesh.FaceCount(), 12u);

    // Euler characteristic for closed mesh: V - E + F = 2
    int chi = static_cast<int>(result->Mesh.VertexCount()) -
              static_cast<int>(result->Mesh.EdgeCount()) +
              static_cast<int>(result->Mesh.FaceCount());
    EXPECT_EQ(chi, 2) << "V=" << result->Mesh.VertexCount()
                       << " E=" << result->Mesh.EdgeCount()
                       << " F=" << result->Mesh.FaceCount();
}

TEST(ConvexHull_Mesh, MeshNotBuiltByDefault)
{
    auto points = MakeOctahedronPoints();
    auto result = Geometry::ConvexHullBuilder::Build(points);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->Mesh.IsEmpty());
}

// =============================================================================
// BuildFromMesh Tests
// =============================================================================

TEST(ConvexHull_FromMesh, TetrahedronMesh)
{
    Geometry::Halfedge::Mesh mesh;
    auto v0 = mesh.AddVertex({0.0f, 0.0f, 0.0f});
    auto v1 = mesh.AddVertex({1.0f, 0.0f, 0.0f});
    auto v2 = mesh.AddVertex({0.5f, 1.0f, 0.0f});
    auto v3 = mesh.AddVertex({0.5f, 0.5f, 1.0f});
    (void)mesh.AddTriangle(v0, v1, v2);
    (void)mesh.AddTriangle(v0, v3, v1);
    (void)mesh.AddTriangle(v1, v3, v2);
    (void)mesh.AddTriangle(v0, v2, v3);

    auto result = Geometry::ConvexHullBuilder::BuildFromMesh(mesh);
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->HullVertexCount, 4u);
    EXPECT_EQ(result->HullFaceCount, 4u);
    EXPECT_TRUE(SatisfiesEuler(result->HullVertexCount,
                               result->HullEdgeCount,
                               result->HullFaceCount));
}

TEST(ConvexHull_FromMesh, EmptyMeshReturnsNullopt)
{
    Geometry::Halfedge::Mesh mesh;
    auto result = Geometry::ConvexHullBuilder::BuildFromMesh(mesh);
    EXPECT_FALSE(result.has_value());
}

// =============================================================================
// Robustness Tests
// =============================================================================

TEST(ConvexHull_Robustness, DuplicatePoints)
{
    auto points = MakeCubePoints();
    // Add duplicates of each vertex
    auto duplicated = points;
    duplicated.insert(duplicated.end(), points.begin(), points.end());
    duplicated.insert(duplicated.end(), points.begin(), points.end());

    auto result = Geometry::ConvexHullBuilder::Build(duplicated);
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->HullVertexCount, 8u);
    EXPECT_TRUE(AllPointsInsideOrOn(duplicated, result->Hull));
}

TEST(ConvexHull_Robustness, NearlyCoplanarWithOffset)
{
    // Points in a grid with a very small z-offset for some
    std::vector<glm::vec3> points;
    for (int i = 0; i < 5; ++i)
    {
        for (int j = 0; j < 5; ++j)
        {
            points.push_back({static_cast<float>(i), static_cast<float>(j), 0.0f});
        }
    }
    // Add two points with z-offset to make it 3D
    points.push_back({2.5f, 2.5f, 0.1f});
    points.push_back({2.5f, 2.5f, -0.1f});

    auto result = Geometry::ConvexHullBuilder::Build(points);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(AllPointsInsideOrOn(points, result->Hull));
}

TEST(ConvexHull_Robustness, PointsOnHullEdges)
{
    // Cube with extra points on edges (midpoints)
    auto points = MakeCubePoints();

    // Add midpoints of all 12 cube edges
    for (std::size_t i = 0; i < 8; ++i)
    {
        for (std::size_t j = i + 1; j < 8; ++j)
        {
            glm::vec3 mid = (points[i] + points[j]) * 0.5f;
            // Only add if it's actually on a cube edge (differs in exactly 1 coord)
            glm::vec3 diff = glm::abs(points[i] - points[j]);
            int diffCount = (diff.x > 0.5f ? 1 : 0) +
                            (diff.y > 0.5f ? 1 : 0) +
                            (diff.z > 0.5f ? 1 : 0);
            if (diffCount == 1)
                points.push_back(mid);
        }
    }

    auto result = Geometry::ConvexHullBuilder::Build(points);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(AllPointsInsideOrOn(points, result->Hull));
}

TEST(ConvexHull_Robustness, ScaledCoordinates)
{
    // Test with large coordinates
    auto points = MakeOctahedronPoints();
    for (auto& p : points)
        p *= 1000.0f;

    auto result = Geometry::ConvexHullBuilder::Build(points);
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->HullVertexCount, 6u);
    EXPECT_EQ(result->HullFaceCount, 8u);
    EXPECT_TRUE(AllPointsInsideOrOn(points, result->Hull));
}

TEST(ConvexHull_Robustness, SmallCoordinates)
{
    // Test with very small coordinates
    auto points = MakeOctahedronPoints();
    for (auto& p : points)
        p *= 0.001f;

    Geometry::ConvexHullBuilder::ConvexHullParams params;
    params.DistanceEpsilon = 1e-12;

    auto result = Geometry::ConvexHullBuilder::Build(points, params);
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->HullVertexCount, 6u);
    EXPECT_TRUE(AllPointsInsideOrOn(points, result->Hull, 1e-9));
}

// =============================================================================
// Convexity Verification
// =============================================================================

TEST(ConvexHull_Convexity, AllFaceNormalsPointOutward)
{
    std::mt19937 rng(77);
    std::uniform_real_distribution<float> dist(-5.0f, 5.0f);

    std::vector<glm::vec3> points(80);
    for (auto& p : points)
        p = {dist(rng), dist(rng), dist(rng)};

    auto result = Geometry::ConvexHullBuilder::Build(points);
    ASSERT_TRUE(result.has_value());

    // Every hull vertex should be on the non-positive side of every plane
    for (const auto& v : result->Hull.Vertices)
    {
        for (const auto& plane : result->Hull.Planes)
        {
            double d = static_cast<double>(glm::dot(plane.Normal, v)) -
                       static_cast<double>(plane.Distance);
            EXPECT_LE(d, 1e-5)
                << "Hull vertex should be inside or on all planes";
        }
    }
}

// =============================================================================
// Diagnostic Fields
// =============================================================================

TEST(ConvexHull_Diagnostics, InputPointCountCorrect)
{
    std::vector<glm::vec3> points = MakeSpherePoints(37);
    auto result = Geometry::ConvexHullBuilder::Build(points);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->InputPointCount, 37u);
}

TEST(ConvexHull_Diagnostics, InteriorPlusHullEqualsInput)
{
    std::mt19937 rng(55);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

    std::vector<glm::vec3> points(50);
    for (auto& p : points)
        p = {dist(rng), dist(rng), dist(rng)};

    auto result = Geometry::ConvexHullBuilder::Build(points);
    ASSERT_TRUE(result.has_value());

    // Hull vertices + interior points = input points
    // Note: points on hull edges/faces (but not vertices) are counted as
    // interior because they're not hull vertices. So this is approximate.
    EXPECT_EQ(result->HullVertexCount + result->InteriorPointCount,
              result->InputPointCount);
}
