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
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.MeshPrimitiveViewPacker;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.StableEntityLookup;
import Geometry.Properties;

namespace gs = Extrinsic::ECS::Components::GeometrySources;
namespace pn = Extrinsic::ECS::Components::GeometrySources::PropertyNames;

using Extrinsic::ECS::EntityHandle;
using Extrinsic::ECS::Scene::Registry;

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

    void SetTexcoords(gs::Vertices& v, const std::vector<glm::vec2>& texcoords)
    {
        auto uv = v.Properties.GetOrAdd<glm::vec2>("v:texcoord", glm::vec2(0.0f));
        uv.Vector() = texcoords;
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
        SetTexcoords(vertices, {
            {0.0f, 0.0f},
            {1.0f, 0.0f},
            {0.0f, 1.0f},
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

    // Same as above but without explicit edges: the edge-view packer derives
    // wireframe lines from halfedge/face topology.
    void AttachTriangleMeshWithoutEdges(Registry& scene, EntityHandle entity)
    {
        auto& raw = scene.Raw();
        auto& vertices = raw.emplace<gs::Vertices>(entity);
        SetPositions(vertices, {
            {0.0f, 0.0f, 0.0f},
            {1.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f},
        });
        SetTexcoords(vertices, {
            {0.0f, 0.0f},
            {1.0f, 0.0f},
            {0.0f, 1.0f},
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

    EntityHandle MakeMeshVertexOnlyRenderable(Registry& scene)
    {
        namespace E = Extrinsic::ECS::Components;
        const EntityHandle entity = scene.Create();
        auto& raw = scene.Raw();
        raw.emplace<E::Transform::WorldMatrix>(entity).Matrix = glm::mat4{1.f};
        raw.emplace<gs::HasMeshTopology>(entity);
        auto& vertices = raw.emplace<gs::Vertices>(entity);
        SetPositions(vertices, {
            {0.0f, 0.0f, 0.0f},
            {1.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f},
        });
        return entity;
    }

    void EnableEdgeView(Registry& scene, EntityHandle entity, float width = 1.0f)
    {
        namespace G = Extrinsic::Graphics::Components;
        G::RenderEdges edges{};
        edges.WidthSource = width;
        scene.Raw().emplace_or_replace<G::RenderEdges>(entity, edges);
    }

    void EnableVertexView(
        Registry& scene,
        EntityHandle entity,
        Extrinsic::Graphics::Components::RenderPoints::RenderType type =
            Extrinsic::Graphics::Components::RenderPoints::RenderType::Sphere,
        float size = 6.0f)
    {
        namespace G = Extrinsic::Graphics::Components;
        G::RenderPoints points{};
        points.Type = type;
        points.SizeSource = size;
        scene.Raw().emplace_or_replace<G::RenderPoints>(entity, points);
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

TEST(MeshPrimitiveViewExtraction, EnableEdgeViewUploadsSeparateEdgeRenderable)
{
    Extrinsic::Runtime::Engine engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.Initialize();

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    const EntityHandle entity = MakeMeshRenderable(scene);
    const auto stableId = Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    EnableEdgeView(scene, entity);

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
    const auto config = gpuWorld.GetEntityConfigForTest(view->MeshEdgeViewInstance);
    EXPECT_FLOAT_EQ(config.Line.LineWidth, 1.0f);
    EXPECT_EQ(config.Line.LineWidthBDA, 0u);
    EXPECT_EQ(config.ColorSourceMode, 1u);
    EXPECT_FLOAT_EQ(config.UniformColor.r, 0.02f);
    EXPECT_FLOAT_EQ(config.UniformColor.g, 0.02f);
    EXPECT_FLOAT_EQ(config.UniformColor.b, 0.02f);
    EXPECT_FLOAT_EQ(config.UniformColor.a, 1.0f);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(MeshPrimitiveViewExtraction, EdgeViewWidthConfigUpdateReusesGeometry)
{
    Extrinsic::Runtime::Engine engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.Initialize();

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    const EntityHandle entity = MakeMeshRenderable(scene);
    const auto stableId = Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    EnableEdgeView(scene, entity, 2.5f);

    auto stats = extraction.ExtractAndSubmit(scene,
                                             engine.GetRenderer(),
                                             &engine.GetGpuAssetCache());
    ASSERT_EQ(stats.MeshEdgeViewUploads, 1u);

    auto view = extraction.FindRenderableSidecarForTest(stableId);
    ASSERT_TRUE(view.has_value());
    const auto firstGeometry = view->MeshEdgeViewGeometry;
    const auto firstInstance = view->MeshEdgeViewInstance;
    ASSERT_TRUE(firstGeometry.IsValid());
    ASSERT_TRUE(firstInstance.IsValid());

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    auto config = gpuWorld.GetEntityConfigForTest(firstInstance);
    EXPECT_FLOAT_EQ(config.Line.LineWidth, 2.5f);
    EXPECT_EQ(config.Line.LineWidthBDA, 0u);

    EnableEdgeView(scene, entity, 4.75f);

    stats = extraction.ExtractAndSubmit(scene,
                                        engine.GetRenderer(),
                                        &engine.GetGpuAssetCache());
    EXPECT_EQ(stats.MeshEdgeViewUploads, 0u);
    EXPECT_EQ(stats.MeshEdgeViewReuseHits, 1u);
    EXPECT_EQ(stats.MeshEdgeViewReuploads, 0u);

    view = extraction.FindRenderableSidecarForTest(stableId);
    ASSERT_TRUE(view.has_value());
    EXPECT_EQ(view->MeshEdgeViewGeometry, firstGeometry);
    EXPECT_EQ(view->MeshEdgeViewInstance, firstInstance);
    config = gpuWorld.GetEntityConfigForTest(firstInstance);
    EXPECT_FLOAT_EQ(config.Line.LineWidth, 4.75f);
    EXPECT_EQ(config.Line.LineWidthBDA, 0u);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(MeshPrimitiveViewExtraction, EnableVertexViewUploadsSeparatePointRenderable)
{
    Extrinsic::Runtime::Engine engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.Initialize();

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    const EntityHandle entity = MakeMeshRenderable(scene);
    const auto stableId = Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    EnableVertexView(
        scene,
        entity,
        Extrinsic::Graphics::Components::RenderPoints::RenderType::Flat,
        9.0f);

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
    const auto config = gpuWorld.GetEntityConfigForTest(view->MeshVertexViewInstance);
    EXPECT_FLOAT_EQ(config.Point.PointSize, 9.0f);
    EXPECT_EQ(config.Point.PointMode, 0u);
    EXPECT_EQ(config.ColorSourceMode, 1u);
    EXPECT_FLOAT_EQ(config.UniformColor.r, 0.02f);
    EXPECT_FLOAT_EQ(config.UniformColor.g, 0.02f);
    EXPECT_FLOAT_EQ(config.UniformColor.b, 0.02f);
    EXPECT_FLOAT_EQ(config.UniformColor.a, 1.0f);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(MeshPrimitiveViewExtraction, MeshVertexPointLaneDoesNotRequireSurfaceTopology)
{
    Extrinsic::Runtime::Engine engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.Initialize();

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    const EntityHandle entity = MakeMeshVertexOnlyRenderable(scene);
    const auto stableId = Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    EnableVertexView(scene, entity);

    const auto stats = extraction.ExtractAndSubmit(scene,
                                                   engine.GetRenderer(),
                                                   &engine.GetGpuAssetCache());

    EXPECT_EQ(stats.MeshGeometryUploads, 0u);
    EXPECT_EQ(stats.MeshVertexViewUploads, 1u);
    EXPECT_EQ(stats.MeshVertexViewFailedPack, 0u);
    EXPECT_EQ(stats.MeshVertexViewMissingPositions, 0u);
    EXPECT_EQ(stats.MeshEdgeViewUploads, 0u);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 1u);

    const auto view = extraction.FindRenderableSidecarForTest(stableId);
    ASSERT_TRUE(view.has_value());
    EXPECT_FALSE(view->HasMeshResidency);
    EXPECT_FALSE(view->HasMeshEdgeView);
    ASSERT_TRUE(view->HasMeshVertexView);
    EXPECT_TRUE(view->MeshVertexViewInstance.IsValid());
    EXPECT_TRUE(view->MeshVertexViewGeometry.IsValid());
    EXPECT_EQ(gpuWorld.GetInstanceGeometry(view->MeshVertexViewInstance),
              view->MeshVertexViewGeometry);

    const auto gpuAvailability = extraction.FindGpuRenderableAvailability(stableId);
    ASSERT_TRUE(gpuAvailability.has_value());
    EXPECT_FALSE(gpuAvailability->Surface.HasGeometry);
    EXPECT_FALSE(gpuAvailability->Edges.HasGeometry);
    EXPECT_TRUE(gpuAvailability->Points.HasInstance);
    EXPECT_TRUE(gpuAvailability->Points.HasGeometry);
    EXPECT_EQ(gpuAvailability->Points.Geometry, view->MeshVertexViewGeometry);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(MeshPrimitiveViewExtraction, VertexViewConfigUpdateReusesGeometry)
{
    Extrinsic::Runtime::Engine engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.Initialize();

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    const EntityHandle entity = MakeMeshRenderable(scene);
    const auto stableId = Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    EnableVertexView(
        scene,
        entity,
        Extrinsic::Graphics::Components::RenderPoints::RenderType::Flat,
        9.0f);

    auto stats = extraction.ExtractAndSubmit(scene,
                                             engine.GetRenderer(),
                                             &engine.GetGpuAssetCache());
    ASSERT_EQ(stats.MeshVertexViewUploads, 1u);

    auto view = extraction.FindRenderableSidecarForTest(stableId);
    ASSERT_TRUE(view.has_value());
    const auto firstGeometry = view->MeshVertexViewGeometry;
    const auto firstInstance = view->MeshVertexViewInstance;
    ASSERT_TRUE(firstGeometry.IsValid());
    ASSERT_TRUE(firstInstance.IsValid());

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    auto config = gpuWorld.GetEntityConfigForTest(firstInstance);
    EXPECT_FLOAT_EQ(config.Point.PointSize, 9.0f);
    EXPECT_EQ(config.Point.PointMode, 0u);

    EnableVertexView(
        scene,
        entity,
        Extrinsic::Graphics::Components::RenderPoints::RenderType::Surfel,
        12.0f);

    stats = extraction.ExtractAndSubmit(scene,
                                        engine.GetRenderer(),
                                        &engine.GetGpuAssetCache());
    EXPECT_EQ(stats.MeshVertexViewUploads, 0u);
    EXPECT_EQ(stats.MeshVertexViewReuseHits, 1u);
    EXPECT_EQ(stats.MeshVertexViewReuploads, 0u);

    view = extraction.FindRenderableSidecarForTest(stableId);
    ASSERT_TRUE(view.has_value());
    EXPECT_EQ(view->MeshVertexViewGeometry, firstGeometry);
    EXPECT_EQ(view->MeshVertexViewInstance, firstInstance);
    config = gpuWorld.GetEntityConfigForTest(firstInstance);
    EXPECT_FLOAT_EQ(config.Point.PointSize, 12.0f);
    EXPECT_EQ(config.Point.PointMode, 2u);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(MeshPrimitiveViewExtraction, EnableBothViewsAllocatesThreeIndependentRenderables)
{
    Extrinsic::Runtime::Engine engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.Initialize();

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    const EntityHandle entity = MakeMeshRenderable(scene);
    const auto stableId = Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    EnableEdgeView(scene, entity);
    EnableVertexView(scene, entity);

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

    const auto gpuAvailability = extraction.FindGpuRenderableAvailability(stableId);
    ASSERT_TRUE(gpuAvailability.has_value());
    EXPECT_TRUE(gpuAvailability->HasRenderable);
    EXPECT_EQ(gpuAvailability->Surface.Lane, Extrinsic::Runtime::GeometryRenderLane::Surface);
    EXPECT_EQ(gpuAvailability->Edges.Lane, Extrinsic::Runtime::GeometryRenderLane::Edges);
    EXPECT_EQ(gpuAvailability->Points.Lane, Extrinsic::Runtime::GeometryRenderLane::Points);
    EXPECT_TRUE(gpuAvailability->Surface.HasInstance);
    EXPECT_TRUE(gpuAvailability->Surface.HasGeometry);
    EXPECT_TRUE(gpuAvailability->Edges.HasInstance);
    EXPECT_TRUE(gpuAvailability->Edges.HasGeometry);
    EXPECT_TRUE(gpuAvailability->Points.HasInstance);
    EXPECT_TRUE(gpuAvailability->Points.HasGeometry);
    EXPECT_EQ(gpuAvailability->Surface.Instance, view->Instance);
    EXPECT_EQ(gpuAvailability->Surface.Geometry, view->MeshGeometry);
    EXPECT_EQ(gpuAvailability->Edges.Instance, view->MeshEdgeViewInstance);
    EXPECT_EQ(gpuAvailability->Edges.Geometry, view->MeshEdgeViewGeometry);
    EXPECT_EQ(gpuAvailability->Points.Instance, view->MeshVertexViewInstance);
    EXPECT_EQ(gpuAvailability->Points.Geometry, view->MeshVertexViewGeometry);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(MeshPrimitiveViewExtraction, EdgeAndPointComponentsDoNotRequireRenderSurface)
{
    namespace G = Extrinsic::Graphics::Components;

    Extrinsic::Runtime::Engine engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.Initialize();

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    const EntityHandle entity = MakeMeshRenderable(scene);
    scene.Raw().remove<G::RenderSurface>(entity);
    EnableEdgeView(scene, entity);
    EnableVertexView(
        scene,
        entity,
        G::RenderPoints::RenderType::Flat,
        8.0f);
    const auto stableId = Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    const auto stats = extraction.ExtractAndSubmit(scene,
                                                   engine.GetRenderer(),
                                                   &engine.GetGpuAssetCache());

    EXPECT_EQ(stats.MeshGeometryUploads, 0u);
    EXPECT_EQ(stats.MeshEdgeViewUploads, 1u);
    EXPECT_EQ(stats.MeshVertexViewUploads, 1u);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 2u);

    const auto view = extraction.FindRenderableSidecarForTest(stableId);
    ASSERT_TRUE(view.has_value());
    EXPECT_FALSE(view->HasMeshResidency);
    EXPECT_TRUE(view->HasMeshEdgeView);
    EXPECT_TRUE(view->HasMeshVertexView);
    EXPECT_EQ(gpuWorld.GetInstanceGeometry(view->MeshEdgeViewInstance),
              view->MeshEdgeViewGeometry);
    EXPECT_EQ(gpuWorld.GetInstanceGeometry(view->MeshVertexViewInstance),
              view->MeshVertexViewGeometry);
    const auto config =
        gpuWorld.GetEntityConfigForTest(view->MeshVertexViewInstance);
    EXPECT_EQ(config.Point.PointMode, 0u);
    EXPECT_FLOAT_EQ(config.Point.PointSize, 8.0f);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(MeshPrimitiveViewExtraction, LaneOverridesColorEdgeAndVertexViewsIndependently)
{
    namespace G = Extrinsic::Graphics::Components;

    Extrinsic::Runtime::Engine engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.Initialize();

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    const EntityHandle entity = MakeMeshRenderable(scene);
    EnableEdgeView(scene, entity, 3.0f);
    EnableVertexView(
        scene,
        entity,
        G::RenderPoints::RenderType::Flat,
        9.0f);

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

    const auto stableId = Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity);
    Extrinsic::Runtime::RenderExtractionCache extraction;
    const auto stats = extraction.ExtractAndSubmit(scene,
                                                   engine.GetRenderer(),
                                                   &engine.GetGpuAssetCache());
    EXPECT_EQ(stats.MeshEdgeViewUploads, 1u);
    EXPECT_EQ(stats.MeshVertexViewUploads, 1u);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    const auto view = extraction.FindRenderableSidecarForTest(stableId);
    ASSERT_TRUE(view.has_value());
    ASSERT_TRUE(view->HasMeshEdgeView);
    ASSERT_TRUE(view->HasMeshVertexView);

    const auto edgeConfig =
        gpuWorld.GetEntityConfigForTest(view->MeshEdgeViewInstance);
    EXPECT_EQ(edgeConfig.UniformColor, glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));
    EXPECT_FLOAT_EQ(edgeConfig.Line.LineWidth, 3.0f);

    const auto pointConfig =
        gpuWorld.GetEntityConfigForTest(view->MeshVertexViewInstance);
    EXPECT_EQ(pointConfig.UniformColor, glm::vec4(0.0f, 0.75f, 0.25f, 1.0f));
    EXPECT_EQ(pointConfig.Point.PointMode, 0u);
    EXPECT_FLOAT_EQ(pointConfig.Point.PointSize, 9.0f);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(MeshPrimitiveViewExtraction, RepeatedExtractionReusesViewsWithoutReupload)
{
    Extrinsic::Runtime::Engine engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.Initialize();

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    const EntityHandle entity = MakeMeshRenderable(scene);
    const auto stableId = Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    EnableEdgeView(scene, entity);
    EnableVertexView(scene, entity);

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

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    auto& raw = scene.Raw();
    const EntityHandle entity = MakeMeshRenderable(scene);
    const auto stableId = Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    EnableEdgeView(scene, entity);
    EnableVertexView(scene, entity);

    auto stats = extraction.ExtractAndSubmit(scene, engine.GetRenderer(), &engine.GetGpuAssetCache());
    ASSERT_EQ(stats.MeshEdgeViewUploads, 1u);
    ASSERT_EQ(stats.MeshVertexViewUploads, 1u);

    const auto firstView = extraction.FindRenderableSidecarForTest(stableId);
    ASSERT_TRUE(firstView.has_value());
    const auto firstEdge = firstView->MeshEdgeViewGeometry;
    const auto firstVertex = firstView->MeshVertexViewGeometry;

    D::MarkVertexPositionsDirty(raw, entity);

    stats = extraction.ExtractAndSubmit(scene, engine.GetRenderer(), &engine.GetGpuAssetCache());
    // The surface updates its resident SoA position channel in place, while
    // the derived edge/vertex view geometry still repacks on position edits.
    EXPECT_EQ(stats.MeshGeometryReuploads, 1u);
    EXPECT_EQ(stats.MeshGeometryPartialUploads, 1u);
    EXPECT_EQ(stats.MeshGeometryReleases, 0u);
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
    // Resident surface + old edge/vertex (queued) + new edge/vertex (live).
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 5u);

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
    EXPECT_EQ(stats.MeshGeometryFreeRetires, 0u);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(MeshPrimitiveViewExtraction, DisablingEdgeViewReleasesItsGeometryAfterWindow)
{
    Extrinsic::Runtime::Engine engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.Initialize();

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    const EntityHandle entity = MakeMeshRenderable(scene);
    const auto stableId = Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    EnableEdgeView(scene, entity);
    EnableVertexView(scene, entity);

    auto stats = extraction.ExtractAndSubmit(scene, engine.GetRenderer(), &engine.GetGpuAssetCache());
    ASSERT_EQ(stats.MeshEdgeViewUploads, 1u);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    ASSERT_EQ(gpuWorld.GetLiveInstanceCount(), 3u);
    ASSERT_EQ(gpuWorld.GetLiveGeometryCount(), 3u);

    // Disable just the edge view; the vertex view stays resident.
    scene.Raw().remove<Extrinsic::Graphics::Components::RenderEdges>(entity);

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

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    const EntityHandle entity = MakeMeshRenderable(scene);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    EnableEdgeView(scene, entity);
    EnableVertexView(scene, entity);

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

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    const EntityHandle entity = MakeMeshRenderable(scene);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    EnableEdgeView(scene, entity);
    EnableVertexView(scene, entity);

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

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    const EntityHandle entity = MakeMeshRenderable(scene);
    const auto stableId = Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    EnableEdgeView(scene, entity);
    EnableVertexView(scene, entity);

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

TEST(MeshPrimitiveViewExtraction, EdgeViewDerivesWireframeWithoutExplicitEdges)
{
    Extrinsic::Runtime::Engine engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.Initialize();

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    // Triangle mesh whose `Edges` PropertySet is empty (no explicit edges).
    const EntityHandle entity = MakeMeshRenderable(scene, /*withEdges=*/false);
    const auto stableId = Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    EnableEdgeView(scene, entity);

    const auto stats = extraction.ExtractAndSubmit(scene,
                                                   engine.GetRenderer(),
                                                   &engine.GetGpuAssetCache());

    EXPECT_EQ(stats.MeshGeometryUploads, 1u);
    EXPECT_EQ(stats.MeshEdgeViewUploads, 1u);
    EXPECT_EQ(stats.MeshEdgeViewMissingEdgeTopology, 0u);
    EXPECT_EQ(stats.MeshEdgeViewInvalidEdges, 0u);
    EXPECT_EQ(stats.MeshEdgeViewFailedPack, 0u);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    // Parent surface instance + derived edge view instance.
    EXPECT_EQ(gpuWorld.GetLiveInstanceCount(), 2u);
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 2u);

    const auto view = extraction.FindRenderableSidecarForTest(stableId);
    ASSERT_TRUE(view.has_value());
    EXPECT_TRUE(view->HasMeshResidency);
    EXPECT_TRUE(view->HasMeshEdgeView);
    EXPECT_TRUE(view->MeshEdgeViewInstance.IsValid());
    EXPECT_TRUE(view->MeshEdgeViewGeometry.IsValid());
    EXPECT_EQ(gpuWorld.GetInstanceGeometry(view->MeshEdgeViewInstance), view->MeshEdgeViewGeometry);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(MeshPrimitiveViewExtraction, OutOfRangeEdgeEndpointIncrementsInvalidEdgesCounter)
{
    Extrinsic::Runtime::Engine engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.Initialize();

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    auto& raw = scene.Raw();
    const EntityHandle entity = MakeMeshRenderable(scene);
    const auto stableId = Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity);

    // Corrupt one edge endpoint to reference a non-existent vertex.
    auto& edges = raw.get<gs::Edges>(entity);
    SetEdges(edges, /*v0*/ {0u, 1u, 2u}, /*v1*/ {1u, 2u, 9u});

    Extrinsic::Runtime::RenderExtractionCache extraction;
    EnableEdgeView(scene, entity);

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

TEST(MeshPrimitiveViewExtraction, NonMeshEntityWithRenderComponentsCreatesNoMeshViews)
{
    namespace G = Extrinsic::Graphics::Components;

    Extrinsic::Runtime::Engine engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.Initialize();

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    auto& raw = scene.Raw();
    const EntityHandle entity = scene.Create();
    raw.emplace<Extrinsic::ECS::Components::Transform::WorldMatrix>(entity).Matrix = glm::mat4{1.f};
    raw.emplace<G::RenderPoints>(entity);
    raw.emplace<G::RenderEdges>(entity);
    // Point-cloud shape: Vertices only → DetectDomain resolves PointCloud, not Mesh.
    auto& vertices = raw.emplace<gs::Vertices>(entity);
    SetPositions(vertices, {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}});
    const auto stableId = Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity);

    Extrinsic::Runtime::RenderExtractionCache extraction;

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
