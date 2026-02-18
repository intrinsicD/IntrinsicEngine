#include <gtest/gtest.h>
#include <algorithm>
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

static std::vector<glm::vec4> MakeRandomColors(std::size_t n)
{
    std::vector<glm::vec4> colors;
    colors.reserve(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        float t = static_cast<float>(i) / static_cast<float>(n);
        colors.push_back({t, 1.0f - t, 0.5f, 1.0f});
    }
    return colors;
}

static Geometry::PointCloud::Cloud MakeSphereCloud(std::size_t n, float radius = 1.0f, bool withNormals = true, bool withColors = false)
{
    Geometry::PointCloud::Cloud cloud;
    cloud.Positions = MakeSpherePoints(n, radius);
    if (withNormals) cloud.Normals = MakeSphereNormals(cloud.Positions);
    if (withColors)  cloud.Colors = MakeRandomColors(n);
    return cloud;
}

// =============================================================================
// Cloud structure tests
// =============================================================================

TEST(PointCloud_Cloud, EmptyCloudIsValid)
{
    Geometry::PointCloud::Cloud cloud;
    EXPECT_TRUE(cloud.IsValid());
    EXPECT_TRUE(cloud.Empty());
    EXPECT_EQ(cloud.Size(), 0u);
    EXPECT_FALSE(cloud.HasNormals());
    EXPECT_FALSE(cloud.HasColors());
    EXPECT_FALSE(cloud.HasRadii());
}

TEST(PointCloud_Cloud, PositionsOnlyIsValid)
{
    Geometry::PointCloud::Cloud cloud;
    cloud.Positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    EXPECT_TRUE(cloud.IsValid());
    EXPECT_FALSE(cloud.Empty());
    EXPECT_EQ(cloud.Size(), 3u);
    EXPECT_FALSE(cloud.HasNormals());
}

TEST(PointCloud_Cloud, FullAttributesAreValid)
{
    auto cloud = MakeSphereCloud(100, 1.0f, true, true);
    cloud.Radii.resize(100, 0.01f);
    EXPECT_TRUE(cloud.IsValid());
    EXPECT_TRUE(cloud.HasNormals());
    EXPECT_TRUE(cloud.HasColors());
    EXPECT_TRUE(cloud.HasRadii());
}

TEST(PointCloud_Cloud, MismatchedNormalsInvalid)
{
    Geometry::PointCloud::Cloud cloud;
    cloud.Positions = {{0, 0, 0}, {1, 0, 0}};
    cloud.Normals = {{0, 1, 0}}; // Wrong count
    EXPECT_FALSE(cloud.IsValid());
}

TEST(PointCloud_Cloud, MismatchedColorsInvalid)
{
    Geometry::PointCloud::Cloud cloud;
    cloud.Positions = {{0, 0, 0}, {1, 0, 0}};
    cloud.Colors = {{1, 1, 1, 1}, {0, 0, 0, 1}, {0.5, 0.5, 0.5, 1}}; // Wrong count
    EXPECT_FALSE(cloud.IsValid());
}

// =============================================================================
// Bounding box tests
// =============================================================================

TEST(PointCloud_BoundingBox, EmptyCloudReturnsZeroAABB)
{
    Geometry::PointCloud::Cloud cloud;
    auto bb = Geometry::PointCloud::ComputeBoundingBox(cloud);
    EXPECT_FLOAT_EQ(bb.Min.x, 0.0f);
    EXPECT_FLOAT_EQ(bb.Max.x, 0.0f);
}

TEST(PointCloud_BoundingBox, SinglePointBB)
{
    Geometry::PointCloud::Cloud cloud;
    cloud.Positions = {{3.0f, -2.0f, 5.0f}};
    auto bb = Geometry::PointCloud::ComputeBoundingBox(cloud);
    EXPECT_FLOAT_EQ(bb.Min.x, 3.0f);
    EXPECT_FLOAT_EQ(bb.Min.y, -2.0f);
    EXPECT_FLOAT_EQ(bb.Max.z, 5.0f);
}

TEST(PointCloud_BoundingBox, UnitSphereBoundedByUnitCube)
{
    auto cloud = MakeSphereCloud(500);
    auto bb = Geometry::PointCloud::ComputeBoundingBox(cloud);

    // Sphere of radius 1 should have AABB roughly [-1,1]^3.
    EXPECT_NEAR(bb.Min.x, -1.0f, 0.1f);
    EXPECT_NEAR(bb.Min.y, -1.0f, 0.01f);
    EXPECT_NEAR(bb.Min.z, -1.0f, 0.1f);
    EXPECT_NEAR(bb.Max.x, 1.0f, 0.1f);
    EXPECT_NEAR(bb.Max.y, 1.0f, 0.01f);
    EXPECT_NEAR(bb.Max.z, 1.0f, 0.1f);
}

