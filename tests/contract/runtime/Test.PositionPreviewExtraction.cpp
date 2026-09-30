// GRAPHICS-156 (ADR 0030 decisions 5 and 7): extraction observes an entity's position ring
// front. While it exists the entity shows uncommitted positions (its blocks get the front,
// culling is bypassed, picks stay at the entity level); when it disappears the blocks are
// restored from the current CPU positions by a forced channel upload, so a CPU edit made
// during the preview shows after Discard. Null device: the block bytes are the CPU shadow.
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <entt/entity/entity.hpp>
#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include "GeometryResidencyFingerprint.hpp"

import Extrinsic.Core.Config.Engine;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Graphics.SceneHandles;
import Extrinsic.RHI.Handles;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.AssetWorkflowModule;
import Extrinsic.Runtime.SceneDocumentModule;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.StableEntityLookup;
import Extrinsic.Runtime.WorldHandle;
import Geometry.HalfedgeMesh;
import Geometry.Properties;

namespace gs = Extrinsic::ECS::Components::GeometrySources;
namespace pn = Extrinsic::ECS::Components::GeometrySources::PropertyNames;

using Extrinsic::ECS::EntityHandle;
using Extrinsic::ECS::Scene::Registry;
using Extrinsic::Runtime::GeometryElementDomain;
using Extrinsic::Runtime::GeometryPropertyRef;
using Extrinsic::Runtime::RenderExtractionCache;

namespace
{
    template <typename T>
    [[nodiscard]] T& RequiredEngineService(Extrinsic::Runtime::Engine& engine)
    {
        T* const service = engine.Services().Find<T>();
        EXPECT_NE(service, nullptr);
        return *service;
    }

    [[nodiscard]] Extrinsic::Core::Config::EngineConfig HeadlessConfig()
    {
        Extrinsic::Core::Config::EngineConfig config{};
        config.Simulation.WorkerThreadCount = 1u;
        config.ReferenceScene.Enabled = false;
        return config;
    }

    struct Fixture
    {
        Extrinsic::Runtime::Engine Engine{HeadlessConfig()};
        RenderExtractionCache Extraction{};
        // The front the fake residency serves for `v:position` refs, and the refs asked.
        std::optional<RenderExtractionCache::GpuPropertyFront> Front{};
        std::vector<GeometryPropertyRef> Asked{};

        Fixture()
        {
            Engine.EmplaceModule<Extrinsic::Runtime::SceneDocumentModule>();
            Engine.EmplaceModule<Extrinsic::Runtime::AssetWorkflowModule>();
            Engine.Initialize();
            Extraction.SetGpuPropertyObserver(
                [this](Extrinsic::Runtime::WorldHandle, entt::entity, const GeometryPropertyRef& ref)
                    -> std::optional<RenderExtractionCache::GpuPropertyFront> {
                    Asked.push_back(ref);
                    if (ref.ValueKind != Geometry::PropertyValueKind::Vec3) return std::nullopt;
                    return Front;
                });
        }
        ~Fixture()
        {
            Extraction.Shutdown(Engine.GetRenderer());
            Engine.Shutdown();
        }

        [[nodiscard]] Registry& Scene() { return *Engine.Worlds().Get(Engine.ActiveWorld()); }
        [[nodiscard]] Extrinsic::Runtime::RuntimeRenderExtractionStats Extract()
        {
            return Extraction.ExtractAndSubmit(
                Scene(), Engine.GetRenderer(), &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(Engine));
        }
        static RenderExtractionCache::GpuPropertyFront FrontFor(const std::uint32_t rows, const std::uint64_t stamp)
        {
            return {.Buffer = Extrinsic::RHI::BufferHandle{7u, 1u}, .Address = 0x7000u,
                    .Bytes = std::uint64_t(rows) * 12u, .Count = rows, .Stamp = stamp};
        }
        [[nodiscard]] Extrinsic::Graphics::GpuGeometryResidencyView View(const Extrinsic::Graphics::GpuGeometryHandle geometry)
        {
            Extrinsic::Graphics::GpuGeometryResidencyView view{};
            EXPECT_TRUE(Engine.GetRenderer().GetGpuWorld().TryGetGeometryResidencyView(geometry, view));
            return view;
        }
    };

