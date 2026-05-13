#include <cstdint>
#include <memory>

#include <gtest/gtest.h>
#include <glm/glm.hpp>

import Extrinsic.Core.Config.Engine;
import Extrinsic.ECS.Components.AssetInstance;
import Extrinsic.ECS.Component.ProceduralGeometryRef;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.Component.GpuSceneSlot;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.RenderExtraction;

using Extrinsic::ECS::EntityHandle;
using Extrinsic::ECS::Scene::Registry;

namespace
{
    class StubApplication final : public Extrinsic::Runtime::IApplication
    {
    public:
        void OnInitialize(Extrinsic::Runtime::Engine& /*engine*/) override {}
        void OnSimTick(Extrinsic::Runtime::Engine& /*engine*/, double /*fixedDt*/) override {}
        void OnVariableTick(Extrinsic::Runtime::Engine& /*engine*/,
                            double /*alpha*/,
                            double /*dt*/) override {}
        void OnShutdown(Extrinsic::Runtime::Engine& /*engine*/) override {}
    };

    [[nodiscard]] Extrinsic::Core::Config::EngineConfig HeadlessConfig()
    {
        Extrinsic::Core::Config::EngineConfig config{};
        config.ReferenceScene.Enabled = false;
        return config;
    }

    EntityHandle MakeProceduralRenderable(Registry& scene)
    {
        namespace E = Extrinsic::ECS::Components;
        namespace G = Extrinsic::Graphics::Components;
        const EntityHandle entity = scene.Create();
        auto& raw = scene.Raw();
        raw.emplace<E::Transform::WorldMatrix>(entity).Matrix = glm::mat4{1.f};
        raw.emplace<G::RenderSurface>(entity);
        raw.emplace<E::ProceduralGeometryRef>(entity);
        return entity;
    }
}

