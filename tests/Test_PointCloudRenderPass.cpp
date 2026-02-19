#include <gtest/gtest.h>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

import Graphics;
import Geometry;

// =============================================================================
// PointCloudRenderPass — Compile-time contract tests
// =============================================================================
//
// These tests validate the CPU-side contract of PointCloudRenderPass without
// requiring a GPU device. They verify:
//   - GpuPointData layout and alignment (GPU SSBO compatibility).
//   - Point packing and color packing correctness.
//   - Staging buffer accumulation (SubmitPoints / ResetPoints / HasContent).
//   - PointCloudRenderer ECS component correctness.

// ---- GpuPointData Layout Tests ----

TEST(PointCloudRenderPass_Contract, GpuPointDataIs32Bytes)
{
    // GPU SSBO alignment: 32 bytes = 2 x vec4.
    EXPECT_EQ(sizeof(Graphics::Passes::PointCloudRenderPass::GpuPointData), 32u);
}

TEST(PointCloudRenderPass_Contract, GpuPointDataAlignedTo16)
{
    EXPECT_EQ(alignof(Graphics::Passes::PointCloudRenderPass::GpuPointData), 16u);
}

// ---- Color Packing Tests ----

TEST(PointCloudRenderPass_Contract, PackColorRed)
{
    uint32_t red = Graphics::Passes::PointCloudRenderPass::PackColor(255, 0, 0, 255);
    EXPECT_EQ(red & 0xFF, 255u);         // R
    EXPECT_EQ((red >> 8) & 0xFF, 0u);    // G
    EXPECT_EQ((red >> 16) & 0xFF, 0u);   // B
    EXPECT_EQ((red >> 24) & 0xFF, 255u); // A
}

TEST(PointCloudRenderPass_Contract, PackColorGreen)
{
    uint32_t green = Graphics::Passes::PointCloudRenderPass::PackColor(0, 255, 0, 255);
    EXPECT_EQ(green & 0xFF, 0u);
    EXPECT_EQ((green >> 8) & 0xFF, 255u);
    EXPECT_EQ((green >> 16) & 0xFF, 0u);
    EXPECT_EQ((green >> 24) & 0xFF, 255u);
}

TEST(PointCloudRenderPass_Contract, PackColorBlue)
{
    uint32_t blue = Graphics::Passes::PointCloudRenderPass::PackColor(0, 0, 255, 255);
    EXPECT_EQ(blue & 0xFF, 0u);
    EXPECT_EQ((blue >> 8) & 0xFF, 0u);
    EXPECT_EQ((blue >> 16) & 0xFF, 255u);
    EXPECT_EQ((blue >> 24) & 0xFF, 255u);
}

TEST(PointCloudRenderPass_Contract, PackColorF_White)
{
    uint32_t white = Graphics::Passes::PointCloudRenderPass::PackColorF(1.0f, 1.0f, 1.0f, 1.0f);
    EXPECT_EQ(white & 0xFF, 255u);
    EXPECT_EQ((white >> 8) & 0xFF, 255u);
    EXPECT_EQ((white >> 16) & 0xFF, 255u);
    EXPECT_EQ((white >> 24) & 0xFF, 255u);
}

TEST(PointCloudRenderPass_Contract, PackColorF_Clamps)
{
    // Out-of-range values should be clamped.
    uint32_t c = Graphics::Passes::PointCloudRenderPass::PackColorF(-1.0f, 2.0f, 0.5f);
    EXPECT_EQ(c & 0xFF, 0u);             // Clamped to 0
    EXPECT_EQ((c >> 8) & 0xFF, 255u);    // Clamped to 255
    uint8_t b = static_cast<uint8_t>((c >> 16) & 0xFF);
    EXPECT_NEAR(b, 128, 1);              // ~0.5 * 255
}

// ---- PackPoint Tests ----

