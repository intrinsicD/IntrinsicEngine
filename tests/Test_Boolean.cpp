#include <gtest/gtest.h>

#include <glm/glm.hpp>

import Geometry;

// -------------------------------------------------------------------------
// Helper: build an axis-aligned quad-faced cube mesh.
// -------------------------------------------------------------------------
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

// =========================================================================
// Disjoint volumes — Union
// =========================================================================

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

// =========================================================================
// Disjoint volumes — Intersection (empty result)
// =========================================================================

TEST(BooleanCSG, DisjointIntersectionReturnsEmptyMesh)
{
    const auto a = MakeCube(1.0f, {-5.0f, 0.0f, 0.0f});
    const auto b = MakeCube(1.0f, { 5.0f, 0.0f, 0.0f});

    Geometry::Halfedge::Mesh out;
    const auto result = Geometry::Boolean::Compute(a, b, Geometry::Boolean::Operation::Intersection, out);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->ExactResult);
    EXPECT_EQ(out.FaceCount(), 0u);
    EXPECT_EQ(out.VertexCount(), 0u);
}

// =========================================================================
// Disjoint volumes — Difference (returns A)
// =========================================================================

TEST(BooleanCSG, DisjointDifferenceReturnsFirstMesh)
{
    const auto a = MakeCube(1.0f, {-5.0f, 0.0f, 0.0f});
    const auto b = MakeCube(1.0f, { 5.0f, 0.0f, 0.0f});

    Geometry::Halfedge::Mesh out;
    const auto result = Geometry::Boolean::Compute(a, b, Geometry::Boolean::Operation::Difference, out);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->ExactResult);
    EXPECT_EQ(out.FaceCount(), a.FaceCount());
    EXPECT_EQ(out.VertexCount(), a.VertexCount());
}

// =========================================================================
// Disjoint volumes — all three operations in one test
// =========================================================================

TEST(BooleanCSG, DisjointVolumes_AllOperations)
{
    const auto a = MakeCube(1.0f, {-5.0f, 0.0f, 0.0f});
    const auto b = MakeCube(1.0f, { 5.0f, 0.0f, 0.0f});

    {
        Geometry::Halfedge::Mesh out;
        const auto r = Geometry::Boolean::Compute(a, b, Geometry::Boolean::Operation::Union, out);
        ASSERT_TRUE(r.has_value());
        EXPECT_EQ(out.FaceCount(), a.FaceCount() + b.FaceCount());
    }
    {
        Geometry::Halfedge::Mesh out;
        const auto r = Geometry::Boolean::Compute(a, b, Geometry::Boolean::Operation::Intersection, out);
        ASSERT_TRUE(r.has_value());
        EXPECT_EQ(out.FaceCount(), 0u);
    }
    {
        Geometry::Halfedge::Mesh out;
        const auto r = Geometry::Boolean::Compute(a, b, Geometry::Boolean::Operation::Difference, out);
        ASSERT_TRUE(r.has_value());
        EXPECT_EQ(out.FaceCount(), a.FaceCount());
    }
}

// =========================================================================
// Full containment — Intersection returns inner
// =========================================================================

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

// =========================================================================
// Full containment — Union returns outer
// =========================================================================

TEST(BooleanCSG, ContainmentUnionReturnsOuterMesh)
{
    const auto outer = MakeCube(2.0f, {0.0f, 0.0f, 0.0f});
    const auto inner = MakeCube(0.5f, {0.0f, 0.0f, 0.0f});

    Geometry::Halfedge::Mesh out;
    const auto result = Geometry::Boolean::Compute(outer, inner, Geometry::Boolean::Operation::Union, out);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->ExactResult);
    EXPECT_EQ(out.FaceCount(), outer.FaceCount());
}

// =========================================================================
// Full containment — Difference: A inside B → empty result
// =========================================================================

TEST(BooleanCSG, ContainmentDifference_AInsideB_ReturnsEmpty)
{
    const auto outer = MakeCube(2.0f, {0.0f, 0.0f, 0.0f});
    const auto inner = MakeCube(0.5f, {0.0f, 0.0f, 0.0f});

    Geometry::Halfedge::Mesh out;
    const auto result = Geometry::Boolean::Compute(inner, outer, Geometry::Boolean::Operation::Difference, out);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->ExactResult);
    EXPECT_EQ(out.FaceCount(), 0u);
    EXPECT_EQ(out.VertexCount(), 0u);
}

// =========================================================================
// Full containment — Difference: B inside A → nullopt (shell subtraction)
// =========================================================================

TEST(BooleanCSG, ContainmentDifference_BInsideA_ReturnsNullopt)
{
    const auto outer = MakeCube(2.0f, {0.0f, 0.0f, 0.0f});
    const auto inner = MakeCube(0.5f, {0.0f, 0.0f, 0.0f});

    Geometry::Halfedge::Mesh out;
    const auto result = Geometry::Boolean::Compute(outer, inner, Geometry::Boolean::Operation::Difference, out);
    EXPECT_FALSE(result.has_value());
}

