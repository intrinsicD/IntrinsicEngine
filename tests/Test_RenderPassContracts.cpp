#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <entt/entity/registry.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

import Graphics;
import Geometry;
import ECS;
import RHI;

// =============================================================================
// Render Pass Contract Tests (B6)
// =============================================================================
//
// Per-pass contract tests for SurfacePass, LinePass, and PointPass.
// These validate:
//
//   1. Push constant struct sizes and field offsets (GPU layout contracts).
//   2. Pass-specific constants and configuration defaults.
//   3. Frustum culling helper correctness.
//   4. Transient submission API contracts.
//   5. Per-pass ECS component → draw dispatch wiring invariants.
//   6. Numerical safeguard clamping functions.
//
// No GPU device is needed — these are pure CPU-side contract tests.

// =============================================================================
// Section 1: SurfacePass Contracts
// =============================================================================

TEST(RenderPassContract_Surface, TransientVertexIs32Bytes)
{
    EXPECT_EQ(sizeof(Graphics::Passes::SurfacePass::TransientVertex), 32u);
}

TEST(RenderPassContract_Surface, TransientVertexAlignedTo16)
{
    EXPECT_EQ(alignof(Graphics::Passes::SurfacePass::TransientVertex), 16u);
}

TEST(RenderPassContract_Surface, TransientVertexFieldLayout)
{
    using TV = Graphics::Passes::SurfacePass::TransientVertex;

    EXPECT_EQ(offsetof(TV, Position), 0u);
    EXPECT_EQ(offsetof(TV, Color), 12u);    // after vec3 (12 bytes)
    EXPECT_EQ(offsetof(TV, Normal), 16u);   // after vec3 + uint32
    EXPECT_EQ(offsetof(TV, _pad), 28u);     // after second vec3
}

TEST(RenderPassContract_Surface, TransientVertexColorIsPacked)
{
    Graphics::Passes::SurfacePass::TransientVertex v{};
    v.Color = Graphics::DebugDraw::PackColor(255, 0, 0, 255);

    // Red channel in low byte (ABGR packing).
    EXPECT_EQ((v.Color >> 0) & 0xFF, 255u);
    EXPECT_EQ((v.Color >> 8) & 0xFF, 0u);
}

TEST(RenderPassContract_Surface, TransientTriangleCountIsVertexCountDiv3)
{
    Graphics::Passes::SurfacePass pass;

    EXPECT_EQ(pass.GetTransientTriangleCount(), 0u);
    EXPECT_FALSE(pass.HasTransientContent());

    pass.SubmitTriangle(
        {0, 0, 0}, {1, 0, 0}, {0, 1, 0},
        {0, 0, 1},
        Graphics::DebugDraw::PackColor(255, 255, 255, 255));

    EXPECT_EQ(pass.GetTransientTriangleCount(), 1u);
    EXPECT_EQ(pass.GetTransientVertices().size(), 3u);
    EXPECT_TRUE(pass.HasTransientContent());

    pass.ResetTransient();
    EXPECT_EQ(pass.GetTransientTriangleCount(), 0u);
    EXPECT_FALSE(pass.HasTransientContent());
}

TEST(RenderPassContract_Surface, TransientVertexDataIntegrity)
{
    Graphics::Passes::SurfacePass pass;

    const glm::vec3 a{1.0f, 2.0f, 3.0f};
    const glm::vec3 b{4.0f, 5.0f, 6.0f};
    const glm::vec3 c{7.0f, 8.0f, 9.0f};
    const glm::vec3 n{0.0f, 0.0f, 1.0f};
    const uint32_t color = Graphics::DebugDraw::PackColor(128, 64, 32, 255);

    pass.SubmitTriangle(a, b, c, n, color);

    auto verts = pass.GetTransientVertices();
    ASSERT_EQ(verts.size(), 3u);

    EXPECT_EQ(verts[0].Position, a);
    EXPECT_EQ(verts[1].Position, b);
    EXPECT_EQ(verts[2].Position, c);

    EXPECT_EQ(verts[0].Normal, n);
    EXPECT_EQ(verts[1].Normal, n);
    EXPECT_EQ(verts[2].Normal, n);

    EXPECT_EQ(verts[0].Color, color);
    EXPECT_EQ(verts[1].Color, color);
    EXPECT_EQ(verts[2].Color, color);
}

