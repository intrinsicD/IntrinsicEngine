#include <gtest/gtest.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

import Geometry;
import Graphics;

using Graphics::DebugDraw;

static Geometry::AABB MakeLocalAabb()
{
    return Geometry::AABB{glm::vec3{-1.0f, -2.0f, -0.5f}, glm::vec3{1.0f, 2.0f, 0.5f}};
}

static Geometry::OBB MakeWorldObb()
{
    Geometry::OBB obb;
    obb.Center = glm::vec3(10.0f, -2.0f, 5.0f);
    obb.Extents = glm::vec3(3.0f, 2.0f, 1.0f);
    obb.Rotation = glm::angleAxis(glm::radians(45.0f), glm::normalize(glm::vec3(0.0f, 1.0f, 1.0f)));
    return obb;
}

TEST(BoundingDebugDraw, DisabledEmitsNothing)
{
    DebugDraw dd;

    Graphics::BoundingDebugDrawSettings s;
    s.Enabled = false;

    DrawBoundingVolumes(dd, MakeLocalAabb(), MakeWorldObb(), s);

    EXPECT_EQ(dd.GetLineCount(), 0u);
    EXPECT_EQ(dd.GetOverlayLineCount(), 0u);
}

TEST(BoundingDebugDraw, DrawObbAndAabb)
{
    DebugDraw dd;

    Graphics::BoundingDebugDrawSettings s;
    s.Enabled = true;
    s.Overlay = false;
    s.DrawAABB = true;
    s.DrawOBB = true;
    s.DrawBoundingSphere = false;

    DrawBoundingVolumes(dd, MakeLocalAabb(), MakeWorldObb(), s);

    // AABB: 12 segments + OBB: 12 segments.
    EXPECT_EQ(dd.GetLineCount(), 24u);
    EXPECT_EQ(dd.GetOverlayLineCount(), 0u);
}

TEST(BoundingDebugDraw, DrawSphereAddsExpectedSegments)
{
    DebugDraw dd;

    Graphics::BoundingDebugDrawSettings s;
    s.Enabled = true;
    s.Overlay = true;
    s.DrawAABB = false;
    s.DrawOBB = false;
    s.DrawBoundingSphere = true;

    DrawBoundingVolumes(dd, MakeLocalAabb(), MakeWorldObb(), s);

    // DebugDraw sphere defaults to 3 circles * 24 segments = 72.
    EXPECT_EQ(dd.GetOverlayLineCount(), 72u);
    EXPECT_EQ(dd.GetLineCount(), 0u);
}

TEST(BoundingDebugDraw, InvalidInputRejected)
{
    DebugDraw dd;

    Geometry::AABB invalidLocal;
    invalidLocal.Min = glm::vec3(2.0f);
    invalidLocal.Max = glm::vec3(-2.0f);

    Graphics::BoundingDebugDrawSettings s;
    s.Enabled = true;

    DrawBoundingVolumes(dd, invalidLocal, MakeWorldObb(), s);
    EXPECT_EQ(dd.GetLineCount(), 0u);
    EXPECT_EQ(dd.GetOverlayLineCount(), 0u);
}