    EntityHandle MakePointCloudRenderable(Registry& scene, const std::vector<glm::vec3>& positions)
    {
        namespace E = Extrinsic::ECS::Components;
        namespace G = Extrinsic::Graphics::Components;
        const EntityHandle entity = scene.Create();
        auto& raw = scene.Raw();
        raw.emplace<E::Transform::WorldMatrix>(entity).Matrix = glm::mat4{1.f};
        raw.emplace<G::RenderPoints>(entity);
        auto& vertices = raw.emplace<gs::Vertices>(entity);
        vertices.Properties.Resize(positions.size());
        vertices.Properties.GetOrAdd<glm::vec3>(std::string{pn::kPosition}, glm::vec3(0.0f)).Vector() = positions;
        return entity;
    }

    // The residency view's float32 fingerprint over the packed positions (three points).
    [[nodiscard]] std::uint64_t Fingerprint(const std::vector<glm::vec3>& p)
    {
        EXPECT_EQ(p.size(), 3u);
        return Extrinsic::Tests::GeometryFloat32Fingerprint(
            {p[0].x, p[0].y, p[0].z, p[1].x, p[1].y, p[1].z, p[2].x, p[2].y, p[2].z});
    }
}

TEST(PositionPreviewExtraction, AFrontShowsUncommittedPositionsAndDiscardRestoresTheConcurrentCpuEdit)
{
    Fixture f;
    const std::vector<glm::vec3> original{{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}};
    const EntityHandle entity = MakePointCloudRenderable(f.Scene(), original);
    const auto id = Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity);

    // No ring: the CPU positions are uploaded and refinement stays on.
    auto stats = f.Extract();
    EXPECT_EQ(stats.PointCloudGeometryUploads, 1u);
    EXPECT_EQ(stats.PositionPreviewsObserved, 0u);
    EXPECT_FALSE(f.Extraction.ShowsUncommittedPositions(id));
    ASSERT_FALSE(f.Asked.empty());
    EXPECT_EQ(f.Asked.back().Domain, GeometryElementDomain::PointCloudPoint);
    EXPECT_EQ(f.Asked.back().Name, "v:position");
    EXPECT_EQ(f.Asked.back().ValueKind, Geometry::PropertyValueKind::Vec3);
    const auto sidecar = f.Extraction.FindRenderableSidecarForTest(id);
    ASSERT_TRUE(sidecar.has_value());
    EXPECT_EQ(f.View(sidecar->PointCloudGeometry).PositionFingerprint, Fingerprint(original));

    // A ring front: the entity shows uncommitted positions; the block keeps its CPU shadow.
    f.Front = Fixture::FrontFor(3u, 1u);
    stats = f.Extract();
    EXPECT_EQ(stats.PositionPreviewsObserved, 1u);
    EXPECT_EQ(stats.PositionPreviewBlocksRejected, 0u);
    EXPECT_EQ(stats.PositionPreviewRestores, 0u);
    EXPECT_EQ(stats.PointCloudGeometryReuseHits, 1u);
    EXPECT_TRUE(f.Extraction.ShowsUncommittedPositions(id));
    EXPECT_TRUE(f.Extraction.FindRenderableSidecarForTest(id)->ShowsUncommittedPositions);

    // A concurrent CPU edit during the preview uploads as usual (the front is copied over it).
    const std::vector<glm::vec3> edited{{0.f, 0.f, 0.f}, {2.f, 0.f, 0.f}, {0.f, 2.f, 0.f}};
    f.Scene().Raw().get<gs::Vertices>(entity).Properties.Get<glm::vec3>(std::string{pn::kPosition}).Vector() = edited;
    stats = f.Extract();
    EXPECT_EQ(stats.PointCloudGeometryPartialUploads, 1u);
    EXPECT_EQ(stats.PositionPreviewsObserved, 1u);
    EXPECT_TRUE(f.Extraction.ShowsUncommittedPositions(id));

    // Discard (the ring is gone): the block is restored from the current CPU positions by a
    // forced upload although no revision changed since the last frame.
    f.Front.reset();
    stats = f.Extract();
    EXPECT_EQ(stats.PositionPreviewRestores, 1u);
    EXPECT_EQ(stats.PositionPreviewsObserved, 0u);
    EXPECT_EQ(stats.PointCloudGeometryPartialUploads, 1u) << "the restore is a forced position upload";
    EXPECT_FALSE(f.Extraction.ShowsUncommittedPositions(id));
    EXPECT_FALSE(f.Scene().Raw().any_of<Extrinsic::ECS::Components::DirtyTags::DirtyVertexPositions>(entity));
    const auto restored = f.View(f.Extraction.FindRenderableSidecarForTest(id)->PointCloudGeometry);
    EXPECT_EQ(restored.PositionFingerprint, Fingerprint(edited)) << "the edit made during the preview shows";
    EXPECT_FALSE(restored.PositionShadowStale);

    // A steady frame after the restore uploads nothing.
    stats = f.Extract();
    EXPECT_EQ(stats.PointCloudGeometryPartialUploads, 0u);
    EXPECT_EQ(stats.PositionPreviewRestores, 0u);
}

