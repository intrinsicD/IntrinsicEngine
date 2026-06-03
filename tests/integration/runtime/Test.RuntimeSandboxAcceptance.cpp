// RUNTIME-095 Slice 1 — CPU/null working-sandbox acceptance.
//
// Composes the promoted runtime/graphics path end to end on the Null backend:
// one mesh, one graph, and one point cloud authored through promoted ECS
// `GeometrySources`, extracted once through `RenderExtractionCache`, proving all
// three residency lanes upload and bind distinct `GpuWorld` instance/geometry
// handles; a runtime camera controller producing a finite/invertible frame
// camera; runtime whole-entity selection over the acceptance scene; and the
// sandbox editor panel frame enumerating the scene. This is the headless
// acceptance for Theme A; the opt-in `gpu;vulkan` default-recipe present smoke
// is the deferred Operational slice.

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <glm/glm.hpp>

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Geometry2D;
import Extrinsic.ECS.Component.MetaData;
import Extrinsic.ECS.Component.StableId;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.Selection;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.CameraSnapshots;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Platform.Input;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.SandboxEditorUi;
import Extrinsic.Runtime.SelectionController;
import Geometry.Properties;

namespace ECS = Extrinsic::ECS;
namespace ECSC = Extrinsic::ECS::Components;
namespace gs = Extrinsic::ECS::Components::GeometrySources;
namespace pn = Extrinsic::ECS::Components::GeometrySources::PropertyNames;
namespace G = Extrinsic::Graphics::Components;
namespace Runtime = Extrinsic::Runtime;
namespace Core = Extrinsic::Core;
namespace Graphics = Extrinsic::Graphics;

using Extrinsic::ECS::EntityHandle;
using Extrinsic::ECS::Scene::Registry;

namespace
{
    constexpr std::uint32_t kInvalidIndex = std::numeric_limits<std::uint32_t>::max();

    class StubApplication final : public Runtime::IApplication
    {
    public:
        void OnInitialize(Runtime::Engine&) override {}
        void OnSimTick(Runtime::Engine&, double) override {}
        void OnVariableTick(Runtime::Engine&, double, double) override {}
        void OnShutdown(Runtime::Engine&) override {}
    };

    [[nodiscard]] Core::Config::EngineConfig HeadlessConfig()
    {
        Core::Config::EngineConfig config{};
        config.ReferenceScene.Enabled = false;
        return config;
    }

    // Stamp the common selectable/identifiable/transform components every
    // acceptance entity needs to appear in the editor hierarchy and be
    // selectable, plus a durable StableId.
    void StampCommon(Registry& scene, EntityHandle entity, std::string name, std::uint32_t stableId)
    {
        auto& raw = scene.Raw();
        raw.emplace<ECSC::MetaData>(entity, std::move(name));
        raw.emplace<ECSC::Transform::Component>(entity);
        raw.emplace<ECSC::Transform::WorldMatrix>(entity).Matrix = glm::mat4{1.f};
        raw.emplace<ECSC::Selection::SelectableTag>(entity);
        raw.emplace<ECSC::StableId>(entity, ECSC::StableId{stableId, 1u});
    }

    void SetPositions(Geometry::PropertySet& props, const std::vector<glm::vec3>& positions)
    {
        props.Resize(positions.size());
        auto pos = props.GetOrAdd<glm::vec3>(std::string{pn::kPosition}, glm::vec3(0.0f));
        pos.Vector() = positions;
    }

    EntityHandle MakeMesh(Registry& scene)
    {
        const EntityHandle entity = scene.Create();
        StampCommon(scene, entity, "AcceptanceMesh", 101u);
        auto& raw = scene.Raw();
        raw.emplace<G::RenderSurface>(entity);

        auto& vertices = raw.emplace<gs::Vertices>(entity);
        SetPositions(vertices.Properties, {{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}});
        raw.emplace<gs::Edges>(entity);
        auto& halfedges = raw.emplace<gs::Halfedges>(entity);
        halfedges.Properties.Resize(6);
        halfedges.Properties.GetOrAdd<std::uint32_t>(std::string{pn::kHalfedgeToVertex}, kInvalidIndex).Vector() =
            {1u, 2u, 0u, 0u, 2u, 1u};
        halfedges.Properties.GetOrAdd<std::uint32_t>(std::string{pn::kHalfedgeNext}, kInvalidIndex).Vector() =
            {1u, 2u, 0u, 5u, 3u, 4u};
        halfedges.Properties.GetOrAdd<std::uint32_t>(std::string{pn::kHalfedgeFace}, kInvalidIndex).Vector() =
            {0u, 0u, 0u, kInvalidIndex, kInvalidIndex, kInvalidIndex};
        auto& faces = raw.emplace<gs::Faces>(entity);
        faces.Properties.Resize(1);
        faces.Properties.GetOrAdd<std::uint32_t>(std::string{pn::kFaceHalfedge}, kInvalidIndex).Vector() = {0u};
        return entity;
    }

