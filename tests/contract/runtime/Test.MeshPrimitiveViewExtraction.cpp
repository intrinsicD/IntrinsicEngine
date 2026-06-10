#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <glm/glm.hpp>

import Extrinsic.Core.Config.Engine;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Component.ProceduralGeometryRef;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.MeshPrimitiveViewPacker;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.StableEntityLookup;
import Geometry.Properties;

namespace gs = Extrinsic::ECS::Components::GeometrySources;
namespace pn = Extrinsic::ECS::Components::GeometrySources::PropertyNames;

using Extrinsic::ECS::EntityHandle;
using Extrinsic::ECS::Scene::Registry;
using Extrinsic::Runtime::MeshPrimitiveViewSettings;

namespace
{
    constexpr std::uint32_t kInvalidIndex = std::numeric_limits<std::uint32_t>::max();

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

    void SetPositions(gs::Vertices& v, const std::vector<glm::vec3>& positions)
    {
        v.Properties.Resize(positions.size());
        auto pos = v.Properties.GetOrAdd<glm::vec3>(std::string{pn::kPosition}, glm::vec3(0.0f));
        pos.Vector() = positions;
    }

    void SetEdges(gs::Edges& e,
                  const std::vector<std::uint32_t>& v0,
                  const std::vector<std::uint32_t>& v1)
    {
        e.Properties.Resize(v0.size());
        auto p0 = e.Properties.GetOrAdd<std::uint32_t>(std::string{pn::kEdgeV0}, kInvalidIndex);
        auto p1 = e.Properties.GetOrAdd<std::uint32_t>(std::string{pn::kEdgeV1}, kInvalidIndex);
        p0.Vector() = v0;
        p1.Vector() = v1;
    }

    void SetHalfedges(gs::Halfedges& h,
                      const std::vector<std::uint32_t>& toVertex,
                      const std::vector<std::uint32_t>& next,
                      const std::vector<std::uint32_t>& face)
    {
        h.Properties.Resize(toVertex.size());
        auto pt = h.Properties.GetOrAdd<std::uint32_t>(std::string{pn::kHalfedgeToVertex}, kInvalidIndex);
        auto pnxt = h.Properties.GetOrAdd<std::uint32_t>(std::string{pn::kHalfedgeNext}, kInvalidIndex);
        auto pf = h.Properties.GetOrAdd<std::uint32_t>(std::string{pn::kHalfedgeFace}, kInvalidIndex);
        pt.Vector() = toVertex;
        pnxt.Vector() = next;
        pf.Vector() = face;
    }

    void SetFaces(gs::Faces& f, const std::vector<std::uint32_t>& faceHe)
    {
        f.Properties.Resize(faceHe.size());
        auto p = f.Properties.GetOrAdd<std::uint32_t>(std::string{pn::kFaceHalfedge}, kInvalidIndex);
        p.Vector() = faceHe;
    }

    // Attach a single-triangle mesh `GeometrySources` whose `Edges` PropertySet
    // carries explicit `(e:v0, e:v1)` endpoints. The halfedge/face wiring mirrors
    // `Test.MeshGeometryExtraction.cpp` so `BuildConstView` resolves
    // `Domain::Mesh` and the surface packer emits faces, while the explicit edges
    // let `PackMeshEdgeView` derive a three-edge line list.
    void AttachTriangleMeshWithEdges(Registry& scene, EntityHandle entity)
    {
        auto& raw = scene.Raw();
        auto& vertices = raw.emplace<gs::Vertices>(entity);
        SetPositions(vertices, {
            {0.0f, 0.0f, 0.0f},
            {1.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f},
        });
        auto& edges = raw.emplace<gs::Edges>(entity);
        SetEdges(edges, /*v0*/ {0u, 1u, 2u}, /*v1*/ {1u, 2u, 0u});
        auto& halfedges = raw.emplace<gs::Halfedges>(entity);
        SetHalfedges(halfedges,
                     /*toVertex*/ {1u, 2u, 0u, 0u, 2u, 1u},
                     /*next*/     {1u, 2u, 0u, 5u, 3u, 4u},
                     /*face*/     {0u, 0u, 0u, kInvalidIndex, kInvalidIndex, kInvalidIndex});
        auto& faces = raw.emplace<gs::Faces>(entity);
        SetFaces(faces, {0u});
    }

