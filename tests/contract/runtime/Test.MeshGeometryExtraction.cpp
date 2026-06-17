#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <glm/glm.hpp>

import Extrinsic.Asset.Registry;
import Extrinsic.Core.Config.Engine;
import Extrinsic.ECS.Components.AssetInstance;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Component.ProceduralGeometryRef;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.Component.GpuSceneSlot;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Graphics.Material;
import Extrinsic.Graphics.Renderer;
import Extrinsic.RHI.Bindless;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.SamplerManager;
import Extrinsic.RHI.TextureManager;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.ProgressiveRenderData;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.StableEntityLookup;
import Geometry.Properties;

#include "MockRHI.hpp"

namespace gs = Extrinsic::ECS::Components::GeometrySources;
namespace pn = Extrinsic::ECS::Components::GeometrySources::PropertyNames;

using Extrinsic::ECS::EntityHandle;
using Extrinsic::ECS::Scene::Registry;

namespace
{
    constexpr std::uint32_t kInvalidIndex = std::numeric_limits<std::uint32_t>::max();
    std::array<std::byte, 64u> ZeroTextureBytes{};

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

    void SetTexcoords(gs::Vertices& v, const std::vector<glm::vec2>& texcoords)
    {
        auto uv = v.Properties.GetOrAdd<glm::vec2>("v:texcoord", glm::vec2(0.0f));
        uv.Vector() = texcoords;
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
        SetTexcoords(vertices, {
            {0.0f, 0.0f},
            {1.0f, 0.0f},
            {0.0f, 1.0f},
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

    Extrinsic::RHI::TextureDesc TestTextureDesc()
    {
        return Extrinsic::RHI::TextureDesc{
            .Width = 4u,
            .Height = 4u,
            .MipLevels = 1u,
            .Fmt = Extrinsic::RHI::Format::RGBA8_UNORM,
            .Usage = Extrinsic::RHI::TextureUsage::Sampled |
                     Extrinsic::RHI::TextureUsage::TransferDst,
            .DebugName = "runtime-extraction-material-binding-texture",
        };
    }

    Extrinsic::RHI::SamplerDesc TestSamplerDesc()
    {
        return Extrinsic::RHI::SamplerDesc{
            .MagFilter = Extrinsic::RHI::FilterMode::Nearest,
            .MinFilter = Extrinsic::RHI::FilterMode::Nearest,
            .MipFilter = Extrinsic::RHI::MipmapMode::Nearest,
            .AddressU = Extrinsic::RHI::AddressMode::ClampToEdge,
            .AddressV = Extrinsic::RHI::AddressMode::ClampToEdge,
            .AddressW = Extrinsic::RHI::AddressMode::ClampToEdge,
            .DebugName = "runtime-extraction-material-binding-sampler",
        };
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
    EXPECT_EQ(stats.MeshGeometryMissingTexcoords, 0u);
    EXPECT_EQ(stats.MeshGeometryNonFiniteTexcoords, 0u);
    EXPECT_EQ(stats.MeshGeometryReleases, 0u);
    EXPECT_EQ(stats.ProceduralRenderablesEnumerated, 0u);
    EXPECT_EQ(stats.ProceduralGeometryUploads, 0u);
    EXPECT_EQ(stats.SourceAssetObservationCount, 0u);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    EXPECT_EQ(gpuWorld.GetLiveInstanceCount(), 1u);
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 1u);

    const auto stableId = Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity);
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

TEST(MeshGeometryExtraction, MaterialTextureBindingsResolveOntoExtractionMaterialSidecar)
{
    Extrinsic::Tests::MockDevice device{};
    Extrinsic::RHI::BufferManager buffers{device};
    Extrinsic::RHI::TextureManager textures{device, device.Bindless};
    Extrinsic::RHI::SamplerManager samplers{device};
    Extrinsic::Tests::MockTransferQueue transfer{};
    Extrinsic::Graphics::GpuAssetCache cache{buffers, textures, samplers, transfer};
    ASSERT_TRUE(cache.InitializeFallbackTexture(
        Extrinsic::Graphics::GpuTextureFallbackDesc{
            .Bytes = std::span{ZeroTextureBytes},
            .Desc = TestTextureDesc(),
            .SamplerDesc = TestSamplerDesc(),
        }).has_value());

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer =
        Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    Registry scene;
    const EntityHandle entity = MakeMeshRenderable(scene);
    const auto stableId = Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity);
    const Extrinsic::Assets::AssetId normalAsset{91u, 1u};

    Extrinsic::Runtime::RenderExtractionCache extraction;
    extraction.SetMaterialTextureAssetBindings(
        stableId,
        Extrinsic::Graphics::MaterialTextureAssetBindings{.Normal = normalAsset});

    const auto stats = extraction.ExtractAndSubmit(scene, *renderer, &cache);
    EXPECT_EQ(stats.MaterialTextureBindingRecordCount, 1u);
    EXPECT_EQ(stats.MaterialTextureBindingResolveCount, 1u);
    EXPECT_EQ(stats.MaterialTextureBindingResolveFailureCount, 0u);

    const auto sidecar = extraction.FindRenderableSidecarForTest(stableId);
    ASSERT_TRUE(sidecar.has_value());
    ASSERT_TRUE(sidecar->HasMaterialLease);
    const Extrinsic::Graphics::MaterialParams params =
        renderer->GetMaterialSystem().GetParams(sidecar->MaterialHandle);
    EXPECT_NE(params.NormalID, Extrinsic::RHI::kInvalidBindlessIndex);

    extraction.Shutdown(*renderer);
    renderer->Shutdown();
}

TEST(MeshGeometryExtraction, ProgressivePresentationBindingsAreConsumedDuringExtraction)
{
    Extrinsic::Tests::MockDevice device{};
    Extrinsic::RHI::BufferManager buffers{device};
    Extrinsic::RHI::TextureManager textures{device, device.Bindless};
    Extrinsic::RHI::SamplerManager samplers{device};
    Extrinsic::Tests::MockTransferQueue transfer{};
    Extrinsic::Graphics::GpuAssetCache cache{buffers, textures, samplers, transfer};
    ASSERT_TRUE(cache.InitializeFallbackTexture(
        Extrinsic::Graphics::GpuTextureFallbackDesc{
            .Bytes = std::span{ZeroTextureBytes},
            .Desc = TestTextureDesc(),
            .SamplerDesc = TestSamplerDesc(),
        }).has_value());

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer =
        Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    Registry scene;
    const EntityHandle entity = MakeMeshRenderable(scene);

    namespace R = Extrinsic::Runtime;
    R::ProgressiveSlotBinding albedo{};
    albedo.Semantic = R::ProgressiveSlotSemantic::Albedo;
    albedo.SourceKind = R::ProgressiveSlotSourceKind::UniformDefault;
    albedo.UniformDefault.Vector = {0.15f, 0.25f, 0.35f, 1.0f};

    R::ProgressiveSlotBinding normal{};
    normal.Semantic = R::ProgressiveSlotSemantic::Normal;
    normal.SourceKind = R::ProgressiveSlotSourceKind::GeneratedTextureAsset;
    normal.GeneratedTexture = Extrinsic::Assets::AssetId{123u, 1u};
    normal.Readiness = R::ProgressiveReadinessState::Ready;

    scene.Raw().emplace_or_replace<R::ProgressivePresentationBindings>(
        entity,
        R::ProgressivePresentationBindings{
            .Shape = R::ProgressiveEntityShape::MeshLeaf,
            .Lanes = {R::ProgressiveRenderLaneBinding{
                .Lane = R::ProgressiveRenderLane::Surface,
                .PresentationKey = "mesh.surface",
            }},
            .Presentations = {R::ProgressivePresentationBinding{
                .Key = "mesh.surface",
                .Kind = R::ProgressivePresentationKind::SurfaceMaterial,
                .Slots = {albedo, normal},
            }},
        });

    Extrinsic::Runtime::RenderExtractionCache extraction;
    const auto stats = extraction.ExtractAndSubmit(scene, *renderer, &cache);

    EXPECT_EQ(stats.ProgressivePresentationEntityCount, 1u);
    EXPECT_EQ(stats.ProgressivePresentationLaneCount, 1u);
    EXPECT_EQ(stats.ProgressivePresentationSlotCount, 2u);
    EXPECT_EQ(stats.ProgressiveDefaultSlotCount, 1u);
    EXPECT_EQ(stats.ProgressiveReadyTextureSlotCount, 1u);
    EXPECT_EQ(stats.ProgressiveMaterialTextureBindingResolveCount, 1u);
    EXPECT_EQ(stats.ProgressiveMaterialTextureBindingResolveFailureCount, 0u);

    const auto sidecar = extraction.FindRenderableSidecarForTest(
        Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity));
    ASSERT_TRUE(sidecar.has_value());
    ASSERT_TRUE(sidecar->HasMaterialLease);
    const Extrinsic::Graphics::MaterialParams params =
        renderer->GetMaterialSystem().GetParams(sidecar->MaterialHandle);
    EXPECT_NE(params.NormalID, Extrinsic::RHI::kInvalidBindlessIndex);

    extraction.Shutdown(*renderer);
    renderer->Shutdown();
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
        extraction.FindRenderableSidecarForTest(Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity));
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
        extraction.FindRenderableSidecarForTest(Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity));
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

