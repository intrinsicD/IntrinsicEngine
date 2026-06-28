#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>

#include <glm/glm.hpp>

import Geometry.AABB;

namespace
{
    constexpr float kTolerance = 1.0e-5f;

    void ExpectVecNear(const glm::vec3& actual, const glm::vec3& expected, float tolerance = kTolerance)
    {
        EXPECT_NEAR(actual.x, expected.x, tolerance);
        EXPECT_NEAR(actual.y, expected.y, tolerance);
        EXPECT_NEAR(actual.z, expected.z, tolerance);
    }

    bool Contains(const Geometry::AABB& box, const glm::vec3& point)
    {
        return point.x >= box.Min.x - kTolerance && point.x <= box.Max.x + kTolerance &&
               point.y >= box.Min.y - kTolerance && point.y <= box.Max.y + kTolerance &&
               point.z >= box.Min.z - kTolerance && point.z <= box.Max.z + kTolerance;
    }

    bool HasPositiveVolumeOverlap(const Geometry::AABB& a, const Geometry::AABB& b)
    {
        const Geometry::AABB overlap = Geometry::Intersection(a, b);
        if (!overlap.IsValid())
        {
            return false;
        }

        const glm::vec3 size = overlap.GetSize();
        return size.x > kTolerance && size.y > kTolerance && size.z > kTolerance;
    }
}

TEST(AABBCubify, MakeCubicExpandsAroundCenterAndContainsOriginal)
{
    const Geometry::AABB box{{-2.0f, -1.0f, 0.25f}, {4.0f, 3.0f, 1.75f}};

    const Geometry::AABB cube = box.MakeCubic();
    ASSERT_TRUE(cube.IsValid());

    const glm::vec3 extents = cube.GetExtents();
    EXPECT_NEAR(extents.x, extents.y, kTolerance);
    EXPECT_NEAR(extents.y, extents.z, kTolerance);
    ExpectVecNear(cube.GetCenter(), box.GetCenter());

    for (const glm::vec3& corner : box.GetCorners())
    {
        EXPECT_TRUE(Contains(cube, corner));
    }
}

TEST(AABBCubify, ChildOctantsTileParentWithoutPositiveOverlap)
{
    const Geometry::AABB parent{{-4.0f, -2.0f, 1.0f}, {4.0f, 6.0f, 5.0f}};
    std::array<Geometry::AABB, 8> children{};

    for (std::uint32_t octant = 0u; octant < 8u; ++octant)
    {
        children[octant] = parent.ChildOctant(octant);
        ASSERT_TRUE(children[octant].IsValid());
        EXPECT_TRUE(Contains(children[octant], parent.OctantCenter(octant)));
        EXPECT_TRUE(Contains(parent, children[octant].Min));
        EXPECT_TRUE(Contains(parent, children[octant].Max));
    }

    const Geometry::AABB unionBox = Geometry::Union(children);
    ExpectVecNear(unionBox.Min, parent.Min);
    ExpectVecNear(unionBox.Max, parent.Max);

    for (std::size_t i = 0; i < children.size(); ++i)
    {
        for (std::size_t j = i + 1u; j < children.size(); ++j)
        {
            EXPECT_FALSE(HasPositiveVolumeOverlap(children[i], children[j]));
        }
    }
}

TEST(AABBCubify, InvalidInputAndOutOfRangeOctantsFailClosed)
{
    const Geometry::AABB invalid;
    const Geometry::AABB cubic = invalid.MakeCubic();
    const Geometry::AABB child = invalid.ChildOctant(0u);
    const glm::vec3 center = invalid.OctantCenter(0u);

    EXPECT_FALSE(cubic.IsValid());
    EXPECT_FALSE(child.IsValid());
    EXPECT_TRUE(std::isfinite(center.x));
    EXPECT_TRUE(std::isfinite(center.y));
    EXPECT_TRUE(std::isfinite(center.z));

    const Geometry::AABB parent{{0.0f, 0.0f, 0.0f}, {2.0f, 2.0f, 2.0f}};
    EXPECT_FALSE(parent.ChildOctant(8u).IsValid());
    ExpectVecNear(parent.OctantCenter(99u), parent.GetCenter());
}