    // Same as above but without explicit edges: `Edges` is empty so
    // `PackMeshEdgeView` reports `MissingEdgeTopology`, while the surface mesh
    // still resolves `Domain::Mesh` (halfedge/face topology present).
    void AttachTriangleMeshWithoutEdges(Registry& scene, EntityHandle entity)
    {
        auto& raw = scene.Raw();
        auto& vertices = raw.emplace<gs::Vertices>(entity);
        SetPositions(vertices, {
            {0.0f, 0.0f, 0.0f},
            {1.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f},
        });
        (void)raw.emplace<gs::Edges>(entity); // empty: no `e:v0`/`e:v1`.
        auto& halfedges = raw.emplace<gs::Halfedges>(entity);
        SetHalfedges(halfedges,
                     /*toVertex*/ {1u, 2u, 0u, 0u, 2u, 1u},
                     /*next*/     {1u, 2u, 0u, 5u, 3u, 4u},
                     /*face*/     {0u, 0u, 0u, kInvalidIndex, kInvalidIndex, kInvalidIndex});
        auto& faces = raw.emplace<gs::Faces>(entity);
        SetFaces(faces, {0u});
    }

    EntityHandle MakeMeshRenderable(Registry& scene, bool withEdges = true)
    {
        namespace E = Extrinsic::ECS::Components;
        namespace G = Extrinsic::Graphics::Components;
        const EntityHandle entity = scene.Create();
        auto& raw = scene.Raw();
        raw.emplace<E::Transform::WorldMatrix>(entity).Matrix = glm::mat4{1.f};
        raw.emplace<G::RenderSurface>(entity);
        if (withEdges)
        {
            AttachTriangleMeshWithEdges(scene, entity);
        }
        else
        {
            AttachTriangleMeshWithoutEdges(scene, entity);
        }
        return entity;
    }

    void DriveViewDeferredRetireWindow(Extrinsic::Runtime::RenderExtractionCache& extraction,
                                       Extrinsic::Graphics::IRenderer& renderer,
                                       std::uint64_t baseFrame,
                                       std::uint32_t framesInFlight)
    {
        for (std::uint32_t i = 0; i <= framesInFlight; ++i)
        {
            extraction.TickMeshGeometry(baseFrame + i, framesInFlight, renderer);
            extraction.TickMeshPrimitiveViewGeometry(baseFrame + i, framesInFlight, renderer);
        }
    }
}

