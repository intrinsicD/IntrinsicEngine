#include <gtest/gtest.h>

#include <cstdint>
#include <cmath>
#include <span>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

import Graphics;

using Graphics::DebugDraw;

// =========================================================================
// Helper: check that all line endpoints have the expected color
// =========================================================================
static void ExpectAllColor(std::span<const DebugDraw::LineSegment> lines, uint32_t color)
{
    for (const auto& seg : lines)
    {
        EXPECT_EQ(seg.ColorStart, color);
        EXPECT_EQ(seg.ColorEnd, color);
    }
}

// =========================================================================
// Color Packing
// =========================================================================

TEST(DebugDraw_Color, PackColorBytes)
{
    uint32_t c = DebugDraw::PackColor(255, 0, 0, 255);
    EXPECT_EQ(c & 0xFF, 255);          // R
    EXPECT_EQ((c >> 8) & 0xFF, 0);     // G
    EXPECT_EQ((c >> 16) & 0xFF, 0);    // B
    EXPECT_EQ((c >> 24) & 0xFF, 255);  // A
}

TEST(DebugDraw_Color, PackColorFloat)
{
    uint32_t c = DebugDraw::PackColorF(1.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_EQ(c, DebugDraw::Red());
}

TEST(DebugDraw_Color, PackColorGreen)
{
    uint32_t c = DebugDraw::PackColor(0, 255, 0, 255);
    EXPECT_EQ(c, DebugDraw::Green());
}

TEST(DebugDraw_Color, PackColorFloatClamped)
{
    uint32_t c = DebugDraw::PackColorF(2.0f, -1.0f, 0.5f, 1.0f);
    EXPECT_EQ(c & 0xFF, 255);          // R clamped to 255
    EXPECT_EQ((c >> 8) & 0xFF, 0);     // G clamped to 0
    // B: 0.5 * 255 + 0.5 = 128
    EXPECT_EQ((c >> 16) & 0xFF, 128);  // B = 128
}

TEST(DebugDraw_Color, PredefinedColors)
{
    // Just verify predefined colors are non-zero and distinct
    EXPECT_NE(DebugDraw::Red(), 0u);
    EXPECT_NE(DebugDraw::Green(), 0u);
    EXPECT_NE(DebugDraw::Blue(), 0u);
    EXPECT_NE(DebugDraw::Red(), DebugDraw::Green());
    EXPECT_NE(DebugDraw::Green(), DebugDraw::Blue());
    EXPECT_NE(DebugDraw::Red(), DebugDraw::Blue());
}

// =========================================================================
// LineSegment Layout
// =========================================================================

TEST(DebugDraw_Layout, LineSegmentSize)
{
    EXPECT_EQ(sizeof(DebugDraw::LineSegment), 32u);
    EXPECT_EQ(alignof(DebugDraw::LineSegment), 16u);
}

// =========================================================================
// Reset / Empty State
// =========================================================================

TEST(DebugDraw, InitiallyEmpty)
{
    DebugDraw dd;
    EXPECT_EQ(dd.GetLineCount(), 0u);
    EXPECT_EQ(dd.GetOverlayLineCount(), 0u);
    EXPECT_FALSE(dd.HasContent());
    EXPECT_TRUE(dd.GetLines().empty());
    EXPECT_TRUE(dd.GetOverlayLines().empty());
}

TEST(DebugDraw, ResetClearsAll)
{
    DebugDraw dd;
    dd.Line({0, 0, 0}, {1, 1, 1}, DebugDraw::Red());
    dd.OverlayLine({0, 0, 0}, {1, 1, 1}, DebugDraw::Green());
    EXPECT_TRUE(dd.HasContent());

    dd.Reset();
    EXPECT_EQ(dd.GetLineCount(), 0u);
    EXPECT_EQ(dd.GetOverlayLineCount(), 0u);
    EXPECT_FALSE(dd.HasContent());
}

// =========================================================================
// Single Line
// =========================================================================

TEST(DebugDraw, SingleLine)
{
    DebugDraw dd;
    glm::vec3 a{0, 0, 0}, b{1, 2, 3};
    dd.Line(a, b, DebugDraw::Red());

    EXPECT_EQ(dd.GetLineCount(), 1u);
    auto lines = dd.GetLines();
    ASSERT_EQ(lines.size(), 1u);

    EXPECT_EQ(lines[0].Start, a);
    EXPECT_EQ(lines[0].End, b);
    EXPECT_EQ(lines[0].ColorStart, DebugDraw::Red());
    EXPECT_EQ(lines[0].ColorEnd, DebugDraw::Red());
}

TEST(DebugDraw, LineGradient)
{
    DebugDraw dd;
    dd.Line({0, 0, 0}, {1, 0, 0}, DebugDraw::Red(), DebugDraw::Blue());

    auto lines = dd.GetLines();
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0].ColorStart, DebugDraw::Red());
    EXPECT_EQ(lines[0].ColorEnd, DebugDraw::Blue());
}

