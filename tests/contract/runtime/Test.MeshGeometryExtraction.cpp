#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <glm/glm.hpp>

import Extrinsic.Core.Config.Engine;
import Extrinsic.ECS.Components.AssetInstance;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Component.ProceduralGeometryRef;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.Component.GpuSceneSlot;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.RenderExtraction;
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
        config.ReferenceScene.Enabled = false;
        return config;
    }

    void SetPositions(gs::Vertices& v, const std::vector<glm::vec3>& positions)
    {
        v.Properties.Resize(positions.size());
        auto pos = v.Properties.GetOrAdd<glm::vec3>(std::string{pn::kPosition}, glm::vec3(0.0f));
        pos.Vector() = positions;
    }

    void SetHalfedges(gs::Halfedges& h,
                      const std::vector<std::uint32_t>& toVertex,
                      const std::vector<std::uint32_t>& next,
                      const std::vector<std::uint32_t>& face)
    {
        h.Properties.Resize(toVertex.size());
        auto pt = h.Properties.GetOrAdd<std::uint32_t>(std::string{pn::kHalfedgeToVertex}, kInvalidIndex);
        auto pn_ = h.Properties.GetOrAdd<std::uint32_t>(std::string{pn::kHalfedgeNext}, kInvalidIndex);
        auto pf = h.Properties.GetOrAdd<std::uint32_t>(std::string{pn::kHalfedgeFace}, kInvalidIndex);
        pt.Vector() = toVertex;
        pn_.Vector() = next;
        pf.Vector() = face;
    }

    void SetFaces(gs::Faces& f, const std::vector<std::uint32_t>& faceHe)
    {
        f.Properties.Resize(faceHe.size());
        auto p = f.Properties.GetOrAdd<std::uint32_t>(std::string{pn::kFaceHalfedge}, kInvalidIndex);
        p.Vector() = faceHe;
    }

    // Attach a single-triangle mesh `GeometrySources` (Vertices+Halfedges+Faces)
    // to `entity` so `BuildConstView` resolves `Domain::Mesh`. Halfedge wiring
    // mirrors `Test.MeshGeometryPacker.cpp::BuildSingleTriangle` so the packer
    // emits exactly three surface indices (1, 2, 0).
    void AttachTriangleMeshSources(Registry& scene, EntityHandle entity)
    {
        auto& raw = scene.Raw();
        auto& vertices = raw.emplace<gs::Vertices>(entity);
        SetPositions(vertices, {
            {0.0f, 0.0f, 0.0f},
            {1.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f},
        });
        auto& edges = raw.emplace<gs::Edges>(entity);
        (void)edges; // empty Edges PropertySet is sufficient for `DetectDomain`.
        auto& halfedges = raw.emplace<gs::Halfedges>(entity);
        SetHalfedges(halfedges,
                     /*toVertex*/ {1u, 2u, 0u, 0u, 2u, 1u},
                     /*next*/     {1u, 2u, 0u, 5u, 3u, 4u},
                     /*face*/     {0u, 0u, 0u, kInvalidIndex, kInvalidIndex, kInvalidIndex});
        auto& faces = raw.emplace<gs::Faces>(entity);
        SetFaces(faces, {0u});
    }

    EntityHandle MakeMeshRenderable(Registry& scene)
    {
        namespace E = Extrinsic::ECS::Components;
        namespace G = Extrinsic::Graphics::Components;
        const EntityHandle entity = scene.Create();
        auto& raw = scene.Raw();
        raw.emplace<E::Transform::WorldMatrix>(entity).Matrix = glm::mat4{1.f};
        raw.emplace<G::RenderSurface>(entity);
        AttachTriangleMeshSources(scene, entity);
        return entity;
    }
}