// =========================================================================
// Full containment — reversed operand order
// =========================================================================

TEST(BooleanCSG, ContainmentIntersection_Reversed)
{
    const auto outer = MakeCube(2.0f, {0.0f, 0.0f, 0.0f});
    const auto inner = MakeCube(0.5f, {0.0f, 0.0f, 0.0f});

    Geometry::Halfedge::Mesh out;
    const auto result = Geometry::Boolean::Compute(inner, outer, Geometry::Boolean::Operation::Intersection, out);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->ExactResult);
    EXPECT_EQ(out.FaceCount(), inner.FaceCount());
}

TEST(BooleanCSG, ContainmentUnion_Reversed)
{
    const auto outer = MakeCube(2.0f, {0.0f, 0.0f, 0.0f});
    const auto inner = MakeCube(0.5f, {0.0f, 0.0f, 0.0f});

    Geometry::Halfedge::Mesh out;
    const auto result = Geometry::Boolean::Compute(inner, outer, Geometry::Boolean::Operation::Union, out);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->ExactResult);
    EXPECT_EQ(out.FaceCount(), outer.FaceCount());
}

// =========================================================================
// Partial overlap — returns nullopt (not yet implemented)
// =========================================================================

TEST(BooleanCSG, PartialOverlapReturnsNullopt)
{
    const auto a = MakeCube(1.0f, {-0.3f, 0.0f, 0.0f});
    const auto b = MakeCube(1.0f, { 0.3f, 0.0f, 0.0f});

    Geometry::Halfedge::Mesh out;
    const auto result = Geometry::Boolean::Compute(a, b, Geometry::Boolean::Operation::Union, out);
    EXPECT_FALSE(result.has_value());
}

TEST(BooleanCSG, PartialOverlapDifferenceReturnsNullopt)
{
    const auto a = MakeCube(1.0f, {-0.3f, 0.0f, 0.0f});
    const auto b = MakeCube(1.0f, { 0.3f, 0.0f, 0.0f});

    Geometry::Halfedge::Mesh out;
    const auto result = Geometry::Boolean::Compute(a, b, Geometry::Boolean::Operation::Difference, out);
    EXPECT_FALSE(result.has_value());
}

TEST(BooleanCSG, PartialOverlapIntersectionReturnsNullopt)
{
    const auto a = MakeCube(1.0f, {-0.3f, 0.0f, 0.0f});
    const auto b = MakeCube(1.0f, { 0.3f, 0.0f, 0.0f});

    Geometry::Halfedge::Mesh out;
    const auto result = Geometry::Boolean::Compute(a, b, Geometry::Boolean::Operation::Intersection, out);
    EXPECT_FALSE(result.has_value());
}

// =========================================================================
// Degenerate inputs — empty meshes
// =========================================================================

TEST(BooleanCSG, EmptyMeshA_ReturnsNullopt)
{
    Geometry::Halfedge::Mesh empty;
    const auto b = MakeCube(1.0f, {0.0f, 0.0f, 0.0f});

    Geometry::Halfedge::Mesh out;
    const auto result = Geometry::Boolean::Compute(empty, b, Geometry::Boolean::Operation::Union, out);
    EXPECT_FALSE(result.has_value());
}

TEST(BooleanCSG, EmptyMeshB_ReturnsNullopt)
{
    const auto a = MakeCube(1.0f, {0.0f, 0.0f, 0.0f});
    Geometry::Halfedge::Mesh empty;

    Geometry::Halfedge::Mesh out;
    const auto result = Geometry::Boolean::Compute(a, empty, Geometry::Boolean::Operation::Union, out);
    EXPECT_FALSE(result.has_value());
}

TEST(BooleanCSG, BothMeshesEmpty_ReturnsNullopt)
{
    Geometry::Halfedge::Mesh a;
    Geometry::Halfedge::Mesh b;

    Geometry::Halfedge::Mesh out;
    const auto result = Geometry::Boolean::Compute(a, b, Geometry::Boolean::Operation::Intersection, out);
    EXPECT_FALSE(result.has_value());
}

// =========================================================================
// Degenerate inputs — coincident meshes (same position, same size)
// =========================================================================

TEST(BooleanCSG, IdenticalMeshes_UnionReturnsOneMesh)
{
    const auto a = MakeCube(1.0f, {0.0f, 0.0f, 0.0f});
    const auto b = MakeCube(1.0f, {0.0f, 0.0f, 0.0f});

    Geometry::Halfedge::Mesh out;
    const auto result = Geometry::Boolean::Compute(a, b, Geometry::Boolean::Operation::Union, out);
    // Both are inside each other — containment should apply.
    if (result.has_value())
    {
        EXPECT_TRUE(result->ExactResult);
        EXPECT_EQ(out.FaceCount(), a.FaceCount());
    }
}