// =========================================================================
// AABB Box (12 edges)
// =========================================================================

TEST(DebugDraw, BoxProduces12Edges)
{
    DebugDraw dd;
    dd.Box({-1, -1, -1}, {1, 1, 1}, DebugDraw::Green());

    EXPECT_EQ(dd.GetLineCount(), 12u);
    ExpectAllColor(dd.GetLines(), DebugDraw::Green());
}

TEST(DebugDraw, BoxEndpointsWithinBounds)
{
    DebugDraw dd;
    glm::vec3 lo{-1, -2, -3}, hi{4, 5, 6};
    dd.Box(lo, hi, DebugDraw::White());

    for (const auto& seg : dd.GetLines())
    {
        for (int axis = 0; axis < 3; ++axis)
        {
            EXPECT_GE(seg.Start[axis], lo[axis] - 1e-5f);
            EXPECT_LE(seg.Start[axis], hi[axis] + 1e-5f);
            EXPECT_GE(seg.End[axis], lo[axis] - 1e-5f);
            EXPECT_LE(seg.End[axis], hi[axis] + 1e-5f);
        }
    }
}

// =========================================================================
// WireBox (OBB with identity transform == AABB)
// =========================================================================

TEST(DebugDraw, WireBoxIdentity12Edges)
{
    DebugDraw dd;
    dd.WireBox(glm::mat4(1.0f), glm::vec3(1.0f), DebugDraw::Yellow());
    EXPECT_EQ(dd.GetLineCount(), 12u);
}

TEST(DebugDraw, WireBoxTransformedPreservesEdgeCount)
{
    DebugDraw dd;
    glm::mat4 transform = glm::translate(glm::mat4(1.0f), glm::vec3(5, 0, 0));
    dd.WireBox(transform, glm::vec3(0.5f, 1.0f, 2.0f), DebugDraw::Cyan());
    EXPECT_EQ(dd.GetLineCount(), 12u);

    // All endpoints should be near the translated center
    for (const auto& seg : dd.GetLines())
    {
        EXPECT_NEAR(seg.Start.x, 5.0f, 2.5f);
        EXPECT_NEAR(seg.End.x, 5.0f, 2.5f);
    }
}

// =========================================================================
// Sphere (3 great circles)
// =========================================================================

TEST(DebugDraw, SphereDefault24SegmentsProduces72Lines)
{
    DebugDraw dd;
    dd.Sphere({0, 0, 0}, 1.0f, DebugDraw::Blue());
    // 3 great circles * 24 segments = 72 line segments
    EXPECT_EQ(dd.GetLineCount(), 72u);
}

TEST(DebugDraw, Sphere8SegmentsProduces24Lines)
{
    DebugDraw dd;
    dd.Sphere({0, 0, 0}, 1.0f, DebugDraw::Blue(), 8);
    // 3 great circles * 8 segments = 24 line segments
    EXPECT_EQ(dd.GetLineCount(), 24u);
}

