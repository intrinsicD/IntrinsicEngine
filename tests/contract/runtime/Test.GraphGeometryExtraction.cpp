#include <cstdint>
#include <limits>
#include <memory>
#include <string>

#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include "GeometryResidencyFingerprint.hpp"

import Extrinsic.Core.Config.Engine;
import Extrinsic.ECS.Components.AssetInstance;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Component.ProceduralGeometryRef;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.Component.GpuSceneSlot;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.AssetWorkflowModule;
import Extrinsic.Runtime.SceneDocumentModule;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.StableEntityLookup;
import Geometry.Graph;
import Geometry.Properties;

namespace gs = Extrinsic::ECS::Components::GeometrySources;
namespace pn = Extrinsic::ECS::Components::GeometrySources::PropertyNames;

using Extrinsic::ECS::EntityHandle;
using Extrinsic::ECS::Scene::Registry;

namespace
{
    template <typename T>
    [[nodiscard]] T& RequiredEngineService(
        Extrinsic::Runtime::Engine& engine)
    {
        T* const service = engine.Services().Find<T>();
        EXPECT_NE(service, nullptr);
        return *service;
    }

    void InitializeAssetWorkflowEngine(
        Extrinsic::Runtime::Engine& engine)
    {
        engine.EmplaceModule<
            Extrinsic::Runtime::SceneDocumentModule>();
        engine.EmplaceModule<
            Extrinsic::Runtime::AssetWorkflowModule>();
        engine.Initialize();
    }

    [[nodiscard]] Extrinsic::Core::Config::EngineConfig HeadlessConfig()
    {
        Extrinsic::Core::Config::EngineConfig config{};
        config.Simulation.WorkerThreadCount = 1u;
        config.ReferenceScene.Enabled = false;
        return config;
    }

    // Attach the canonical graph source matrix (Vertices + Halfedges + Edges)
    // and separate provenance marker so `BuildConstView` resolves Graph.
    void AttachLineGraphSources(Registry& scene, EntityHandle entity)
    {
        auto& raw = scene.Raw();
        Geometry::Graph::Graph graph{};
        const auto v0 = graph.AddVertex({0.0f, 0.0f, 0.0f});
        const auto v1 = graph.AddVertex({1.0f, 0.0f, 0.0f});
        const auto v2 = graph.AddVertex({0.0f, 1.0f, 0.0f});
        (void)graph.AddEdge(v0, v1);
        (void)graph.AddEdge(v1, v2);
        gs::PopulateFromGraph(raw, entity, graph);
    }

    void AttachPointGraphSources(Registry& scene, EntityHandle entity)
    {
        auto& raw = scene.Raw();
        Geometry::Graph::Graph graph{};
        (void)graph.AddVertex({0.0f, 0.0f, 0.0f});
        (void)graph.AddVertex({1.0f, 0.0f, 0.0f});
        gs::PopulateFromGraph(raw, entity, graph);
    }

    EntityHandle MakeLineGraphRenderable(Registry& scene)
    {
        namespace E = Extrinsic::ECS::Components;
        namespace G = Extrinsic::Graphics::Components;
        const EntityHandle entity = scene.Create();
        auto& raw = scene.Raw();
        raw.emplace<E::Transform::WorldMatrix>(entity).Matrix = glm::mat4{1.f};
        raw.emplace<G::RenderEdges>(entity);
        AttachLineGraphSources(scene, entity);
        return entity;
    }

    EntityHandle MakePointGraphRenderable(Registry& scene)
    {
        namespace E = Extrinsic::ECS::Components;
        namespace G = Extrinsic::Graphics::Components;
        const EntityHandle entity = scene.Create();
        auto& raw = scene.Raw();
        raw.emplace<E::Transform::WorldMatrix>(entity).Matrix = glm::mat4{1.f};
        raw.emplace<G::RenderPoints>(entity);
        AttachPointGraphSources(scene, entity);
        return entity;
    }

    EntityHandle MakeLineAndPointGraphRenderable(Registry& scene)
    {
        namespace E = Extrinsic::ECS::Components;
        namespace G = Extrinsic::Graphics::Components;
        const EntityHandle entity = scene.Create();
        auto& raw = scene.Raw();
        raw.emplace<E::Transform::WorldMatrix>(entity).Matrix = glm::mat4{1.f};
        raw.emplace<G::RenderEdges>(entity);
        raw.emplace<G::RenderPoints>(entity);
        AttachLineGraphSources(scene, entity);
        return entity;
    }
}

