#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <gtest/gtest.h>

import Extrinsic.Core.Config.Engine;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Component.ProceduralGeometryRef;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.AssetWorkflowModule;
import Extrinsic.Runtime.SceneDocumentModule;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.StableEntityLookup;
import Geometry.PointCloud;
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

    void SetPositions(gs::Vertices& v, const std::vector<glm::vec3>& positions)
    {
        v.Properties.Resize(positions.size());
        auto pos = v.Properties.GetOrAdd<glm::vec3>(std::string{pn::kPosition}, glm::vec3(0.0f));
        pos.Vector() = positions;
    }

    // Attach a point-cloud `GeometrySources` (Vertices only) to `entity` so
    // `BuildConstView` resolves `Domain::PointCloud` — a point cloud carries no
    // edges/halfedges/faces/nodes, so the vertex source alone is sufficient.
    void AttachPointCloudSources(Registry& scene, EntityHandle entity)
    {
        auto& raw = scene.Raw();
        auto& vertices = raw.emplace<gs::Vertices>(entity);
        SetPositions(vertices, {
            {0.0f, 0.0f, 0.0f},
            {1.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f},
        });
    }

    EntityHandle MakePointCloudRenderable(Registry& scene)
    {
        namespace E = Extrinsic::ECS::Components;
        namespace G = Extrinsic::Graphics::Components;
        const EntityHandle entity = scene.Create();
        auto& raw = scene.Raw();
        raw.emplace<E::Transform::WorldMatrix>(entity).Matrix = glm::mat4{1.f};
        raw.emplace<G::RenderPoints>(entity);  // default uniform float SizeSource.
        AttachPointCloudSources(scene, entity);
        return entity;
    }
}

