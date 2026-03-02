#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include <gtest/gtest.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

import Geometry;

// Mirrors the GPU compute culler rule in assets/shaders/instance_cull_multigeo.comp:
//   d = dot(plane.xyz, center) + plane.w
//   if (d < -radius) => culled
namespace
{
    [[nodiscard]] bool SphereVisibleGpuRule(const std::array<glm::vec4, 6>& planes, const glm::vec3& c, float r)
    {
        for (const glm::vec4& p : planes)
        {
            const float d = glm::dot(glm::vec3(p), c) + p.w;
            if (d < -r)
                return false;
        }
        return true;
    }
}

TEST(Culling, FrustumSphere_MatchesGeometryOverlap)
{
    // Camera at origin looking down -Z (right-handed), Vulkan depth 0..1.
    const glm::mat4 view = glm::mat4(1.0f);
    const glm::mat4 proj = glm::perspectiveRH_ZO(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 1000.0f);

    const glm::mat4 viewProj = proj * view;

    const Geometry::Frustum fr = Geometry::Frustum::CreateFromMatrix(viewProj);

    std::array<glm::vec4, 6> planes{};
    for (int i = 0; i < 6; ++i)
        planes[i] = glm::vec4(fr.Planes[i].Normal, fr.Planes[i].Distance);

    // Sphere in front of the camera.
    const Geometry::Sphere s0{.Center = {0.0f, 0.0f, -5.0f}, .Radius = 1.0f};

    EXPECT_TRUE(Geometry::TestOverlap(fr, s0));
    EXPECT_TRUE(SphereVisibleGpuRule(planes, s0.Center, s0.Radius));

    // Sphere behind the camera should be culled.
    const Geometry::Sphere s1{.Center = {0.0f, 0.0f, +5.0f}, .Radius = 1.0f};

    EXPECT_FALSE(Geometry::TestOverlap(fr, s1));
    EXPECT_FALSE(SphereVisibleGpuRule(planes, s1.Center, s1.Radius));
}

TEST(Culling, RoutingTable_SparseHandleToDense)
{
    // This mirrors the Stage3 routing contract:
    // - Instance.GeometryID stores sparse GeometryHandle.Index
    // - handleToDense[sparse] yields dense geometry id, or 0xFFFFFFFF to reject.

    constexpr uint32_t invalid = 0xFFFFFFFFu;

    // Suppose we have sparse handles 2 and 7 active this frame.
    std::vector<uint32_t> handleToDense;
    handleToDense.assign(8, invalid);
    handleToDense[2] = 0;
    handleToDense[7] = 1;

    // Valid.
    EXPECT_NE(handleToDense[2], invalid);
    EXPECT_NE(handleToDense[7], invalid);

    // Unmapped sparse index => invalid.
    EXPECT_EQ(handleToDense[0], invalid);
    EXPECT_EQ(handleToDense[3], invalid);
}

// =============================================================================
// CPU-side frustum culling for retained-mode render passes.
// =============================================================================
// These tests validate the FrustumCullSphere logic used by
// RetainedLineRenderPass and RetainedPointCloudRenderPass. The logic mirrors
// instance_cull.comp: transform local bounding sphere to world space, then
// test against 6 frustum planes.

namespace
{
    // Mirrors FrustumCullSphere from Graphics.PassUtils.hpp — pure CPU logic,
    // no module dependencies beyond Geometry (which is already imported).
    [[nodiscard]] bool FrustumCullSphere_CPU(const glm::mat4& worldMatrix,
                                              const glm::vec4& localBounds,
                                              const Geometry::Frustum& frustum)
    {
        const float localRadius = localBounds.w;
        if (localRadius <= 0.0f)
            return false;

        const glm::vec3 localCenter{localBounds.x, localBounds.y, localBounds.z};
        const glm::vec3 worldCenter = glm::vec3(worldMatrix * glm::vec4(localCenter, 1.0f));

        const float sx = glm::length(glm::vec3(worldMatrix[0]));
        const float sy = glm::length(glm::vec3(worldMatrix[1]));
        const float sz = glm::length(glm::vec3(worldMatrix[2]));
        const float worldRadius = localRadius * glm::max(sx, glm::max(sy, sz));

        return Geometry::TestOverlap(frustum, Geometry::Sphere{worldCenter, worldRadius});
    }