TEST(MeshGeometryExtraction, EntityDestructionRetiresMeshGeometryAfterDeferredWindow)
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
    EXPECT_EQ(stats.MeshGeometryFreeRetires, 0u);
    // Slice C — release is deferred through the same framesInFlight
    // window the procedural cache uses; the upload is still live until
    // TickMeshGeometry fires past the deadline.
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 1u);
    EXPECT_EQ(gpuWorld.GetLiveInstanceCount(), 0u);

    constexpr std::uint32_t framesInFlight = 2u;
    constexpr std::uint64_t baseFrame = 200u;
    for (std::uint32_t i = 0; i <= framesInFlight; ++i)
    {
        extraction.TickMeshGeometry(baseFrame + i, framesInFlight, engine.GetRenderer());
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
    EXPECT_EQ(stats.MeshGeometryFreeRetires, 1u);

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
        Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity));
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
        Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity));
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

TEST(MeshGeometryExtraction, MissingTexcoordsUploadsWithDefaultUvFallback)
{
    Extrinsic::Runtime::Engine engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.Initialize();

    auto& scene = engine.GetScene();
    auto& raw = scene.Raw();
    const EntityHandle entity = MakeMeshRenderable(scene);

    auto& vertices = raw.get<gs::Vertices>(entity);
    auto& registry = vertices.Properties.Registry();
    const auto id = registry.Find("v:texcoord");
    ASSERT_TRUE(id.has_value());
    ASSERT_TRUE(registry.Remove(*id));

    Extrinsic::Runtime::RenderExtractionCache extraction;
    const auto stats = extraction.ExtractAndSubmit(scene,
                                                    engine.GetRenderer(),
                                                    &engine.GetGpuAssetCache());

    EXPECT_EQ(stats.MeshGeometryUploads, 1u);
    EXPECT_EQ(stats.MeshGeometryMissingTexcoords, 1u);
    EXPECT_EQ(stats.MeshGeometryNonFiniteTexcoords, 0u);
    EXPECT_EQ(stats.MeshGeometryMissingPositions, 0u);
    EXPECT_EQ(stats.MeshGeometryInvalidTopology, 0u);
    EXPECT_EQ(stats.MeshGeometryFailedPack, 0u);
    EXPECT_EQ(engine.GetRenderer().GetGpuWorld().GetLiveGeometryCount(), 1u);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(MeshGeometryExtraction, NonFiniteTexcoordsUploadWithDefaultUvFallback)
{
    Extrinsic::Runtime::Engine engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.Initialize();

    auto& scene = engine.GetScene();
    auto& raw = scene.Raw();
    const EntityHandle entity = MakeMeshRenderable(scene);

    auto& vertices = raw.get<gs::Vertices>(entity);
    auto texcoords = vertices.Properties.Get<glm::vec2>("v:texcoord");
    ASSERT_TRUE(texcoords);
    texcoords.Vector()[1].y = std::numeric_limits<float>::infinity();

    Extrinsic::Runtime::RenderExtractionCache extraction;
    const auto stats = extraction.ExtractAndSubmit(scene,
                                                    engine.GetRenderer(),
                                                    &engine.GetGpuAssetCache());

    EXPECT_EQ(stats.MeshGeometryUploads, 1u);
    EXPECT_EQ(stats.MeshGeometryMissingTexcoords, 0u);
    EXPECT_EQ(stats.MeshGeometryNonFiniteTexcoords, 1u);
    EXPECT_EQ(stats.MeshGeometryMissingPositions, 0u);
    EXPECT_EQ(stats.MeshGeometryInvalidTopology, 0u);
    EXPECT_EQ(stats.MeshGeometryFailedPack, 0u);
    EXPECT_EQ(engine.GetRenderer().GetGpuWorld().GetLiveGeometryCount(), 1u);

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
    SetTexcoords(vertices, {
        {0.0f, 0.0f},
        {1.0f, 0.0f},
        {0.0f, 1.0f},
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
    EXPECT_EQ(stats.MeshGeometryMissingTexcoords, 0u);
    EXPECT_EQ(stats.MeshGeometryNonFiniteTexcoords, 0u);
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
    SetTexcoords(vertices, {
        {0.0f, 0.0f},
        {1.0f, 0.0f},
        {0.0f, 1.0f},
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
    EXPECT_EQ(stats.MeshGeometryMissingTexcoords, 0u);
    EXPECT_EQ(stats.MeshGeometryNonFiniteTexcoords, 0u);

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
    EXPECT_EQ(stats.MeshGeometryFreeRetires, 0u);
    EXPECT_EQ(stats.ProceduralGeometryUploads, 1u);

    const auto view = extraction.FindRenderableSidecarForTest(
        Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity));
    ASSERT_TRUE(view.has_value());
    EXPECT_FALSE(view->HasMeshResidency);
    ASSERT_TRUE(view->ProceduralKey.has_value());
    // Instance is bound to the procedural handle, not the queued mesh slot.
    EXPECT_TRUE(view->Geometry.IsValid());
    EXPECT_EQ(gpuWorld.GetInstanceGeometry(view->Instance), view->Geometry);
    // Slice C — the old mesh slot is queued for deferred retire and
    // remains live until TickMeshGeometry fires past the deadline. So
    // both the procedural slot AND the queued mesh slot are live now.
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 2u);

    constexpr std::uint32_t framesInFlight = 2u;
    constexpr std::uint64_t baseFrame = 400u;
    for (std::uint32_t i = 0; i <= framesInFlight; ++i)
    {
        extraction.TickMeshGeometry(baseFrame + i, framesInFlight, engine.GetRenderer());
    }
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 1u);
    stats = extraction.ExtractAndSubmit(scene,
                                         engine.GetRenderer(),
                                         &engine.GetGpuAssetCache());
    EXPECT_EQ(stats.MeshGeometryFreeRetires, 1u);

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
    EXPECT_EQ(stats.MeshGeometryFreeRetires, 0u);
    EXPECT_EQ(stats.SourceAssetObservationCount, 1u);

    const auto view = extraction.FindRenderableSidecarForTest(
        Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity));
    ASSERT_TRUE(view.has_value());
    EXPECT_FALSE(view->HasMeshResidency);
    // Slice C — the instance is explicitly detached from the queued
    // mesh slot, but the slot itself is still live until
    // TickMeshGeometry fires past the deadline.
    EXPECT_FALSE(gpuWorld.GetInstanceGeometry(view->Instance).IsValid());
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 1u);

    constexpr std::uint32_t framesInFlight = 2u;
    constexpr std::uint64_t baseFrame = 500u;
    for (std::uint32_t i = 0; i <= framesInFlight; ++i)
    {
        extraction.TickMeshGeometry(baseFrame + i, framesInFlight, engine.GetRenderer());
    }
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 0u);
    stats = extraction.ExtractAndSubmit(scene,
                                         engine.GetRenderer(),
                                         &engine.GetGpuAssetCache());
    EXPECT_EQ(stats.MeshGeometryFreeRetires, 1u);

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
    EXPECT_EQ(stats.MeshGeometryFreeRetires, 0u);

    const auto view = extraction.FindRenderableSidecarForTest(
        Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity));
    ASSERT_TRUE(view.has_value());
    EXPECT_FALSE(view->HasMeshResidency);
    EXPECT_FALSE(gpuWorld.GetInstanceGeometry(view->Instance).IsValid());
    // Slice C — released mesh slot is still live during the deferred-
    // retire window.
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 1u);

    constexpr std::uint32_t framesInFlight = 2u;
    constexpr std::uint64_t baseFrame = 600u;
    for (std::uint32_t i = 0; i <= framesInFlight; ++i)
    {
        extraction.TickMeshGeometry(baseFrame + i, framesInFlight, engine.GetRenderer());
    }
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
        Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity));
    ASSERT_TRUE(view.has_value());
    EXPECT_FALSE(view->HasMeshResidency);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