TEST(PointCloudRenderPass_Contract, PackPointValues)
{
    auto pt = Graphics::Passes::PointCloudRenderPass::PackPoint(
        1.0f, 2.0f, 3.0f,   // position
        0.0f, 1.0f, 0.0f,   // normal
        0.01f,               // size
        0xFFFF0000);         // color

    EXPECT_FLOAT_EQ(pt.PosX, 1.0f);
    EXPECT_FLOAT_EQ(pt.PosY, 2.0f);
    EXPECT_FLOAT_EQ(pt.PosZ, 3.0f);
    EXPECT_FLOAT_EQ(pt.Size, 0.01f);
    EXPECT_FLOAT_EQ(pt.NormX, 0.0f);
    EXPECT_FLOAT_EQ(pt.NormY, 1.0f);
    EXPECT_FLOAT_EQ(pt.NormZ, 0.0f);
    EXPECT_EQ(pt.Color, 0xFFFF0000u);
}

// ---- Staging Buffer Tests ----

TEST(PointCloudRenderPass_Contract, InitiallyEmpty)
{
    Graphics::Passes::PointCloudRenderPass pass;
    EXPECT_FALSE(pass.HasContent());
    EXPECT_EQ(pass.GetPointCount(), 0u);
}

TEST(PointCloudRenderPass_Contract, SubmitAndCount)
{
    Graphics::Passes::PointCloudRenderPass pass;

    auto pt = Graphics::Passes::PointCloudRenderPass::PackPoint(
        0, 0, 0, 0, 1, 0, 0.01f, 0xFFFFFFFF);
    pass.SubmitPoints(&pt, 1);

    EXPECT_TRUE(pass.HasContent());
    EXPECT_EQ(pass.GetPointCount(), 1u);
}

TEST(PointCloudRenderPass_Contract, SubmitBatch)
{
    Graphics::Passes::PointCloudRenderPass pass;

    std::vector<Graphics::Passes::PointCloudRenderPass::GpuPointData> points(100);
    for (uint32_t i = 0; i < 100; ++i)
    {
        points[i] = Graphics::Passes::PointCloudRenderPass::PackPoint(
            static_cast<float>(i), 0, 0, 0, 1, 0, 0.01f, 0xFFFFFFFF);
    }

    pass.SubmitPoints(points.data(), 100);
    EXPECT_EQ(pass.GetPointCount(), 100u);
}

TEST(PointCloudRenderPass_Contract, ResetClearsPoints)
{
    Graphics::Passes::PointCloudRenderPass pass;

    auto pt = Graphics::Passes::PointCloudRenderPass::PackPoint(
        0, 0, 0, 0, 1, 0, 0.01f, 0xFFFFFFFF);
    pass.SubmitPoints(&pt, 1);
    EXPECT_TRUE(pass.HasContent());

    pass.ResetPoints();
    EXPECT_FALSE(pass.HasContent());
    EXPECT_EQ(pass.GetPointCount(), 0u);
}

TEST(PointCloudRenderPass_Contract, MultipleSubmitsAccumulate)
{
    Graphics::Passes::PointCloudRenderPass pass;

    auto pt = Graphics::Passes::PointCloudRenderPass::PackPoint(
        0, 0, 0, 0, 1, 0, 0.01f, 0xFFFFFFFF);
    pass.SubmitPoints(&pt, 1);
    pass.SubmitPoints(&pt, 1);
    pass.SubmitPoints(&pt, 1);

    EXPECT_EQ(pass.GetPointCount(), 3u);
}

// ---- Configuration Defaults ----

TEST(PointCloudRenderPass_Contract, DefaultConfiguration)
{
    Graphics::Passes::PointCloudRenderPass pass;
    EXPECT_FLOAT_EQ(pass.SizeMultiplier, 1.0f);
    EXPECT_EQ(pass.RenderMode, 0u); // Flat disc by default
}

// ---- ECS Component Tests ----

TEST(PointCloudRenderer_Component, DefaultValues)
{
    ECS::PointCloudRenderer::Component comp;
    EXPECT_EQ(comp.PointCount(), 0u);
    EXPECT_FALSE(comp.HasNormals());
    EXPECT_FALSE(comp.HasColors());
    EXPECT_FALSE(comp.HasRadii());
    EXPECT_EQ(comp.RenderMode, 0u);
    EXPECT_FLOAT_EQ(comp.DefaultRadius, 0.005f);
    EXPECT_FLOAT_EQ(comp.SizeMultiplier, 1.0f);
    EXPECT_TRUE(comp.Visible);
}

