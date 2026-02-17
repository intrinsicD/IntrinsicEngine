#include <gtest/gtest.h>
#include <cmath>
#include <numbers>

#include <glm/glm.hpp>

import Geometry;

// =============================================================================
// Test mesh builders (same shapes as Test_MeshOperations.cpp)
// =============================================================================

static Geometry::Halfedge::Mesh MakeEquilateralTriangle()
{
    Geometry::Halfedge::Mesh mesh;
    auto v0 = mesh.AddVertex({0.0f, 0.0f, 0.0f});
    auto v1 = mesh.AddVertex({1.0f, 0.0f, 0.0f});
    auto v2 = mesh.AddVertex({0.5f, std::sqrt(3.0f) / 2.0f, 0.0f});
    (void)mesh.AddTriangle(v0, v1, v2);
    return mesh;
}

static Geometry::Halfedge::Mesh MakeTetrahedron()
{
    Geometry::Halfedge::Mesh mesh;
    auto v0 = mesh.AddVertex({1.0f, 1.0f, 1.0f});
    auto v1 = mesh.AddVertex({1.0f, -1.0f, -1.0f});
    auto v2 = mesh.AddVertex({-1.0f, 1.0f, -1.0f});
    auto v3 = mesh.AddVertex({-1.0f, -1.0f, 1.0f});

    (void)mesh.AddTriangle(v0, v1, v2);
    (void)mesh.AddTriangle(v0, v2, v3);
    (void)mesh.AddTriangle(v0, v3, v1);
    (void)mesh.AddTriangle(v1, v3, v2);

    return mesh;
}

static Geometry::Halfedge::Mesh MakeIcosahedron()
{
    Geometry::Halfedge::Mesh mesh;
    const float phi = (1.0f + std::sqrt(5.0f)) / 2.0f;
    const float scale = 1.0f / std::sqrt(1.0f + phi * phi);

    auto v0  = mesh.AddVertex(glm::vec3( 0,  1,  phi) * scale);
    auto v1  = mesh.AddVertex(glm::vec3( 0, -1,  phi) * scale);
    auto v2  = mesh.AddVertex(glm::vec3( 0,  1, -phi) * scale);
    auto v3  = mesh.AddVertex(glm::vec3( 0, -1, -phi) * scale);
    auto v4  = mesh.AddVertex(glm::vec3( 1,  phi,  0) * scale);
    auto v5  = mesh.AddVertex(glm::vec3(-1,  phi,  0) * scale);
    auto v6  = mesh.AddVertex(glm::vec3( 1, -phi,  0) * scale);
    auto v7  = mesh.AddVertex(glm::vec3(-1, -phi,  0) * scale);
    auto v8  = mesh.AddVertex(glm::vec3( phi,  0,  1) * scale);
    auto v9  = mesh.AddVertex(glm::vec3(-phi,  0,  1) * scale);
    auto v10 = mesh.AddVertex(glm::vec3( phi,  0, -1) * scale);
    auto v11 = mesh.AddVertex(glm::vec3(-phi,  0, -1) * scale);

    (void)mesh.AddTriangle(v0, v1, v8);
    (void)mesh.AddTriangle(v0, v8, v4);
    (void)mesh.AddTriangle(v0, v4, v5);
    (void)mesh.AddTriangle(v0, v5, v9);
    (void)mesh.AddTriangle(v0, v9, v1);
    (void)mesh.AddTriangle(v1, v6, v8);
    (void)mesh.AddTriangle(v1, v7, v6);
    (void)mesh.AddTriangle(v1, v9, v7);
    (void)mesh.AddTriangle(v2, v3, v11);
    (void)mesh.AddTriangle(v2, v10, v3);
    (void)mesh.AddTriangle(v2, v4, v10);
    (void)mesh.AddTriangle(v2, v5, v4);
    (void)mesh.AddTriangle(v2, v11, v5);
    (void)mesh.AddTriangle(v3, v6, v7);
    (void)mesh.AddTriangle(v3, v10, v6);
    (void)mesh.AddTriangle(v3, v7, v11);
    (void)mesh.AddTriangle(v4, v8, v10);
    (void)mesh.AddTriangle(v5, v11, v9);
    (void)mesh.AddTriangle(v6, v10, v8);
    (void)mesh.AddTriangle(v7, v9, v11);

    return mesh;
}

// Subdivided triangle: 6 vertices, 4 faces, has a boundary
static Geometry::Halfedge::Mesh MakeSubdividedTriangle()
{
    const float s = std::sqrt(3.0f);
    Geometry::Halfedge::Mesh mesh;
    auto v0 = mesh.AddVertex({0.0f, 0.0f, 0.0f});
    auto v1 = mesh.AddVertex({2.0f, 0.0f, 0.0f});
    auto v2 = mesh.AddVertex({1.0f, s, 0.0f});
    auto v3 = mesh.AddVertex({1.0f, 0.0f, 0.0f});
    auto v4 = mesh.AddVertex({1.5f, s / 2.0f, 0.0f});
    auto v5 = mesh.AddVertex({0.5f, s / 2.0f, 0.0f});

    (void)mesh.AddTriangle(v0, v3, v5);
    (void)mesh.AddTriangle(v3, v1, v4);
    (void)mesh.AddTriangle(v5, v4, v2);
    (void)mesh.AddTriangle(v3, v4, v5);

    return mesh;
}

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