TEST(GraphGeometryExtraction, LineGraphUploadsOnceAndBindsInstanceGeometry)
{
    Extrinsic::Runtime::Engine engine(HeadlessConfig());
    InitializeAssetWorkflowEngine(engine);

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    const EntityHandle entity = MakeLineGraphRenderable(scene);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    const auto stats = extraction.ExtractAndSubmit(scene,
                                                    engine.GetRenderer(),
                                                    &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));

    EXPECT_EQ(stats.CandidateRenderableCount, 1u);
    EXPECT_EQ(stats.AllocatedInstanceCount, 1u);
    EXPECT_EQ(stats.GraphGeometryUploads, 1u);
    EXPECT_EQ(stats.GraphGeometryReuseHits, 0u);
    EXPECT_EQ(stats.GraphGeometryFailedPack, 0u);
    EXPECT_EQ(stats.GraphGeometryMissingNodes, 0u);
    EXPECT_EQ(stats.GraphGeometryInvalidEdges, 0u);
    EXPECT_EQ(stats.GraphGeometryReleases, 0u);
    // The mesh bridge must not fire on a graph-domain entity.
    EXPECT_EQ(stats.MeshGeometryUploads, 0u);
    EXPECT_EQ(stats.MeshGeometryFailedPack, 0u);
    EXPECT_EQ(stats.SourceAssetObservationCount, 0u);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    EXPECT_EQ(gpuWorld.GetLiveInstanceCount(), 1u);
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 1u);

    const auto stableId = Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity);
    const auto view = extraction.FindRenderableSidecarForTest(stableId);
    ASSERT_TRUE(view.has_value());
    EXPECT_TRUE(view->Instance.IsValid());
    EXPECT_TRUE(view->Geometry.IsValid());
    EXPECT_TRUE(view->HasGraphResidency);
    EXPECT_FALSE(view->HasMeshResidency);
    EXPECT_EQ(view->GraphGeometry, view->Geometry);
    EXPECT_FALSE(view->HasProceduralResidency);
    EXPECT_FALSE(view->HasSourceAsset);
    EXPECT_EQ(gpuWorld.GetInstanceGeometry(view->Instance), view->Geometry);

    Extrinsic::Graphics::GpuGeometryResidencyView residency{};
    ASSERT_TRUE(gpuWorld.TryGetGeometryResidencyView(
        view->GraphGeometry, residency));
    EXPECT_EQ(residency.VertexCount, 3u);
    EXPECT_EQ(residency.SurfaceIndexCount, 0u);
    EXPECT_EQ(residency.Record.LineIndexCount, 4u);
    EXPECT_EQ(residency.PositionByteCount, sizeof(float) * 9u);
    EXPECT_EQ(residency.TexcoordByteCount, sizeof(float) * 6u);
    EXPECT_EQ(residency.NormalByteCount, 0u);
    EXPECT_EQ(residency.PositionFingerprint,
              Extrinsic::Tests::GeometryFloat32Fingerprint(
                  {0.0f, 0.0f, 0.0f,
                   1.0f, 0.0f, 0.0f,
                   0.0f, 1.0f, 0.0f}));
    EXPECT_EQ(residency.TexcoordFingerprint,
              Extrinsic::Tests::GeometryFloat32Fingerprint(
                  {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}));
    EXPECT_EQ(residency.NormalFingerprint, 0u);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(GraphGeometryExtraction, PointGraphUploadsOnceAndBindsInstanceGeometry)
{
    Extrinsic::Runtime::Engine engine(HeadlessConfig());
    InitializeAssetWorkflowEngine(engine);

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    const EntityHandle entity = MakePointGraphRenderable(scene);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    const auto stats = extraction.ExtractAndSubmit(scene,
                                                    engine.GetRenderer(),
                                                    &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));

    EXPECT_EQ(stats.GraphGeometryUploads, 1u);
    EXPECT_EQ(stats.GraphGeometryFailedPack, 0u);
    EXPECT_EQ(stats.GraphGeometryMissingNodes, 0u);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 1u);

    const auto stableId = Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity);
    const auto view = extraction.FindRenderableSidecarForTest(stableId);
    ASSERT_TRUE(view.has_value());
    EXPECT_TRUE(view->HasGraphResidency);
    EXPECT_EQ(gpuWorld.GetInstanceGeometry(view->Instance), view->GraphGeometry);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(GraphGeometryExtraction, LineAndPointGraphUploadsSingleHandleForBothLanes)
{
    Extrinsic::Runtime::Engine engine(HeadlessConfig());
    InitializeAssetWorkflowEngine(engine);

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    const EntityHandle entity = MakeLineAndPointGraphRenderable(scene);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    const auto stats = extraction.ExtractAndSubmit(scene,
                                                    engine.GetRenderer(),
                                                    &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));

    // Both lanes share one upload / one geometry handle, but line and point
    // lanes use separate instances so their visualization configs can diverge.
    EXPECT_EQ(stats.GraphGeometryUploads, 1u);
    EXPECT_EQ(stats.GraphGeometryFailedPack, 0u);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    EXPECT_EQ(gpuWorld.GetLiveInstanceCount(), 2u);
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 1u);

    const auto stableId = Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity);
    const auto view = extraction.FindRenderableSidecarForTest(stableId);
    ASSERT_TRUE(view.has_value());
    EXPECT_TRUE(view->HasGraphResidency);
    EXPECT_EQ(view->GraphGeometry, view->Geometry);
    EXPECT_EQ(gpuWorld.GetInstanceGeometry(view->Instance), view->GraphGeometry);
    EXPECT_TRUE(view->HasGraphPointLaneInstance);
    EXPECT_EQ(gpuWorld.GetInstanceGeometry(view->GraphPointLaneInstance),
              view->GraphGeometry);

    const auto gpuAvailability = extraction.FindGpuRenderableAvailability(stableId);
    ASSERT_TRUE(gpuAvailability.has_value());
    EXPECT_TRUE(gpuAvailability->HasRenderable);
    EXPECT_FALSE(gpuAvailability->Surface.HasInstance);
    EXPECT_FALSE(gpuAvailability->Surface.HasGeometry);
    EXPECT_TRUE(gpuAvailability->Edges.HasInstance);
    EXPECT_TRUE(gpuAvailability->Edges.HasGeometry);
    EXPECT_TRUE(gpuAvailability->Points.HasInstance);
    EXPECT_TRUE(gpuAvailability->Points.HasGeometry);
    EXPECT_EQ(gpuAvailability->Edges.Instance, view->Instance);
    EXPECT_EQ(gpuAvailability->Edges.Geometry, view->GraphGeometry);
    EXPECT_EQ(gpuAvailability->Points.Instance, view->GraphPointLaneInstance);
    EXPECT_EQ(gpuAvailability->Points.Geometry, view->GraphGeometry);
    EXPECT_EQ(gpuAvailability->NamedBufferCount, 0u);
    EXPECT_FALSE(gpuAvailability->HasPositionsBuffer);
    EXPECT_FALSE(gpuAvailability->HasEdgesBuffer);
    EXPECT_FALSE(gpuAvailability->HasSizesBuffer);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(GraphGeometryExtraction, LaneOverridesColorLineAndPointLanesIndependently)
{
    namespace G = Extrinsic::Graphics::Components;

    Extrinsic::Runtime::Engine engine(HeadlessConfig());
    InitializeAssetWorkflowEngine(engine);

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    const EntityHandle entity = MakeLineAndPointGraphRenderable(scene);
    auto& raw = scene.Raw();

    G::VisualizationConfig base{};
    base.Source = G::VisualizationConfig::ColorSource::UniformColor;
    base.Color = glm::vec4{1.0f, 0.0f, 0.0f, 1.0f};
    raw.emplace<G::VisualizationConfig>(entity, base);

    G::VisualizationLaneOverrides overrides{};
    overrides.Edges = base;
    overrides.Edges->Color = glm::vec4{0.0f, 0.0f, 1.0f, 1.0f};
    overrides.Points = base;
    overrides.Points->Color = glm::vec4{0.0f, 0.75f, 0.25f, 1.0f};
    raw.emplace<G::VisualizationLaneOverrides>(entity, overrides);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    const auto stats = extraction.ExtractAndSubmit(scene,
                                                   engine.GetRenderer(),
                                                   &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));

    EXPECT_EQ(stats.GraphGeometryUploads, 1u);
    EXPECT_EQ(stats.AllocatedInstanceCount, 2u);
    EXPECT_EQ(stats.SubmittedTransformCount, 2u);
    EXPECT_EQ(stats.SubmittedVisualizationCount, 2u);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    const auto view =
        extraction.FindRenderableSidecarForTest(Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity));
    ASSERT_TRUE(view.has_value());
    EXPECT_TRUE(view->HasGraphPointLaneInstance);
    EXPECT_EQ(gpuWorld.GetInstanceGeometry(view->GraphPointLaneInstance),
              view->GraphGeometry);

    const auto lineConfig = gpuWorld.GetEntityConfigForTest(view->Instance);
    EXPECT_EQ(lineConfig.UniformColor, glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));

    const auto pointConfig =
        gpuWorld.GetEntityConfigForTest(view->GraphPointLaneInstance);
    EXPECT_EQ(pointConfig.UniformColor, glm::vec4(0.0f, 0.75f, 0.25f, 1.0f));

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(GraphGeometryExtraction, RepeatedExtractionReusesGraphHandleWithoutReupload)
{
    Extrinsic::Runtime::Engine engine(HeadlessConfig());
    InitializeAssetWorkflowEngine(engine);

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    const EntityHandle entity = MakeLineGraphRenderable(scene);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    auto stats = extraction.ExtractAndSubmit(scene,
                                             engine.GetRenderer(),
                                             &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));
    ASSERT_EQ(stats.GraphGeometryUploads, 1u);
    ASSERT_EQ(stats.GraphGeometryReuseHits, 0u);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    ASSERT_EQ(gpuWorld.GetLiveGeometryCount(), 1u);

    const auto firstView =
        extraction.FindRenderableSidecarForTest(Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity));
    ASSERT_TRUE(firstView.has_value());
    const auto firstHandle = firstView->GraphGeometry;

    stats = extraction.ExtractAndSubmit(scene,
                                        engine.GetRenderer(),
                                        &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));
    EXPECT_EQ(stats.GraphGeometryUploads, 0u);
    EXPECT_EQ(stats.GraphGeometryReuseHits, 1u);
    EXPECT_EQ(stats.GraphGeometryReleases, 0u);
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 1u);

    const auto secondView =
        extraction.FindRenderableSidecarForTest(Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity));
    ASSERT_TRUE(secondView.has_value());
    EXPECT_EQ(secondView->GraphGeometry, firstHandle);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(GraphGeometryExtraction, PropertyRevisionUpdatesResidentPositionsWithoutDirtyTag)
{
    namespace D = Extrinsic::ECS::Components::DirtyTags;

    Extrinsic::Runtime::Engine engine(HeadlessConfig());
    InitializeAssetWorkflowEngine(engine);

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    auto& raw = scene.Raw();
    const EntityHandle entity = MakeLineGraphRenderable(scene);
    const auto stableId =
        Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    auto stats = extraction.ExtractAndSubmit(
        scene,
        engine.GetRenderer(),
        &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));
    ASSERT_EQ(stats.GraphGeometryUploads, 1u);
    const auto firstView = extraction.FindRenderableSidecarForTest(stableId);
    ASSERT_TRUE(firstView.has_value());
    const auto handle = firstView->GraphGeometry;
    Extrinsic::Graphics::GpuGeometryResidencyView firstResidency{};
    ASSERT_TRUE(engine.GetRenderer().GetGpuWorld().TryGetGeometryResidencyView(
        handle, firstResidency));

    auto positions = raw.get<gs::Vertices>(entity)
                         .Properties.Get<glm::vec3>(pn::kPosition);
    ASSERT_TRUE(positions.IsValid());
    positions[1] = glm::vec3{3.0f, -1.0f, 0.25f};
    EXPECT_FALSE((raw.any_of<D::GpuDirty,
                             D::DirtyVertexPositions,
                             D::DirtyVertexAttributes>(entity)));

    stats = extraction.ExtractAndSubmit(
        scene,
        engine.GetRenderer(),
        &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));
    EXPECT_EQ(stats.GraphGeometryReuploads, 1u);
    EXPECT_EQ(stats.GraphGeometryPartialUploads, 1u);
    EXPECT_EQ(stats.GraphGeometryReuseHits, 0u);

    const auto secondView = extraction.FindRenderableSidecarForTest(stableId);
    ASSERT_TRUE(secondView.has_value());
    EXPECT_EQ(secondView->GraphGeometry, handle);
    Extrinsic::Graphics::GpuGeometryResidencyView secondResidency{};
    ASSERT_TRUE(engine.GetRenderer().GetGpuWorld().TryGetGeometryResidencyView(
        handle, secondResidency));
    EXPECT_NE(secondResidency.PositionFingerprint,
              firstResidency.PositionFingerprint);
    EXPECT_EQ(secondResidency.PositionFingerprint,
              Extrinsic::Tests::GeometryFloat32Fingerprint(
                  {0.0f, 0.0f, 0.0f,
                   3.0f, -1.0f, 0.25f,
                   0.0f, 1.0f, 0.0f}));

    stats = extraction.ExtractAndSubmit(
        scene,
        engine.GetRenderer(),
        &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));
    EXPECT_EQ(stats.GraphGeometryReuploads, 0u);
    EXPECT_EQ(stats.GraphGeometryReuseHits, 1u);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(GraphGeometryExtraction, TwoGraphEntitiesAllocateIndependentUploads)
{
    Extrinsic::Runtime::Engine engine(HeadlessConfig());
    InitializeAssetWorkflowEngine(engine);

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    (void)MakeLineGraphRenderable(scene);
    (void)MakePointGraphRenderable(scene);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    const auto stats = extraction.ExtractAndSubmit(scene,
                                                    engine.GetRenderer(),
                                                    &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));

    EXPECT_EQ(stats.CandidateRenderableCount, 2u);
    EXPECT_EQ(stats.AllocatedInstanceCount, 2u);
    EXPECT_EQ(stats.GraphGeometryUploads, 2u);
    EXPECT_EQ(stats.GraphGeometryReuseHits, 0u);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    EXPECT_EQ(gpuWorld.GetLiveInstanceCount(), 2u);
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 2u);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(GraphGeometryExtraction, EntityDestructionRetiresGraphGeometryAfterDeferredWindow)
{
    Extrinsic::Runtime::Engine engine(HeadlessConfig());
    InitializeAssetWorkflowEngine(engine);

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    const EntityHandle entity = MakeLineGraphRenderable(scene);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    auto stats = extraction.ExtractAndSubmit(scene,
                                             engine.GetRenderer(),
                                             &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));
    ASSERT_EQ(stats.GraphGeometryUploads, 1u);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    ASSERT_EQ(gpuWorld.GetLiveGeometryCount(), 1u);

    scene.Destroy(entity);

    stats = extraction.ExtractAndSubmit(scene,
                                        engine.GetRenderer(),
                                        &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));
    EXPECT_EQ(stats.FreedInstanceCount, 1u);
    EXPECT_EQ(stats.GraphGeometryReleases, 1u);
    EXPECT_EQ(stats.GraphGeometryFreeRetires, 0u);
    // Release is deferred through the framesInFlight window; the upload is
    // still live until TickGeometryResidency fires past the deadline.
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 1u);
    EXPECT_EQ(gpuWorld.GetLiveInstanceCount(), 0u);

    constexpr std::uint32_t framesInFlight = 2u;
    constexpr std::uint64_t baseFrame = 200u;
    for (std::uint32_t i = 0; i <= framesInFlight; ++i)
    {
        extraction.TickGeometryResidency(baseFrame + i, framesInFlight, engine.GetRenderer());
        if (i < framesInFlight)
        {
            EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 1u);
        }
    }
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 0u);

    stats = extraction.ExtractAndSubmit(scene,
                                        engine.GetRenderer(),
                                        &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));
    EXPECT_EQ(stats.GraphGeometryFreeRetires, 1u);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(GraphGeometryExtraction, ShutdownReleasesPendingGraphResidency)
{
    Extrinsic::Runtime::Engine engine(HeadlessConfig());
    InitializeAssetWorkflowEngine(engine);

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    (void)MakeLineGraphRenderable(scene);
    (void)MakePointGraphRenderable(scene);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    auto stats = extraction.ExtractAndSubmit(scene,
                                             engine.GetRenderer(),
                                             &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));
    ASSERT_EQ(stats.GraphGeometryUploads, 2u);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    ASSERT_EQ(gpuWorld.GetLiveGeometryCount(), 2u);

    extraction.Shutdown(engine.GetRenderer());
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 0u);
    EXPECT_EQ(gpuWorld.GetLiveInstanceCount(), 0u);
    EXPECT_EQ(extraction.GetLastStats().GraphGeometryReleases, 2u);

    engine.Shutdown();
}

