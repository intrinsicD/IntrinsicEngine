#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <vector>

#include <glm/glm.hpp>

import Geometry.AABB;
import Geometry.Sphere;
import Geometry.BVH;
import Geometry.KDTree;
import Geometry.Properties;
import Geometry.Ray;

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

TEST(BVH, QueryRayIncludesBoundaryCoincidentBoxes)
{
    std::vector<Geometry::AABB> boxes{
        Geometry::AABB{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}},
        Geometry::AABB{{3.0f, 0.0f, 0.0f}, {4.0f, 1.0f, 1.0f}},
    };

    Geometry::BVH bvh;
    ASSERT_TRUE(bvh.Build(boxes).has_value());

    std::vector<Geometry::BVH::ElementIndex> overlaps;
    bvh.QueryRay(Geometry::Ray{{0.0f, 5.0f, 0.0f}, {0.0f, -1.0f, 0.0f}}, overlaps);

    EXPECT_NE(std::find(overlaps.begin(), overlaps.end(), 0u), overlaps.end());
    EXPECT_EQ(std::find(overlaps.begin(), overlaps.end(), 1u), overlaps.end());
}

TEST(BVH, KDTreeNamesAliasTheSharedTree)
{
    static_assert(std::is_same_v<Geometry::KDTree, Geometry::BVH>);
    static_assert(std::is_same_v<Geometry::KDTreeBuildParams, Geometry::BVHBuildParams>);
}

TEST(BVH, RejectsInfiniteBoxesAndClearsPreviousTree)
{
    Geometry::BVH bvh;
    std::vector<Geometry::AABB> boxes{{{0, 0, 0}, {1, 1, 1}}, {{2, 0, 0}, {3, 1, 1}}};
    ASSERT_TRUE(bvh.Build(boxes).has_value());
    boxes[1].Max.x = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(bvh.Build(boxes).has_value());
    EXPECT_TRUE(bvh.Nodes().empty());
}

TEST(BVH, KnnAndRadiusUseElementBoxDistances)
{
    // Extended boxes along x; distances are to the boxes, not their centers.
    std::vector<Geometry::AABB> boxes;
    for (int i = 0; i < 40; ++i)
        boxes.push_back({{float(i) * 3.0f, 0, 0}, {float(i) * 3.0f + 2.0f, 1, 1}});
    Geometry::BVH bvh;
    ASSERT_TRUE(bvh.Build(boxes).has_value());

    const glm::vec3 query{10.5f, 0.5f, 0.5f}; // inside box 3 ([9,11]); box 4 starts at 12
    std::vector<std::uint32_t> out;
    ASSERT_TRUE(bvh.QueryKNN(query, 3, out).has_value());
    EXPECT_EQ(out, (std::vector<std::uint32_t>{3, 4, 2}));
    // Box 2 ends at x = 8, exactly 2.5 away: the radius boundary is inclusive.
    ASSERT_TRUE(bvh.QueryRadius(query, 2.5f, out).has_value());
    EXPECT_EQ(out, (std::vector<std::uint32_t>{2, 3, 4}));
}
