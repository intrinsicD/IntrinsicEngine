#include <gtest/gtest.h>

#include <glm/glm.hpp>

import Geometry;

static Geometry::Halfedge::Mesh MakeCube(float halfExtent, glm::vec3 center)
{
    Geometry::Halfedge::Mesh mesh;
    const glm::vec3 c = center;
    const float h = halfExtent;

    auto v0 = mesh.AddVertex(c + glm::vec3{-h, -h, -h});
    auto v1 = mesh.AddVertex(c + glm::vec3{ h, -h, -h});
    auto v2 = mesh.AddVertex(c + glm::vec3{ h,  h, -h});
    auto v3 = mesh.AddVertex(c + glm::vec3{-h,  h, -h});
    auto v4 = mesh.AddVertex(c + glm::vec3{-h, -h,  h});
    auto v5 = mesh.AddVertex(c + glm::vec3{ h, -h,  h});
    auto v6 = mesh.AddVertex(c + glm::vec3{ h,  h,  h});
    auto v7 = mesh.AddVertex(c + glm::vec3{-h,  h,  h});

    (void)mesh.AddQuad(v3, v2, v1, v0);
    (void)mesh.AddQuad(v4, v5, v6, v7);
    (void)mesh.AddQuad(v0, v1, v5, v4);
    (void)mesh.AddQuad(v2, v3, v7, v6);
    (void)mesh.AddQuad(v0, v4, v7, v3);
    (void)mesh.AddQuad(v1, v2, v6, v5);
    return mesh;
}

TEST(BooleanCSG, DisjointUnionAppendsBothMeshes)
{
    const auto a = MakeCube(1.0f, {-5.0f, 0.0f, 0.0f});
    const auto b = MakeCube(1.0f, { 5.0f, 0.0f, 0.0f});

    Geometry::Halfedge::Mesh out;
    const auto result = Geometry::Boolean::Compute(a, b, Geometry::Boolean::Operation::Union, out);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->ExactResult);
    EXPECT_EQ(out.FaceCount(), a.FaceCount() + b.FaceCount());
}

TEST(BooleanCSG, ContainmentIntersectionReturnsInnerMesh)
{
    const auto outer = MakeCube(2.0f, {0.0f, 0.0f, 0.0f});
    const auto inner = MakeCube(0.5f, {0.0f, 0.0f, 0.0f});

    Geometry::Halfedge::Mesh out;
    const auto result = Geometry::Boolean::Compute(outer, inner, Geometry::Boolean::Operation::Intersection, out);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->ExactResult);
    EXPECT_EQ(out.FaceCount(), inner.FaceCount());
}

TEST(BooleanCSG, PartialOverlapReturnsNullopt)
{
    const auto a = MakeCube(1.0f, {-0.3f, 0.0f, 0.0f});
    const auto b = MakeCube(1.0f, { 0.3f, 0.0f, 0.0f});

    Geometry::Halfedge::Mesh out;
    const auto result = Geometry::Boolean::Compute(a, b, Geometry::Boolean::Operation::Union, out);
    EXPECT_FALSE(result.has_value());
}
