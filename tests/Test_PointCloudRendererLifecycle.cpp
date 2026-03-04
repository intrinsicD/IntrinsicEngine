#include <gtest/gtest.h>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

import Graphics;
import Geometry;

// =============================================================================
// PointCloudRendererLifecycle — Compile-time contract tests
// =============================================================================
//
// These tests validate the CPU-side contract of the standalone retained-mode
// point cloud rendering path without requiring a GPU device. They verify:
//   - Component GPU state fields and defaults.
//   - HasGpuGeometry() query correctness.
//   - GpuDirty lifecycle transitions.
//   - Component coexistence with GeometryHandle from ModelLoader.
//   - PointCloudRenderer::Component vs Surface::Component routing.

// ---- Component GPU State Defaults ----

TEST(PointCloudRendererLifecycle_Contract, DefaultGpuStateFields)
{
    ECS::PointCloudRenderer::Component comp;

    EXPECT_FALSE(comp.Geometry.IsValid());
    EXPECT_EQ(comp.GpuSlot, ECS::PointCloudRenderer::Component::kInvalidSlot);
    EXPECT_TRUE(comp.GpuDirty);
    EXPECT_FALSE(comp.HasGpuNormals);
    EXPECT_FALSE(comp.HasGpuGeometry());
}

TEST(PointCloudRendererLifecycle_Contract, InvalidSlotSentinel)
{
    // kInvalidSlot must be ~0u (same as Surface).
    EXPECT_EQ(ECS::PointCloudRenderer::Component::kInvalidSlot, ~0u);
    EXPECT_EQ(ECS::PointCloudRenderer::Component::kInvalidSlot,
              ECS::Surface::Component::kInvalidSlot);
}

// ---- HasGpuGeometry Query ----

TEST(PointCloudRendererLifecycle_Contract, HasGpuGeometryFalseByDefault)
{
    ECS::PointCloudRenderer::Component comp;
    EXPECT_FALSE(comp.HasGpuGeometry());
}

TEST(PointCloudRendererLifecycle_Contract, HasGpuGeometryTrueWhenValid)
{
    ECS::PointCloudRenderer::Component comp;
    comp.Geometry = Geometry::GeometryHandle(0, 1);
    EXPECT_TRUE(comp.HasGpuGeometry());
}

// ---- GpuDirty Lifecycle Transitions ----

TEST(PointCloudRendererLifecycle_Contract, CpuOriginatedCloudIsDirty)
{
    // When creating a point cloud from code (CPU data), GpuDirty starts true.
    ECS::PointCloudRenderer::Component comp;
    comp.Positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    comp.Normals = {{0, 1, 0}, {0, 1, 0}, {0, 0, 1}};

    EXPECT_TRUE(comp.GpuDirty);
    EXPECT_FALSE(comp.HasGpuGeometry());
    EXPECT_EQ(comp.PointCount(), 3u);
}

TEST(PointCloudRendererLifecycle_Contract, FileLoadedCloudIsNotDirty)
{
    // When ModelLoader pre-uploads geometry, GpuDirty is set to false.
    ECS::PointCloudRenderer::Component comp;
    comp.Geometry = Geometry::GeometryHandle(0, 1);
    comp.HasGpuNormals = true;
    comp.GpuDirty = false;

    EXPECT_FALSE(comp.GpuDirty);
    EXPECT_TRUE(comp.HasGpuGeometry());
    EXPECT_TRUE(comp.HasRenderableNormals());
    EXPECT_EQ(comp.PointCount(), 0u); // No CPU data.
}

// ---- Component Field Compatibility ----

TEST(PointCloudRendererLifecycle_Contract, RenderModePreserved)
{
    ECS::PointCloudRenderer::Component comp;
    comp.RenderMode = Geometry::PointCloud::RenderMode::EWA;
    comp.Geometry = Geometry::GeometryHandle(0, 1);
    comp.GpuDirty = false;

    EXPECT_EQ(comp.RenderMode, Geometry::PointCloud::RenderMode::EWA);
    EXPECT_TRUE(comp.HasGpuGeometry());
}

TEST(PointCloudRendererLifecycle_Contract, DefaultColorPreserved)
{
    ECS::PointCloudRenderer::Component comp;
    comp.DefaultColor = {0.5f, 0.3f, 0.1f, 1.0f};

    EXPECT_FLOAT_EQ(comp.DefaultColor.r, 0.5f);
    EXPECT_FLOAT_EQ(comp.DefaultColor.g, 0.3f);
    EXPECT_FLOAT_EQ(comp.DefaultColor.b, 0.1f);
}

