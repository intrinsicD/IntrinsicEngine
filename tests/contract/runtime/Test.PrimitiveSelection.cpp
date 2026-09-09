#include <array>
#include <cstdint>
#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <gtest/gtest.h>
#include <vector>
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Extrinsic.ECS.Components.Selection;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.SceneInteractionModule;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.GeometryProcessingOperations;
import Geometry.HalfedgeMesh;
import Geometry.Graph;
import Geometry.PointCloud;
namespace R = Extrinsic::Runtime;
namespace GS = Extrinsic::ECS::Components::GeometrySources;
using D = R::GeometryElementDomain;
using Edit = R::PrimitiveSelectionEdit;
using Status = R::PrimitiveSelectionStatus;
namespace
{
    struct Harness
    {
        Extrinsic::ECS::Scene::Registry Scene;
        R::SelectionController Selection;
        Extrinsic::ECS::EntityHandle Entity;
        std::uint32_t Id;
        Harness()
        {
            Geometry::HalfedgeMesh::Mesh mesh;
            const auto a = mesh.AddVertex({0, 0, 0}), b = mesh.AddVertex({1, 0, 0});
            const auto c = mesh.AddVertex({1, 1, 0}), d = mesh.AddVertex({0, 1, 0});
            EXPECT_TRUE(mesh.AddTriangle(a, b, c));
            EXPECT_TRUE(mesh.AddTriangle(a, c, d));
            Entity = Scene.Create();
            GS::PopulateFromMesh(Scene.Raw(), Entity, mesh);
            Scene.Raw().emplace<Extrinsic::ECS::Components::Selection::SelectableTag>(Entity);
            Id = R::SelectionController::ToStableEntityId(Entity);
        }
        R::PrimitiveSelectionSnapshot Change(Edit edit, std::initializer_list<std::uint32_t> values,
                                             D domain = D::MeshVertex)
        {
            return Selection.EditPrimitives(Scene, Id, domain, edit,
                                            {values.begin(), values.size()});
        }
        auto Read(D domain = D::MeshVertex) const
        {
            return Selection.ReadPrimitives(Scene, Id, domain);
        }
    };
} // namespace
TEST(PrimitiveSelection, OrderedUniqueSetsAndAtomicInvalidEdits)
{
    Harness h;
    EXPECT_EQ(h.Change(Edit::Replace, {2, 0, 2}).Indices, (std::vector<std::uint32_t>{2, 0}));
    EXPECT_EQ(h.Change(Edit::Add, {0, 1}).Indices, (std::vector<std::uint32_t>{2, 0, 1}));
    EXPECT_EQ(h.Change(Edit::Toggle, {0, 0, 3}).Indices, (std::vector<std::uint32_t>{2, 1, 3}));
    EXPECT_EQ(h.Change(Edit::Add, {0}).Indices, (std::vector<std::uint32_t>{2, 1, 3, 0}));
    const auto generation = h.Selection.SelectionGeneration();
    EXPECT_EQ(h.Change(Edit::Add, {1, 999}).Status, Status::InvalidIndex);
    EXPECT_EQ(h.Selection.SelectionGeneration(), generation);
    EXPECT_EQ(h.Read().Indices, (std::vector<std::uint32_t>{2, 1, 3, 0}));
    EXPECT_EQ(h.Change(Edit::Remove, {2, 3}).Indices, (std::vector<std::uint32_t>{1, 0}));
    EXPECT_EQ(h.Change(Edit::Invert, {}).Indices, (std::vector<std::uint32_t>{2, 3}));
    EXPECT_EQ(h.Change(Edit::All, {}).Indices, (std::vector<std::uint32_t>{0, 1, 2, 3}));
    EXPECT_TRUE(h.Change(Edit::Clear, {}).Indices.empty());
}
TEST(PrimitiveSelection, GeometryMutationsInvalidateOnlyRelevantIdentity)
{
    Harness h;
    ASSERT_TRUE(h.Change(Edit::Replace, {2}).Usable());
    auto& vertices = h.Scene.Raw().get<GS::Vertices>(h.Entity).Properties;
    vertices.Get<glm::vec3>("v:position")[2] *= 2.f;
    (void)vertices.GetOrAdd<float>("v:custom", 4.f);
    EXPECT_EQ(h.Read().Indices, (std::vector<std::uint32_t>{2}));
    auto& halfedges = h.Scene.Raw().get<GS::Halfedges>(h.Entity).Properties;
    halfedges.Get<std::uint32_t>("h:to_vertex")[0] = 3;
    EXPECT_EQ(h.Read().Status, Status::StaleTopology);
    EXPECT_TRUE(h.Read().Indices.empty());
    EXPECT_EQ(h.Change(Edit::Add, {1}).Status, Status::StaleTopology);
    EXPECT_TRUE(h.Change(Edit::Replace, {1}).Usable());
    vertices.Get<bool>("v:deleted")[1] = true;
    EXPECT_EQ(h.Change(Edit::Replace, {1}).Status, Status::InvalidIndex);
    EXPECT_EQ(h.Read().Status, Status::StaleTopology);
    h.Selection.PrunePrimitives(h.Scene);
    EXPECT_EQ(h.Read().Status, Status::Empty);
    EXPECT_EQ(h.Change(Edit::All, {}).Indices, (std::vector<std::uint32_t>{0, 2, 3}));
}
TEST(PrimitiveSelection, DomainsRemainSeparateAndSceneResetDropsEverySet)
{
    Harness h;
    EXPECT_TRUE(h.Change(Edit::Replace, {1}).Usable());
    EXPECT_TRUE(h.Change(Edit::Replace, {0}, D::MeshEdge).Usable());
    EXPECT_TRUE(h.Change(Edit::Replace, {1}, D::MeshFace).Usable());
    EXPECT_TRUE(h.Change(Edit::Replace, {1}, D::MeshHalfedge).Usable());
    EXPECT_EQ(h.Change(Edit::Replace, {0}, D::GraphNode).Status, Status::UnsupportedDomain);
    EXPECT_EQ(h.Selection.PrimitiveSnapshots(h.Scene).size(), 4);
    h.Selection.ClearSceneState(h.Scene);
    EXPECT_TRUE(h.Selection.PrimitiveSnapshots(h.Scene).empty());
}
TEST(PrimitiveSelection, GraphNodesEdgesHalfedgesAndPointCloudRows)
{
    Harness h;
    Geometry::Graph::Graph graph;
    const auto a = graph.AddVertex({0, 0, 0}), b = graph.AddVertex({1, 0, 0});
    (void)graph.AddEdge(a, b);
    GS::PopulateFromGraph(h.Scene.Raw(), h.Entity, graph);
    EXPECT_TRUE(h.Change(Edit::Replace, {1}, D::GraphNode).Usable());
    EXPECT_TRUE(h.Change(Edit::Replace, {0}, D::GraphEdge).Usable());
    EXPECT_TRUE(h.Change(Edit::Replace, {1}, D::GraphHalfedge).Usable());
    EXPECT_EQ(h.Read(D::MeshFace).Status, Status::UnsupportedDomain);
    Geometry::PointCloud::Cloud cloud;
    (void)cloud.AddPoint({1, 2, 3});
    GS::PopulateFromCloud(h.Scene.Raw(), h.Entity, cloud);
    EXPECT_TRUE(h.Change(Edit::Replace, {0}, D::PointCloudPoint).Usable());
    EXPECT_EQ(h.Read(D::GraphNode).Status, Status::UnsupportedDomain);
}
TEST(PrimitiveSelection, PicksCaptureTargetAndModifiersAndDoNotToggleOwnerOff)
{
    Harness h;
    h.Selection.GetConfig().Interaction.Target = R::SelectionTarget::Vertex;
    h.Selection.RequestClickPick(1, 2, R::SelectionPickMode::Add);
    const auto first = h.Selection.ConsumePendingPick();
    ASSERT_TRUE(first);
    h.Selection.GetConfig().Interaction.Target = R::SelectionTarget::Face;
    ASSERT_TRUE(h.Selection.ConsumeHit(h.Scene, h.Id, first->Sequence,
                                       R::PrimitiveSelectionHit{D::MeshVertex, 2}));
    EXPECT_EQ(h.Read().Indices, (std::vector<std::uint32_t>{2}));
    EXPECT_TRUE(h.Selection.IsSelected(h.Entity));
    EXPECT_FALSE(h.Selection.ConsumeHit(h.Scene, h.Id, first->Sequence,
                                        R::PrimitiveSelectionHit{D::MeshVertex, 3}));
    h.Selection.GetConfig().Interaction.Target = R::SelectionTarget::Vertex;
    h.Selection.RequestClickPick(1, 2, R::SelectionPickMode::Toggle);
    auto next = h.Selection.ConsumePendingPick();
    ASSERT_TRUE(h.Selection.ConsumeHit(h.Scene, h.Id, next->Sequence,
                                       R::PrimitiveSelectionHit{D::MeshVertex, 2}));
    EXPECT_TRUE(h.Read().Indices.empty());
    EXPECT_TRUE(h.Selection.IsSelected(h.Entity));
    h.Selection.RequestHoverPick(1, 2);
    next = h.Selection.ConsumePendingPick();
    ASSERT_TRUE(h.Selection.ConsumeHit(h.Scene, h.Id, next->Sequence,
                                       R::PrimitiveSelectionHit{D::MeshVertex, 2}));
    EXPECT_TRUE(h.Read().Indices.empty());
    h.Scene.Raw().remove<Extrinsic::ECS::Components::Selection::SelectableTag>(h.Entity);
    h.Selection.RequestClickPick(1, 2);
    next = h.Selection.ConsumePendingPick();
    ASSERT_TRUE(h.Selection.ConsumeHit(h.Scene, h.Id, next->Sequence,
                                       R::PrimitiveSelectionHit{D::MeshVertex, 2}));
    EXPECT_TRUE(h.Read().Indices.empty());
}
TEST(PrimitiveSelection, BackgroundPreservesOwnerAndClearsPrimitiveSets)
{
    Harness h;
    ASSERT_TRUE(h.Selection.SetSelectedByStableEntityId(h.Scene, h.Id));
    ASSERT_TRUE(h.Change(Edit::Add, {0, 2}).Usable());
    h.Selection.GetConfig().Interaction.Target = R::SelectionTarget::Vertex;
    h.Selection.RequestClickPick(1, 1, R::SelectionPickMode::Add);
    ASSERT_TRUE(h.Selection.ConsumeNoHit(h.Scene, h.Selection.ConsumePendingPick()->Sequence));
    EXPECT_EQ(h.Read().Indices.size(), 2);
    h.Selection.RequestClickPick(1, 1);
    ASSERT_TRUE(h.Selection.ConsumeNoHit(h.Scene, h.Selection.ConsumePendingPick()->Sequence));
    EXPECT_TRUE(h.Read().Indices.empty());
    EXPECT_TRUE(h.Selection.IsSelected(h.Entity));
}
TEST(PrimitiveSelection, HighlightSnapshotAndMethodUseDoNotOverwriteProperties)
{
    Harness h;
    ASSERT_TRUE(h.Change(Edit::Replace, {2, 0}).Usable());
    ASSERT_TRUE(h.Change(Edit::Replace, {0}, D::MeshFace).Usable());
    const auto snapshot =
        R::BuildPrimitiveSelectionRenderSnapshot(h.Scene, h.Selection, R::DefaultWorldHandle);
    ASSERT_EQ(snapshot.DebugPoints.size(), 2);
    EXPECT_EQ(snapshot.DebugLines.size(), 3);
    EXPECT_EQ(snapshot.DebugPoints[0].Position, glm::vec3(1, 1, 0));
    R::EditorGeometryProcessingContext context{.Scene = &h.Scene, .Selection = &h.Selection};
    const auto commands = R::BindEditorGeometryProcessingCommands(context);
    const auto selected = R::ReadEditorPrimitiveSelection(commands, h.Id, D::MeshVertex);
    R::EditorGeodesicsCommand method{h.Id, {.SourceVertices = selected.Indices}};
    const auto result = R::ApplyEditorGeodesicsCommand(context, method);
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_DOUBLE_EQ(result.Diagnostics.Distances[0], 0);
    EXPECT_DOUBLE_EQ(result.Diagnostics.Distances[2], 0);
    EXPECT_EQ(h.Read().Indices, selected.Indices);
    h.Selection.GetConfig().Interaction.Highlight = false;
    EXPECT_TRUE(
        R::BuildPrimitiveSelectionRenderSnapshot(h.Scene, h.Selection, R::DefaultWorldHandle)
            .DebugPoints.empty());
}
TEST(PrimitiveSelection, ConfigValidationAndExpiredCommandsFailClosed)
{
    Extrinsic::Core::Config::EngineConfig config;
    R::SetSelectionInteractionConfig(config,
                                     {.Target = R::SelectionTarget::Edge, .PointRadius = 0.03f});
    const auto decoded = R::GetSelectionInteractionConfig(config);
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded->Target, R::SelectionTarget::Edge);
    EXPECT_FLOAT_EQ(decoded->PointRadius, 0.03f);
    for (const auto text : {R"({"target":"face"})", R"({"highlight":false})"})
        EXPECT_TRUE(R::ValidateSelectionConfigSection(text, {}, {}).Usable());
    for (const auto text : {R"({"target":"typo"})", R"({"point_radius":0})", R"({"highlight":4})",
                            R"({"unknown":1})"})
        EXPECT_FALSE(R::ValidateSelectionConfigSection(text, {}, {}).Usable());
    Harness h;
    bool active = true;
    const auto commands = R::BindEditorGeometryProcessingCommands(
        {.Scene = &h.Scene, .Selection = &h.Selection, .AttachmentActive = [&] { return active; }});
    active = false;
    const std::array values{0u};
    EXPECT_FALSE(
        R::ApplyEditorPrimitiveSelection(commands, h.Id, D::MeshVertex, Edit::Replace, values)
            .Usable());
    EXPECT_FALSE(R::ReadEditorPrimitiveSelection(commands, h.Id, D::MeshVertex).Usable());
    EXPECT_TRUE(h.Read().Indices.empty());
}