TEST(GraphGeometryExtraction, ProceduralRefPreemptsGraphPathOnSameEntity)
{
    namespace E = Extrinsic::ECS::Components;
    namespace G = Extrinsic::Graphics::Components;

    Extrinsic::Runtime::Engine engine(HeadlessConfig());
    InitializeAssetWorkflowEngine(engine);

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    auto& raw = scene.Raw();
    const EntityHandle entity = scene.Create();
    raw.emplace<E::Transform::WorldMatrix>(entity).Matrix = glm::mat4{1.f};
    raw.emplace<G::RenderEdges>(entity);
    raw.emplace<E::ProceduralGeometryRef>(entity);
    AttachLineGraphSources(scene, entity);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    const auto stats = extraction.ExtractAndSubmit(scene,
                                                    engine.GetRenderer(),
                                                    &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));

    // Procedural intent declared → graph path must not run.
    EXPECT_EQ(stats.ProceduralRenderablesEnumerated, 1u);
    EXPECT_EQ(stats.ProceduralGeometryUploads, 1u);
    EXPECT_EQ(stats.GraphGeometryUploads, 0u);
    EXPECT_EQ(stats.GraphGeometryFailedPack, 0u);

    const auto view = extraction.FindRenderableSidecarForTest(
        Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity));
    ASSERT_TRUE(view.has_value());
    EXPECT_FALSE(view->HasGraphResidency);
    EXPECT_TRUE(view->HasProceduralResidency);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(GraphGeometryExtraction, MissingNodePositionsIncrementsMissingNodesCounter)
{
    namespace E = Extrinsic::ECS::Components;
    namespace G = Extrinsic::Graphics::Components;

    Extrinsic::Runtime::Engine engine(HeadlessConfig());
    InitializeAssetWorkflowEngine(engine);

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    auto& raw = scene.Raw();
    const EntityHandle entity = scene.Create();
    raw.emplace<E::Transform::WorldMatrix>(entity).Matrix = glm::mat4{1.f};
    raw.emplace<G::RenderPoints>(entity);
    // Graph-domain sources but the Vertices PropertySet carries no `v:position`.
    (void)raw.emplace<gs::Vertices>(entity);
    (void)raw.emplace<gs::Halfedges>(entity);
    (void)raw.emplace<gs::Edges>(entity);
    raw.emplace<gs::HasGraphTopology>(entity);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    const auto stats = extraction.ExtractAndSubmit(scene,
                                                    engine.GetRenderer(),
                                                    &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));

    EXPECT_EQ(stats.GraphGeometryUploads, 0u);
    EXPECT_EQ(stats.GraphGeometryMissingNodes, 1u);
    EXPECT_EQ(stats.GraphGeometryInvalidEdges, 0u);
    EXPECT_EQ(stats.GraphGeometryFailedPack, 0u);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 0u);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(GraphGeometryExtraction, OutOfRangeEdgeIncrementsInvalidEdgesCounter)
{
    namespace E = Extrinsic::ECS::Components;
    namespace G = Extrinsic::Graphics::Components;

    Extrinsic::Runtime::Engine engine(HeadlessConfig());
    InitializeAssetWorkflowEngine(engine);

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    auto& raw = scene.Raw();
    const EntityHandle entity = scene.Create();
    raw.emplace<E::Transform::WorldMatrix>(entity).Matrix = glm::mat4{1.f};
    raw.emplace<G::RenderEdges>(entity);
    Geometry::Graph::Graph graph{};
    const auto v0 = graph.AddVertex({0.0f, 0.0f, 0.0f});
    const auto v1 = graph.AddVertex({1.0f, 0.0f, 0.0f});
    (void)graph.AddEdge(v0, v1);
    gs::PopulateFromGraph(raw, entity, graph);
    auto& edges = raw.get<gs::Edges>(entity);
    // Endpoint 7 indexes past the two-node range.
    edges.Properties.Get<std::uint32_t>(pn::kEdgeV1).Vector()[0] = 7u;

    Extrinsic::Runtime::RenderExtractionCache extraction;
    const auto stats = extraction.ExtractAndSubmit(scene,
                                                    engine.GetRenderer(),
                                                    &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));

    EXPECT_EQ(stats.GraphGeometryUploads, 0u);
    EXPECT_EQ(stats.GraphGeometryInvalidEdges, 1u);
    EXPECT_EQ(stats.GraphGeometryMissingNodes, 0u);
    EXPECT_EQ(stats.GraphGeometryFailedPack, 0u);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(GraphGeometryExtraction, SurfaceOnlyGraphEntityFailsClosedAsFailedPack)
{
    namespace E = Extrinsic::ECS::Components;
    namespace G = Extrinsic::Graphics::Components;

    Extrinsic::Runtime::Engine engine(HeadlessConfig());
    InitializeAssetWorkflowEngine(engine);

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    auto& raw = scene.Raw();
    const EntityHandle entity = scene.Create();
    raw.emplace<E::Transform::WorldMatrix>(entity).Matrix = glm::mat4{1.f};
    // A graph-domain entity with only a surface hint requests neither lane,
    // so the graph plan builder returns NoRenderLane → folded into FailedPack.
    raw.emplace<G::RenderSurface>(entity);
    AttachLineGraphSources(scene, entity);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    const auto stats = extraction.ExtractAndSubmit(scene,
                                                    engine.GetRenderer(),
                                                    &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));

    EXPECT_EQ(stats.GraphGeometryUploads, 0u);
    EXPECT_EQ(stats.GraphGeometryFailedPack, 1u);
    EXPECT_EQ(stats.GraphGeometryMissingNodes, 0u);
    EXPECT_EQ(stats.GraphGeometryInvalidEdges, 0u);

    const auto view = extraction.FindRenderableSidecarForTest(
        Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity));
    ASSERT_TRUE(view.has_value());
    EXPECT_FALSE(view->HasGraphResidency);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(GraphGeometryExtraction, AddingProceduralRefAfterGraphUploadReleasesGraphResidency)
{
    namespace E = Extrinsic::ECS::Components;

    Extrinsic::Runtime::Engine engine(HeadlessConfig());
    InitializeAssetWorkflowEngine(engine);

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    const EntityHandle entity = MakeLineGraphRenderable(scene);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    auto stats = extraction.ExtractAndSubmit(scene,
                                             engine.GetRenderer(),
                                             &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));
    ASSERT_EQ(stats.GraphGeometryUploads, 1u);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    ASSERT_EQ(gpuWorld.GetLiveGeometryCount(), 1u);

    // Eligibility flip: a procedural ref appears on the same entity. The graph
    // path releases its cached upload and the procedural path takes over.
    scene.Raw().emplace<E::ProceduralGeometryRef>(entity);

    stats = extraction.ExtractAndSubmit(scene,
                                        engine.GetRenderer(),
                                        &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));
    EXPECT_EQ(stats.GraphGeometryUploads, 0u);
    EXPECT_EQ(stats.GraphGeometryReuseHits, 0u);
    EXPECT_EQ(stats.GraphGeometryReleases, 1u);
    EXPECT_EQ(stats.GraphGeometryFreeRetires, 0u);
    EXPECT_EQ(stats.ProceduralGeometryUploads, 1u);

    const auto view = extraction.FindRenderableSidecarForTest(
        Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity));
    ASSERT_TRUE(view.has_value());
    EXPECT_FALSE(view->HasGraphResidency);
    ASSERT_TRUE(view->HasProceduralResidency);
    // Instance bound to the procedural handle, not the queued graph slot.
    EXPECT_TRUE(view->Geometry.IsValid());
    EXPECT_EQ(gpuWorld.GetInstanceGeometry(view->Instance), view->Geometry);
    // Both the procedural slot and the queued graph slot are live during the
    // deferred-retire window.
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 2u);

    constexpr std::uint32_t framesInFlight = 2u;
    constexpr std::uint64_t baseFrame = 400u;
    for (std::uint32_t i = 0; i <= framesInFlight; ++i)
    {
        extraction.TickGeometryResidency(baseFrame + i, framesInFlight, engine.GetRenderer());
    }
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 1u);
    stats = extraction.ExtractAndSubmit(scene,
                                        engine.GetRenderer(),
                                        &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));
    EXPECT_EQ(stats.GraphGeometryFreeRetires, 1u);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(GraphGeometryExtraction, LosingGraphHintReleasesGraphResidency)
{
    namespace G = Extrinsic::Graphics::Components;

    Extrinsic::Runtime::Engine engine(HeadlessConfig());
    InitializeAssetWorkflowEngine(engine);

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    const EntityHandle entity = MakeLineGraphRenderable(scene);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    auto stats = extraction.ExtractAndSubmit(scene,
                                             engine.GetRenderer(),
                                             &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));
    ASSERT_EQ(stats.GraphGeometryUploads, 1u);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    ASSERT_EQ(gpuWorld.GetLiveGeometryCount(), 1u);

    // Eligibility flip: the entity drops its graph-domain topology so
    // `BuildConstView` no longer resolves `Domain::Graph`. The graph path
    // must release even though no procedural / asset path took over. A
    // `RenderSurface` hint keeps the entity a renderable candidate.
    auto& raw = scene.Raw();
    raw.remove<gs::Vertices>(entity);
    raw.remove<gs::Edges>(entity);
    raw.remove<gs::Halfedges>(entity);
    raw.remove<gs::HasGraphTopology>(entity);
    raw.remove<G::RenderEdges>(entity);
    raw.emplace<G::RenderSurface>(entity);

    stats = extraction.ExtractAndSubmit(scene,
                                        engine.GetRenderer(),
                                        &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));
    EXPECT_EQ(stats.GraphGeometryUploads, 0u);
    EXPECT_EQ(stats.GraphGeometryReuseHits, 0u);
    EXPECT_EQ(stats.GraphGeometryReleases, 1u);
    EXPECT_EQ(stats.GraphGeometryFreeRetires, 0u);

    const auto view = extraction.FindRenderableSidecarForTest(
        Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity));
    ASSERT_TRUE(view.has_value());
    EXPECT_FALSE(view->HasGraphResidency);
    EXPECT_FALSE(gpuWorld.GetInstanceGeometry(view->Instance).IsValid());
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 1u);

    constexpr std::uint32_t framesInFlight = 2u;
    constexpr std::uint64_t baseFrame = 600u;
    for (std::uint32_t i = 0; i <= framesInFlight; ++i)
    {
        extraction.TickGeometryResidency(baseFrame + i, framesInFlight, engine.GetRenderer());
    }
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 0u);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

