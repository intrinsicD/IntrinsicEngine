#include <gtest/gtest.h>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <numeric>
#include <span>
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
// Helper: create a DenseGrid from a sphere SDF
// =============================================================================

static Geometry::Grid::DenseGrid MakeSphereSDF(
    std::size_t nx, std::size_t ny, std::size_t nz,
    float radius = 1.0f,
    glm::vec3 center = {0.0f, 0.0f, 0.0f})
{
    Geometry::Grid::GridDimensions dims;
    dims.NX = nx;
    dims.NY = ny;
    dims.NZ = nz;

    float extent = radius * 2.0f;
    dims.Origin = center - glm::vec3(extent);
    dims.Spacing = glm::vec3(2.0f * extent / static_cast<float>(nx));

    Geometry::Grid::DenseGrid grid(dims);
    auto scalar = grid.AddProperty<float>("scalar", 0.0f);

    for (std::size_t z = 0; z <= nz; ++z)
        for (std::size_t y = 0; y <= ny; ++y)
            for (std::size_t x = 0; x <= nx; ++x)
            {
                glm::vec3 pos = grid.WorldPosition(x, y, z);
                float dist = glm::length(pos - center) - radius;
                grid.Set(scalar, x, y, z, dist);
            }

    return grid;
}

// =============================================================================
// Helper: create a DenseGrid from a plane SDF (z = 0)
// =============================================================================

static Geometry::Grid::DenseGrid MakePlaneSDF(
    std::size_t nx, std::size_t ny, std::size_t nz)
{
    Geometry::Grid::GridDimensions dims;
    dims.NX = nx;
    dims.NY = ny;
    dims.NZ = nz;
    dims.Origin = {-1.0f, -1.0f, -1.0f};
    dims.Spacing = glm::vec3(2.0f / static_cast<float>(nx));

    Geometry::Grid::DenseGrid grid(dims);
    auto scalar = grid.AddProperty<float>("scalar", 0.0f);

    for (std::size_t z = 0; z <= nz; ++z)
        for (std::size_t y = 0; y <= ny; ++y)
            for (std::size_t x = 0; x <= nx; ++x)
            {
                glm::vec3 pos = grid.WorldPosition(x, y, z);
                grid.Set(scalar, x, y, z, pos.z);
            }

    return grid;
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

TEST(SurfaceReconstruction, AcceptsBorrowedSpanInput)
{
    const std::array<glm::vec3, 6> points = {
        glm::vec3{1.0f, 0.0f, 0.0f},
        glm::vec3{-1.0f, 0.0f, 0.0f},
        glm::vec3{0.0f, 1.0f, 0.0f},
        glm::vec3{0.0f, -1.0f, 0.0f},
        glm::vec3{0.0f, 0.0f, 1.0f},
        glm::vec3{0.0f, 0.0f, -1.0f},
    };
    const std::array<glm::vec3, 6> normals = {
        glm::vec3{1.0f, 0.0f, 0.0f},
        glm::vec3{-1.0f, 0.0f, 0.0f},
        glm::vec3{0.0f, 1.0f, 0.0f},
        glm::vec3{0.0f, -1.0f, 0.0f},
        glm::vec3{0.0f, 0.0f, 1.0f},
        glm::vec3{0.0f, 0.0f, -1.0f},
    };

    Geometry::SurfaceReconstruction::ReconstructionParams params;
    params.Resolution = 8;
    params.EstimateNormals = false;

    auto result = Geometry::SurfaceReconstruction::Reconstruct(
        std::span<const glm::vec3>{points}, std::span<const glm::vec3>{normals}, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_GT(result->OutputVertexCount, 0u);
    EXPECT_GT(result->OutputFaceCount, 0u);
}