TEST(PointCloudGeometryExtraction, CloudUploadsOnceAndBindsInstanceGeometry)
{
    Extrinsic::Runtime::Engine engine(HeadlessConfig());
    InitializeAssetWorkflowEngine(engine);

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    const EntityHandle entity = MakePointCloudRenderable(scene);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    const auto stats = extraction.ExtractAndSubmit(scene,
                                                    engine.GetRenderer(),
                                                    &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));

    EXPECT_EQ(stats.CandidateRenderableCount, 1u);
    EXPECT_EQ(stats.AllocatedInstanceCount, 1u);
    EXPECT_EQ(stats.PointCloudGeometryUploads, 1u);
    EXPECT_EQ(stats.PointCloudGeometryReuseHits, 0u);
    EXPECT_EQ(stats.PointCloudGeometryFailedPack, 0u);
    EXPECT_EQ(stats.PointCloudGeometryMissingPositions, 0u);
    EXPECT_EQ(stats.PointCloudGeometryInvalidPoints, 0u);
    EXPECT_EQ(stats.PointCloudGeometryReleases, 0u);
    // Neither the mesh nor graph bridge must fire on a point-cloud entity.
    EXPECT_EQ(stats.MeshGeometryUploads, 0u);
    EXPECT_EQ(stats.GraphGeometryUploads, 0u);
    EXPECT_EQ(stats.SourceAssetObservationCount, 0u);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    EXPECT_EQ(gpuWorld.GetLiveInstanceCount(), 1u);
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 1u);

    const auto stableId = Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity);
    const auto view = extraction.FindRenderableSidecarForTest(stableId);
    ASSERT_TRUE(view.has_value());
    EXPECT_TRUE(view->Instance.IsValid());
    EXPECT_TRUE(view->Geometry.IsValid());
    EXPECT_TRUE(view->HasPointCloudResidency);
    EXPECT_FALSE(view->HasMeshResidency);
    EXPECT_FALSE(view->HasGraphResidency);
    EXPECT_EQ(view->PointCloudGeometry, view->Geometry);
    EXPECT_FALSE(view->ProceduralKey.has_value());
    EXPECT_FALSE(view->HasSourceAsset);
    EXPECT_EQ(gpuWorld.GetInstanceGeometry(view->Instance), view->Geometry);

    const auto gpuAvailability = extraction.FindGpuRenderableAvailability(stableId);
    ASSERT_TRUE(gpuAvailability.has_value());
    EXPECT_TRUE(gpuAvailability->HasRenderable);
    EXPECT_FALSE(gpuAvailability->Surface.HasInstance);
    EXPECT_FALSE(gpuAvailability->Surface.HasGeometry);
    EXPECT_FALSE(gpuAvailability->Edges.HasInstance);
    EXPECT_FALSE(gpuAvailability->Edges.HasGeometry);
    EXPECT_TRUE(gpuAvailability->Points.HasInstance);
    EXPECT_TRUE(gpuAvailability->Points.HasGeometry);
    EXPECT_EQ(gpuAvailability->Points.Instance, view->Instance);
    EXPECT_EQ(gpuAvailability->Points.Geometry, view->PointCloudGeometry);
    EXPECT_EQ(gpuAvailability->NamedBufferCount, 0u);
    EXPECT_FALSE(gpuAvailability->HasPositionsBuffer);
    EXPECT_FALSE(gpuAvailability->HasEdgesBuffer);
    EXPECT_FALSE(gpuAvailability->HasSizesBuffer);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(PointCloudGeometryExtraction, PopulateFromCloudResolvesPointCloudDomainAndUploads)
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

    // Populate via the real geometry-source path rather than hand-attached
    // PropertySets: build a small cloud and let PopulateFromCloud emplace the
    // Vertices source so DetectDomain resolves Domain::PointCloud.
    Geometry::PointCloud::Cloud cloud;
    cloud.AddPoint({0.0f, 0.0f, 0.0f});
    cloud.AddPoint({1.0f, 0.0f, 0.0f});
    cloud.AddPoint({0.0f, 1.0f, 0.0f});
    cloud.AddPoint({1.0f, 1.0f, 0.0f});
    gs::PopulateFromCloud(raw, entity, cloud);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    const auto stats = extraction.ExtractAndSubmit(scene,
                                                    engine.GetRenderer(),
                                                    &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));

    EXPECT_EQ(stats.PointCloudGeometryUploads, 1u);
    EXPECT_EQ(stats.PointCloudGeometryFailedPack, 0u);
    EXPECT_EQ(stats.PointCloudGeometryMissingPositions, 0u);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 1u);

    const auto view =
        extraction.FindRenderableSidecarForTest(Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity));
    ASSERT_TRUE(view.has_value());
    EXPECT_TRUE(view->HasPointCloudResidency);
    EXPECT_EQ(gpuWorld.GetInstanceGeometry(view->Instance), view->PointCloudGeometry);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(PointCloudGeometryExtraction, RepeatedExtractionReusesPointCloudHandleWithoutReupload)
{
    Extrinsic::Runtime::Engine engine(HeadlessConfig());
    InitializeAssetWorkflowEngine(engine);

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    const EntityHandle entity = MakePointCloudRenderable(scene);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    auto stats = extraction.ExtractAndSubmit(scene,
                                             engine.GetRenderer(),
                                             &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));
    ASSERT_EQ(stats.PointCloudGeometryUploads, 1u);
    ASSERT_EQ(stats.PointCloudGeometryReuseHits, 0u);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    ASSERT_EQ(gpuWorld.GetLiveGeometryCount(), 1u);

    const auto firstView =
        extraction.FindRenderableSidecarForTest(Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity));
    ASSERT_TRUE(firstView.has_value());
    const auto firstHandle = firstView->PointCloudGeometry;

    stats = extraction.ExtractAndSubmit(scene,
                                        engine.GetRenderer(),
                                        &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));
    EXPECT_EQ(stats.PointCloudGeometryUploads, 0u);
    EXPECT_EQ(stats.PointCloudGeometryReuseHits, 1u);
    EXPECT_EQ(stats.PointCloudGeometryReleases, 0u);
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 1u);

    const auto secondView =
        extraction.FindRenderableSidecarForTest(Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity));
    ASSERT_TRUE(secondView.has_value());
    EXPECT_EQ(secondView->PointCloudGeometry, firstHandle);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(PointCloudGeometryExtraction, TwoCloudEntitiesAllocateIndependentUploads)
{
    Extrinsic::Runtime::Engine engine(HeadlessConfig());
    InitializeAssetWorkflowEngine(engine);

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    (void)MakePointCloudRenderable(scene);
    (void)MakePointCloudRenderable(scene);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    const auto stats = extraction.ExtractAndSubmit(scene,
                                                    engine.GetRenderer(),
                                                    &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));

    EXPECT_EQ(stats.CandidateRenderableCount, 2u);
    EXPECT_EQ(stats.AllocatedInstanceCount, 2u);
    EXPECT_EQ(stats.PointCloudGeometryUploads, 2u);
    EXPECT_EQ(stats.PointCloudGeometryReuseHits, 0u);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    EXPECT_EQ(gpuWorld.GetLiveInstanceCount(), 2u);
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 2u);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(PointCloudGeometryExtraction, EntityDestructionRetiresPointCloudGeometryAfterDeferredWindow)
{
    Extrinsic::Runtime::Engine engine(HeadlessConfig());
    InitializeAssetWorkflowEngine(engine);

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    const EntityHandle entity = MakePointCloudRenderable(scene);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    auto stats = extraction.ExtractAndSubmit(scene,
                                             engine.GetRenderer(),
                                             &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));
    ASSERT_EQ(stats.PointCloudGeometryUploads, 1u);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    ASSERT_EQ(gpuWorld.GetLiveGeometryCount(), 1u);

    scene.Destroy(entity);

    stats = extraction.ExtractAndSubmit(scene,
                                        engine.GetRenderer(),
                                        &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));
    EXPECT_EQ(stats.FreedInstanceCount, 1u);
    EXPECT_EQ(stats.PointCloudGeometryReleases, 1u);
    EXPECT_EQ(stats.PointCloudGeometryFreeRetires, 0u);
    // Release is deferred through the framesInFlight window; the upload is
    // still live until TickPointCloudGeometry fires past the deadline.
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 1u);
    EXPECT_EQ(gpuWorld.GetLiveInstanceCount(), 0u);

    constexpr std::uint32_t framesInFlight = 2u;
    constexpr std::uint64_t baseFrame = 200u;
    for (std::uint32_t i = 0; i <= framesInFlight; ++i)
    {
        extraction.TickPointCloudGeometry(baseFrame + i, framesInFlight, engine.GetRenderer());
        if (i < framesInFlight)
        {
            EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 1u);
        }
    }
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 0u);

    stats = extraction.ExtractAndSubmit(scene,
                                        engine.GetRenderer(),
                                        &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));
    EXPECT_EQ(stats.PointCloudGeometryFreeRetires, 1u);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(PointCloudGeometryExtraction, CacheDestructionToleratesPendingDeferredRetireState)
{
    Extrinsic::Runtime::Engine engine(HeadlessConfig());
    InitializeAssetWorkflowEngine(engine);

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    const EntityHandle entity = MakePointCloudRenderable(scene);
    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();

    {
        Extrinsic::Runtime::RenderExtractionCache extraction;
        auto stats = extraction.ExtractAndSubmit(scene,
                                                 engine.GetRenderer(),
                                                 &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));
        ASSERT_EQ(stats.PointCloudGeometryUploads, 1u);

        scene.Destroy(entity);
        stats = extraction.ExtractAndSubmit(scene,
                                            engine.GetRenderer(),
                                            &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));
        ASSERT_EQ(stats.PointCloudGeometryReleases, 1u);
        ASSERT_EQ(gpuWorld.GetLiveInstanceCount(), 0u);
        ASSERT_EQ(gpuWorld.GetLiveGeometryCount(), 1u);
    }

    // Destruction safely drops the cache-owned retire record. Explicit
    // Shutdown remains the contract that releases renderer-owned residency.
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 1u);
    engine.Shutdown();
}