// RUNTIME-086 — a change in requested render lanes must repack even when no
// geometry dirty tag is set, because the line lane's presence changes the
// packed upload. A points-only graph that later gains `RenderEdges` must
// repack (with line indices) rather than rebind the lineless cached upload.
TEST(GraphGeometryExtraction, GainingLineHintRepacksGraphWithoutDirtyTag)
{
    namespace E = Extrinsic::ECS::Components;
    namespace G = Extrinsic::Graphics::Components;
    namespace D = Extrinsic::ECS::Components::DirtyTags;

    Extrinsic::Runtime::Engine engine(HeadlessConfig());
    InitializeAssetWorkflowEngine(engine);

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    auto& raw = scene.Raw();
    const EntityHandle entity = scene.Create();
    raw.emplace<E::Transform::WorldMatrix>(entity).Matrix = glm::mat4{1.f};
    raw.emplace<G::RenderPoints>(entity);
    // Graph sources include Edges so a later line hint can pack, but only
    // RenderPoints is requested for the first upload (points-only lane mask).
    AttachLineGraphSources(scene, entity);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    auto stats = extraction.ExtractAndSubmit(scene,
                                             engine.GetRenderer(),
                                             &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));
    ASSERT_EQ(stats.GraphGeometryUploads, 1u);
    ASSERT_EQ(stats.GraphGeometryReuploads, 0u);

    const auto stableId = Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity);
    const auto firstView = extraction.FindRenderableSidecarForTest(stableId);
    ASSERT_TRUE(firstView.has_value());
    const auto firstHandle = firstView->GraphGeometry;
    ASSERT_TRUE(firstHandle.IsValid());

    // Gain a line hint. No geometry dirty tag is set, so the old reuse guard
    // would have rebound the lineless upload.
    raw.emplace<G::RenderEdges>(entity);
    ASSERT_FALSE((raw.any_of<D::GpuDirty,
                             D::DirtyVertexPositions,
                             D::DirtyVertexAttributes,
                             D::DirtyEdgeTopology>(entity)));

    stats = extraction.ExtractAndSubmit(scene,
                                        engine.GetRenderer(),
                                        &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));
    EXPECT_EQ(stats.GraphGeometryUploads, 0u);
    EXPECT_EQ(stats.GraphGeometryReuploads, 1u);
    EXPECT_EQ(stats.GraphGeometryReuseHits, 0u);
    EXPECT_EQ(stats.GraphGeometryReleases, 1u);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    const auto secondView = extraction.FindRenderableSidecarForTest(stableId);
    ASSERT_TRUE(secondView.has_value());
    EXPECT_TRUE(secondView->HasGraphResidency);
    EXPECT_NE(secondView->GraphGeometry, firstHandle);
    EXPECT_EQ(gpuWorld.GetInstanceGeometry(secondView->Instance), secondView->GraphGeometry);

    // The lane mask is now stable, so a clean re-extraction returns to reuse.
    stats = extraction.ExtractAndSubmit(scene,
                                        engine.GetRenderer(),
                                        &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));
    EXPECT_EQ(stats.GraphGeometryReuploads, 0u);
    EXPECT_EQ(stats.GraphGeometryReuseHits, 1u);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(GraphGeometryExtraction, LosingLineHintRepacksGraphWithoutDirtyTag)
{
    namespace G = Extrinsic::Graphics::Components;
    namespace D = Extrinsic::ECS::Components::DirtyTags;

    Extrinsic::Runtime::Engine engine(HeadlessConfig());
    InitializeAssetWorkflowEngine(engine);

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    const EntityHandle entity = MakeLineAndPointGraphRenderable(scene);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    auto stats = extraction.ExtractAndSubmit(scene,
                                             engine.GetRenderer(),
                                             &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));
    ASSERT_EQ(stats.GraphGeometryUploads, 1u);

    const auto stableId = Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity);
    const auto firstHandle =
        extraction.FindRenderableSidecarForTest(stableId)->GraphGeometry;

    // Drop the line lane; RenderPoints keeps the entity a graph renderable.
    auto& raw = scene.Raw();
    raw.remove<G::RenderEdges>(entity);
    ASSERT_FALSE((raw.any_of<D::GpuDirty,
                             D::DirtyVertexPositions,
                             D::DirtyVertexAttributes,
                             D::DirtyEdgeTopology>(entity)));

    stats = extraction.ExtractAndSubmit(scene,
                                        engine.GetRenderer(),
                                        &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));
    EXPECT_EQ(stats.GraphGeometryUploads, 0u);
    EXPECT_EQ(stats.GraphGeometryReuploads, 1u);
    EXPECT_EQ(stats.GraphGeometryReuseHits, 0u);

    const auto secondView = extraction.FindRenderableSidecarForTest(stableId);
    ASSERT_TRUE(secondView.has_value());
    EXPECT_TRUE(secondView->HasGraphResidency);
    EXPECT_NE(secondView->GraphGeometry, firstHandle);
    EXPECT_FALSE(secondView->HasGraphPointLaneInstance);

    const auto gpuAvailability = extraction.FindGpuRenderableAvailability(stableId);
    ASSERT_TRUE(gpuAvailability.has_value());
    EXPECT_FALSE(gpuAvailability->Surface.HasGeometry);
    EXPECT_FALSE(gpuAvailability->Edges.HasGeometry);
    EXPECT_TRUE(gpuAvailability->Points.HasInstance);
    EXPECT_TRUE(gpuAvailability->Points.HasGeometry);
    EXPECT_EQ(gpuAvailability->Points.Instance, secondView->Instance);
    EXPECT_EQ(gpuAvailability->Points.Geometry, secondView->GraphGeometry);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

