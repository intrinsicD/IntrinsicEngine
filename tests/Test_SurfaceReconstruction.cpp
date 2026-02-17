#include <gtest/gtest.h>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <numeric>
#include <vector>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>

import Geometry;

// =============================================================================
// Helper: generate unit sphere point cloud (Fibonacci sampling)
// =============================================================================

static std::vector<glm::vec3> MakeSpherePoints(std::size_t n, float radius = 1.0f)
{
    std::vector<glm::vec3> points;
    points.reserve(n);

    const float goldenAngle = static_cast<float>(std::numbers::pi) * (3.0f - std::sqrt(5.0f));

    for (std::size_t i = 0; i < n; ++i)
    {
        float y = 1.0f - (2.0f * static_cast<float>(i) / static_cast<float>(n - 1));
        float r = std::sqrt(1.0f - y * y);
        float theta = goldenAngle * static_cast<float>(i);
        float x = std::cos(theta) * r;
        float z = std::sin(theta) * r;
        points.push_back(glm::vec3(x, y, z) * radius);
    }

    return points;
}

// =============================================================================
// Helper: generate sphere normals (analytical, outward-pointing)
// =============================================================================

static std::vector<glm::vec3> MakeSphereNormals(const std::vector<glm::vec3>& points)
{
    std::vector<glm::vec3> normals;
    normals.reserve(points.size());
    for (const auto& p : points)
    {
        float len = glm::length(p);
        if (len > 1e-8f)
            normals.push_back(p / len);
        else
            normals.push_back({0.0f, 1.0f, 0.0f});
    }
    return normals;
}

// =============================================================================
// Helper: create a scalar grid from a sphere SDF
// =============================================================================

static Geometry::MarchingCubes::ScalarGrid MakeSphereSDF(
    std::size_t nx, std::size_t ny, std::size_t nz,
    float radius = 1.0f,
    glm::vec3 center = {0.0f, 0.0f, 0.0f})
{
    Geometry::MarchingCubes::ScalarGrid grid;
    grid.NX = nx;
    grid.NY = ny;
    grid.NZ = nz;

    float extent = radius * 2.0f;
    grid.Origin = center - glm::vec3(extent);
    grid.Spacing = glm::vec3(2.0f * extent / static_cast<float>(nx));

    grid.Values.resize((nx + 1) * (ny + 1) * (nz + 1));

    for (std::size_t z = 0; z <= nz; ++z)
        for (std::size_t y = 0; y <= ny; ++y)
            for (std::size_t x = 0; x <= nx; ++x)
            {
                glm::vec3 pos = grid.VertexPosition(x, y, z);
                float dist = glm::length(pos - center) - radius;
                grid.Set(x, y, z, dist);
            }

    return grid;
}

// =============================================================================
// Helper: create a scalar grid from a plane SDF (z = 0)
// =============================================================================

static Geometry::MarchingCubes::ScalarGrid MakePlaneSDF(
    std::size_t nx, std::size_t ny, std::size_t nz)
{
    Geometry::MarchingCubes::ScalarGrid grid;
    grid.NX = nx;
    grid.NY = ny;
    grid.NZ = nz;
    grid.Origin = {-1.0f, -1.0f, -1.0f};
    grid.Spacing = glm::vec3(2.0f / static_cast<float>(nx));

    grid.Values.resize((nx + 1) * (ny + 1) * (nz + 1));

    for (std::size_t z = 0; z <= nz; ++z)
        for (std::size_t y = 0; y <= ny; ++y)
            for (std::size_t x = 0; x <= nx; ++x)
            {
                glm::vec3 pos = grid.VertexPosition(x, y, z);
                grid.Set(x, y, z, pos.z);
            }

    return grid;
}

// =============================================================================
// Marching Cubes — Basic Tests
// =============================================================================