TEST(PointCloudGeometryExtraction, ShutdownReleasesPendingPointCloudResidency)
{
    Extrinsic::Runtime::Engine engine(HeadlessConfig());
    InitializeAssetWorkflowEngine(engine);

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    (void)MakePointCloudRenderable(scene);
    (void)MakePointCloudRenderable(scene);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    auto stats = extraction.ExtractAndSubmit(scene,
                                             engine.GetRenderer(),
                                             &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));
    ASSERT_EQ(stats.PointCloudGeometryUploads, 2u);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    ASSERT_EQ(gpuWorld.GetLiveGeometryCount(), 2u);

    extraction.Shutdown(engine.GetRenderer());
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 0u);
    EXPECT_EQ(gpuWorld.GetLiveInstanceCount(), 0u);
    EXPECT_EQ(extraction.GetLastStats().PointCloudGeometryReleases, 2u);

    engine.Shutdown();
}

TEST(PointCloudGeometryExtraction, ProceduralRefPreemptsPointCloudPathOnSameEntity)
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
    raw.emplace<E::ProceduralGeometryRef>(entity);
    AttachPointCloudSources(scene, entity);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    const auto stats = extraction.ExtractAndSubmit(scene,
                                                    engine.GetRenderer(),
                                                    &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));

    // Procedural intent declared → point-cloud path must not run.
    EXPECT_EQ(stats.ProceduralRenderablesEnumerated, 1u);
    EXPECT_EQ(stats.ProceduralGeometryUploads, 1u);
    EXPECT_EQ(stats.PointCloudGeometryUploads, 0u);
    EXPECT_EQ(stats.PointCloudGeometryFailedPack, 0u);

    const auto view = extraction.FindRenderableSidecarForTest(
        Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity));
    ASSERT_TRUE(view.has_value());
    EXPECT_FALSE(view->HasPointCloudResidency);
    EXPECT_TRUE(view->ProceduralKey.has_value());

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(PointCloudGeometryExtraction, MissingPositionsIncrementsMissingPositionsCounter)
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
    // Point-cloud-domain source but the Vertices PropertySet carries no
    // `v:position`.
    (void)raw.emplace<gs::Vertices>(entity);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    const auto stats = extraction.ExtractAndSubmit(scene,
                                                    engine.GetRenderer(),
                                                    &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));

    EXPECT_EQ(stats.PointCloudGeometryUploads, 0u);
    EXPECT_EQ(stats.PointCloudGeometryMissingPositions, 1u);
    EXPECT_EQ(stats.PointCloudGeometryInvalidPoints, 0u);
    EXPECT_EQ(stats.PointCloudGeometryFailedPack, 0u);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 0u);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(PointCloudGeometryExtraction, NonFinitePositionIncrementsInvalidPointsCounter)
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
    auto& vertices = raw.emplace<gs::Vertices>(entity);
    SetPositions(vertices, {
        {0.0f, 0.0f, 0.0f},
        {std::numeric_limits<float>::infinity(), 0.0f, 0.0f},
    });

    Extrinsic::Runtime::RenderExtractionCache extraction;
    const auto stats = extraction.ExtractAndSubmit(scene,
                                                    engine.GetRenderer(),
                                                    &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));

    EXPECT_EQ(stats.PointCloudGeometryUploads, 0u);
    EXPECT_EQ(stats.PointCloudGeometryInvalidPoints, 1u);
    EXPECT_EQ(stats.PointCloudGeometryMissingPositions, 0u);
    EXPECT_EQ(stats.PointCloudGeometryFailedPack, 0u);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(PointCloudGeometryExtraction, PerPointSizeSourceFailsClosedAsFailedPack)
{
    namespace E = Extrinsic::ECS::Components;
    namespace G = Extrinsic::Graphics::Components;

    Extrinsic::Runtime::Engine engine(HeadlessConfig());
    InitializeAssetWorkflowEngine(engine);

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    auto& raw = scene.Raw();
    const EntityHandle entity = scene.Create();
    raw.emplace<E::Transform::WorldMatrix>(entity).Matrix = glm::mat4{1.f};
    // A per-point size buffer (string SizeSource) is unsupported in this slice
    // — only a uniform float point size is. The bridge fails closed rather than
    // uploading geometry that the point pass cannot size correctly.
    auto& points = raw.emplace<G::RenderPoints>(entity);
    points.SizeSource = std::string{"v:radius"};
    AttachPointCloudSources(scene, entity);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    const auto stats = extraction.ExtractAndSubmit(scene,
                                                    engine.GetRenderer(),
                                                    &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));

    EXPECT_EQ(stats.PointCloudGeometryUploads, 0u);
    EXPECT_EQ(stats.PointCloudGeometryFailedPack, 1u);
    EXPECT_EQ(stats.PointCloudGeometryMissingPositions, 0u);
    EXPECT_EQ(stats.PointCloudGeometryInvalidPoints, 0u);

    const auto view = extraction.FindRenderableSidecarForTest(
        Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity));
    ASSERT_TRUE(view.has_value());
    EXPECT_FALSE(view->HasPointCloudResidency);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 0u);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(PointCloudGeometryExtraction, AddingProceduralRefAfterUploadReleasesPointCloudResidency)
{
    namespace E = Extrinsic::ECS::Components;

    Extrinsic::Runtime::Engine engine(HeadlessConfig());
    InitializeAssetWorkflowEngine(engine);

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    const EntityHandle entity = MakePointCloudRenderable(scene);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    auto stats = extraction.ExtractAndSubmit(scene,
                                             engine.GetRenderer(),
                                             &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));
    ASSERT_EQ(stats.PointCloudGeometryUploads, 1u);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    ASSERT_EQ(gpuWorld.GetLiveGeometryCount(), 1u);

    // Eligibility flip: a procedural ref appears on the same entity. The
    // point-cloud path releases its cached upload and procedural takes over.
    scene.Raw().emplace<E::ProceduralGeometryRef>(entity);

    stats = extraction.ExtractAndSubmit(scene,
                                        engine.GetRenderer(),
                                        &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));
    EXPECT_EQ(stats.PointCloudGeometryUploads, 0u);
    EXPECT_EQ(stats.PointCloudGeometryReuseHits, 0u);
    EXPECT_EQ(stats.PointCloudGeometryReleases, 1u);
    EXPECT_EQ(stats.PointCloudGeometryFreeRetires, 0u);
    EXPECT_EQ(stats.ProceduralGeometryUploads, 1u);

    const auto view = extraction.FindRenderableSidecarForTest(
        Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity));
    ASSERT_TRUE(view.has_value());
    EXPECT_FALSE(view->HasPointCloudResidency);
    ASSERT_TRUE(view->ProceduralKey.has_value());
    // Instance bound to the procedural handle, not the queued point-cloud slot.
    EXPECT_TRUE(view->Geometry.IsValid());
    EXPECT_EQ(gpuWorld.GetInstanceGeometry(view->Instance), view->Geometry);
    // Both the procedural slot and the queued point-cloud slot are live during
    // the deferred-retire window.
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 2u);

    constexpr std::uint32_t framesInFlight = 2u;
    constexpr std::uint64_t baseFrame = 400u;
    for (std::uint32_t i = 0; i <= framesInFlight; ++i)
    {
        extraction.TickPointCloudGeometry(baseFrame + i, framesInFlight, engine.GetRenderer());
    }
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 1u);
    stats = extraction.ExtractAndSubmit(scene,
                                        engine.GetRenderer(),
                                        &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));
    EXPECT_EQ(stats.PointCloudGeometryFreeRetires, 1u);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(PointCloudGeometryExtraction, LosingPointHintReleasesPointCloudResidency)
{
    namespace G = Extrinsic::Graphics::Components;

    Extrinsic::Runtime::Engine engine(HeadlessConfig());
    InitializeAssetWorkflowEngine(engine);

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    const EntityHandle entity = MakePointCloudRenderable(scene);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    auto stats = extraction.ExtractAndSubmit(scene,
                                             engine.GetRenderer(),
                                             &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));
    ASSERT_EQ(stats.PointCloudGeometryUploads, 1u);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    ASSERT_EQ(gpuWorld.GetLiveGeometryCount(), 1u);

    // Eligibility flip: the entity drops its point-cloud topology so
    // `BuildConstView` no longer resolves `Domain::PointCloud`. A
    // `RenderSurface` hint keeps the entity a renderable candidate.
    auto& raw = scene.Raw();
    raw.remove<gs::Vertices>(entity);
    raw.remove<G::RenderPoints>(entity);
    raw.emplace<G::RenderSurface>(entity);

    stats = extraction.ExtractAndSubmit(scene,
                                        engine.GetRenderer(),
                                        &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));
    EXPECT_EQ(stats.PointCloudGeometryUploads, 0u);
    EXPECT_EQ(stats.PointCloudGeometryReuseHits, 0u);
    EXPECT_EQ(stats.PointCloudGeometryReleases, 1u);
    EXPECT_EQ(stats.PointCloudGeometryFreeRetires, 0u);

    const auto view = extraction.FindRenderableSidecarForTest(
        Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity));
    ASSERT_TRUE(view.has_value());
    EXPECT_FALSE(view->HasPointCloudResidency);
    EXPECT_FALSE(gpuWorld.GetInstanceGeometry(view->Instance).IsValid());
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 1u);

    const auto gpuAvailability = extraction.FindGpuRenderableAvailability(
        Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity));
    ASSERT_TRUE(gpuAvailability.has_value());
    EXPECT_FALSE(gpuAvailability->Surface.HasGeometry);
    EXPECT_FALSE(gpuAvailability->Edges.HasGeometry);
    EXPECT_FALSE(gpuAvailability->Points.HasGeometry);

    constexpr std::uint32_t framesInFlight = 2u;
    constexpr std::uint64_t baseFrame = 600u;
    for (std::uint32_t i = 0; i <= framesInFlight; ++i)
    {
        extraction.TickPointCloudGeometry(baseFrame + i, framesInFlight, engine.GetRenderer());
    }
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 0u);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(PointCloudGeometryExtraction, UnsupportedSurfaceAndEdgeHintsFailClosed)
{
    namespace E = Extrinsic::ECS::Components;
    namespace G = Extrinsic::Graphics::Components;

    Extrinsic::Runtime::Engine engine(HeadlessConfig());
    InitializeAssetWorkflowEngine(engine);

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    auto& raw = scene.Raw();
    const EntityHandle entity = scene.Create();
    raw.emplace<E::Transform::WorldMatrix>(entity).Matrix = glm::mat4{1.f};
    AttachPointCloudSources(scene, entity);
    raw.emplace<G::RenderSurface>(entity);
    raw.emplace<G::RenderEdges>(entity);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    const auto stats = extraction.ExtractAndSubmit(scene,
                                                    engine.GetRenderer(),
                                                    &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));

    EXPECT_EQ(stats.CandidateRenderableCount, 1u);
    EXPECT_EQ(stats.PointCloudGeometryUploads, 0u);
    EXPECT_EQ(stats.PointCloudGeometryFailedPack, 1u);
    EXPECT_EQ(stats.PointCloudGeometryReleases, 0u);

    const auto view = extraction.FindRenderableSidecarForTest(
        Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity));
    ASSERT_TRUE(view.has_value());
    EXPECT_FALSE(view->HasPointCloudResidency);
    EXPECT_FALSE(engine.GetRenderer().GetGpuWorld().GetInstanceGeometry(view->Instance).IsValid());

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(PointCloudGeometryExtraction, UnsupportedSurfaceAndEdgeHintsReleasePriorResidency)
{
    namespace G = Extrinsic::Graphics::Components;

    Extrinsic::Runtime::Engine engine(HeadlessConfig());
    InitializeAssetWorkflowEngine(engine);

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    const EntityHandle entity = MakePointCloudRenderable(scene);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    auto stats = extraction.ExtractAndSubmit(scene,
                                             engine.GetRenderer(),
                                             &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));
    ASSERT_EQ(stats.PointCloudGeometryUploads, 1u);

    auto& raw = scene.Raw();
    raw.remove<G::RenderPoints>(entity);
    raw.emplace<G::RenderSurface>(entity);
    raw.emplace<G::RenderEdges>(entity);

    stats = extraction.ExtractAndSubmit(scene,
                                        engine.GetRenderer(),
                                        &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));

    EXPECT_EQ(stats.PointCloudGeometryUploads, 0u);
    EXPECT_EQ(stats.PointCloudGeometryFailedPack, 1u);
    EXPECT_EQ(stats.PointCloudGeometryReuseHits, 0u);
    EXPECT_EQ(stats.PointCloudGeometryReleases, 1u);

    const auto view = extraction.FindRenderableSidecarForTest(
        Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity));
    ASSERT_TRUE(view.has_value());
    EXPECT_FALSE(view->HasPointCloudResidency);
    EXPECT_FALSE(engine.GetRenderer().GetGpuWorld().GetInstanceGeometry(view->Instance).IsValid());

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