namespace
{
    void DriveGraphDeferredRetireWindow(Extrinsic::Runtime::RenderExtractionCache& extraction,
                                        Extrinsic::Graphics::IRenderer& renderer,
                                        std::uint64_t baseFrame,
                                        std::uint32_t framesInFlight)
    {
        for (std::uint32_t i = 0; i <= framesInFlight; ++i)
        {
            extraction.TickGeometryResidency(baseFrame + i, framesInFlight, renderer);
        }
    }
}

// Graph dirty-domain coverage. Coarse GPU/topology tags still require a full
// replacement upload, while vertex-channel tags use the in-place partial
// channel upload path and keep the existing resident handle.
class GraphGeometryExtractionDirtyTag
    : public ::testing::TestWithParam<const char*>
{
};

TEST_P(GraphGeometryExtractionDirtyTag, DirtyTagTriggersReupload)
{
    namespace D = Extrinsic::ECS::Components::DirtyTags;

    Extrinsic::Runtime::Engine engine(HeadlessConfig());
    InitializeAssetWorkflowEngine(engine);

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    auto& raw = scene.Raw();
    const EntityHandle entity = MakeLineGraphRenderable(scene);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    auto stats = extraction.ExtractAndSubmit(scene,
                                             engine.GetRenderer(),
                                             &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));
    ASSERT_EQ(stats.GraphGeometryUploads, 1u);
    ASSERT_EQ(stats.GraphGeometryReuploads, 0u);

    const auto firstView =
        extraction.FindRenderableSidecarForTest(Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity));
    ASSERT_TRUE(firstView.has_value());
    const auto firstHandle = firstView->GraphGeometry;
    ASSERT_TRUE(firstHandle.IsValid());

    const std::string param{GetParam()};
    if (param == "GpuDirty")
        D::MarkGpuDirty(raw, entity);
    else if (param == "DirtyVertexPositions")
        D::MarkVertexPositionsDirty(raw, entity);
    else if (param == "DirtyVertexAttributes")
        D::MarkVertexAttributesDirty(raw, entity);
    else if (param == "DirtyVertexTexcoords")
        D::MarkVertexTexcoordsDirty(raw, entity);
    else if (param == "DirtyVertexNormals")
        D::MarkVertexNormalsDirty(raw, entity);
    else if (param == "DirtyVertexColors")
        D::MarkVertexColorsDirty(raw, entity);
    else if (param == "DirtyEdgeTopology")
        D::MarkEdgeTopologyDirty(raw, entity);
    else
        FAIL() << "Unhandled dirty-tag parameterization: " << param;

    const bool fullUploadExpected =
        param == "GpuDirty" || param == "DirtyEdgeTopology";

    stats = extraction.ExtractAndSubmit(scene,
                                        engine.GetRenderer(),
                                        &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));
    EXPECT_EQ(stats.GraphGeometryUploads, 0u);
    EXPECT_EQ(stats.GraphGeometryReuploads, 1u);
    EXPECT_EQ(stats.GraphGeometryPartialUploads, fullUploadExpected ? 0u : 1u);
    EXPECT_EQ(stats.GraphGeometryReuseHits, 0u);
    EXPECT_EQ(stats.GraphGeometryReleases, fullUploadExpected ? 1u : 0u);
    EXPECT_EQ(stats.GraphGeometryFreeRetires, 0u);

    const auto secondView =
        extraction.FindRenderableSidecarForTest(Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity));
    ASSERT_TRUE(secondView.has_value());
    EXPECT_TRUE(secondView->HasGraphResidency);
    if (fullUploadExpected)
    {
        EXPECT_NE(secondView->GraphGeometry, firstHandle);
    }
    else
    {
        EXPECT_EQ(secondView->GraphGeometry, firstHandle);
    }
    EXPECT_EQ(secondView->Geometry, secondView->GraphGeometry);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), fullUploadExpected ? 2u : 1u);
    EXPECT_EQ(gpuWorld.GetInstanceGeometry(secondView->Instance), secondView->GraphGeometry);

    EXPECT_FALSE((raw.any_of<D::GpuDirty,
                             D::DirtyVertexPositions,
                             D::DirtyVertexAttributes,
                             D::DirtyVertexTexcoords,
                             D::DirtyVertexNormals,
                             D::DirtyVertexColors,
                             D::DirtyEdgeTopology>(entity)));

    stats = extraction.ExtractAndSubmit(scene,
                                        engine.GetRenderer(),
                                        &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));
    EXPECT_EQ(stats.GraphGeometryReuploads, 0u);
    EXPECT_EQ(stats.GraphGeometryReuseHits, 1u);
    EXPECT_EQ(stats.GraphGeometryReleases, 0u);

    constexpr std::uint32_t framesInFlight = 2u;
    DriveGraphDeferredRetireWindow(extraction,
                                   engine.GetRenderer(),
                                   /*baseFrame=*/700u,
                                   framesInFlight);
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 1u);

    stats = extraction.ExtractAndSubmit(scene,
                                        engine.GetRenderer(),
                                        &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));
    EXPECT_EQ(stats.GraphGeometryFreeRetires, fullUploadExpected ? 1u : 0u);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

