#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <random>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

import Geometry.AABB;
import Geometry.Sphere;
import Geometry.Statistics;
import Geometry.Octree;
import Geometry.KDTree;
import Geometry.BVH;
import Geometry.Overlap;
import Geometry.Properties;
import Geometry.Ray;

using namespace Geometry;

// -----------------------------------------------------------------------------
// Helper Functions
// -----------------------------------------------------------------------------

static std::vector<AABB> GenerateRandomAABBs(size_t count, float worldSize, float maxBoxSize, unsigned seed = 42)
{
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> posDist(-worldSize / 2, worldSize / 2);
    std::uniform_real_distribution<float> sizeDist(0.1f, maxBoxSize);

    std::vector<AABB> result;
    result.reserve(count);

    for (size_t i = 0; i < count; ++i)
    {
        glm::vec3 center(posDist(rng), posDist(rng), posDist(rng));
        glm::vec3 halfSize(sizeDist(rng), sizeDist(rng), sizeDist(rng));
        result.push_back(AABB{center - halfSize, center + halfSize});
    }

    return result;
}

static std::vector<AABB> GenerateGridAABBs(int gridSize, float spacing)
{
    std::vector<AABB> result;
    result.reserve(gridSize * gridSize * gridSize);

    float boxSize = spacing * 0.8f;  // Slight gap between boxes

    for (int x = 0; x < gridSize; ++x)
    {
        for (int y = 0; y < gridSize; ++y)
        {
            for (int z = 0; z < gridSize; ++z)
            {
                glm::vec3 center(x * spacing, y * spacing, z * spacing);
                glm::vec3 halfSize(boxSize * 0.5f);
                result.push_back(AABB{center - halfSize, center + halfSize});
            }
        }
    }

    return result;
}

// -----------------------------------------------------------------------------
// BuildFromPoints Tests
// -----------------------------------------------------------------------------

TEST(Octree, BuildFromPointsMatchesExplicitPointAabbs)
{
    const std::array<glm::vec3, 6> points{
        glm::vec3{-2.0f, 0.0f, 1.0f},
        glm::vec3{-1.0f, 1.0f, 0.0f},
        glm::vec3{0.0f, 0.0f, 0.0f},
        glm::vec3{1.0f, 1.0f, 0.0f},
        glm::vec3{2.0f, 0.0f, 1.0f},
        glm::vec3{0.0f, 2.0f, 2.0f},
    };

    Octree::SplitPolicy policy{};
    policy.SplitPoint = Octree::SplitPoint::Mean;
    policy.TightChildren = true;

    Octree explicitTree;
    std::vector<AABB> pointAabbs;
    pointAabbs.reserve(points.size());
    for (const glm::vec3& p : points)
        pointAabbs.push_back(AABB{.Min = p, .Max = p});

    ASSERT_TRUE(explicitTree.Build(std::move(pointAabbs), policy, 2u, 8u));

    Octree pointsTree;
    ASSERT_TRUE(pointsTree.BuildFromPoints(points, policy, 2u, 8u));

    ASSERT_EQ(pointsTree.ElementAabbs.size(), explicitTree.ElementAabbs.size());
    EXPECT_EQ(pointsTree.m_Nodes.size(), explicitTree.m_Nodes.size());

    for (std::size_t i = 0; i < points.size(); ++i)
    {
        EXPECT_EQ(pointsTree.ElementAabbs[i].Min, explicitTree.ElementAabbs[i].Min);
        EXPECT_EQ(pointsTree.ElementAabbs[i].Max, explicitTree.ElementAabbs[i].Max);
    }

    const std::array<glm::vec3, 3> queries{
        glm::vec3{0.05f, 0.0f, 0.02f},
        glm::vec3{1.8f, 0.1f, 0.9f},
        glm::vec3{-1.1f, 0.9f, 0.1f},
    };

    for (const glm::vec3& query : queries)
    {
        std::size_t explicitNearest = Octree::kInvalidIndex;
        std::size_t pointsNearest = Octree::kInvalidIndex;
        explicitTree.QueryNearest(query, explicitNearest);
        pointsTree.QueryNearest(query, pointsNearest);
        EXPECT_EQ(pointsNearest, explicitNearest);

        std::vector<std::size_t> explicitKnn;
        std::vector<std::size_t> pointsKnn;
        explicitTree.QueryKNN(query, 3u, explicitKnn);
        pointsTree.QueryKNN(query, 3u, pointsKnn);
        EXPECT_EQ(pointsKnn, explicitKnn);
    }
}