// =============================================================================
// Statistics tests
// =============================================================================

TEST(PointCloud_Statistics, EmptyReturnsNullopt)
{
    Geometry::PointCloud::Cloud cloud;
    auto result = Geometry::PointCloud::ComputeStatistics(cloud);
    EXPECT_FALSE(result.has_value());
}

TEST(PointCloud_Statistics, SinglePointStats)
{
    Geometry::PointCloud::Cloud cloud;
    cloud.Positions = {{1.0f, 2.0f, 3.0f}};
    auto result = Geometry::PointCloud::ComputeStatistics(cloud);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->PointCount, 1u);
    EXPECT_FLOAT_EQ(result->Centroid.x, 1.0f);
    EXPECT_FLOAT_EQ(result->Centroid.y, 2.0f);
    EXPECT_FLOAT_EQ(result->AverageSpacing, 0.0f);
}

TEST(PointCloud_Statistics, SphereStatistics)
{
    auto cloud = MakeSphereCloud(500);
    Geometry::PointCloud::StatisticsParams params;
    params.SpacingSampleCount = 100;
    auto result = Geometry::PointCloud::ComputeStatistics(cloud, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->PointCount, 500u);

    // Centroid of uniform sphere should be near origin.
    EXPECT_NEAR(glm::length(result->Centroid), 0.0f, 0.1f);

    // Bounding box diagonal should be ~2*sqrt(3) ~ 3.46.
    EXPECT_NEAR(result->BoundingBoxDiagonal, 2.0f * std::sqrt(3.0f), 0.3f);

    // Average spacing should be positive and reasonable.
    EXPECT_GT(result->AverageSpacing, 0.0f);
    EXPECT_LT(result->AverageSpacing, 0.5f); // 500 points on unit sphere

    EXPECT_LE(result->MinSpacing, result->AverageSpacing);
    EXPECT_GE(result->MaxSpacing, result->AverageSpacing);
}

// =============================================================================
// Voxel downsampling tests
// =============================================================================

TEST(PointCloud_Downsample, EmptyReturnsNullopt)
{
    Geometry::PointCloud::Cloud cloud;
    auto result = Geometry::PointCloud::VoxelDownsample(cloud);
    EXPECT_FALSE(result.has_value());
}

TEST(PointCloud_Downsample, InvalidVoxelSizeReturnsNullopt)
{
    auto cloud = MakeSphereCloud(100);
    Geometry::PointCloud::DownsampleParams params;
    params.VoxelSize = 0.0f;
    auto result = Geometry::PointCloud::VoxelDownsample(cloud, params);
    EXPECT_FALSE(result.has_value());

    params.VoxelSize = -1.0f;
    result = Geometry::PointCloud::VoxelDownsample(cloud, params);
    EXPECT_FALSE(result.has_value());
}

TEST(PointCloud_Downsample, LargeVoxelCollapsesToFewPoints)
{
    auto cloud = MakeSphereCloud(500, 1.0f);
    Geometry::PointCloud::DownsampleParams params;
    params.VoxelSize = 2.0f; // Larger than sphere diameter → very few cells.
    auto result = Geometry::PointCloud::VoxelDownsample(cloud, params);
    ASSERT_TRUE(result.has_value());

    // With voxel size 2 on a unit sphere, expect very few output points.
    EXPECT_LT(result->ReducedCount, 20u);
    EXPECT_EQ(result->OriginalCount, 500u);
    EXPECT_GT(result->ReductionRatio, 0.0f);
    EXPECT_LT(result->ReductionRatio, 0.1f);
}

TEST(PointCloud_Downsample, SmallVoxelPreservesPoints)
{
    auto cloud = MakeSphereCloud(200, 1.0f);
    Geometry::PointCloud::DownsampleParams params;
    params.VoxelSize = 0.001f; // Very small → almost all points preserved.
    auto result = Geometry::PointCloud::VoxelDownsample(cloud, params);
    ASSERT_TRUE(result.has_value());

    // Should preserve nearly all points.
    EXPECT_EQ(result->ReducedCount, 200u);
    EXPECT_NEAR(result->ReductionRatio, 1.0f, 0.01f);
}

TEST(PointCloud_Downsample, PreservesNormals)
{
    auto cloud = MakeSphereCloud(200, 1.0f, true);
    Geometry::PointCloud::DownsampleParams params;
    params.VoxelSize = 0.5f;
    params.PreserveNormals = true;
    auto result = Geometry::PointCloud::VoxelDownsample(cloud, params);
    ASSERT_TRUE(result.has_value());

    // Output should have normals.
    EXPECT_TRUE(result->Downsampled.HasNormals());

    // All normals should be unit length.
    for (const auto& n : result->Downsampled.Normals)
    {
        float len = glm::length(n);
        EXPECT_NEAR(len, 1.0f, 0.01f);
    }
}

