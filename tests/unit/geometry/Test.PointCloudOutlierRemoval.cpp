// Test.PointCloudOutlierRemoval — GEOM-016 explicit outlier-removal operators.
//
// Covers RemoveStatisticalOutliers and RemoveRadiusOutliers: known two-cluster
// + isolated-outlier fixtures, deterministic kept/rejected ordering, invalid
// input handling, non-finite rejection, and kept-attribute preservation.

#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

#include <glm/glm.hpp>
#include <gtest/gtest.h>

import Geometry.PointCloud;
import Geometry.PointCloud.Utils;

namespace
{
    using Cloud = Geometry::PointCloud::Cloud;
    namespace PC = Geometry::PointCloud;

    // A dense axis-aligned grid block of sideN x sideN points at the given
    // origin with uniform spacing, with per-point normals enabled.
    void AppendGridBlock(Cloud& cloud, int sideN, float spacing, const glm::vec3& origin)
    {
        for (int y = 0; y < sideN; ++y)
            for (int x = 0; x < sideN; ++x)
            {
                const auto h = cloud.AddPoint(
                    origin + glm::vec3(static_cast<float>(x) * spacing,
                                       static_cast<float>(y) * spacing, 0.0f));
                cloud.Normal(h) = glm::vec3(0.0f, 0.0f, 1.0f);
            }
    }

    // Two dense clusters (inliers) plus a set of far isolated outliers appended
    // last; returns the original indices of the injected outliers.
    Cloud MakeTwoClusterFixture(std::vector<std::size_t>& outOutlierIndices)
    {
        Cloud cloud;
        cloud.EnableNormals();

        AppendGridBlock(cloud, 5, 0.05f, glm::vec3(0.0f));
        AppendGridBlock(cloud, 5, 0.05f, glm::vec3(2.0f, 0.0f, 0.0f));

        outOutlierIndices.clear();
        const glm::vec3 outliers[] = {
            glm::vec3(10.0f, 10.0f, 10.0f),
            glm::vec3(-8.0f, 5.0f, -3.0f),
            glm::vec3(12.0f, -7.0f, 4.0f),
        };
        for (const glm::vec3& p : outliers)
        {
            const auto h = cloud.AddPoint(p);
            cloud.Normal(h) = glm::vec3(0.0f, 0.0f, 1.0f);
            outOutlierIndices.push_back(cloud.VerticesSize() - 1);
        }
        return cloud;
    }

    bool Contains(const std::vector<std::size_t>& v, std::size_t value)
    {
        for (std::size_t e : v)
            if (e == value)
                return true;
        return false;
    }

    bool IsStrictlyAscending(const std::vector<std::size_t>& v)
    {
        for (std::size_t i = 1; i < v.size(); ++i)
            if (v[i - 1] >= v[i])
                return false;
        return true;
    }
}

// =============================================================================
// Statistical outlier removal
// =============================================================================

TEST(PointCloudOutlierRemoval, StatisticalRejectsKnownOutliers)
{
    std::vector<std::size_t> outliers;
    Cloud cloud = MakeTwoClusterFixture(outliers);
    const std::size_t total = cloud.VerticesSize();

    PC::StatisticalOutlierRemovalParams params{};
    params.KNeighbors = 8;
    params.StdDevMultiplier = 1.0f;

    const PC::OutlierRemovalResult result = PC::RemoveStatisticalOutliers(cloud, params);

    ASSERT_EQ(result.Status, PC::OutlierRemovalStatus::Success);
    EXPECT_EQ(result.OriginalCount, total);
    EXPECT_EQ(result.KeptCount + result.RejectedCount, total);

    // Every injected far outlier must be rejected; all dense inliers kept.
    for (std::size_t idx : outliers)
        EXPECT_TRUE(Contains(result.RejectedIndices, idx)) << "outlier idx " << idx << " not rejected";
    EXPECT_EQ(result.RejectedCount, outliers.size());
    EXPECT_EQ(result.KeptCount, total - outliers.size());

    // Diagnostics populated and self-consistent.
    EXPECT_GT(result.StdDevDistance, 0.0f);
    EXPECT_NEAR(result.DistanceThreshold,
                result.MeanDistance + params.StdDevMultiplier * result.StdDevDistance, 1e-4f);

    // Filtered cloud matches kept partition and preserves normals.
    EXPECT_EQ(result.Filtered.VerticesSize(), result.KeptCount);
    ASSERT_TRUE(result.Filtered.HasNormals());
}