// -----------------------------------------------------------------------------
// Build Tests
// -----------------------------------------------------------------------------

TEST(Octree, Build_EmptyInput)
{
    Octree octree;
    std::vector<AABB> empty;

    Octree::SplitPolicy policy;
    bool success = octree.Build(empty, policy, 8, 10);

    EXPECT_FALSE(success);
}

TEST(Octree, Build_SingleElement)
{
    Octree octree;
    std::vector<AABB> aabbs = {AABB{{0, 0, 0}, {1, 1, 1}}};

    Octree::SplitPolicy policy;
    bool success = octree.Build(aabbs, policy, 8, 10);

    EXPECT_TRUE(success);
    EXPECT_EQ(octree.m_Nodes.size(), 1u);  // Just root
    EXPECT_TRUE(octree.m_Nodes[0].IsLeaf);
}

TEST(Octree, Build_SmallSet)
{
    Octree octree;
    auto aabbs = GenerateRandomAABBs(10, 100.0f, 5.0f);

    Octree::SplitPolicy policy;
    bool success = octree.Build(aabbs, policy, 4, 10);

    EXPECT_TRUE(success);
    EXPECT_TRUE(octree.ValidateStructure());
}

TEST(Octree, Build_LargeSet)
{
    Octree octree;
    auto aabbs = GenerateRandomAABBs(1000, 100.0f, 2.0f);

    Octree::SplitPolicy policy;
    policy.SplitPoint = Octree::SplitPoint::Median;
    bool success = octree.Build(aabbs, policy, 8, 10);

    EXPECT_TRUE(success);
    EXPECT_TRUE(octree.ValidateStructure());
}

TEST(Octree, Build_DifferentSplitPolicies)
{
    auto aabbs = GenerateRandomAABBs(100, 50.0f, 3.0f);

    // Test Center split
    {
        Octree octree;
        Octree::SplitPolicy policy;
        policy.SplitPoint = Octree::SplitPoint::Center;
        EXPECT_TRUE(octree.Build(aabbs, policy, 8, 10));
        EXPECT_TRUE(octree.ValidateStructure());
    }

    // Test Mean split
    {
        Octree octree;
        Octree::SplitPolicy policy;
        policy.SplitPoint = Octree::SplitPoint::Mean;
        EXPECT_TRUE(octree.Build(aabbs, policy, 8, 10));
        EXPECT_TRUE(octree.ValidateStructure());
    }

    // Test Median split
    {
        Octree octree;
        Octree::SplitPolicy policy;
        policy.SplitPoint = Octree::SplitPoint::Median;
        EXPECT_TRUE(octree.Build(aabbs, policy, 8, 10));
        EXPECT_TRUE(octree.ValidateStructure());
    }
}

// -----------------------------------------------------------------------------
// AABB Query Tests
// -----------------------------------------------------------------------------

TEST(Octree, QueryAABB_EmptyResult)
{
    Octree octree;
    auto aabbs = GenerateRandomAABBs(100, 50.0f, 2.0f);

    Octree::SplitPolicy policy;
    ASSERT_TRUE(octree.Build(aabbs, policy, 8, 10));

    // Query far outside the data
    AABB query{{1000, 1000, 1000}, {1001, 1001, 1001}};
    std::vector<size_t> results;
    octree.QueryAABB(query, results);

    EXPECT_TRUE(results.empty());
}

TEST(Octree, QueryAABB_AllElements)
{
    Octree octree;
    auto aabbs = GenerateRandomAABBs(50, 10.0f, 1.0f);

    Octree::SplitPolicy policy;
    ASSERT_TRUE(octree.Build(aabbs, policy, 8, 10));

    // Query encompassing all elements
    AABB query{{-100, -100, -100}, {100, 100, 100}};
    std::vector<size_t> results;
    octree.QueryAABB(query, results);

    EXPECT_EQ(results.size(), aabbs.size());
}