TEST(PointCloud_Downsample, PreservesColors)
{
    auto cloud = MakeSphereCloud(200, 1.0f, false, true);
    Geometry::PointCloud::DownsampleParams params;
    params.VoxelSize = 0.5f;
    params.PreserveColors = true;
    auto result = Geometry::PointCloud::VoxelDownsample(cloud, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->Downsampled.HasColors());
}

TEST(PointCloud_Downsample, OutputCloudIsValid)
{
    auto cloud = MakeSphereCloud(300, 1.0f, true, true);
    cloud.Radii.resize(300, 0.01f);
    Geometry::PointCloud::DownsampleParams params;
    params.VoxelSize = 0.3f;
    auto result = Geometry::PointCloud::VoxelDownsample(cloud, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->Downsampled.IsValid());
}

// =============================================================================
// Radius estimation tests
// =============================================================================

TEST(PointCloud_Radius, TooFewPointsReturnsNullopt)
{
    Geometry::PointCloud::Cloud cloud;
    cloud.Positions = {{0, 0, 0}};
    auto result = Geometry::PointCloud::EstimateRadii(cloud);
    EXPECT_FALSE(result.has_value());
}

TEST(PointCloud_Radius, SphereRadiiReasonable)
{
    auto cloud = MakeSphereCloud(500, 1.0f);
    Geometry::PointCloud::RadiusEstimationParams params;
    params.KNeighbors = 6;
    params.ScaleFactor = 1.0f;
    auto result = Geometry::PointCloud::EstimateRadii(cloud, params);
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->Radii.size(), 500u);
    EXPECT_GT(result->AverageRadius, 0.0f);
    EXPECT_LT(result->AverageRadius, 0.5f);
    EXPECT_LE(result->MinRadius, result->AverageRadius);
    EXPECT_GE(result->MaxRadius, result->AverageRadius);

    // All radii should be positive.
    for (float r : result->Radii)
    {
        EXPECT_GE(r, 0.0f);
    }
}

TEST(PointCloud_Radius, ScaleFactorMultipliesRadius)
{
    auto cloud = MakeSphereCloud(200, 1.0f);

    Geometry::PointCloud::RadiusEstimationParams params1;
    params1.KNeighbors = 6;
    params1.ScaleFactor = 1.0f;
    auto r1 = Geometry::PointCloud::EstimateRadii(cloud, params1);

    Geometry::PointCloud::RadiusEstimationParams params2;
    params2.KNeighbors = 6;
    params2.ScaleFactor = 2.0f;
    auto r2 = Geometry::PointCloud::EstimateRadii(cloud, params2);

    ASSERT_TRUE(r1.has_value() && r2.has_value());
    EXPECT_NEAR(r2->AverageRadius, r1->AverageRadius * 2.0f, r1->AverageRadius * 0.01f);
}

// =============================================================================
// Random subsampling tests
// =============================================================================

TEST(PointCloud_Subsample, EmptyReturnsNullopt)
{
    Geometry::PointCloud::Cloud cloud;
    auto result = Geometry::PointCloud::RandomSubsample(cloud);
    EXPECT_FALSE(result.has_value());
}

TEST(PointCloud_Subsample, SubsampleReducesCount)
{
    auto cloud = MakeSphereCloud(500, 1.0f, true, true);
    Geometry::PointCloud::SubsampleParams params;
    params.TargetCount = 100;
    auto result = Geometry::PointCloud::RandomSubsample(cloud, params);
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->Subsampled.Size(), 100u);
    EXPECT_EQ(result->SelectedIndices.size(), 100u);
    EXPECT_TRUE(result->Subsampled.HasNormals());
    EXPECT_TRUE(result->Subsampled.HasColors());
    EXPECT_TRUE(result->Subsampled.IsValid());
}

TEST(PointCloud_Subsample, TargetLargerThanCloudReturnsAll)
{
    auto cloud = MakeSphereCloud(50);
    Geometry::PointCloud::SubsampleParams params;
    params.TargetCount = 200;
    auto result = Geometry::PointCloud::RandomSubsample(cloud, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Subsampled.Size(), 50u);
}

TEST(PointCloud_Subsample, DeterministicWithSameSeed)
{
    auto cloud = MakeSphereCloud(200);
    Geometry::PointCloud::SubsampleParams params;
    params.TargetCount = 50;
    params.Seed = 42;

    auto r1 = Geometry::PointCloud::RandomSubsample(cloud, params);
    auto r2 = Geometry::PointCloud::RandomSubsample(cloud, params);
    ASSERT_TRUE(r1.has_value() && r2.has_value());
    EXPECT_EQ(r1->SelectedIndices, r2->SelectedIndices);
}

