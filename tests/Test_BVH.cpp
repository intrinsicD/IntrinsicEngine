#include <gtest/gtest.h>

#include <array>
#include <vector>

#include <glm/glm.hpp>

import Geometry;

TEST(BVH, RejectsDegenerateBuildInputs)
{
    Geometry::BVH bvh;

    std::array<Geometry::AABB, 0> empty{};
    EXPECT_FALSE(bvh.Build(empty).has_value());

    Geometry::BVHBuildParams params{};
    params.LeafSize = 0;
    std::array<Geometry::AABB, 1> one{{Geometry::AABB{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}}};
    EXPECT_FALSE(bvh.Build(one, params).has_value());

    params = {};
    params.MinSplitExtent = -1.0f;
    EXPECT_FALSE(bvh.Build(one, params).has_value());

    std::vector<Geometry::AABB> invalid{
        Geometry::AABB{{1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}}
    };
    EXPECT_FALSE(bvh.Build(invalid).has_value());
}

TEST(BVH, QueryAabbAndRayMatchExpectedPrimitiveSet)
{
    std::vector<Geometry::AABB> boxes{
        Geometry::AABB{{-1.0f, -1.0f, -0.25f}, { 1.0f,  1.0f,  0.25f}},
        Geometry::AABB{{ 3.0f, -1.0f, -1.00f}, { 4.0f,  1.0f,  1.00f}},
        Geometry::AABB{{-4.0f, -1.0f, -1.00f}, {-3.0f,  1.0f,  1.00f}},
        Geometry::AABB{{ 0.2f,  0.2f,  2.00f}, { 0.4f,  0.4f,  2.20f}},
    };

    Geometry::BVH bvh;
    const auto build = bvh.Build(boxes);
    ASSERT_TRUE(build.has_value());
    EXPECT_EQ(build->ElementCount, boxes.size());
    EXPECT_GE(build->NodeCount, 1u);

    std::vector<Geometry::BVH::ElementIndex> overlaps;
    bvh.QueryAABB(Geometry::AABB{{-0.5f, -0.5f, -0.1f}, {0.5f, 0.5f, 0.1f}}, overlaps);
    ASSERT_EQ(overlaps.size(), 1u);
    EXPECT_EQ(overlaps[0], 0u);

    overlaps.clear();
    bvh.QuerySphere(Geometry::Sphere{{3.5f, 0.0f, 0.0f}, 1.0f}, overlaps);
    ASSERT_EQ(overlaps.size(), 1u);
    EXPECT_EQ(overlaps[0], 1u);

    overlaps.clear();
    bvh.QueryRay(Geometry::Ray{{0.0f, 0.0f, -5.0f}, {0.0f, 0.0f, 1.0f}}, overlaps);
    ASSERT_EQ(overlaps.size(), 1u);
    EXPECT_EQ(overlaps[0], 0u);

    overlaps.clear();
    bvh.QueryRay(Geometry::Ray{{0.3f, 0.3f, -5.0f}, {0.0f, 0.0f, 1.0f}}, overlaps);
    ASSERT_EQ(overlaps.size(), 2u);
    EXPECT_EQ(overlaps[0], 0u);
    EXPECT_EQ(overlaps[1], 3u);
}

TEST(BVH, HandlesCoincidentCentroidsWithoutDroppingElements)
{
    std::vector<Geometry::AABB> boxes{
        Geometry::AABB{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}},
        Geometry::AABB{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}},
        Geometry::AABB{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}},
        Geometry::AABB{{1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}},
    };

    Geometry::BVH bvh;
    ASSERT_TRUE(bvh.Build(boxes).has_value());

    std::vector<Geometry::BVH::ElementIndex> overlaps;
    bvh.QuerySphere(Geometry::Sphere{{0.0f, 0.0f, 0.0f}, 0.01f}, overlaps);
    ASSERT_EQ(overlaps.size(), 3u);
    EXPECT_EQ(overlaps[0], 0u);
    EXPECT_EQ(overlaps[1], 1u);
    EXPECT_EQ(overlaps[2], 2u);
}