namespace
{
    void DrivePointCloudDeferredRetireWindow(Extrinsic::Runtime::RenderExtractionCache& extraction,
                                             Extrinsic::Graphics::IRenderer& renderer,
                                             std::uint64_t baseFrame,
                                             std::uint32_t framesInFlight)
    {
        for (std::uint32_t i = 0; i <= framesInFlight; ++i)
        {
            extraction.TickPointCloudGeometry(baseFrame + i, framesInFlight, renderer);
        }
    }
}

// Point-cloud dirty-domain coverage. Coarse GPU dirtiness still requires a full
// replacement upload, while vertex-channel tags use the in-place partial
// channel upload path and keep the existing resident handle. A point cloud has
// no edge/face topology, so only the vertex-domain and coarse tags are
// exercised.
class PointCloudGeometryExtractionDirtyTag
    : public ::testing::TestWithParam<const char*>
{
};

TEST_P(PointCloudGeometryExtractionDirtyTag, DirtyTagTriggersReupload)
{
    namespace D = Extrinsic::ECS::Components::DirtyTags;

    Extrinsic::Runtime::Engine engine(HeadlessConfig());
    InitializeAssetWorkflowEngine(engine);

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    auto& raw = scene.Raw();
    const EntityHandle entity = MakePointCloudRenderable(scene);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    auto stats = extraction.ExtractAndSubmit(scene,
                                             engine.GetRenderer(),
                                             &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));
    ASSERT_EQ(stats.PointCloudGeometryUploads, 1u);
    ASSERT_EQ(stats.PointCloudGeometryReuploads, 0u);

    const auto firstView =
        extraction.FindRenderableSidecarForTest(Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity));
    ASSERT_TRUE(firstView.has_value());
    const auto firstHandle = firstView->PointCloudGeometry;
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
    else
        FAIL() << "Unhandled dirty-tag parameterization: " << param;

    const bool fullUploadExpected = param == "GpuDirty";

    stats = extraction.ExtractAndSubmit(scene,
                                        engine.GetRenderer(),
                                        &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));
    EXPECT_EQ(stats.PointCloudGeometryUploads, 0u);
    EXPECT_EQ(stats.PointCloudGeometryReuploads, 1u);
    EXPECT_EQ(stats.PointCloudGeometryPartialUploads, fullUploadExpected ? 0u : 1u);
    EXPECT_EQ(stats.PointCloudGeometryReuseHits, 0u);
    EXPECT_EQ(stats.PointCloudGeometryReleases, fullUploadExpected ? 1u : 0u);
    EXPECT_EQ(stats.PointCloudGeometryFreeRetires, 0u);

    const auto secondView =
        extraction.FindRenderableSidecarForTest(Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity));
    ASSERT_TRUE(secondView.has_value());
    EXPECT_TRUE(secondView->HasPointCloudResidency);
    if (fullUploadExpected)
    {
        EXPECT_NE(secondView->PointCloudGeometry, firstHandle);
    }
    else
    {
        EXPECT_EQ(secondView->PointCloudGeometry, firstHandle);
    }
    EXPECT_EQ(secondView->Geometry, secondView->PointCloudGeometry);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), fullUploadExpected ? 2u : 1u);
    EXPECT_EQ(gpuWorld.GetInstanceGeometry(secondView->Instance), secondView->PointCloudGeometry);

    EXPECT_FALSE((raw.any_of<D::GpuDirty,
                             D::DirtyVertexPositions,
                             D::DirtyVertexAttributes,
                             D::DirtyVertexTexcoords,
                             D::DirtyVertexNormals,
                             D::DirtyVertexColors>(entity)));

    stats = extraction.ExtractAndSubmit(scene,
                                        engine.GetRenderer(),
                                        &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));
    EXPECT_EQ(stats.PointCloudGeometryReuploads, 0u);
    EXPECT_EQ(stats.PointCloudGeometryReuseHits, 1u);
    EXPECT_EQ(stats.PointCloudGeometryReleases, 0u);

    constexpr std::uint32_t framesInFlight = 2u;
    DrivePointCloudDeferredRetireWindow(extraction,
                                        engine.GetRenderer(),
                                        /*baseFrame=*/700u,
                                        framesInFlight);
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 1u);

    stats = extraction.ExtractAndSubmit(scene,
                                        engine.GetRenderer(),
                                        &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));
    EXPECT_EQ(stats.PointCloudGeometryFreeRetires, fullUploadExpected ? 1u : 0u);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