    EntityHandle MakeGraph(Registry& scene)
    {
        const EntityHandle entity = scene.Create();
        StampCommon(scene, entity, "AcceptanceGraph", 102u);
        auto& raw = scene.Raw();
        raw.emplace<G::RenderLines>(entity);

        auto& nodes = raw.emplace<gs::Nodes>(entity);
        SetPositions(nodes.Properties, {{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}});
        auto& edges = raw.emplace<gs::Edges>(entity);
        edges.Properties.Resize(2);
        edges.Properties.GetOrAdd<std::uint32_t>(std::string{pn::kEdgeV0}, kInvalidIndex).Vector() = {0u, 1u};
        edges.Properties.GetOrAdd<std::uint32_t>(std::string{pn::kEdgeV1}, kInvalidIndex).Vector() = {1u, 2u};
        raw.emplace<gs::HasGraphTopology>(entity);
        return entity;
    }

    EntityHandle MakePointCloud(Registry& scene)
    {
        const EntityHandle entity = scene.Create();
        StampCommon(scene, entity, "AcceptanceCloud", 103u);
        auto& raw = scene.Raw();
        raw.emplace<G::RenderPoints>(entity); // default uniform float SizeSource.

        auto& vertices = raw.emplace<gs::Vertices>(entity);
        SetPositions(vertices.Properties, {{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {1.f, 1.f, 0.f}});
        return entity;
    }
}

// The mesh, graph, and point-cloud residency lanes all upload exactly once and
// bind three distinct live instance/geometry pairs through one extraction.
TEST(RuntimeSandboxAcceptance, MeshGraphPointCloudAllResideThroughOneExtraction)
{
    Runtime::Engine engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.Initialize();
    auto& scene = engine.GetScene();

    const EntityHandle mesh = MakeMesh(scene);
    const EntityHandle graph = MakeGraph(scene);
    const EntityHandle cloud = MakePointCloud(scene);

    Runtime::RenderExtractionCache extraction;
    const auto stats = extraction.ExtractAndSubmit(scene,
                                                   engine.GetRenderer(),
                                                   &engine.GetGpuAssetCache());

    EXPECT_EQ(stats.CandidateRenderableCount, 3u);
    EXPECT_EQ(stats.AllocatedInstanceCount, 3u);
    EXPECT_EQ(stats.MeshGeometryUploads, 1u);
    EXPECT_EQ(stats.GraphGeometryUploads, 1u);
    EXPECT_EQ(stats.PointCloudGeometryUploads, 1u);
    EXPECT_EQ(stats.MeshGeometryFailedPack, 0u);
    EXPECT_EQ(stats.GraphGeometryFailedPack, 0u);
    EXPECT_EQ(stats.PointCloudGeometryFailedPack, 0u);

    auto& gpuWorld = engine.GetRenderer().GetGpuWorld();
    EXPECT_EQ(gpuWorld.GetLiveInstanceCount(), 3u);
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 3u);

    const auto meshView = extraction.FindRenderableSidecarForTest(static_cast<std::uint32_t>(mesh));
    const auto graphView = extraction.FindRenderableSidecarForTest(static_cast<std::uint32_t>(graph));
    const auto cloudView = extraction.FindRenderableSidecarForTest(static_cast<std::uint32_t>(cloud));
    ASSERT_TRUE(meshView.has_value());
    ASSERT_TRUE(graphView.has_value());
    ASSERT_TRUE(cloudView.has_value());

    EXPECT_TRUE(meshView->HasMeshResidency);
    EXPECT_TRUE(graphView->HasGraphResidency);
    EXPECT_TRUE(cloudView->HasPointCloudResidency);

    // Each family bound a distinct geometry handle.
    EXPECT_NE(meshView->Geometry, graphView->Geometry);
    EXPECT_NE(meshView->Geometry, cloudView->Geometry);
    EXPECT_NE(graphView->Geometry, cloudView->Geometry);

