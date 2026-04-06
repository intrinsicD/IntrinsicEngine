// tests/Test_RuntimeGeometry_SDF.cpp
#include <gtest/gtest.h>
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>
#include <random>

import Core;
import Geometry;

using namespace Geometry;
using namespace Geometry::Validation;

// --- Helper for approximate vector equality ---
inline void ExpectVec3Near(const glm::vec3& a, const glm::vec3& b, float tolerance = 0.01f)
{
    EXPECT_NEAR(a.x, b.x, tolerance) << "Expected " << glm::to_string(b) << ", got " << glm::to_string(a);
    EXPECT_NEAR(a.y, b.y, tolerance);
    EXPECT_NEAR(a.z, b.z, tolerance);
}

TEST(GeometryProperties, NoRTTI_System)
{
    PropertySet vertices;
    vertices.Resize(3); // Triangle

    // Add Dynamic Property
    auto colorProp = vertices.Add<glm::vec3>("Color", {1,1,1});
    auto weightProp = vertices.Add<float>("Weight", 0.0f);

    EXPECT_TRUE(colorProp.IsValid());
    EXPECT_TRUE(weightProp.IsValid());

    // Modify Data
    colorProp[0] = glm::vec3{1.0f, 0.0f, 0.0f};
    weightProp[1] = 0.5f;

    // Retrieve by Name
    auto fetchedProp = vertices.Get<glm::vec3>("Color");
    EXPECT_TRUE(fetchedProp.IsValid());
    EXPECT_EQ(fetchedProp[0].x, 1.0f);

    // Type Safety Check (Try to get float as vec3)
    auto invalidProp = vertices.Get<glm::vec3>("Weight");
    EXPECT_FALSE(invalidProp.IsValid());
}

TEST(SDF_Solver, Sphere_Vs_Sphere)
{
    Sphere s1{{0,0,0}, 1.0f};
    Sphere s2{{1.5f,0,0}, 1.0f}; // Overlap by 0.5

    auto sdf1 = SDF::CreateSDF(s1);
    auto sdf2 = SDF::CreateSDF(s2);

    // Initial guess: Midpoint
    glm::vec3 guess = (s1.Center + s2.Center) * 0.5f;

    auto result = SDF::Contact_General_SDF(sdf1, sdf2, guess);

    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(result->PenetrationDepth, 0.5f, 0.05f);
    ExpectVec3Near(result->Normal, {1, 0, 0});
}

TEST(SDF_Solver, OBB_Vs_Sphere_Deep)
{
    // A rotated box
    OBB box;
    box.Center = {0, 0, 0};
    box.Extents = {1, 1, 1};
    box.Rotation = glm::angleAxis(glm::radians(45.0f), glm::vec3(0, 0, 1)); // 45 deg Z

    // Sphere penetrating the corner
    // Unrotated box corner is at roughly (1.414, 0, 0)
    Sphere s{{1.0f, 0, 0}, 0.5f};

    auto sdfBox = SDF::CreateSDF(box);
    auto sdfSphere = SDF::CreateSDF(s);

    glm::vec3 guess = (box.Center + s.Center) * 0.5f;
    auto result = SDF::Contact_General_SDF(sdfBox, sdfSphere, guess);

    ASSERT_TRUE(result.has_value());

    // Normal A->B (Box -> Sphere).
    // Box center (0,0,0). Sphere center (1,0,0).
    // Normal should point roughly +X.
    EXPECT_GT(result->Normal.x, 0.5f);
}

TEST(SDF_Solver, Capsule_Vs_Box)
{
    // Vertical Capsule at origin
    Capsule cap{{0, -1, 0}, {0, 1, 0}, 0.5f};

    // Box hitting it from the side
    OBB box;
    box.Center = {0.8f, 0, 0};
    box.Extents = {0.5f, 0.5f, 0.5f};
    box.Rotation = glm::quat(1,0,0,0);

    // Gap check:
    // Capsule surface at x=0.5.
    // Box surface at 0.8 - 0.5 = 0.3.
    // Overlap = 0.5 - 0.3 = 0.2.

    auto sdfCap = SDF::CreateSDF(cap);
    auto sdfBox = SDF::CreateSDF(box);

    auto result = SDF::Contact_General_SDF(sdfCap, sdfBox, {0.4f, 0, 0});

    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(result->PenetrationDepth, 0.2f, 0.05f);
    // Normal should be along X axis
    EXPECT_NEAR(std::abs(result->Normal.x), 1.0f, 0.01f);
}

