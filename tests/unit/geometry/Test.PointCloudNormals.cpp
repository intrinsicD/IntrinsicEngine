#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>
#include <vector>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>

import Geometry.KDTree;
import Geometry.Octree;
import Geometry.PointCloud;
import Geometry.PointCloud.Normals;
import Geometry.Properties;

namespace
{
    namespace PointNormals = Geometry::PointCloud::Normals;

    [[nodiscard]] std::vector<glm::vec3> MakeFlatGrid(const int n = 10, const float spacing = 0.1f)
    {
        std::vector<glm::vec3> points;
        points.reserve(static_cast<std::size_t>(n * n));
        for (int y = 0; y < n; ++y)
        {
            for (int x = 0; x < n; ++x)
            {
                points.emplace_back(static_cast<float>(x) * spacing,
                                    static_cast<float>(y) * spacing,
                                    0.0f);
            }
        }
        return points;
    }

    [[nodiscard]] std::vector<glm::vec3> MakeSpherePoints(const int nLat = 10, const int nLon = 20)
    {
        std::vector<glm::vec3> points;
        for (int i = 1; i < nLat; ++i)
        {
            const float theta = static_cast<float>(i) / static_cast<float>(nLat) * 3.14159265f;
            for (int j = 0; j < nLon; ++j)
            {
                const float phi = static_cast<float>(j) / static_cast<float>(nLon) * 2.0f * 3.14159265f;
                points.emplace_back(std::sin(theta) * std::cos(phi),
                                    std::sin(theta) * std::sin(phi),
                                    std::cos(theta));
            }
        }
        points.emplace_back(0.0f, 0.0f, 1.0f);
        points.emplace_back(0.0f, 0.0f, -1.0f);
        return points;
    }

    [[nodiscard]] Geometry::PointCloud::Cloud MakeCloud(std::span<const glm::vec3> points)
    {
        Geometry::PointCloud::Cloud cloud;
        cloud.Reserve(points.size());
        for (const glm::vec3 point : points)
        {
            cloud.AddPoint(point);
        }
        return cloud;
    }

    void ExpectFiniteUnit(const glm::vec3 normal)
    {
        EXPECT_TRUE(std::isfinite(normal.x));
        EXPECT_TRUE(std::isfinite(normal.y));
        EXPECT_TRUE(std::isfinite(normal.z));
        EXPECT_NEAR(glm::length(normal), 1.0f, 1.0e-4f);
    }
}

TEST(PointCloudNormals, DebugNamesAreStable)
{
    EXPECT_EQ(PointNormals::DebugName(PointNormals::NeighborhoodBackend::KDTree), "KDTree");
    EXPECT_EQ(PointNormals::DebugName(PointNormals::NeighborhoodBackend::SuppliedKDTree), "SuppliedKDTree");
    EXPECT_EQ(PointNormals::DebugName(PointNormals::NeighborhoodBackend::SuppliedOctree), "SuppliedOctree");
    EXPECT_EQ(PointNormals::DebugName(PointNormals::OrientationMode::None), "None");
    EXPECT_EQ(PointNormals::DebugName(PointNormals::OrientationMode::MinimumSpanningTree), "MinimumSpanningTree");
    EXPECT_EQ(PointNormals::DebugName(PointNormals::RecomputeStatus::Success), "Success");
}

TEST(PointCloudNormals, FlatGridEstimatePointsAlongZ)
{
    const auto points = MakeFlatGrid();

    const auto result = PointNormals::Estimate(points);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->Normals.size(), points.size());
    EXPECT_EQ(result->Backend, PointNormals::NeighborhoodBackend::KDTree);
    EXPECT_GT(result->Diagnostics.KNNDistanceEvaluationCount, 0u);

    for (const glm::vec3 normal : result->Normals)
    {
        ExpectFiniteUnit(normal);
        EXPECT_NEAR(std::abs(normal.z), 1.0f, 0.1f);
        EXPECT_NEAR(normal.x, 0.0f, 0.1f);
        EXPECT_NEAR(normal.y, 0.0f, 0.1f);
    }
}

TEST(PointCloudNormals, SphereNormalsAreUnitAndMostlyOutwardAfterOrientation)
{
    const auto points = MakeSpherePoints();

    PointNormals::Params params;
    params.KNeighbors = 15;
    params.Orientation = PointNormals::OrientationMode::MinimumSpanningTree;

    const auto result = PointNormals::Estimate(points, params);
    ASSERT_TRUE(result.has_value());

    std::size_t outwardCount = 0;
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        ExpectFiniteUnit(result->Normals[i]);
        if (glm::dot(points[i], result->Normals[i]) > 0.0f)
        {
            ++outwardCount;
        }
    }

    EXPECT_GT(outwardCount, points.size() * 9u / 10u);
}

TEST(PointCloudNormals, EstimateReturnsNulloptForEmptyOrTooFewPoints)
{
    const std::vector<glm::vec3> empty;
    EXPECT_FALSE(PointNormals::Estimate(empty).has_value());

    const std::array<glm::vec3, 2> points{
        glm::vec3{0.0f, 0.0f, 0.0f},
        glm::vec3{1.0f, 0.0f, 0.0f},
    };
    EXPECT_FALSE(PointNormals::Estimate(std::span<const glm::vec3>{points}).has_value());
}

TEST(PointCloudNormals, OrientationCanBeDisabled)
{
    const auto points = MakeFlatGrid();

    PointNormals::Params params;
    params.Orientation = PointNormals::OrientationMode::None;

    const auto result = PointNormals::Estimate(points, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Diagnostics.FlippedOrientationCount, 0u);
}