TEST(MeshPrimitiveViewExtraction, EnableEdgeViewUploadsSeparateLineRenderable)
{
    Extrinsic::Runtime::Engine engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.Initialize();

    auto& scene = engine.GetScene();
    const EntityHandle entity = MakeMeshRenderable(scene);
    const auto stableId = Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    extraction.SetMeshPrimitiveViewSettings(stableId, MeshPrimitiveViewSettings{.EnableEdgeView = true});

    const auto stats = extraction.ExtractAndSubmit(scene,
                                                   engine.GetRenderer(),
                                                   &engine.GetGpuAssetCache());

    EXPECT_EQ(stats.MeshGeometryUploads, 1u);
    EXPECT_EQ(stats.MeshEdgeViewUploads, 1u);
    EXPECT_EQ(stats.MeshEdgeViewReuseHits, 0u);
    EXPECT_EQ(stats.MeshVertexViewUploads, 0u);
    EXPECT_EQ(stats.MeshEdgeViewFailedPack, 0u);
    EXPECT_EQ(stats.MeshEdgeViewMissingEdgeTopology, 0u);
    EXPECT_EQ(stats.MeshEdgeViewInvalidEdges, 0u);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    // Parent surface instance + edge view instance; surface geometry + edge geometry.
    EXPECT_EQ(gpuWorld.GetLiveInstanceCount(), 2u);
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 2u);

    const auto view = extraction.FindRenderableSidecarForTest(stableId);
    ASSERT_TRUE(view.has_value());
    EXPECT_TRUE(view->HasMeshResidency);
    ASSERT_TRUE(view->HasMeshEdgeView);
    EXPECT_TRUE(view->MeshEdgeViewInstance.IsValid());
    EXPECT_TRUE(view->MeshEdgeViewGeometry.IsValid());
    EXPECT_NE(view->MeshEdgeViewGeometry, view->MeshGeometry);
    EXPECT_NE(view->MeshEdgeViewInstance, view->Instance);
    EXPECT_FALSE(view->HasMeshVertexView);
    EXPECT_EQ(gpuWorld.GetInstanceGeometry(view->MeshEdgeViewInstance), view->MeshEdgeViewGeometry);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(MeshPrimitiveViewExtraction, EnableVertexViewUploadsSeparatePointRenderable)
{
    Extrinsic::Runtime::Engine engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.Initialize();

    auto& scene = engine.GetScene();
    const EntityHandle entity = MakeMeshRenderable(scene);
    const auto stableId = Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    extraction.SetMeshPrimitiveViewSettings(stableId, MeshPrimitiveViewSettings{.EnableVertexView = true});

    const auto stats = extraction.ExtractAndSubmit(scene,
                                                   engine.GetRenderer(),
                                                   &engine.GetGpuAssetCache());

    EXPECT_EQ(stats.MeshVertexViewUploads, 1u);
    EXPECT_EQ(stats.MeshVertexViewReuseHits, 0u);
    EXPECT_EQ(stats.MeshVertexViewFailedPack, 0u);
    EXPECT_EQ(stats.MeshVertexViewMissingPositions, 0u);
    EXPECT_EQ(stats.MeshEdgeViewUploads, 0u);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    EXPECT_EQ(gpuWorld.GetLiveInstanceCount(), 2u);
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 2u);

    const auto view = extraction.FindRenderableSidecarForTest(stableId);
    ASSERT_TRUE(view.has_value());
    ASSERT_TRUE(view->HasMeshVertexView);
    EXPECT_FALSE(view->HasMeshEdgeView);
    EXPECT_NE(view->MeshVertexViewGeometry, view->MeshGeometry);
    EXPECT_EQ(gpuWorld.GetInstanceGeometry(view->MeshVertexViewInstance), view->MeshVertexViewGeometry);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(MeshPrimitiveViewExtraction, EnableBothViewsAllocatesThreeIndependentRenderables)
{
    Extrinsic::Runtime::Engine engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.Initialize();

    auto& scene = engine.GetScene();
    const EntityHandle entity = MakeMeshRenderable(scene);
    const auto stableId = Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    extraction.SetMeshPrimitiveViewSettings(
        stableId, MeshPrimitiveViewSettings{.EnableEdgeView = true, .EnableVertexView = true});

    const auto stats = extraction.ExtractAndSubmit(scene,
                                                   engine.GetRenderer(),
                                                   &engine.GetGpuAssetCache());

    EXPECT_EQ(stats.MeshGeometryUploads, 1u);
    EXPECT_EQ(stats.MeshEdgeViewUploads, 1u);
    EXPECT_EQ(stats.MeshVertexViewUploads, 1u);
    EXPECT_EQ(stats.AllocatedInstanceCount, 3u);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    EXPECT_EQ(gpuWorld.GetLiveInstanceCount(), 3u);
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 3u);

    const auto view = extraction.FindRenderableSidecarForTest(stableId);
    ASSERT_TRUE(view.has_value());
    ASSERT_TRUE(view->HasMeshEdgeView);
    ASSERT_TRUE(view->HasMeshVertexView);
    // Surface, edge, and vertex are three distinct geometry handles + instances.
    EXPECT_NE(view->MeshGeometry, view->MeshEdgeViewGeometry);
    EXPECT_NE(view->MeshGeometry, view->MeshVertexViewGeometry);
    EXPECT_NE(view->MeshEdgeViewGeometry, view->MeshVertexViewGeometry);
    EXPECT_NE(view->MeshEdgeViewInstance, view->MeshVertexViewInstance);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(MeshPrimitiveViewExtraction, RepeatedExtractionReusesViewsWithoutReupload)
{
    Extrinsic::Runtime::Engine engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.Initialize();

    auto& scene = engine.GetScene();
    const EntityHandle entity = MakeMeshRenderable(scene);
    const auto stableId = Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    extraction.SetMeshPrimitiveViewSettings(
        stableId, MeshPrimitiveViewSettings{.EnableEdgeView = true, .EnableVertexView = true});

    auto stats = extraction.ExtractAndSubmit(scene, engine.GetRenderer(), &engine.GetGpuAssetCache());
    ASSERT_EQ(stats.MeshEdgeViewUploads, 1u);
    ASSERT_EQ(stats.MeshVertexViewUploads, 1u);

    const auto firstView = extraction.FindRenderableSidecarForTest(stableId);
    ASSERT_TRUE(firstView.has_value());
    const auto firstEdge = firstView->MeshEdgeViewGeometry;
    const auto firstVertex = firstView->MeshVertexViewGeometry;

    stats = extraction.ExtractAndSubmit(scene, engine.GetRenderer(), &engine.GetGpuAssetCache());
    EXPECT_EQ(stats.MeshEdgeViewUploads, 0u);
    EXPECT_EQ(stats.MeshVertexViewUploads, 0u);
    EXPECT_EQ(stats.MeshEdgeViewReuseHits, 1u);
    EXPECT_EQ(stats.MeshVertexViewReuseHits, 1u);
    EXPECT_EQ(stats.MeshEdgeViewReleases, 0u);
    EXPECT_EQ(stats.MeshVertexViewReleases, 0u);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 3u);

    const auto secondView = extraction.FindRenderableSidecarForTest(stableId);
    ASSERT_TRUE(secondView.has_value());
    EXPECT_EQ(secondView->MeshEdgeViewGeometry, firstEdge);
    EXPECT_EQ(secondView->MeshVertexViewGeometry, firstVertex);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(MeshPrimitiveViewExtraction, VertexPositionDirtyRepacksBothViews)
{
    namespace D = Extrinsic::ECS::Components::DirtyTags;

    Extrinsic::Runtime::Engine engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.Initialize();

    auto& scene = engine.GetScene();
    auto& raw = scene.Raw();
    const EntityHandle entity = MakeMeshRenderable(scene);
    const auto stableId = Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    extraction.SetMeshPrimitiveViewSettings(
        stableId, MeshPrimitiveViewSettings{.EnableEdgeView = true, .EnableVertexView = true});

    auto stats = extraction.ExtractAndSubmit(scene, engine.GetRenderer(), &engine.GetGpuAssetCache());
    ASSERT_EQ(stats.MeshEdgeViewUploads, 1u);
    ASSERT_EQ(stats.MeshVertexViewUploads, 1u);

    const auto firstView = extraction.FindRenderableSidecarForTest(stableId);
    ASSERT_TRUE(firstView.has_value());
    const auto firstEdge = firstView->MeshEdgeViewGeometry;
    const auto firstVertex = firstView->MeshVertexViewGeometry;

    D::MarkVertexPositionsDirty(raw, entity);

    stats = extraction.ExtractAndSubmit(scene, engine.GetRenderer(), &engine.GetGpuAssetCache());
    // Surface + both views repack on the shared dirty signal.
    EXPECT_EQ(stats.MeshGeometryReuploads, 1u);
    EXPECT_EQ(stats.MeshEdgeViewReuploads, 1u);
    EXPECT_EQ(stats.MeshVertexViewReuploads, 1u);
    EXPECT_EQ(stats.MeshEdgeViewReleases, 1u);
    EXPECT_EQ(stats.MeshVertexViewReleases, 1u);
    EXPECT_EQ(stats.MeshEdgeViewReuseHits, 0u);
    EXPECT_EQ(stats.MeshVertexViewReuseHits, 0u);

    const auto secondView = extraction.FindRenderableSidecarForTest(stableId);
    ASSERT_TRUE(secondView.has_value());
    EXPECT_NE(secondView->MeshEdgeViewGeometry, firstEdge);
    EXPECT_NE(secondView->MeshVertexViewGeometry, firstVertex);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    // Old surface/edge/vertex (queued) + new surface/edge/vertex (live) = 6.
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 6u);

    // The dirty tags drained, so a clean frame returns the views to reuse.
    stats = extraction.ExtractAndSubmit(scene, engine.GetRenderer(), &engine.GetGpuAssetCache());
    EXPECT_EQ(stats.MeshEdgeViewReuseHits, 1u);
    EXPECT_EQ(stats.MeshVertexViewReuseHits, 1u);
    EXPECT_EQ(stats.MeshEdgeViewReuploads, 0u);
    EXPECT_EQ(stats.MeshVertexViewReuploads, 0u);

    constexpr std::uint32_t framesInFlight = 2u;
    DriveViewDeferredRetireWindow(extraction, engine.GetRenderer(), /*baseFrame=*/900u, framesInFlight);
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 3u);

    stats = extraction.ExtractAndSubmit(scene, engine.GetRenderer(), &engine.GetGpuAssetCache());
    EXPECT_EQ(stats.MeshPrimitiveViewFreeRetires, 2u);
    EXPECT_EQ(stats.MeshGeometryFreeRetires, 1u);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(MeshPrimitiveViewExtraction, DisablingEdgeViewReleasesItsGeometryAfterWindow)
{
    Extrinsic::Runtime::Engine engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.Initialize();

    auto& scene = engine.GetScene();
    const EntityHandle entity = MakeMeshRenderable(scene);
    const auto stableId = Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    extraction.SetMeshPrimitiveViewSettings(
        stableId, MeshPrimitiveViewSettings{.EnableEdgeView = true, .EnableVertexView = true});

    auto stats = extraction.ExtractAndSubmit(scene, engine.GetRenderer(), &engine.GetGpuAssetCache());
    ASSERT_EQ(stats.MeshEdgeViewUploads, 1u);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    ASSERT_EQ(gpuWorld.GetLiveInstanceCount(), 3u);
    ASSERT_EQ(gpuWorld.GetLiveGeometryCount(), 3u);

    // Disable just the edge view; the vertex view stays resident.
    extraction.SetMeshPrimitiveViewSettings(stableId, MeshPrimitiveViewSettings{.EnableVertexView = true});

    stats = extraction.ExtractAndSubmit(scene, engine.GetRenderer(), &engine.GetGpuAssetCache());
    EXPECT_EQ(stats.MeshEdgeViewReleases, 1u);
    EXPECT_EQ(stats.MeshVertexViewReleases, 0u);
    EXPECT_EQ(stats.MeshVertexViewReuseHits, 1u);

    const auto view = extraction.FindRenderableSidecarForTest(stableId);
    ASSERT_TRUE(view.has_value());
    EXPECT_FALSE(view->HasMeshEdgeView);
    EXPECT_TRUE(view->HasMeshVertexView);
    // Edge instance freed immediately; geometry queued for deferred retire.
    EXPECT_EQ(gpuWorld.GetLiveInstanceCount(), 2u);
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 3u);

    constexpr std::uint32_t framesInFlight = 2u;
    DriveViewDeferredRetireWindow(extraction, engine.GetRenderer(), /*baseFrame=*/1000u, framesInFlight);
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 2u);

    stats = extraction.ExtractAndSubmit(scene, engine.GetRenderer(), &engine.GetGpuAssetCache());
    EXPECT_EQ(stats.MeshPrimitiveViewFreeRetires, 1u);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(MeshPrimitiveViewExtraction, EntityDestructionReleasesViewsAfterWindow)
{
    Extrinsic::Runtime::Engine engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.Initialize();

    auto& scene = engine.GetScene();
    const EntityHandle entity = MakeMeshRenderable(scene);
    const auto stableId = Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    extraction.SetMeshPrimitiveViewSettings(
        stableId, MeshPrimitiveViewSettings{.EnableEdgeView = true, .EnableVertexView = true});

    auto stats = extraction.ExtractAndSubmit(scene, engine.GetRenderer(), &engine.GetGpuAssetCache());
    ASSERT_EQ(stats.MeshEdgeViewUploads, 1u);
    ASSERT_EQ(stats.MeshVertexViewUploads, 1u);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    ASSERT_EQ(gpuWorld.GetLiveInstanceCount(), 3u);

    scene.Destroy(entity);

    stats = extraction.ExtractAndSubmit(scene, engine.GetRenderer(), &engine.GetGpuAssetCache());
    // Parent + edge + vertex instances all freed this frame.
    EXPECT_EQ(stats.FreedInstanceCount, 3u);
    EXPECT_EQ(stats.MeshEdgeViewReleases, 1u);
    EXPECT_EQ(stats.MeshVertexViewReleases, 1u);
    EXPECT_EQ(gpuWorld.GetLiveInstanceCount(), 0u);
    // Surface + edge + vertex geometry queued for the deferred-retire window.
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 3u);

    constexpr std::uint32_t framesInFlight = 2u;
    DriveViewDeferredRetireWindow(extraction, engine.GetRenderer(), /*baseFrame=*/1100u, framesInFlight);
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 0u);

    stats = extraction.ExtractAndSubmit(scene, engine.GetRenderer(), &engine.GetGpuAssetCache());
    EXPECT_EQ(stats.MeshPrimitiveViewFreeRetires, 2u);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(MeshPrimitiveViewExtraction, ShutdownReleasesViewResidency)
{
    Extrinsic::Runtime::Engine engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.Initialize();

    auto& scene = engine.GetScene();
    const EntityHandle entity = MakeMeshRenderable(scene);
    const auto stableId = Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    extraction.SetMeshPrimitiveViewSettings(
        stableId, MeshPrimitiveViewSettings{.EnableEdgeView = true, .EnableVertexView = true});

    auto stats = extraction.ExtractAndSubmit(scene, engine.GetRenderer(), &engine.GetGpuAssetCache());
    ASSERT_EQ(stats.MeshEdgeViewUploads, 1u);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    ASSERT_EQ(gpuWorld.GetLiveGeometryCount(), 3u);

    extraction.Shutdown(engine.GetRenderer());
    EXPECT_EQ(gpuWorld.GetLiveInstanceCount(), 0u);
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 0u);
    EXPECT_EQ(extraction.GetLastStats().MeshEdgeViewReleases, 1u);
    EXPECT_EQ(extraction.GetLastStats().MeshVertexViewReleases, 1u);

    engine.Shutdown();
}