TEST(MarchingCubes, InvalidGridReturnsNullopt)
{
    Geometry::MarchingCubes::ScalarGrid grid;
    grid.NX = 0;
    grid.NY = 0;
    grid.NZ = 0;

    auto result = Geometry::MarchingCubes::Extract(grid);
    EXPECT_FALSE(result.has_value());
}

TEST(MarchingCubes, EmptyGridReturnsNullopt)
{
    // Grid where all values are above the isovalue (all outside)
    Geometry::MarchingCubes::ScalarGrid grid;
    grid.NX = 2;
    grid.NY = 2;
    grid.NZ = 2;
    grid.Origin = {0.0f, 0.0f, 0.0f};
    grid.Spacing = {1.0f, 1.0f, 1.0f};
    grid.Values.assign(3 * 3 * 3, 1.0f); // all positive = all outside

    auto result = Geometry::MarchingCubes::Extract(grid);
    EXPECT_FALSE(result.has_value());
}

TEST(MarchingCubes, AllInsideReturnsNullopt)
{
    // Grid where all values are below the isovalue (all inside)
    Geometry::MarchingCubes::ScalarGrid grid;
    grid.NX = 2;
    grid.NY = 2;
    grid.NZ = 2;
    grid.Origin = {0.0f, 0.0f, 0.0f};
    grid.Spacing = {1.0f, 1.0f, 1.0f};
    grid.Values.assign(3 * 3 * 3, -1.0f); // all negative = all inside

    auto result = Geometry::MarchingCubes::Extract(grid);
    EXPECT_FALSE(result.has_value());
}

TEST(MarchingCubes, SphereSDF_ProducesTriangles)
{
    auto grid = MakeSphereSDF(10, 10, 10);

    auto result = Geometry::MarchingCubes::Extract(grid);
    ASSERT_TRUE(result.has_value());
    EXPECT_GT(result->TriangleCount, 0u);
    EXPECT_GT(result->VertexCount, 0u);
    EXPECT_EQ(result->Vertices.size(), result->VertexCount);
    EXPECT_EQ(result->Triangles.size(), result->TriangleCount);
}

TEST(MarchingCubes, SphereSDF_NormalsComputed)
{
    auto grid = MakeSphereSDF(10, 10, 10);

    Geometry::MarchingCubes::MarchingCubesParams params;
    params.ComputeNormals = true;

    auto result = Geometry::MarchingCubes::Extract(grid, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Normals.size(), result->VertexCount);

    // Normals should be approximately unit length
    for (std::size_t i = 0; i < result->Normals.size(); ++i)
    {
        float len = glm::length(result->Normals[i]);
        EXPECT_NEAR(len, 1.0f, 0.01f) << "Normal " << i << " should be unit length";
    }
}

TEST(MarchingCubes, SphereSDF_NormalsPointOutward)
{
    auto grid = MakeSphereSDF(16, 16, 16);

    auto result = Geometry::MarchingCubes::Extract(grid);
    ASSERT_TRUE(result.has_value());

    // For a centered sphere, normals should roughly point away from origin
    std::size_t outwardCount = 0;
    for (std::size_t i = 0; i < result->VertexCount; ++i)
    {
        glm::vec3 radial = glm::normalize(result->Vertices[i]);
        float dot = glm::dot(result->Normals[i], radial);
        if (dot > 0.0f)
            ++outwardCount;
    }

    // At least 90% should point outward
    EXPECT_GT(outwardCount, result->VertexCount * 9 / 10);
}

TEST(MarchingCubes, SphereSDF_VerticesNearSurface)
{
    const float radius = 1.0f;
    auto grid = MakeSphereSDF(20, 20, 20, radius);

    auto result = Geometry::MarchingCubes::Extract(grid);
    ASSERT_TRUE(result.has_value());

    // All vertices should be close to the sphere surface (distance ~= radius)
    for (std::size_t i = 0; i < result->VertexCount; ++i)
    {
        float r = glm::length(result->Vertices[i]);
        EXPECT_NEAR(r, radius, 0.3f) << "Vertex " << i << " should be near the sphere surface";
    }
}

