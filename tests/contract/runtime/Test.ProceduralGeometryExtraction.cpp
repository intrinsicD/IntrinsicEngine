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
import Extrinsic.Runtime.StableEntityLookup;

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
        config.Simulation.WorkerThreadCount = 1u;
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

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
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

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
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

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
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

    const auto stableId = Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity);
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

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
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

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
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

TEST(ProceduralGeometryExtraction, EntityDestructionRetiresGeometryAfterDeferredWindow)
{
    Extrinsic::Runtime::Engine engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.Initialize();

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    const EntityHandle entity = MakeProceduralRenderable(scene);

    Extrinsic::Runtime::RenderExtractionCache extraction;

    // First tick: upload the procedural geometry.
    auto stats = extraction.ExtractAndSubmit(scene,
                                              engine.GetRenderer(),
                                              &engine.GetGpuAssetCache());
    ASSERT_EQ(stats.ProceduralGeometryUploads, 1u);
    ASSERT_EQ(stats.ProceduralGeometryReleases, 0u);
    ASSERT_EQ(stats.ProceduralGeometryFreeRetires, 0u);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    ASSERT_EQ(gpuWorld.GetLiveGeometryCount(), 1u);

    // Destroy the only entity referencing the geometry.
    scene.Destroy(entity);

    // Second tick: extraction observes no live renderable, decrements the
    // cache refcount to zero, and enqueues the geometry for deferred retire.
    stats = extraction.ExtractAndSubmit(scene,
                                         engine.GetRenderer(),
                                         &engine.GetGpuAssetCache());
    EXPECT_EQ(stats.FreedInstanceCount, 1u);
    EXPECT_EQ(stats.ProceduralGeometryReleases, 1u);
    EXPECT_EQ(stats.ProceduralGeometryFreeRetires, 0u);
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 1u);

    // Drive the deferred-retire window: anchor tick + framesInFlight idle
    // ticks before the free fires.  GpuAssetCache::Tick uses the same
    // semantics (`deadline = currentFrame + framesInFlight`,
    // free when `deadline <= currentFrame`).
    constexpr std::uint32_t framesInFlight = 2u;
    constexpr std::uint64_t baseFrame = 200u;
    for (std::uint32_t i = 0; i <= framesInFlight; ++i)
    {
        extraction.TickProceduralGeometry(baseFrame + i, framesInFlight, engine.GetRenderer());
        if (i < framesInFlight)
        {
            EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 1u);
        }
    }
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 0u);

    // The next ExtractAndSubmit surfaces the FreeRetires delta.
    stats = extraction.ExtractAndSubmit(scene,
                                         engine.GetRenderer(),
                                         &engine.GetGpuAssetCache());
    EXPECT_EQ(stats.ProceduralGeometryFreeRetires, 1u);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(ProceduralGeometryExtraction, RecreateProceduralEntityCancelsRetireAndKeepsHandle)
{
    Extrinsic::Runtime::Engine engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.Initialize();

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    const EntityHandle first = MakeProceduralRenderable(scene);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    auto stats = extraction.ExtractAndSubmit(scene,
                                              engine.GetRenderer(),
                                              &engine.GetGpuAssetCache());
    ASSERT_EQ(stats.ProceduralGeometryUploads, 1u);

    const auto firstView =
        extraction.FindRenderableSidecarForTest(Extrinsic::Runtime::StableEntityLookup::ToRenderId(first));
    ASSERT_TRUE(firstView.has_value());
    const auto originalHandle = firstView->Geometry;
    ASSERT_TRUE(originalHandle.IsValid());

    scene.Destroy(first);
    stats = extraction.ExtractAndSubmit(scene,
                                         engine.GetRenderer(),
                                         &engine.GetGpuAssetCache());
    ASSERT_EQ(stats.ProceduralGeometryReleases, 1u);
    ASSERT_EQ(stats.ProceduralGeometryFreeRetires, 0u);

    // Anchor the deadline so the resurrection is observably inside the window.
    extraction.TickProceduralGeometry(300u, /*framesInFlight=*/3u, engine.GetRenderer());

    // Resurrect: a fresh entity with the same procedural params.
    const EntityHandle second = MakeProceduralRenderable(scene);
    stats = extraction.ExtractAndSubmit(scene,
                                         engine.GetRenderer(),
                                         &engine.GetGpuAssetCache());
    EXPECT_EQ(stats.ProceduralGeometryUploads, 0u);
    EXPECT_EQ(stats.ProceduralGeometryRetireCancellations, 1u);
    EXPECT_EQ(stats.ProceduralGeometryFreeRetires, 0u);

    const auto secondView =
        extraction.FindRenderableSidecarForTest(Extrinsic::Runtime::StableEntityLookup::ToRenderId(second));
    ASSERT_TRUE(secondView.has_value());
    EXPECT_EQ(secondView->Geometry, originalHandle);

    // Advance past the original deadline: the cancellation must hold.
    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    for (std::uint32_t i = 0; i < 5u; ++i)
    {
        extraction.TickProceduralGeometry(310u + i, 3u, engine.GetRenderer());
    }
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 1u);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}