TEST(MeshGeometryExtraction, SingleMeshEntityUploadsOnceAndBindsInstanceGeometry)
{
    Extrinsic::Runtime::Engine engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.Initialize();

    auto& scene = engine.GetScene();
    const EntityHandle entity = MakeMeshRenderable(scene);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    const auto stats = extraction.ExtractAndSubmit(scene,
                                                    engine.GetRenderer(),
                                                    &engine.GetGpuAssetCache());

    EXPECT_EQ(stats.CandidateRenderableCount, 1u);
    EXPECT_EQ(stats.AllocatedInstanceCount, 1u);
    EXPECT_EQ(stats.MeshGeometryUploads, 1u);
    EXPECT_EQ(stats.MeshGeometryReuseHits, 0u);
    EXPECT_EQ(stats.MeshGeometryFailedPack, 0u);
    EXPECT_EQ(stats.MeshGeometryMissingPositions, 0u);
    EXPECT_EQ(stats.MeshGeometryInvalidTopology, 0u);
    EXPECT_EQ(stats.MeshGeometryReleases, 0u);
    EXPECT_EQ(stats.ProceduralRenderablesEnumerated, 0u);
    EXPECT_EQ(stats.ProceduralGeometryUploads, 0u);
    EXPECT_EQ(stats.SourceAssetObservationCount, 0u);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    EXPECT_EQ(gpuWorld.GetLiveInstanceCount(), 1u);
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 1u);

    const auto stableId = static_cast<std::uint32_t>(entity);
    const auto view = extraction.FindRenderableSidecarForTest(stableId);
    ASSERT_TRUE(view.has_value());
    EXPECT_TRUE(view->Instance.IsValid());
    EXPECT_TRUE(view->Geometry.IsValid());
    EXPECT_TRUE(view->HasMeshResidency);
    EXPECT_EQ(view->MeshGeometry, view->Geometry);
    EXPECT_FALSE(view->ProceduralKey.has_value());
    EXPECT_FALSE(view->HasSourceAsset);
    EXPECT_EQ(gpuWorld.GetInstanceGeometry(view->Instance), view->Geometry);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(MeshGeometryExtraction, RepeatedExtractionReusesMeshHandleWithoutReupload)
{
    Extrinsic::Runtime::Engine engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.Initialize();

    auto& scene = engine.GetScene();
    const EntityHandle entity = MakeMeshRenderable(scene);
    (void)entity;

    Extrinsic::Runtime::RenderExtractionCache extraction;

    auto stats = extraction.ExtractAndSubmit(scene,
                                              engine.GetRenderer(),
                                              &engine.GetGpuAssetCache());
    ASSERT_EQ(stats.MeshGeometryUploads, 1u);
    ASSERT_EQ(stats.MeshGeometryReuseHits, 0u);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    ASSERT_EQ(gpuWorld.GetLiveGeometryCount(), 1u);

    const auto firstView =
        extraction.FindRenderableSidecarForTest(static_cast<std::uint32_t>(entity));
    ASSERT_TRUE(firstView.has_value());
    const auto firstHandle = firstView->MeshGeometry;

    stats = extraction.ExtractAndSubmit(scene,
                                         engine.GetRenderer(),
                                         &engine.GetGpuAssetCache());
    EXPECT_EQ(stats.MeshGeometryUploads, 0u);
    EXPECT_EQ(stats.MeshGeometryReuseHits, 1u);
    EXPECT_EQ(stats.MeshGeometryReleases, 0u);
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 1u);

    const auto secondView =
        extraction.FindRenderableSidecarForTest(static_cast<std::uint32_t>(entity));
    ASSERT_TRUE(secondView.has_value());
    EXPECT_EQ(secondView->MeshGeometry, firstHandle);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(MeshGeometryExtraction, TwoMeshEntitiesAllocateIndependentMeshUploads)
{
    Extrinsic::Runtime::Engine engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.Initialize();

    auto& scene = engine.GetScene();
    (void)MakeMeshRenderable(scene);
    (void)MakeMeshRenderable(scene);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    const auto stats = extraction.ExtractAndSubmit(scene,
                                                    engine.GetRenderer(),
                                                    &engine.GetGpuAssetCache());

    // Slice B does not share mesh uploads across entities; that is the
    // procedural cache's pattern, not the runtime mesh bridge's.
    EXPECT_EQ(stats.CandidateRenderableCount, 2u);
    EXPECT_EQ(stats.AllocatedInstanceCount, 2u);
    EXPECT_EQ(stats.MeshGeometryUploads, 2u);
    EXPECT_EQ(stats.MeshGeometryReuseHits, 0u);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    EXPECT_EQ(gpuWorld.GetLiveInstanceCount(), 2u);
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 2u);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(MeshGeometryExtraction, EntityDestructionFreesMeshGeometryAndIncrementsRelease)
{
    Extrinsic::Runtime::Engine engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.Initialize();

    auto& scene = engine.GetScene();
    const EntityHandle entity = MakeMeshRenderable(scene);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    auto stats = extraction.ExtractAndSubmit(scene,
                                              engine.GetRenderer(),
                                              &engine.GetGpuAssetCache());
    ASSERT_EQ(stats.MeshGeometryUploads, 1u);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    ASSERT_EQ(gpuWorld.GetLiveGeometryCount(), 1u);

    scene.Destroy(entity);

    stats = extraction.ExtractAndSubmit(scene,
                                         engine.GetRenderer(),
                                         &engine.GetGpuAssetCache());
    EXPECT_EQ(stats.FreedInstanceCount, 1u);
    EXPECT_EQ(stats.MeshGeometryReleases, 1u);
    // Slice B frees immediately; the deferred-retire window is Slice C.
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 0u);
    EXPECT_EQ(gpuWorld.GetLiveInstanceCount(), 0u);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(MeshGeometryExtraction, ShutdownReleasesPendingMeshResidency)
{
    Extrinsic::Runtime::Engine engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.Initialize();

    auto& scene = engine.GetScene();
    (void)MakeMeshRenderable(scene);
    (void)MakeMeshRenderable(scene);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    auto stats = extraction.ExtractAndSubmit(scene,
                                              engine.GetRenderer(),
                                              &engine.GetGpuAssetCache());
    ASSERT_EQ(stats.MeshGeometryUploads, 2u);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    ASSERT_EQ(gpuWorld.GetLiveGeometryCount(), 2u);

    extraction.Shutdown(engine.GetRenderer());
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 0u);
    EXPECT_EQ(gpuWorld.GetLiveInstanceCount(), 0u);
    EXPECT_EQ(extraction.GetLastStats().MeshGeometryReleases, 2u);

    engine.Shutdown();
}

