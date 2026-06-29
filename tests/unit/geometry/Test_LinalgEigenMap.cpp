#include <gtest/gtest.h>

#include <array>
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

TEST(GeometryLinalgEigenMap, MapAsMatrixAliasesStridedRows)
{
    std::array<double, 12> values{
        1.0, 2.0, -100.0, -200.0,
        3.0, 4.0, -300.0, -400.0,
        5.0, 6.0, -500.0, -600.0};

    auto m = Geometry::Linalg::MapAsMatrix(std::span<double>{values}, 3u, 2u, 4);
    ASSERT_EQ(m.rows(), 3);
    ASSERT_EQ(m.cols(), 2);
    EXPECT_DOUBLE_EQ(m(2, 1), 6.0);

    m(1, 1) = 42.0;
    EXPECT_DOUBLE_EQ(values[5], 42.0);
    EXPECT_DOUBLE_EQ(values[6], -300.0);

    const auto invalid = Geometry::Linalg::MapAsMatrix(std::span<double>{values}.first(5), 3u, 2u, 4);
    EXPECT_EQ(invalid.rows(), 0);
    EXPECT_EQ(invalid.cols(), 0);
}

TEST(GeometryLinalgEigenMap, MapVectorAsMatrixAliasesFixedSizeVectors)
{
    std::vector<glm::dvec4> values{
        {1.0, 2.0, 3.0, 4.0},
        {5.0, 6.0, 7.0, 8.0}};

    auto m = Geometry::Linalg::MapVectorAsMatrix(std::span<glm::dvec4>{values});
    ASSERT_EQ(m.rows(), 2);
    ASSERT_EQ(m.cols(), 4);
    EXPECT_DOUBLE_EQ(m(1, 2), 7.0);

    m(0, 3) = -9.0;
    EXPECT_DOUBLE_EQ(values[0].w, -9.0);

    const auto cm = Geometry::Linalg::MapVectorAsMatrix(std::span<const glm::dvec4>{values});
    EXPECT_DOUBLE_EQ(cm(0, 3), -9.0);
}
