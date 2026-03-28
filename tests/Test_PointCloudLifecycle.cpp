#include <gtest/gtest.h>
#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

import Graphics;
import Geometry;

// =============================================================================
// PointCloudLifecycle — Compile-time contract tests
// =============================================================================
//
// These tests validate the CPU-side contract of the Cloud-backed retained-mode
// point cloud sync system without requiring a GPU device. They verify:
//   - Component GPU state fields and defaults.
//   - HasGpuGeometry() query correctness.
//   - GpuDirty lifecycle transitions.
//   - Cloud span accessors for zero-copy upload.
//   - Per-point attribute extraction readiness (colors, radii).
//   - Cloud-backed and preloaded-geometry point-cloud paths.

// ---- Component GPU State Defaults ----

TEST(PointCloudLifecycle_Contract, DefaultGpuStateFields)
{
    ECS::PointCloud::Data comp;

    EXPECT_FALSE(comp.GpuGeometry.IsValid());
    EXPECT_EQ(comp.GpuSlot, ECS::kInvalidGpuSlot);
    EXPECT_TRUE(comp.GpuDirty);
    EXPECT_FALSE(comp.HasGpuGeometry());
    EXPECT_EQ(comp.GpuPointCount, 0u);
    EXPECT_EQ(comp.PositionRevision, 1u);
    EXPECT_FALSE(comp.KMeansJobPending);
    EXPECT_EQ(comp.KMeansPendingClusterCount, 0u);
    EXPECT_EQ(comp.KMeansLastBackend, Geometry::KMeans::Backend::CPU);
}

TEST(PointCloudLifecycle_Contract, InvalidSlotSentinel)
{
    // All components now use the shared ECS::kInvalidGpuSlot constant.
    EXPECT_EQ(ECS::kInvalidGpuSlot, ~0u);
}

// ---- HasGpuGeometry Query ----

TEST(PointCloudLifecycle_Contract, HasGpuGeometryFalseByDefault)
{
    ECS::PointCloud::Data comp;
    EXPECT_FALSE(comp.HasGpuGeometry());
}

TEST(PointCloudLifecycle_Contract, HasGpuGeometryTrueWhenValid)
{
    ECS::PointCloud::Data comp;
    comp.GpuGeometry = Geometry::GeometryHandle(0, 1);
    EXPECT_TRUE(comp.HasGpuGeometry());
}

// ---- CloudRef Queries ----

TEST(PointCloudLifecycle_Contract, NullCloudRefQueriesReturnFalse)
{
    ECS::PointCloud::Data comp;
    EXPECT_EQ(comp.PointCount(), 0u);
    EXPECT_FALSE(comp.HasNormals());
    EXPECT_FALSE(comp.HasColors());
    EXPECT_FALSE(comp.HasRadii());
}

TEST(PointCloudLifecycle_Contract, CloudRefDelegatesQueries)
{
    auto cloud = std::make_shared<Geometry::PointCloud::Cloud>();
    cloud->AddPoint({0.f, 0.f, 0.f});
    cloud->AddPoint({1.f, 0.f, 0.f});
    cloud->AddPoint({0.f, 1.f, 0.f});

    ECS::PointCloud::Data comp;
    comp.CloudRef = cloud;

    EXPECT_EQ(comp.PointCount(), 3u);
    EXPECT_FALSE(comp.HasNormals());
    EXPECT_FALSE(comp.HasColors());
    EXPECT_FALSE(comp.HasRadii());
}

TEST(PointCloudLifecycle_Contract, CloudRefWithNormals)
{
    auto cloud = std::make_shared<Geometry::PointCloud::Cloud>();
    cloud->EnableNormals();
    cloud->AddPoint({0.f, 0.f, 0.f});

    ECS::PointCloud::Data comp;
    comp.CloudRef = cloud;

    EXPECT_TRUE(comp.HasNormals());
}

TEST(PointCloudLifecycle_Contract, CloudRefWithColors)
{
    auto cloud = std::make_shared<Geometry::PointCloud::Cloud>();
    cloud->EnableColors();
    cloud->AddPoint({0.f, 0.f, 0.f});

    ECS::PointCloud::Data comp;
    comp.CloudRef = cloud;

    EXPECT_TRUE(comp.HasColors());
}

