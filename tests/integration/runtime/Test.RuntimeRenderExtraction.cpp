#include <cstdint>
#include <array>
#include <cstddef>
#include <memory>
#include <span>

#include <gtest/gtest.h>
#include <glm/glm.hpp>

import Extrinsic.Asset.Registry;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Components.AssetInstance;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.Component.Light;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.Component.GpuSceneSlot;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.Graphics.Renderer;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.RHI.TransferQueue;
import Extrinsic.RHI.Types;
import Extrinsic.Runtime.RenderExtraction;

#include "MockRHI.hpp"

using namespace Extrinsic;

namespace
{
    class ImmediateTransferQueue final : public RHI::ITransferQueue
    {
    public:
        [[nodiscard]] RHI::TransferToken UploadBuffer(RHI::BufferHandle,
                                                      const void*,
                                                      std::uint64_t size,
                                                      std::uint64_t = 0) override
        {
            return RHI::TransferToken{++m_NextToken + size};
        }

        [[nodiscard]] RHI::TransferToken UploadBuffer(RHI::BufferHandle,
                                                      std::span<const std::byte> src,
                                                      std::uint64_t = 0) override
        {
            return RHI::TransferToken{++m_NextToken + static_cast<std::uint64_t>(src.size())};
        }

        [[nodiscard]] RHI::TransferToken UploadTexture(RHI::TextureHandle,
                                                       const void*,
                                                       std::uint64_t size,
                                                       std::uint32_t = 0,
                                                       std::uint32_t = 0) override
        {
            return RHI::TransferToken{++m_NextToken + size};
        }

        [[nodiscard]] RHI::TransferToken UploadTextureFullChain(RHI::TextureHandle,
                                                                std::span<const std::byte> bytes) override
        {
            return RHI::TransferToken{++m_NextToken + static_cast<std::uint64_t>(bytes.size())};
        }

        [[nodiscard]] bool IsComplete(RHI::TransferToken) const override { return true; }
        void CollectCompleted() override {}

    private:
        std::uint64_t m_NextToken = 0;
    };

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

        Runtime::RuntimeRenderExtractionStats Extract(ECS::Scene::Registry& scene,
                                                      Graphics::GpuAssetCache* gpuAssets = nullptr)
        {
            RHI::FrameHandle frame{};
            EXPECT_TRUE(Renderer->BeginFrame(frame));
            return Extraction.ExtractAndSubmit(scene, *Renderer, gpuAssets);
        }
    };

    Assets::AssetId MakeAssetId(const std::uint32_t index)
    {
        return Assets::AssetId{index, 1u};
    }

    RHI::BufferDesc TestBufferDesc()
    {
        return RHI::BufferDesc{
            .SizeBytes = 64,
            .Usage = RHI::BufferUsage::Vertex | RHI::BufferUsage::TransferDst,
            .DebugName = "runtime-render-extraction-asset",
        };
    }

    std::array<std::byte, 64> ZeroBytes64{};
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

TEST(RuntimeRenderExtraction, ObservesAssetSourceAsUnavailableWithoutGpuAssetCache)
{
    RendererFixture fixture;
    ECS::Scene::Registry scene;
    auto& registry = scene.Raw();

    const auto entity = scene.Create();
    registry.emplace<ECS::Components::Transform::WorldMatrix>(entity).Matrix = glm::mat4{1.f};
    registry.emplace<Graphics::Components::RenderSurface>(entity);
    registry.emplace<ECS::Components::AssetInstance::Source>(entity).AssetId = 2u;

    const auto stats = fixture.Extract(scene);

    EXPECT_EQ(stats.CandidateRenderableCount, 1u);
    EXPECT_EQ(stats.SourceAssetObservationCount, 1u);
    EXPECT_EQ(stats.SourceAssetCacheUnavailableCount, 1u);
    EXPECT_EQ(stats.SourceAssetViewUnavailableCount, 0u);
    EXPECT_EQ(stats.SourceAssetRebindRequiredCount, 0u);
    EXPECT_EQ(stats.SourceAssetUpToDateCount, 0u);
}

