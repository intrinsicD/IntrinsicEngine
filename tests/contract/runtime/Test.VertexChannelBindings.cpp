// RUNTIME-315 render-attribute binding table, source resolution and model.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include "EditorFeatureTestContext.hpp"

import Extrinsic.ECS.Component.Culling.World;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.Colormap;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.Runtime.CameraFocusCommand;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.VertexChannelBindings;
import Extrinsic.Runtime.GizmoInteraction;
import Extrinsic.Runtime.VisualizationEditingOperations;
import Extrinsic.Runtime.VisualizationRecipes;
import Geometry.Properties;

namespace
{
    namespace Runtime = Extrinsic::Runtime;
    namespace ECS = Extrinsic::ECS;
    namespace GS = Extrinsic::ECS::Components::GeometrySources;
    namespace G = Extrinsic::Graphics::Components;
    using A = Runtime::RenderAttribute;
    using D = Runtime::GeometryElementDomain;
    using Status = Runtime::GeometryPropertyResolutionStatus;
    using Kind = Geometry::PropertyValueKind;
    using Intrinsic::Tests::EditorGeometry::AddGraphSource;
    using Intrinsic::Tests::EditorGeometry::AddPointCloudSource;
    using Intrinsic::Tests::EditorGeometry::AddTriangleMeshSource;
    using Intrinsic::Tests::EditorGeometry::MakeSelectable;
    using Intrinsic::Tests::EditorGeometry::SetPositions;

    template <class T>
    void SetProperty(Geometry::PropertySet& properties, const std::string& name, std::vector<T> values)
    {
        properties.GetOrAdd<T>(name, T{}).Vector() = std::move(values);
    }

    using L = Runtime::GeometryRenderLane;

    // The lane a source on `domain` drew before rows were keyed by lane: the
    // lane its Color overlay targets by default, the size/width lane, and for
    // structural streams the entity's first lane with that attribute.
    [[nodiscard]] L LaneFor(const A attribute, const D domain)
    {
        if (attribute == A::PointSize)
            return L::Points;
        if (attribute == A::LineWidth)
            return L::Edges;
        switch (domain)
        {
        case D::MeshVertex:
        case D::MeshHalfedge:
        case D::MeshFace:     return L::Surface;
        case D::MeshEdge:
        case D::GraphEdge:    return L::Edges;
        case D::GraphNode:    return attribute == A::Normal ? L::Points : L::Edges;
        default:              return L::Points;
        }
    }

    [[nodiscard]] const Runtime::EditorAttributeBindingRow* FindRow(
        const Runtime::EditorAttributeBindingModel& model, const L lane, const A attribute)
    {
        const auto found = std::ranges::find_if(model.Rows,
            [&](const auto& row)
            {
                return row.Lane == lane && row.Attribute == attribute;
            });
        return found == model.Rows.end() ? nullptr : &*found;
    }

    // The row that reads `domain` on the lane LaneFor() picks.
    [[nodiscard]] const Runtime::EditorAttributeBindingRow* FindRow(
        const Runtime::EditorAttributeBindingModel& model, const A attribute, const D domain)
    {
        const auto* row = FindRow(model, LaneFor(attribute, domain), attribute);
        return row != nullptr &&
                std::ranges::any_of(row->Domains,
                    [&](const auto& d)
                    {
                        return d.Domain == domain;
                    })
            ? row
            : nullptr;
    }

    // (lane, attribute) -> the element domains the row reads, for whole-table comparisons.
    [[nodiscard]] std::map<std::pair<L, A>, std::vector<D>> RowDomains(
        const Runtime::EditorAttributeBindingModel& model)
    {
        std::map<std::pair<L, A>, std::vector<D>> rows;
        for (const auto& row : model.Rows)
            for (const auto& domain : row.Domains)
                rows[{row.Lane, row.Attribute}].push_back(domain.Domain);
        return rows;
    }

    [[nodiscard]] const Runtime::EditorAttributeBindingCandidate* FindCandidate(
        const Runtime::EditorAttributeBindingRow& row, const std::string& name)
    {
        const auto found = std::ranges::find_if(row.Candidates, [&](const auto& candidate) {
            return candidate.Property.Name == name;
        });
        return found == row.Candidates.end() ? nullptr : &*found;
    }
}

TEST(VertexChannelBindings, TableListsEveryAttributeDomainRowWithItsTypeRule)
{
    // (attribute, domain, accepted type text, requires finite, canonical property)
    const std::vector<std::tuple<A, D, std::string, bool, std::string>> expected{
        {A::Position, D::MeshVertex, "vec3", true, "v:position"},
        {A::Position, D::GraphNode, "vec3", true, "v:position"},
        {A::Position, D::PointCloudPoint, "vec3", true, "v:position"},
        {A::Normal, D::MeshVertex, "vec3", false, "v:normal"},
        {A::Normal, D::MeshHalfedge, "vec3", false, "h:normal"},
        {A::Normal, D::GraphNode, "vec3", false, ""},
        {A::Normal, D::PointCloudPoint, "vec3", false, "p:normal"},
        {A::Texcoord, D::MeshVertex, "vec2", false, "v:texcoord"},
        {A::Texcoord, D::MeshHalfedge, "vec2", false, "h:texcoord"},
        {A::Color, D::MeshVertex, "scalar or vector", false, ""},
        {A::Color, D::MeshEdge, "scalar or vector", false, ""},
        {A::Color, D::MeshFace, "scalar or vector", false, ""},
        {A::Color, D::GraphNode, "scalar or vector", false, ""},
        {A::Color, D::GraphEdge, "scalar or vector", false, ""},
        {A::Color, D::PointCloudPoint, "scalar or vector", false, ""},
        {A::PointSize, D::MeshVertex, "float", true, ""},
        {A::PointSize, D::GraphNode, "float", true, ""},
        {A::PointSize, D::PointCloudPoint, "float", true, ""},
        {A::LineWidth, D::MeshEdge, "float", true, ""},
        {A::LineWidth, D::GraphEdge, "float", true, ""},
    };
    ASSERT_EQ(Runtime::RenderAttributeRules().size(), expected.size());
    for (const auto& [attribute, domain, type, finite, canonical] : expected)
    {
        SCOPED_TRACE(std::string{Runtime::ToString(attribute)} + " on " +
                     std::string{Runtime::ToString(domain)});
        const Runtime::RenderAttributeRule* rule = Runtime::FindRenderAttributeRule(attribute, domain);
        ASSERT_NE(rule, nullptr);
        EXPECT_EQ(Runtime::RenderAttributeExpectedTypeText(*rule), type);
        EXPECT_EQ(rule->RequireFiniteValues, finite);
        EXPECT_EQ(rule->CanonicalProperty, canonical);
        EXPECT_FALSE(rule->DefaultDescription.empty());
        Runtime::RenderAttribute parsed{};
        ASSERT_TRUE(Runtime::TryParseRenderAttribute(Runtime::ToString(attribute), parsed));
        EXPECT_EQ(parsed, attribute);
    }
    EXPECT_EQ(Runtime::FindRenderAttributeRule(A::Position, D::MeshFace), nullptr);
    EXPECT_EQ(Runtime::FindRenderAttributeRule(A::LineWidth, D::PointCloudPoint), nullptr);
    EXPECT_EQ(Runtime::FindRenderAttributeRule(A::Texcoord, D::GraphNode), nullptr);
}

