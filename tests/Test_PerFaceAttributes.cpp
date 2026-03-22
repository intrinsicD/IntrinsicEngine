#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <limits>
#include <numeric>
#include <vector>

#include <glm/glm.hpp>

import Graphics;
import Geometry;
import RHI;

#include "TestMeshBuilders.h"

namespace
{
    namespace TestGpuColor
    {
        constexpr uint32_t PackColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) noexcept
        {
            return static_cast<uint32_t>(r)
                 | (static_cast<uint32_t>(g) << 8)
                 | (static_cast<uint32_t>(b) << 16)
                 | (static_cast<uint32_t>(a) << 24);
        }

        constexpr uint32_t PackColorF(float r, float g, float b, float a = 1.0f) noexcept
        {
            auto clamp = [](float v) noexcept -> uint8_t {
                if (!std::isfinite(v)) return 0;
                if (v <= 0.0f) return 0;
                if (v >= 1.0f) return 255;
                return static_cast<uint8_t>(v * 255.0f + 0.5f);
            };
            return PackColor(clamp(r), clamp(g), clamp(b), clamp(a));
        }

        constexpr uint32_t ScalarToHeatColor(float t, float a = 1.0f) noexcept
        {
            if (t <= 0.0f) t = 0.0f;
            if (t >= 1.0f) t = 1.0f;

            float r = 0.0f, g = 0.0f, b = 0.0f;
            if (t < 0.25f)
            {
                b = 1.0f;
                g = t * 4.0f;
            }
            else if (t < 0.5f)
            {
                g = 1.0f;
                b = 1.0f - (t - 0.25f) * 4.0f;
            }
            else if (t < 0.75f)
            {
                g = 1.0f;
                r = (t - 0.5f) * 4.0f;
            }
            else
            {
                r = 1.0f;
                g = 1.0f - (t - 0.75f) * 4.0f;
            }

            return PackColorF(r, g, b, a);
        }

        constexpr uint32_t LabelToColor(int label) noexcept
        {
            if (label < 0) return 0u;

            constexpr uint32_t kPalette[] = {
                PackColor(0x1F, 0x77, 0xB4),
                PackColor(0xFF, 0x7F, 0x0E),
                PackColor(0x2C, 0xA0, 0x2C),
                PackColor(0xD6, 0x27, 0x28),
                PackColor(0x94, 0x67, 0xBD),
                PackColor(0x8C, 0x56, 0x4B),
                PackColor(0xE3, 0x77, 0xC2),
                PackColor(0x7F, 0x7F, 0x7F),
                PackColor(0xBC, 0xBD, 0x22),
                PackColor(0x17, 0xBE, 0xCF),
                PackColor(0xAA, 0xFF, 0xAA),
                PackColor(0xFF, 0xBB, 0x78),
            };
            constexpr int kPaletteSize = static_cast<int>(sizeof(kPalette) / sizeof(kPalette[0]));
            return kPalette[label % kPaletteSize];
        }
    }
}

// =============================================================================
// Per-Face Attribute Pipeline Tests (TODO 1.7)
// =============================================================================
//
// These tests verify the end-to-end per-face attribute rendering pipeline
// from PropertySet face data through GPU color packing to SurfacePass
// integration. Covers three visualization use cases:
//
//   1. Flat-shading per-face colors (from "f:color" PropertySet).
//   2. Curvature/scalar visualization (ScalarToHeatColor colormap).
//   3. Segmentation labels (LabelToColor categorical palette).
//
// No GPU device is needed — these are pure CPU-side contract tests.

// =============================================================================
// Helpers
// =============================================================================

// Use shared builders from TestMeshBuilders.h where possible.
// MakeTriangle forwards to the shared equivalent.
// MakeTetrahedron uses different vertex coordinates than the shared version
// (approximate regular tetrahedron vs exact), so it remains local.
static Geometry::Halfedge::Mesh MakeTriangle() { return MakeRightTriangle(); }

