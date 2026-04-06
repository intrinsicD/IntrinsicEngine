// tests/Test_NormalEstimation.cpp — Point cloud normal estimation tests.
// Covers: PCA normal computation, MST orientation, unit length, degenerate
// input, and planar point cloud correctness.

#include <gtest/gtest.h>
#include <array>
#include <cmath>
#include <cstddef>
#include <span>
#include <vector>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>

import Geometry;

namespace
{
    // Generate a flat grid of points on the XY plane.
    std::vector<glm::vec3> MakeFlatGrid(int n = 10, float spacing = 0.1f)
    {
        std::vector<glm::vec3> points;
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x)
                points.emplace_back(x * spacing, y * spacing, 0.0f);
        return points;
    }

    // Generate points on the surface of a unit sphere.
    std::vector<glm::vec3> MakeSpherePoints(int nLat = 10, int nLon = 20)
    {
        std::vector<glm::vec3> points;
        for (int i = 1; i < nLat; ++i)
        {
            float theta = static_cast<float>(i) / nLat * 3.14159265f;
            for (int j = 0; j < nLon; ++j)
            {
                float phi = static_cast<float>(j) / nLon * 2.0f * 3.14159265f;
                points.emplace_back(
                    std::sin(theta) * std::cos(phi),
                    std::sin(theta) * std::sin(phi),
                    std::cos(theta));
            }
        }
        // Add poles
        points.emplace_back(0.0f, 0.0f, 1.0f);
        points.emplace_back(0.0f, 0.0f, -1.0f);
        return points;
    }
}

TEST(NormalEstimation, FlatGridNormalsPointAlongZ)
{
    auto points = MakeFlatGrid();

    auto result = Geometry::NormalEstimation::EstimateNormals(points);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->Normals.size(), points.size());

    // All normals for a flat XY grid should be approximately ±Z
    for (const auto& n : result->Normals)
    {
        EXPECT_NEAR(std::abs(n.z), 1.0f, 0.1f);
        EXPECT_NEAR(n.x, 0.0f, 0.1f);
        EXPECT_NEAR(n.y, 0.0f, 0.1f);
    }
}

TEST(NormalEstimation, NormalsAreUnitLength)
{
    auto points = MakeSpherePoints();

    auto result = Geometry::NormalEstimation::EstimateNormals(points);
    ASSERT_TRUE(result.has_value());

    for (const auto& n : result->Normals)
    {
        float len = glm::length(n);
        EXPECT_NEAR(len, 1.0f, 1e-4f);
    }
}

TEST(NormalEstimation, SphereNormalsPointOutward)
{
    auto points = MakeSpherePoints();

    Geometry::NormalEstimation::EstimationParams params;
    params.KNeighbors = 15;
    params.OrientNormals = true;

    auto result = Geometry::NormalEstimation::EstimateNormals(points, params);
    ASSERT_TRUE(result.has_value());

    // With MST orientation, normals should be consistently oriented.
    // For a sphere centered at origin, most normals should point outward
    // (dot(point, normal) > 0). Allow some tolerance for near-equator points.
    int outwardCount = 0;
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        if (glm::dot(points[i], result->Normals[i]) > 0.0f)
            ++outwardCount;
    }
    // At least 90% should point outward after MST orientation
    EXPECT_GT(outwardCount, static_cast<int>(points.size() * 0.9));
}

TEST(NormalEstimation, ReturnsNulloptForEmptyInput)
{
    std::vector<glm::vec3> empty;

    auto result = Geometry::NormalEstimation::EstimateNormals(empty);
    EXPECT_FALSE(result.has_value());
}

TEST(NormalEstimation, ReturnsNulloptForTooFewPoints)
{
    std::vector<glm::vec3> points = {{0, 0, 0}, {1, 0, 0}};

    auto result = Geometry::NormalEstimation::EstimateNormals(points);
    EXPECT_FALSE(result.has_value());
}

TEST(NormalEstimation, WithoutOrientation)
{
    auto points = MakeFlatGrid();

    Geometry::NormalEstimation::EstimationParams params;
    params.OrientNormals = false;

    auto result = Geometry::NormalEstimation::EstimateNormals(points, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->FlippedCount, 0u); // No flipping when orientation disabled
}

TEST(NormalEstimation, DegenerateCountReported)
{
    auto points = MakeFlatGrid();

    auto result = Geometry::NormalEstimation::EstimateNormals(points);
    ASSERT_TRUE(result.has_value());
    // A well-formed grid should have no degenerate neighborhoods
    EXPECT_EQ(result->DegenerateCount, 0u);
}

TEST(NormalEstimation, CustomKNeighbors)
{
    auto points = MakeSpherePoints();

    Geometry::NormalEstimation::EstimationParams params;
    params.KNeighbors = 25; // Larger neighborhood

    auto result = Geometry::NormalEstimation::EstimateNormals(points, params);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->Normals.size(), points.size());
}

TEST(NormalEstimation, AcceptsBorrowedSpanInput)
{
    const std::array<glm::vec3, 4> points = {
        glm::vec3{0.0f, 0.0f, 0.0f},
        glm::vec3{1.0f, 0.0f, 0.0f},
        glm::vec3{0.0f, 1.0f, 0.0f},
        glm::vec3{1.0f, 1.0f, 0.0f},
    };

    auto result = Geometry::NormalEstimation::EstimateNormals(std::span<const glm::vec3>{points});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Normals.size(), points.size());
}

// =============================================================================
// Tests merged from Test_GeometryProcessing2.cpp
// =============================================================================

namespace
{
    // Generate unit sphere point cloud via Fibonacci sampling.
    std::vector<glm::vec3> MakeSpherePointCloud(std::size_t n)
    {
        std::vector<glm::vec3> points;
        points.reserve(n);

        const float goldenAngle = static_cast<float>(3.14159265358979323846) * (3.0f - std::sqrt(5.0f));

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

    // Generate planar point cloud on the XY plane.
    std::vector<glm::vec3> MakePlanarPointCloud(std::size_t nx, std::size_t ny)
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