TEST(MarchingCubes, SphereSDF_HigherResolutionMoreTriangles)
{
    auto gridLow = MakeSphereSDF(5, 5, 5);
    auto gridHigh = MakeSphereSDF(20, 20, 20);

    auto resultLow = Geometry::MarchingCubes::Extract(gridLow);
    auto resultHigh = Geometry::MarchingCubes::Extract(gridHigh);

    ASSERT_TRUE(resultLow.has_value());
    ASSERT_TRUE(resultHigh.has_value());

    EXPECT_GT(resultHigh->TriangleCount, resultLow->TriangleCount);
}

TEST(MarchingCubes, PlaneSDF_ProducesTriangles)
{
    auto grid = MakePlaneSDF(10, 10, 10);

    auto result = Geometry::MarchingCubes::Extract(grid);
    ASSERT_TRUE(result.has_value());
    EXPECT_GT(result->TriangleCount, 0u);

    // All vertices should have z ≈ 0 (the plane)
    for (std::size_t i = 0; i < result->VertexCount; ++i)
    {
        EXPECT_NEAR(result->Vertices[i].z, 0.0f, 0.01f)
            << "Vertex " << i << " should be on the z=0 plane";
    }
}

TEST(MarchingCubes, NoNormalsWhenDisabled)
{
    auto grid = MakeSphereSDF(5, 5, 5);

    Geometry::MarchingCubes::MarchingCubesParams params;
    params.ComputeNormals = false;

    auto result = Geometry::MarchingCubes::Extract(grid, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->Normals.empty());
}

TEST(MarchingCubes, VertexWelding_NoduplicateVertices)
{
    // A simple sphere should have far fewer vertices than 3 * triangleCount
    // (if unwelded, each triangle would have 3 unique vertices)
    auto grid = MakeSphereSDF(10, 10, 10);

    auto result = Geometry::MarchingCubes::Extract(grid);
    ASSERT_TRUE(result.has_value());

    // With welding, vertexCount should be much less than 3 * triangleCount
    EXPECT_LT(result->VertexCount, result->TriangleCount * 3);

    // Verify all triangle indices are valid
    for (const auto& tri : result->Triangles)
    {
        EXPECT_LT(tri[0], result->VertexCount);
        EXPECT_LT(tri[1], result->VertexCount);
        EXPECT_LT(tri[2], result->VertexCount);
    }
}

TEST(MarchingCubes, CustomIsovalue)
{
    // Sphere SDF with non-zero isovalue = larger/smaller sphere
    auto grid = MakeSphereSDF(15, 15, 15, 1.0f);

    Geometry::MarchingCubes::MarchingCubesParams paramsSmall;
    paramsSmall.Isovalue = 0.3f; // outer surface -> smaller sphere

    Geometry::MarchingCubes::MarchingCubesParams paramsLarge;
    paramsLarge.Isovalue = -0.3f; // inner surface -> larger sphere

    auto resultSmall = Geometry::MarchingCubes::Extract(grid, paramsSmall);
    auto resultLarge = Geometry::MarchingCubes::Extract(grid, paramsLarge);

    ASSERT_TRUE(resultSmall.has_value());
    ASSERT_TRUE(resultLarge.has_value());

    // Compute average radius of each
    float avgRadiusSmall = 0.0f;
    for (const auto& v : resultSmall->Vertices)
        avgRadiusSmall += glm::length(v);
    avgRadiusSmall /= static_cast<float>(resultSmall->VertexCount);

    float avgRadiusLarge = 0.0f;
    for (const auto& v : resultLarge->Vertices)
        avgRadiusLarge += glm::length(v);
    avgRadiusLarge /= static_cast<float>(resultLarge->VertexCount);

    // Smaller isovalue -> larger sphere radius
    EXPECT_LT(avgRadiusSmall, avgRadiusLarge);
}