TEST(VertexChannelBindings, SourceResolutionRefusesWrongDomainKindAndNonFiniteValues)
{
    ECS::Scene::Registry registry;
    const ECS::EntityHandle mesh = MakeSelectable(registry, "Mesh");
    AddTriangleMeshSource(registry, mesh);
    auto& vertices = registry.Raw().get<GS::Vertices>(mesh).Properties;
    SetProperty<glm::vec3>(vertices, "v:offset", {{0, 0, 1}, {1, 0, 1}, {0, 1, 1}});
    SetProperty<glm::vec3>(vertices, "v:broken",
                           {{0, 0, 1}, {std::numeric_limits<float>::quiet_NaN(), 0, 0}, {0, 1, 1}});
    SetProperty<float>(vertices, "v:size", {1.0f, 2.0f, 3.0f});
    SetProperty<float>(vertices, "v:bad_size", {1.0f, std::numeric_limits<float>::infinity(), 3.0f});
    SetProperty<glm::vec2>(registry.Raw().get<GS::Halfedges>(mesh).Properties, "h:uv2",
                           std::vector<glm::vec2>(6u, glm::vec2{0.5f}));

    const Runtime::GeometryEntityAvailability availability =
        Runtime::BuildGeometryAvailability(registry.Raw(), mesh);
    const auto resolve = [&](const A attribute, const D domain, const char* name) {
        return Runtime::ResolveRenderAttributeSource(availability, attribute, domain, name).Status;
    };

    EXPECT_EQ(resolve(A::Position, D::MeshVertex, "v:offset"), Status::Resolved);
    EXPECT_EQ(resolve(A::Position, D::MeshVertex, "v:size"), Status::ValueKindMismatch);
    EXPECT_EQ(resolve(A::Position, D::MeshVertex, "v:missing"), Status::MissingProperty);
    EXPECT_EQ(resolve(A::Position, D::MeshVertex, ""), Status::MissingName);
    EXPECT_EQ(resolve(A::Position, D::MeshVertex, "v:broken"), Status::NonFiniteValues);
    // Shading normals repair non-finite elements, so the same source is usable.
    EXPECT_EQ(resolve(A::Normal, D::MeshVertex, "v:broken"), Status::Resolved);
    EXPECT_EQ(resolve(A::Position, D::MeshFace, "v:offset"), Status::UnsupportedDomain);
    EXPECT_EQ(resolve(A::Position, D::GraphNode, "v:offset"), Status::UnsupportedDomain);
    EXPECT_EQ(resolve(A::Texcoord, D::MeshHalfedge, "h:uv2"), Status::Resolved);
    EXPECT_EQ(resolve(A::Texcoord, D::MeshVertex, "h:uv2"), Status::MissingProperty);
    EXPECT_EQ(resolve(A::Color, D::MeshVertex, "v:size"), Status::Resolved);
    EXPECT_EQ(resolve(A::PointSize, D::MeshVertex, "v:size"), Status::Resolved);
    EXPECT_EQ(resolve(A::PointSize, D::MeshVertex, "v:bad_size"), Status::NonFiniteValues);
    EXPECT_EQ(resolve(A::PointSize, D::MeshVertex, "v:offset"), Status::ValueKindMismatch);
    EXPECT_EQ(resolve(A::LineWidth, D::MeshEdge, "e:v0"), Status::ValueKindMismatch);
}

TEST(VertexChannelBindings, ModelListsRowsCandidatesAndCurrentSourcesForEveryEntityKind)
{
    ECS::Scene::Registry registry;
    const ECS::EntityHandle mesh = MakeSelectable(registry, "Mesh");
    AddTriangleMeshSource(registry, mesh);
    auto& meshVertices = registry.Raw().get<GS::Vertices>(mesh).Properties;
    SetProperty<glm::vec3>(meshVertices, "v:offset", {{0, 0, 1}, {1, 0, 1}, {0, 1, 1}});
    SetProperty<float>(meshVertices, "v:temperature", {0.0f, 0.5f, 1.0f});

    const auto meshId = Runtime::SelectionController::ToStableEntityId(mesh);
    Runtime::EditorAttributeBindingModel model =
        Runtime::BuildEditorAttributeBindingModel(registry, meshId);
    ASSERT_TRUE(model.HasEntity);
    // Faces = surface, Edges = edges, Vertices = points: each section lists
    // the attributes of its own primitive, read from the lane's domains.
    using Rows = std::map<std::pair<L, A>, std::vector<D>>;
    EXPECT_EQ(RowDomains(model),
        (Rows{{{L::Surface, A::Position}, {D::MeshVertex}},
            {{L::Surface, A::Normal}, {D::MeshVertex, D::MeshHalfedge}},
            {{L::Surface, A::Texcoord}, {D::MeshVertex, D::MeshHalfedge}},
            {{L::Surface, A::Color}, {D::MeshVertex, D::MeshFace}},
            {{L::Edges, A::Position}, {D::MeshVertex}},
            {{L::Edges, A::Color}, {D::MeshVertex, D::MeshEdge}},
            {{L::Edges, A::LineWidth}, {D::MeshEdge}},
            {{L::Points, A::Position}, {D::MeshVertex}},
            {{L::Points, A::Normal}, {D::MeshVertex}},
            {{L::Points, A::Color}, {D::MeshVertex}},
            {{L::Points, A::PointSize}, {D::MeshVertex}}}));

    // A Color row names its lane's overlay; other attributes have none.
    using Target = Runtime::EditorVisualizationTarget;
    EXPECT_EQ(FindRow(model, L::Surface, A::Color)->OverlayTarget, Target::Surface);
    EXPECT_EQ(FindRow(model, L::Edges, A::Color)->OverlayTarget, Target::Edges);
    EXPECT_EQ(FindRow(model, L::Points, A::Color)->OverlayTarget, Target::Points);
    EXPECT_FALSE(FindRow(model, A::Position, D::MeshVertex)->OverlayTarget.has_value());

    const auto* position = FindRow(model, A::Position, D::MeshVertex);
    ASSERT_NE(position, nullptr);
    EXPECT_FALSE(position->Bound);
    EXPECT_EQ(position->ExpectedType, "vec3");
    ASSERT_EQ(position->Domains.size(), 1u);
    EXPECT_EQ(position->Domains.front().ElementCount, 3u);
    EXPECT_EQ(position->DefaultSource, "v:position");
    const auto* offset = FindCandidate(*position, "v:offset");
    ASSERT_NE(offset, nullptr);
    EXPECT_TRUE(offset->Compatible);
    EXPECT_EQ(offset->Property.ValueKind, Kind::Vec3);
    EXPECT_EQ(offset->ElementCount, 3u);
    const auto* temperature = FindCandidate(*position, "v:temperature");
    ASSERT_NE(temperature, nullptr);
    EXPECT_FALSE(temperature->Compatible);
    EXPECT_EQ(temperature->Reason, Status::ValueKindMismatch);
    EXPECT_EQ(temperature->DisabledReason, "requires vec3");
    // Connectivity/deletion rows are never offered.
    EXPECT_EQ(FindCandidate(*FindRow(model, A::LineWidth, D::MeshEdge), "e:v0"), nullptr);

    // Current sources come from the storage that owns each attribute.
    registry.Raw().emplace<Runtime::VertexChannelBindingSet>(mesh,
        Runtime::VertexChannelBindingSet{
            .Surface = {.Position = {.Enabled = true,
                            .Property = {.Domain = D::MeshVertex, .Name = "v:offset", .ValueKind = Kind::Vec3}},
                .Normal = {.Enabled = true,
                    .Property = {.Domain = D::MeshVertex, .Name = "v:gone", .ValueKind = Kind::Vec3}}}});
    G::VisualizationConfig color{};
    color.Source = G::VisualizationConfig::ColorSource::ScalarField;
    color.ScalarFieldName = "v:temperature";
    color.ScalarDomain = G::VisualizationConfig::Domain::Vertex;
    registry.Raw().emplace<G::VisualizationConfig>(mesh, color);
    registry.Raw().emplace<G::RenderPoints>(mesh, G::RenderPoints{.SizeProperty = "v:temperature"});

    model = Runtime::BuildEditorAttributeBindingModel(registry, meshId);
    position = FindRow(model, A::Position, D::MeshVertex);
    ASSERT_TRUE(position->Bound);
    EXPECT_EQ(position->Source.Name, "v:offset");
    EXPECT_FALSE(position->UsingFallback);
    const auto* normal = FindRow(model, A::Normal, D::MeshVertex);
    ASSERT_TRUE(normal->Bound);
    EXPECT_TRUE(normal->UsingFallback);
    EXPECT_EQ(normal->Resolution.Status, Status::MissingProperty);
    EXPECT_NE(normal->Diagnostic.find("drawing the default"), std::string::npos);
    EXPECT_EQ(normal->Source.Domain, D::MeshVertex);
    const auto* vertexColor = FindRow(model, A::Color, D::MeshVertex);
    ASSERT_TRUE(vertexColor->Bound);
    EXPECT_EQ(vertexColor->Source.Name, "v:temperature");
    EXPECT_EQ(vertexColor->Source.Domain, D::MeshVertex);
    EXPECT_EQ(vertexColor->Source.ValueKind, Kind::Float);
    const auto* size = FindRow(model, A::PointSize, D::MeshVertex);
    ASSERT_TRUE(size->Bound);
    EXPECT_EQ(size->Source.Name, "v:temperature");
    EXPECT_FALSE(size->UsingFallback);

    EXPECT_FALSE(FindRow(model, A::LineWidth, D::MeshEdge)->Bound);

    // Graphs and point clouds read the same table.
    const ECS::EntityHandle graph = MakeSelectable(registry, "Graph");
    AddGraphSource(registry, graph);
    model = Runtime::BuildEditorAttributeBindingModel(
        registry, Runtime::SelectionController::ToStableEntityId(graph));
    EXPECT_EQ(RowDomains(model),
        (Rows{{{L::Edges, A::Position}, {D::GraphNode}},
            {{L::Edges, A::Color}, {D::GraphNode, D::GraphEdge}},
            {{L::Edges, A::LineWidth}, {D::GraphEdge}},
            {{L::Points, A::Position}, {D::GraphNode}},
            {{L::Points, A::Normal}, {D::GraphNode}},
            {{L::Points, A::Color}, {D::GraphNode}},
            {{L::Points, A::PointSize}, {D::GraphNode}}}));
    EXPECT_EQ(FindRow(model, A::LineWidth, D::GraphEdge)->Domains.front().ElementCount, 2u);

    const ECS::EntityHandle cloud = MakeSelectable(registry, "Cloud");
    AddPointCloudSource(registry, cloud, 2u);
    SetPositions(registry.Raw().get<GS::Vertices>(cloud), {{0, 0, 0}, {1, 0, 0}});
    model = Runtime::BuildEditorAttributeBindingModel(
        registry, Runtime::SelectionController::ToStableEntityId(cloud));
    EXPECT_EQ(RowDomains(model),
        (Rows{{{L::Points, A::Position}, {D::PointCloudPoint}},
            {{L::Points, A::Normal}, {D::PointCloudPoint}},
            {{L::Points, A::Color}, {D::PointCloudPoint}},
            {{L::Points, A::PointSize}, {D::PointCloudPoint}}}));

    EXPECT_FALSE(Runtime::BuildEditorAttributeBindingModel(registry, 0u).HasEntity);
}

