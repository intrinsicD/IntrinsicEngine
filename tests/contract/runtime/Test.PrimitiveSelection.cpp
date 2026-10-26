#include <array>
#include <cstdint>
#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <gtest/gtest.h>
#include <algorithm>
#include <vector>
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Extrinsic.ECS.Components.Selection;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.VertexChannelBindings;
import Extrinsic.Runtime.SceneInteractionModule;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.GeometryProcessingOperations;
import Extrinsic.Runtime.MeshFieldOperations;
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
    auto& vertices = h.Scene.Raw().get<GS::Vertices>(h.Entity).Properties;
    ASSERT_TRUE(vertices.Exists("v:deleted"));
    vertices.Get<glm::vec3>("v:position")[0] += glm::vec3{1, 2, 3};
    EXPECT_EQ(h.Read(D::PointCloudPoint).Indices, (std::vector<std::uint32_t>{0}));
    vertices.Get<bool>("v:deleted")[0] = true;
    EXPECT_EQ(h.Read(D::PointCloudPoint).Status, Status::StaleTopology);
    EXPECT_EQ(h.Change(Edit::Replace, {0}, D::PointCloudPoint).Status, Status::InvalidIndex);
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
    R::EditorProcessingContext context{.Scene = &h.Scene, .Selection = &h.Selection};
    const auto processing = R::BindEditorProcessingCommands(context);
    const auto selected = R::ReadEditorPrimitiveSelection(processing, h.Id, D::MeshVertex);
    R::EditorGeodesicsCommand method{h.Id, {.SourceVertices = selected.Indices}};
    const auto result = R::ApplyEditorGeodesicsCommand(processing, method);
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_DOUBLE_EQ(result.Diagnostics.Distances[0], 0);
    EXPECT_DOUBLE_EQ(result.Diagnostics.Distances[2], 0);
    EXPECT_EQ(h.Read().Indices, selected.Indices);
    // GRAPHICS-156: an entity showing uncommitted GPU positions contributes no highlight
    // (the CPU positions the highlight would use are not what is on screen).
    const auto suppressed = R::BuildPrimitiveSelectionRenderSnapshot(
        h.Scene, h.Selection, R::DefaultWorldHandle, [&](const std::uint32_t id) { return id == h.Id; });
    EXPECT_TRUE(suppressed.DebugPoints.empty());
    EXPECT_TRUE(suppressed.DebugLines.empty());
    const auto other = R::BuildPrimitiveSelectionRenderSnapshot(
        h.Scene, h.Selection, R::DefaultWorldHandle, [&](const std::uint32_t id) { return id != h.Id; });
    EXPECT_EQ(other.DebugPoints.size(), 2);
    EXPECT_EQ(other.DebugLines.size(), 3);
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
    const auto commands = R::BindEditorProcessingCommands(
        [&] { R::EditorProcessingContext context{}; context.Scene=&h.Scene; context.Selection=&h.Selection; context.AttachmentActive=[&] { return active; }; return context; }());
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
    R::EditorProcessingContext context;
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
    const auto commands = R::BindEditorProcessingCommands(context);
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