TEST(PointCloudNormals, CloudRecomputeWritesCanonicalVertexNormalProperty)
{
    const auto points = MakeFlatGrid();
    auto cloud = MakeCloud(points);

    PointNormals::Params params;
    params.OutputProperty = "v:normal";
    const auto result = PointNormals::Recompute(cloud, params);

    ASSERT_EQ(result.Status, PointNormals::RecomputeStatus::Success);
    EXPECT_EQ(result.Backend, PointNormals::NeighborhoodBackend::KDTree);
    ASSERT_TRUE(result.Normals.IsValid());
    EXPECT_EQ(result.Normals.Vector().size(), points.size());
    EXPECT_EQ(result.Diagnostics.WrittenCount, points.size());
    EXPECT_GE(result.Diagnostics.ValidNormalPointCount, points.size() * 9u / 10u);
    EXPECT_TRUE(cloud.PointProperties().Get<glm::vec3>("v:normal").IsValid());
    EXPECT_FALSE(cloud.PointProperties().Get<glm::vec3>("p:normal").IsValid());

    for (std::size_t i = 0; i < points.size(); ++i)
    {
        ExpectFiniteUnit(result.Normals[Geometry::VertexHandle{static_cast<Geometry::PropertyIndex>(i)}]);
    }
}

TEST(PointCloudNormals, SuppliedKDTreePathReusesCallerIndex)
{
    const auto points = MakeFlatGrid();
    auto cloud = MakeCloud(points);

    Geometry::KDTree tree;
    ASSERT_TRUE(tree.BuildFromPoints(points).has_value());

    PointNormals::Params params;
    params.KNeighbors = 12;
    const auto result = PointNormals::Recompute(cloud, tree, params);

    ASSERT_EQ(result.Status, PointNormals::RecomputeStatus::Success);
    EXPECT_EQ(result.Backend, PointNormals::NeighborhoodBackend::SuppliedKDTree);
    ASSERT_TRUE(result.Normals.IsValid());
    EXPECT_GE(result.Diagnostics.ValidNormalPointCount, points.size() * 9u / 10u);
}

TEST(PointCloudNormals, SuppliedOctreePathReusesCallerIndex)
{
    const auto points = MakeFlatGrid();
    auto cloud = MakeCloud(points);

    Geometry::Octree octree;
    Geometry::Octree::SplitPolicy policy;
    policy.SplitPoint = Geometry::Octree::SplitPoint::Mean;
    policy.TightChildren = true;
    ASSERT_TRUE(octree.BuildFromPoints(points, policy, 16, 8));

    PointNormals::Params params;
    params.KNeighbors = 12;
    const auto result = PointNormals::Recompute(cloud, octree, params);

    ASSERT_EQ(result.Status, PointNormals::RecomputeStatus::Success);
    EXPECT_EQ(result.Backend, PointNormals::NeighborhoodBackend::SuppliedOctree);
    ASSERT_TRUE(result.Normals.IsValid());
    EXPECT_GE(result.Diagnostics.ValidNormalPointCount, points.size() * 9u / 10u);
}

TEST(PointCloudNormals, RadiusSearchFailsClosedWhenTooFewNeighbors)
{
    const auto points = MakeFlatGrid();
    auto cloud = MakeCloud(points);

    PointNormals::Params params;
    params.UseRadiusSearch = true;
    params.Radius = 0.001f;
    params.MinimumNeighbors = 2;

    const auto result = PointNormals::Recompute(cloud, params);

    ASSERT_EQ(result.Status, PointNormals::RecomputeStatus::Success);
    EXPECT_EQ(result.Diagnostics.ValidNormalPointCount, 0u);
    EXPECT_EQ(result.Diagnostics.TooFewNeighborCount, points.size());
    EXPECT_EQ(result.Diagnostics.FallbackPointCount, points.size());
    ASSERT_TRUE(result.Normals.IsValid());
    for (const glm::vec3 normal : result.Normals.Vector())
    {
        EXPECT_EQ(normal, glm::vec3(0.0f, 0.0f, 1.0f));
    }
}

TEST(PointCloudNormals, NonFinitePointDoesNotProduceNaNNormal)
{
    auto points = MakeFlatGrid();
    points[3].x = std::numeric_limits<float>::quiet_NaN();
    auto cloud = MakeCloud(points);

    const auto result = PointNormals::Recompute(cloud);

    ASSERT_EQ(result.Status, PointNormals::RecomputeStatus::Success);
    EXPECT_EQ(result.Diagnostics.NonFinitePointCount, 1u);
    EXPECT_GE(result.Diagnostics.FallbackPointCount, 1u);
    ASSERT_TRUE(result.Normals.IsValid());
    for (const glm::vec3 normal : result.Normals.Vector())
    {
        EXPECT_TRUE(std::isfinite(normal.x));
        EXPECT_TRUE(std::isfinite(normal.y));
        EXPECT_TRUE(std::isfinite(normal.z));
    }
}

TEST(PointCloudNormals, WrongOutputPropertyTypeFailsClosed)
{
    const auto points = MakeFlatGrid();
    auto cloud = MakeCloud(points);
    auto wrongType = cloud.PointProperties().GetOrAdd<float>("v:normal", 0.0f);
    ASSERT_TRUE(wrongType.IsValid());

    const auto result = PointNormals::Recompute(cloud);

    EXPECT_EQ(result.Status, PointNormals::RecomputeStatus::PropertyTypeConflict);
    EXPECT_FALSE(result.Normals.IsValid());
    EXPECT_TRUE(cloud.PointProperties().Get<float>("v:normal").IsValid());
}