static Geometry::Halfedge::Mesh MakeLocalTetrahedron()
{
    using namespace Geometry;
    Halfedge::Mesh mesh;
    auto v0 = mesh.AddVertex(glm::vec3(0, 0, 0));
    auto v1 = mesh.AddVertex(glm::vec3(1, 0, 0));
    auto v2 = mesh.AddVertex(glm::vec3(0.5f, 0, 0.866f));
    auto v3 = mesh.AddVertex(glm::vec3(0.5f, 0.816f, 0.289f));
    (void)mesh.AddTriangle(v0, v1, v2);
    (void)mesh.AddTriangle(v0, v2, v3);
    (void)mesh.AddTriangle(v0, v3, v1);
    (void)mesh.AddTriangle(v1, v3, v2);
    return mesh;
}

// Extract per-face colors from a Halfedge::Mesh into packed ABGR uint32_t
// array suitable for Surface::Component::CachedFaceColors.
// Returns empty vector if "f:color" property doesn't exist.
static std::vector<uint32_t> ExtractFaceColorsFromMesh(const Geometry::Halfedge::Mesh& mesh)
{
    if (!mesh.FaceProperties().Exists("f:color"))
        return {};

    auto colorProp = Geometry::FaceProperty<glm::vec4>(
        mesh.FaceProperties().Get<glm::vec4>("f:color"));
    if (!colorProp.IsValid())
        return {};

    std::vector<uint32_t> packed;
    packed.reserve(mesh.FacesSize());

    for (std::size_t i = 0; i < mesh.FacesSize(); ++i)
    {
        const Geometry::FaceHandle f{static_cast<Geometry::PropertyIndex>(i)};
        if (mesh.IsDeleted(f))
            continue;

        const glm::vec4& c = colorProp[f];
        packed.push_back(TestGpuColor::PackColorF(c.r, c.g, c.b, c.a));
    }

    return packed;
}

// =============================================================================
// Section 1: Flat-Shading Per-Face Colors (PropertySet → CachedFaceColors)
// =============================================================================

TEST(PerFaceAttr_FlatShading, ExtractFromPropertySet_SingleTriangle)
{
    using namespace Geometry;
    auto mesh = MakeTriangle();

    // Add per-face color property.
    auto color = FaceProperty<glm::vec4>(
        mesh.FaceProperties().GetOrAdd<glm::vec4>("f:color", glm::vec4(1.0f)));
    ASSERT_TRUE(color.IsValid());

    // Set face 0 to red.
    color[FaceHandle{0}] = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);

    // Extract and verify.
    auto packed = ExtractFaceColorsFromMesh(mesh);
    ASSERT_EQ(packed.size(), 1u);

    uint32_t expected = TestGpuColor::PackColorF(1.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_EQ(packed[0], expected);
}

TEST(PerFaceAttr_FlatShading, ExtractFromPropertySet_MultipleFaces)
{
    using namespace Geometry;
    auto mesh = MakeLocalTetrahedron();

    auto color = FaceProperty<glm::vec4>(
        mesh.FaceProperties().GetOrAdd<glm::vec4>("f:color", glm::vec4(1.0f)));
    ASSERT_TRUE(color.IsValid());

    // Assign distinct colors to each face.
    color[FaceHandle{0}] = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f); // red
    color[FaceHandle{1}] = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f); // green
    color[FaceHandle{2}] = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f); // blue
    color[FaceHandle{3}] = glm::vec4(1.0f, 1.0f, 0.0f, 1.0f); // yellow

    auto packed = ExtractFaceColorsFromMesh(mesh);
    ASSERT_EQ(packed.size(), 4u);

    EXPECT_EQ(packed[0], TestGpuColor::PackColorF(1, 0, 0, 1));
    EXPECT_EQ(packed[1], TestGpuColor::PackColorF(0, 1, 0, 1));
    EXPECT_EQ(packed[2], TestGpuColor::PackColorF(0, 0, 1, 1));
    EXPECT_EQ(packed[3], TestGpuColor::PackColorF(1, 1, 0, 1));
}