namespace
{
    // Drive the deferred-retire window on the runtime mesh queue past the
    // anchor deadline so queued handles are actually freed. Mirrors the
    // helper pattern used in `Test.ProceduralGeometryExtraction.cpp`.
    void DriveMeshDeferredRetireWindow(Extrinsic::Runtime::RenderExtractionCache& extraction,
                                       Extrinsic::Graphics::IRenderer& renderer,
                                       std::uint64_t baseFrame,
                                       std::uint32_t framesInFlight)
    {
        for (std::uint32_t i = 0; i <= framesInFlight; ++i)
        {
            extraction.TickMeshGeometry(baseFrame + i, framesInFlight, renderer);
        }
    }
}

// RUNTIME-085 Slice C — dirty-domain reupload coverage. Each fine-grained
// dirty tag and the coarse `GpuDirty` must individually trigger a repack +
// upload, queue the old handle for deferred retire, surface the change as
// `MeshGeometryReuploads` + `MeshGeometryReleases`, and drain the tag(s) so
// subsequent clean ticks return to the `MeshGeometryReuseHits` path.
class MeshGeometryExtractionDirtyTag
    : public ::testing::TestWithParam<const char*>
{
};

TEST_P(MeshGeometryExtractionDirtyTag, DirtyTagTriggersReupload)
{
    namespace D = Extrinsic::ECS::Components::DirtyTags;

    Extrinsic::Runtime::Engine engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.Initialize();

    auto& scene = engine.GetScene();
    auto& raw = scene.Raw();
    const EntityHandle entity = MakeMeshRenderable(scene);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    auto stats = extraction.ExtractAndSubmit(scene,
                                              engine.GetRenderer(),
                                              &engine.GetGpuAssetCache());
    ASSERT_EQ(stats.MeshGeometryUploads, 1u);
    ASSERT_EQ(stats.MeshGeometryReuploads, 0u);

    const auto firstView =
        extraction.FindRenderableSidecarForTest(Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity));
    ASSERT_TRUE(firstView.has_value());
    const auto firstHandle = firstView->MeshGeometry;
    ASSERT_TRUE(firstHandle.IsValid());

    // Stamp exactly one dirty domain on the entity.
    const std::string param{GetParam()};
    if (param == "GpuDirty")
        D::MarkGpuDirty(raw, entity);
    else if (param == "DirtyVertexPositions")
        D::MarkVertexPositionsDirty(raw, entity);
    else if (param == "DirtyFaceTopology")
        D::MarkFaceTopologyDirty(raw, entity);
    else if (param == "DirtyEdgeTopology")
        D::MarkEdgeTopologyDirty(raw, entity);
    else
        FAIL() << "Unhandled dirty-tag parameterization: " << param;

    stats = extraction.ExtractAndSubmit(scene,
                                         engine.GetRenderer(),
                                         &engine.GetGpuAssetCache());
    EXPECT_EQ(stats.MeshGeometryUploads, 0u);
    EXPECT_EQ(stats.MeshGeometryReuploads, 1u);
    EXPECT_EQ(stats.MeshGeometryReuseHits, 0u);
    EXPECT_EQ(stats.MeshGeometryReleases, 1u);
    EXPECT_EQ(stats.MeshGeometryFreeRetires, 0u);

    const auto secondView =
        extraction.FindRenderableSidecarForTest(Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity));
    ASSERT_TRUE(secondView.has_value());
    EXPECT_TRUE(secondView->HasMeshResidency);
    EXPECT_NE(secondView->MeshGeometry, firstHandle);
    EXPECT_EQ(secondView->Geometry, secondView->MeshGeometry);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    // Both the old (queued) and the new (live) handle are alive until
    // the deferred-retire window fires.
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 2u);
    EXPECT_EQ(gpuWorld.GetInstanceGeometry(secondView->Instance), secondView->MeshGeometry);

    // The dirty tag has been drained, so a third tick returns to the
    // reuse path with no reupload.
    EXPECT_FALSE((raw.any_of<D::GpuDirty,
                              D::DirtyVertexPositions,
                              D::DirtyFaceTopology,
                              D::DirtyEdgeTopology>(entity)));

    stats = extraction.ExtractAndSubmit(scene,
                                         engine.GetRenderer(),
                                         &engine.GetGpuAssetCache());
    EXPECT_EQ(stats.MeshGeometryReuploads, 0u);
    EXPECT_EQ(stats.MeshGeometryReuseHits, 1u);
    EXPECT_EQ(stats.MeshGeometryReleases, 0u);

    // Drive the deferred-retire window: the old handle finally fires.
    constexpr std::uint32_t framesInFlight = 2u;
    DriveMeshDeferredRetireWindow(extraction,
                                   engine.GetRenderer(),
                                   /*baseFrame=*/700u,
                                   framesInFlight);
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 1u);

    stats = extraction.ExtractAndSubmit(scene,
                                         engine.GetRenderer(),
                                         &engine.GetGpuAssetCache());
    EXPECT_EQ(stats.MeshGeometryFreeRetires, 1u);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