TEST(PointCloudOutlierRemoval, StatisticalIsDeterministic)
{
    std::vector<std::size_t> outliers;
    Cloud cloud = MakeTwoClusterFixture(outliers);

    PC::StatisticalOutlierRemovalParams params{};
    params.KNeighbors = 8;

    const PC::OutlierRemovalResult a = PC::RemoveStatisticalOutliers(cloud, params);
    const PC::OutlierRemovalResult b = PC::RemoveStatisticalOutliers(cloud, params);

    EXPECT_EQ(a.KeptIndices, b.KeptIndices);
    EXPECT_EQ(a.RejectedIndices, b.RejectedIndices);
    EXPECT_TRUE(IsStrictlyAscending(a.KeptIndices));
    EXPECT_TRUE(IsStrictlyAscending(a.RejectedIndices));
}

TEST(PointCloudOutlierRemoval, StatisticalRejectsNonFinitePositions)
{
    std::vector<std::size_t> outliers;
    Cloud cloud = MakeTwoClusterFixture(outliers);
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    const auto h = cloud.AddPoint(glm::vec3(nan, 0.0f, 0.0f));
    cloud.Normal(h) = glm::vec3(0.0f, 0.0f, 1.0f);
    const std::size_t nanIdx = cloud.VerticesSize() - 1;

    PC::StatisticalOutlierRemovalParams params{};
    params.KNeighbors = 8;

    const PC::OutlierRemovalResult result = PC::RemoveStatisticalOutliers(cloud, params);
    ASSERT_EQ(result.Status, PC::OutlierRemovalStatus::Success);
    EXPECT_TRUE(Contains(result.RejectedIndices, nanIdx));
    EXPECT_GE(result.NonFiniteCount, std::size_t{1});
    EXPECT_TRUE(std::isfinite(result.MeanDistance));
    EXPECT_TRUE(std::isfinite(result.StdDevDistance));
}

TEST(PointCloudOutlierRemoval, StatisticalInvalidInputs)
{
    // Empty cloud.
    Cloud empty;
    EXPECT_EQ(PC::RemoveStatisticalOutliers(empty).Status, PC::OutlierRemovalStatus::EmptyInput);

    // KNeighbors == 0.
    std::vector<std::size_t> outliers;
    Cloud cloud = MakeTwoClusterFixture(outliers);
    PC::StatisticalOutlierRemovalParams zeroK{};
    zeroK.KNeighbors = 0;
    EXPECT_EQ(PC::RemoveStatisticalOutliers(cloud, zeroK).Status,
              PC::OutlierRemovalStatus::InvalidParameters);

    // Too few points to fill a neighborhood.
    Cloud tiny;
    tiny.AddPoint(glm::vec3(0.0f));
    tiny.AddPoint(glm::vec3(1.0f, 0.0f, 0.0f));
    PC::StatisticalOutlierRemovalParams bigK{};
    bigK.KNeighbors = 16;
    EXPECT_EQ(PC::RemoveStatisticalOutliers(tiny, bigK).Status,
              PC::OutlierRemovalStatus::InsufficientPoints);

    // A KNeighbors near SIZE_MAX must still fail closed, not wrap k + 1 to 0
    // and keep every point with Success.
    PC::StatisticalOutlierRemovalParams overflowK{};
    overflowK.KNeighbors = std::numeric_limits<std::size_t>::max();
    const PC::OutlierRemovalResult overflowResult = PC::RemoveStatisticalOutliers(cloud, overflowK);
    EXPECT_EQ(overflowResult.Status, PC::OutlierRemovalStatus::InsufficientPoints);
    EXPECT_EQ(overflowResult.KeptCount, std::size_t{0});
}