// =============================================================================
// Marching Cubes — ToMesh conversion
// =============================================================================

TEST(MarchingCubes, ToMesh_ProducesValidMesh)
{
    auto grid = MakeSphereSDF(10, 10, 10);

    auto result = Geometry::MarchingCubes::Extract(grid);
    ASSERT_TRUE(result.has_value());

    auto mesh = Geometry::MarchingCubes::ToMesh(*result);
    ASSERT_TRUE(mesh.has_value());
    EXPECT_GT(mesh->VertexCount(), 0u);
    EXPECT_GT(mesh->FaceCount(), 0u);
}

TEST(MarchingCubes, ToMesh_EmptyResultReturnsNullopt)
{
    Geometry::MarchingCubes::MarchingCubesResult emptyResult;
    auto mesh = Geometry::MarchingCubes::ToMesh(emptyResult);
    EXPECT_FALSE(mesh.has_value());
}

TEST(MarchingCubes, ToMesh_ClosedSphere_EulerCharacteristic)
{
    // A sphere mesh should have Euler characteristic V - E + F = 2
    auto grid = MakeSphereSDF(15, 15, 15);
    auto result = Geometry::MarchingCubes::Extract(grid);
    ASSERT_TRUE(result.has_value());

    auto mesh = Geometry::MarchingCubes::ToMesh(*result);
    ASSERT_TRUE(mesh.has_value());

    int V = static_cast<int>(mesh->VertexCount());
    int E = static_cast<int>(mesh->EdgeCount());
    int F = static_cast<int>(mesh->FaceCount());
    int euler = V - E + F;

    // For a closed genus-0 surface, Euler = 2
    // MC may produce slightly non-manifold edges, so the halfedge mesh
    // might not be perfectly closed. We check that Euler is 2.
    EXPECT_EQ(euler, 2)
        << "V=" << V << " E=" << E << " F=" << F << " Euler=" << euler;
}

// =============================================================================
// Marching Cubes — ScalarGrid utilities
// =============================================================================

TEST(MarchingCubes, ScalarGrid_IsValid)
{
    Geometry::MarchingCubes::ScalarGrid grid;
    grid.NX = 2;
    grid.NY = 3;
    grid.NZ = 4;
    grid.Values.resize(3 * 4 * 5); // (NX+1)*(NY+1)*(NZ+1) = 60
    EXPECT_TRUE(grid.IsValid());

    grid.Values.resize(10); // wrong size
    EXPECT_FALSE(grid.IsValid());
}

TEST(MarchingCubes, ScalarGrid_AtAndSet)
{
    Geometry::MarchingCubes::ScalarGrid grid;
    grid.NX = 2;
    grid.NY = 2;
    grid.NZ = 2;
    grid.Values.resize(27, 0.0f);

    grid.Set(1, 1, 1, 42.0f);
    EXPECT_FLOAT_EQ(grid.At(1, 1, 1), 42.0f);
}

TEST(MarchingCubes, ScalarGrid_VertexPosition)
{
    Geometry::MarchingCubes::ScalarGrid grid;
    grid.NX = 10;
    grid.NY = 10;
    grid.NZ = 10;
    grid.Origin = {-1.0f, -2.0f, -3.0f};
    grid.Spacing = {0.2f, 0.4f, 0.6f};

    glm::vec3 p = grid.VertexPosition(5, 5, 5);
    EXPECT_NEAR(p.x, -1.0f + 5 * 0.2f, 1e-6f);
    EXPECT_NEAR(p.y, -2.0f + 5 * 0.4f, 1e-6f);
    EXPECT_NEAR(p.z, -3.0f + 5 * 0.6f, 1e-6f);
}

// =============================================================================
// Surface Reconstruction — Basic Tests
// =============================================================================

TEST(SurfaceReconstruction, EmptyPointsReturnsNullopt)
{
    std::vector<glm::vec3> points;
    auto result = Geometry::SurfaceReconstruction::Reconstruct(points);
    EXPECT_FALSE(result.has_value());
}