TEST(DebugDraw, SpherePointsNearRadius)
{
    DebugDraw dd;
    float radius = 2.0f;
    glm::vec3 center{1, 2, 3};
    dd.Sphere(center, radius, DebugDraw::White(), 16);

    for (const auto& seg : dd.GetLines())
    {
        float dStart = glm::length(seg.Start - center);
        float dEnd = glm::length(seg.End - center);
        EXPECT_NEAR(dStart, radius, 0.01f);
        EXPECT_NEAR(dEnd, radius, 0.01f);
    }
}

// =========================================================================
// Circle
// =========================================================================

TEST(DebugDraw, CircleSegmentCount)
{
    DebugDraw dd;
    dd.Circle({0, 0, 0}, {0, 1, 0}, 1.0f, DebugDraw::Magenta(), 16);
    EXPECT_EQ(dd.GetLineCount(), 16u);
}

TEST(DebugDraw, CirclePointsOnPlane)
{
    DebugDraw dd;
    glm::vec3 center{0, 5, 0};
    glm::vec3 normal{0, 1, 0}; // XZ plane at y=5
    dd.Circle(center, normal, 3.0f, DebugDraw::White(), 32);

    for (const auto& seg : dd.GetLines())
    {
        // All points should be at y=5 (on the plane)
        EXPECT_NEAR(seg.Start.y, 5.0f, 0.01f);
        EXPECT_NEAR(seg.End.y, 5.0f, 0.01f);
    }
}

// =========================================================================
// Arrow
// =========================================================================

TEST(DebugDraw, ArrowProduces5Lines)
{
    DebugDraw dd;
    dd.Arrow({0, 0, 0}, {0, 0, 5}, 0.5f, DebugDraw::Red());
    // 1 shaft + 4 arrowhead lines
    EXPECT_EQ(dd.GetLineCount(), 5u);
}

TEST(DebugDraw, ArrowDegenerateProducesNothing)
{
    DebugDraw dd;
    dd.Arrow({1, 1, 1}, {1, 1, 1}, 0.5f, DebugDraw::Red());
    // Zero-length arrow should produce nothing
    EXPECT_EQ(dd.GetLineCount(), 0u);
}

// =========================================================================
// Axes
// =========================================================================

TEST(DebugDraw, AxesProduces3Lines)
{
    DebugDraw dd;
    dd.Axes({0, 0, 0}, 1.0f);
    EXPECT_EQ(dd.GetLineCount(), 3u);

    auto lines = dd.GetLines();
    // Line 0: X axis (red)
    EXPECT_EQ(lines[0].ColorStart, DebugDraw::Red());
    // Line 1: Y axis (green)
    EXPECT_EQ(lines[1].ColorStart, DebugDraw::Green());
    // Line 2: Z axis (blue)
    EXPECT_EQ(lines[2].ColorStart, DebugDraw::Blue());
}

TEST(DebugDraw, AxesTransformProduces3Lines)
{
    DebugDraw dd;
    glm::mat4 transform = glm::translate(glm::mat4(1.0f), glm::vec3(10, 20, 30));
    dd.Axes(transform, 2.0f);
    EXPECT_EQ(dd.GetLineCount(), 3u);

    // All lines should start at the transform's origin
    for (const auto& seg : dd.GetLines())
    {
        EXPECT_NEAR(seg.Start.x, 10.0f, 0.01f);
        EXPECT_NEAR(seg.Start.y, 20.0f, 0.01f);
        EXPECT_NEAR(seg.Start.z, 30.0f, 0.01f);
    }
}

// =========================================================================
// Frustum
// =========================================================================

TEST(DebugDraw, FrustumProduces12Lines)
{
    DebugDraw dd;
    // Use a perspective projection matrix
    glm::mat4 proj = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
    glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    glm::mat4 invViewProj = glm::inverse(proj * view);
    dd.Frustum(invViewProj, DebugDraw::Yellow());

    // 4 near + 4 far + 4 connecting = 12 edges
    EXPECT_EQ(dd.GetLineCount(), 12u);
}

// =========================================================================
// Grid
// =========================================================================