TEST(RuntimeRenderExtraction, ObservesReadyAssetGenerationAsRebindRequired)
{
    RendererFixture fixture;
    ImmediateTransferQueue transfer;
    Graphics::GpuAssetCache gpuAssets(fixture.Renderer->GetBufferManager(),
                                      fixture.Renderer->GetTextureManager(),
                                      fixture.Renderer->GetSamplerManager(),
                                      transfer);
    const auto asset = MakeAssetId(3u);
    auto upload = gpuAssets.RequestUpload(Graphics::GpuBufferRequest{
        .Id = asset,
        .Bytes = std::span{ZeroBytes64},
        .Desc = TestBufferDesc(),
    });
    ASSERT_TRUE(upload.has_value()) << static_cast<int>(upload.error());
    gpuAssets.Tick(0u, fixture.Device.GetFramesInFlight());

    ECS::Scene::Registry scene;
    auto& registry = scene.Raw();
    const auto entity = scene.Create();
    registry.emplace<ECS::Components::Transform::WorldMatrix>(entity).Matrix = glm::mat4{1.f};
    registry.emplace<Graphics::Components::RenderSurface>(entity);
    registry.emplace<ECS::Components::AssetInstance::Source>(entity).AssetId = asset.Index;

    const auto stats = fixture.Extract(scene, &gpuAssets);

    EXPECT_EQ(stats.CandidateRenderableCount, 1u);
    EXPECT_EQ(stats.SourceAssetObservationCount, 1u);
    EXPECT_EQ(stats.SourceAssetCacheUnavailableCount, 0u);
    EXPECT_EQ(stats.SourceAssetViewUnavailableCount, 0u);
    EXPECT_EQ(stats.SourceAssetRebindRequiredCount, 1u);
    EXPECT_EQ(stats.SourceAssetUpToDateCount, 0u);
}

TEST(RuntimeRenderExtraction, HelperDoesNotMarkRebindGenerationAsSeen)
{
    RendererFixture fixture;
    ImmediateTransferQueue transfer;
    Graphics::GpuAssetCache gpuAssets(fixture.Renderer->GetBufferManager(),
                                      fixture.Renderer->GetTextureManager(),
                                      fixture.Renderer->GetSamplerManager(),
                                      transfer);
    const auto asset = MakeAssetId(4u);
    auto upload = gpuAssets.RequestUpload(Graphics::GpuBufferRequest{
        .Id = asset,
        .Bytes = std::span{ZeroBytes64},
        .Desc = TestBufferDesc(),
    });
    ASSERT_TRUE(upload.has_value()) << static_cast<int>(upload.error());
    gpuAssets.Tick(0u, fixture.Device.GetFramesInFlight());
    const auto view = gpuAssets.GetView(asset);
    ASSERT_TRUE(view.has_value());

    Graphics::Components::GpuSceneSlot slot{};
    const auto first = Runtime::ObserveRenderableAssetGeneration(slot, asset, &gpuAssets);

    EXPECT_EQ(first.Status, Runtime::RuntimeRenderableAssetObservationStatus::RebindRequired);
    EXPECT_EQ(first.ObservedGeneration, view->Generation);
    EXPECT_EQ(slot.SourceAsset, asset);
    EXPECT_EQ(slot.LastSeenAssetGeneration, 0u);

    slot.UpdateLastSeenAssetGeneration(view->Generation);
    const auto second = Runtime::ObserveRenderableAssetGeneration(slot, asset, &gpuAssets);

    EXPECT_EQ(second.Status, Runtime::RuntimeRenderableAssetObservationStatus::UpToDate);
    EXPECT_EQ(second.ObservedGeneration, view->Generation);
    EXPECT_EQ(slot.LastSeenAssetGeneration, view->Generation);
}

