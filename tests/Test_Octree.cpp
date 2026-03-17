#include <gtest/gtest.h>

#include <array>
#include <vector>

#include <glm/glm.hpp>

import Geometry;

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

    Geometry::Octree::SplitPolicy policy{};
    policy.SplitPoint = Geometry::Octree::SplitPoint::Mean;
    policy.TightChildren = true;

    Geometry::Octree explicitTree;
    std::vector<Geometry::AABB> pointAabbs;
    pointAabbs.reserve(points.size());
    for (const glm::vec3& p : points)
        pointAabbs.push_back(Geometry::AABB{.Min = p, .Max = p});

    ASSERT_TRUE(explicitTree.Build(std::move(pointAabbs), policy, 2u, 8u));

    Geometry::Octree pointsTree;
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
        std::size_t explicitNearest = Geometry::Octree::kInvalidIndex;
        std::size_t pointsNearest = Geometry::Octree::kInvalidIndex;
        explicitTree.QueryNearest(query, explicitNearest);
        pointsTree.QueryNearest(query, pointsNearest);
        EXPECT_EQ(pointsNearest, explicitNearest);

        std::vector<std::size_t> explicitKnn;
        std::vector<std::size_t> pointsKnn;
        explicitTree.QueryKnn(query, 3u, explicitKnn);
        pointsTree.QueryKnn(query, 3u, pointsKnn);
        EXPECT_EQ(pointsKnn, explicitKnn);
    }
}