// RUNTIME-315: highlights sit on the displayed (bound) positions, so the
// highlight overlays what is drawn rather than the canonical geometry.
TEST(PrimitiveSelection, HighlightFollowsBoundPositions)
{
    Harness h;
    auto& vertices = h.Scene.Raw().get<GS::Vertices>(h.Entity).Properties;
    auto lifted = vertices.Get<glm::vec3>("v:position").Vector();
    for (glm::vec3& p : lifted)
        p.z += 3.0f;
    vertices.GetOrAdd<glm::vec3>("v:lifted", glm::vec3{0.0f}).Vector() = lifted;
    h.Scene.Raw().emplace<R::VertexChannelBindingSet>(h.Entity,
        R::VertexChannelBindingSet{
            .Surface = {.Position = {.Enabled = true,
                            .Property = {D::MeshVertex, "v:lifted", Geometry::PropertyValueKind::Vec3}}}});
    ASSERT_TRUE(h.Change(Edit::Replace, {2}).Usable());
    auto snapshot = R::BuildPrimitiveSelectionRenderSnapshot(h.Scene, h.Selection, R::DefaultWorldHandle);
    ASSERT_EQ(snapshot.DebugPoints.size(), 1u);
    EXPECT_EQ(snapshot.DebugPoints[0].Position, glm::vec3(1, 1, 3));

    // UI-083: once the entity draws points, selected vertices are highlighted on
    // the points lane, which still shows the canonical positions.
    h.Scene.Raw().emplace<Extrinsic::Graphics::Components::RenderPoints>(h.Entity);
    snapshot = R::BuildPrimitiveSelectionRenderSnapshot(h.Scene, h.Selection, R::DefaultWorldHandle);
    ASSERT_EQ(snapshot.DebugPoints.size(), 1u);
    EXPECT_EQ(snapshot.DebugPoints[0].Position, glm::vec3(1, 1, 0));
}
// UI-083 slice 3c: a selection is highlighted on every drawn lane that shows its domain, once
// per distinct displayed position source: vertices on surface, edges and points; mesh edges on
// surface and edges; faces on the surface only.
namespace
{
    // Every `z` the selection's markers or lines sit at, in order.
    [[nodiscard]] std::vector<float> MarkerDepths(const R::RuntimeSceneInteractionRenderSnapshot& snapshot)
    {
        std::vector<float> depths;
        for (const auto& point : snapshot.DebugPoints)
            depths.push_back(point.Position.z);
        return depths;
    }

    [[nodiscard]] std::vector<float> LineDepths(const R::RuntimeSceneInteractionRenderSnapshot& snapshot)
    {
        std::vector<float> depths;
        for (const auto& line : snapshot.DebugLines)
        {
            EXPECT_EQ(line.Start.z, line.End.z);
            depths.push_back(line.Start.z);
        }
        return depths;
    }

    // `z`-lifted copy of the canonical positions under `name`.
    void AddLiftedPositions(Geometry::PropertySet& vertices, const char* name, const float lift)
    {
        auto lifted = vertices.Get<glm::vec3>("v:position").Vector();
        for (glm::vec3& p : lifted)
            p.z += lift;
        vertices.GetOrAdd<glm::vec3>(name, glm::vec3{0.0f}).Vector() = lifted;
    }

    [[nodiscard]] R::VertexChannelSourceBinding BoundPosition(const D domain, const char* name)
    {
        return {.Enabled = true, .Property = {domain, name, Geometry::PropertyValueKind::Vec3}};
    }
}