INSTANTIATE_TEST_SUITE_P(AllDirtyDomains,
                         PointCloudGeometryExtractionDirtyTag,
                         ::testing::Values("GpuDirty",
                                           "DirtyVertexPositions",
                                           "DirtyVertexAttributes",
                                           "DirtyVertexTexcoords",
                                           "DirtyVertexNormals",
                                           "DirtyVertexColors"));

TEST(PointCloudGeometryExtraction, VertexCountChangeFallsBackToFullUpload)
{
    namespace D = Extrinsic::ECS::Components::DirtyTags;

    Extrinsic::Runtime::Engine engine(HeadlessConfig());
    InitializeAssetWorkflowEngine(engine);

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    auto& raw = scene.Raw();
    const EntityHandle entity = MakePointCloudRenderable(scene);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    auto stats = extraction.ExtractAndSubmit(scene,
                                             engine.GetRenderer(),
                                             &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));
    ASSERT_EQ(stats.PointCloudGeometryUploads, 1u);

    const auto stableId =
        Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity);
    const auto firstView = extraction.FindRenderableSidecarForTest(stableId);
    ASSERT_TRUE(firstView.has_value());
    const auto firstHandle = firstView->PointCloudGeometry;
    ASSERT_TRUE(firstHandle.IsValid());

    auto& vertices = raw.get<gs::Vertices>(entity);
    SetPositions(vertices, {
        {0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
    });
    D::MarkVertexPositionsDirty(raw, entity);

    stats = extraction.ExtractAndSubmit(scene,
                                        engine.GetRenderer(),
                                        &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));
    EXPECT_EQ(stats.PointCloudGeometryUploads, 0u);
    EXPECT_EQ(stats.PointCloudGeometryReuploads, 1u);
    EXPECT_EQ(stats.PointCloudGeometryPartialUploads, 0u);
    EXPECT_EQ(stats.PointCloudGeometryReleases, 1u);

    const auto secondView = extraction.FindRenderableSidecarForTest(stableId);
    ASSERT_TRUE(secondView.has_value());
    EXPECT_TRUE(secondView->HasPointCloudResidency);
    EXPECT_NE(secondView->PointCloudGeometry, firstHandle);
    EXPECT_EQ(secondView->Geometry, secondView->PointCloudGeometry);
    EXPECT_FALSE(raw.any_of<D::DirtyVertexPositions>(entity));

    EXPECT_EQ(engine.GetRenderer().GetGpuWorld().GetLiveGeometryCount(), 2u);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