TEST(PointCloudLifecycle_Contract, CloudRefWithRadii)
{
    auto cloud = std::make_shared<Geometry::PointCloud::Cloud>();
    cloud->EnableRadii();
    cloud->AddPoint({0.f, 0.f, 0.f});

    ECS::PointCloud::Data comp;
    comp.CloudRef = cloud;

    EXPECT_TRUE(comp.HasRadii());
}

// ---- Cloud Span Accessors for Zero-Copy Upload ----

TEST(PointCloudLifecycle_Contract, CloudSpanAccessorsForUpload)
{
    auto cloud = std::make_shared<Geometry::PointCloud::Cloud>();
    cloud->EnableNormals();
    cloud->AddPoint({1.f, 2.f, 3.f});
    cloud->AddPoint({4.f, 5.f, 6.f});

    // PointCloudLifecycleSystem reads these spans directly for GPU upload.
    auto positions = cloud->Positions();
    auto normals = cloud->Normals();

    EXPECT_EQ(positions.size(), 2u);
    EXPECT_FLOAT_EQ(positions[0].x, 1.f);
    EXPECT_FLOAT_EQ(positions[1].z, 6.f);

    EXPECT_EQ(normals.size(), 2u);
}

// ---- Upload Request Construction ----

TEST(PointCloudLifecycle_Contract, UploadRequestFromCloudSpans)
{
    auto cloud = std::make_shared<Geometry::PointCloud::Cloud>();
    cloud->EnableNormals();
    cloud->AddPoint({0.f, 0.f, 0.f});
    cloud->AddPoint({1.f, 0.f, 0.f});
    cloud->AddPoint({0.f, 1.f, 0.f});

    // Simulate what PointCloudLifecycleSystem does: build a
    // GeometryUploadRequest from Cloud span accessors.
    Graphics::GeometryUploadRequest upload{};
    upload.Positions = cloud->Positions();
    upload.Normals = cloud->Normals();
    upload.Topology = Graphics::PrimitiveTopology::Points;
    upload.UploadMode = Graphics::GeometryUploadMode::Staged;

    EXPECT_EQ(upload.Positions.size(), 3u);
    EXPECT_EQ(upload.Normals.size(), 3u);
    EXPECT_EQ(upload.Topology, Graphics::PrimitiveTopology::Points);
    EXPECT_EQ(upload.UploadMode, Graphics::GeometryUploadMode::Staged);
    EXPECT_TRUE(upload.Indices.empty());
}

TEST(PointCloudLifecycle_Contract, UploadRequestOmitsNormalsWhenCloudHasNone)
{
    auto cloud = std::make_shared<Geometry::PointCloud::Cloud>();
    cloud->AddPoint({0.f, 0.f, 0.f});
    cloud->AddPoint({1.f, 0.f, 0.f});
    EXPECT_FALSE(cloud->HasNormals());

    Graphics::GeometryUploadRequest upload{};
    upload.Positions = cloud->Positions();

    EXPECT_EQ(upload.Positions.size(), 2u);
    EXPECT_TRUE(upload.Normals.empty());
}

// ---- GpuDirty Lifecycle Transitions ----

TEST(PointCloudLifecycle_Contract, NewComponentIsDirty)
{
    ECS::PointCloud::Data comp;
    comp.CloudRef = std::make_shared<Geometry::PointCloud::Cloud>();
    comp.CloudRef->AddPoint({0.f, 0.f, 0.f});

    EXPECT_TRUE(comp.GpuDirty);
    EXPECT_FALSE(comp.HasGpuGeometry());
}

TEST(PointCloudLifecycle_Contract, DirtyFlagClearedAfterUpload)
{
    ECS::PointCloud::Data comp;
    comp.CloudRef = std::make_shared<Geometry::PointCloud::Cloud>();
    comp.CloudRef->AddPoint({0.f, 0.f, 0.f});

    // Simulate lifecycle clearing dirty flag after upload.
    comp.GpuGeometry = Geometry::GeometryHandle(0, 1);
    comp.GpuPointCount = 1;
    comp.GpuDirty = false;

    EXPECT_FALSE(comp.GpuDirty);
    EXPECT_TRUE(comp.HasGpuGeometry());
    EXPECT_EQ(comp.GpuPointCount, 1u);
}

// ---- Rendering Parameters ----

TEST(PointCloudLifecycle_Contract, RenderModeDefault)
{
    ECS::PointCloud::Data comp;
    EXPECT_EQ(comp.RenderMode, Geometry::PointCloud::RenderMode::FlatDisc);
}