TEST(Octree, QueryAABB_PartialOverlap)
{
    Octree octree;
    auto aabbs = GenerateGridAABBs(5, 2.0f);  // 125 boxes in 5x5x5 grid

    Octree::SplitPolicy policy;
    ASSERT_TRUE(octree.Build(aabbs, policy, 8, 10));

    // Query should hit a subset
    AABB query{{0, 0, 0}, {4, 4, 4}};  // Should hit ~27 boxes (3x3x3 region)
    std::vector<size_t> results;
    octree.QueryAABB(query, results);

    EXPECT_GT(results.size(), 0u);
    EXPECT_LT(results.size(), aabbs.size());

    // Verify all results actually overlap
    for (size_t idx : results)
    {
        EXPECT_TRUE(TestOverlap(aabbs[idx], query));
    }
}

TEST(Octree, QueryAABB_CorrectResults)
{
    Octree octree;
    auto aabbs = GenerateRandomAABBs(200, 50.0f, 2.0f, 123);

    Octree::SplitPolicy policy;
    ASSERT_TRUE(octree.Build(aabbs, policy, 8, 10));

    AABB query{{-10, -10, -10}, {10, 10, 10}};
    std::vector<size_t> octreeResults;
    octree.QueryAABB(query, octreeResults);

    // Brute force check
    std::vector<size_t> bruteForceResults;
    for (size_t i = 0; i < aabbs.size(); ++i)
    {
        if (TestOverlap(aabbs[i], query))
        {
            bruteForceResults.push_back(i);
        }
    }

    std::sort(octreeResults.begin(), octreeResults.end());
    std::sort(bruteForceResults.begin(), bruteForceResults.end());

    EXPECT_EQ(octreeResults, bruteForceResults);
}

// -----------------------------------------------------------------------------
// Sphere Query Tests
// -----------------------------------------------------------------------------

TEST(Octree, QuerySphere_Basic)
{
    Octree octree;
    auto aabbs = GenerateGridAABBs(5, 2.0f);

    Octree::SplitPolicy policy;
    ASSERT_TRUE(octree.Build(aabbs, policy, 8, 10));

    Sphere query{{4, 4, 4}, 3.0f};
    std::vector<size_t> results;
    octree.QuerySphere(query, results);

    EXPECT_GT(results.size(), 0u);

    // Verify correctness
    for (size_t idx : results)
    {
        EXPECT_TRUE(TestOverlap(aabbs[idx], query));
    }
}

TEST(Octree, QuerySphere_CorrectResults)
{
    Octree octree;
    auto aabbs = GenerateRandomAABBs(150, 40.0f, 2.0f, 456);

    Octree::SplitPolicy policy;
    ASSERT_TRUE(octree.Build(aabbs, policy, 8, 10));

    Sphere query{{0, 0, 0}, 10.0f};
    std::vector<size_t> octreeResults;
    octree.QuerySphere(query, octreeResults);

    // Brute force
    std::vector<size_t> bruteForceResults;
    for (size_t i = 0; i < aabbs.size(); ++i)
    {
        if (TestOverlap(aabbs[i], query))
        {
            bruteForceResults.push_back(i);
        }
    }

    std::sort(octreeResults.begin(), octreeResults.end());
    std::sort(bruteForceResults.begin(), bruteForceResults.end());

    EXPECT_EQ(octreeResults, bruteForceResults);
}

// -----------------------------------------------------------------------------
// Ray Query Tests
// -----------------------------------------------------------------------------

TEST(Octree, QueryRay_Basic)
{
    Octree octree;
    auto aabbs = GenerateGridAABBs(5, 2.0f);

    Octree::SplitPolicy policy;
    ASSERT_TRUE(octree.Build(aabbs, policy, 8, 10));

    Ray query{{-10, 2, 2}, glm::normalize(glm::vec3(1, 0, 0))};
    std::vector<size_t> results;
    octree.QueryRay(query, results);

    // Ray along X at Y=2, Z=2 should hit several boxes
    EXPECT_GT(results.size(), 0u);
}

