#include <gtest/gtest.h>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

import Graphics;
import Geometry;

// =============================================================================
// PointCloudRenderer ECS Component — Compile-time contract tests
// =============================================================================
//
// These tests validate the CPU-side contract of the PointCloudRenderer ECS
// component and Cloud-to-Component integration without requiring a GPU device.

// ---- ECS Component Tests ----

TEST(PointCloudRenderer_Component, DefaultValues)
{
    ECS::PointCloudRenderer::Component comp;
    EXPECT_EQ(comp.PointCount(), 0u);
    EXPECT_FALSE(comp.HasNormals());
    EXPECT_FALSE(comp.HasColors());
    EXPECT_FALSE(comp.HasRadii());
    EXPECT_EQ(comp.RenderMode, Geometry::PointCloud::RenderMode::FlatDisc);
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
    comp.RenderMode = Geometry::PointCloud::RenderMode::FlatDisc;

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

// ---- Integration: PointCloud Cloud -> ECS Component ----

TEST(PointCloud_Integration, CloudToComponent)
{
    // Simulate the pipeline: create a Cloud, process it, attach to ECS.
    Geometry::PointCloud::Cloud cloud;
    cloud.EnableNormals();
    cloud.EnableColors();

    const std::initializer_list<glm::vec3> pts  = {{0,0,0},{1,0,0},{0,1,0},{1,1,0}};
    const std::initializer_list<glm::vec3> nrms = {{0,0,1},{0,0,1},{0,0,1},{0,0,1}};
    const std::initializer_list<glm::vec4> cols = {{1,0,0,1},{0,1,0,1},{0,0,1,1},{1,1,0,1}};

    auto pit = pts.begin();
    auto nit = nrms.begin();
    auto cit = cols.begin();
    for (std::size_t i = 0; i < 4u; ++i, ++pit, ++nit, ++cit)
    {
        auto ph = cloud.AddPoint(*pit);
        cloud.Normal(ph) = *nit;
        cloud.Color(ph)  = *cit;
    }

    // Estimate radii.
    auto radiiResult = Geometry::PointCloud::EstimateRadii(cloud, {});
    ASSERT_TRUE(radiiResult.has_value());

    // Create ECS component from cloud.
    auto positions = cloud.Positions();
    auto normals   = cloud.Normals();
    auto colors    = cloud.Colors();

    ECS::PointCloudRenderer::Component comp;
    comp.Positions = std::vector<glm::vec3>(positions.begin(), positions.end());
    comp.Normals   = std::vector<glm::vec3>(normals.begin(),   normals.end());
    comp.Colors    = std::vector<glm::vec4>(colors.begin(),    colors.end());
    comp.Radii     = radiiResult->Radii;
    comp.RenderMode = Geometry::PointCloud::RenderMode::FlatDisc;

    EXPECT_EQ(comp.PointCount(), 4u);
    EXPECT_TRUE(comp.HasNormals());
    EXPECT_TRUE(comp.HasColors());
    EXPECT_TRUE(comp.HasRadii());
}

// ---- EWA Render Mode Tests ----

TEST(PointCloudRenderer_Component, EWAModeAssignable)
{
    ECS::PointCloudRenderer::Component comp;
    comp.RenderMode = Geometry::PointCloud::RenderMode::EWA;
    EXPECT_EQ(comp.RenderMode, Geometry::PointCloud::RenderMode::EWA);
}

TEST(PointCloud_Integration, CloudToComponentEWA)
{
    // Verify that EWA mode can be assigned to the component after cloud creation.
    Geometry::PointCloud::Cloud cloud;
    cloud.EnableNormals();

    cloud.AddPoint({0, 0, 0});
    cloud.AddPoint({1, 0, 0});

    ECS::PointCloudRenderer::Component comp;
    auto positions = cloud.Positions();
    auto normals   = cloud.Normals();
    comp.Positions = std::vector<glm::vec3>(positions.begin(), positions.end());
    comp.Normals   = std::vector<glm::vec3>(normals.begin(),   normals.end());
    comp.RenderMode = Geometry::PointCloud::RenderMode::EWA;

    EXPECT_EQ(comp.PointCount(), 2u);
    EXPECT_TRUE(comp.HasNormals());
    EXPECT_EQ(comp.RenderMode, Geometry::PointCloud::RenderMode::EWA);
}
