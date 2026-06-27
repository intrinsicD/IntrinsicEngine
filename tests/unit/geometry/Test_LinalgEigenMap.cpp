#include <gtest/gtest.h>

#include <span>
#include <vector>
#include <glm/glm.hpp>

import Geometry.Linalg;

TEST(GeometryLinalgEigenMap, MapsVec3ArrayAsNby3)
{
    std::vector<glm::dvec3> pts{
        {1.0, 2.0, 3.0}, {4.0, 5.0, 6.0}, {-7.0, 8.0, -9.0}};
    auto m = Geometry::Linalg::MapVec3Array(std::span<const glm::dvec3>(pts));
    ASSERT_EQ(m.rows(), 3);
    ASSERT_EQ(m.cols(), 3);
    for (int i = 0; i < 3; ++i)
    {
        EXPECT_DOUBLE_EQ(m(i, 0), pts[i].x);
        EXPECT_DOUBLE_EQ(m(i, 1), pts[i].y);
        EXPECT_DOUBLE_EQ(m(i, 2), pts[i].z);
    }
}

TEST(GeometryLinalgEigenMap, MutableMapAliasesStorage)
{
    std::vector<glm::dvec3> pts{{0.0, 0.0, 0.0}, {1.0, 1.0, 1.0}};
    auto m = Geometry::Linalg::MapVec3Array(std::span<glm::dvec3>(pts));
    m(1, 2) = 42.0; // write through the map
    EXPECT_DOUBLE_EQ(pts[1].z, 42.0); // reflected in the backing storage
}

TEST(GeometryLinalgEigenMap, EmptySpanYieldsZeroRows)
{
    std::vector<glm::dvec3> empty;
    auto m = Geometry::Linalg::MapVec3Array(std::span<const glm::dvec3>(empty));
    EXPECT_EQ(m.rows(), 0);
    EXPECT_EQ(m.cols(), 3);
}