TEST(SDF_Solver, No_Overlap)
{
    Sphere s1{{0,0,0}, 1.0f};
    Sphere s2{{3.0f,0,0}, 1.0f};

    auto sdf1 = SDF::CreateSDF(s1);
    auto sdf2 = SDF::CreateSDF(s2);

    auto result = SDF::Contact_General_SDF(sdf1, sdf2, {1.5f, 0, 0});
    EXPECT_FALSE(result.has_value());
}

TEST(SDF_Solver, Sphere_Vs_Triangle)
{
    // Triangle on the floor
    Triangle t{
            {-2, 0, -2},
            { 2, 0, -2},
            { 0, 0,  2}
    };

    // Sphere falling onto it
    Sphere s{{0, 0.5f, 0}, 1.0f};

    auto sdfTri = SDF::CreateSDF(t);
    auto sdfSphere = SDF::CreateSDF(s);

    auto result = SDF::Contact_General_SDF(sdfTri, sdfSphere, {0, 0.2f, 0});

    ASSERT_TRUE(result.has_value());
    // Sphere Radius 1.0. Center Y=0.5. Floor Y=0.
    // Penetration = 0.5.
    EXPECT_NEAR(result->PenetrationDepth, 0.5f, 0.05f);
    // Normal should be Up (0, 1, 0)
    // Note: Sign depends on A-B vs B-A convention.
    EXPECT_NEAR(std::abs(result->Normal.y), 1.0f, 0.05f);
}


// -----------------------------------------------------------------------------
// Halfedge Mesh Tests
// -----------------------------------------------------------------------------

TEST(HalfedgeMesh, SingleTriangle_Connectivity)
{
    using Geometry::Halfedge::Mesh;

    Mesh m;
    auto v0 = m.AddVertex();
    auto v1 = m.AddVertex();
    auto v2 = m.AddVertex();

    auto f = m.AddTriangle(v0, v1, v2);
    ASSERT_TRUE(f.has_value());

    EXPECT_EQ(m.VerticesSize(), 3u);
    EXPECT_EQ(m.FacesSize(), 1u);
    EXPECT_EQ(m.EdgesSize(), 3u);
    EXPECT_EQ(m.HalfedgesSize(), 6u);

    const HalfedgeHandle h0 = m.Halfedge(*f);
    ASSERT_TRUE(h0.IsValid());
    const HalfedgeHandle h1 = m.NextHalfedge(h0);
    const HalfedgeHandle h2 = m.NextHalfedge(h1);

    EXPECT_EQ(m.NextHalfedge(h2), h0);
    EXPECT_EQ(m.PrevHalfedge(h0), h2);

    // Face halfedges are interior.
    EXPECT_FALSE(m.IsBoundary(h0));
    EXPECT_FALSE(m.IsBoundary(h1));
    EXPECT_FALSE(m.IsBoundary(h2));

    // Their opposites are boundary.
    EXPECT_TRUE(m.IsBoundary(m.OppositeHalfedge(h0)));
    EXPECT_TRUE(m.IsBoundary(m.OppositeHalfedge(h1)));
    EXPECT_TRUE(m.IsBoundary(m.OppositeHalfedge(h2)));
}