INSTANTIATE_TEST_SUITE_P(AllDirtyDomains,
                          MeshGeometryExtractionDirtyTag,
                          ::testing::Values("GpuDirty",
                                            "DirtyVertexPositions",
                                            "DirtyFaceTopology",
                                            "DirtyEdgeTopology"));

TEST(MeshGeometryExtraction, MultipleDirtyTagsCoalesceIntoSingleReupload)
{
    namespace D = Extrinsic::ECS::Components::DirtyTags;

    Extrinsic::Runtime::Engine engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.Initialize();

    auto& scene = engine.GetScene();
    auto& raw = scene.Raw();
    const EntityHandle entity = MakeMeshRenderable(scene);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    auto stats = extraction.ExtractAndSubmit(scene,
                                              engine.GetRenderer(),
                                              &engine.GetGpuAssetCache());
    ASSERT_EQ(stats.MeshGeometryUploads, 1u);

    // Stamp every dirty tag at once. The reupload semantics are
    // pack-and-replace, not per-tag re-upload, so the per-frame
    // reupload count must remain 1 regardless of how many tags fire.
    D::MarkGpuDirty(raw, entity);
    D::MarkVertexPositionsDirty(raw, entity);
    D::MarkFaceTopologyDirty(raw, entity);
    D::MarkEdgeTopologyDirty(raw, entity);

    stats = extraction.ExtractAndSubmit(scene,
                                         engine.GetRenderer(),
                                         &engine.GetGpuAssetCache());
    EXPECT_EQ(stats.MeshGeometryReuploads, 1u);
    EXPECT_EQ(stats.MeshGeometryReleases, 1u);
    EXPECT_EQ(stats.MeshGeometryReuseHits, 0u);

    EXPECT_FALSE((raw.any_of<D::GpuDirty,
                              D::DirtyVertexPositions,
                              D::DirtyFaceTopology,
                              D::DirtyEdgeTopology>(entity)));

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(MeshGeometryExtraction, ReuploadFailureReleasesStaleResidencyAndPreservesDirtyTag)
{
    namespace D = Extrinsic::ECS::Components::DirtyTags;

    Extrinsic::Runtime::Engine engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.Initialize();

    auto& scene = engine.GetScene();
    auto& raw = scene.Raw();
    const EntityHandle entity = MakeMeshRenderable(scene);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    auto stats = extraction.ExtractAndSubmit(scene,
                                              engine.GetRenderer(),
                                              &engine.GetGpuAssetCache());
    ASSERT_EQ(stats.MeshGeometryUploads, 1u);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    const auto firstView =
        extraction.FindRenderableSidecarForTest(Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity));
    ASSERT_TRUE(firstView.has_value());

    // Corrupt the mesh so the next pack returns `InvalidTopology` (face
    // points at an out-of-range halfedge), then mark the entity dirty.
    auto& faces = raw.get<gs::Faces>(entity);
    auto faceHe = faces.Properties.GetOrAdd<std::uint32_t>(std::string{pn::kFaceHalfedge}, kInvalidIndex);
    faceHe.Vector() = {99u};
    D::MarkFaceTopologyDirty(raw, entity);

    stats = extraction.ExtractAndSubmit(scene,
                                         engine.GetRenderer(),
                                         &engine.GetGpuAssetCache());
    EXPECT_EQ(stats.MeshGeometryReuploads, 0u);
    EXPECT_EQ(stats.MeshGeometryInvalidTopology, 1u);
    EXPECT_EQ(stats.MeshGeometryReuseHits, 0u);
    // Fail-closed: the stale upload is released (deferred) rather than left
    // bound, so invalid topology does not keep rendering the last-good frame.
    EXPECT_EQ(stats.MeshGeometryReleases, 1u);
    EXPECT_EQ(stats.MeshGeometryFreeRetires, 0u);

    const auto secondView =
        extraction.FindRenderableSidecarForTest(Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity));
    ASSERT_TRUE(secondView.has_value());
    EXPECT_FALSE(secondView->HasMeshResidency);
    EXPECT_FALSE(gpuWorld.GetInstanceGeometry(secondView->Instance).IsValid());
    // Dirty tag is preserved so a later frame re-attempts once the input is
    // fixed; the released slot stays live through the deferred-retire window.
    EXPECT_TRUE(raw.any_of<D::DirtyFaceTopology>(entity));
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 1u);

    constexpr std::uint32_t framesInFlight = 2u;
    DriveMeshDeferredRetireWindow(extraction,
                                  engine.GetRenderer(),
                                  /*baseFrame=*/800u,
                                  framesInFlight);
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 0u);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}