TEST(Octree, QueryRay_Miss)
{
    Octree octree;
    std::vector<AABB> aabbs = {AABB{{0, 0, 0}, {1, 1, 1}}};

    Octree::SplitPolicy policy;
    ASSERT_TRUE(octree.Build(aabbs, policy, 8, 10));

    // Ray that misses the box
    Ray query{{10, 10, 10}, glm::normalize(glm::vec3(1, 0, 0))};
    std::vector<size_t> results;
    octree.QueryRay(query, results);

    EXPECT_TRUE(results.empty());
}

// -----------------------------------------------------------------------------
// Nearest Neighbor Query Tests
// -----------------------------------------------------------------------------

TEST(Octree, QueryNearest_Basic)
{
    Octree octree;
    std::vector<AABB> aabbs = {
        AABB{{0, 0, 0}, {1, 1, 1}},
        AABB{{10, 10, 10}, {11, 11, 11}},
        AABB{{-20, 0, 0}, {-19, 1, 1}}
    };

    Octree::SplitPolicy policy;
    ASSERT_TRUE(octree.Build(aabbs, policy, 8, 10));

    glm::vec3 queryPoint{0.5f, 0.5f, 0.5f};
    size_t result;
    octree.QueryNearest(queryPoint, result);

    EXPECT_EQ(result, 0u);  // First box contains the point
}

TEST(Octree, QueryNearest_CorrectResult)
{
    Octree octree;
    auto aabbs = GenerateRandomAABBs(100, 50.0f, 2.0f, 789);

    Octree::SplitPolicy policy;
    ASSERT_TRUE(octree.Build(aabbs, policy, 8, 10));

    glm::vec3 queryPoint{5.0f, 5.0f, 5.0f};
    size_t octreeResult;
    octree.QueryNearest(queryPoint, octreeResult);

    // Brute force find nearest
    double minDistSq = std::numeric_limits<double>::max();
    size_t bruteForceResult = 0;
    for (size_t i = 0; i < aabbs.size(); ++i)
    {
        double distSq = SquaredDistance(aabbs[i], queryPoint);
        if (distSq < minDistSq)
        {
            minDistSq = distSq;
            bruteForceResult = i;
        }
    }

    EXPECT_EQ(octreeResult, bruteForceResult);
}

// -----------------------------------------------------------------------------
// KNN Query Tests
// -----------------------------------------------------------------------------

TEST(Octree, QueryKNN_Basic)
{
    Octree octree;
    std::vector<AABB> aabbs = {
        AABB{{0, 0, 0}, {1, 1, 1}},      // Closest to origin
        AABB{{3, 0, 0}, {4, 1, 1}},      // Second
        AABB{{6, 0, 0}, {7, 1, 1}},      // Third
        AABB{{10, 0, 0}, {11, 1, 1}},    // Fourth
        AABB{{20, 0, 0}, {21, 1, 1}}     // Fifth
    };

    Octree::SplitPolicy policy;
    ASSERT_TRUE(octree.Build(aabbs, policy, 8, 10));

    glm::vec3 queryPoint{0.5f, 0.5f, 0.5f};
    std::vector<size_t> results;
    octree.QueryKNN(queryPoint, 3, results);

    ASSERT_EQ(results.size(), 3u);
    // Results should be sorted by distance (closest first)
    EXPECT_EQ(results[0], 0u);  // Closest
    EXPECT_EQ(results[1], 1u);  // Second
    EXPECT_EQ(results[2], 2u);  // Third
}

TEST(Octree, QueryKNN_KGreaterThanElements)
{
    Octree octree;
    std::vector<AABB> aabbs = {
        AABB{{0, 0, 0}, {1, 1, 1}},
        AABB{{5, 0, 0}, {6, 1, 1}}
    };

    Octree::SplitPolicy policy;
    ASSERT_TRUE(octree.Build(aabbs, policy, 8, 10));

    std::vector<size_t> results;
    octree.QueryKNN({0, 0, 0}, 10, results);  // Ask for 10, only 2 exist

    EXPECT_EQ(results.size(), 2u);
}