namespace
{
    using Cmd = Runtime::EditorCommandStatus;
    namespace Dirty = Extrinsic::ECS::Components::DirtyTags;

    struct BindingFixture
    {
        ECS::Scene::Registry Registry;
        Runtime::SelectionController Selection;
        Runtime::EditorCommandHistory History;
        Intrinsic::Tests::EditorFeatureTestContext Context =
            Intrinsic::Tests::MakeContext(Registry, Selection);

        BindingFixture()
        {
            Context.CommandHistory = &History;
            Context.VisualizationCommandsAvailable = true;
        }

        Cmd Bind(const ECS::EntityHandle entity, const A attribute, const D domain,
                 std::string name = {})
        {
            return BindOn(entity, LaneFor(attribute, domain), attribute, domain, std::move(name));
        }

        Cmd BindOn(
            const ECS::EntityHandle entity, const L lane, const A attribute, const D domain, std::string name = {})
        {
            return Runtime::ApplyEditorAttributeBindingCommand(Context,
                Runtime::EditorAttributeBindingCommand{
                    .StableEntityId = Runtime::SelectionController::ToStableEntityId(entity),
                    .Lane = lane,
                    .Attribute = attribute,
                    .Domain = domain,
                    .PropertyName = std::move(name)});
        }

        [[nodiscard]] const Runtime::EditorAttributeBindingRow& Row(
            const ECS::EntityHandle entity, const A attribute, const D domain)
        {
            Model = Runtime::BuildEditorAttributeBindingModel(
                Registry, Runtime::SelectionController::ToStableEntityId(entity));
            const auto* row = FindRow(Model, attribute, domain);
            EXPECT_NE(row, nullptr);
            static const Runtime::EditorAttributeBindingRow kMissing{};
            return row != nullptr ? *row : kMissing;
        }

        [[nodiscard]] const Runtime::EditorAttributeBindingRow& RowOn(
            const ECS::EntityHandle entity, const L lane, const A attribute)
        {
            Model = Runtime::BuildEditorAttributeBindingModel(
                Registry, Runtime::SelectionController::ToStableEntityId(entity));
            const auto* row = FindRow(Model, lane, attribute);
            EXPECT_NE(row, nullptr);
            static const Runtime::EditorAttributeBindingRow kMissing{};
            return row != nullptr ? *row : kMissing;
        }

        Runtime::EditorAttributeBindingModel Model{};
    };
}

TEST(VertexChannelBindings, StructuralBindingAndDefaultAreOneUndoableStepOnEveryEntityKind)
{
    BindingFixture f;
    const ECS::EntityHandle mesh = MakeSelectable(f.Registry, "Mesh");
    AddTriangleMeshSource(f.Registry, mesh);
    const ECS::EntityHandle graph = MakeSelectable(f.Registry, "Graph");
    AddGraphSource(f.Registry, graph);
    const ECS::EntityHandle cloud = MakeSelectable(f.Registry, "Cloud");
    AddPointCloudSource(f.Registry, cloud, 3u);
    SetPositions(f.Registry.Raw().get<GS::Vertices>(cloud), {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}});

    for (const auto& [entity, domain] : {std::pair{mesh, D::MeshVertex}, std::pair{graph, D::GraphNode},
                                         std::pair{cloud, D::PointCloudPoint}})
    {
        SCOPED_TRACE(std::string{Runtime::ToString(domain)});
        auto& properties = f.Registry.Raw().get<GS::Vertices>(entity).Properties;
        SetProperty<glm::vec3>(properties, "v:n2", std::vector<glm::vec3>(3u, glm::vec3{1, 0, 0}));
        f.Registry.Raw().remove<Dirty::DirtyVertexNormals>(entity);
        const std::size_t undoBefore = f.History.UndoCount();

        ASSERT_EQ(f.Bind(entity, A::Normal, domain, "v:n2"), Cmd::Applied);
        EXPECT_EQ(f.History.UndoCount(), undoBefore + 1u);
        const auto& bindings = f.Registry.Raw().get<Runtime::VertexChannelBindingSet>(entity);
        EXPECT_EQ(Runtime::FindVertexChannelSourceBinding(bindings, LaneFor(A::Normal, domain), A::Normal)->Property,
            (Runtime::GeometryPropertyRef{domain, "v:n2", Kind::Vec3}));
        EXPECT_TRUE(f.Registry.Raw().all_of<Dirty::DirtyVertexNormals>(entity));
        EXPECT_EQ(f.Bind(entity, A::Normal, domain, "v:n2"), Cmd::NoChange);
        EXPECT_EQ(f.Row(entity, A::Normal, domain).Source.Name, "v:n2");

        ASSERT_EQ(f.Bind(entity, A::Normal, domain), Cmd::Applied);  // Default
        EXPECT_FALSE(f.Registry.Raw().all_of<Runtime::VertexChannelBindingSet>(entity));
        EXPECT_EQ(f.Bind(entity, A::Normal, domain), Cmd::NoChange);

        ASSERT_EQ(f.History.Undo().Status, Runtime::EditorCommandHistoryStatus::Undone);
        EXPECT_EQ(f.Row(entity, A::Normal, domain).Source.Name, "v:n2");
        ASSERT_EQ(f.History.Undo().Status, Runtime::EditorCommandHistoryStatus::Undone);
        EXPECT_FALSE(f.Registry.Raw().all_of<Runtime::VertexChannelBindingSet>(entity));
        ASSERT_EQ(f.History.Redo().Status, Runtime::EditorCommandHistoryStatus::Redone);
        EXPECT_TRUE(f.Row(entity, A::Normal, domain).Bound);

        // A bound property that disappears is reported, never silently dropped.
        auto n2 = properties.Get<glm::vec3>("v:n2");
        properties.Remove(n2);
        const auto& stale = f.Row(entity, A::Normal, domain);
        EXPECT_TRUE(stale.Bound);
        EXPECT_TRUE(stale.UsingFallback);
        EXPECT_EQ(stale.Resolution.Status, Status::MissingProperty);
        EXPECT_FALSE(stale.Diagnostic.empty());
    }
}

TEST(VertexChannelBindings, PositionBindingIsOneUndoableStepOnEveryEntityKindAndNeverCopies)
{
    BindingFixture f;
    const ECS::EntityHandle mesh = MakeSelectable(f.Registry, "Mesh");
    AddTriangleMeshSource(f.Registry, mesh);
    const ECS::EntityHandle graph = MakeSelectable(f.Registry, "Graph");
    AddGraphSource(f.Registry, graph);
    const ECS::EntityHandle cloud = MakeSelectable(f.Registry, "Cloud");
    AddPointCloudSource(f.Registry, cloud, 3u);
    SetPositions(f.Registry.Raw().get<GS::Vertices>(cloud), {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}});

    for (const auto& [entity, domain] : {std::pair{mesh, D::MeshVertex}, std::pair{graph, D::GraphNode},
                                         std::pair{cloud, D::PointCloudPoint}})
    {
        SCOPED_TRACE(std::string{Runtime::ToString(domain)});
        auto& properties = f.Registry.Raw().get<GS::Vertices>(entity).Properties;
        SetProperty<glm::vec3>(properties, "v:offset", std::vector<glm::vec3>(3u, glm::vec3{0, 0, 2}));
        const std::vector<glm::vec3> canonical = properties.Get<glm::vec3>("v:position").Vector();
        f.Registry.Raw().remove<Dirty::DirtyVertexPositions>(entity);
        const std::size_t undoBefore = f.History.UndoCount();

        ASSERT_EQ(f.Bind(entity, A::Position, domain, "v:offset"), Cmd::Applied);
        EXPECT_EQ(f.History.UndoCount(), undoBefore + 1u);
        const auto* bindings = f.Registry.Raw().try_get<Runtime::VertexChannelBindingSet>(entity);
        ASSERT_NE(bindings, nullptr);
        const L lane = LaneFor(A::Position, domain);
        EXPECT_EQ(Runtime::FindVertexChannelSourceBinding(*bindings, lane, A::Position)->Property,
            (Runtime::GeometryPropertyRef{domain, "v:offset", Kind::Vec3}));
        EXPECT_TRUE(f.Registry.Raw().all_of<Dirty::DirtyVertexPositions>(entity));
        EXPECT_EQ(properties.Get<glm::vec3>("v:position").Vector(), canonical);  // never copied
        const auto displayed = Runtime::ResolveDisplayedPositions(properties, domain, bindings, lane);
        EXPECT_TRUE(displayed.Bound);
        EXPECT_EQ(displayed.Name, "v:offset");
        // UI-083: only the bound lane moves; every other lane keeps the canonical positions.
        for (const L other : {L::Surface, L::Edges, L::Points})
        {
            if (other != lane)
                EXPECT_EQ(Runtime::ResolveDisplayedPositions(properties, domain, bindings, other).Name, "v:position");
        }

        ASSERT_EQ(f.Bind(entity, A::Position, domain), Cmd::Applied);  // Default
        EXPECT_FALSE(f.Row(entity, A::Position, domain).Bound);
        ASSERT_EQ(f.History.Undo().Status, Runtime::EditorCommandHistoryStatus::Undone);
        EXPECT_EQ(f.Row(entity, A::Position, domain).Source.Name, "v:offset");
    }
}

