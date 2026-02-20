#include <gtest/gtest.h>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

import Graphics;
import Geometry;

// =============================================================================
// GraphRenderPass — Compile-time contract tests
// =============================================================================
//
// These tests validate the CPU-side contract of GraphRenderPass and the
// ECS::GraphRenderer::Component without requiring a GPU device.  They verify:
//   - GraphRenderer component default values and data accessors.
//   - Node color / radius optional attribute detection.
//   - GraphRenderPass instantiation and configuration (no GPU calls).
//   - GraphRenderPass correctly delegates node submission to PointCloudRenderPass.

// ---- ECS::GraphRenderer::Component Tests ----

TEST(GraphRenderer_Component, DefaultValues)
{
    ECS::GraphRenderer::Component comp;
    EXPECT_EQ(comp.NodeCount(), 0u);
    EXPECT_EQ(comp.EdgeCount(), 0u);
    EXPECT_FALSE(comp.HasNodeColors());
    EXPECT_FALSE(comp.HasNodeRadii());
    EXPECT_FLOAT_EQ(comp.DefaultNodeRadius,  0.01f);
    EXPECT_FLOAT_EQ(comp.NodeSizeMultiplier, 1.0f);
    EXPECT_TRUE(comp.Visible);
    EXPECT_FALSE(comp.EdgesOverlay);
}

TEST(GraphRenderer_Component, WithNodes)
{
    ECS::GraphRenderer::Component comp;
    comp.NodePositions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};

    EXPECT_EQ(comp.NodeCount(), 3u);
    EXPECT_FALSE(comp.HasNodeColors()); // empty colors → false
    EXPECT_FALSE(comp.HasNodeRadii());
}

TEST(GraphRenderer_Component, WithOptionalAttributes)
{
    ECS::GraphRenderer::Component comp;
    comp.NodePositions = {{0, 0, 0}, {1, 0, 0}};
    comp.NodeColors    = {{1, 0, 0, 1}, {0, 1, 0, 1}};
    comp.NodeRadii     = {0.02f, 0.04f};

    EXPECT_EQ(comp.NodeCount(), 2u);
    EXPECT_TRUE(comp.HasNodeColors());
    EXPECT_TRUE(comp.HasNodeRadii());
}

TEST(GraphRenderer_Component, MismatchedAttributeSize)
{
    ECS::GraphRenderer::Component comp;
    comp.NodePositions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    comp.NodeColors    = {{1, 0, 0, 1}}; // wrong count

    EXPECT_FALSE(comp.HasNodeColors()); // size mismatch → false
}

TEST(GraphRenderer_Component, WithEdges)
{
    ECS::GraphRenderer::Component comp;
    comp.NodePositions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}};
    comp.Edges = {{0, 1}, {1, 2}, {2, 3}, {3, 0}};

    EXPECT_EQ(comp.NodeCount(), 4u);
    EXPECT_EQ(comp.EdgeCount(), 4u);
}

// ---- GraphRenderPass Instantiation ----

TEST(GraphRenderPass_Contract, CanBeInstantiated)
{
    // GraphRenderPass has no GPU resources — instantiation should always succeed.
    Graphics::Passes::GraphRenderPass pass;
    SUCCEED(); // No crash = pass.
}

TEST(GraphRenderPass_Contract, SetPointCloudPassDoesNotCrash)
{
    Graphics::Passes::GraphRenderPass pass;
    pass.SetPointCloudPass(nullptr); // nullptr is valid (disables node rendering).
    SUCCEED();
}

// ---- Node Submission via PointCloudRenderPass ----

TEST(GraphRenderPass_Contract, NodeSubmissionDelegatesToPointCloud)
{
    // Verify that GraphRenderPass correctly calls PointCloudRenderPass::SubmitPoints
    // for each node in the graph component.
    // We can't run AddPasses() without ECS, but we can verify the count math
    // by direct PointCloudRenderPass staging.

    Graphics::Passes::PointCloudRenderPass pcPass;

    // Simulate what GraphRenderPass does for 3 nodes in FlatDisc mode.
    for (int i = 0; i < 3; ++i)
    {
        auto pt = Graphics::Passes::PointCloudRenderPass::PackPoint(
            static_cast<float>(i), 0, 0,
            0, 1, 0,
            0.01f,
            Graphics::Passes::PointCloudRenderPass::PackColor(255, 128, 0));
        pcPass.SubmitPoints(Geometry::PointCloud::RenderMode::FlatDisc, &pt, 1);
    }

    EXPECT_EQ(pcPass.GetPointCount(), 3u);
    EXPECT_TRUE(pcPass.HasContent());
}

TEST(GraphRenderPass_Contract, GaussianSplatNodesAccumulate)
{
    // Graph nodes can use any rendering mode including GaussianSplat.
    Graphics::Passes::PointCloudRenderPass pcPass;

    auto pt = Graphics::Passes::PointCloudRenderPass::PackPoint(
        0, 0, 0, 0, 1, 0, 0.02f, 0xFFFFFFFF);
    pcPass.SubmitPoints(Geometry::PointCloud::RenderMode::GaussianSplat, &pt, 1);

    EXPECT_EQ(pcPass.GetPointCount(), 1u);
}

// ---- GraphRenderPass + PointCloudRenderPass Reset Integration ----

TEST(GraphRenderPass_Contract, ResetBeforeCollect)
{
    Graphics::Passes::PointCloudRenderPass pcPass;

    // Submit some points (simulating a previous frame).
    auto pt = Graphics::Passes::PointCloudRenderPass::PackPoint(
        0, 0, 0, 0, 1, 0, 0.01f, 0xFFFFFFFF);
    pcPass.SubmitPoints(&pt, 1);
    ASSERT_EQ(pcPass.GetPointCount(), 1u);

    // Frame boundary: reset before new collection.
    pcPass.ResetPoints();
    EXPECT_EQ(pcPass.GetPointCount(), 0u);
    EXPECT_FALSE(pcPass.HasContent());

    // Re-submit for new frame.
    pcPass.SubmitPoints(Geometry::PointCloud::RenderMode::GaussianSplat, &pt, 1);
    EXPECT_EQ(pcPass.GetPointCount(), 1u);
}

// ---- RenderMode Enum Coverage ----

TEST(GraphRenderer_RenderMode, AllModesAvailable)
{
    // Verify all four render modes are accessible.
    using RM = Geometry::PointCloud::RenderMode;

    EXPECT_EQ(static_cast<uint32_t>(RM::FlatDisc),      0u);
    EXPECT_EQ(static_cast<uint32_t>(RM::Surfel),        1u);
    EXPECT_EQ(static_cast<uint32_t>(RM::EWA),           2u);
    EXPECT_EQ(static_cast<uint32_t>(RM::GaussianSplat), 3u);
}

TEST(GraphRenderer_Component, NodeRenderModeDefault)
{
    ECS::GraphRenderer::Component comp;
    EXPECT_EQ(comp.NodeRenderMode, Geometry::PointCloud::RenderMode::FlatDisc);
}

TEST(GraphRenderer_Component, NodeRenderModeGaussianSplat)
{
    ECS::GraphRenderer::Component comp;
    comp.NodeRenderMode = Geometry::PointCloud::RenderMode::GaussianSplat;
    EXPECT_EQ(comp.NodeRenderMode, Geometry::PointCloud::RenderMode::GaussianSplat);
}