TEST(SurfaceReconstruction, TooFewPointsReturnsNullopt)
{
    std::vector<glm::vec3> points = {{0,0,0}, {1,0,0}};
    auto result = Geometry::SurfaceReconstruction::Reconstruct(points);
    EXPECT_FALSE(result.has_value());
}

TEST(SurfaceReconstruction, MismatchedNormalsReturnsNullopt)
{
    auto points = MakeSpherePoints(100);
    std::vector<glm::vec3> normals = {{0,0,1}, {0,0,1}}; // wrong size

    Geometry::SurfaceReconstruction::ReconstructionParams params;
    params.EstimateNormals = false;

    auto result = Geometry::SurfaceReconstruction::Reconstruct(points, normals, params);
    EXPECT_FALSE(result.has_value());
}

TEST(SurfaceReconstruction, NoNormalsAndNoEstimationReturnsNullopt)
{
    auto points = MakeSpherePoints(100);
    std::vector<glm::vec3> emptyNormals;

    Geometry::SurfaceReconstruction::ReconstructionParams params;
    params.EstimateNormals = false;

    auto result = Geometry::SurfaceReconstruction::Reconstruct(points, emptyNormals, params);
    EXPECT_FALSE(result.has_value());
}


TEST(SurfaceReconstruction, ZeroResolutionReturnsNullopt)
{
    auto points = MakeSpherePoints(100);
    auto normals = MakeSphereNormals(points);

    Geometry::SurfaceReconstruction::ReconstructionParams params;
    params.Resolution = 0;
    params.EstimateNormals = false;

    auto result = Geometry::SurfaceReconstruction::Reconstruct(points, normals, params);
    EXPECT_FALSE(result.has_value());
}

