#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <vector>
#include <random>
#include <algorithm>
#include <numeric>

import Geometry;

using namespace Geometry;

// -----------------------------------------------------------------------------
// Helper Functions
// -----------------------------------------------------------------------------

std::vector<AABB> GenerateRandomAABBs(size_t count, float worldSize, float maxBoxSize, unsigned seed = 42)
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

std::vector<AABB> GenerateGridAABBs(int gridSize, float spacing)
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
    octree.Build(aabbs, policy, 8, 10);

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
    octree.Build(aabbs, policy, 8, 10);

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
    octree.Build(aabbs, policy, 8, 10);

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
    octree.Build(aabbs, policy, 8, 10);

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
    octree.Build(aabbs, policy, 8, 10);

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
    octree.Build(aabbs, policy, 8, 10);

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
    octree.Build(aabbs, policy, 8, 10);

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
    octree.Build(aabbs, policy, 8, 10);

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
    octree.Build(aabbs, policy, 8, 10);

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
    octree.Build(aabbs, policy, 8, 10);

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

TEST(Octree, QueryKnn_Basic)
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
    octree.Build(aabbs, policy, 8, 10);

    glm::vec3 queryPoint{0.5f, 0.5f, 0.5f};
    std::vector<size_t> results;
    octree.QueryKnn(queryPoint, 3, results);

    ASSERT_EQ(results.size(), 3u);
    // Results should be sorted by distance (closest first)
    EXPECT_EQ(results[0], 0u);  // Closest
    EXPECT_EQ(results[1], 1u);  // Second
    EXPECT_EQ(results[2], 2u);  // Third
}

TEST(Octree, QueryKnn_KGreaterThanElements)
{
    Octree octree;
    std::vector<AABB> aabbs = {
        AABB{{0, 0, 0}, {1, 1, 1}},
        AABB{{5, 0, 0}, {6, 1, 1}}
    };

    Octree::SplitPolicy policy;
    octree.Build(aabbs, policy, 8, 10);

    std::vector<size_t> results;
    octree.QueryKnn({0, 0, 0}, 10, results);  // Ask for 10, only 2 exist

    EXPECT_EQ(results.size(), 2u);
}

TEST(Octree, QueryKnn_CorrectResults)
{
    Octree octree;
    auto aabbs = GenerateRandomAABBs(100, 50.0f, 2.0f, 321);

    Octree::SplitPolicy policy;
    octree.Build(aabbs, policy, 8, 10);

    glm::vec3 queryPoint{0, 0, 0};
    const size_t k = 5;

    std::vector<size_t> octreeResults;
    octree.QueryKnn(queryPoint, k, octreeResults);

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
    octree.Build(aabbs, policy, 8, 10);

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
    octree.Build(aabbs, policy, 8, 10);

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
    octree.Build(aabbs, policy, 8, 10);

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