TEST(PerFaceAttr_FlatShading, NoPropertyReturnsEmpty)
{
    auto mesh = MakeTriangle();

    // No "f:color" property set — extraction should return empty.
    auto packed = ExtractFaceColorsFromMesh(mesh);
    EXPECT_TRUE(packed.empty());
}

TEST(PerFaceAttr_FlatShading, EmptyMeshReturnsEmpty)
{
    Geometry::Halfedge::Mesh mesh;
    auto packed = ExtractFaceColorsFromMesh(mesh);
    EXPECT_TRUE(packed.empty());
}

TEST(PerFaceAttr_FlatShading, PopulateSurfaceComponent)
{
    using namespace Geometry;
    auto mesh = MakeQuadPair();

    auto color = FaceProperty<glm::vec4>(
        mesh.FaceProperties().GetOrAdd<glm::vec4>("f:color", glm::vec4(1.0f)));
    ASSERT_TRUE(color.IsValid());

    color[FaceHandle{0}] = glm::vec4(0.5f, 0.3f, 0.8f, 1.0f);
    color[FaceHandle{1}] = glm::vec4(0.1f, 0.9f, 0.2f, 1.0f);

    // Simulate the lifecycle system populating CachedFaceColors.
    ECS::Surface::Component surf;
    surf.CachedFaceColors = ExtractFaceColorsFromMesh(mesh);
    surf.FaceColorsDirty = false;

    ASSERT_EQ(surf.CachedFaceColors.size(), 2u);

    // Verify colors match the PropertySet values.
    EXPECT_EQ(surf.CachedFaceColors[0], TestGpuColor::PackColorF(0.5f, 0.3f, 0.8f, 1.0f));
    EXPECT_EQ(surf.CachedFaceColors[1], TestGpuColor::PackColorF(0.1f, 0.9f, 0.2f, 1.0f));
}

TEST(PerFaceAttr_FlatShading, TransparencyPreserved)
{
    using namespace Geometry;
    auto mesh = MakeTriangle();

    auto color = FaceProperty<glm::vec4>(
        mesh.FaceProperties().GetOrAdd<glm::vec4>("f:color", glm::vec4(1.0f)));
    ASSERT_TRUE(color.IsValid());

    // Semi-transparent face.
    color[FaceHandle{0}] = glm::vec4(1.0f, 0.0f, 0.0f, 0.5f);

    auto packed = ExtractFaceColorsFromMesh(mesh);
    ASSERT_EQ(packed.size(), 1u);

    // Verify alpha channel is preserved.
    uint8_t a = (packed[0] >> 24) & 0xFF;
    EXPECT_NEAR(a, 128, 1); // 0.5 * 255 + 0.5 ≈ 128
}

// =============================================================================
// Section 2: Curvature / Scalar Visualization (ScalarToHeatColor)
// =============================================================================

TEST(PerFaceAttr_Curvature, ScalarToHeatColor_ZeroIsBlue)
{
    uint32_t c = TestGpuColor::ScalarToHeatColor(0.0f);

    // At t=0: pure blue (R=0, G=0, B=1).
    uint8_t r = (c >> 0) & 0xFF;
    uint8_t g = (c >> 8) & 0xFF;
    uint8_t b = (c >> 16) & 0xFF;

    EXPECT_EQ(r, 0u);
    EXPECT_EQ(g, 0u);
    EXPECT_EQ(b, 255u);
}

TEST(PerFaceAttr_Curvature, ScalarToHeatColor_OneIsRed)
{
    uint32_t c = TestGpuColor::ScalarToHeatColor(1.0f);

    // At t=1: pure red (R=1, G=0, B=0).
    uint8_t r = (c >> 0) & 0xFF;
    uint8_t g = (c >> 8) & 0xFF;
    uint8_t b = (c >> 16) & 0xFF;

    EXPECT_EQ(r, 255u);
    EXPECT_EQ(g, 0u);
    EXPECT_EQ(b, 0u);
}

