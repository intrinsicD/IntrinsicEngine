#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

#include <glm/glm.hpp>

import Geometry;

TEST(KDTree, RejectsDegenerateBuildInputs)
{
    Geometry::KDTree tree;

    std::array<glm::vec3, 0> empty{};
    EXPECT_FALSE(tree.BuildFromPoints(empty).has_value());

    Geometry::KDTreeBuildParams params{};
    params.LeafSize = 0;

    std::array<glm::vec3, 2> points{
        glm::vec3{0.0f, 0.0f, 0.0f},
        glm::vec3{1.0f, 0.0f, 0.0f},
    };
    EXPECT_FALSE(tree.BuildFromPoints(points, params).has_value());

    params = {};
    params.MinSplitExtent = -1.0f;
    EXPECT_FALSE(tree.BuildFromPoints(points, params).has_value());
}

TEST(KDTree, KnnMatchesBruteForceOrderingForPointAabbs)
{
    const std::vector<glm::vec3> points{
        {-2.0f, 1.0f, 0.0f}, {-1.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f},
        {2.0f, 0.0f, 1.0f},  {0.0f, 2.0f, 2.0f},  {1.5f, -0.5f, 0.0f}, {-0.5f, 1.5f, 0.5f},
    };

    Geometry::KDTree tree;
    auto buildResult = tree.BuildFromPoints(points);
    ASSERT_TRUE(buildResult.has_value());
    EXPECT_EQ(buildResult->ElementCount, points.size());

    const glm::vec3 query{0.25f, 0.5f, 0.25f};
    constexpr std::uint32_t k = 4;

    std::vector<Geometry::KDTree::ElementIndex> kdIndices;
    const auto knn = tree.QueryKnn(query, k, kdIndices);
    ASSERT_TRUE(knn.has_value());
    EXPECT_EQ(knn->ReturnedCount, k);

    std::vector<std::pair<float, std::uint32_t>> brute;
    brute.reserve(points.size());
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(points.size()); ++i)
    {
        const glm::vec3 d = points[i] - query;
        brute.emplace_back(glm::dot(d, d), i);
    }
    std::sort(brute.begin(), brute.end(), [](const auto& a, const auto& b)
    {
        if (a.first != b.first) return a.first < b.first;
        return a.second < b.second;
    });

    ASSERT_EQ(kdIndices.size(), k);
    for (std::size_t i = 0; i < k; ++i)
    {
        EXPECT_EQ(kdIndices[i], brute[i].second);
    }
}

TEST(KDTree, RadiusQueryMatchesBruteForceSetForPointAabbs)
{
    const std::vector<glm::vec3> points{
        {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f},
        {2.0f, 2.0f, 2.0f}, {-1.0f, 0.0f, 0.0f},
    };

    Geometry::KDTree tree;
    ASSERT_TRUE(tree.BuildFromPoints(points).has_value());

    const glm::vec3 query{0.0f, 0.0f, 0.0f};
    constexpr float radius = 1.01f;

    std::vector<Geometry::KDTree::ElementIndex> kdIndices;
    const auto radiusResult = tree.QueryRadius(query, radius, kdIndices);
    ASSERT_TRUE(radiusResult.has_value());

    std::vector<std::uint32_t> brute;
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(points.size()); ++i)
    {
        const glm::vec3 d = points[i] - query;
        if (glm::dot(d, d) <= radius * radius)
        {
            brute.push_back(i);
        }
    }
    std::sort(brute.begin(), brute.end());

    EXPECT_EQ(kdIndices, brute);
}

TEST(KDTree, SupportsVolumetricElementsThroughAabbInput)
{
    std::vector<Geometry::AABB> boxes{
        Geometry::AABB{.Min = {-1.0f, -1.0f, -1.0f}, .Max = {1.0f, 1.0f, 1.0f}},
        Geometry::AABB{.Min = {3.0f, 3.0f, 3.0f}, .Max = {4.0f, 4.0f, 4.0f}},
        Geometry::AABB{.Min = {-4.0f, 0.0f, 0.0f}, .Max = {-3.0f, 1.0f, 1.0f}},
    };

    Geometry::KDTree tree;
    ASSERT_TRUE(tree.Build(boxes).has_value());

    std::vector<Geometry::KDTree::ElementIndex> overlap;
    tree.QueryAABB(Geometry::AABB{.Min = {-0.5f, -0.5f, -0.5f}, .Max = {0.25f, 0.25f, 0.25f}}, overlap);
    ASSERT_EQ(overlap.size(), 1u);
    EXPECT_EQ(overlap[0], 0u);

    std::vector<Geometry::KDTree::ElementIndex> sphereOverlap;
    tree.QuerySphere(Geometry::Sphere{.Center = {3.5f, 3.5f, 3.5f}, .Radius = 1.0f}, sphereOverlap);
    ASSERT_EQ(sphereOverlap.size(), 1u);
    EXPECT_EQ(sphereOverlap[0], 1u);
}

TEST(KDTree, HandlesCoincidentElementsAndInvalidQueries)
{
    std::vector<Geometry::AABB> boxes{
        Geometry::AABB{.Min = {1.0f, 2.0f, 3.0f}, .Max = {1.0f, 2.0f, 3.0f}},
        Geometry::AABB{.Min = {1.0f, 2.0f, 3.0f}, .Max = {1.0f, 2.0f, 3.0f}},
        Geometry::AABB{.Min = {1.0f, 2.0f, 3.0f}, .Max = {1.0f, 2.0f, 3.0f}},
        Geometry::AABB{.Min = {2.0f, 2.0f, 3.0f}, .Max = {2.0f, 2.0f, 3.0f}},
    };

    Geometry::KDTree tree;
    ASSERT_TRUE(tree.Build(boxes).has_value());

    std::vector<Geometry::KDTree::ElementIndex> indices;
    const auto knn = tree.QueryKnn(glm::vec3{1.0f, 2.0f, 3.0f}, 3, indices);
    ASSERT_TRUE(knn.has_value());
    EXPECT_EQ(knn->ReturnedCount, 3u);

    EXPECT_FALSE(tree.QueryKnn(glm::vec3{0.0f}, 0, indices).has_value());
    EXPECT_FALSE(tree.QueryRadius(glm::vec3{0.0f}, -1.0f, indices).has_value());
    EXPECT_FALSE(tree.QueryRadius(glm::vec3{0.0f}, std::numeric_limits<float>::quiet_NaN(), indices).has_value());
}