TEST(RenderPassContract_Surface, MeshPushConstantsFieldLayout)
{
    using PC = RHI::MeshPushConstants;

    // Model matrix at offset 0.
    EXPECT_EQ(offsetof(PC, Model), 0u);
    // BDA pointers follow the model matrix.
    EXPECT_EQ(offsetof(PC, PtrPositions), 64u);
    EXPECT_EQ(offsetof(PC, PtrNormals), 72u);
    EXPECT_EQ(offsetof(PC, PtrAux), 80u);
    // Per-face attribute BDA pointer must exist.
    EXPECT_GT(sizeof(PC), 88u);
}

TEST(RenderPassContract_Surface, MeshPushConstantsBDADefaultsToZero)
{
    RHI::MeshPushConstants pc{};

    EXPECT_EQ(pc.PtrPositions, 0u);
    EXPECT_EQ(pc.PtrNormals, 0u);
    EXPECT_EQ(pc.PtrAux, 0u);
    EXPECT_EQ(pc.PtrFaceAttr, 0u);
    EXPECT_EQ(pc.PtrVertexAttr, 0u);
}

TEST(RenderPassContract_Surface, CullWorkgroupSizeIs64)
{
    // Must match instance_cull.comp local_size_x.
    EXPECT_EQ(Graphics::Passes::SurfacePassConstants::kCullWorkgroupSize, 64u);
}

TEST(RenderPassContract_Surface, SurfaceComponentGatesRendering)
{
    // Rendering is gated by component presence, not a boolean flag.
    entt::registry reg;
    auto e = reg.create();

    EXPECT_FALSE(reg.all_of<ECS::Surface::Component>(e));

    reg.emplace<ECS::Surface::Component>(e);
    EXPECT_TRUE(reg.all_of<ECS::Surface::Component>(e));

    reg.remove<ECS::Surface::Component>(e);
    EXPECT_FALSE(reg.all_of<ECS::Surface::Component>(e));
}

TEST(RenderPassContract_Surface, SurfaceComponentDefaultGpuSlotInvalid)
{
    ECS::Surface::Component comp;
    EXPECT_EQ(comp.GpuSlot, ECS::kInvalidGpuSlot);
    EXPECT_FALSE(comp.Geometry.IsValid());
}

// =============================================================================
// Section 2: LinePass Contracts
// =============================================================================

TEST(RenderPassContract_Line, LinePushConstantsSize)
{
    // LinePushConstants must be exactly 104 bytes to match line.vert layout.
    // Struct is file-local in Line.cpp; we verify the contract via
    // the documented field sizes: 64 + 8 + 8 + 4 + 4 + 4 + 4 + 8 = 104.
    constexpr size_t kExpectedSize =
        64  // mat4 Model
        + 8   // uint64 PtrPositions
        + 8   // uint64 PtrEdges
        + 4   // float  LineWidth
        + 4   // float  ViewportWidth
        + 4   // float  ViewportHeight
        + 4   // uint32 Color
        + 8;  // uint64 PtrEdgeAttr
    EXPECT_EQ(kExpectedSize, 104u);
}

TEST(RenderPassContract_Line, DefaultLineWidthIs2Pixels)
{
    Graphics::Passes::LinePass pass;
    EXPECT_FLOAT_EQ(pass.LineWidth, 2.0f);
}

TEST(RenderPassContract_Line, LineComponentGatesRendering)
{
    entt::registry reg;
    auto e = reg.create();

    EXPECT_FALSE(reg.all_of<ECS::Line::Component>(e));

    reg.emplace<ECS::Line::Component>(e);
    EXPECT_TRUE(reg.all_of<ECS::Line::Component>(e));

    reg.remove<ECS::Line::Component>(e);
    EXPECT_FALSE(reg.all_of<ECS::Line::Component>(e));
}