TEST(VertexChannelBindings, DisplayedPositionsFallBackToCanonicalWhenTheBoundSourceIsUnusable)
{
    ECS::Scene::Registry registry;
    const ECS::EntityHandle mesh = MakeSelectable(registry, "Mesh");
    AddTriangleMeshSource(registry, mesh);
    auto& properties = registry.Raw().get<GS::Vertices>(mesh).Properties;
    SetProperty<glm::vec3>(properties, "v:offset", std::vector<glm::vec3>(3u, glm::vec3{0, 0, 2}));
    Runtime::VertexChannelBindingSet bindings{
        .Surface = {.Position = {.Enabled = true, .Property = {D::MeshVertex, "v:offset", Kind::Vec3}}}};
    EXPECT_TRUE(Runtime::ResolveDisplayedPositions(properties, D::MeshVertex, &bindings, L::Surface).Bound);

    properties.Get<glm::vec3>("v:offset").Vector()[1].x = std::numeric_limits<float>::infinity();
    auto displayed = Runtime::ResolveDisplayedPositions(properties, D::MeshVertex, &bindings, L::Surface);
    EXPECT_FALSE(displayed.Bound);  // never draws garbage
    EXPECT_EQ(displayed.Name, "v:position");
    EXPECT_EQ(displayed.Values.size(), 3u);

    bindings.Surface.Position.Property.Name = "v:gone";
    EXPECT_FALSE(Runtime::ResolveDisplayedPositions(properties, D::MeshVertex, &bindings, L::Surface).Bound);
    bindings.Surface.Position.Property = {D::GraphNode, "v:offset", Kind::Vec3};  // other domain
    EXPECT_FALSE(Runtime::ResolveDisplayedPositions(properties, D::MeshVertex, &bindings, L::Surface).Bound);
    EXPECT_EQ(Runtime::ResolveDisplayedPositions(properties, D::MeshVertex, nullptr, L::Surface).Name, "v:position");
}

// UI-083 slice 3b: the points lane draws its bound normal, else the canonical one (mesh
// v:normal, cloud p:normal, none on graph nodes). Non-finite values do not disqualify a
// source: each element is repaired on upload.
TEST(VertexChannelBindings, PointNormalsFollowTheBoundSourceElseTheCanonicalOne)
{
    ECS::Scene::Registry registry;
    const ECS::EntityHandle mesh = MakeSelectable(registry, "Mesh");
    AddTriangleMeshSource(registry, mesh);
    auto& properties = registry.Raw().get<GS::Vertices>(mesh).Properties;
    SetProperty<glm::vec3>(properties, "v:normal", std::vector<glm::vec3>(3u, glm::vec3{0, 0, 1}));
    SetProperty<glm::vec3>(properties, "v:alt", std::vector<glm::vec3>(3u, glm::vec3{1, 0, 0}));
    properties.Get<glm::vec3>("v:alt").Vector()[1].x = std::numeric_limits<float>::quiet_NaN();
    Runtime::VertexChannelBindingSet bindings{
        .Points = {.Normal = {.Enabled = true, .Property = {D::MeshVertex, "v:alt", Kind::Vec3}}}};
    EXPECT_EQ(Runtime::ResolvePointNormalSource(properties, D::MeshVertex, &bindings), "v:alt");
    // Only the points lane's slot counts.
    Runtime::VertexChannelBindingSet surfaceOnly{.Surface = bindings.Points};
    EXPECT_EQ(Runtime::ResolvePointNormalSource(properties, D::MeshVertex, &surfaceOnly), "v:normal");
    bindings.Points.Normal.Property.Name = "v:gone";
    EXPECT_EQ(Runtime::ResolvePointNormalSource(properties, D::MeshVertex, &bindings), "v:normal");
    bindings.Points.Normal.Property = {D::GraphNode, "v:alt", Kind::Vec3};  // other domain
    EXPECT_EQ(Runtime::ResolvePointNormalSource(properties, D::MeshVertex, &bindings), "v:normal");
    EXPECT_EQ(Runtime::ResolvePointNormalSource(properties, D::MeshVertex, nullptr), "v:normal");
    auto normal = properties.Get<glm::vec3>("v:normal");
    properties.Remove(normal);
    EXPECT_EQ(Runtime::ResolvePointNormalSource(properties, D::MeshVertex, nullptr), "");

    const ECS::EntityHandle graph = MakeSelectable(registry, "Graph");
    AddGraphSource(registry, graph);
    auto& nodes = registry.Raw().get<GS::Vertices>(graph).Properties;
    EXPECT_EQ(Runtime::ResolvePointNormalSource(nodes, D::GraphNode, nullptr), "");
    SetProperty<glm::vec3>(nodes, "v:dir", std::vector<glm::vec3>(nodes.Size(), glm::vec3{0, 1, 0}));
    const Runtime::VertexChannelBindingSet graphBindings{
        .Points = {.Normal = {.Enabled = true, .Property = {D::GraphNode, "v:dir", Kind::Vec3}}}};
    EXPECT_EQ(Runtime::ResolvePointNormalSource(nodes, D::GraphNode, &graphBindings), "v:dir");

    const ECS::EntityHandle cloud = MakeSelectable(registry, "Cloud");
    AddPointCloudSource(registry, cloud, 2u);
    auto& points = registry.Raw().get<GS::Vertices>(cloud).Properties;
    EXPECT_EQ(Runtime::ResolvePointNormalSource(points, D::PointCloudPoint, nullptr), "");
    SetProperty<glm::vec3>(points, "p:normal", std::vector<glm::vec3>(2u, glm::vec3{0, 0, 1}));
    EXPECT_EQ(Runtime::ResolvePointNormalSource(points, D::PointCloudPoint, nullptr), "p:normal");
}