TEST(PointCloudRendererLifecycle_Contract, VisibilityToggle)
{
    ECS::PointCloudRenderer::Component comp;
    EXPECT_TRUE(comp.Visible);

    comp.Visible = false;
    EXPECT_FALSE(comp.Visible);
}

// ---- Topology Routing ----

TEST(PointCloudRendererLifecycle_Contract, TopologyRouting)
{
    // Point topology should produce PointCloudRenderer, not Surface.
    // This test validates the semantic intent — actual routing happens in SceneManager.
    EXPECT_EQ(static_cast<int>(Graphics::PrimitiveTopology::Points), 2);
    EXPECT_NE(static_cast<int>(Graphics::PrimitiveTopology::Points),
              static_cast<int>(Graphics::PrimitiveTopology::Triangles));
}

// ---- Upload Request Construction ----

TEST(PointCloudRendererLifecycle_Contract, UploadRequestFromComponent)
{
    // Simulate what PointCloudRendererLifecycle does: build a GeometryUploadRequest
    // from component CPU data.
    ECS::PointCloudRenderer::Component comp;
    comp.Positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    comp.Normals = {{0, 1, 0}, {0, 1, 0}, {0, 0, 1}};

    Graphics::GeometryUploadRequest upload{};
    upload.Positions = comp.Positions;
    upload.Normals = comp.Normals;
    upload.Topology = Graphics::PrimitiveTopology::Points;
    upload.UploadMode = Graphics::GeometryUploadMode::Staged;

    EXPECT_EQ(upload.Positions.size(), 3u);
    EXPECT_EQ(upload.Normals.size(), 3u);
    EXPECT_EQ(upload.Topology, Graphics::PrimitiveTopology::Points);
    EXPECT_EQ(upload.UploadMode, Graphics::GeometryUploadMode::Staged);
    EXPECT_TRUE(upload.Indices.empty());
}

TEST(PointCloudRendererLifecycle_Contract, UploadRequestOmitsNormalsWhenMissing)
{
    // Missing normals now remain absent in the upload request.
    ECS::PointCloudRenderer::Component comp;
    comp.Positions = {{0, 0, 0}, {1, 0, 0}};
    EXPECT_FALSE(comp.HasNormals());

    Graphics::GeometryUploadRequest upload{};
    upload.Positions = comp.Positions;

    EXPECT_EQ(upload.Positions.size(), 2u);
    EXPECT_TRUE(upload.Normals.empty());
}

// ---- CPU Data Clearing After Upload ----

TEST(PointCloudRendererLifecycle_Contract, CpuDataClearedAfterUpload)
{
    // After lifecycle uploads to GPU, it clears CPU vectors to free memory.
    ECS::PointCloudRenderer::Component comp;
    comp.Positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    comp.Normals = {{0, 1, 0}, {0, 1, 0}, {0, 0, 1}};
    comp.Colors = {{1, 0, 0, 1}, {0, 1, 0, 1}, {0, 0, 1, 1}};
    comp.Radii = {0.01f, 0.02f, 0.03f};

    EXPECT_EQ(comp.PointCount(), 3u);

    // Simulate lifecycle clearing.
    comp.Positions.clear();
    comp.Positions.shrink_to_fit();
    comp.Normals.clear();
    comp.Normals.shrink_to_fit();
    comp.Colors.clear();
    comp.Colors.shrink_to_fit();
    comp.Radii.clear();
    comp.Radii.shrink_to_fit();

    EXPECT_EQ(comp.PointCount(), 0u);
    EXPECT_FALSE(comp.HasNormals());
    EXPECT_FALSE(comp.HasColors());
    EXPECT_FALSE(comp.HasRadii());
}

TEST(PointCloudRendererLifecycle_Contract, HasRenderableNormalsPersistsAfterCpuCleanup)
{
    ECS::PointCloudRenderer::Component comp;
    comp.Positions = {{0, 0, 0}};
    comp.Normals = {{0, 1, 0}};

    EXPECT_TRUE(comp.HasNormals());
    EXPECT_TRUE(comp.HasRenderableNormals());

    // Simulate lifecycle upload completion and CPU vector cleanup.
    comp.HasGpuNormals = true;
    comp.Positions.clear();
    comp.Normals.clear();

    EXPECT_FALSE(comp.HasNormals());
    EXPECT_TRUE(comp.HasRenderableNormals());
}