TEST(Octree, QueryKNN_CorrectResults)
{
    Octree octree;
    auto aabbs = GenerateRandomAABBs(100, 50.0f, 2.0f, 321);

    Octree::SplitPolicy policy;
    ASSERT_TRUE(octree.Build(aabbs, policy, 8, 10));

    glm::vec3 queryPoint{0, 0, 0};
    const size_t k = 5;

    std::vector<size_t> octreeResults;
    octree.QueryKNN(queryPoint, k, octreeResults);

    // Brute force KNN
    std::vector<std::pair<double, size_t>> allDistances;
    for (size_t i = 0; i < aabbs.size(); ++i)
    {
        allDistances.emplace_back(SquaredDistance(aabbs[i], queryPoint), i);
    }
    std::sort(allDistances.begin(), allDistances.end());

    std::vector<size_t> bruteForceResults;
    for (size_t i = 0; i < k && i < allDistances.size(); ++i)
    {
        bruteForceResults.push_back(allDistances[i].second);
    }

    EXPECT_EQ(octreeResults, bruteForceResults);
}

// -----------------------------------------------------------------------------
// Node Property Tests
// -----------------------------------------------------------------------------

TEST(Octree, AddNodeProperty)
{
    Octree octree;
    auto aabbs = GenerateRandomAABBs(50, 20.0f, 2.0f);

    Octree::SplitPolicy policy;
    ASSERT_TRUE(octree.Build(aabbs, policy, 8, 10));

    auto floatProp = octree.AddNodeProperty<float>("Density", 0.0f);
    EXPECT_TRUE(floatProp.IsValid());

    // Set some values using NodeHandle
    NodeHandle node0{0};
    floatProp[node0] = 1.5f;
    EXPECT_FLOAT_EQ(floatProp[node0], 1.5f);
}

TEST(Octree, GetNodeProperty)
{
    Octree octree;
    auto aabbs = GenerateRandomAABBs(20, 10.0f, 1.0f);

    Octree::SplitPolicy policy;
    ASSERT_TRUE(octree.Build(aabbs, policy, 8, 10));

    [[maybe_unused]] auto _ = octree.AddNodeProperty<int>("Count", 42);

    auto prop = octree.GetNodeProperty<int>("Count");
    EXPECT_TRUE(prop.IsValid());

    NodeHandle node0{0};
    EXPECT_EQ(prop[node0], 42);  // Default value
}

TEST(Octree, HasNodeProperty)
{
    Octree octree;
    auto aabbs = GenerateRandomAABBs(10, 5.0f, 1.0f);

    Octree::SplitPolicy policy;
    ASSERT_TRUE(octree.Build(aabbs, policy, 8, 10));

    EXPECT_FALSE(octree.HasNodeProperty("Custom"));

    [[maybe_unused]] auto _ = octree.AddNodeProperty<float>("Custom", 0.0f);

    EXPECT_TRUE(octree.HasNodeProperty("Custom"));
}

// -----------------------------------------------------------------------------
// Edge Cases
// -----------------------------------------------------------------------------

TEST(Octree, AllElementsAtSamePoint)
{
    Octree octree;
    std::vector<AABB> aabbs;
    for (int i = 0; i < 100; ++i)
    {
        aabbs.push_back(AABB{{0, 0, 0}, {0.001f, 0.001f, 0.001f}});
    }

    Octree::SplitPolicy policy;
    bool success = octree.Build(aabbs, policy, 8, 10);

    EXPECT_TRUE(success);
    EXPECT_TRUE(octree.ValidateStructure());
}

TEST(Octree, LargeExtentDifferences)
{
    Octree octree;
    std::vector<AABB> aabbs = {
        AABB{{0, 0, 0}, {0.001f, 0.001f, 0.001f}},          // Tiny
        AABB{{-1000, -1000, -1000}, {1000, 1000, 1000}}     // Huge
    };

    Octree::SplitPolicy policy;
    bool success = octree.Build(aabbs, policy, 8, 10);

    EXPECT_TRUE(success);

    // Query should find both when encompassing
    std::vector<size_t> results;
    octree.QueryAABB(AABB{{-2000, -2000, -2000}, {2000, 2000, 2000}}, results);
    EXPECT_EQ(results.size(), 2u);
}