TEST(VertexChannelBindings, CameraFocusAndDefaultVectorFieldLengthFollowTheDisplayedPositions)
{
    BindingFixture f;
    auto& raw = f.Registry.Raw();
    const ECS::EntityHandle mesh = MakeSelectable(f.Registry, "Mesh");
    AddTriangleMeshSource(f.Registry, mesh);  // canonical triangle near the origin
    raw.emplace_or_replace<ECS::Components::Transform::WorldMatrix>(mesh).Matrix = glm::mat4{1.0f};
    auto& bounds = raw.emplace_or_replace<ECS::Components::Culling::World::Bounds>(mesh);
    bounds.WorldBoundingSphere.Center = {0.5f, 0.5f, 0.0f};
    bounds.WorldBoundingSphere.Radius = 0.75f;
    auto& properties = raw.get<GS::Vertices>(mesh).Properties;
    SetProperty<glm::vec3>(properties, "v:shifted",
                           {{0, 0, 100}, {10, 0, 100}, {0, 10, 100}});
    SetProperty<glm::vec3>(properties, "v:flow", std::vector<glm::vec3>(3u, glm::vec3{1, 0, 0}));
    const std::vector<ECS::EntityHandle> entities{mesh};

    const auto canonical = Runtime::ComputeFocusTargetForEntities(f.Registry, entities);
    ASSERT_TRUE(canonical.has_value());
    EXPECT_NEAR(canonical->Center.z, 0.0f, 1e-4f);

    ASSERT_EQ(f.Bind(mesh, A::Position, D::MeshVertex, "v:shifted"), Cmd::Applied);
    // Focus, the camera pose/preset command and view capture all frame through
    // this function: it frames what is drawn, not the canonical bounds component.
    const auto displayed = Runtime::ComputeFocusTargetForEntities(f.Registry, entities);
    ASSERT_TRUE(displayed.has_value());
    EXPECT_NEAR(displayed->Center.x, 5.0f, 1e-3f);
    EXPECT_NEAR(displayed->Center.y, 5.0f, 1e-3f);
    EXPECT_NEAR(displayed->Center.z, 100.0f, 1e-3f);
    EXPECT_GE(displayed->Radius, std::sqrt(50.0f) - 1e-3f);
    const Runtime::GizmoInteraction gizmo;
    const auto frame = gizmo.ComputeFrame(
        f.Registry, entities, Runtime::GizmoOrientation::Global, Runtime::GizmoPivotMode::BoundsCenters);
    ASSERT_TRUE(frame.Available());
    EXPECT_LT(glm::length(frame.Pivot - displayed->Center), 1e-3f);

    // The default glyph length is 2% of the displayed diagonal (canonical: 0.028).
    ASSERT_EQ(Runtime::ApplyEditorGeometryVectorFieldCommand(
                  f.Context, Runtime::EditorGeometryVectorFieldCommand{
                                 .StableEntityId = Runtime::SelectionController::ToStableEntityId(mesh),
                                 .Operation = Runtime::EditorVectorFieldOperation::Add,
                                 .Layer = {.Vector = {D::MeshVertex, "v:flow", Kind::Vec3}},
                                 .UseLayerStyle = false}),
              Cmd::Applied);
    const auto& recipe = raw.get<Runtime::GeometryPresentationRecipe>(mesh);
    ASSERT_EQ(recipe.VectorFields.size(), 1u);
    EXPECT_NEAR(recipe.VectorFields.front().Length, 0.02f * std::sqrt(200.0f), 1e-4f);
    auto shifted = properties.GetOrAdd<glm::vec3>("v:shifted", {});
    for (auto& point : shifted.Vector())
        point.z = 200.0f;
    const auto updated = gizmo.ComputeFrame(
        f.Registry, entities, Runtime::GizmoOrientation::Global, Runtime::GizmoPivotMode::BoundsCenters);
    EXPECT_NEAR(updated.Pivot.z, 200.0f, 1e-3f);
    const auto unchanged = gizmo.ComputeFrame(
        f.Registry, entities, Runtime::GizmoOrientation::Global, Runtime::GizmoPivotMode::BoundsCenters);
    EXPECT_EQ(unchanged.Pivot, updated.Pivot);

    // UI-083: with the surface and the points drawn (the points still canonical), focus
    // and the gizmo pivot span both drawn sources (canonical z 0, surface z 200).
    raw.emplace<G::RenderSurface>(mesh);
    raw.emplace<G::RenderPoints>(mesh);
    const auto both = Runtime::ComputeFocusTargetForEntities(f.Registry, entities);
    ASSERT_TRUE(both.has_value());
    EXPECT_NEAR(both->Center.z, 100.0f, 1e-3f);
    const auto spanning = gizmo.ComputeFrame(
        f.Registry, entities, Runtime::GizmoOrientation::Global, Runtime::GizmoPivotMode::BoundsCenters);
    EXPECT_NEAR(spanning.Pivot.z, 100.0f, 1e-3f);

    // A hidden surface no longer counts: only the drawn, canonical points are framed.
    raw.remove<G::RenderSurface>(mesh);
    const auto pointsOnly = Runtime::ComputeFocusTargetForEntities(f.Registry, entities);
    ASSERT_TRUE(pointsOnly.has_value());
    EXPECT_NEAR(pointsOnly->Center.z, 0.0f, 1e-3f);
    const auto pointsPivot = gizmo.ComputeFrame(
        f.Registry, entities, Runtime::GizmoOrientation::Global, Runtime::GizmoPivotMode::BoundsCenters);
    EXPECT_NEAR(pointsPivot.Pivot.z, 0.0f, 1e-3f);
}

TEST(VertexChannelBindings, NormalAndTexcoordBindOnVertexOrCornerDomainsThroughOneSlot)
{
    BindingFixture f;
    const ECS::EntityHandle mesh = MakeSelectable(f.Registry, "Mesh");
    AddTriangleMeshSource(f.Registry, mesh);
    SetProperty<glm::vec3>(f.Registry.Raw().get<GS::Vertices>(mesh).Properties, "v:n2",
                           std::vector<glm::vec3>(3u, glm::vec3{1, 0, 0}));
    SetProperty<glm::vec3>(f.Registry.Raw().get<GS::Halfedges>(mesh).Properties, "h:n2",
                           std::vector<glm::vec3>(6u, glm::vec3{0, 1, 0}));
    SetProperty<glm::vec2>(f.Registry.Raw().get<GS::Halfedges>(mesh).Properties, "h:uv2",
                           std::vector<glm::vec2>(6u, glm::vec2{0.25f}));

    ASSERT_EQ(f.Bind(mesh, A::Normal, D::MeshVertex, "v:n2"), Cmd::Applied);
    ASSERT_EQ(f.Bind(mesh, A::Normal, D::MeshHalfedge, "h:n2"), Cmd::Applied);
    EXPECT_EQ(f.Row(mesh, A::Normal, D::MeshVertex).Source.Domain, D::MeshHalfedge);  // one normal source at a time
    EXPECT_EQ(f.Row(mesh, A::Normal, D::MeshHalfedge).Source.Name, "h:n2");
    // Default clears what the surface row draws, whichever of its domains that is.
    EXPECT_EQ(f.Bind(mesh, A::Normal, D::MeshVertex), Cmd::Applied);
    EXPECT_FALSE(f.Row(mesh, A::Normal, D::MeshHalfedge).Bound);
    f.Registry.Raw().remove<Dirty::DirtyVertexTexcoords>(mesh);
    ASSERT_EQ(f.Bind(mesh, A::Texcoord, D::MeshHalfedge, "h:uv2"), Cmd::Applied);
    EXPECT_EQ(
        f.Registry.Raw().get<Runtime::VertexChannelBindingSet>(mesh).Surface.Texcoord.Property.Domain, D::MeshHalfedge);
    EXPECT_TRUE(f.Registry.Raw().all_of<Dirty::DirtyVertexTexcoords>(mesh));
    EXPECT_EQ(f.History.UndoCount(), 4u);
}

TEST(VertexChannelBindings, CommandRefusesMismatchesWithTypedReasons)
{
    BindingFixture f;
    const ECS::EntityHandle mesh = MakeSelectable(f.Registry, "Mesh");
    AddTriangleMeshSource(f.Registry, mesh);
    auto& vertices = f.Registry.Raw().get<GS::Vertices>(mesh).Properties;
    SetProperty<float>(vertices, "v:size", {1.0f, 2.0f, 3.0f});
    SetProperty<glm::vec3>(vertices, "v:broken",
                           {{0, 0, 1}, {std::numeric_limits<float>::infinity(), 0, 0}, {0, 1, 1}});
    SetProperty<glm::vec3>(f.Registry.Raw().get<GS::Faces>(mesh).Properties, "f:offset", {{0, 0, 1}});

    EXPECT_EQ(f.Bind(mesh, A::Position, D::MeshVertex, "v:size"), Cmd::AttributeSourceTypeMismatch);
    EXPECT_EQ(f.Bind(mesh, A::Position, D::MeshVertex, "v:none"), Cmd::AttributeSourceMissing);
    EXPECT_EQ(f.Bind(mesh, A::Position, D::MeshVertex, "v:broken"), Cmd::AttributeSourceNonFinite);
    EXPECT_EQ(f.Bind(mesh, A::Position, D::MeshFace, "f:offset"), Cmd::UnsupportedRenderAttribute);
    EXPECT_EQ(f.Bind(mesh, A::Position, D::GraphNode, "v:size"), Cmd::UnsupportedRenderAttribute);
    EXPECT_EQ(f.Bind(mesh, A::Texcoord, D::MeshVertex, "v:size"), Cmd::AttributeSourceTypeMismatch);
    EXPECT_EQ(f.Bind(mesh, A::Normal, D::MeshVertex, "v:broken"), Cmd::Applied);  // repaired per element
    EXPECT_EQ(Runtime::ApplyEditorAttributeBindingCommand(
                  f.Context, Runtime::EditorAttributeBindingCommand{
                                 .StableEntityId = 999999u, .Attribute = A::Position,
                                 .Domain = D::MeshVertex, .PropertyName = "v:size"}),
              Cmd::StaleEntity);
    EXPECT_EQ(f.History.UndoCount(), 1u);  // only the accepted normal binding
}