    // Compute AABB-based bounding sphere from positions — mirrors
    // ComputeBoundingSphereFromPositions in Graphics.Geometry.cpp.
    [[nodiscard]] glm::vec4 ComputeBoundingSphere(const std::vector<glm::vec3>& positions)
    {
        if (positions.empty())
            return {0.0f, 0.0f, 0.0f, 0.0f};

        glm::vec3 minP = positions[0];
        glm::vec3 maxP = positions[0];
        for (const auto& p : positions)
        {
            minP = glm::min(minP, p);
            maxP = glm::max(maxP, p);
        }

        const glm::vec3 center = (minP + maxP) * 0.5f;
        const float radius = glm::length(maxP - center);
        return {center.x, center.y, center.z, std::max(radius, 1e-3f)};
    }

    // Standard test frustum: camera at origin looking down -Z.
    [[nodiscard]] Geometry::Frustum MakeTestFrustum()
    {
        const glm::mat4 view = glm::mat4(1.0f);
        const glm::mat4 proj = glm::perspectiveRH_ZO(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 1000.0f);
        return Geometry::Frustum::CreateFromMatrix(proj * view);
    }
}

// ---- Bounding sphere computation from positions ----

TEST(Culling, BoundingSphere_EmptyPositions)
{
    const glm::vec4 sphere = ComputeBoundingSphere({});
    EXPECT_FLOAT_EQ(sphere.w, 0.0f); // Empty → zero radius.
}

TEST(Culling, BoundingSphere_SinglePoint)
{
    const glm::vec4 sphere = ComputeBoundingSphere({{3.0f, 4.0f, 5.0f}});
    EXPECT_FLOAT_EQ(sphere.x, 3.0f);
    EXPECT_FLOAT_EQ(sphere.y, 4.0f);
    EXPECT_FLOAT_EQ(sphere.z, 5.0f);
    EXPECT_GE(sphere.w, 1e-3f); // Clamped to epsilon.
}