TEST(ProceduralGeometryExtraction, SingleRenderableProducesOneInstanceAndOneGeometry)
{
    Extrinsic::Runtime::Engine engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.Initialize();

    auto& scene = engine.GetScene();
    const EntityHandle entity = MakeProceduralRenderable(scene);
    (void)entity;

    Extrinsic::Runtime::RenderExtractionCache extraction;
    const auto stats = extraction.ExtractAndSubmit(scene,
                                                    engine.GetRenderer(),
                                                    &engine.GetGpuAssetCache());

    EXPECT_EQ(stats.CandidateRenderableCount, 1u);
    EXPECT_EQ(stats.AllocatedInstanceCount, 1u);
    EXPECT_EQ(stats.ProceduralRenderablesEnumerated, 1u);
    EXPECT_EQ(stats.ProceduralGeometryUploads, 1u);
    EXPECT_EQ(stats.ProceduralGeometryReuseHits, 0u);
    EXPECT_EQ(stats.ProceduralAndAssetSourceConflict, 0u);
    EXPECT_EQ(stats.ProceduralGeometryFailedPack, 0u);
    EXPECT_EQ(stats.ProceduralGeometryInvalidParams, 0u);
    EXPECT_EQ(stats.ProceduralGeometryMissingPacker, 0u);
    EXPECT_EQ(stats.SourceAssetObservationCount, 0u);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    EXPECT_EQ(gpuWorld.GetLiveInstanceCount(), 1u);
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 1u);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(ProceduralGeometryExtraction, TwoRenderablesSharingKeyShareGeometryAndDedup)
{
    Extrinsic::Runtime::Engine engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.Initialize();

    auto& scene = engine.GetScene();
    (void)MakeProceduralRenderable(scene);
    (void)MakeProceduralRenderable(scene);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    const auto stats = extraction.ExtractAndSubmit(scene,
                                                    engine.GetRenderer(),
                                                    &engine.GetGpuAssetCache());

    EXPECT_EQ(stats.CandidateRenderableCount, 2u);
    EXPECT_EQ(stats.AllocatedInstanceCount, 2u);
    EXPECT_EQ(stats.ProceduralRenderablesEnumerated, 2u);
    EXPECT_EQ(stats.ProceduralGeometryUploads, 1u);
    EXPECT_EQ(stats.ProceduralGeometryReuseHits, 1u);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    EXPECT_EQ(gpuWorld.GetLiveInstanceCount(), 2u);
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 1u);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(ProceduralGeometryExtraction, GpuWorldReportsBoundGeometryForProceduralInstance)
{
    Extrinsic::Runtime::Engine engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.Initialize();

    auto& scene = engine.GetScene();
    const EntityHandle entity = MakeProceduralRenderable(scene);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    const auto stats = extraction.ExtractAndSubmit(scene,
                                                    engine.GetRenderer(),
                                                    &engine.GetGpuAssetCache());
    ASSERT_EQ(stats.AllocatedInstanceCount, 1u);
    ASSERT_EQ(stats.ProceduralGeometryUploads, 1u);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    ASSERT_EQ(gpuWorld.GetLiveInstanceCount(), 1u);
    ASSERT_EQ(gpuWorld.GetLiveGeometryCount(), 1u);

    const auto stableId = static_cast<std::uint32_t>(entity);
    const auto view = extraction.FindRenderableSidecarForTest(stableId);
    ASSERT_TRUE(view.has_value());
    EXPECT_TRUE(view->Instance.IsValid());
    EXPECT_TRUE(view->Geometry.IsValid());
    ASSERT_TRUE(view->ProceduralKey.has_value());
    EXPECT_EQ(view->ProceduralKey->Kind,
              Extrinsic::ECS::Components::ProceduralGeometryKind::Triangle);
    EXPECT_FALSE(view->HasSourceAsset);

    const auto bound = gpuWorld.GetInstanceGeometry(view->Instance);
    EXPECT_EQ(bound, view->Geometry);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(ProceduralGeometryExtraction, AssetAndProceduralSourcesOnSameEntityIncrementConflict)
{
    namespace E = Extrinsic::ECS::Components;
    namespace G = Extrinsic::Graphics::Components;

    Extrinsic::Runtime::Engine engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.Initialize();

    auto& scene = engine.GetScene();
    auto& raw = scene.Raw();
    const EntityHandle entity = scene.Create();
    raw.emplace<E::Transform::WorldMatrix>(entity).Matrix = glm::mat4{1.f};
    raw.emplace<G::RenderSurface>(entity);
    raw.emplace<E::ProceduralGeometryRef>(entity);
    raw.emplace<E::AssetInstance::Source>(entity).AssetId = 17u;

    Extrinsic::Runtime::RenderExtractionCache extraction;
    const auto stats = extraction.ExtractAndSubmit(scene,
                                                    engine.GetRenderer(),
                                                    &engine.GetGpuAssetCache());

    EXPECT_EQ(stats.CandidateRenderableCount, 1u);
    EXPECT_EQ(stats.ProceduralRenderablesEnumerated, 1u);
    EXPECT_EQ(stats.ProceduralAndAssetSourceConflict, 1u);
    EXPECT_EQ(stats.ProceduralGeometryUploads, 0u);
    EXPECT_EQ(stats.SourceAssetObservationCount, 1u);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    EXPECT_EQ(gpuWorld.GetLiveInstanceCount(), 1u);
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 0u);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(ProceduralGeometryExtraction, ProceduralSourceClearsSlotSourceAssetSentinel)
{
    Extrinsic::Runtime::Engine engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.Initialize();

    auto& scene = engine.GetScene();
    (void)MakeProceduralRenderable(scene);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    const auto stats = extraction.ExtractAndSubmit(scene,
                                                    engine.GetRenderer(),
                                                    &engine.GetGpuAssetCache());

    ASSERT_EQ(stats.ProceduralGeometryUploads, 1u);
    EXPECT_EQ(stats.SourceAssetObservationCount, 0u);
    EXPECT_EQ(stats.SourceAssetRebindRequiredCount, 0u);
    EXPECT_EQ(stats.SourceAssetCacheUnavailableCount, 0u);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}