INSTANTIATE_TEST_SUITE_P(AllDirtyDomains,
                         GraphGeometryExtractionDirtyTag,
                         ::testing::Values("GpuDirty",
                                           "DirtyVertexPositions",
                                           "DirtyVertexAttributes",
                                           "DirtyVertexTexcoords",
                                           "DirtyVertexNormals",
                                           "DirtyVertexColors",
                                           "DirtyEdgeTopology"));

// RUNTIME-087 follow-up — a dirty-reupload pack failure on a graph entity that
// already has a valid upload must release the stale residency (fail-closed) so
// invalid node data does not keep rendering the last-good frame, while leaving
// the dirty tag set for later recovery.
TEST(GraphGeometryExtraction, ReuploadFailureReleasesStaleResidencyAndPreservesDirtyTag)
{
    namespace D = Extrinsic::ECS::Components::DirtyTags;

    Extrinsic::Runtime::Engine engine(HeadlessConfig());
    InitializeAssetWorkflowEngine(engine);

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    auto& raw = scene.Raw();
    const EntityHandle entity = MakeLineGraphRenderable(scene);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    auto stats = extraction.ExtractAndSubmit(scene,
                                             engine.GetRenderer(),
                                             &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));
    ASSERT_EQ(stats.GraphGeometryUploads, 1u);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    ASSERT_EQ(gpuWorld.GetLiveGeometryCount(), 1u);

    // Corrupt a node position to non-finite, then mark vertex positions dirty so
    // the next graph plan build returns NonFinitePosition (folded into FailedPack).
    auto& nodes = raw.get<gs::Vertices>(entity);
    auto pos = nodes.Properties.GetOrAdd<glm::vec3>(std::string{pn::kPosition}, glm::vec3(0.0f));
    pos.Vector() = {
        {0.0f, 0.0f, 0.0f},
        {std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
    };
    D::MarkVertexPositionsDirty(raw, entity);

    stats = extraction.ExtractAndSubmit(scene,
                                        engine.GetRenderer(),
                                        &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));
    EXPECT_EQ(stats.GraphGeometryReuploads, 0u);
    EXPECT_EQ(stats.GraphGeometryFailedPack, 1u);
    EXPECT_EQ(stats.GraphGeometryReuseHits, 0u);
    EXPECT_EQ(stats.GraphGeometryReleases, 1u);
    EXPECT_EQ(stats.GraphGeometryFreeRetires, 0u);

    const auto view =
        extraction.FindRenderableSidecarForTest(Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity));
    ASSERT_TRUE(view.has_value());
    EXPECT_FALSE(view->HasGraphResidency);
    EXPECT_FALSE(gpuWorld.GetInstanceGeometry(view->Instance).IsValid());
    EXPECT_TRUE(raw.any_of<D::DirtyVertexPositions>(entity));
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 1u);

    constexpr std::uint32_t framesInFlight = 2u;
    DriveGraphDeferredRetireWindow(extraction,
                                   engine.GetRenderer(),
                                   /*baseFrame=*/900u,
                                   framesInFlight);
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 0u);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}