TEST(PerFaceAttr_Curvature, ScalarToHeatColor_MidIsGreen)
{
    uint32_t c = TestGpuColor::ScalarToHeatColor(0.5f);

    // At t=0.5: green (R=0, G=1, B=0).
    uint8_t r = (c >> 0) & 0xFF;
    uint8_t g = (c >> 8) & 0xFF;
    uint8_t b = (c >> 16) & 0xFF;

    EXPECT_EQ(r, 0u);
    EXPECT_EQ(g, 255u);
    EXPECT_EQ(b, 0u);
}

TEST(PerFaceAttr_Curvature, ScalarToHeatColor_Monotonic)
{
    // Red channel should be monotonically non-decreasing across [0,1].
    uint8_t prevR = 0;
    for (int i = 0; i <= 100; ++i)
    {
        float t = static_cast<float>(i) / 100.0f;
        uint32_t c = TestGpuColor::ScalarToHeatColor(t);
        uint8_t r = (c >> 0) & 0xFF;
        EXPECT_GE(r, prevR) << "Red channel decreased at t=" << t;
        prevR = r;
    }
}

TEST(PerFaceAttr_Curvature, ScalarToHeatColor_ClampsOutOfRange)
{
    // Negative values should clamp to blue (same as 0).
    uint32_t neg = TestGpuColor::ScalarToHeatColor(-1.0f);
    uint32_t zero = TestGpuColor::ScalarToHeatColor(0.0f);
    EXPECT_EQ(neg, zero);

    // Values > 1 should clamp to red (same as 1).
    uint32_t over = TestGpuColor::ScalarToHeatColor(5.0f);
    uint32_t one = TestGpuColor::ScalarToHeatColor(1.0f);
    EXPECT_EQ(over, one);
}

TEST(PerFaceAttr_Curvature, ScalarToHeatColor_AlphaDefault)
{
    // Default alpha should be 1.0 (opaque).
    uint32_t c = TestGpuColor::ScalarToHeatColor(0.5f);
    uint8_t a = (c >> 24) & 0xFF;
    EXPECT_EQ(a, 255u);
}

TEST(PerFaceAttr_Curvature, ScalarToHeatColor_CustomAlpha)
{
    uint32_t c = TestGpuColor::ScalarToHeatColor(0.5f, 0.5f);
    uint8_t a = (c >> 24) & 0xFF;
    EXPECT_NEAR(a, 128, 1);
}

TEST(PerFaceAttr_Curvature, CurvatureVisualization_PerFaceScalar)
{
    // Simulate curvature visualization: per-face scalar values mapped to
    // heat colormap and stored as CachedFaceColors.
    auto mesh = MakeLocalTetrahedron();

    // Simulate per-face curvature values.
    std::vector<float> curvature = {0.0f, 0.33f, 0.66f, 1.0f};
    ASSERT_EQ(curvature.size(), mesh.FaceCount());

    // Map curvature to packed colors.
    ECS::Surface::Component surf;
    surf.CachedFaceColors.reserve(curvature.size());
    for (float k : curvature)
        surf.CachedFaceColors.push_back(TestGpuColor::ScalarToHeatColor(k));
    surf.FaceColorsDirty = false;

    ASSERT_EQ(surf.CachedFaceColors.size(), 4u);

    // Face 0 (k=0) should be blue.
    uint8_t r0 = (surf.CachedFaceColors[0] >> 0) & 0xFF;
    uint8_t b0 = (surf.CachedFaceColors[0] >> 16) & 0xFF;
    EXPECT_EQ(r0, 0u);
    EXPECT_EQ(b0, 255u);

    // Face 3 (k=1) should be red.
    uint8_t r3 = (surf.CachedFaceColors[3] >> 0) & 0xFF;
    uint8_t b3 = (surf.CachedFaceColors[3] >> 16) & 0xFF;
    EXPECT_EQ(r3, 255u);
    EXPECT_EQ(b3, 0u);
}