    // Re-extraction reuses every lane (no spurious re-upload of a static scene).
    const auto reuse = extraction.ExtractAndSubmit(scene,
                                                   engine.GetRenderer(),
                                                   &engine.GetGpuAssetCache());
    EXPECT_EQ(reuse.MeshGeometryUploads, 0u);
    EXPECT_EQ(reuse.GraphGeometryUploads, 0u);
    EXPECT_EQ(reuse.PointCloudGeometryUploads, 0u);
    EXPECT_EQ(gpuWorld.GetLiveInstanceCount(), 3u);
    EXPECT_EQ(gpuWorld.GetLiveGeometryCount(), 3u);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

// A runtime camera controller produces a finite, invertible frame camera for
// the acceptance viewport.
TEST(RuntimeSandboxAcceptance, CameraControllerProducesFiniteInvertibleFrameCamera)
{
    Runtime::OrbitCameraController controller{};
    Extrinsic::Platform::Input::Context input{};
    input.Update();
    controller.Update(input, 1.0 / 60.0);

    const Graphics::CameraViewInput view = controller.GetView(Core::Extent2D{1280, 720});
    EXPECT_TRUE(view.Valid);

    const Graphics::CameraViewSnapshot snapshot =
        Graphics::BuildCameraViewSnapshot(view, Core::Extent2D{1280, 720});
    ASSERT_TRUE(snapshot.Valid);
    // Invertible: VP * inverse(VP) ~= identity.
    const glm::mat4 product = snapshot.ViewProjection * snapshot.InverseViewProjection;
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            EXPECT_NEAR(product[c][r], c == r ? 1.f : 0.f, 1.0e-3f);
}

// Runtime selection works for an entity of each geometry family.
TEST(RuntimeSandboxAcceptance, SelectionControllerSelectsEntityOfEachFamily)
{
    Runtime::Engine engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.Initialize();
    auto& scene = engine.GetScene();

    const EntityHandle mesh = MakeMesh(scene);
    const EntityHandle graph = MakeGraph(scene);
    const EntityHandle cloud = MakePointCloud(scene);

    Runtime::SelectionController& selection = engine.GetSelectionController();

    for (const EntityHandle entity : {mesh, graph, cloud})
    {
        ASSERT_TRUE(selection.SetSelectedEntity(scene, entity));
        ASSERT_EQ(selection.SelectedCount(), 1u);
        EXPECT_TRUE(selection.IsSelected(entity));
        ASSERT_EQ(selection.SelectedStableIds().size(), 1u);
        EXPECT_EQ(selection.SelectedStableIds()[0],
                  Runtime::SelectionController::ToStableEntityId(entity));
    }

    engine.Shutdown();
}

// The sandbox editor panel frame enumerates the full acceptance scene and
// reports selection state without owning engine state.
TEST(RuntimeSandboxAcceptance, EditorPanelFrameEnumeratesAcceptanceScene)
{
    Runtime::Engine engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.Initialize();
    auto& scene = engine.GetScene();

    const EntityHandle mesh = MakeMesh(scene);
    (void)MakeGraph(scene);
    (void)MakePointCloud(scene);

    Runtime::SelectionController& selection = engine.GetSelectionController();
    ASSERT_TRUE(selection.SetSelectedEntity(scene, mesh));

    const Runtime::SandboxEditorContext context{
        .Scene = &scene,
        .Selection = &selection,
        .ImGuiAdapterAvailable = true,
    };
    const Runtime::SandboxEditorPanelFrame frame = Runtime::BuildSandboxEditorPanelFrame(context);

    EXPECT_EQ(frame.Hierarchy.size(), 3u);
    const auto meshRow = std::find_if(frame.Hierarchy.begin(), frame.Hierarchy.end(),
        [mesh](const Runtime::SandboxEditorEntityRow& row) { return row.Entity == mesh; });
    ASSERT_NE(meshRow, frame.Hierarchy.end());
    EXPECT_EQ(meshRow->Name, "AcceptanceMesh");
    EXPECT_TRUE(meshRow->Selectable);
    EXPECT_TRUE(meshRow->Selected);
    EXPECT_TRUE(meshRow->HasDurableStableId);

    ASSERT_EQ(frame.Selection.SelectedStableIds.size(), 1u);
    EXPECT_EQ(frame.Selection.SelectedStableIds[0],
              Runtime::SelectionController::ToStableEntityId(mesh));

    engine.Shutdown();
}
