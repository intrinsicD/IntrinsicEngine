// RUNTIME-315 render-attribute binding table, source resolution and model.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include "EditorFeatureTestContext.hpp"

import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.VertexChannelBindings;
import Extrinsic.Runtime.VisualizationEditingOperations;
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

    [[nodiscard]] const Runtime::EditorAttributeBindingRow* FindRow(
        const Runtime::EditorAttributeBindingModel& model, const A attribute, const D domain)
    {
        const auto found = std::ranges::find_if(model.Rows, [&](const auto& row) {
            return row.Attribute == attribute && row.Domain == domain;
        });
        return found == model.Rows.end() ? nullptr : &*found;
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
        {A::Normal, D::PointCloudPoint, "vec3", false, ""},
        {A::Texcoord, D::MeshVertex, "vec2", false, "v:texcoord"},
        {A::Texcoord, D::MeshHalfedge, "vec2", false, "h:texcoord"},
        {A::Color, D::MeshVertex, "float, vec3 or vec4", false, ""},
        {A::Color, D::MeshEdge, "float, vec3 or vec4", false, ""},
        {A::Color, D::MeshFace, "float, vec3 or vec4", false, ""},
        {A::Color, D::GraphNode, "float, vec3 or vec4", false, ""},
        {A::Color, D::GraphEdge, "float, vec3 or vec4", false, ""},
        {A::Color, D::PointCloudPoint, "float, vec3 or vec4", false, ""},
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
    std::set<std::pair<A, D>> meshRows;
    for (const auto& row : model.Rows)
        meshRows.emplace(row.Attribute, row.Domain);
    EXPECT_EQ(meshRows, (std::set<std::pair<A, D>>{
                            {A::Position, D::MeshVertex}, {A::Normal, D::MeshVertex},
                            {A::Normal, D::MeshHalfedge}, {A::Texcoord, D::MeshVertex},
                            {A::Texcoord, D::MeshHalfedge}, {A::Color, D::MeshVertex},
                            {A::Color, D::MeshEdge}, {A::Color, D::MeshFace},
                            {A::PointSize, D::MeshVertex}, {A::LineWidth, D::MeshEdge}}));

    const auto* position = FindRow(model, A::Position, D::MeshVertex);
    ASSERT_NE(position, nullptr);
    EXPECT_FALSE(position->Bound);
    EXPECT_EQ(position->ExpectedType, "vec3");
    EXPECT_EQ(position->ExpectedElementCount, 3u);
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
    registry.Raw().emplace<Runtime::VertexChannelBindingSet>(
        mesh, Runtime::VertexChannelBindingSet{
                  .Position = {.Enabled = true,
                               .Property = {.Domain = D::MeshVertex, .Name = "v:offset",
                                            .ValueKind = Kind::Vec3}},
                  .Normal = {.Enabled = true,
                             .Property = {.Domain = D::MeshVertex, .Name = "v:gone",
                                          .ValueKind = Kind::Vec3}}});
    G::VisualizationConfig color{};
    color.Source = G::VisualizationConfig::ColorSource::ScalarField;
    color.ScalarFieldName = "v:temperature";
    color.ScalarDomain = G::VisualizationConfig::Domain::Vertex;
    registry.Raw().emplace<G::VisualizationConfig>(mesh, color);
    registry.Raw().emplace<G::RenderPoints>(mesh, G::RenderPoints{.SizeSource = std::string{"v:temperature"}});

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
    EXPECT_FALSE(FindRow(model, A::Normal, D::MeshHalfedge)->Bound);
    const auto* vertexColor = FindRow(model, A::Color, D::MeshVertex);
    ASSERT_TRUE(vertexColor->Bound);
    EXPECT_EQ(vertexColor->Source.Name, "v:temperature");
    EXPECT_EQ(vertexColor->Source.ValueKind, Kind::Float);
    EXPECT_FALSE(FindRow(model, A::Color, D::MeshFace)->Bound);
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
    std::set<std::pair<A, D>> graphRows;
    for (const auto& row : model.Rows)
        graphRows.emplace(row.Attribute, row.Domain);
    EXPECT_EQ(graphRows, (std::set<std::pair<A, D>>{
                             {A::Position, D::GraphNode}, {A::Normal, D::GraphNode},
                             {A::Color, D::GraphNode}, {A::Color, D::GraphEdge},
                             {A::PointSize, D::GraphNode}, {A::LineWidth, D::GraphEdge}}));
    EXPECT_EQ(FindRow(model, A::LineWidth, D::GraphEdge)->ExpectedElementCount, 2u);

    const ECS::EntityHandle cloud = MakeSelectable(registry, "Cloud");
    AddPointCloudSource(registry, cloud, 2u);
    SetPositions(registry.Raw().get<GS::Vertices>(cloud), {{0, 0, 0}, {1, 0, 0}});
    model = Runtime::BuildEditorAttributeBindingModel(
        registry, Runtime::SelectionController::ToStableEntityId(cloud));
    std::set<std::pair<A, D>> cloudRows;
    for (const auto& row : model.Rows)
        cloudRows.emplace(row.Attribute, row.Domain);
    EXPECT_EQ(cloudRows, (std::set<std::pair<A, D>>{
                             {A::Position, D::PointCloudPoint}, {A::Normal, D::PointCloudPoint},
                             {A::Color, D::PointCloudPoint}, {A::PointSize, D::PointCloudPoint}}));

    EXPECT_FALSE(Runtime::BuildEditorAttributeBindingModel(registry, 0u).HasEntity);
}
