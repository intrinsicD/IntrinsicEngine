#include <cstdint>
#include <memory>

#include <gtest/gtest.h>
#include <glm/glm.hpp>

import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.Component.Light;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.Graphics.Renderer;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.Runtime.RenderExtraction;

#include "MockRHI.hpp"

using namespace Extrinsic;

namespace
{
    struct RendererFixture
    {
        Tests::MockDevice Device{};
        std::unique_ptr<Graphics::IRenderer> Renderer{Graphics::CreateRenderer()};
        Runtime::RenderExtractionCache Extraction{};

        RendererFixture()
        {
            Renderer->Initialize(Device);
        }

        ~RendererFixture()
        {
            Extraction.Shutdown(*Renderer);
            Renderer->Shutdown();
        }

        Runtime::RuntimeRenderExtractionStats Extract(ECS::Scene::Registry& scene)
        {
            RHI::FrameHandle frame{};
            EXPECT_TRUE(Renderer->BeginFrame(frame));
            return Extraction.ExtractAndSubmit(scene, *Renderer);
        }
    };
}

TEST(RuntimeRenderExtraction, CreatesUpdatesAndClearsDirtyTransformSidecar)
{
    RendererFixture fixture;
    ECS::Scene::Registry scene;
    auto& registry = scene.Raw();

    const auto entity = scene.Create();
    auto& world = registry.emplace<ECS::Components::Transform::WorldMatrix>(entity);
    world.Matrix = glm::mat4{1.f};
    world.Matrix[3] = glm::vec4{2.f, 3.f, 4.f, 1.f};
    registry.emplace<Graphics::Components::RenderSurface>(entity);
    registry.emplace<ECS::Components::DirtyTags::DirtyTransform>(entity);

    auto stats = fixture.Extract(scene);

    EXPECT_EQ(stats.CandidateRenderableCount, 1u);
    EXPECT_EQ(stats.AllocatedInstanceCount, 1u);
    EXPECT_EQ(stats.SubmittedTransformCount, 1u);
    EXPECT_EQ(stats.SubmittedVisualizationCount, 1u);
    EXPECT_EQ(stats.DirtyTransformCount, 1u);
    EXPECT_EQ(fixture.Extraction.GetTrackedRenderableCount(), 1u);
    EXPECT_EQ(fixture.Renderer->GetGpuWorld().GetLiveInstanceCount(), 1u);
    EXPECT_FALSE(registry.any_of<ECS::Components::DirtyTags::DirtyTransform>(entity));

    world.Matrix[3] = glm::vec4{5.f, 6.f, 7.f, 1.f};
    stats = fixture.Extract(scene);

    EXPECT_EQ(stats.AllocatedInstanceCount, 0u);
    EXPECT_EQ(stats.FreedInstanceCount, 0u);
    EXPECT_EQ(stats.SubmittedTransformCount, 1u);
    EXPECT_EQ(stats.DirtyTransformCount, 0u);
    EXPECT_EQ(fixture.Extraction.GetTrackedRenderableCount(), 1u);
    EXPECT_EQ(fixture.Renderer->GetGpuWorld().GetLiveInstanceCount(), 1u);
}

TEST(RuntimeRenderExtraction, RetiresDestroyedRenderableSidecar)
{
    RendererFixture fixture;
    ECS::Scene::Registry scene;
    auto& registry = scene.Raw();

    const auto entity = scene.Create();
    registry.emplace<ECS::Components::Transform::WorldMatrix>(entity).Matrix = glm::mat4{1.f};
    registry.emplace<Graphics::Components::RenderLines>(entity);

    auto stats = fixture.Extract(scene);
    ASSERT_EQ(stats.AllocatedInstanceCount, 1u);
    ASSERT_EQ(fixture.Renderer->GetGpuWorld().GetLiveInstanceCount(), 1u);

    scene.Destroy(entity);
    stats = fixture.Extract(scene);

    EXPECT_EQ(stats.CandidateRenderableCount, 0u);
    EXPECT_EQ(stats.SubmittedTransformCount, 0u);
    EXPECT_EQ(stats.FreedInstanceCount, 1u);
    EXPECT_EQ(fixture.Extraction.GetTrackedRenderableCount(), 0u);
    EXPECT_EQ(fixture.Renderer->GetGpuWorld().GetLiveInstanceCount(), 0u);
}

TEST(RuntimeRenderExtraction, ExtractsVisualizationAndLightsWithoutRenderableOwnership)
{
    RendererFixture fixture;
    ECS::Scene::Registry scene;
    auto& registry = scene.Raw();

    const auto visualized = scene.Create();
    registry.emplace<ECS::Components::Transform::WorldMatrix>(visualized).Matrix = glm::mat4{1.f};
    registry.emplace<Graphics::Components::RenderPoints>(visualized);
    registry.emplace<Graphics::Components::VisualizationConfig>(visualized).Source =
        Graphics::Components::VisualizationConfig::ColorSource::UniformColor;

    const auto light = scene.Create();
    auto& lightWorld = registry.emplace<ECS::Components::Transform::WorldMatrix>(light);
    lightWorld.Matrix = glm::mat4{1.f};
    lightWorld.Matrix[3] = glm::vec4{1.f, 2.f, 3.f, 1.f};
    auto& point = registry.emplace<ECS::Components::Lights::PointLight>(light);
    point.Color = {0.25f, 0.5f, 1.f};
    point.Intensity = 3.f;

    const auto stats = fixture.Extract(scene);

    EXPECT_EQ(stats.CandidateRenderableCount, 1u);
    EXPECT_EQ(stats.SubmittedTransformCount, 1u);
    EXPECT_EQ(stats.SubmittedVisualizationCount, 1u);
    EXPECT_EQ(stats.SubmittedLightCount, 1u);
    EXPECT_EQ(stats.SkippedInvalidEntityCount, 0u);
    EXPECT_EQ(fixture.Extraction.GetTrackedRenderableCount(), 1u);
    EXPECT_EQ(fixture.Renderer->GetGpuWorld().GetLiveInstanceCount(), 1u);
}