TEST(PerFaceAttr_Curvature, CurvatureFromPropertySet)
{
    using namespace Geometry;
    auto mesh = MakeQuadPair();

    // Add per-face scalar curvature property.
    auto curvProp = FaceProperty<float>(
        mesh.FaceProperties().GetOrAdd<float>("f:curvature", 0.0f));
    ASSERT_TRUE(curvProp.IsValid());

    curvProp[FaceHandle{0}] = 0.2f; // low curvature
    curvProp[FaceHandle{1}] = 0.8f; // high curvature

    // Extract curvature values from PropertySet and map to colors.
    ECS::Surface::Component surf;
    for (std::size_t i = 0; i < mesh.FacesSize(); ++i)
    {
        FaceHandle f{static_cast<PropertyIndex>(i)};
        if (!mesh.IsDeleted(f))
        {
            float k = curvProp[f];
            surf.CachedFaceColors.push_back(TestGpuColor::ScalarToHeatColor(k));
        }
    }

    ASSERT_EQ(surf.CachedFaceColors.size(), 2u);

    // Low curvature face (0.2) should have more blue than red.
    uint8_t r_low = (surf.CachedFaceColors[0] >> 0) & 0xFF;
    uint8_t b_low = (surf.CachedFaceColors[0] >> 16) & 0xFF;
    EXPECT_LT(r_low, b_low);

    // High curvature face (0.8) should have more red than blue.
    uint8_t r_high = (surf.CachedFaceColors[1] >> 0) & 0xFF;
    uint8_t b_high = (surf.CachedFaceColors[1] >> 16) & 0xFF;
    EXPECT_GT(r_high, b_high);
}

// =============================================================================
// Section 3: Segmentation Labels (LabelToColor)
// =============================================================================

TEST(PerFaceAttr_Segmentation, LabelToColor_DistinctForFirstTwelve)
{
    // The palette has 12 entries; labels 0–11 should produce 12 distinct colors.
    std::vector<uint32_t> colors;
    for (int i = 0; i < 12; ++i)
        colors.push_back(TestGpuColor::LabelToColor(i));

    // All should be distinct.
    for (std::size_t i = 0; i < colors.size(); ++i)
        for (std::size_t j = i + 1; j < colors.size(); ++j)
            EXPECT_NE(colors[i], colors[j])
                << "Labels " << i << " and " << j << " produced the same color";
}

TEST(PerFaceAttr_Segmentation, LabelToColor_WrapsAfterPaletteSize)
{
    // Labels beyond palette size wrap around.
    uint32_t c0 = TestGpuColor::LabelToColor(0);
    uint32_t c12 = TestGpuColor::LabelToColor(12);
    EXPECT_EQ(c0, c12);

    uint32_t c1 = TestGpuColor::LabelToColor(1);
    uint32_t c13 = TestGpuColor::LabelToColor(13);
    EXPECT_EQ(c1, c13);
}

TEST(PerFaceAttr_Segmentation, LabelToColor_NegativeLabelReturnsBlack)
{
    uint32_t c = TestGpuColor::LabelToColor(-1);
    EXPECT_EQ(c, 0u); // transparent black
}

TEST(PerFaceAttr_Segmentation, LabelToColor_AllOpaque)
{
    // All palette colors should have alpha = 255 (fully opaque).
    for (int i = 0; i < 12; ++i)
    {
        uint32_t c = TestGpuColor::LabelToColor(i);
        uint8_t a = (c >> 24) & 0xFF;
        EXPECT_EQ(a, 0xFFu) << "Label " << i << " has non-opaque alpha";
    }
}