TEST(VertexChannelBindings, ColorBindsThroughTheOverlayOnEveryLaneAndDefaultClearsIt)
{
    using Source = G::VisualizationConfig::ColorSource;
    BindingFixture f;
    const ECS::EntityHandle mesh = MakeSelectable(f.Registry, "Mesh");
    AddTriangleMeshSource(f.Registry, mesh);
    SetProperty<float>(f.Registry.Raw().get<GS::Vertices>(mesh).Properties, "v:heat", {0.0f, 0.5f, 1.0f});
    SetProperty<glm::vec4>(f.Registry.Raw().get<GS::Faces>(mesh).Properties, "f:paint",
                           {{1, 0, 0, 1}});
    SetProperty<glm::vec3>(f.Registry.Raw().get<GS::Edges>(mesh).Properties, "e:paint",
                           std::vector<glm::vec3>(3u, glm::vec3{0, 1, 0}));

    ASSERT_EQ(f.Bind(mesh, A::Color, D::MeshVertex, "v:heat"), Cmd::Applied);
    EXPECT_EQ(f.Row(mesh, A::Color, D::MeshVertex).Source.Name, "v:heat");
    ASSERT_EQ(f.Bind(mesh, A::Color, D::MeshFace, "f:paint"), Cmd::Applied);
    EXPECT_EQ(f.Row(mesh, A::Color, D::MeshFace).Source.Name, "f:paint");
    EXPECT_EQ(f.Row(mesh, A::Color, D::MeshVertex).Source.Domain, D::MeshFace);  // one surface overlay
    ASSERT_EQ(f.Bind(mesh, A::Color, D::MeshEdge, "e:paint"), Cmd::Applied);
    const auto* edges = f.Registry.Raw().try_get<G::VisualizationLaneOverrides>(mesh);
    ASSERT_NE(edges, nullptr);
    ASSERT_TRUE(edges->Edges.has_value());
    EXPECT_EQ(edges->Edges->Source, Source::PerEdgeBuffer);
    EXPECT_EQ(f.Row(mesh, A::Color, D::MeshEdge).Source.Name, "e:paint");

    const std::size_t undoBefore = f.History.UndoCount();
    ASSERT_EQ(f.Bind(mesh, A::Color, D::MeshFace), Cmd::Applied);  // Default
    EXPECT_EQ(f.History.UndoCount(), undoBefore + 1u);
    EXPECT_FALSE(f.Row(mesh, A::Color, D::MeshFace).Bound);
    EXPECT_TRUE(f.Row(mesh, A::Color, D::MeshEdge).Bound);
    EXPECT_EQ(f.Bind(mesh, A::Color, D::MeshFace), Cmd::NoChange);
    ASSERT_EQ(f.History.Undo().Status, Runtime::EditorCommandHistoryStatus::Undone);
    EXPECT_EQ(f.Row(mesh, A::Color, D::MeshFace).Source.Name, "f:paint");

    // Graph and point-cloud lanes read and write the same overlay.
    const ECS::EntityHandle graph = MakeSelectable(f.Registry, "Graph");
    AddGraphSource(f.Registry, graph);
    SetProperty<float>(f.Registry.Raw().get<GS::Edges>(graph).Properties, "e:load", {1.0f, 2.0f});
    ASSERT_EQ(f.Bind(graph, A::Color, D::GraphEdge, "e:load"), Cmd::Applied);
    EXPECT_EQ(f.Row(graph, A::Color, D::GraphEdge).Source.Name, "e:load");
    const ECS::EntityHandle cloud = MakeSelectable(f.Registry, "Cloud");
    AddPointCloudSource(f.Registry, cloud, 2u);
    SetPositions(f.Registry.Raw().get<GS::Vertices>(cloud), {{0, 0, 0}, {1, 0, 0}});
    SetProperty<glm::vec3>(f.Registry.Raw().get<GS::Vertices>(cloud).Properties, "v:rgb",
                           {{1, 0, 0}, {0, 0, 1}});
    ASSERT_EQ(f.Bind(cloud, A::Color, D::PointCloudPoint, "v:rgb"), Cmd::Applied);
    EXPECT_EQ(f.Row(cloud, A::Color, D::PointCloudPoint).Source.Name, "v:rgb");
    const auto* points = f.Registry.Raw().try_get<G::VisualizationLaneOverrides>(cloud);
    ASSERT_NE(points, nullptr);
    ASSERT_TRUE(points->Points.has_value());
    EXPECT_EQ(points->Points->Source, Source::PerVertexBuffer);

    // The overlay's recipe encoder is the validator.
    SetProperty<float>(f.Registry.Raw().get<GS::Vertices>(cloud).Properties, "v:nan",
                       {std::numeric_limits<float>::quiet_NaN(), 1.0f});
    EXPECT_EQ(f.Bind(cloud, A::Color, D::PointCloudPoint, "v:nan"), Cmd::AttributeSourceNonFinite);
    const auto* nanCandidate = FindCandidate(f.Row(cloud, A::Color, D::PointCloudPoint), "v:nan");
    ASSERT_NE(nanCandidate, nullptr);
    EXPECT_FALSE(nanCandidate->Compatible);
}

TEST(VertexChannelBindings, PixelSizeBindingsWriteTheRenderHintAndDefaultRestoresUniform)
{
    BindingFixture f;
    const ECS::EntityHandle cloud = MakeSelectable(f.Registry, "Cloud");
    AddPointCloudSource(f.Registry, cloud, 2u);
    SetPositions(f.Registry.Raw().get<GS::Vertices>(cloud), {{0, 0, 0}, {1, 0, 0}});
    SetProperty<float>(f.Registry.Raw().get<GS::Vertices>(cloud).Properties, "v:radius", {2.0f, 3.0f});
    SetProperty<float>(f.Registry.Raw().get<GS::Vertices>(cloud).Properties, "v:nan",
                       {std::numeric_limits<float>::quiet_NaN(), 3.0f});
    const auto size = [&]
    {
        return f.Registry.Raw().get<G::RenderPoints>(cloud).SizeProperty;
    };

    EXPECT_EQ(f.Bind(cloud, A::PointSize, D::PointCloudPoint, "v:nan"), Cmd::AttributeSourceNonFinite);
    EXPECT_EQ(f.Bind(cloud, A::PointSize, D::PointCloudPoint), Cmd::NoChange);
    ASSERT_EQ(f.Bind(cloud, A::PointSize, D::PointCloudPoint, "v:radius"), Cmd::Applied);
    EXPECT_EQ(size(), "v:radius");
    EXPECT_EQ(f.Row(cloud, A::PointSize, D::PointCloudPoint).Source.Name, "v:radius");
    ASSERT_EQ(f.Bind(cloud, A::PointSize, D::PointCloudPoint), Cmd::Applied);
    EXPECT_TRUE(size().empty());
    ASSERT_EQ(f.History.Undo().Status, Runtime::EditorCommandHistoryStatus::Undone);
    EXPECT_EQ(size(), "v:radius");

    const ECS::EntityHandle graph = MakeSelectable(f.Registry, "Graph");
    AddGraphSource(f.Registry, graph);
    SetProperty<float>(f.Registry.Raw().get<GS::Edges>(graph).Properties, "e:width", {1.0f, 4.0f});
    ASSERT_EQ(f.Bind(graph, A::LineWidth, D::GraphEdge, "e:width"), Cmd::Applied);
    EXPECT_EQ(f.Registry.Raw().get<G::RenderEdges>(graph).WidthProperty, "e:width");
}

// Each Appearance section colors its own primitive: a Color chosen on the
// points lane of a mesh or a graph leaves the surface and the edges alone.
TEST(VertexChannelBindings, ColorBindsOnlyTheLaneItIsChosenOn)
{
    using Source = G::VisualizationConfig::ColorSource;
    BindingFixture f;
    const ECS::EntityHandle mesh = MakeSelectable(f.Registry, "Mesh");
    AddTriangleMeshSource(f.Registry, mesh);
    SetProperty<float>(f.Registry.Raw().get<GS::Vertices>(mesh).Properties, "v:heat", {0.0f, 0.5f, 1.0f});

    ASSERT_EQ(f.BindOn(mesh, L::Points, A::Color, D::MeshVertex, "v:heat"), Cmd::Applied);
    EXPECT_EQ(f.RowOn(mesh, L::Points, A::Color).Source.Name, "v:heat");
    EXPECT_FALSE(f.RowOn(mesh, L::Surface, A::Color).Bound);
    EXPECT_FALSE(f.RowOn(mesh, L::Edges, A::Color).Bound);
    const auto* meshLanes = f.Registry.Raw().try_get<G::VisualizationLaneOverrides>(mesh);
    ASSERT_NE(meshLanes, nullptr);
    ASSERT_TRUE(meshLanes->Points.has_value());
    EXPECT_EQ(meshLanes->Points->Source, Source::ScalarField);
    EXPECT_FALSE(meshLanes->Surface.has_value());
    ASSERT_EQ(f.BindOn(mesh, L::Edges, A::Color, D::MeshVertex, "v:heat"), Cmd::Applied);
    EXPECT_TRUE(f.RowOn(mesh, L::Edges, A::Color).Bound);
    ASSERT_EQ(f.BindOn(mesh, L::Points, A::Color, D::MeshVertex), Cmd::Applied);  // Default
    EXPECT_FALSE(f.RowOn(mesh, L::Points, A::Color).Bound);
    EXPECT_TRUE(f.RowOn(mesh, L::Edges, A::Color).Bound);

    const ECS::EntityHandle graph = MakeSelectable(f.Registry, "Graph");
    AddGraphSource(f.Registry, graph);
    const std::size_t nodes = f.Registry.Raw().get<GS::Vertices>(graph).Properties.Size();
    SetProperty<float>(f.Registry.Raw().get<GS::Vertices>(graph).Properties, "v:rank", std::vector<float>(nodes, 2.0f));
    ASSERT_EQ(f.BindOn(graph, L::Points, A::Color, D::GraphNode, "v:rank"), Cmd::Applied);
    EXPECT_TRUE(f.RowOn(graph, L::Points, A::Color).Bound);
    EXPECT_FALSE(f.RowOn(graph, L::Edges, A::Color).Bound);
}