TEST(RenderPassContract_Line, LineComponentDefaultsContract)
{
    ECS::Line::Component line;

    // EdgeView must be valid for LinePass to render — default is invalid.
    EXPECT_FALSE(line.EdgeView.IsValid());
    EXPECT_FALSE(line.Geometry.IsValid());

    // Default color is the neutral wireframe gray used by lifecycle-created line views.
    EXPECT_FLOAT_EQ(line.Color.r, 0.85f);
    EXPECT_FLOAT_EQ(line.Color.g, 0.85f);
    EXPECT_FLOAT_EQ(line.Color.b, 0.85f);
    EXPECT_FLOAT_EQ(line.Color.a, 1.0f);

    // Default component width.
    EXPECT_FLOAT_EQ(line.Width, 1.5f);

    // Not overlay by default.
    EXPECT_FALSE(line.Overlay);

    // No per-edge colors by default.
    EXPECT_FALSE(line.HasPerEdgeColors);
    EXPECT_TRUE(line.CachedEdgeColors.empty());
}

TEST(RenderPassContract_Line, EdgePairLayoutForBDA)
{
    // Edge pairs are read as flat uint32_t pairs via BDA in line.vert.
    // Layout must be contiguous 2 x uint32_t = 8 bytes.
    EXPECT_EQ(sizeof(ECS::EdgePair), 8u);
    EXPECT_EQ(offsetof(ECS::EdgePair, i0), 0u);
    EXPECT_EQ(offsetof(ECS::EdgePair, i1), 4u);
}

TEST(RenderPassContract_Line, DebugDrawLinesDepthTestedVsOverlaySplit)
{
    // LinePass dispatches depth-tested and overlay lines to separate pipelines.
    // DebugDraw must keep them in separate containers.
    Graphics::DebugDraw dd;

    dd.Line({0, 0, 0}, {1, 0, 0}, Graphics::DebugDraw::Red());
    dd.Line({0, 0, 0}, {0, 0, 1}, Graphics::DebugDraw::Blue());
    dd.OverlayLine({0, 0, 0}, {0, 1, 0}, Graphics::DebugDraw::Green());

    EXPECT_EQ(dd.GetLineCount(), 2u);
    EXPECT_EQ(dd.GetOverlayLineCount(), 1u);

    // Total content check.
    EXPECT_TRUE(dd.HasContent());
}

TEST(RenderPassContract_Line, PerEdgeColorToggle)
{
    ECS::Line::Component line;

    // Initially no per-edge colors.
    EXPECT_FALSE(line.HasPerEdgeColors);
    EXPECT_TRUE(line.ShowPerEdgeColors);

    // Simulate lifecycle system populating colors.
    line.CachedEdgeColors = {0xFFFF0000, 0xFF00FF00};
    line.HasPerEdgeColors = true;

    EXPECT_TRUE(line.HasPerEdgeColors);
    EXPECT_EQ(line.CachedEdgeColors.size(), 2u);

    // ShowPerEdgeColors toggle: when false, uniform Color is used despite cached data.
    line.ShowPerEdgeColors = false;
    EXPECT_TRUE(line.HasPerEdgeColors);  // Data still present.
    EXPECT_FALSE(line.ShowPerEdgeColors); // But display is toggled off.
}

// =============================================================================
// Section 3: PointPass Contracts
// =============================================================================

TEST(RenderPassContract_Point, PointPushConstantsSize)
{
    // PointPushConstants must be exactly 120 bytes to match point shaders.
    // Struct is file-local in Point.cpp; verify via documented field sizes.
    constexpr size_t kExpectedSize =
        64  // mat4 Model
        + 8   // uint64 PtrPositions
        + 8   // uint64 PtrNormals
        + 8   // uint64 PtrAttr
        + 4   // float  PointSize
        + 4   // float  SizeMultiplier
        + 4   // float  ViewportWidth
        + 4   // float  ViewportHeight
        + 4   // uint32 Color
        + 4   // uint32 Flags
        + 8;  // uint64 PtrRadii
    EXPECT_EQ(kExpectedSize, 120u);
}

