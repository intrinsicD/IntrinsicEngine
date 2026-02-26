#include <gtest/gtest.h>

#include <vector>
#include <glm/glm.hpp>

import Graphics;

using Graphics::DebugDraw;

TEST(BVHDebugDraw, DisabledEmitsNothing)
{
    DebugDraw dd;
    std::vector<glm::vec3> positions{
        {-1.0f, -1.0f, 0.0f}, {1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 0.0f},
        {-1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, {-1.0f, 1.0f, 0.0f}
    };

    Graphics::BVHDebugDrawSettings settings{};
    settings.Enabled = false;

    DrawBVH(dd, positions, {}, settings);

    EXPECT_EQ(dd.GetLineCount(), 0u);
    EXPECT_EQ(dd.GetOverlayLineCount(), 0u);
}

TEST(BVHDebugDraw, OverlayRootOnly)
{
    DebugDraw dd;
    std::vector<glm::vec3> positions{
        {-2.0f, 0.0f, 0.0f}, {-1.0f, 1.0f, 0.0f}, {-1.0f, -1.0f, 0.0f},
        {1.0f, 0.0f, 0.0f}, {2.0f, 1.0f, 0.0f}, {2.0f, -1.0f, 0.0f}
    };

    Graphics::BVHDebugDrawSettings settings{};
    settings.Enabled = true;
    settings.Overlay = true;
    settings.MaxDepth = 0;

    DrawBVH(dd, positions, {}, settings);

    EXPECT_EQ(dd.GetOverlayLineCount(), 12u);
    EXPECT_EQ(dd.GetLineCount(), 0u);
}

TEST(BVHDebugDraw, LeafOnlyDrawsLeaves)
{
    DebugDraw dd;
    std::vector<glm::vec3> positions{
        {-2.0f, 0.0f, 0.0f}, {-1.0f, 1.0f, 0.0f}, {-1.0f, -1.0f, 0.0f},
        {1.0f, 0.0f, 0.0f}, {2.0f, 1.0f, 0.0f}, {2.0f, -1.0f, 0.0f}
    };

    Graphics::BVHDebugDrawSettings settings{};
    settings.Enabled = true;
    settings.Overlay = false;
    settings.MaxDepth = 4;
    settings.LeafOnly = true;
    settings.DrawInternal = false;
    settings.LeafTriangleCount = 1;

    DrawBVH(dd, positions, {}, settings);

    EXPECT_EQ(dd.GetLineCount(), 24u);
    EXPECT_EQ(dd.GetOverlayLineCount(), 0u);
}
