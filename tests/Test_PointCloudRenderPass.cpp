#include <gtest/gtest.h>
#include <memory>

#include <glm/glm.hpp>

import Graphics;
import Geometry;

// =============================================================================
// PointCloud::Data ECS Component — Compile-time contract tests
// =============================================================================

TEST(PointCloudData_Component, DefaultValues)
{
    ECS::PointCloud::Data comp;
    EXPECT_EQ(comp.PointCount(), 0u);
    EXPECT_FALSE(comp.HasNormals());
    EXPECT_FALSE(comp.HasRenderableNormals());
    EXPECT_FALSE(comp.HasColors());
    EXPECT_FALSE(comp.HasRadii());
    EXPECT_EQ(comp.RenderMode, Geometry::PointCloud::RenderMode::FlatDisc);
    EXPECT_FLOAT_EQ(comp.DefaultRadius, 0.005f);
    EXPECT_FLOAT_EQ(comp.SizeMultiplier, 1.0f);
    EXPECT_TRUE(comp.Visible);
}

TEST(PointCloudData_Component, CloudBackedQueries)
{
    auto cloud = std::make_shared<Geometry::PointCloud::Cloud>();
    cloud->EnableNormals();
    cloud->EnableColors();
    cloud->EnableRadii();

    auto p0 = cloud->AddPoint({0, 0, 0});
    cloud->Normal(p0) = {0, 1, 0};
    cloud->Color(p0) = {1, 0, 0, 1};
    cloud->Radius(p0) = 0.01f;

    ECS::PointCloud::Data comp;
    comp.CloudRef = cloud;

    EXPECT_EQ(comp.PointCount(), 1u);
    EXPECT_TRUE(comp.HasNormals());
    EXPECT_TRUE(comp.HasRenderableNormals());
    EXPECT_TRUE(comp.HasColors());
    EXPECT_TRUE(comp.HasRadii());
}

TEST(PointCloudData_Component, PreloadedGpuNormalsPath)
{
    ECS::PointCloud::Data comp;
    comp.HasGpuNormals = true;

    EXPECT_FALSE(comp.HasNormals());
    EXPECT_TRUE(comp.HasRenderableNormals());
}

TEST(PointCloudData_Component, EWAModeAssignable)
{
    ECS::PointCloud::Data comp;
    comp.RenderMode = Geometry::PointCloud::RenderMode::EWA;
    EXPECT_EQ(comp.RenderMode, Geometry::PointCloud::RenderMode::EWA);
}