TEST(Culling, BoundingSphere_UnitCubeVertices)
{
    // Unit cube: [-1,1]^3. AABB center = (0,0,0), half-diagonal = sqrt(3).
    std::vector<glm::vec3> verts = {
        {-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
        {-1, -1,  1}, {1, -1,  1}, {1, 1,  1}, {-1, 1,  1}
    };

    const glm::vec4 sphere = ComputeBoundingSphere(verts);

    // Center should be at origin.
    EXPECT_NEAR(sphere.x, 0.0f, 1e-5f);
    EXPECT_NEAR(sphere.y, 0.0f, 1e-5f);
    EXPECT_NEAR(sphere.z, 0.0f, 1e-5f);

    // Radius = half-diagonal of [-1,1]^3 = sqrt(3) ≈ 1.732.
    EXPECT_NEAR(sphere.w, std::sqrt(3.0f), 1e-4f);
}

TEST(Culling, BoundingSphere_OffsetGeometry)
{
    // Geometry offset from origin: center should reflect the offset.
    std::vector<glm::vec3> verts = {
        {10, 20, 30}, {12, 20, 30}, {10, 22, 30}, {12, 22, 30}
    };

    const glm::vec4 sphere = ComputeBoundingSphere(verts);

    EXPECT_NEAR(sphere.x, 11.0f, 1e-5f);
    EXPECT_NEAR(sphere.y, 21.0f, 1e-5f);
    EXPECT_NEAR(sphere.z, 30.0f, 1e-5f);
    EXPECT_GT(sphere.w, 0.0f);
}

TEST(Culling, BoundingSphere_ContainsAllVertices)
{
    // Randomly-offset geometry — sphere must contain all points.
    std::vector<glm::vec3> verts = {
        {1, 2, 3}, {-4, 5, -6}, {7, -8, 9}, {0, 0, 0}, {-1, -1, -1}
    };

    const glm::vec4 sphere = ComputeBoundingSphere(verts);
    const glm::vec3 center{sphere.x, sphere.y, sphere.z};
    const float radius = sphere.w;

    for (const auto& v : verts)
    {
        const float dist = glm::length(v - center);
        EXPECT_LE(dist, radius + 1e-4f) << "Vertex " << v.x << "," << v.y << "," << v.z
                                          << " outside sphere (dist=" << dist << ", radius=" << radius << ")";
    }
}

// ---- FrustumCullSphere: world-space transform + frustum test ----

TEST(Culling, FrustumCullSphere_IdentityTransform_InFront)
{
    const auto frustum = MakeTestFrustum();

    // Object at z=-5 (in front of camera), radius 1.
    const glm::vec4 localBounds{0.0f, 0.0f, -5.0f, 1.0f};
    const glm::mat4 identity(1.0f);

    EXPECT_TRUE(FrustumCullSphere_CPU(identity, localBounds, frustum));
}

TEST(Culling, FrustumCullSphere_IdentityTransform_Behind)
{
    const auto frustum = MakeTestFrustum();

    // Object at z=+5 (behind camera), radius 1.
    const glm::vec4 localBounds{0.0f, 0.0f, 5.0f, 1.0f};
    const glm::mat4 identity(1.0f);

    EXPECT_FALSE(FrustumCullSphere_CPU(identity, localBounds, frustum));
}

TEST(Culling, FrustumCullSphere_TranslatedIntoView)
{
    const auto frustum = MakeTestFrustum();

    // Geometry centered at origin, but translated to z=-10 by world matrix.
    const glm::vec4 localBounds{0.0f, 0.0f, 0.0f, 1.0f};
    const glm::mat4 world = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -10.0f));

    EXPECT_TRUE(FrustumCullSphere_CPU(world, localBounds, frustum));
}

TEST(Culling, FrustumCullSphere_TranslatedOutOfView)
{
    const auto frustum = MakeTestFrustum();

    // Geometry centered at origin, but translated far to the right (out of frustum).
    const glm::vec4 localBounds{0.0f, 0.0f, 0.0f, 1.0f};
    const glm::mat4 world = glm::translate(glm::mat4(1.0f), glm::vec3(1000.0f, 0.0f, -5.0f));

    EXPECT_FALSE(FrustumCullSphere_CPU(world, localBounds, frustum));
}

TEST(Culling, FrustumCullSphere_ScaledUp)
{
    const auto frustum = MakeTestFrustum();

    // Small local sphere (radius 0.1) at origin, but scaled 100x by world matrix.
    // The world-space radius becomes 10 — should be visible when placed in view.
    const glm::vec4 localBounds{0.0f, 0.0f, 0.0f, 0.1f};
    const glm::mat4 world = glm::translate(
        glm::scale(glm::mat4(1.0f), glm::vec3(100.0f)),
        glm::vec3(0.0f, 0.0f, -0.5f)); // Local -0.5 * 100 = world z=-50

    EXPECT_TRUE(FrustumCullSphere_CPU(world, localBounds, frustum));
}

TEST(Culling, FrustumCullSphere_ZeroRadius)
{
    const auto frustum = MakeTestFrustum();

    // Zero radius = empty/inactive geometry → always culled.
    const glm::vec4 localBounds{0.0f, 0.0f, -5.0f, 0.0f};
    const glm::mat4 identity(1.0f);

    EXPECT_FALSE(FrustumCullSphere_CPU(identity, localBounds, frustum));
}