// RUNTIME-087 follow-up — a dirty-reupload pack failure on a cloud that already
// has a valid upload must release the stale residency (fail-closed) so invalid
// point data does not keep rendering the last-good positions, while leaving the
// dirty tag set for later recovery.
TEST(PointCloudGeometryExtraction, ReuploadFailureReleasesStaleResidencyAndPreservesDirtyTag)
{
    namespace D = Extrinsic::ECS::Components::DirtyTags;

    Extrinsic::Runtime::Engine engine(HeadlessConfig());
    InitializeAssetWorkflowEngine(engine);

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    auto& raw = scene.Raw();
    const EntityHandle entity = MakePointCloudRenderable(scene);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    auto stats = extraction.ExtractAndSubmit(scene,
                                             engine.GetRenderer(),
                                             &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));
    ASSERT_EQ(stats.PointCloudGeometryUploads, 1u);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    ASSERT_EQ(gpuWorld.GetLiveGeometryCount(), 1u);

    // Corrupt a point position to non-finite, then mark vertex positions dirty
    // so the next PackCloud returns NonFinitePosition.
    auto& vertices = raw.get<gs::Vertices>(entity);
    SetPositions(vertices, {
        {0.0f, 0.0f, 0.0f},
        {std::numeric_limits<float>::infinity(), 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
    });
    D::MarkVertexPositionsDirty(raw, entity);

    stats = extraction.ExtractAndSubmit(scene,
                                        engine.GetRenderer(),
                                        &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));
    EXPECT_EQ(stats.PointCloudGeometryReuploads, 0u);
    EXPECT_EQ(stats.PointCloudGeometryInvalidPoints, 1u);
    EXPECT_EQ(stats.PointCloudGeometryReuseHits, 0u);
    EXPECT_EQ(stats.PointCloudGeometryReleases, 1u);
    EXPECT_EQ(stats.PointCloudGeometryFreeRetires, 0u);

    const auto view =
        extraction.FindRenderableSidecarForTest(Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity));
    ASSERT_TRUE(view.has_value());
    EXPECT_FALSE(view->HasPointCloudResidency);
    EXPECT_FALSE(gpuWorld.GetInstanceGeometry(view->Instance).IsValid());
    EXPECT_TRUE(raw.any_of<D::DirtyVertexPositions>(entity));
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 1u);

    constexpr std::uint32_t framesInFlight = 2u;
    DrivePointCloudDeferredRetireWindow(extraction,
                                        engine.GetRenderer(),
                                        /*baseFrame=*/900u,
                                        framesInFlight);
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 0u);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