TEST(SurfaceReconstruction, InvalidNormalsAreRejected)
{
    auto points = MakeSpherePoints(64);
    auto normals = MakeSphereNormals(points);

    normals[0] = {0.0f, 0.0f, 0.0f};
    normals[1] = {std::numeric_limits<float>::infinity(), 0.0f, 0.0f};

    Geometry::SurfaceReconstruction::ReconstructionParams params;
    params.Resolution = 18;
    params.EstimateNormals = false;

    auto result = Geometry::SurfaceReconstruction::Reconstruct(points, normals, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_GT(result->OutputVertexCount, 0u);
}

TEST(SurfaceReconstruction, AllInvalidNormalsReturnNullopt)
{
    auto points = MakeSpherePoints(32);
    std::vector<glm::vec3> normals(points.size(), glm::vec3(0.0f));

    Geometry::SurfaceReconstruction::ReconstructionParams params;
    params.Resolution = 16;
    params.EstimateNormals = false;

    auto result = Geometry::SurfaceReconstruction::Reconstruct(points, normals, params);
    EXPECT_FALSE(result.has_value());
}

TEST(SurfaceReconstruction, SphereWithProvidedNormals)
{
    auto points = MakeSpherePoints(200);
    auto normals = MakeSphereNormals(points);

    Geometry::SurfaceReconstruction::ReconstructionParams params;
    params.Resolution = 20;
    params.EstimateNormals = false;

    auto result = Geometry::SurfaceReconstruction::Reconstruct(points, normals, params);
    ASSERT_TRUE(result.has_value());

    EXPECT_GT(result->OutputVertexCount, 0u);
    EXPECT_GT(result->OutputFaceCount, 0u);
    EXPECT_GT(result->GridNX, 0u);
    EXPECT_GT(result->GridNY, 0u);
    EXPECT_GT(result->GridNZ, 0u);
}

TEST(SurfaceReconstruction, SphereWithEstimatedNormals)
{
    auto points = MakeSpherePoints(200);

    Geometry::SurfaceReconstruction::ReconstructionParams params;
    params.Resolution = 20;
    params.EstimateNormals = true;
    params.NormalKNeighbors = 15;

    auto result = Geometry::SurfaceReconstruction::Reconstruct(points, {}, params);
    ASSERT_TRUE(result.has_value());

    EXPECT_GT(result->OutputVertexCount, 0u);
    EXPECT_GT(result->OutputFaceCount, 0u);
}

TEST(SurfaceReconstruction, SphereWithWeightedDistance)
{
    auto points = MakeSpherePoints(200);
    auto normals = MakeSphereNormals(points);

    Geometry::SurfaceReconstruction::ReconstructionParams params;
    params.Resolution = 20;
    params.KNeighbors = 5; // weighted average
    params.EstimateNormals = false;

    auto result = Geometry::SurfaceReconstruction::Reconstruct(points, normals, params);
    ASSERT_TRUE(result.has_value());

    EXPECT_GT(result->OutputVertexCount, 0u);
    EXPECT_GT(result->OutputFaceCount, 0u);
}

TEST(SurfaceReconstruction, OutputMeshHasVerticesAndFaces)
{
    auto points = MakeSpherePoints(200);
    auto normals = MakeSphereNormals(points);

    Geometry::SurfaceReconstruction::ReconstructionParams params;
    params.Resolution = 15;
    params.EstimateNormals = false;

    auto result = Geometry::SurfaceReconstruction::Reconstruct(points, normals, params);
    ASSERT_TRUE(result.has_value());

    // The output mesh should be a valid halfedge mesh
    EXPECT_EQ(result->OutputVertexCount, result->OutputMesh.VertexCount());
    EXPECT_EQ(result->OutputFaceCount, result->OutputMesh.FaceCount());
}

TEST(SurfaceReconstruction, HigherResolutionMoreDetail)
{
    auto points = MakeSpherePoints(300);
    auto normals = MakeSphereNormals(points);

    Geometry::SurfaceReconstruction::ReconstructionParams paramsLow;
    paramsLow.Resolution = 10;
    paramsLow.EstimateNormals = false;

    Geometry::SurfaceReconstruction::ReconstructionParams paramsHigh;
    paramsHigh.Resolution = 25;
    paramsHigh.EstimateNormals = false;

    auto resultLow = Geometry::SurfaceReconstruction::Reconstruct(points, normals, paramsLow);
    auto resultHigh = Geometry::SurfaceReconstruction::Reconstruct(points, normals, paramsHigh);

    ASSERT_TRUE(resultLow.has_value());
    ASSERT_TRUE(resultHigh.has_value());

    // Higher resolution should produce more faces
    EXPECT_GT(resultHigh->OutputFaceCount, resultLow->OutputFaceCount);
}

TEST(SurfaceReconstruction, SphereReconstructionApproximatesRadius)
{
    const float radius = 2.0f;
    auto points = MakeSpherePoints(300, radius);
    auto normals = MakeSphereNormals(points);

    Geometry::SurfaceReconstruction::ReconstructionParams params;
    params.Resolution = 25;
    params.EstimateNormals = false;

    auto result = Geometry::SurfaceReconstruction::Reconstruct(points, normals, params);
    ASSERT_TRUE(result.has_value());

    // Compute average vertex distance from origin
    double avgRadius = 0.0;
    std::size_t count = 0;
    for (std::size_t vi = 0; vi < result->OutputMesh.VerticesSize(); ++vi)
    {
        Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(vi)};
        if (result->OutputMesh.IsDeleted(vh)) continue;
        float r = glm::length(result->OutputMesh.Position(vh));
        avgRadius += static_cast<double>(r);
        ++count;
    }
    avgRadius /= static_cast<double>(count);

    // Average radius should be close to the input sphere radius
    // (within a tolerance due to grid discretization)
    EXPECT_NEAR(avgRadius, static_cast<double>(radius), 0.5)
        << "Average vertex radius should approximate the sphere radius";
}