TEST(HalfedgeMesh, TwoTriangles_SharedEdge)
{
    using Geometry::Halfedge::Mesh;

    Mesh m;
    for (int i = 0; i < 4; ++i) (void)m.AddVertex();

    ASSERT_TRUE(m.AddTriangle(VertexHandle{0}, VertexHandle{1}, VertexHandle{2}).has_value());
    ASSERT_TRUE(m.AddTriangle(VertexHandle{0}, VertexHandle{2}, VertexHandle{3}).has_value());

    EXPECT_EQ(m.VerticesSize(), 4u);
    EXPECT_EQ(m.FacesSize(), 2u);
    EXPECT_EQ(m.EdgesSize(), 5u);
    EXPECT_EQ(m.HalfedgesSize(), 10u);

    // Find halfedge 0->2 and check it is not boundary and opposite is also not boundary.
    auto h02 = m.FindHalfedge(VertexHandle{0}, VertexHandle{2});
    ASSERT_TRUE(h02.has_value());
    EXPECT_FALSE(m.IsBoundary(*h02));
    EXPECT_FALSE(m.IsBoundary(m.OppositeHalfedge(*h02)));
}

TEST(HalfedgeMesh, AddFace_RejectsNonBoundaryInsertion)
{
    using Geometry::Halfedge::Mesh;

    // Build a closed triangle, then attempt to add another face reusing a directed halfedge that already has a face.
    Mesh m;
    auto v0 = m.AddVertex();
    auto v1 = m.AddVertex();
    auto v2 = m.AddVertex();
    auto v3 = m.AddVertex();

    ASSERT_TRUE(m.AddTriangle(v0, v1, v2).has_value());

    // This tries to use the existing halfedge v0->v1 as part of a new face; should fail.
    const std::array<VertexHandle, 3> verts = {v0, v1, v3};
    auto f = m.AddFace(verts);
    EXPECT_FALSE(f.has_value());
}

TEST(HalfedgeMesh, DeleteFace_ThenGarbageCollect)
{
    using Geometry::Halfedge::Mesh;

    Mesh m;
    for (int i = 0; i < 4; ++i) (void)m.AddVertex();

    auto f0 = m.AddTriangle(VertexHandle{0}, VertexHandle{1}, VertexHandle{2});
    auto f1 = m.AddTriangle(VertexHandle{0}, VertexHandle{2}, VertexHandle{3});
    ASSERT_TRUE(f0.has_value());
    ASSERT_TRUE(f1.has_value());

    EXPECT_FALSE(m.HasGarbage());

    m.DeleteFace(*f0);
    EXPECT_TRUE(m.HasGarbage());

    m.GarbageCollection();
    EXPECT_FALSE(m.HasGarbage());

    // We should now have exactly one face left.
    EXPECT_EQ(m.FaceCount(), 1u);
}

TEST(GeometryPrimitives, AABB_CommonUtils)
{
    const AABB box{{-1.0f, -2.0f, -3.0f}, {3.0f, 2.0f, 1.0f}};

    const glm::vec3 closest = ClosestPoint(box, {5.0f, 0.5f, -4.0f});
    ExpectVec3Near(closest, {3.0f, 0.5f, -3.0f}, 1e-5f);
    EXPECT_NEAR(Distance(box, glm::vec3{5.0f, 0.5f, -4.0f}), std::sqrt(5.0), 1e-5);
    EXPECT_NEAR(SquaredDistance(box, glm::vec3{5.0f, 0.5f, -4.0f}), 5.0, 1e-5);
    EXPECT_NEAR(SignedDistance(box, glm::vec3{0.0f, 0.0f, 0.0f}), -1.0, 1e-5);

    const auto corners = box.GetCorners();
    EXPECT_EQ(corners.size(), 8u);
    ExpectVec3Near(corners[0], {-1.0f, -2.0f, -3.0f}, 1e-5f);
    ExpectVec3Near(corners[6], {3.0f, 2.0f, 1.0f}, 1e-5f);
}