TEST(MeshGeometryExtraction, ProceduralRefPreemptsMeshPathOnSameEntity)
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
    AttachTriangleMeshSources(scene, entity);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    const auto stats = extraction.ExtractAndSubmit(scene,
                                                    engine.GetRenderer(),
                                                    &engine.GetGpuAssetCache());

    // Procedural intent declared → mesh path must not run, regardless of
    // mesh-domain GeometrySources being attached.
    EXPECT_EQ(stats.ProceduralRenderablesEnumerated, 1u);
    EXPECT_EQ(stats.ProceduralGeometryUploads, 1u);
    EXPECT_EQ(stats.MeshGeometryUploads, 0u);
    EXPECT_EQ(stats.MeshGeometryFailedPack, 0u);

    const auto view = extraction.FindRenderableSidecarForTest(
        static_cast<std::uint32_t>(entity));
    ASSERT_TRUE(view.has_value());
    EXPECT_FALSE(view->HasMeshResidency);
    EXPECT_TRUE(view->ProceduralKey.has_value());

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(MeshGeometryExtraction, AssetSourcePresentPreemptsMeshPathOnSameEntity)
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
    raw.emplace<E::AssetInstance::Source>(entity).AssetId = 17u;
    AttachTriangleMeshSources(scene, entity);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    const auto stats = extraction.ExtractAndSubmit(scene,
                                                    engine.GetRenderer(),
                                                    &engine.GetGpuAssetCache());

    EXPECT_EQ(stats.SourceAssetObservationCount, 1u);
    EXPECT_EQ(stats.MeshGeometryUploads, 0u);
    EXPECT_EQ(stats.MeshGeometryFailedPack, 0u);

    const auto view = extraction.FindRenderableSidecarForTest(
        static_cast<std::uint32_t>(entity));
    ASSERT_TRUE(view.has_value());
    EXPECT_FALSE(view->HasMeshResidency);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(MeshGeometryExtraction, MissingPositionsIncrementsMissingPositionsCounter)
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

    // Mesh-domain GeometrySources but the Vertices PropertySet carries no
    // `v:position` — the packer must report `MissingPositions` and the
    // extraction layer must fold that into the dedicated counter rather
    // than the generic `FailedPack`.
    auto& vertices = raw.emplace<gs::Vertices>(entity);
    (void)vertices; // empty PropertySet → no `v:position`.
    (void)raw.emplace<gs::Edges>(entity);
    auto& halfedges = raw.emplace<gs::Halfedges>(entity);
    SetHalfedges(halfedges, {0u}, {0u}, {0u});
    auto& faces = raw.emplace<gs::Faces>(entity);
    SetFaces(faces, {0u});

    Extrinsic::Runtime::RenderExtractionCache extraction;
    const auto stats = extraction.ExtractAndSubmit(scene,
                                                    engine.GetRenderer(),
                                                    &engine.GetGpuAssetCache());

    EXPECT_EQ(stats.MeshGeometryUploads, 0u);
    EXPECT_EQ(stats.MeshGeometryMissingPositions, 1u);
    EXPECT_EQ(stats.MeshGeometryInvalidTopology, 0u);
    EXPECT_EQ(stats.MeshGeometryFailedPack, 0u);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 0u);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(MeshGeometryExtraction, InvalidTopologyIncrementsInvalidTopologyCounter)
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

    auto& vertices = raw.emplace<gs::Vertices>(entity);
    SetPositions(vertices, {
        {0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
    });
    (void)raw.emplace<gs::Edges>(entity);
    auto& halfedges = raw.emplace<gs::Halfedges>(entity);
    SetHalfedges(halfedges,
                 /*toVertex*/ {1u, 2u, 0u},
                 /*next*/     {1u, 2u, 0u},
                 /*face*/     {0u, 0u, 0u});
    auto& faces = raw.emplace<gs::Faces>(entity);
    // `f:halfedge` references halfedge 99 — out of range.
    SetFaces(faces, {99u});

    Extrinsic::Runtime::RenderExtractionCache extraction;
    const auto stats = extraction.ExtractAndSubmit(scene,
                                                    engine.GetRenderer(),
                                                    &engine.GetGpuAssetCache());

    EXPECT_EQ(stats.MeshGeometryUploads, 0u);
    EXPECT_EQ(stats.MeshGeometryInvalidTopology, 1u);
    EXPECT_EQ(stats.MeshGeometryMissingPositions, 0u);
    EXPECT_EQ(stats.MeshGeometryFailedPack, 0u);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(MeshGeometryExtraction, DegenerateAllFacesIncrementsFailedPackCounter)
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

    auto& vertices = raw.emplace<gs::Vertices>(entity);
    SetPositions(vertices, {
        {0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
    });
    (void)raw.emplace<gs::Edges>(entity);
    auto& halfedges = raw.emplace<gs::Halfedges>(entity);
    SetHalfedges(halfedges,
                 /*toVertex*/ {1u, 2u, 0u},
                 /*next*/     {1u, 2u, 0u},
                 /*face*/     {0u, 0u, 0u});
    auto& faces = raw.emplace<gs::Faces>(entity);
    // Every face slot points at an invalid halfedge sentinel → packer
    // walks no rings and returns `DegenerateAllFaces`, which Slice B
    // folds into the generic `FailedPack` bucket.
    SetFaces(faces, {kInvalidIndex, kInvalidIndex});

    Extrinsic::Runtime::RenderExtractionCache extraction;
    const auto stats = extraction.ExtractAndSubmit(scene,
                                                    engine.GetRenderer(),
                                                    &engine.GetGpuAssetCache());

    EXPECT_EQ(stats.MeshGeometryUploads, 0u);
    EXPECT_EQ(stats.MeshGeometryFailedPack, 1u);
    EXPECT_EQ(stats.MeshGeometryMissingPositions, 0u);
    EXPECT_EQ(stats.MeshGeometryInvalidTopology, 0u);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(MeshGeometryExtraction, AddingProceduralRefAfterMeshUploadReleasesMeshResidency)
{
    namespace E = Extrinsic::ECS::Components;

    Extrinsic::Runtime::Engine engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.Initialize();

    auto& scene = engine.GetScene();
    const EntityHandle entity = MakeMeshRenderable(scene);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    auto stats = extraction.ExtractAndSubmit(scene,
                                              engine.GetRenderer(),
                                              &engine.GetGpuAssetCache());
    ASSERT_EQ(stats.MeshGeometryUploads, 1u);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    ASSERT_EQ(gpuWorld.GetLiveGeometryCount(), 1u);

    // Eligibility flip: a procedural ref appears on the same entity. The
    // mesh path must release its cached upload this frame and the
    // procedural path must take over the instance binding.
    scene.Raw().emplace<E::ProceduralGeometryRef>(entity);

    stats = extraction.ExtractAndSubmit(scene,
                                         engine.GetRenderer(),
                                         &engine.GetGpuAssetCache());
    EXPECT_EQ(stats.MeshGeometryUploads, 0u);
    EXPECT_EQ(stats.MeshGeometryReuseHits, 0u);
    EXPECT_EQ(stats.MeshGeometryReleases, 1u);
    EXPECT_EQ(stats.ProceduralGeometryUploads, 1u);

    const auto view = extraction.FindRenderableSidecarForTest(
        static_cast<std::uint32_t>(entity));
    ASSERT_TRUE(view.has_value());
    EXPECT_FALSE(view->HasMeshResidency);
    ASSERT_TRUE(view->ProceduralKey.has_value());
    // Instance is bound to the procedural handle, not a freed mesh slot.
    EXPECT_TRUE(view->Geometry.IsValid());
    EXPECT_EQ(gpuWorld.GetInstanceGeometry(view->Instance), view->Geometry);
    // Only the procedural geometry remains live.
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 1u);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(MeshGeometryExtraction, AddingAssetSourceAfterMeshUploadReleasesMeshResidency)
{
    namespace E = Extrinsic::ECS::Components;

    Extrinsic::Runtime::Engine engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.Initialize();

    auto& scene = engine.GetScene();
    const EntityHandle entity = MakeMeshRenderable(scene);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    auto stats = extraction.ExtractAndSubmit(scene,
                                              engine.GetRenderer(),
                                              &engine.GetGpuAssetCache());
    ASSERT_EQ(stats.MeshGeometryUploads, 1u);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    ASSERT_EQ(gpuWorld.GetLiveGeometryCount(), 1u);

    // Eligibility flip: an asset source appears on the same entity. The
    // mesh upload is released; the asset observation path runs in its
    // place. Asset observation never sets instance geometry, so the
    // instance is left detached (no live geometry) until later asset
    // wiring takes over (out of scope for Slice B).
    scene.Raw().emplace<E::AssetInstance::Source>(entity).AssetId = 17u;

    stats = extraction.ExtractAndSubmit(scene,
                                         engine.GetRenderer(),
                                         &engine.GetGpuAssetCache());
    EXPECT_EQ(stats.MeshGeometryReleases, 1u);
    EXPECT_EQ(stats.MeshGeometryReuseHits, 0u);
    EXPECT_EQ(stats.SourceAssetObservationCount, 1u);

    const auto view = extraction.FindRenderableSidecarForTest(
        static_cast<std::uint32_t>(entity));
    ASSERT_TRUE(view.has_value());
    EXPECT_FALSE(view->HasMeshResidency);
    // The mesh handle has been freed; the instance no longer points at
    // any live geometry slot.
    EXPECT_FALSE(gpuWorld.GetInstanceGeometry(view->Instance).IsValid());
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 0u);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(MeshGeometryExtraction, LosingMeshDomainTopologyReleasesMeshResidency)
{
    Extrinsic::Runtime::Engine engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.Initialize();

    auto& scene = engine.GetScene();
    const EntityHandle entity = MakeMeshRenderable(scene);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    auto stats = extraction.ExtractAndSubmit(scene,
                                              engine.GetRenderer(),
                                              &engine.GetGpuAssetCache());
    ASSERT_EQ(stats.MeshGeometryUploads, 1u);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    ASSERT_EQ(gpuWorld.GetLiveGeometryCount(), 1u);

    // Eligibility flip: the entity drops its mesh-domain topology
    // (e.g., upstream code re-populated as a point cloud). `DetectDomain`
    // no longer resolves `Domain::Mesh`, so the mesh path must release
    // the cached upload even though no procedural / asset path has
    // taken over.
    auto& raw = scene.Raw();
    raw.remove<gs::Halfedges>(entity);
    raw.remove<gs::Faces>(entity);
    raw.remove<gs::Edges>(entity);

    stats = extraction.ExtractAndSubmit(scene,
                                         engine.GetRenderer(),
                                         &engine.GetGpuAssetCache());
    EXPECT_EQ(stats.MeshGeometryUploads, 0u);
    EXPECT_EQ(stats.MeshGeometryReuseHits, 0u);
    EXPECT_EQ(stats.MeshGeometryReleases, 1u);

    const auto view = extraction.FindRenderableSidecarForTest(
        static_cast<std::uint32_t>(entity));
    ASSERT_TRUE(view.has_value());
    EXPECT_FALSE(view->HasMeshResidency);
    EXPECT_FALSE(gpuWorld.GetInstanceGeometry(view->Instance).IsValid());
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 0u);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(MeshGeometryExtraction, NonMeshDomainEntityIsIgnoredByMeshPath)
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
    // Point-cloud shape: Vertices only, no Halfedges/Faces. DetectDomain
    // returns PointCloud → mesh path must not fire.
    auto& vertices = raw.emplace<gs::Vertices>(entity);
    SetPositions(vertices, {{0.0f, 0.0f, 0.0f}});

    Extrinsic::Runtime::RenderExtractionCache extraction;
    const auto stats = extraction.ExtractAndSubmit(scene,
                                                    engine.GetRenderer(),
                                                    &engine.GetGpuAssetCache());

    EXPECT_EQ(stats.MeshGeometryUploads, 0u);
    EXPECT_EQ(stats.MeshGeometryFailedPack, 0u);
    EXPECT_EQ(stats.MeshGeometryMissingPositions, 0u);
    EXPECT_EQ(stats.MeshGeometryInvalidTopology, 0u);

    const auto view = extraction.FindRenderableSidecarForTest(
        static_cast<std::uint32_t>(entity));
    ASSERT_TRUE(view.has_value());
    EXPECT_FALSE(view->HasMeshResidency);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}
