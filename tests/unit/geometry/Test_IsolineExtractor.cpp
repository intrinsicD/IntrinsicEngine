#include <gtest/gtest.h>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

import Graphics;
import Geometry;

using namespace Graphics;
using namespace Geometry;

// =============================================================================
// IsolineExtractor — unit tests for contour line extraction from scalar fields.
// =============================================================================

namespace
{
    // Build a single triangle: v0=(0,0,0), v1=(1,0,0), v2=(0,1,0)
    Halfedge::Mesh MakeSingleTriangleWithScalar(const std::string& propName,
                                                  float s0, float s1, float s2)
    {
        Halfedge::Mesh mesh;
        auto v0 = mesh.AddVertex({0.0f, 0.0f, 0.0f});
        auto v1 = mesh.AddVertex({1.0f, 0.0f, 0.0f});
        auto v2 = mesh.AddVertex({0.0f, 1.0f, 0.0f});
        (void)mesh.AddTriangle(v0, v1, v2);

        auto prop = mesh.VertexProperties().Add<float>(propName, 0.0f);
        auto& vec = prop.Vector();
        vec[v0.Index] = s0;
        vec[v1.Index] = s1;
        vec[v2.Index] = s2;

        return mesh;
    }
}

TEST(IsolineExtractor, ZeroCountReturnsEmpty)
{
    auto mesh = MakeSingleTriangleWithScalar("val", 0.0f, 1.0f, 2.0f);
    auto result = IsolineExtractor::Extract(mesh, "val", 0, 0.0f, 2.0f);
    EXPECT_TRUE(result.Points.empty());
}

TEST(IsolineExtractor, InvalidRangeReturnsEmpty)
{
    auto mesh = MakeSingleTriangleWithScalar("val", 0.0f, 1.0f, 2.0f);
    // rangeMin >= rangeMax
    auto result = IsolineExtractor::Extract(mesh, "val", 5, 2.0f, 0.0f);
    EXPECT_TRUE(result.Points.empty());
}

TEST(IsolineExtractor, NonexistentPropertyReturnsEmpty)
{
    auto mesh = MakeSingleTriangleWithScalar("val", 0.0f, 1.0f, 2.0f);
    auto result = IsolineExtractor::Extract(mesh, "nonexistent", 5, 0.0f, 2.0f);
    EXPECT_TRUE(result.Points.empty());
}

TEST(IsolineExtractor, SingleIsolineProducesOneSegment)
{
    // Triangle with scalars 0, 0, 2. One isoline at t=1.
    // Iso crosses edge v0-v2 and v1-v2.
    auto mesh = MakeSingleTriangleWithScalar("val", 0.0f, 0.0f, 2.0f);
    auto result = IsolineExtractor::Extract(mesh, "val", 1, 0.0f, 2.0f);

    // One isoline at iso = 2 * (1/(1+1)) = 1.0
    // Two crossings expected → one segment → 2 points.
    ASSERT_EQ(result.Points.size(), 2u);

    // Both crossing points should lie in z=0 plane (since the triangle is in z=0).
    EXPECT_FLOAT_EQ(result.Points[0].z, 0.0f);
    EXPECT_FLOAT_EQ(result.Points[1].z, 0.0f);
}

TEST(IsolineExtractor, MultipleIsolinesProduceMultipleSegments)
{
    // Triangle with scalars 0, 4, 8.
    auto mesh = MakeSingleTriangleWithScalar("val", 0.0f, 4.0f, 8.0f);
    // 3 isolines in range [0, 8] → values at 2, 4, 6.
    auto result = IsolineExtractor::Extract(mesh, "val", 3, 0.0f, 8.0f);

    // Each isoline should produce one segment (2 points).
    EXPECT_EQ(result.Points.size(), 6u); // 3 segments × 2 points
}

TEST(IsolineExtractor, IsoOutsideRangeProducesNoSegments)
{
    // Triangle with scalars all in [0, 1], isolines in [5, 10].
    auto mesh = MakeSingleTriangleWithScalar("val", 0.0f, 0.5f, 1.0f);
    auto result = IsolineExtractor::Extract(mesh, "val", 5, 5.0f, 10.0f);
    EXPECT_TRUE(result.Points.empty());
}

TEST(IsolineExtractor, TwoTriangleMesh)
{
    // Two-triangle square: v0=(0,0,0), v1=(1,0,0), v2=(1,1,0), v3=(0,1,0)
    // Triangles: (v0,v1,v2) and (v0,v2,v3)
    // Scalar: 0 at y=0, 1 at y=1 → linear ramp.
    Halfedge::Mesh mesh;
    auto v0 = mesh.AddVertex({0.0f, 0.0f, 0.0f});
    auto v1 = mesh.AddVertex({1.0f, 0.0f, 0.0f});
    auto v2 = mesh.AddVertex({1.0f, 1.0f, 0.0f});
    auto v3 = mesh.AddVertex({0.0f, 1.0f, 0.0f});
    (void)mesh.AddTriangle(v0, v1, v2);
    (void)mesh.AddTriangle(v0, v2, v3);

    auto prop = mesh.VertexProperties().Add<float>("height", 0.0f);
    auto& vec = prop.Vector();
    vec[v0.Index] = 0.0f;
    vec[v1.Index] = 0.0f;
    vec[v2.Index] = 1.0f;
    vec[v3.Index] = 1.0f;

    // One isoline at mid-height (0.5).
    auto result = IsolineExtractor::Extract(mesh, "height", 1, 0.0f, 1.0f);

    // Each triangle should produce one segment → 2 segments → 4 points.
    EXPECT_EQ(result.Points.size(), 4u);

    // All crossing points should have y ≈ 0.5 (midway in the y direction).
    for (const auto& p : result.Points)
    {
        EXPECT_NEAR(p.y, 0.5f, 0.01f);
    }
}