TEST(GeometryPrimitives, OBB_CommonUtils)
{
    OBB box;
    box.Center = {2.0f, 0.0f, 0.0f};
    box.Extents = {1.0f, 2.0f, 0.5f};
    box.Rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3{0.0f, 0.0f, 1.0f});

    const glm::vec3 closest = ClosestPoint(box, {4.5f, 0.0f, 0.25f});
    ExpectVec3Near(closest, {4.0f, 0.0f, 0.25f}, 1e-4f);
    EXPECT_NEAR(Distance(box, glm::vec3{4.5f, 0.0f, 0.25f}), 0.5, 1e-5);
    EXPECT_NEAR(SignedDistance(box, glm::vec3{2.0f, 0.0f, 0.0f}), -0.5, 1e-5);

    const auto axes = box.GetAxes();
    ExpectVec3Near(axes[0], {0.0f, 1.0f, 0.0f}, 1e-5f);
    ExpectVec3Near(axes[1], {-1.0f, 0.0f, 0.0f}, 1e-5f);

    const AABB aabb = ToAABB(box);
    ExpectVec3Near(aabb.Min, {0.0f, -1.0f, -0.5f}, 1e-4f);
    ExpectVec3Near(aabb.Max, {4.0f, 1.0f, 0.5f}, 1e-4f);
}

TEST(GeometryPrimitives, Plane_CommonUtils)
{
    Plane plane{{0.0f, 2.0f, 0.0f}, -4.0f};
    plane.Normalize();

    EXPECT_NEAR(SignedDistance(plane, {1.0f, 5.0f, -2.0f}), 3.0, 1e-5);
    EXPECT_NEAR(Distance(plane, {1.0f, -1.0f, -2.0f}), 3.0, 1e-5);
    ExpectVec3Near(ClosestPoint(plane, {1.0f, 5.0f, -2.0f}), {1.0f, 2.0f, -2.0f}, 1e-5f);
}

TEST(GeometryPrimitives, Segment_And_Triangle_ClosestPoint)
{
    const Segment segment{{0.0f, 0.0f, 0.0f}, {4.0f, 0.0f, 0.0f}};
    EXPECT_NEAR(ClosestPointParameter(segment, {3.0f, 2.0f, 0.0f}), 0.75f, 1e-6f);
    ExpectVec3Near(ClosestPoint(segment, {3.0f, 2.0f, 0.0f}), {3.0f, 0.0f, 0.0f}, 1e-5f);
    EXPECT_NEAR(Distance(segment, {3.0f, 2.0f, 0.0f}), 2.0, 1e-5);

    const Triangle triangle{{0.0f, 0.0f, 0.0f}, {2.0f, 0.0f, 0.0f}, {0.0f, 2.0f, 0.0f}};
    ExpectVec3Near(ClosestPoint(triangle, {0.5f, 0.5f, 3.0f}), {0.5f, 0.5f, 0.0f}, 1e-5f);
    EXPECT_NEAR(Distance(triangle, {0.5f, 0.5f, 3.0f}), 3.0, 1e-5);
}

// ---------------------------------------------------------------------------
// EPA Scratch Arena (merged from Test_RuntimeGeometry_NoHeapEPA.cpp)
// ---------------------------------------------------------------------------

TEST(Physics_Contact, EPA_UsesScratchArena_Smoke)
{
    // Use small tetrahedra rather than cube corner sets.
    // The current GJK implementation only reliably returns a tetrahedron simplex
    // when the configuration is clearly 3D and non-degenerate.
    Geometry::ConvexHull a;
    Geometry::ConvexHull b;

    a.Vertices = {
        {0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
    };

    b.Vertices = {
        {0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
    };

    // Shift B so it overlaps A with a clear interior intersection.
    for (glm::vec3& v : b.Vertices) v += glm::vec3(0.15f, 0.15f, 0.15f);

    Core::Memory::LinearArena scratch(256 * 1024);
    scratch.Reset();

    const std::size_t beforeUsed = scratch.GetUsed();

    auto contact = Geometry::ComputeContact(a, b, scratch);

    ASSERT_TRUE(contact.has_value())
        << "GJK/EPA fallback did not report contact. If this flakes, it indicates a GJK degeneracy case.";

    EXPECT_GT(scratch.GetUsed(), beforeUsed) << "Expected EPA to consume scratch arena memory";
}