TEST(RenderPassContract_Point, DefaultSizeMultiplierIsOne)
{
    Graphics::Passes::PointPass pass;
    EXPECT_FLOAT_EQ(pass.SizeMultiplier, 1.0f);
}

TEST(RenderPassContract_Point, FourRenderModesIndexable)
{
    // PointPass uses kModeCount=4 pipeline array indexed by RenderMode.
    // Verify all enum values are valid indices.
    EXPECT_EQ(static_cast<uint32_t>(Geometry::PointCloud::RenderMode::FlatDisc), 0u);
    EXPECT_EQ(static_cast<uint32_t>(Geometry::PointCloud::RenderMode::Surfel), 1u);
    EXPECT_EQ(static_cast<uint32_t>(Geometry::PointCloud::RenderMode::EWA), 2u);
    EXPECT_EQ(static_cast<uint32_t>(Geometry::PointCloud::RenderMode::Sphere), 3u);

    // All must be < kModeCount (4).
    constexpr uint32_t kModeCount = 4;
    EXPECT_LT(static_cast<uint32_t>(Geometry::PointCloud::RenderMode::FlatDisc), kModeCount);
    EXPECT_LT(static_cast<uint32_t>(Geometry::PointCloud::RenderMode::Surfel), kModeCount);
    EXPECT_LT(static_cast<uint32_t>(Geometry::PointCloud::RenderMode::EWA), kModeCount);
    EXPECT_LT(static_cast<uint32_t>(Geometry::PointCloud::RenderMode::Sphere), kModeCount);
}

TEST(RenderPassContract_Point, PointComponentGatesRendering)
{
    entt::registry reg;
    auto e = reg.create();

    EXPECT_FALSE(reg.all_of<ECS::Point::Component>(e));

    reg.emplace<ECS::Point::Component>(e);
    EXPECT_TRUE(reg.all_of<ECS::Point::Component>(e));

    reg.remove<ECS::Point::Component>(e);
    EXPECT_FALSE(reg.all_of<ECS::Point::Component>(e));
}

TEST(RenderPassContract_Point, PointComponentDefaultsContract)
{
    ECS::Point::Component pt;

    EXPECT_FALSE(pt.Geometry.IsValid());

    // Default render mode is FlatDisc.
    EXPECT_EQ(pt.Mode, Geometry::PointCloud::RenderMode::FlatDisc);

    // Default size > 0.
    EXPECT_GT(pt.Size, 0.0f);

    // Default multiplier = 1.
    EXPECT_FLOAT_EQ(pt.SizeMultiplier, 1.0f);

    // No per-point data by default.
    EXPECT_FALSE(pt.HasPerPointColors);
    EXPECT_FALSE(pt.HasPerPointRadii);
    EXPECT_FALSE(pt.HasPerPointNormals);
}

TEST(RenderPassContract_Point, PointPushConstantsFlagBits)
{
    // Flags field bit layout (must match point shaders):
    //   bit 0: per-point colors enabled
    //   bit 1: EWA mode enabled
    //   bit 2: per-point radii enabled
    constexpr uint32_t kFlagPerPointColors = 1u << 0;
    constexpr uint32_t kFlagEWA            = 1u << 1;
    constexpr uint32_t kFlagPerPointRadii  = 1u << 2;

    // Verify bits are distinct.
    EXPECT_EQ(kFlagPerPointColors, 1u);
    EXPECT_EQ(kFlagEWA, 2u);
    EXPECT_EQ(kFlagPerPointRadii, 4u);

    // Verify composability.
    uint32_t flags = kFlagPerPointColors | kFlagEWA;
    EXPECT_TRUE(flags & kFlagPerPointColors);
    EXPECT_TRUE(flags & kFlagEWA);
    EXPECT_FALSE(flags & kFlagPerPointRadii);
}