// RUNTIME-087 follow-up — switching a resident cloud to an unsupported
// per-point size-source buffer (the `std::string` alternative) must also
// release the stale residency, since the size-source check fails closed before
// the reuse path.
TEST(PointCloudGeometryExtraction, SwitchingToUnsupportedSizeSourceReleasesResidency)
{
    namespace G = Extrinsic::Graphics::Components;

    Extrinsic::Runtime::Engine engine(HeadlessConfig());
    InitializeAssetWorkflowEngine(engine);

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    auto& raw = scene.Raw();
    const EntityHandle entity = MakePointCloudRenderable(scene);  // default uniform float size.

    Extrinsic::Runtime::RenderExtractionCache extraction;
    auto stats = extraction.ExtractAndSubmit(scene,
                                             engine.GetRenderer(),
                                             &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));
    ASSERT_EQ(stats.PointCloudGeometryUploads, 1u);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    ASSERT_EQ(gpuWorld.GetLiveGeometryCount(), 1u);

    // Switch to a per-point size buffer. No dirty tag is needed — the
    // size-source check runs before the reuse path.
    raw.get<G::RenderPoints>(entity).SizeSource = std::string{"v:radius"};

    stats = extraction.ExtractAndSubmit(scene,
                                        engine.GetRenderer(),
                                        &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));
    EXPECT_EQ(stats.PointCloudGeometryUploads, 0u);
    EXPECT_EQ(stats.PointCloudGeometryReuseHits, 0u);
    EXPECT_EQ(stats.PointCloudGeometryFailedPack, 1u);
    EXPECT_EQ(stats.PointCloudGeometryReleases, 1u);
    EXPECT_EQ(stats.PointCloudGeometryFreeRetires, 0u);

    const auto view =
        extraction.FindRenderableSidecarForTest(Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity));
    ASSERT_TRUE(view.has_value());
    EXPECT_FALSE(view->HasPointCloudResidency);
    EXPECT_FALSE(gpuWorld.GetInstanceGeometry(view->Instance).IsValid());
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 1u);

    constexpr std::uint32_t framesInFlight = 2u;
    DrivePointCloudDeferredRetireWindow(extraction,
                                        engine.GetRenderer(),
                                        /*baseFrame=*/950u,
                                        framesInFlight);
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 0u);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}