// A source outside the lane table is refused with a typed status.
TEST(VertexChannelBindings, LaneRowsRefuseDomainsTheirLaneDoesNotRead)
{
    BindingFixture f;
    const ECS::EntityHandle mesh = MakeSelectable(f.Registry, "Mesh");
    AddTriangleMeshSource(f.Registry, mesh);
    SetProperty<glm::vec3>(f.Registry.Raw().get<GS::Halfedges>(mesh).Properties,
        "h:dir",
        std::vector<glm::vec3>(f.Registry.Raw().get<GS::Halfedges>(mesh).Properties.Size(), glm::vec3{0, 0, 1}));
    SetProperty<glm::vec3>(
        f.Registry.Raw().get<GS::Vertices>(mesh).Properties, "v:dir", std::vector<glm::vec3>(3u, glm::vec3{0, 0, 1}));
    // Corner normals feed the surface only; edges read no normal.
    EXPECT_EQ(f.BindOn(mesh, L::Points, A::Normal, D::MeshHalfedge, "h:dir"), Cmd::UnsupportedRenderAttribute);
    EXPECT_EQ(f.BindOn(mesh, L::Surface, A::Normal, D::MeshHalfedge, "h:dir"), Cmd::Applied);
    EXPECT_EQ(f.BindOn(mesh, L::Edges, A::Normal, D::MeshVertex, "v:dir"), Cmd::UnsupportedRenderAttribute);
    EXPECT_EQ(f.BindOn(mesh, L::Points, A::Texcoord, D::MeshVertex), Cmd::UnsupportedRenderAttribute);

    const ECS::EntityHandle cloud = MakeSelectable(f.Registry, "Cloud");
    AddPointCloudSource(f.Registry, cloud, 2u);
    SetPositions(f.Registry.Raw().get<GS::Vertices>(cloud), {{0, 0, 0}, {1, 0, 0}});
    EXPECT_EQ(f.BindOn(cloud, L::Surface, A::Color, D::MeshVertex), Cmd::UnsupportedRenderAttribute);
}

// A uniform size or width is kept while a property is bound; Default brings
// the last uniform value back instead of the component default.
TEST(VertexChannelBindings, DefaultRestoresTheRememberedUniformSizeAndWidth)
{
    BindingFixture f;
    const ECS::EntityHandle mesh = MakeSelectable(f.Registry, "Mesh");
    AddTriangleMeshSource(f.Registry, mesh);
    SetProperty<float>(f.Registry.Raw().get<GS::Vertices>(mesh).Properties, "v:radius", {1.0f, 2.0f, 3.0f});
    SetProperty<float>(f.Registry.Raw().get<GS::Edges>(mesh).Properties, "e:width", {1.0f, 2.0f, 3.0f});
    const auto stableId = Runtime::SelectionController::ToStableEntityId(mesh);
    ASSERT_EQ(Runtime::ApplyEditorRenderHintCommand(f.Context,
                  Runtime::EditorRenderHintCommand{.StableEntityId = stableId,
                      .SetEdges = true,
                      .EnableEdges = true,
                      .SetUniformEdgeWidth = true,
                      .UniformEdgeWidth = 2.5f,
                      .SetPoints = true,
                      .EnablePoints = true,
                      .SetUniformPointSize = true,
                      .UniformPointSize = 3.5f}),
        Cmd::Applied);

    ASSERT_EQ(f.BindOn(mesh, L::Points, A::PointSize, D::MeshVertex, "v:radius"), Cmd::Applied);
    ASSERT_EQ(f.BindOn(mesh, L::Edges, A::LineWidth, D::MeshEdge, "e:width"), Cmd::Applied);
    const auto& points = f.Registry.Raw().get<G::RenderPoints>(mesh);
    const auto& edges = f.Registry.Raw().get<G::RenderEdges>(mesh);
    EXPECT_EQ(points.SizeProperty, "v:radius");
    EXPECT_FLOAT_EQ(points.Size, 3.5f);
    EXPECT_FLOAT_EQ(edges.Width, 2.5f);

    ASSERT_EQ(f.BindOn(mesh, L::Points, A::PointSize, D::MeshVertex), Cmd::Applied);
    ASSERT_EQ(f.BindOn(mesh, L::Edges, A::LineWidth, D::MeshEdge), Cmd::Applied);
    EXPECT_TRUE(f.Registry.Raw().get<G::RenderPoints>(mesh).SizeProperty.empty());
    EXPECT_FLOAT_EQ(f.Registry.Raw().get<G::RenderPoints>(mesh).Size, 3.5f);
    EXPECT_TRUE(f.Registry.Raw().get<G::RenderEdges>(mesh).WidthProperty.empty());
    EXPECT_FLOAT_EQ(f.Registry.Raw().get<G::RenderEdges>(mesh).Width, 2.5f);
}

TEST(VertexChannelBindings, ColorDefaultMasksOnlyItsLaneWhenTheOverlayIsInherited)
{
    using Source = G::VisualizationConfig::ColorSource;
    BindingFixture f;
    const ECS::EntityHandle graph = MakeSelectable(f.Registry, "Graph");
    AddGraphSource(f.Registry, graph);
    SetProperty<float>(f.Registry.Raw().get<GS::Vertices>(graph).Properties, "v:heat", {0.0f, 1.0f, 2.0f});
    G::VisualizationConfig inherited{};
    inherited.Source = Source::ScalarField;
    inherited.ScalarFieldName = "v:heat";
    inherited.ScalarDomain = G::VisualizationConfig::Domain::Vertex;
    f.Registry.Raw().emplace<G::VisualizationConfig>(graph, inherited);
    ASSERT_EQ(f.Row(graph, A::Color, D::GraphNode).Source.Name, "v:heat");

    ASSERT_EQ(f.Bind(graph, A::Color, D::GraphNode), Cmd::Applied);
    EXPECT_FALSE(f.Row(graph, A::Color, D::GraphNode).Bound);
    // The entity-level overlay itself is untouched; only the edges lane masks it.
    ASSERT_TRUE(f.Registry.Raw().all_of<G::VisualizationConfig>(graph));
    EXPECT_EQ(f.Registry.Raw().get<G::VisualizationConfig>(graph).ScalarFieldName, "v:heat");
    const auto& lanes = f.Registry.Raw().get<G::VisualizationLaneOverrides>(graph);
    ASSERT_TRUE(lanes.Edges.has_value());
    EXPECT_EQ(lanes.Edges->Source, Source::Material);
    EXPECT_FALSE(lanes.Points.has_value());
    ASSERT_EQ(f.History.Undo().Status, Runtime::EditorCommandHistoryStatus::Undone);
    EXPECT_EQ(f.Row(graph, A::Color, D::GraphNode).Source.Name, "v:heat");
}