TEST(MeshPrimitiveViewExtraction, ProceduralRefFlipReleasesViews)
{
    namespace E = Extrinsic::ECS::Components;

    Extrinsic::Runtime::Engine engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.Initialize();

    auto& scene = engine.GetScene();
    const EntityHandle entity = MakeMeshRenderable(scene);
    const auto stableId = Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    extraction.SetMeshPrimitiveViewSettings(
        stableId, MeshPrimitiveViewSettings{.EnableEdgeView = true, .EnableVertexView = true});

    auto stats = extraction.ExtractAndSubmit(scene, engine.GetRenderer(), &engine.GetGpuAssetCache());
    ASSERT_EQ(stats.MeshEdgeViewUploads, 1u);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    ASSERT_EQ(gpuWorld.GetLiveInstanceCount(), 3u);

    // Procedural intent appears: the mesh path (and therefore the views) must
    // stop running and release. The parent instance is rebound to procedural.
    scene.Raw().emplace<E::ProceduralGeometryRef>(entity);

    stats = extraction.ExtractAndSubmit(scene, engine.GetRenderer(), &engine.GetGpuAssetCache());
    EXPECT_EQ(stats.MeshEdgeViewReleases, 1u);
    EXPECT_EQ(stats.MeshVertexViewReleases, 1u);
    EXPECT_EQ(stats.MeshEdgeViewUploads, 0u);
    EXPECT_EQ(stats.ProceduralGeometryUploads, 1u);

    const auto view = extraction.FindRenderableSidecarForTest(stableId);
    ASSERT_TRUE(view.has_value());
    EXPECT_FALSE(view->HasMeshEdgeView);
    EXPECT_FALSE(view->HasMeshVertexView);
    // View instances freed; only the parent instance (procedural now) survives.
    EXPECT_EQ(gpuWorld.GetLiveInstanceCount(), 1u);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(MeshPrimitiveViewExtraction, MissingEdgeTopologyFailsEdgeViewButKeepsSurface)
{
    Extrinsic::Runtime::Engine engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.Initialize();

    auto& scene = engine.GetScene();
    // Triangle mesh whose `Edges` PropertySet is empty (no explicit edges).
    const EntityHandle entity = MakeMeshRenderable(scene, /*withEdges=*/false);
    const auto stableId = Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    extraction.SetMeshPrimitiveViewSettings(stableId, MeshPrimitiveViewSettings{.EnableEdgeView = true});

    const auto stats = extraction.ExtractAndSubmit(scene,
                                                   engine.GetRenderer(),
                                                   &engine.GetGpuAssetCache());

    EXPECT_EQ(stats.MeshGeometryUploads, 1u);
    EXPECT_EQ(stats.MeshEdgeViewUploads, 0u);
    EXPECT_EQ(stats.MeshEdgeViewMissingEdgeTopology, 1u);
    EXPECT_EQ(stats.MeshEdgeViewInvalidEdges, 0u);
    EXPECT_EQ(stats.MeshEdgeViewFailedPack, 0u);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    // Only the surface mesh is resident; no edge view instance/geometry.
    EXPECT_EQ(gpuWorld.GetLiveInstanceCount(), 1u);
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 1u);

    const auto view = extraction.FindRenderableSidecarForTest(stableId);
    ASSERT_TRUE(view.has_value());
    EXPECT_TRUE(view->HasMeshResidency);
    EXPECT_FALSE(view->HasMeshEdgeView);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(MeshPrimitiveViewExtraction, OutOfRangeEdgeEndpointIncrementsInvalidEdgesCounter)
{
    Extrinsic::Runtime::Engine engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.Initialize();

    auto& scene = engine.GetScene();
    auto& raw = scene.Raw();
    const EntityHandle entity = MakeMeshRenderable(scene);
    const auto stableId = Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity);

    // Corrupt one edge endpoint to reference a non-existent vertex.
    auto& edges = raw.get<gs::Edges>(entity);
    SetEdges(edges, /*v0*/ {0u, 1u, 2u}, /*v1*/ {1u, 2u, 9u});

    Extrinsic::Runtime::RenderExtractionCache extraction;
    extraction.SetMeshPrimitiveViewSettings(stableId, MeshPrimitiveViewSettings{.EnableEdgeView = true});

    const auto stats = extraction.ExtractAndSubmit(scene,
                                                   engine.GetRenderer(),
                                                   &engine.GetGpuAssetCache());

    EXPECT_EQ(stats.MeshGeometryUploads, 1u);
    EXPECT_EQ(stats.MeshEdgeViewUploads, 0u);
    EXPECT_EQ(stats.MeshEdgeViewInvalidEdges, 1u);
    EXPECT_EQ(stats.MeshEdgeViewMissingEdgeTopology, 0u);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 1u);

    const auto view = extraction.FindRenderableSidecarForTest(stableId);
    ASSERT_TRUE(view.has_value());
    EXPECT_FALSE(view->HasMeshEdgeView);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(MeshPrimitiveViewExtraction, NonMeshEntityWithSettingsCreatesNoViews)
{
    namespace G = Extrinsic::Graphics::Components;

    Extrinsic::Runtime::Engine engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.Initialize();

    auto& scene = engine.GetScene();
    auto& raw = scene.Raw();
    const EntityHandle entity = scene.Create();
    raw.emplace<Extrinsic::ECS::Components::Transform::WorldMatrix>(entity).Matrix = glm::mat4{1.f};
    raw.emplace<G::RenderPoints>(entity);
    // Point-cloud shape: Vertices only → DetectDomain resolves PointCloud, not Mesh.
    auto& vertices = raw.emplace<gs::Vertices>(entity);
    SetPositions(vertices, {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}});
    const auto stableId = Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    extraction.SetMeshPrimitiveViewSettings(
        stableId, MeshPrimitiveViewSettings{.EnableEdgeView = true, .EnableVertexView = true});

    const auto stats = extraction.ExtractAndSubmit(scene,
                                                   engine.GetRenderer(),
                                                   &engine.GetGpuAssetCache());

    // Mesh primitive views only attach to a resident mesh; a point-cloud entity
    // never enters the mesh branch, so no view counters move.
    EXPECT_EQ(stats.MeshEdgeViewUploads, 0u);
    EXPECT_EQ(stats.MeshVertexViewUploads, 0u);
    EXPECT_EQ(stats.MeshEdgeViewFailedPack, 0u);
    EXPECT_EQ(stats.MeshVertexViewFailedPack, 0u);

    const auto view = extraction.FindRenderableSidecarForTest(stableId);
    ASSERT_TRUE(view.has_value());
    EXPECT_FALSE(view->HasMeshEdgeView);
    EXPECT_FALSE(view->HasMeshVertexView);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}