TEST(Culling, FrustumCullSphere_NegativeRadius)
{
    const auto frustum = MakeTestFrustum();

    // Negative radius = sentinel "preserve bounds" in GPUScene → treat as culled.
    const glm::vec4 localBounds{0.0f, 0.0f, -5.0f, -1.0f};
    const glm::mat4 identity(1.0f);

    EXPECT_FALSE(FrustumCullSphere_CPU(identity, localBounds, frustum));
}

TEST(Culling, FrustumCullSphere_MatchesGpuRule)
{
    // Validate that FrustumCullSphere_CPU matches the GPU cull shader rule
    // for a variety of positions and transforms.
    const auto frustum = MakeTestFrustum();

    std::array<glm::vec4, 6> planes{};
    for (int i = 0; i < 6; ++i)
        planes[i] = glm::vec4(frustum.Planes[i].Normal, frustum.Planes[i].Distance);

    struct TestCase
    {
        glm::mat4 WorldMatrix;
        glm::vec4 LocalBounds;
    };

    const std::array<TestCase, 5> cases = {{
        {glm::mat4(1.0f), {0.0f, 0.0f, -5.0f, 1.0f}},       // In front, visible
        {glm::mat4(1.0f), {0.0f, 0.0f, 5.0f, 1.0f}},        // Behind, culled
        {glm::translate(glm::mat4(1.0f), {100.0f, 0.0f, -5.0f}), {0.0f, 0.0f, 0.0f, 0.5f}}, // Far right, culled
        {glm::translate(glm::mat4(1.0f), {0.0f, 0.0f, -500.0f}), {0.0f, 0.0f, 0.0f, 2.0f}}, // Far in front, visible
        {glm::scale(glm::mat4(1.0f), {5.0f, 5.0f, 5.0f}), {0.0f, 0.0f, -2.0f, 1.0f}},      // Scaled, in front
    }};

    for (size_t i = 0; i < cases.size(); ++i)
    {
        const auto& tc = cases[i];
        const bool cpuResult = FrustumCullSphere_CPU(tc.WorldMatrix, tc.LocalBounds, frustum);

        // Compute world-space sphere the same way FrustumCullSphere does.
        const glm::vec3 localCenter{tc.LocalBounds.x, tc.LocalBounds.y, tc.LocalBounds.z};
        const glm::vec3 worldCenter = glm::vec3(tc.WorldMatrix * glm::vec4(localCenter, 1.0f));
        const float sx = glm::length(glm::vec3(tc.WorldMatrix[0]));
        const float sy = glm::length(glm::vec3(tc.WorldMatrix[1]));
        const float sz = glm::length(glm::vec3(tc.WorldMatrix[2]));
        const float worldRadius = tc.LocalBounds.w * glm::max(sx, glm::max(sy, sz));
        const bool gpuResult = SphereVisibleGpuRule(planes, worldCenter, worldRadius);

        EXPECT_EQ(cpuResult, gpuResult) << "Mismatch at test case " << i;
    }
}

TEST(Culling, FrustumCullSphere_NonUniformScale)
{
    const auto frustum = MakeTestFrustum();

    // Non-uniform scale: 1x, 1y, 100z. The max axis scale is 100.
    // Local sphere at (0,0,-0.05) radius 0.01 → world sphere at (0,0,-5) radius 1.0.
    const glm::vec4 localBounds{0.0f, 0.0f, -0.05f, 0.01f};
    const glm::mat4 world = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, 1.0f, 100.0f));

    EXPECT_TRUE(FrustumCullSphere_CPU(world, localBounds, frustum));
}

TEST(Culling, FrustumCullSphere_BeyondFarPlane)
{
    const auto frustum = MakeTestFrustum(); // Far plane at 1000.

    // Object at z=-1500 (beyond far plane), small radius.
    const glm::vec4 localBounds{0.0f, 0.0f, -1500.0f, 1.0f};
    const glm::mat4 identity(1.0f);

    EXPECT_FALSE(FrustumCullSphere_CPU(identity, localBounds, frustum));
}
