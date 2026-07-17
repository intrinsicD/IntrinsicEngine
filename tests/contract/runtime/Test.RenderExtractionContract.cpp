// Extraction-boundary contract tests: semantic filtering (render hints),
// fail-closed draw-candidate publication, and snapshot isolation from live
// ECS storage. Domain-specific residency behavior lives in
// Test.{Mesh,Graph,PointCloud,Procedural}GeometryExtraction.cpp; this file
// covers the cross-domain boundary guarantees documented on
// the Engine-owned render-extraction service.

#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <glm/glm.hpp>

import Extrinsic.Core.Config.Engine;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.RenderWorld;
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

    // Single-triangle mesh sources; halfedge wiring mirrors
    // Test.MeshGeometryExtraction.cpp::AttachTriangleMeshSources.
    void AttachTriangleMeshSources(Registry& scene, const EntityHandle entity)
    {
        auto& raw = scene.Raw();
        auto& vertices = raw.emplace<gs::Vertices>(entity);
        SetPositions(vertices, {
            {0.0f, 0.0f, 0.0f},
            {1.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f},
        });
        (void)raw.emplace<gs::Edges>(entity);
        auto& halfedges = raw.emplace<gs::Halfedges>(entity);
        SetHalfedges(halfedges,
                     /*toVertex*/ {1u, 2u, 0u, 0u, 2u, 1u},
                     /*next*/     {1u, 2u, 0u, 5u, 3u, 4u},
                     /*face*/     {0u, 0u, 0u, kInvalidIndex, kInvalidIndex, kInvalidIndex});
        auto& faces = raw.emplace<gs::Faces>(entity);
        SetFaces(faces, {0u});
    }
}

// An entity carrying only a WorldMatrix — no RenderSurface/RenderEdges/
// RenderPoints hint — must not become a renderable candidate, must not
// allocate a GPU instance, and must not publish a draw candidate.
TEST(RenderExtractionContract, WorldMatrixWithoutRenderHintPublishesNoDrawCandidate)
{
    namespace E = Extrinsic::ECS::Components;

    Extrinsic::Runtime::Engine engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.Initialize();

    auto& scene = engine.GetScene();
    const EntityHandle entity = scene.Create();
    scene.Raw().emplace<E::Transform::WorldMatrix>(entity).Matrix = glm::mat4{1.f};

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    const std::uint32_t liveInstancesBefore = gpuWorld.GetLiveInstanceCount();

    Extrinsic::Runtime::RenderExtractionCache extraction;
    const auto stats = extraction.ExtractAndSubmit(scene, engine.GetRenderer());

    EXPECT_EQ(stats.CandidateRenderableCount, 0u);
    EXPECT_EQ(stats.SubmittedTransformCount, 0u);
    EXPECT_EQ(stats.AllocatedInstanceCount, 0u);
    EXPECT_EQ(gpuWorld.GetLiveInstanceCount(), liveInstancesBefore);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

// A renderable-hinted mesh entity whose geometry fails to pack (no
// `v:position`) is counted as a candidate but must fail closed: no geometry
// upload and no draw candidate in the submitted snapshot.
TEST(RenderExtractionContract, FailedMeshPackPublishesNoDrawCandidate)
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

    // Mesh-domain sources without `v:position`: the packer reports
    // MissingPositions and the entity must not publish a draw candidate.
    (void)raw.emplace<gs::Vertices>(entity);
    (void)raw.emplace<gs::Edges>(entity);
    auto& halfedges = raw.emplace<gs::Halfedges>(entity);
    SetHalfedges(halfedges, {0u}, {0u}, {0u});
    auto& faces = raw.emplace<gs::Faces>(entity);
    SetFaces(faces, {0u});

    Extrinsic::Runtime::RenderExtractionCache extraction;
    const auto stats = extraction.ExtractAndSubmit(scene,
                                                   engine.GetRenderer(),
                                                   &engine.GetGpuAssetCache());

    EXPECT_EQ(stats.CandidateRenderableCount, 1u);
    EXPECT_EQ(stats.MeshGeometryUploads, 0u);
    EXPECT_EQ(stats.MeshGeometryMissingPositions, 1u);
    EXPECT_EQ(stats.SubmittedTransformCount, 0u);
    EXPECT_EQ(engine.GetRenderer().GetGpuWorld().GetLiveGeometryCount(), 0u);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

// The submitted snapshot must be renderer-owned: mutating and destroying the
// live ECS entity after ExtractAndSubmit must not change what
// ExtractRenderWorld returns for the already-submitted slot.
TEST(RenderExtractionContract, SnapshotSurvivesLiveEcsMutationAndDestruction)
{
    namespace E = Extrinsic::ECS::Components;
    namespace G = Extrinsic::Graphics::Components;

    Extrinsic::Runtime::Engine engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.Initialize();

    auto& scene = engine.GetScene();
    auto& raw = scene.Raw();
    const EntityHandle entity = scene.Create();
    glm::mat4 model{1.f};
    model[3] = glm::vec4{5.f, 0.f, 0.f, 1.f};
    raw.emplace<E::Transform::WorldMatrix>(entity).Matrix = model;
    raw.emplace<G::RenderSurface>(entity);
    AttachTriangleMeshSources(scene, entity);

    Extrinsic::Runtime::RenderExtractionCache extraction;
    const auto stats = extraction.ExtractAndSubmit(scene,
                                                   engine.GetRenderer(),
                                                   &engine.GetGpuAssetCache());
    ASSERT_EQ(stats.SubmittedTransformCount, 1u);

    // Mutate the live component, then destroy the entity entirely. If the
    // snapshot aliased live ECS storage, the values below would change.
    raw.get<E::Transform::WorldMatrix>(entity).Matrix[3] =
        glm::vec4{1000.f, 0.f, 0.f, 1.f};
    scene.Destroy(entity);

    const Extrinsic::Graphics::RenderWorld world =
        engine.GetRenderer().ExtractRenderWorld(Extrinsic::Graphics::RenderFrameInput{
            .Alpha = 0.f,
            .Viewport = {64u, 64u},
        });

    ASSERT_EQ(world.Renderables.size(), 1u);
    // Default extraction bounds center on the model translation captured at
    // extraction time (5, not 1000).
    EXPECT_FLOAT_EQ(world.Renderables[0].Bounds.WorldSphere.x, 5.f);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}