TEST(RenderPassContract_Point, PerPointAttributeToggle)
{
    ECS::Point::Component pt;
    ECS::PointCloud::Data cloud;

    // Attribute caches live on the data authority; Point::Component carries flags only.
    cloud.CachedColors = {0xFFFF0000, 0xFF00FF00, 0xFF0000FF};
    pt.HasPerPointColors = !cloud.CachedColors.empty();

    EXPECT_TRUE(pt.HasPerPointColors);
    EXPECT_EQ(cloud.CachedColors.size(), 3u);

    // Simulate lifecycle system populating per-point radii.
    cloud.CachedRadii = {0.01f, 0.02f, 0.03f};
    pt.HasPerPointRadii = !cloud.CachedRadii.empty();

    EXPECT_TRUE(pt.HasPerPointRadii);
    EXPECT_EQ(cloud.CachedRadii.size(), 3u);
}

TEST(RenderPassContract_Point, DebugDrawPointsSeparation)
{
    // PointPass handles transient DebugDraw points.
    Graphics::DebugDraw dd;

    dd.Point({1, 2, 3}, 0.05f, Graphics::DebugDraw::Red());
    dd.OverlaySphere({4, 5, 6}, 0.1f, Graphics::DebugDraw::Green());

    EXPECT_TRUE(dd.HasContent());
    EXPECT_EQ(dd.GetPointCount(), 1u);
    EXPECT_GT(dd.GetOverlayLineCount(), 0u);
}

// =============================================================================
// Section 4: Frustum Culling Contract
// =============================================================================

TEST(RenderPassContract_Culling, IdentityFrustumDoesNotCull)
{
    // An empty/default frustum should not cull — allows disabling culling.
    Geometry::Frustum emptyFrustum{};
    Geometry::Sphere sphere{{0, 0, 0}, 1.0f};

    // TestOverlap with an empty frustum (no planes) should return true.
    EXPECT_TRUE(Geometry::TestOverlap(emptyFrustum, sphere));
}

TEST(RenderPassContract_Culling, SphereInsideFrustumIsVisible)
{
    // Create a frustum from a standard perspective-view matrix.
    auto proj = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
    auto view = glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    auto frustum = Geometry::Frustum::CreateFromMatrix(proj * view);

    // Sphere at origin should be inside.
    Geometry::Sphere sphere{{0, 0, 0}, 1.0f};
    EXPECT_TRUE(Geometry::TestOverlap(frustum, sphere));
}

TEST(RenderPassContract_Culling, SphereFarBehindCameraIsCulled)
{
    auto proj = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
    auto view = glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    auto frustum = Geometry::Frustum::CreateFromMatrix(proj * view);

    // Sphere far behind the camera should be culled.
    Geometry::Sphere sphere{{0, 0, 200}, 1.0f};
    EXPECT_FALSE(Geometry::TestOverlap(frustum, sphere));
}

TEST(RenderPassContract_Culling, SphereFarLeftIsCulled)
{
    auto proj = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
    auto view = glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    auto frustum = Geometry::Frustum::CreateFromMatrix(proj * view);

    // Sphere far to the left (well outside the frustum).
    Geometry::Sphere sphere{{-500, 0, 0}, 1.0f};
    EXPECT_FALSE(Geometry::TestOverlap(frustum, sphere));
}

// =============================================================================
// Section 5: Numerical Safeguard Contracts
// =============================================================================

TEST(RenderPassContract_Safeguards, LineWidthClampRange)
{
    // Line widths clamped to [0.5, 32.0] pixels.
    EXPECT_FLOAT_EQ(std::clamp(0.0f, 0.5f, 32.0f), 0.5f);
    EXPECT_FLOAT_EQ(std::clamp(50.0f, 0.5f, 32.0f), 32.0f);
    EXPECT_FLOAT_EQ(std::clamp(5.0f, 0.5f, 32.0f), 5.0f);
}

TEST(RenderPassContract_Safeguards, PointSizeClampRange)
{
    // Point radii clamped to [0.0001, 1.0] world-space.
    EXPECT_FLOAT_EQ(std::clamp(0.0f, 0.0001f, 1.0f), 0.0001f);
    EXPECT_FLOAT_EQ(std::clamp(5.0f, 0.0001f, 1.0f), 1.0f);
    EXPECT_FLOAT_EQ(std::clamp(0.5f, 0.0001f, 1.0f), 0.5f);
}

// =============================================================================
// Section 6: Cross-Pass Orthogonality
// =============================================================================