TEST(PrimitiveSelection, SettingsUseSharedPreviewApplyAndRemainReproducible)
{
    namespace Config = Extrinsic::Core::Config;
    Config::EngineConfigSectionRegistry registry;
    ASSERT_TRUE(registry.Register(R::MakeSelectionConfigSectionRegistration()));
    R::RuntimeEngineConfigControlState state;
    Config::PopulateEngineConfigSectionDefaults(state.ActiveConfig, registry);
    int previews = 0, applies = 0;
    R::EditorGeometryProcessingContext context;
    context.EngineConfigControlState = &state;
    context.EngineConfigCommandsAvailable = true;
    context.PreviewEngineConfigDocument = [&](const std::string& document, const std::string& source) {
        ++previews;
        return Config::PreviewEngineConfig(document, state.ActiveConfig, {source, &registry});
    };
    context.ApplyEngineConfigHotSubset = [&](const Config::EngineConfigLoadResult& preview) {
        ++applies;
        state.ActiveConfig = preview.Preview.Config;
        return R::RuntimeEngineConfigApplyResult{.Status = R::RuntimeEngineConfigApplyStatus::Applied};
    };
    const auto commands = R::BindEditorGeometryProcessingCommands(context);
    const R::SelectionInteractionConfig config{.Target = R::SelectionTarget::Face, .Highlight = false, .PointRadius = 0.1f};
    ASSERT_TRUE(R::ApplyEditorSelectionInteractionConfig(commands, config).Succeeded());
    EXPECT_EQ(previews, 1);
    EXPECT_EQ(applies, 1);
    EXPECT_EQ(R::GetEditorSelectionInteractionConfig(commands).Target, R::SelectionTarget::Face);
    const auto saved = Config::SerializeEngineConfig(state.ActiveConfig);
    const auto loaded = Config::PreviewEngineConfig(saved, {}, {"selection-roundtrip", &registry});
    ASSERT_TRUE(Config::IsConfigUsable(loaded));
    EXPECT_FALSE(R::GetSelectionInteractionConfig(loaded.Preview.Config)->Highlight);
    EXPECT_FALSE(R::ApplyEditorSelectionInteractionConfig(commands, {.PointRadius = -1.f}).Succeeded());
    EXPECT_EQ(applies, 1);
}