TEST(VertexChannelBindings, RebindingColorKeepsOverlayStylingAndUndoRedoRestoresItExactly)
{
    BindingFixture f;
    const ECS::EntityHandle mesh = MakeSelectable(f.Registry, "Mesh");
    AddTriangleMeshSource(f.Registry, mesh);
    auto& vertices = f.Registry.Raw().get<GS::Vertices>(mesh).Properties;
    SetProperty<float>(vertices, "v:heat", {0.0f, 0.5f, 1.0f});
    SetProperty<float>(vertices, "v:cold", {2.0f, 1.0f, 0.0f});
    const auto id = Runtime::SelectionController::ToStableEntityId(mesh);
    Runtime::EditorVisualizationConfigCommand styled{
        .StableEntityId = id,
        .Target = Runtime::EditorVisualizationTarget::Surface,
        .Source = G::VisualizationConfig::ColorSource::ScalarField,
        .ScalarFieldName = "v:heat",
        .ScalarDomain = G::VisualizationConfig::Domain::Vertex,
        .ScalarAutoRange = false,
        .ScalarRangeMin = -1.0f,
        .ScalarRangeMax = 3.0f,
        .ScalarBinCount = 4u,
        .IsolineCount = 5u,
        .ScalarColormap = Extrinsic::Graphics::Colormap::Type::Inferno,
        .IsolineWidth = 2.5f,
    };
    ASSERT_EQ(Runtime::ApplyEditorVisualizationConfigCommand(f.Context, styled), Cmd::Applied);
    const auto surface = [&] {
        return f.Registry.Raw().get<G::VisualizationLaneOverrides>(mesh).Surface.value();
    };
    const G::VisualizationConfig before = surface();

    ASSERT_EQ(f.Bind(mesh, A::Color, D::MeshVertex, "v:cold"), Cmd::Applied);
    const G::VisualizationConfig bound = surface();
    EXPECT_EQ(bound.ScalarFieldName, "v:cold");
    EXPECT_EQ(bound.Scalar.Map, Extrinsic::Graphics::Colormap::Type::Inferno);
    EXPECT_EQ(bound.Scalar.BinCount, 4u);
    EXPECT_EQ(bound.Scalar.Isolines.Num, 5u);
    EXPECT_FLOAT_EQ(bound.Scalar.Isolines.Width, 2.5f);

    const auto same = [](const G::VisualizationConfig& a, const G::VisualizationConfig& b) {
        return a.Source == b.Source && a.ScalarFieldName == b.ScalarFieldName &&
               a.Scalar.Map == b.Scalar.Map && a.Scalar.AutoRange == b.Scalar.AutoRange &&
               a.Scalar.RangeMin == b.Scalar.RangeMin && a.Scalar.RangeMax == b.Scalar.RangeMax &&
               a.Scalar.BinCount == b.Scalar.BinCount && a.Scalar.Isolines.Num == b.Scalar.Isolines.Num &&
               a.Scalar.Isolines.Width == b.Scalar.Isolines.Width;
    };
    ASSERT_EQ(f.History.Undo().Status, Runtime::EditorCommandHistoryStatus::Undone);
    EXPECT_TRUE(same(surface(), before));
    ASSERT_EQ(f.History.Redo().Status, Runtime::EditorCommandHistoryStatus::Redone);
    EXPECT_TRUE(same(surface(), bound));
}

TEST(VertexChannelBindings, ShowPropertyAndColorBindingKeepTheLaneColormapAlike)
{
    BindingFixture f;
    const ECS::EntityHandle mesh = MakeSelectable(f.Registry, "Mesh");
    AddTriangleMeshSource(f.Registry, mesh);
    auto& vertices = f.Registry.Raw().get<GS::Vertices>(mesh).Properties;
    SetProperty<float>(vertices, "v:a", {0.0f, 0.5f, 1.0f});
    SetProperty<float>(vertices, "v:b", {1.0f, 0.5f, 0.0f});
    const auto id = Runtime::SelectionController::ToStableEntityId(mesh);
    const auto surfaceMap = [&] {
        return f.Registry.Raw().get<G::VisualizationLaneOverrides>(mesh).Surface->Scalar.Map;
    };
    const auto showProperty = [&](const char* name) {
        return Runtime::ApplyEditorVisualizationRecipeCommand(
            f.Context, Runtime::EditorVisualizationRecipeCommand{
                           .StableEntityId = id,
                           .Recipe = Runtime::MakeEditorPropertyVisualizationRecipe(
                               {D::MeshVertex, name, Kind::Float})});
    };

    ASSERT_EQ(showProperty("v:a"), Cmd::Applied);  // seeds the lane with the recipe colormap
    EXPECT_EQ(surfaceMap(), Extrinsic::Graphics::Colormap::Type::Viridis);
    Runtime::EditorVisualizationConfigCommand styled{
        .StableEntityId = id,
        .Target = Runtime::EditorVisualizationTarget::Surface,
        .Source = G::VisualizationConfig::ColorSource::ScalarField,
        .ScalarFieldName = "v:a",
        .ScalarDomain = G::VisualizationConfig::Domain::Vertex,
        .ScalarColormap = Extrinsic::Graphics::Colormap::Type::Inferno,
    };
    ASSERT_EQ(Runtime::ApplyEditorVisualizationConfigCommand(f.Context, styled), Cmd::Applied);
    ASSERT_EQ(showProperty("v:b"), Cmd::Applied);
    EXPECT_EQ(surfaceMap(), Extrinsic::Graphics::Colormap::Type::Inferno);
    ASSERT_EQ(f.Bind(mesh, A::Color, D::MeshVertex, "v:a"), Cmd::Applied);
    EXPECT_EQ(surfaceMap(), Extrinsic::Graphics::Colormap::Type::Inferno);

    // An explicit colormap, even the default one, overrides the lane's.
    Runtime::VisualizationRecipe explicitViridis =
        Runtime::MakeEditorPropertyVisualizationRecipe({D::MeshVertex, "v:b", Kind::Float});
    std::get<Runtime::ScalarVisualizationRecipe>(explicitViridis.Data).Colormap =
        Extrinsic::Graphics::Colormap::Type::Viridis;
    ASSERT_EQ(Runtime::ApplyEditorVisualizationRecipeCommand(
                  f.Context, Runtime::EditorVisualizationRecipeCommand{.StableEntityId = id,
                                                                       .Recipe = explicitViridis}),
              Cmd::Applied);
    EXPECT_EQ(surfaceMap(), Extrinsic::Graphics::Colormap::Type::Viridis);
}

// Every (lane, attribute, domain) of the lane table: a compatible property on
// that domain binds as one undoable step, the model reports it as the lane
// row's source, and Default restores the default source.
TEST(VertexChannelBindings, EveryTableRowBindsAndRestoresItsDefault)
{
    BindingFixture f;
    const ECS::EntityHandle mesh = MakeSelectable(f.Registry, "Mesh");
    AddTriangleMeshSource(f.Registry, mesh);
    f.Registry.Raw().emplace<G::RenderPoints>(mesh);
    f.Registry.Raw().emplace<G::RenderEdges>(mesh);
    const ECS::EntityHandle graph = MakeSelectable(f.Registry, "Graph");
    AddGraphSource(f.Registry, graph);
    const ECS::EntityHandle cloud = MakeSelectable(f.Registry, "Cloud");
    AddPointCloudSource(f.Registry, cloud, 3u);
    SetPositions(f.Registry.Raw().get<GS::Vertices>(cloud), {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}});

    const auto entityFor = [&](const D domain) {
        switch (domain)
        {
        case D::GraphNode:
        case D::GraphEdge: return graph;
        case D::PointCloudPoint: return cloud;
        default: return mesh;
        }
    };
    const auto propertiesFor = [&](const D domain) -> Geometry::PropertySet& {
        auto& raw = f.Registry.Raw();
        const ECS::EntityHandle entity = entityFor(domain);
        switch (domain)
        {
        case D::MeshEdge:
        case D::GraphEdge: return raw.get<GS::Edges>(entity).Properties;
        case D::MeshHalfedge: return raw.get<GS::Halfedges>(entity).Properties;
        case D::MeshFace: return raw.get<GS::Faces>(entity).Properties;
        default: return raw.get<GS::Vertices>(entity).Properties;
        }
    };

    for (const Runtime::RenderLaneAttributeRow& laneRow : Runtime::RenderLaneAttributeRows())
    {
        for (const D domain : laneRow.Domains)
        {
            const Runtime::RenderAttributeRule* rule = Runtime::FindRenderAttributeRule(laneRow.Attribute, domain);
            ASSERT_NE(rule, nullptr);
            SCOPED_TRACE(std::string{Runtime::ToString(laneRow.Lane)} + " " +
                std::string{Runtime::ToString(laneRow.Attribute)} + " on " + std::string{Runtime::ToString(domain)});
            Geometry::PropertySet& properties = propertiesFor(domain);
            const std::string name = "x:" + std::string{Runtime::ToString(laneRow.Attribute)};
            const std::size_t count = properties.Size();
            if (rule->AcceptsVec2)
                SetProperty<glm::vec2>(properties, name, std::vector<glm::vec2>(count, glm::vec2{0.5f}));
            else if (rule->AcceptsVec3 || rule->ValidatedByVisualizationRecipe)
                SetProperty<glm::vec3>(properties, name, std::vector<glm::vec3>(count, glm::vec3{0.0f, 0.0f, 1.0f}));
            else
                SetProperty<float>(properties, name, std::vector<float>(count, 3.0f));

            const ECS::EntityHandle entity = entityFor(domain);
            const std::size_t undoBefore = f.History.UndoCount();
            ASSERT_EQ(f.BindOn(entity, laneRow.Lane, laneRow.Attribute, domain, name), Cmd::Applied);
            EXPECT_EQ(f.History.UndoCount(), undoBefore + 1u);
            const auto& bound = f.RowOn(entity, laneRow.Lane, laneRow.Attribute);
            EXPECT_TRUE(bound.Bound);
            EXPECT_EQ(bound.Source.Name, name);
            EXPECT_EQ(bound.Source.Domain, domain);
            EXPECT_FALSE(bound.UsingFallback);
            ASSERT_EQ(f.BindOn(entity, laneRow.Lane, laneRow.Attribute, domain), Cmd::Applied);
            EXPECT_FALSE(f.RowOn(entity, laneRow.Lane, laneRow.Attribute).Bound);
        }
    }
}