TEST(PrimitiveSelection, HighlightShowsTheSelectionOnEveryDrawnLane)
{
    namespace G = Extrinsic::Graphics::Components;
    Harness h;
    auto& raw = h.Scene.Raw();
    raw.emplace<G::RenderSurface>(h.Entity);
    raw.emplace<G::RenderEdges>(h.Entity);
    raw.emplace<G::RenderPoints>(h.Entity);
    const auto snapshot = [&]
    {
        return R::BuildPrimitiveSelectionRenderSnapshot(h.Scene, h.Selection, R::DefaultWorldHandle);
    };

    // Every lane shows the canonical positions: one marker.
    ASSERT_TRUE(h.Change(Edit::Replace, {2}).Usable());
    EXPECT_EQ(MarkerDepths(snapshot()), (std::vector<float>{0.0f}));

    // Three distinct sources: the surface at z+3, the edges at z+2, the points canonical.
    auto& vertices = raw.get<GS::Vertices>(h.Entity).Properties;
    AddLiftedPositions(vertices, "v:lift3", 3.0f);
    AddLiftedPositions(vertices, "v:lift2", 2.0f);
    auto& bindings = raw.emplace<R::VertexChannelBindingSet>(h.Entity,
        R::VertexChannelBindingSet{.Surface = {.Position = BoundPosition(D::MeshVertex, "v:lift3")},
            .Edges = {.Position = BoundPosition(D::MeshVertex, "v:lift2")}});
    EXPECT_EQ(MarkerDepths(snapshot()), (std::vector<float>{3.0f, 2.0f, 0.0f})) << "vertex: surface, edges, points";
    EXPECT_EQ(snapshot().DebugPoints.front().Position, glm::vec3(1, 1, 3));

    // A mesh halfedge on the surface and the edges lane, at its edge's (index / 2) endpoints.
    ASSERT_TRUE(h.Change(Edit::Replace, {3}, D::MeshHalfedge).Usable());
    {
        const auto& edges = raw.get<GS::Edges>(h.Entity).Properties;
        const auto& canonical = vertices.Get<glm::vec3>("v:position").Vector();
        const glm::vec3 from = canonical[edges.Get<std::uint32_t>("e:v0")[1]];
        const glm::vec3 to = canonical[edges.Get<std::uint32_t>("e:v1")[1]];
        const auto halfedgeSnapshot = snapshot();
        ASSERT_EQ(LineDepths(halfedgeSnapshot), (std::vector<float>{3.0f, 2.0f})) << "halfedge: surface, edges";
        for (const auto& line : halfedgeSnapshot.DebugLines)
        {
            const glm::vec3 lift{0.0f, 0.0f, line.Start.z};
            EXPECT_EQ(line.Start, from + lift);
            EXPECT_EQ(line.End, to + lift);
        }
    }
    ASSERT_TRUE(h.Change(Edit::Clear, {}, D::MeshHalfedge).Usable());

    // A mesh edge on the surface and the edges lane, not the points lane.
    ASSERT_TRUE(h.Change(Edit::Replace, {0}, D::MeshEdge).Usable());
    EXPECT_EQ(LineDepths(snapshot()), (std::vector<float>{3.0f, 2.0f})) << "edge: surface, edges";

    // A face on the surface only (the edge's two lines come first).
    ASSERT_TRUE(h.Change(Edit::Replace, {0}, D::MeshFace).Usable());
    EXPECT_EQ(LineDepths(snapshot()), (std::vector<float>{3.0f, 2.0f, 3.0f, 3.0f, 3.0f})) << "face: surface";

    // Edges rebound to the surface's property: their markers merge with the surface's.
    bindings.Edges.Position = BoundPosition(D::MeshVertex, "v:lift3");
    EXPECT_EQ(MarkerDepths(snapshot()), (std::vector<float>{3.0f, 0.0f})) << "same source, one marker";

    // Fallback: a face on a mesh drawn only as points keeps the surface.
    raw.remove<G::RenderSurface>(h.Entity);
    raw.remove<G::RenderEdges>(h.Entity);
    ASSERT_TRUE(h.Change(Edit::Clear, {}, D::MeshEdge).Usable());
    EXPECT_EQ(LineDepths(snapshot()), (std::vector<float>{3.0f, 3.0f, 3.0f})) << "face fallback: surface";
}