TEST(Octree, FailedRebuildLeavesEmptyTree)
{
    Octree octree;
    std::vector<glm::vec3> points;
    for (int i = 0; i < 1000; ++i) points.push_back({float(i), 0.0f, 0.0f});
    ASSERT_TRUE(octree.BuildFromPoints(points, {}, 8, 10));

    // A failed rebuild must not keep nodes that index the new (empty) element array.
    EXPECT_FALSE(octree.BuildFromPoints(std::vector<glm::vec3>{}, {}, 8, 10));
    EXPECT_TRUE(octree.m_Nodes.empty());
    EXPECT_TRUE(octree.GetElementIndices().empty());
    EXPECT_TRUE(octree.ValidateStructure());

    std::vector<size_t> results{7u};
    octree.QueryKNN({3.0f, 0.0f, 0.0f}, 4, results);
    EXPECT_TRUE(results.empty());
    octree.QueryAABB(AABB{{-1, -1, -1}, {1, 1, 1}}, results);
    EXPECT_TRUE(results.empty());
    std::size_t nearest = 0;
    octree.QueryNearest({3.0f, 0.0f, 0.0f}, nearest);
    EXPECT_EQ(nearest, std::numeric_limits<std::size_t>::max());
}

TEST(Octree, RejectsNonFiniteElementsAndClearsPreviousTree)
{
    Octree octree;
    std::vector<glm::vec3> points{{0, 0, 0}, {1, 0, 0}, {2, 0, 0}};
    ASSERT_TRUE(octree.BuildFromPoints(points, {}, 1, 10));
    for (const float bad : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
    {
        points[1].y = bad;
        EXPECT_FALSE(octree.BuildFromPoints(points, {}, 1, 10));
        EXPECT_TRUE(octree.m_Nodes.empty());
        EXPECT_TRUE(octree.ElementAabbs.empty());
    }
    // Inverted boxes are invalid too.
    EXPECT_FALSE(octree.Build(std::vector<AABB>{AABB{}}, {}, 1, 10));
}

TEST(Octree, OverlapQueryHandlesManyPendingSiblingsInDeepTrees)
{
    // Planar staircase: each level holds three single-point octants plus a recursive cluster
    // in the octant that is visited first, so three siblings stay pending per level. 45 levels
    // exceed the former fixed 128-entry traversal stack.
    constexpr int levels = 45;
    std::vector<glm::vec3> points{{0.0f, 0.0f, 0.0f}};
    for (int level = 0; level < levels; ++level)
    {
        const float c = -std::pow(3.0f, -float(level));
        points.push_back({c, c, 0.0f});
        points.push_back({c, 0.0f, 0.0f});
        points.push_back({0.0f, c, 0.0f});
    }
    Octree octree;
    ASSERT_TRUE(octree.BuildFromPoints(points, {}, 1, 64));
    ASSERT_TRUE(octree.ValidateStructure());

    // A zero-volume query never takes the contained-node shortcut, so it walks every level.
    std::vector<size_t> results;
    octree.QueryAABB(AABB{{-2.0f, -2.0f, 0.0f}, {2.0f, 2.0f, 0.0f}}, results);
    EXPECT_EQ(results.size(), points.size());
}

TEST(Octree, QueryNearestBreaksDistanceTiesBySmallestIndex)
{
    // Six points at exactly unit distance from the origin, in separate leaves. Traversal
    // order must not decide the result: the smallest index wins, as in QueryKNN.
    const std::vector<glm::vec3> points{{0, 0, -1}, {0, -1, 0}, {-1, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    for (std::size_t rotate = 0; rotate < points.size(); ++rotate)
    {
        std::vector<glm::vec3> rotated(points.begin() + rotate, points.end());
        rotated.insert(rotated.end(), points.begin(), points.begin() + rotate);
        Octree octree;
        ASSERT_TRUE(octree.BuildFromPoints(rotated, {}, 1, 10));
        std::size_t nearest = 99;
        octree.QueryNearest({0, 0, 0}, nearest);
        EXPECT_EQ(nearest, 0u) << "rotation " << rotate;
        std::vector<std::size_t> knn;
        octree.QueryKNN({0, 0, 0}, 1, knn);
        ASSERT_EQ(knn.size(), 1u);
        EXPECT_EQ(knn[0], nearest);
    }
}