TEST(PerFaceAttr_Segmentation, SegmentationVisualization_PerFaceLabels)
{
    using namespace Geometry;
    auto mesh = MakeLocalTetrahedron();

    // Add per-face label property.
    auto labelProp = FaceProperty<int>(
        mesh.FaceProperties().GetOrAdd<int>("f:segment", -1));
    ASSERT_TRUE(labelProp.IsValid());

    // Assign labels: two faces in segment 0, one in segment 1, one in segment 2.
    labelProp[FaceHandle{0}] = 0;
    labelProp[FaceHandle{1}] = 0;
    labelProp[FaceHandle{2}] = 1;
    labelProp[FaceHandle{3}] = 2;

    // Extract segmentation labels from PropertySet and map to colors.
    ECS::Surface::Component surf;
    for (std::size_t i = 0; i < mesh.FacesSize(); ++i)
    {
        FaceHandle f{static_cast<PropertyIndex>(i)};
        if (!mesh.IsDeleted(f))
            surf.CachedFaceColors.push_back(
                TestGpuColor::LabelToColor(labelProp[f]));
    }

    ASSERT_EQ(surf.CachedFaceColors.size(), 4u);

    // Faces 0 and 1 are in the same segment → same color.
    EXPECT_EQ(surf.CachedFaceColors[0], surf.CachedFaceColors[1]);

    // Different segments → different colors.
    EXPECT_NE(surf.CachedFaceColors[0], surf.CachedFaceColors[2]);
    EXPECT_NE(surf.CachedFaceColors[0], surf.CachedFaceColors[3]);
    EXPECT_NE(surf.CachedFaceColors[2], surf.CachedFaceColors[3]);
}

// =============================================================================
// Section 4: SurfacePass Integration Contract
// =============================================================================

TEST(PerFaceAttr_Integration, PushConstantsSizeMatchesCurrentLayout)
{
    // MeshPushConstants includes per-face, per-vertex, and index buffer BDA pointers.
    EXPECT_EQ(sizeof(RHI::MeshPushConstants), 128u);
    EXPECT_LE(sizeof(RHI::MeshPushConstants), 128u);
}

TEST(PerFaceAttr_Integration, PushConstantsPtrFaceAttrOffset)
{
    // PtrFaceAttr must be at offset 96 within MeshPushConstants.
    // The struct layout is:
    //   mat4 Model              (64 bytes, offset 0)
    //   uint64_t PtrPositions   (8 bytes, offset 64)
    //   uint64_t PtrNormals     (8 bytes, offset 72)
    //   uint64_t PtrAux         (8 bytes, offset 80)
    //   uint32_t VisibilityBase (4 bytes, offset 88)
    //   float    PointSizePx    (4 bytes, offset 92)
    //   uint64_t PtrFaceAttr    (8 bytes, offset 96)
    //   uint64_t PtrVertexAttr  (8 bytes, offset 104)
    //   uint64_t PtrIndices     (8 bytes, offset 112)
    //   uint64_t PtrCentroids   (8 bytes, offset 120)
    //   Total: 128 bytes
    EXPECT_EQ(offsetof(RHI::MeshPushConstants, PtrFaceAttr), 96u);
    EXPECT_EQ(offsetof(RHI::MeshPushConstants, PtrVertexAttr), 104u);
    EXPECT_EQ(offsetof(RHI::MeshPushConstants, PtrIndices), 112u);
    EXPECT_EQ(offsetof(RHI::MeshPushConstants, PtrCentroids), 120u);
}

TEST(PerFaceAttr_Integration, PushConstantsPtrFaceAttrDefaultZero)
{
    // Default-constructed push constants must have PtrFaceAttr = 0
    // so that the shader falls back to standard texture shading.
    RHI::MeshPushConstants pc{};
    EXPECT_EQ(pc.PtrFaceAttr, 0u);
}