// =============================================================================
// Radius outlier removal
// =============================================================================

TEST(PointCloudOutlierRemoval, RadiusRejectsIsolatedPoints)
{
    std::vector<std::size_t> outliers;
    Cloud cloud = MakeTwoClusterFixture(outliers);
    const std::size_t total = cloud.VerticesSize();

    PC::RadiusOutlierRemovalParams params{};
    params.SearchRadius = 0.15f; // ~3x cluster spacing.
    params.MinNeighbors = 3;

    const PC::OutlierRemovalResult result = PC::RemoveRadiusOutliers(cloud, params);

    ASSERT_EQ(result.Status, PC::OutlierRemovalStatus::Success);
    EXPECT_EQ(result.KeptCount + result.RejectedCount, total);
    for (std::size_t idx : outliers)
        EXPECT_TRUE(Contains(result.RejectedIndices, idx));
    EXPECT_EQ(result.KeptCount, total - outliers.size());

    // Radius removal does not populate the distance-distribution diagnostics.
    EXPECT_FLOAT_EQ(result.MeanDistance, 0.0f);
    EXPECT_FLOAT_EQ(result.StdDevDistance, 0.0f);
    EXPECT_EQ(result.Filtered.VerticesSize(), result.KeptCount);
}

TEST(PointCloudOutlierRemoval, RadiusIsDeterministicAndAscending)
{
    std::vector<std::size_t> outliers;
    Cloud cloud = MakeTwoClusterFixture(outliers);

    PC::RadiusOutlierRemovalParams params{};
    params.SearchRadius = 0.15f;
    params.MinNeighbors = 3;

    const PC::OutlierRemovalResult a = PC::RemoveRadiusOutliers(cloud, params);
    const PC::OutlierRemovalResult b = PC::RemoveRadiusOutliers(cloud, params);

    EXPECT_EQ(a.KeptIndices, b.KeptIndices);
    EXPECT_EQ(a.RejectedIndices, b.RejectedIndices);
    EXPECT_TRUE(IsStrictlyAscending(a.KeptIndices));
    EXPECT_TRUE(IsStrictlyAscending(a.RejectedIndices));
}

TEST(PointCloudOutlierRemoval, RadiusInvalidInputs)
{
    Cloud empty;
    PC::RadiusOutlierRemovalParams ok{};
    ok.SearchRadius = 0.5f;
    EXPECT_EQ(PC::RemoveRadiusOutliers(empty, ok).Status, PC::OutlierRemovalStatus::EmptyInput);

    std::vector<std::size_t> outliers;
    Cloud cloud = MakeTwoClusterFixture(outliers);

    PC::RadiusOutlierRemovalParams zeroR{};
    zeroR.SearchRadius = 0.0f;
    EXPECT_EQ(PC::RemoveRadiusOutliers(cloud, zeroR).Status,
              PC::OutlierRemovalStatus::InvalidParameters);

    PC::RadiusOutlierRemovalParams negR{};
    negR.SearchRadius = -1.0f;
    EXPECT_EQ(PC::RemoveRadiusOutliers(cloud, negR).Status,
              PC::OutlierRemovalStatus::InvalidParameters);
}

TEST(PointCloudOutlierRemoval, RadiusRejectsNonFinitePositions)
{
    std::vector<std::size_t> outliers;
    Cloud cloud = MakeTwoClusterFixture(outliers);
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    cloud.AddPoint(glm::vec3(0.025f, 0.025f, nan));
    const std::size_t nanIdx = cloud.VerticesSize() - 1;

    PC::RadiusOutlierRemovalParams params{};
    params.SearchRadius = 0.15f;
    params.MinNeighbors = 3;

    const PC::OutlierRemovalResult result = PC::RemoveRadiusOutliers(cloud, params);
    ASSERT_EQ(result.Status, PC::OutlierRemovalStatus::Success);
    EXPECT_TRUE(Contains(result.RejectedIndices, nanIdx));
    EXPECT_GE(result.NonFiniteCount, std::size_t{1});
}