TEST(PointCloud_Subsample, DifferentSeedsGiveDifferentResults)
{
    auto cloud = MakeSphereCloud(200);

    Geometry::PointCloud::SubsampleParams params1;
    params1.TargetCount = 50;
    params1.Seed = 42;

    Geometry::PointCloud::SubsampleParams params2;
    params2.TargetCount = 50;
    params2.Seed = 123;

    auto r1 = Geometry::PointCloud::RandomSubsample(cloud, params1);
    auto r2 = Geometry::PointCloud::RandomSubsample(cloud, params2);
    ASSERT_TRUE(r1.has_value() && r2.has_value());
    EXPECT_NE(r1->SelectedIndices, r2->SelectedIndices);
}

TEST(PointCloud_Subsample, IndicesAreValid)
{
    auto cloud = MakeSphereCloud(300, 1.0f, true);
    Geometry::PointCloud::SubsampleParams params;
    params.TargetCount = 100;
    auto result = Geometry::PointCloud::RandomSubsample(cloud, params);
    ASSERT_TRUE(result.has_value());

    // All selected indices must be within bounds.
    for (std::size_t idx : result->SelectedIndices)
    {
        EXPECT_LT(idx, 300u);
    }

    // Indices should be sorted (as per implementation).
    EXPECT_TRUE(std::is_sorted(result->SelectedIndices.begin(), result->SelectedIndices.end()));

    // Positions should match originals.
    for (std::size_t i = 0; i < result->SelectedIndices.size(); ++i)
    {
        std::size_t origIdx = result->SelectedIndices[i];
        EXPECT_FLOAT_EQ(result->Subsampled.Positions[i].x, cloud.Positions[origIdx].x);
        EXPECT_FLOAT_EQ(result->Subsampled.Positions[i].y, cloud.Positions[origIdx].y);
        EXPECT_FLOAT_EQ(result->Subsampled.Positions[i].z, cloud.Positions[origIdx].z);
    }
}

// =============================================================================
// Render mode enum tests
// =============================================================================

TEST(PointCloud_RenderMode, EnumValues)
{
    EXPECT_EQ(static_cast<uint32_t>(Geometry::PointCloud::RenderMode::FlatDisc), 0u);
    EXPECT_EQ(static_cast<uint32_t>(Geometry::PointCloud::RenderMode::Surfel), 1u);
    EXPECT_EQ(static_cast<uint32_t>(Geometry::PointCloud::RenderMode::EWA), 2u);
}

// =============================================================================
// Integration test: downsample then estimate radii
// =============================================================================

TEST(PointCloud_Integration, DownsampleThenEstimateRadii)
{
    auto cloud = MakeSphereCloud(1000, 1.0f, true);

    // Downsample.
    Geometry::PointCloud::DownsampleParams dParams;
    dParams.VoxelSize = 0.2f;
    auto dResult = Geometry::PointCloud::VoxelDownsample(cloud, dParams);
    ASSERT_TRUE(dResult.has_value());
    EXPECT_GT(dResult->Downsampled.Size(), 10u);

    // Estimate radii on downsampled cloud.
    Geometry::PointCloud::RadiusEstimationParams rParams;
    rParams.KNeighbors = 6;
    rParams.ScaleFactor = 1.2f;
    auto rResult = Geometry::PointCloud::EstimateRadii(dResult->Downsampled, rParams);
    ASSERT_TRUE(rResult.has_value());
    EXPECT_EQ(rResult->Radii.size(), dResult->Downsampled.Size());

    // Radii on a coarser cloud should be larger than on the dense one.
    EXPECT_GT(rResult->AverageRadius, 0.05f);
}

// =============================================================================
// Edge case: collinear points
// =============================================================================

TEST(PointCloud_Edge, CollinearPointsDownsample)
{
    Geometry::PointCloud::Cloud cloud;
    for (int i = 0; i < 100; ++i)
        cloud.Positions.push_back({static_cast<float>(i) * 0.01f, 0.0f, 0.0f});

    EXPECT_TRUE(cloud.IsValid());

    Geometry::PointCloud::DownsampleParams params;
    params.VoxelSize = 0.1f;
    auto result = Geometry::PointCloud::VoxelDownsample(cloud, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_LT(result->ReducedCount, cloud.Size());
}

TEST(PointCloud_Edge, DuplicatePointsDownsample)
{
    Geometry::PointCloud::Cloud cloud;
    for (int i = 0; i < 100; ++i)
        cloud.Positions.push_back({0.0f, 0.0f, 0.0f}); // All same point.

    Geometry::PointCloud::DownsampleParams params;
    params.VoxelSize = 0.1f;
    auto result = Geometry::PointCloud::VoxelDownsample(cloud, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->ReducedCount, 1u); // All collapse to one cell.
}
