#include <gtest/gtest.h>
#include <cmath>
#include <numbers>

#include <glm/glm.hpp>

import Geometry;

#include "TestMeshBuilders.h"

// =============================================================================
// Mesh Quality Tests
// =============================================================================

TEST(MeshQuality, EmptyMeshReturnsNullopt)
{
    Geometry::Halfedge::Mesh mesh;
    auto result = Geometry::MeshQuality::ComputeQuality(mesh);
    EXPECT_FALSE(result.has_value());
}

TEST(MeshQuality, EquilateralAngles60)
{
    auto mesh = MakeEquilateralTriangle();
    auto result = Geometry::MeshQuality::ComputeQuality(mesh);
    ASSERT_TRUE(result.has_value());

    // All angles of an equilateral triangle should be 60°
    EXPECT_NEAR(result->MinAngle, 60.0, 0.1);
    EXPECT_NEAR(result->MaxAngle, 60.0, 0.1);
    EXPECT_NEAR(result->MeanAngle, 60.0, 0.1);
}

TEST(MeshQuality, EquilateralAspectRatio1)
{
    auto mesh = MakeEquilateralTriangle();
    auto result = Geometry::MeshQuality::ComputeQuality(mesh);
    ASSERT_TRUE(result.has_value());

    // Equilateral triangle has aspect ratio of 1.0
    EXPECT_NEAR(result->MinAspectRatio, 1.0, 0.01);
    EXPECT_NEAR(result->MaxAspectRatio, 1.0, 0.01);
}

TEST(MeshQuality, TetrahedronClosed)
{
    auto mesh = MakeTetrahedron();
    auto result = Geometry::MeshQuality::ComputeQuality(mesh);
    ASSERT_TRUE(result.has_value());

    EXPECT_TRUE(result->IsClosed);
    EXPECT_EQ(result->BoundaryLoopCount, 0u);
}

TEST(MeshQuality, TetrahedronVolume)
{
    auto mesh = MakeTetrahedron();
    auto result = Geometry::MeshQuality::ComputeQuality(mesh);
    ASSERT_TRUE(result.has_value());

    // Regular tetrahedron with vertices at (±1,±1,±1):
    // Edge length = 2√2, Volume = (edge³)/(6√2) = (2√2)³ / (6√2) = 16√2 / (6√2) = 8/3
    double expectedVolume = 8.0 / 3.0;
    EXPECT_NEAR(std::abs(result->Volume), expectedVolume, 0.01);
}

TEST(MeshQuality, SingleTriangleNotClosed)
{
    auto mesh = MakeEquilateralTriangle();
    auto result = Geometry::MeshQuality::ComputeQuality(mesh);
    ASSERT_TRUE(result.has_value());

    EXPECT_FALSE(result->IsClosed);
    EXPECT_EQ(result->BoundaryLoopCount, 1u);
}

TEST(MeshQuality, EulerCharacteristic)
{
    // Tetrahedron: V=4, E=6, F=4, χ=2
    {
        auto mesh = MakeTetrahedron();
        auto result = Geometry::MeshQuality::ComputeQuality(mesh);
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result->EulerCharacteristic, 2);
        EXPECT_EQ(result->VertexCount, 4u);
        EXPECT_EQ(result->EdgeCount, 6u);
        EXPECT_EQ(result->FaceCount, 4u);
    }

    // Subdivided triangle (open surface): V=6, E=9, F=4, χ=1
    {
        auto mesh = MakeSubdividedTriangle();
        auto result = Geometry::MeshQuality::ComputeQuality(mesh);
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result->EulerCharacteristic, 1);
    }
}

TEST(MeshQuality, IcosahedronUniformValence5)
{
    auto mesh = MakeIcosahedron();
    auto result = Geometry::MeshQuality::ComputeQuality(mesh);
    ASSERT_TRUE(result.has_value());

    // All icosahedron vertices have valence 5
    EXPECT_EQ(result->MinValence, 5u);
    EXPECT_EQ(result->MaxValence, 5u);
    EXPECT_NEAR(result->MeanValence, 5.0, 0.01);
}

TEST(MeshQuality, EdgeLengthStats)
{
    auto mesh = MakeEquilateralTriangle();
    auto result = Geometry::MeshQuality::ComputeQuality(mesh);
    ASSERT_TRUE(result.has_value());

    // All edges of the equilateral triangle have length 1.0
    EXPECT_NEAR(result->MinEdgeLength, 1.0, 0.01);
    EXPECT_NEAR(result->MaxEdgeLength, 1.0, 0.01);
    EXPECT_NEAR(result->MeanEdgeLength, 1.0, 0.01);
    EXPECT_NEAR(result->StdDevEdgeLength, 0.0, 0.01);
}

TEST(MeshQuality, BoundaryLoopCount)
{
    // Closed mesh: 0 loops
    {
        auto mesh = MakeTetrahedron();
        auto result = Geometry::MeshQuality::ComputeQuality(mesh);
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result->BoundaryLoopCount, 0u);
    }

    // Open mesh (disk): 1 loop
    {
        auto mesh = MakeSubdividedTriangle();
        auto result = Geometry::MeshQuality::ComputeQuality(mesh);
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result->BoundaryLoopCount, 1u);
    }
}