TEST(PointCloudLifecycle_Contract, RenderModePreserved)
{
    ECS::PointCloud::Data comp;
    comp.RenderMode = Geometry::PointCloud::RenderMode::EWA;
    EXPECT_EQ(comp.RenderMode, Geometry::PointCloud::RenderMode::EWA);
}

TEST(PointCloudLifecycle_Contract, DefaultColorPreserved)
{
    ECS::PointCloud::Data comp;
    comp.DefaultColor = {0.5f, 0.3f, 0.1f, 1.0f};

    EXPECT_FLOAT_EQ(comp.DefaultColor.r, 0.5f);
    EXPECT_FLOAT_EQ(comp.DefaultColor.g, 0.3f);
    EXPECT_FLOAT_EQ(comp.DefaultColor.b, 0.1f);
}

TEST(PointCloudLifecycle_Contract, VisibilityToggle)
{
    ECS::PointCloud::Data comp;
    EXPECT_TRUE(comp.Visible);

    comp.Visible = false;
    EXPECT_FALSE(comp.Visible);
}

// ---- Cached Attributes ----

TEST(PointCloudLifecycle_Contract, CachedColorsInitiallyEmpty)
{
    ECS::PointCloud::Data comp;
    EXPECT_TRUE(comp.CachedColors.empty());
}

TEST(PointCloudLifecycle_Contract, CachedRadiiInitiallyEmpty)
{
    ECS::PointCloud::Data comp;
    EXPECT_TRUE(comp.CachedRadii.empty());
}

// ---- Single-Path PointCloud Contract ----

TEST(PointCloudLifecycle_Contract, PointCloudDataIsSinglePointCloudPath)
{
    ECS::PointCloud::Data cloudComp;
    cloudComp.CloudRef = std::make_shared<Geometry::PointCloud::Cloud>();
    cloudComp.CloudRef->AddPoint({0.f, 0.f, 0.f});

    EXPECT_EQ(cloudComp.PointCount(), 1u);
    EXPECT_TRUE(cloudComp.GpuDirty);
}

// ---- Cloud Data Survives After Upload (not freed) ----

TEST(PointCloudLifecycle_Contract, CloudDataSurvivesAfterUpload)
{
    // PointCloud::Data keeps the Cloud alive for potential re-upload.
    auto cloud = std::make_shared<Geometry::PointCloud::Cloud>();
    cloud->AddPoint({1.f, 2.f, 3.f});
    cloud->AddPoint({4.f, 5.f, 6.f});

    ECS::PointCloud::Data comp;
    comp.CloudRef = cloud;

    // Simulate upload completion.
    comp.GpuGeometry = Geometry::GeometryHandle(0, 1);
    comp.GpuPointCount = 2;
    comp.GpuDirty = false;

    // Cloud data is still accessible for re-upload or CPU queries.
    EXPECT_EQ(comp.PointCount(), 2u);
    EXPECT_EQ(comp.CloudRef->VerticesSize(), 2u);
    EXPECT_FLOAT_EQ(comp.CloudRef->Positions()[0].x, 1.f);
}

// ---- Re-Upload on Data Change ----

TEST(PointCloudLifecycle_Contract, ReUploadOnDataChange)
{
    auto cloud = std::make_shared<Geometry::PointCloud::Cloud>();
    cloud->AddPoint({0.f, 0.f, 0.f});

    ECS::PointCloud::Data comp;
    comp.CloudRef = cloud;

    // Simulate initial upload.
    comp.GpuGeometry = Geometry::GeometryHandle(0, 1);
    comp.GpuPointCount = 1;
    comp.GpuDirty = false;

    // Modify cloud data (e.g., add more points after downsampling).
    cloud->AddPoint({1.f, 1.f, 1.f});

    // User code or algorithm sets dirty to trigger re-upload.
    comp.GpuDirty = true;

    EXPECT_TRUE(comp.GpuDirty);
    EXPECT_EQ(comp.PointCount(), 2u);
    EXPECT_EQ(comp.GpuPointCount, 1u); // Stale until sync runs.
}

TEST(PointCloudLifecycle_Contract, PreloadedGpuNormalsContract)
{
    ECS::PointCloud::Data comp;
    comp.HasGpuNormals = true;

    EXPECT_FALSE(comp.HasNormals());
    EXPECT_TRUE(comp.HasRenderableNormals());
}