TEST(PerFaceAttr_Integration, PushConstantsPtrFaceAttrCanHoldBDA)
{
    // Verify that PtrFaceAttr can store a full 64-bit BDA address.
    RHI::MeshPushConstants pc{};
    pc.PtrFaceAttr = 0xDEAD'BEEF'1234'5678ull;
    EXPECT_EQ(pc.PtrFaceAttr, 0xDEAD'BEEF'1234'5678ull);
}

TEST(PerFaceAttr_Integration, FaceColorCountMatchesFaceCount)
{
    using namespace Geometry;
    auto mesh = MakeLocalTetrahedron();

    auto color = FaceProperty<glm::vec4>(
        mesh.FaceProperties().GetOrAdd<glm::vec4>("f:color", glm::vec4(1.0f)));
    ASSERT_TRUE(color.IsValid());

    for (std::size_t i = 0; i < mesh.FacesSize(); ++i)
    {
        FaceHandle f{static_cast<PropertyIndex>(i)};
        color[f] = glm::vec4(static_cast<float>(i) / 3.0f, 0.5f, 0.5f, 1.0f);
    }

    auto packed = ExtractFaceColorsFromMesh(mesh);

    // CachedFaceColors count must match the mesh face count exactly.
    // SurfacePass indexes by gl_PrimitiveID, so the buffer must have
    // exactly one entry per rendered triangle.
    EXPECT_EQ(packed.size(), mesh.FaceCount());
}

// =============================================================================
// Section 5: Edge Cases and Robustness
// =============================================================================

TEST(PerFaceAttr_Robustness, PackColorF_NaNClampedToZero)
{
    // NaN should be treated as 0 by the clamp lambda.
    float nan = std::numeric_limits<float>::quiet_NaN();
    uint32_t c = TestGpuColor::PackColorF(nan, nan, nan, 1.0f);
    uint8_t r = (c >> 0) & 0xFF;
    uint8_t g = (c >> 8) & 0xFF;
    uint8_t b = (c >> 16) & 0xFF;
    EXPECT_EQ(r, 0u);
    EXPECT_EQ(g, 0u);
    EXPECT_EQ(b, 0u);
    uint8_t a = (c >> 24) & 0xFF;
    EXPECT_EQ(a, 255u);
}

TEST(PerFaceAttr_Robustness, PackColorF_InfClampedDeterministically)
{
    float inf = std::numeric_limits<float>::infinity();
    uint32_t c = TestGpuColor::PackColorF(inf, -inf, 0.5f, 1.0f);

    EXPECT_EQ((c >> 0) & 0xFF, 0u);
    EXPECT_EQ((c >> 8) & 0xFF, 0u);
    EXPECT_EQ((c >> 16) & 0xFF, 128u);
    EXPECT_EQ((c >> 24) & 0xFF, 255u);
}

TEST(PerFaceAttr_Robustness, DeletedFacesSkipped)
{
    using namespace Geometry;
    Halfedge::Mesh mesh;
    auto v0 = mesh.AddVertex(glm::vec3(0, 0, 0));
    auto v1 = mesh.AddVertex(glm::vec3(1, 0, 0));
    auto v2 = mesh.AddVertex(glm::vec3(0, 1, 0));
    auto v3 = mesh.AddVertex(glm::vec3(1, 1, 0));
    (void)mesh.AddTriangle(v0, v1, v2);
    (void)mesh.AddTriangle(v2, v1, v3);

    auto color = FaceProperty<glm::vec4>(
        mesh.FaceProperties().GetOrAdd<glm::vec4>("f:color", glm::vec4(1.0f)));
    ASSERT_TRUE(color.IsValid());
    color[FaceHandle{0}] = glm::vec4(1, 0, 0, 1);
    color[FaceHandle{1}] = glm::vec4(0, 1, 0, 1);

    // Delete one face.
    mesh.DeleteFace(FaceHandle{0});

    // Extraction should skip the deleted face.
    auto packed = ExtractFaceColorsFromMesh(mesh);
    EXPECT_EQ(packed.size(), mesh.FaceCount());
}

TEST(PerFaceAttr_Robustness, GarbageCollectedMeshStillWorks)
{
    using namespace Geometry;
    Halfedge::Mesh mesh;
    auto v0 = mesh.AddVertex(glm::vec3(0, 0, 0));
    auto v1 = mesh.AddVertex(glm::vec3(1, 0, 0));
    auto v2 = mesh.AddVertex(glm::vec3(0, 1, 0));
    auto v3 = mesh.AddVertex(glm::vec3(1, 1, 0));
    auto v4 = mesh.AddVertex(glm::vec3(0.5f, 0.5f, 0));
    (void)mesh.AddTriangle(v0, v1, v2);
    (void)mesh.AddTriangle(v2, v1, v3);
    (void)mesh.AddTriangle(v3, v1, v4);

    auto color = FaceProperty<glm::vec4>(
        mesh.FaceProperties().GetOrAdd<glm::vec4>("f:color", glm::vec4(1.0f)));
    ASSERT_TRUE(color.IsValid());
    color[FaceHandle{0}] = glm::vec4(1, 0, 0, 1);
    color[FaceHandle{1}] = glm::vec4(0, 1, 0, 1);
    color[FaceHandle{2}] = glm::vec4(0, 0, 1, 1);

    // Delete middle face and run GC.
    mesh.DeleteFace(FaceHandle{1});
    mesh.GarbageCollection();

    // After GC, face indices are compacted. The extraction should
    // produce exactly FaceCount() colors.
    // Re-fetch the property after GC (handles may have shifted).
    auto colorGC = FaceProperty<glm::vec4>(
        mesh.FaceProperties().Get<glm::vec4>("f:color"));
    ASSERT_TRUE(colorGC.IsValid());

    auto packed = ExtractFaceColorsFromMesh(mesh);
    EXPECT_EQ(packed.size(), mesh.FaceCount());
}

TEST(PerFaceAttr_Robustness, LargeMeshScaling)
{
    using namespace Geometry;
    Halfedge::Mesh mesh;

    // Build a grid mesh with many faces.
    constexpr int N = 20; // 20x20 grid → 800 triangles
    std::vector<VertexHandle> verts;
    verts.reserve((N + 1) * (N + 1));

    for (int y = 0; y <= N; ++y)
        for (int x = 0; x <= N; ++x)
            verts.push_back(mesh.AddVertex(glm::vec3(
                static_cast<float>(x), static_cast<float>(y), 0.0f)));

    for (int y = 0; y < N; ++y)
    {
        for (int x = 0; x < N; ++x)
        {
            int i00 = y * (N + 1) + x;
            int i10 = y * (N + 1) + x + 1;
            int i01 = (y + 1) * (N + 1) + x;
            int i11 = (y + 1) * (N + 1) + x + 1;

            (void)mesh.AddTriangle(verts[i00], verts[i10], verts[i01]);
            (void)mesh.AddTriangle(verts[i01], verts[i10], verts[i11]);
        }
    }

    const std::size_t expectedFaces = static_cast<std::size_t>(N * N * 2);
    ASSERT_EQ(mesh.FaceCount(), expectedFaces);

    // Populate face colors with curvature-like values.
    auto curvProp = FaceProperty<float>(
        mesh.FaceProperties().GetOrAdd<float>("f:curvature", 0.0f));
    ASSERT_TRUE(curvProp.IsValid());

    for (std::size_t i = 0; i < mesh.FacesSize(); ++i)
    {
        FaceHandle f{static_cast<PropertyIndex>(i)};
        if (!mesh.IsDeleted(f))
            curvProp[f] = static_cast<float>(i) / static_cast<float>(expectedFaces);
    }

    // Convert to face colors.
    ECS::Surface::Component surf;
    for (std::size_t i = 0; i < mesh.FacesSize(); ++i)
    {
        FaceHandle f{static_cast<PropertyIndex>(i)};
        if (!mesh.IsDeleted(f))
            surf.CachedFaceColors.push_back(
                TestGpuColor::ScalarToHeatColor(curvProp[f]));
    }

    EXPECT_EQ(surf.CachedFaceColors.size(), expectedFaces);
}