TEST(BooleanCSG, IdenticalMeshes_IntersectionReturnsOneMesh)
{
    const auto a = MakeCube(1.0f, {0.0f, 0.0f, 0.0f});
    const auto b = MakeCube(1.0f, {0.0f, 0.0f, 0.0f});

    Geometry::Halfedge::Mesh out;
    const auto result = Geometry::Boolean::Compute(a, b, Geometry::Boolean::Operation::Intersection, out);
    if (result.has_value())
    {
        EXPECT_TRUE(result->ExactResult);
        EXPECT_EQ(out.FaceCount(), a.FaceCount());
    }
}

// =========================================================================
// Edge-contact tests (touching boundaries, no volume overlap)
// =========================================================================

TEST(BooleanCSG, FaceContactUnion)
{
    // Two cubes sharing a face plane at x=1. AABBs overlap at a single
    // plane but no vertex of one is truly inside the other.
    const auto a = MakeCube(1.0f, { 0.0f, 0.0f, 0.0f});
    const auto b = MakeCube(1.0f, { 2.0f, 0.0f, 0.0f});

    Geometry::Halfedge::Mesh out;
    const auto result = Geometry::Boolean::Compute(a, b, Geometry::Boolean::Operation::Union, out);
    // Should produce a valid disjoint-style union or return nullopt.
    if (result.has_value())
    {
        EXPECT_TRUE(result->ExactResult);
        EXPECT_GE(out.FaceCount(), a.FaceCount());
    }
}

TEST(BooleanCSG, EdgeContactUnion)
{
    // Two cubes touching at a single edge along z-axis.
    const auto a = MakeCube(1.0f, { 0.0f, 0.0f, 0.0f});
    const auto b = MakeCube(1.0f, { 2.0f, 2.0f, 0.0f});

    Geometry::Halfedge::Mesh out;
    const auto result = Geometry::Boolean::Compute(a, b, Geometry::Boolean::Operation::Union, out);
    if (result.has_value())
    {
        EXPECT_TRUE(result->ExactResult);
        EXPECT_EQ(out.FaceCount(), a.FaceCount() + b.FaceCount());
    }
}

TEST(BooleanCSG, VertexContactUnion)
{
    // Two cubes touching at a single vertex at (1,1,1).
    const auto a = MakeCube(1.0f, { 0.0f, 0.0f, 0.0f});
    const auto b = MakeCube(1.0f, { 2.0f, 2.0f, 2.0f});

    Geometry::Halfedge::Mesh out;
    const auto result = Geometry::Boolean::Compute(a, b, Geometry::Boolean::Operation::Union, out);
    if (result.has_value())
    {
        EXPECT_TRUE(result->ExactResult);
        EXPECT_EQ(out.FaceCount(), a.FaceCount() + b.FaceCount());
    }
}

// =========================================================================
// Result diagnostics
// =========================================================================

TEST(BooleanCSG, DisjointResult_HasDiagnostic)
{
    const auto a = MakeCube(1.0f, {-5.0f, 0.0f, 0.0f});
    const auto b = MakeCube(1.0f, { 5.0f, 0.0f, 0.0f});

    Geometry::Halfedge::Mesh out;
    const auto result = Geometry::Boolean::Compute(a, b, Geometry::Boolean::Operation::Union, out);
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->VolumesOverlap);
    EXPECT_STRNE(result->Diagnostic, "");
}

TEST(BooleanCSG, ContainmentResult_HasDiagnostic)
{
    const auto outer = MakeCube(2.0f, {0.0f, 0.0f, 0.0f});
    const auto inner = MakeCube(0.5f, {0.0f, 0.0f, 0.0f});

    Geometry::Halfedge::Mesh out;
    const auto result = Geometry::Boolean::Compute(outer, inner, Geometry::Boolean::Operation::Intersection, out);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->VolumesOverlap);
    EXPECT_STRNE(result->Diagnostic, "");
}

// =========================================================================
// Custom epsilon
// =========================================================================

TEST(BooleanCSG, CustomEpsilon_DisjointStillWorks)
{
    const auto a = MakeCube(1.0f, {-5.0f, 0.0f, 0.0f});
    const auto b = MakeCube(1.0f, { 5.0f, 0.0f, 0.0f});

    Geometry::Boolean::BooleanParams params;
    params.Epsilon = 1e-3f;

    Geometry::Halfedge::Mesh out;
    const auto result = Geometry::Boolean::Compute(a, b, Geometry::Boolean::Operation::Union, out, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->ExactResult);
}

// =========================================================================
// Output mesh is cleared before populating
// =========================================================================

TEST(BooleanCSG, OutputMeshIsClearedBeforeWrite)
{
    const auto a = MakeCube(1.0f, {-5.0f, 0.0f, 0.0f});
    const auto b = MakeCube(1.0f, { 5.0f, 0.0f, 0.0f});

    // Pre-populate the output mesh with existing data.
    Geometry::Halfedge::Mesh out = MakeCube(3.0f, {0.0f, 0.0f, 0.0f});
    ASSERT_GT(out.FaceCount(), 0u);

    const auto result = Geometry::Boolean::Compute(a, b, Geometry::Boolean::Operation::Union, out);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(out.FaceCount(), a.FaceCount() + b.FaceCount());
}