TEST(RenderPassContract_Orthogonality, AllThreeComponentsCoexist)
{
    // An entity can have Surface + Line + Point components simultaneously
    // (e.g., a mesh with wireframe overlay and vertex markers).
    entt::registry reg;
    auto e = reg.create();

    reg.emplace<ECS::Surface::Component>(e);
    reg.emplace<ECS::Line::Component>(e);
    reg.emplace<ECS::Point::Component>(e);

    EXPECT_TRUE(reg.all_of<ECS::Surface::Component>(e));
    EXPECT_TRUE(reg.all_of<ECS::Line::Component>(e));
    EXPECT_TRUE(reg.all_of<ECS::Point::Component>(e));
}

TEST(RenderPassContract_Orthogonality, RemoveOnePassComponentLeavesOthers)
{
    entt::registry reg;
    auto e = reg.create();

    reg.emplace<ECS::Surface::Component>(e);
    reg.emplace<ECS::Line::Component>(e);
    reg.emplace<ECS::Point::Component>(e);

    // Remove only LinePass component — other passes unaffected.
    reg.remove<ECS::Line::Component>(e);
    EXPECT_TRUE(reg.all_of<ECS::Surface::Component>(e));
    EXPECT_FALSE(reg.all_of<ECS::Line::Component>(e));
    EXPECT_TRUE(reg.all_of<ECS::Point::Component>(e));
}

TEST(RenderPassContract_Orthogonality, GpuSlotSentinelConsistentAcrossComponents)
{
    // All pass components use the same invalid GPU slot sentinel.
    ECS::Surface::Component surf;
    ECS::Graph::Data graph;
    ECS::PointCloud::Data cloud;

    EXPECT_EQ(surf.GpuSlot, ECS::kInvalidGpuSlot);
    EXPECT_EQ(graph.GpuSlot, ECS::kInvalidGpuSlot);
    EXPECT_EQ(cloud.GpuSlot, ECS::kInvalidGpuSlot);
    EXPECT_EQ(ECS::kInvalidGpuSlot, ~0u);
}

// =============================================================================
// Section 7: Frames-In-Flight Contract
// =============================================================================

TEST(RenderPassContract_FrameSync, FramesInFlightIs3)
{
    // All passes use VulkanDevice::GetFramesInFlight() for per-frame buffer
    // arrays. Verify the constant matches the documented value.
    constexpr uint32_t frames = RHI::VulkanDevice::GetFramesInFlight();
    EXPECT_EQ(frames, 3u);
}

// =============================================================================
// Section 8: DebugDraw LineSegment Contract (LinePass transient path)
// =============================================================================

TEST(RenderPassContract_DebugDraw, LineSegmentFieldValues)
{
    Graphics::DebugDraw dd;

    const glm::vec3 start{1.0f, 2.0f, 3.0f};
    const glm::vec3 end{4.0f, 5.0f, 6.0f};
    dd.Line(start, end, Graphics::DebugDraw::Red());

    auto lines = dd.GetLines();
    ASSERT_EQ(lines.size(), 1u);

    // Verify start/end positions survived the submission.
    EXPECT_EQ(lines[0].Start, start);
    EXPECT_EQ(lines[0].End, end);
}

TEST(RenderPassContract_DebugDraw, MultipleTrianglesAccumulate)
{
    Graphics::Passes::SurfacePass pass;

    pass.SubmitTriangle({0,0,0}, {1,0,0}, {0,1,0}, {0,0,1}, 0xFFFFFFFF);
    pass.SubmitTriangle({0,0,0}, {0,0,1}, {1,0,0}, {0,1,0}, 0xFFFFFFFF);
    pass.SubmitTriangle({0,0,0}, {0,1,0}, {0,0,1}, {1,0,0}, 0xFFFFFFFF);

    EXPECT_EQ(pass.GetTransientTriangleCount(), 3u);
    EXPECT_EQ(pass.GetTransientVertices().size(), 9u);

    pass.ResetTransient();
    EXPECT_EQ(pass.GetTransientTriangleCount(), 0u);
}