TEST(PointCloudRenderer_Component, WithData)
{
    ECS::PointCloudRenderer::Component comp;
    comp.Positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    comp.Normals = {{0, 1, 0}, {0, 1, 0}, {0, 0, 1}};
    comp.Colors = {{1, 0, 0, 1}, {0, 1, 0, 1}, {0, 0, 1, 1}};
    comp.Radii = {0.01f, 0.02f, 0.03f};
    comp.RenderMode = 2; // EWA

    EXPECT_EQ(comp.PointCount(), 3u);
    EXPECT_TRUE(comp.HasNormals());
    EXPECT_TRUE(comp.HasColors());
    EXPECT_TRUE(comp.HasRadii());
}

TEST(PointCloudRenderer_Component, MismatchedDataDetected)
{
    ECS::PointCloudRenderer::Component comp;
    comp.Positions = {{0, 0, 0}, {1, 0, 0}};
    comp.Normals = {{0, 1, 0}}; // Wrong count

    EXPECT_FALSE(comp.HasNormals()); // Size mismatch
}

// ---- GaussianSplat Mode (mode 3) ----

TEST(PointCloudRenderPass_Contract, GaussianSplatModeSubmitAndCount)
{
    Graphics::Passes::PointCloudRenderPass pass;

    auto pt = Graphics::Passes::PointCloudRenderPass::PackPoint(
        1.0f, 2.0f, 3.0f, 0.0f, 1.0f, 0.0f, 0.05f, 0xFFFFFFFF);
    pass.SubmitPoints(Geometry::PointCloud::RenderMode::GaussianSplat, &pt, 1);

    EXPECT_TRUE(pass.HasContent());
    EXPECT_EQ(pass.GetPointCount(), 1u);
}

TEST(PointCloudRenderPass_Contract, AllFourModesAccumulate)
{
    Graphics::Passes::PointCloudRenderPass pass;

    auto pt = Graphics::Passes::PointCloudRenderPass::PackPoint(
        0, 0, 0, 0, 1, 0, 0.01f, 0xFFFFFFFF);

    pass.SubmitPoints(Geometry::PointCloud::RenderMode::FlatDisc,      &pt, 1);
    pass.SubmitPoints(Geometry::PointCloud::RenderMode::Surfel,        &pt, 1);
    pass.SubmitPoints(Geometry::PointCloud::RenderMode::EWA,           &pt, 1);
    pass.SubmitPoints(Geometry::PointCloud::RenderMode::GaussianSplat, &pt, 1);

    EXPECT_EQ(pass.GetPointCount(), 4u);
}

TEST(PointCloudRenderPass_Contract, ResetClearsAllFourModes)
{
    Graphics::Passes::PointCloudRenderPass pass;

    auto pt = Graphics::Passes::PointCloudRenderPass::PackPoint(
        0, 0, 0, 0, 1, 0, 0.01f, 0xFFFFFFFF);
    pass.SubmitPoints(Geometry::PointCloud::RenderMode::GaussianSplat, &pt, 1);
    EXPECT_TRUE(pass.HasContent());

    pass.ResetPoints();
    EXPECT_FALSE(pass.HasContent());
    EXPECT_EQ(pass.GetPointCount(), 0u);
}

// ---- Integration: PointCloud Cloud → ECS Component ----

TEST(PointCloud_Integration, CloudToComponent)
{
    // Simulate the pipeline: create a Cloud, process it, attach to ECS.
    Geometry::PointCloud::Cloud cloud;
    cloud.Positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}};
    cloud.Normals = {{0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}};
    cloud.Colors = {{1, 0, 0, 1}, {0, 1, 0, 1}, {0, 0, 1, 1}, {1, 1, 0, 1}};

    // Estimate radii.
    auto radiiResult = Geometry::PointCloud::EstimateRadii(cloud, {});
    ASSERT_TRUE(radiiResult.has_value());

    // Create ECS component from cloud.
    ECS::PointCloudRenderer::Component comp;
    comp.Positions = cloud.Positions;
    comp.Normals = cloud.Normals;
    comp.Colors = cloud.Colors;
    comp.Radii = radiiResult->Radii;
    comp.RenderMode = 1; // Surfel

    EXPECT_EQ(comp.PointCount(), 4u);
    EXPECT_TRUE(comp.HasNormals());
    EXPECT_TRUE(comp.HasColors());
    EXPECT_TRUE(comp.HasRadii());
}