TEST(PositionPreviewExtraction, AFrontThatDoesNotFitTheBlockIsRejectedAndCounted)
{
    Fixture f;
    const EntityHandle entity = MakePointCloudRenderable(f.Scene(), {{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}});
    const auto id = Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity);
    f.Front = Fixture::FrontFor(2u, 1u); // two rows for a three-point block
    const auto stats = f.Extract();
    EXPECT_EQ(stats.PositionPreviewsObserved, 1u);
    EXPECT_EQ(stats.PositionPreviewBlocksRejected, 1u);
    EXPECT_TRUE(f.Extraction.ShowsUncommittedPositions(id)) << "the entity still counts as previewed (conservative)";
}

TEST(PositionPreviewExtraction, MeshesObserveTheMeshVertexPositionsAndHandTheSeamRemapToTheSurface)
{
    Fixture f;
    namespace E = Extrinsic::ECS::Components;
    namespace G = Extrinsic::Graphics::Components;
    Geometry::HalfedgeMesh::Mesh mesh;
    const auto a = mesh.AddVertex({0, 0, 0}), b = mesh.AddVertex({1, 0, 0}), c = mesh.AddVertex({0, 1, 0});
    ASSERT_TRUE(mesh.AddTriangle(a, b, c));
    auto& scene = f.Scene();
    const EntityHandle entity = scene.Create();
    scene.Raw().emplace<E::Transform::WorldMatrix>(entity).Matrix = glm::mat4{1.f};
    scene.Raw().emplace<G::RenderSurface>(entity);
    scene.Raw().emplace<G::RenderPoints>(entity);
    gs::PopulateFromMesh(scene.Raw(), entity, mesh);
    const auto id = Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity);

    f.Front = Fixture::FrontFor(3u, 1u);
    const auto stats = f.Extract();
    EXPECT_EQ(stats.MeshGeometryUploads, 1u);
    EXPECT_EQ(stats.PositionPreviewsObserved, 1u);
    EXPECT_EQ(stats.PositionPreviewBlocksRejected, 0u) << "surface and vertex view blocks both accept the front";
    EXPECT_TRUE(f.Extraction.ShowsUncommittedPositions(id));
    ASSERT_FALSE(f.Asked.empty());
    EXPECT_EQ(f.Asked.back().Domain, GeometryElementDomain::MeshVertex);
    const auto sidecar = f.Extraction.FindRenderableSidecarForTest(id);
    ASSERT_TRUE(sidecar.has_value());
    EXPECT_TRUE(sidecar->HasMeshResidency);
    EXPECT_TRUE(sidecar->HasMeshVertexView);
}
