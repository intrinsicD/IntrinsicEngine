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