TEST(DebugDraw, GridLineCount)
{
    DebugDraw dd;
    int countU = 4, countV = 3;
    dd.Grid({0, 0, 0}, {1, 0, 0}, {0, 0, 1}, countU, countV, 1.0f, DebugDraw::Gray());

    // (countU + 1) lines in V direction + (countV + 1) lines in U direction
    uint32_t expected = static_cast<uint32_t>((countU + 1) + (countV + 1));
    EXPECT_EQ(dd.GetLineCount(), expected);
}

// =========================================================================
// Cross
// =========================================================================

TEST(DebugDraw, CrossProduces3Lines)
{
    DebugDraw dd;
    dd.Cross({0, 0, 0}, 2.0f, DebugDraw::White());
    EXPECT_EQ(dd.GetLineCount(), 3u);
}

// =========================================================================
// Overlay Lines (separate from depth-tested)
// =========================================================================

TEST(DebugDraw, OverlayIsSeparate)
{
    DebugDraw dd;
    dd.Line({0, 0, 0}, {1, 0, 0}, DebugDraw::Red());
    dd.OverlayLine({0, 0, 0}, {0, 1, 0}, DebugDraw::Green());

    EXPECT_EQ(dd.GetLineCount(), 1u);
    EXPECT_EQ(dd.GetOverlayLineCount(), 1u);
    EXPECT_TRUE(dd.HasContent());

    EXPECT_EQ(dd.GetLines()[0].ColorStart, DebugDraw::Red());
    EXPECT_EQ(dd.GetOverlayLines()[0].ColorStart, DebugDraw::Green());
}

TEST(DebugDraw, OverlayBoxProduces12Edges)
{
    DebugDraw dd;
    dd.OverlayBox({-1, -1, -1}, {1, 1, 1}, DebugDraw::Orange());
    EXPECT_EQ(dd.GetOverlayLineCount(), 12u);
    EXPECT_EQ(dd.GetLineCount(), 0u); // no depth-tested lines
}

TEST(DebugDraw, OverlaySphereProducesLines)
{
    DebugDraw dd;
    dd.OverlaySphere({0, 0, 0}, 1.0f, DebugDraw::Cyan(), 8);
    EXPECT_EQ(dd.GetOverlayLineCount(), 24u);
    EXPECT_EQ(dd.GetLineCount(), 0u);
}

TEST(DebugDraw, OverlayAxesProduces3Lines)
{
    DebugDraw dd;
    dd.OverlayAxes({0, 0, 0}, 1.0f);
    EXPECT_EQ(dd.GetOverlayLineCount(), 3u);
    EXPECT_EQ(dd.GetLineCount(), 0u);
}

// =========================================================================
// Multiple frames (reset between)
// =========================================================================

TEST(DebugDraw, MultipleFrameResets)
{
    DebugDraw dd;

    // Frame 1
    dd.Line({0, 0, 0}, {1, 0, 0}, DebugDraw::Red());
    dd.Box({-1, -1, -1}, {1, 1, 1}, DebugDraw::Green());
    EXPECT_EQ(dd.GetLineCount(), 13u); // 1 + 12

    // Frame 2
    dd.Reset();
    EXPECT_EQ(dd.GetLineCount(), 0u);
    dd.Sphere({0, 0, 0}, 1.0f, DebugDraw::Blue(), 8);
    EXPECT_EQ(dd.GetLineCount(), 24u); // 3 * 8

    // Frame 3
    dd.Reset();
    EXPECT_EQ(dd.GetLineCount(), 0u);
    EXPECT_FALSE(dd.HasContent());
}

// =========================================================================
// Accumulation (multiple primitives in one frame)
// =========================================================================

TEST(DebugDraw, AccumulationCorrectCount)
{
    DebugDraw dd;
    dd.Line({0, 0, 0}, {1, 0, 0}, DebugDraw::Red());    // +1
    dd.Line({0, 0, 0}, {0, 1, 0}, DebugDraw::Green());   // +1
    dd.Box({-1, -1, -1}, {1, 1, 1}, DebugDraw::Blue());   // +12
    dd.Cross({0, 0, 0}, 1.0f, DebugDraw::White());        // +3
    dd.Axes({0, 0, 0}, 1.0f);                              // +3

    EXPECT_EQ(dd.GetLineCount(), 20u);
}