// UI-083 slice 3c: a graph shows nodes on its edges and points lanes, edges and halfedges on its
// edges lane only.
TEST(PrimitiveSelection, GraphHighlightsNodesOnBothLanesAndEdgesOnTheEdgesLane)
{
    namespace G = Extrinsic::Graphics::Components;
    Harness h;
    Geometry::Graph::Graph graph;
    const auto a = graph.AddVertex({0, 0, 0}), b = graph.AddVertex({1, 0, 0}), c = graph.AddVertex({1, 1, 0});
    (void)graph.AddEdge(a, b);
    (void)graph.AddEdge(b, c);
    auto& raw = h.Scene.Raw();
    GS::PopulateFromGraph(raw, h.Entity, graph);
    raw.emplace<G::RenderEdges>(h.Entity);
    raw.emplace<G::RenderPoints>(h.Entity);
    AddLiftedPositions(raw.get<GS::Vertices>(h.Entity).Properties, "v:lift2", 2.0f);
    raw.emplace<R::VertexChannelBindingSet>(
        h.Entity, R::VertexChannelBindingSet{.Edges = {.Position = BoundPosition(D::GraphNode, "v:lift2")}});
    const auto snapshot = [&]
    {
        return R::BuildPrimitiveSelectionRenderSnapshot(h.Scene, h.Selection, R::DefaultWorldHandle);
    };

    ASSERT_TRUE(h.Change(Edit::Replace, {2}, D::GraphNode).Usable());
    EXPECT_EQ(MarkerDepths(snapshot()), (std::vector<float>{2.0f, 0.0f})) << "node: edges, points";

    ASSERT_TRUE(h.Change(Edit::Replace, {0}, D::GraphEdge).Usable());
    EXPECT_EQ(LineDepths(snapshot()), (std::vector<float>{2.0f})) << "edge: edges lane only";

    // Halfedge 3 belongs to edge 1 (b, c).
    ASSERT_TRUE(h.Change(Edit::Clear, {}, D::GraphEdge).Usable());
    ASSERT_TRUE(h.Change(Edit::Replace, {3}, D::GraphHalfedge).Usable());
    const auto halfedge = snapshot();
    EXPECT_EQ(LineDepths(halfedge), (std::vector<float>{2.0f})) << "halfedge: edges lane only";
    ASSERT_EQ(halfedge.DebugLines.size(), 1u);
    EXPECT_EQ(halfedge.DebugLines[0].Start, glm::vec3(1, 0, 2));
    EXPECT_EQ(halfedge.DebugLines[0].End, glm::vec3(1, 1, 2));
}

// RUNTIME-315: a pick read back after the Position binding changed was rendered
// from other positions, so its stamp differs and the pick is discarded, even
// when the newly bound source carries the same property revision.
TEST(PrimitiveSelection, PickStampChangesWithThePositionBindingGeneration)
{
    Harness h;
    auto& vertices = h.Scene.Raw().get<GS::Vertices>(h.Entity).Properties;
    const auto canonical = vertices.Get<glm::vec3>("v:position").Vector();
    vertices.GetOrAdd<glm::vec3>("v:a", glm::vec3{0.0f}).Vector() = canonical;
    const auto unbound = R::BuildPrimitivePickStamp(h.Scene, h.Id, false);
    ASSERT_FALSE(unbound.empty());
    EXPECT_EQ(R::BuildPrimitivePickStamp(h.Scene, h.Id, false), unbound);

    auto& bindings = h.Scene.Raw().emplace<R::VertexChannelBindingSet>(h.Entity,
        R::VertexChannelBindingSet{
            .Surface = {.Position = {.Enabled = true,
                            .Property = {D::MeshVertex, "v:a", Geometry::PropertyValueKind::Vec3}}},
            .BindingGeneration = 1u});
    const auto bound = R::BuildPrimitivePickStamp(h.Scene, h.Id, false);
    EXPECT_NE(bound, unbound);

    bindings.BindingGeneration = 2u;  // rebound; the displayed source's revision is unchanged
    const auto rebound = R::BuildPrimitivePickStamp(h.Scene, h.Id, false);
    EXPECT_NE(rebound, bound);

    // UI-083: an edit of the positions only another lane displays also changes the stamp.
    vertices.GetOrAdd<glm::vec3>("v:b", glm::vec3{0.0f}).Vector() = canonical;
    bindings.Points.Position = {.Enabled = true, .Property = {D::MeshVertex, "v:b", Geometry::PropertyValueKind::Vec3}};
    const auto withPoints = R::BuildPrimitivePickStamp(h.Scene, h.Id, false);
    EXPECT_NE(withPoints, rebound);
    vertices.Get<glm::vec3>("v:b").Vector()[0].z = 1.0f;
    EXPECT_NE(R::BuildPrimitivePickStamp(h.Scene, h.Id, false), withPoints);
    EXPECT_NE(R::BuildPrimitivePickStamp(h.Scene, h.Id, true), R::BuildPrimitivePickStamp(h.Scene, h.Id, false));
    EXPECT_TRUE(R::BuildPrimitivePickStamp(h.Scene, h.Id + 1000u, false).empty());
}
