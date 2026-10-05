#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <functional>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <gtest/gtest.h>
#include <entt/entity/registry.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

import Extrinsic.Core.Base64;
import Extrinsic.Core.Error;
import Extrinsic.Core.IOBackend;
import Extrinsic.ECS.Components.AssetInstance;
import Extrinsic.ECS.Component.Collider;
import Extrinsic.ECS.Component.Hierarchy;
import Extrinsic.ECS.Component.Light;
import Extrinsic.ECS.Component.MetaData;
import Extrinsic.ECS.Component.RigidBody;
import Extrinsic.ECS.Component.ShadowCaster;
import Extrinsic.ECS.Component.StableId;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Extrinsic.ECS.Components.Selection;
import Extrinsic.ECS.Hierarchy.Mutation;
import Extrinsic.ECS.Scene.Bootstrap;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.Colormap;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.MeshSurfaceTopology;
import Extrinsic.Runtime.SceneSerialization;
import Extrinsic.Runtime.VertexChannelBindings;
import Geometry.Properties;
import Geometry.Graph;

namespace Runtime = Extrinsic::Runtime;
namespace Core = Extrinsic::Core;
namespace ECS = Extrinsic::ECS;
namespace ECSC = Extrinsic::ECS::Components;
namespace AssetInstance = Extrinsic::ECS::Components::AssetInstance;
namespace Collider = Extrinsic::ECS::Components::Collider;
namespace Lights = Extrinsic::ECS::Components::Lights;
namespace RigidBody = Extrinsic::ECS::Components::RigidBody;
namespace Shadows = Extrinsic::ECS::Components::Shadows;
namespace GS = Extrinsic::ECS::Components::GeometrySources;
namespace Sel = Extrinsic::ECS::Components::Selection;
namespace G = Extrinsic::Graphics::Components;
namespace PN = Extrinsic::ECS::Components::GeometrySources::PropertyNames;

namespace
{
    constexpr std::uint32_t kInvalidIndex = 0xFFFFFFFFu;

    class MemoryIOBackend final : public Core::IO::IIOBackend
    {
    public:
        [[nodiscard]] Core::Expected<Core::IO::IOReadResult> Read(
            const Core::IO::IORequest& request) override
        {
            const auto it = Files.find(request.Path);
            if (it == Files.end())
                return Core::Err<Core::IO::IOReadResult>(Core::ErrorCode::FileNotFound);
            return Core::IO::IOReadResult{.Data = it->second};
        }

        [[nodiscard]] Core::Result Write(
            const Core::IO::IORequest& request,
            std::span<const std::byte> data) override
        {
            Files[request.Path] = std::vector<std::byte>(data.begin(), data.end());
            return Core::Result{};
        }

        [[nodiscard]] std::string Text(const std::string& path) const
        {
            const auto it = Files.find(path);
            if (it == Files.end())
                return {};
            std::string out;
            out.resize(it->second.size());
            if (!out.empty())
                std::memcpy(out.data(), it->second.data(), it->second.size());
            return out;
        }

        std::unordered_map<std::string, std::vector<std::byte>> Files{};
    };

    void SetPositions(GS::Vertices& vertices,
                      std::vector<glm::vec3> positions)
    {
        vertices.Properties.Resize(positions.size());
        auto property = vertices.Properties.GetOrAdd<glm::vec3>(
            std::string{PN::kPosition},
            glm::vec3{0.0f});
        property.Vector() = std::move(positions);
    }

    void SetTexcoords(GS::Vertices& vertices,
                      std::vector<glm::vec2> texcoords)
    {
        auto property = vertices.Properties.GetOrAdd<glm::vec2>(
            "v:texcoord",
            glm::vec2{0.0f});
        property.Vector() = std::move(texcoords);
    }

    void SetEdges(GS::Edges& edges,
                  std::vector<std::uint32_t> v0,
                  std::vector<std::uint32_t> v1)
    {
        edges.Properties.Resize(v0.size());
        auto p0 = edges.Properties.GetOrAdd<std::uint32_t>(
            std::string{PN::kEdgeV0},
            0u);
        auto p1 = edges.Properties.GetOrAdd<std::uint32_t>(
            std::string{PN::kEdgeV1},
            0u);
        p0.Vector() = std::move(v0);
        p1.Vector() = std::move(v1);
    }

    void SetHalfedges(GS::Halfedges& halfedges,
                      std::vector<std::uint32_t> toVertex,
                      std::vector<std::uint32_t> next,
                      std::vector<std::uint32_t> face)
    {
        halfedges.Properties.Resize(toVertex.size());
        auto to = halfedges.Properties.GetOrAdd<std::uint32_t>(
            std::string{PN::kHalfedgeToVertex},
            kInvalidIndex);
        auto nx = halfedges.Properties.GetOrAdd<std::uint32_t>(
            std::string{PN::kHalfedgeNext},
            kInvalidIndex);
        auto fa = halfedges.Properties.GetOrAdd<std::uint32_t>(
            std::string{PN::kHalfedgeFace},
            kInvalidIndex);
        to.Vector() = std::move(toVertex);
        nx.Vector() = std::move(next);
        fa.Vector() = std::move(face);
    }

    void SetFaces(GS::Faces& faces, std::vector<std::uint32_t> faceHalfedge)
    {
        faces.Properties.Resize(faceHalfedge.size());
        auto halfedge = faces.Properties.GetOrAdd<std::uint32_t>(
            std::string{PN::kFaceHalfedge},
            kInvalidIndex);
        halfedge.Vector() = std::move(faceHalfedge);
    }

    ECS::EntityHandle AddMeshEntity(ECS::Scene::Registry& scene)
    {
        ECS::EntityHandle entity = ECS::Scene::CreateDefault(scene, "Mesh Entity");
        auto& raw = scene.Raw();
        raw.emplace<ECSC::StableId>(entity, ECSC::StableId{11u, 22u});
        raw.emplace<Sel::SelectableTag>(entity);
        auto& transform = raw.get<ECSC::Transform::Component>(entity);
        transform.Position = glm::vec3{1.0f, 2.0f, 3.0f};
        transform.Rotation = glm::quat{0.5f, 0.5f, 0.5f, 0.5f};
        transform.Scale = glm::vec3{2.0f, 3.0f, 4.0f};

        auto& vertices = raw.emplace<GS::Vertices>(entity);
        SetPositions(vertices,
                     {
                         {0.0f, 0.0f, 0.0f},
                         {1.0f, 0.0f, 0.0f},
                         {0.0f, 1.0f, 0.0f},
                     });
        SetTexcoords(vertices,
                     {
                         {0.0f, 0.0f},
                         {1.0f, 0.0f},
                         {0.0f, 1.0f},
                     });
        auto& edges = raw.emplace<GS::Edges>(entity);
        SetEdges(edges, {0u, 1u, 2u}, {1u, 2u, 0u});
        auto& halfedges = raw.emplace<GS::Halfedges>(entity);
        SetHalfedges(halfedges,
                     {1u, 2u, 0u, 0u, 2u, 1u},
                     {1u, 2u, 0u, 5u, 3u, 4u},
                     {0u, 0u, 0u, kInvalidIndex, kInvalidIndex, kInvalidIndex});
        auto& faces = raw.emplace<GS::Faces>(entity);
        SetFaces(faces, {0u});
        raw.emplace<GS::HasMeshTopology>(entity);

        G::RenderSurface surface{};
        surface.Domain = G::RenderSurface::SourceDomain::Face;
        raw.emplace<G::RenderSurface>(entity, surface);
        G::RenderEdges renderEdges{};
        renderEdges.Domain = G::RenderEdges::SourceDomain::Edge;
        renderEdges.WidthSource = 2.5f;
        raw.emplace<G::RenderEdges>(entity, renderEdges);

        G::VisualizationConfig visualization{};
        visualization.Source = G::VisualizationConfig::ColorSource::ScalarField;
        visualization.Color = glm::vec4{0.25f, 0.5f, 0.75f, 1.0f};
        visualization.ScalarFieldName = "curvature";
        visualization.Scalar.Map = Extrinsic::Graphics::Colormap::Type::Plasma;
        visualization.Scalar.AutoRange = false;
        visualization.Scalar.RangeMin = -1.0f;
        visualization.Scalar.RangeMax = 2.0f;
        visualization.Scalar.BinCount = 4u;
        visualization.Scalar.Isolines.Num = 8u;
        visualization.Scalar.Isolines.Color = glm::vec4{0.1f, 0.2f, 0.3f, 1.0f};
        visualization.Scalar.Isolines.Width = 2.25f;
        visualization.Scalar.Isolines.Values[0] = -0.5f;
        visualization.Scalar.Isolines.Values[1] = 1.25f;
        visualization.Scalar.Isolines.ValueCount = 2u;
        visualization.ScalarDomain = G::VisualizationConfig::Domain::Face;
        visualization.ColorBufferName = "v:kmeans_color";
        G::VisualizationLaneOverrides overrides{};
        overrides.Surface = visualization;
        overrides.Surface->UseBakedTexture = true;
        overrides.Surface->Interpretation = decltype(visualization.Interpretation)::NormalDirection;
        overrides.Points = visualization;
        overrides.Points->Source = G::VisualizationConfig::ColorSource::UniformColor;
        overrides.Points->Color = glm::vec4{0.0f, 0.75f, 0.25f, 1.0f};
        raw.emplace<G::VisualizationLaneOverrides>(entity, std::move(overrides));
        raw.emplace<G::VisualizationConfig>(entity, std::move(visualization));
        return entity;
    }

    ECS::EntityHandle AddGraphEntity(ECS::Scene::Registry& scene)
    {
        ECS::EntityHandle entity = ECS::Scene::CreateDefault(scene, "Graph Entity");
        auto& raw = scene.Raw();
        raw.emplace<Sel::SelectableTag>(entity);
        Geometry::Graph::Graph graph{};
        const auto v0 = graph.AddVertex({0.0f, 0.0f, 0.0f});
        const auto v1 = graph.AddVertex({1.0f, 0.0f, 0.0f});
        const auto v2 = graph.AddVertex({2.0f, 0.0f, 0.0f});
        (void)graph.AddEdge(v0, v1);
        (void)graph.AddEdge(v1, v2);
        GS::PopulateFromGraph(raw, entity, graph);
        raw.emplace<G::RenderEdges>(entity);
        G::RenderPoints points{};
        points.Type = G::RenderPoints::RenderType::Flat;
        points.SizeSource = std::string{"node:radius"};
        raw.emplace<G::RenderPoints>(entity, std::move(points));
        return entity;
    }

    ECS::EntityHandle AddPointCloudEntity(ECS::Scene::Registry& scene)
    {
        ECS::EntityHandle entity = ECS::Scene::CreateDefault(scene, "Cloud Entity");
        auto& raw = scene.Raw();
        auto& vertices = raw.emplace<GS::Vertices>(entity);
        SetPositions(vertices,
                     {
                         {-1.0f, 0.0f, 0.0f},
                         {-2.0f, 0.5f, 0.0f},
                     });
        G::RenderPoints points{};
        points.Type = G::RenderPoints::RenderType::Surfel;
        points.SizeSource = 0.125f;
        raw.emplace<G::RenderPoints>(entity, points);
        return entity;
    }

    ECS::EntityHandle FindEntityByName(const ECS::Scene::Registry& scene,
                                       const std::string& name)
    {
        const entt::registry& raw = scene.Raw();
        const auto view = raw.view<const ECSC::MetaData>();
        for (const auto [entity, meta] : view.each())
        {
            if (meta.EntityName == name)
                return entity;
        }
        return ECS::InvalidEntityHandle;
    }
}

TEST(RuntimeSceneSerialization, SaveLoadRoundTripPreservesPromotedSandboxSceneData)
{
    ECS::Scene::Registry source;
    const ECS::EntityHandle mesh = AddMeshEntity(source);
    const ECS::EntityHandle graph = AddGraphEntity(source);
    const ECS::EntityHandle cloud = AddPointCloudEntity(source);
    ECS::Hierarchy::Attach(source.Raw(), graph, mesh);
    ECS::Hierarchy::Attach(source.Raw(), cloud, mesh);

    MemoryIOBackend backend;
    auto saved = Runtime::SaveSceneDocument(source, "scene.json", backend);
    ASSERT_TRUE(saved.has_value()) << static_cast<int>(saved.error());
    EXPECT_EQ(saved->Stats.Entities, 3u);
    EXPECT_EQ(saved->Stats.MeshEntities, 1u);
    EXPECT_EQ(saved->Stats.GraphEntities, 1u);
    EXPECT_EQ(saved->Stats.PointCloudEntities, 1u);
    EXPECT_EQ(saved->Stats.HierarchyLinks, 2u);

    const std::string document = backend.Text("scene.json");
    ASSERT_FALSE(document.empty());
    const nlohmann::json parsed = nlohmann::json::parse(document);
    ASSERT_EQ(parsed["version"].get<std::uint32_t>(), 5u);
    ASSERT_EQ(parsed["entities"].size(), 3u);
    EXPECT_EQ(parsed["stats"]["renderHintEntities"].get<std::uint32_t>(), 3u);
    ASSERT_TRUE(parsed["entities"][0]["render"]["visualization"].is_object());
    EXPECT_EQ(parsed["entities"][0]["render"]["visualization"]["source"].get<std::string>(),
              "ScalarField");
    ASSERT_TRUE(parsed["entities"][0]["geometrySources"]["vertices"]["texcoords"].is_array());
    EXPECT_EQ(parsed["entities"][0]["geometrySources"]["vertices"]["texcoords"].size(), 3u);
    ASSERT_TRUE(parsed["entities"][1]["geometrySources"]["halfedges"]["toVertex"].is_array());
    EXPECT_EQ(parsed["entities"][1]["geometrySources"]["halfedges"]["toVertex"].size(), 4u);
    EXPECT_FALSE(parsed["entities"][1]["geometrySources"]["halfedges"].contains("face"));

    ECS::Scene::Registry loaded;
    auto loadedResult = Runtime::LoadSceneDocument(loaded, "scene.json", backend);
    ASSERT_TRUE(loadedResult.has_value()) << static_cast<int>(loadedResult.error());
    EXPECT_EQ(loadedResult->Stats.Entities, 3u);
    EXPECT_EQ(loadedResult->Stats.HierarchyLinks, 2u);

    const ECS::EntityHandle loadedMesh = FindEntityByName(loaded, "Mesh Entity");
    const ECS::EntityHandle loadedGraph = FindEntityByName(loaded, "Graph Entity");
    const ECS::EntityHandle loadedCloud = FindEntityByName(loaded, "Cloud Entity");
    ASSERT_NE(loadedMesh, ECS::InvalidEntityHandle);
    ASSERT_NE(loadedGraph, ECS::InvalidEntityHandle);
    ASSERT_NE(loadedCloud, ECS::InvalidEntityHandle);

    const entt::registry& raw = loaded.Raw();
    ASSERT_TRUE(raw.all_of<ECSC::StableId>(loadedMesh));
    EXPECT_EQ(raw.get<ECSC::StableId>(loadedMesh).High, 11u);
    EXPECT_EQ(raw.get<ECSC::StableId>(loadedMesh).Low, 22u);
    EXPECT_TRUE(raw.all_of<Sel::SelectableTag>(loadedMesh));
    EXPECT_TRUE(raw.all_of<Sel::SelectableTag>(loadedGraph));
    EXPECT_FALSE(raw.all_of<Sel::SelectableTag>(loadedCloud));

    const auto& transform = raw.get<ECSC::Transform::Component>(loadedMesh);
    EXPECT_FLOAT_EQ(transform.Position.x, 1.0f);
    EXPECT_FLOAT_EQ(transform.Position.y, 2.0f);
    EXPECT_FLOAT_EQ(transform.Position.z, 3.0f);
    EXPECT_FLOAT_EQ(transform.Rotation.w, 0.5f);
    EXPECT_FLOAT_EQ(transform.Scale.z, 4.0f);

    ASSERT_TRUE(raw.all_of<ECSC::Hierarchy::Component>(loadedGraph));
    EXPECT_EQ(raw.get<ECSC::Hierarchy::Component>(loadedGraph).Parent, loadedMesh);
    EXPECT_EQ(raw.get<ECSC::Hierarchy::Component>(loadedCloud).Parent, loadedMesh);

    const GS::ConstSourceView meshView = GS::BuildConstView(raw, loadedMesh);
    ASSERT_EQ(meshView.ActiveDomain, GS::Domain::Mesh);
    ASSERT_NE(meshView.VertexSource, nullptr);
    ASSERT_NE(meshView.EdgeSource, nullptr);
    ASSERT_NE(meshView.HalfedgeSource, nullptr);
    ASSERT_NE(meshView.FaceSource, nullptr);
    const auto meshPositions = meshView.VertexSource->Properties.Get<glm::vec3>(PN::kPosition);
    ASSERT_TRUE(meshPositions.IsValid());
    ASSERT_EQ(meshPositions.Vector().size(), 3u);
    EXPECT_FLOAT_EQ(meshPositions.Vector()[1].x, 1.0f);
    const auto meshTexcoords = meshView.VertexSource->Properties.Get<glm::vec2>("v:texcoord");
    ASSERT_TRUE(meshTexcoords.IsValid());
    ASSERT_EQ(meshTexcoords.Vector().size(), 3u);
    EXPECT_EQ(meshTexcoords.Vector()[0], glm::vec2(0.0f, 0.0f));
    EXPECT_EQ(meshTexcoords.Vector()[1], glm::vec2(1.0f, 0.0f));
    EXPECT_EQ(meshTexcoords.Vector()[2], glm::vec2(0.0f, 1.0f));
    EXPECT_EQ(meshView.FaceSource->Properties.Get<std::uint32_t>(PN::kFaceHalfedge).Vector()[0], 0u);

    const auto& surface = raw.get<G::RenderSurface>(loadedMesh);
    EXPECT_EQ(surface.Domain, G::RenderSurface::SourceDomain::Face);
    const auto& renderEdges = raw.get<G::RenderEdges>(loadedMesh);
    EXPECT_EQ(renderEdges.Domain, G::RenderEdges::SourceDomain::Edge);
    ASSERT_NE(std::get_if<float>(&renderEdges.WidthSource), nullptr);
    EXPECT_FLOAT_EQ(*std::get_if<float>(&renderEdges.WidthSource), 2.5f);
    ASSERT_TRUE(raw.all_of<G::VisualizationConfig>(loadedMesh));
    const auto& visualization = raw.get<G::VisualizationConfig>(loadedMesh);
    EXPECT_EQ(visualization.Source, G::VisualizationConfig::ColorSource::ScalarField);
    EXPECT_EQ(visualization.Color, glm::vec4(0.25f, 0.5f, 0.75f, 1.0f));
    EXPECT_EQ(visualization.ScalarFieldName, "curvature");
    EXPECT_EQ(visualization.Scalar.Map, Extrinsic::Graphics::Colormap::Type::Plasma);
    EXPECT_FALSE(visualization.Scalar.AutoRange);
    EXPECT_FLOAT_EQ(visualization.Scalar.RangeMin, -1.0f);
    EXPECT_FLOAT_EQ(visualization.Scalar.RangeMax, 2.0f);
    EXPECT_EQ(visualization.Scalar.BinCount, 4u);
    EXPECT_EQ(visualization.Scalar.Isolines.Num, 8u);
    EXPECT_EQ(visualization.Scalar.Isolines.Color, glm::vec4(0.1f, 0.2f, 0.3f, 1.0f));
    EXPECT_FLOAT_EQ(visualization.Scalar.Isolines.Width, 2.25f);
    EXPECT_EQ(visualization.Scalar.Isolines.ValueCount, 2u);
    EXPECT_FLOAT_EQ(visualization.Scalar.Isolines.Values[0], -0.5f);
    EXPECT_FLOAT_EQ(visualization.Scalar.Isolines.Values[1], 1.25f);
    EXPECT_EQ(visualization.ScalarDomain, G::VisualizationConfig::Domain::Face);
    EXPECT_EQ(visualization.ColorBufferName, "v:kmeans_color");
    ASSERT_TRUE(raw.all_of<G::VisualizationLaneOverrides>(loadedMesh));
    const auto& overrides = raw.get<G::VisualizationLaneOverrides>(loadedMesh);
    ASSERT_TRUE(overrides.Points.has_value());
    EXPECT_EQ(overrides.Points->Source,
              G::VisualizationConfig::ColorSource::UniformColor);
    EXPECT_EQ(overrides.Points->Color,
              glm::vec4(0.0f, 0.75f, 0.25f, 1.0f));
    ASSERT_TRUE(overrides.Surface.has_value());
    EXPECT_TRUE(overrides.Surface->UseBakedTexture);
    EXPECT_EQ(overrides.Surface->Interpretation, decltype(visualization.Interpretation)::NormalDirection);
    EXPECT_FALSE(overrides.Points->UseBakedTexture);
    EXPECT_FALSE(overrides.Edges.has_value());

    const GS::ConstSourceView graphView = GS::BuildConstView(raw, loadedGraph);
    ASSERT_EQ(graphView.ActiveDomain, GS::Domain::Graph);
    ASSERT_NE(graphView.VertexSource, nullptr);
    ASSERT_NE(graphView.HalfedgeSource, nullptr);
    EXPECT_EQ(graphView.VertexSource->Properties.Get<glm::vec3>(PN::kPosition).Vector().size(), 3u);
    EXPECT_EQ(graphView.HalfedgeSource->Properties
                  .Get<Geometry::Graph::HalfedgeConnectivity>(PN::kHalfedgeConnectivity)
                  .Vector()
                  .size(),
              4u);
    const auto& graphPoints = raw.get<G::RenderPoints>(loadedGraph);
    ASSERT_NE(std::get_if<std::string>(&graphPoints.SizeSource), nullptr);
    EXPECT_EQ(*std::get_if<std::string>(&graphPoints.SizeSource), "node:radius");

    const GS::ConstSourceView cloudView = GS::BuildConstView(raw, loadedCloud);
    ASSERT_EQ(cloudView.ActiveDomain, GS::Domain::PointCloud);
    ASSERT_NE(cloudView.VertexSource, nullptr);
    EXPECT_EQ(cloudView.VertexSource->Properties.Get<glm::vec3>(PN::kPosition).Vector().size(), 2u);
}

TEST(RuntimeSceneSerialization, UnsupportedPersistenceFamiliesReportDiagnosticsAndDropOnLoad)
{
    ECS::Scene::Registry source;
    const ECS::EntityHandle entity = ECS::Scene::CreateDefault(source, "Unsupported Families");
    auto& raw = source.Raw();
    raw.emplace<Lights::PointLight>(entity);
    raw.emplace<Shadows::CasterTag>(entity);
    raw.emplace<Collider::Component>(
        entity,
        Collider::Component{{Collider::MakeSphere(0.5f)}, true});
    raw.emplace<RigidBody::Component>(entity, RigidBody::MakeDynamic(1.0f));
    raw.emplace<AssetInstance::Source>(entity, AssetInstance::Source{.AssetId = 42u});

    MemoryIOBackend backend;
    auto saved = Runtime::SaveSceneDocument(source, "unsupported.json", backend);
    ASSERT_TRUE(saved.has_value()) << static_cast<int>(saved.error());
    EXPECT_EQ(saved->Stats.Entities, 1u);
    EXPECT_EQ(saved->Stats.UnsupportedPersistenceEntities, 1u);
    EXPECT_EQ(saved->Stats.UnsupportedLightEntities, 1u);
    EXPECT_EQ(saved->Stats.UnsupportedShadowEntities, 1u);
    EXPECT_EQ(saved->Stats.UnsupportedPhysicsEntities, 1u);
    EXPECT_EQ(saved->Stats.UnsupportedAssetInstanceEntities, 1u);

    const nlohmann::json parsed = nlohmann::json::parse(backend.Text("unsupported.json"));
    EXPECT_EQ(parsed["stats"]["unsupportedPersistenceEntities"].get<std::uint32_t>(), 1u);
    EXPECT_EQ(parsed["stats"]["unsupportedLightEntities"].get<std::uint32_t>(), 1u);
    EXPECT_EQ(parsed["stats"]["unsupportedShadowEntities"].get<std::uint32_t>(), 1u);
    EXPECT_EQ(parsed["stats"]["unsupportedPhysicsEntities"].get<std::uint32_t>(), 1u);
    EXPECT_EQ(parsed["stats"]["unsupportedAssetInstanceEntities"].get<std::uint32_t>(), 1u);

    ECS::Scene::Registry loaded;
    auto loadedResult = Runtime::LoadSceneDocument(loaded, "unsupported.json", backend);
    ASSERT_TRUE(loadedResult.has_value()) << static_cast<int>(loadedResult.error());
    const ECS::EntityHandle loadedEntity = FindEntityByName(loaded, "Unsupported Families");
    ASSERT_NE(loadedEntity, ECS::InvalidEntityHandle);
    const auto& loadedRaw = loaded.Raw();
    EXPECT_FALSE(loadedRaw.any_of<Lights::PointLight>(loadedEntity));
    EXPECT_FALSE(loadedRaw.any_of<Shadows::CasterTag>(loadedEntity));
    EXPECT_FALSE(loadedRaw.any_of<Collider::Component>(loadedEntity));
    EXPECT_FALSE(loadedRaw.any_of<RigidBody::Component>(loadedEntity));
    EXPECT_FALSE(loadedRaw.any_of<AssetInstance::Source>(loadedEntity));
}

TEST(RuntimeSceneSerialization, InvalidDocumentsFailClosed)
{
    ECS::Scene::Registry scene;
    auto invalidJson = Runtime::DeserializeSceneDocument(scene, "not json");
    EXPECT_FALSE(invalidJson.has_value());
    EXPECT_EQ(invalidJson.error(), Core::ErrorCode::InvalidFormat);

    auto unsupportedVersion = Runtime::DeserializeSceneDocument(
        scene,
        R"({"version":1,"entities":[]})");
    EXPECT_FALSE(unsupportedVersion.has_value());
    EXPECT_EQ(unsupportedVersion.error(), Core::ErrorCode::InvalidFormat);

    auto previousVersion = Runtime::DeserializeSceneDocument(
        scene,
        R"({"version":2,"entities":[]})");
    EXPECT_FALSE(previousVersion.has_value())
        << "version 2 may carry retired presentation color slots (RUNTIME-318)";
    EXPECT_EQ(previousVersion.error(), Core::ErrorCode::InvalidFormat);

    auto version3 = Runtime::DeserializeSceneDocument(
        scene,
        R"({"version":3,"entities":[]})");
    EXPECT_FALSE(version3.has_value()) << "version 3 predates the property tables (RUNTIME-319)";
    EXPECT_EQ(version3.error(), Core::ErrorCode::InvalidFormat);

    auto version4 = Runtime::DeserializeSceneDocument(
        scene,
        R"({"version":4,"entities":[]})");
    EXPECT_FALSE(version4.has_value())
        << "version 4 may carry legacy property-reference keys (REVIEW-007 PK12)";
    EXPECT_EQ(version4.error(), Core::ErrorCode::InvalidFormat);

    auto badGeometry = Runtime::DeserializeSceneDocument(
        scene,
        R"({"version":5,"entities":[{"id":0,"geometrySources":{"domain":"Mesh"}}]})");
    EXPECT_FALSE(badGeometry.has_value());
    EXPECT_EQ(badGeometry.error(), Core::ErrorCode::InvalidFormat);
}

TEST(RuntimeSceneSerialization, MalformedGraphTopologyFailsClosed)
{
    const nlohmann::json valid = nlohmann::json::parse(
        R"({"version":5,"entities":[{"id":0,"geometrySources":{"domain":"Graph","nodes":{"deleted":0,"positions":[[0,0,0],[1,0,0]]},"halfedges":{"toVertex":[1,0],"next":[1,0],"prev":[1,0]},"edges":{"deleted":0,"v0":[0],"v1":[1]}}}]})");

    {
        ECS::Scene::Registry scene;
        EXPECT_TRUE(Runtime::DeserializeSceneDocument(scene, valid.dump()).has_value());
    }

    const auto expectInvalid = [](nlohmann::json document)
    {
        ECS::Scene::Registry scene;
        const auto result =
            Runtime::DeserializeSceneDocument(scene, document.dump());
        EXPECT_FALSE(result.has_value());
        if (!result.has_value())
            EXPECT_EQ(result.error(), Core::ErrorCode::InvalidFormat);
    };

    nlohmann::json outOfRangeVertex = valid;
    outOfRangeVertex["entities"][0]["geometrySources"]["halfedges"]
                    ["toVertex"][0] = 2u;
    expectInvalid(std::move(outOfRangeVertex));

    nlohmann::json outOfRangeNext = valid;
    outOfRangeNext["entities"][0]["geometrySources"]["halfedges"]
                  ["next"][0] = 2u;
    expectInvalid(std::move(outOfRangeNext));

    nlohmann::json inconsistentPrev = valid;
    inconsistentPrev["entities"][0]["geometrySources"]["halfedges"]
                    ["prev"] = {0u, 1u};
    expectInvalid(std::move(inconsistentPrev));

    nlohmann::json disconnectedSuccessors = valid;
    disconnectedSuccessors["entities"][0]["geometrySources"]["halfedges"]
                          ["next"] = {0u, 1u};
    disconnectedSuccessors["entities"][0]["geometrySources"]["halfedges"]
                          ["prev"] = {0u, 1u};
    expectInvalid(std::move(disconnectedSuccessors));

    nlohmann::json wrongHalfedgeCount = valid;
    auto& wrongHalfedges =
        wrongHalfedgeCount["entities"][0]["geometrySources"]["halfedges"];
    wrongHalfedges["toVertex"] = {1u, 0u, 1u, 0u};
    wrongHalfedges["next"] = {1u, 0u, 3u, 2u};
    wrongHalfedges["prev"] = {1u, 0u, 3u, 2u};
    expectInvalid(std::move(wrongHalfedgeCount));

    nlohmann::json outOfRangeEndpoint = valid;
    outOfRangeEndpoint["entities"][0]["geometrySources"]["edges"]
                      ["v1"][0] = 2u;
    expectInvalid(std::move(outOfRangeEndpoint));

    nlohmann::json mismatchedEndpoints = valid;
    mismatchedEndpoints["entities"][0]["geometrySources"]["edges"]
                       ["v0"][0] = 1u;
    mismatchedEndpoints["entities"][0]["geometrySources"]["edges"]
                       ["v1"][0] = 0u;
    expectInvalid(std::move(mismatchedEndpoints));
}


// ============================================================================
// RUNTIME-192 Slice B2 — property value-kind wire-format compatibility.
//
// The in-memory vocabulary uses Geometry::PropertyValueKind, whose debug names
// are "Float"/"Double". The persisted scene format predates that and says
// "ScalarFloat"/"ScalarDouble". Canonical references represent an
// unconstrained kind as Unknown. Since version 5 the reader rejects the legacy
// reference keys propertyName/expectedValueKind (REVIEW-007 PK12).
// ============================================================================

namespace
{
    ECS::EntityHandle AddGeometryPresentationEntity(ECS::Scene::Registry& scene)
    {
        const ECS::EntityHandle entity = AddMeshEntity(scene);

        Runtime::GeometryPresentationSlotRecipe constrained{};
        constrained.Semantic = Runtime::GeometryPresentationSlotSemantic::ScalarField;
        constrained.SourceKind = Runtime::GeometryPresentationSourceKind::PropertyBuffer;
        constrained.UniformDefault.Kind = Geometry::PropertyValueKind::Float;
        constrained.Property = Runtime::GeometryPropertyRef{
            .Domain = Runtime::GeometryElementDomain::MeshVertex,
            .Name = "v:quality",
            .ValueKind = Geometry::PropertyValueKind::Float,
        };

        Runtime::GeometryPresentationSlotRecipe unconstrained{};
        unconstrained.Semantic = Runtime::GeometryPresentationSlotSemantic::Albedo;
        unconstrained.SourceKind = Runtime::GeometryPresentationSourceKind::PropertyBuffer;
        unconstrained.UniformDefault.Kind = Geometry::PropertyValueKind::Double;
        unconstrained.Property = Runtime::GeometryPropertyRef{
            .Domain = Runtime::GeometryElementDomain::MeshVertex,
            .Name = "v:color",
            .ValueKind = Geometry::PropertyValueKind::Unknown,
        };

        Runtime::GeometryPresentationRecipe bindings{};
        bindings.Shape = Runtime::GeometryPresentationShape::Mesh;
        bindings.Presentations.push_back(Runtime::GeometryPresentationBindingRecipe{
            .Key = "mesh.surface",
            .Kind = Runtime::GeometryPresentationKind::SurfaceMaterial,
            .Slots = {constrained, unconstrained},
        });
        bindings.Lanes.push_back(Runtime::GeometryPresentationLaneRecipe{
            .Lane = Runtime::GeometryRenderLane::Surface,
            .PresentationKey = "mesh.surface",
        });

        scene.Raw().emplace<Runtime::GeometryPresentationRecipe>(
            entity, std::move(bindings));
        return entity;
    }

    [[nodiscard]] const Runtime::GeometryPresentationSlotRecipe* FindSlot(
        const Runtime::GeometryPresentationRecipe& bindings,
        const Runtime::GeometryPresentationSlotSemantic semantic) noexcept
    {
        for (const auto& presentation : bindings.Presentations)
        {
            for (const auto& slot : presentation.Slots)
            {
                if (slot.Semantic == semantic)
                    return &slot;
            }
        }
        return nullptr;
    }
}

TEST(RuntimeSceneSerialization, PropertyValueKindKeepsLegacyWireStrings)
{
    ECS::Scene::Registry source;
    (void)AddGeometryPresentationEntity(source);

    MemoryIOBackend backend;
    const auto saved = Runtime::SaveSceneDocument(source, "presentation.json", backend);
    ASSERT_TRUE(saved.has_value()) << static_cast<int>(saved.error());

    const std::string text = backend.Text("presentation.json");

    // The canonical debug names are "Float"/"Double"; the wire must NOT use
    // them, or documents written by older builds stop loading.
    EXPECT_NE(text.find("\"ScalarFloat\""), std::string::npos)
        << "float expectation must persist as ScalarFloat";
    EXPECT_NE(text.find("\"ScalarDouble\""), std::string::npos)
        << "double default must persist as ScalarDouble";
    EXPECT_NE(text.find("\"Unknown\""), std::string::npos)
        << "unconstrained references must persist as Unknown";
    EXPECT_EQ(text.find("\"expectedValueKind\""), std::string::npos);
    EXPECT_EQ(text.find("\"propertyName\""), std::string::npos);
    EXPECT_EQ(text.find("\"kind\":\"Double\""), std::string::npos);
}

namespace
{
    // Applies `mutate` to every saved property reference named `name`.
    void MutatePropertyRefs(nlohmann::json& node, const std::string_view name,
                            const std::function<void(nlohmann::json&)>& mutate)
    {
        if (node.is_object())
        {
            if (node.contains("domain") && node.contains("name") &&
                node["name"].is_string() && node["name"].get<std::string>() == name)
            {
                mutate(node);
            }
            for (auto& [key, child] : node.items())
                MutatePropertyRefs(child, name, mutate);
        }
        else if (node.is_array())
        {
            for (auto& child : node)
                MutatePropertyRefs(child, name, mutate);
        }
    }
}

TEST(RuntimeSceneSerialization, LegacyPropertyRefKeysFailClosed)
{
    ECS::Scene::Registry source;
    (void)AddGeometryPresentationEntity(source);
    MemoryIOBackend backend;
    ASSERT_TRUE(
        Runtime::SaveSceneDocument(source, "presentation.json", backend).has_value());
    const nlohmann::json saved = nlohmann::json::parse(backend.Text("presentation.json"));

    const std::vector<std::pair<std::string, std::function<void(nlohmann::json&)>>> cases{
        {"propertyName only", [](nlohmann::json& ref)
         {
             ref["propertyName"] = ref["name"];
             ref.erase("name");
         }},
        {"expectedValueKind only", [](nlohmann::json& ref)
         {
             ref.erase("valueKind");
             ref["expectedValueKind"] = "ScalarFloat";
         }},
        {"expectedValueKind Any", [](nlohmann::json& ref)
         {
             ref.erase("valueKind");
             ref["expectedValueKind"] = "Any";
         }},
        {"legacy keys beside canonical keys", [](nlohmann::json& ref)
         {
             ref["propertyName"] = ref["name"];
             ref["expectedValueKind"] = "ScalarFloat";
         }},
    };
    for (const auto& [label, mutate] : cases)
    {
        nlohmann::json document = saved;
        MutatePropertyRefs(document, "v:quality", mutate);
        ASSERT_NE(document.dump(), saved.dump()) << label;

        ECS::Scene::Registry loaded;
        const auto result = Runtime::DeserializeSceneDocument(loaded, document.dump());
        EXPECT_FALSE(result.has_value()) << label;
        if (!result.has_value())
            EXPECT_EQ(result.error(), Core::ErrorCode::InvalidFormat) << label;
    }

    // Omitting valueKind stays valid and loads as an unconstrained reference.
    nlohmann::json document = saved;
    MutatePropertyRefs(document, "v:quality", [](nlohmann::json& ref) { ref.erase("valueKind"); });
    ECS::Scene::Registry loaded;
    ASSERT_TRUE(Runtime::DeserializeSceneDocument(loaded, document.dump()).has_value());
    const ECS::EntityHandle entity = FindEntityByName(loaded, "Mesh Entity");
    ASSERT_NE(entity, ECS::InvalidEntityHandle);
    const auto* bindings = loaded.Raw().try_get<Runtime::GeometryPresentationRecipe>(entity);
    ASSERT_NE(bindings, nullptr);
    const auto* slot = FindSlot(*bindings, Runtime::GeometryPresentationSlotSemantic::ScalarField);
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->Property.ValueKind, Geometry::PropertyValueKind::Unknown);
}

TEST(RuntimeSceneSerialization, PropertyValueKindRoundTripsThroughLegacyWire)
{
    ECS::Scene::Registry source;
    (void)AddGeometryPresentationEntity(source);

    MemoryIOBackend backend;
    ASSERT_TRUE(
        Runtime::SaveSceneDocument(source, "presentation.json", backend).has_value());

    ECS::Scene::Registry loaded;
    const auto result =
        Runtime::LoadSceneDocument(loaded, "presentation.json", backend);
    ASSERT_TRUE(result.has_value()) << static_cast<int>(result.error());

    const ECS::EntityHandle entity = FindEntityByName(loaded, "Mesh Entity");
    ASSERT_NE(entity, ECS::InvalidEntityHandle);
    const auto* bindings =
        loaded.Raw().try_get<Runtime::GeometryPresentationRecipe>(entity);
    ASSERT_NE(bindings, nullptr);

    const auto* constrained =
        FindSlot(*bindings, Runtime::GeometryPresentationSlotSemantic::ScalarField);
    ASSERT_NE(constrained, nullptr);
    EXPECT_EQ(constrained->Property.ValueKind,
              Geometry::PropertyValueKind::Float);
    EXPECT_EQ(constrained->UniformDefault.Kind, Geometry::PropertyValueKind::Float);

    // Unknown must come back as an unconstrained reference kind.
    const auto* unconstrained =
        FindSlot(*bindings, Runtime::GeometryPresentationSlotSemantic::Albedo);
    ASSERT_NE(unconstrained, nullptr);
    EXPECT_EQ(unconstrained->Property.ValueKind,
              Geometry::PropertyValueKind::Unknown);
    EXPECT_EQ(unconstrained->UniformDefault.Kind,
              Geometry::PropertyValueKind::Double);
}

TEST(RuntimeSceneSerialization,
     GeometryPresentationRuntimeStateIsNotSerialized)
{
    ECS::Scene::Registry source;
    const ECS::EntityHandle sourceEntity =
        AddGeometryPresentationEntity(source);
    source.Raw().emplace<Runtime::GeometryPresentationRuntimeState>(
        sourceEntity,
        Runtime::GeometryPresentationRuntimeState{
            .RecipeGeneration = 91u,
            .Slots = {
                Runtime::GeometryPresentationSlotStatus{
                    .PresentationKey = "mesh.surface",
                    .Semantic = Runtime::GeometryPresentationSlotSemantic::ScalarField,
                    .Readiness = Runtime::GeometryPresentationReadiness::Ready,
                    .GeneratedTexture = Extrinsic::Assets::AssetId{77u, 3u},
                    .Provenance = Runtime::GeometryPresentationProvenance::GeneratedTextureAsset,
                    .SourceGeneration = 41u,
                    .OutputGeneration = 42u,
                    .Diagnostic = "runtime-only-presentation-diagnostic",
                },
            },
        });

    MemoryIOBackend backend;
    ASSERT_TRUE(
        Runtime::SaveSceneDocument(source, "presentation.json", backend)
            .has_value());
    const std::string document = backend.Text("presentation.json");

    EXPECT_NE(document.find("\"geometryPresentation\""),
              std::string::npos);
    EXPECT_EQ(document.find("runtime-only-presentation-diagnostic"),
              std::string::npos);
    EXPECT_EQ(document.find("\"recipeGeneration\""),
              std::string::npos);
    EXPECT_EQ(document.find("\"sourceGeneration\""),
              std::string::npos);
    EXPECT_EQ(document.find("\"outputGeneration\""),
              std::string::npos);
    EXPECT_EQ(document.find("\"generatedTexture\""),
              std::string::npos);

    ECS::Scene::Registry loaded;
    const auto loadedResult = Runtime::LoadSceneDocument(
        loaded,
        "presentation.json",
        backend);
    ASSERT_TRUE(loadedResult.has_value())
        << static_cast<int>(loadedResult.error());
    const ECS::EntityHandle loadedEntity =
        FindEntityByName(loaded, "Mesh Entity");
    ASSERT_NE(loadedEntity, ECS::InvalidEntityHandle);
    const auto* loadedState = loaded.Raw().try_get<
        Runtime::GeometryPresentationRuntimeState>(loadedEntity);
    ASSERT_NE(loadedState, nullptr);
    EXPECT_EQ(loadedState->RecipeGeneration, 1u);
    EXPECT_TRUE(loadedState->Slots.empty());
}

TEST(RuntimeSceneSerialization,
     LegacyProgressiveRenderDataKeyLoadsAsGeometryPresentationRecipe)
{
    ECS::Scene::Registry source;
    (void)AddGeometryPresentationEntity(source);

    MemoryIOBackend backend;
    ASSERT_TRUE(
        Runtime::SaveSceneDocument(source, "presentation.json", backend)
            .has_value());
    std::string document = backend.Text("presentation.json");
    const std::string canonicalKey = "\"geometryPresentation\"";
    const std::string legacyKey = "\"progressiveRenderData\"";
    const std::size_t keyOffset = document.find(canonicalKey);
    ASSERT_NE(keyOffset, std::string::npos);
    document.replace(keyOffset, canonicalKey.size(), legacyKey);

    ECS::Scene::Registry loaded;
    const auto result = Runtime::DeserializeSceneDocument(loaded, document);
    ASSERT_TRUE(result.has_value()) << static_cast<int>(result.error());
    const ECS::EntityHandle entity =
        FindEntityByName(loaded, "Mesh Entity");
    ASSERT_NE(entity, ECS::InvalidEntityHandle);
    const auto* recipe =
        loaded.Raw().try_get<Runtime::GeometryPresentationRecipe>(entity);
    ASSERT_NE(recipe, nullptr);
    EXPECT_EQ(recipe->Shape, Runtime::GeometryPresentationShape::Mesh);
    ASSERT_EQ(recipe->Presentations.size(), 1u);
    EXPECT_EQ(recipe->Presentations.front().Key, "mesh.surface");

    const auto* state = loaded.Raw().try_get<
        Runtime::GeometryPresentationRuntimeState>(entity);
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->RecipeGeneration, 1u);
    EXPECT_TRUE(state->Slots.empty());
}

TEST(RuntimeSceneSerialization, VectorFieldLayersRoundTripAcrossDomains)
{
    ECS::Scene::Registry source;
    const ECS::EntityHandle sourceEntity = AddGeometryPresentationEntity(source);
    auto& recipe = source.Raw().get<Runtime::GeometryPresentationRecipe>(sourceEntity);
    recipe.VectorFields = {
        Runtime::GeometryVectorFieldLayerRecipe{
            .Vector = {.Domain = Runtime::GeometryElementDomain::MeshVertex,
                       .Name = "v:normal",
                       .ValueKind = Geometry::PropertyValueKind::Vec3},
            .LengthMode = Runtime::GeometryVectorFieldLengthMode::Normalized,
            .Length = 0.125f,
            .LineWidthPx = 3.5f,
            .Color = {0.25f, 0.5f, 0.75f, 0.5f},
            .DepthTested = false,
            .Stride = 4u,
            .MaxGlyphs = 1000u,
            .Enabled = false,
        },
        Runtime::GeometryVectorFieldLayerRecipe{
            .Vector = {.Domain = Runtime::GeometryElementDomain::MeshFace,
                       .Name = "f:flow",
                       .ValueKind = Geometry::PropertyValueKind::Vec3},
            .LengthMode = Runtime::GeometryVectorFieldLengthMode::Raw,
            .Length = 2.0f,
        },
        Runtime::GeometryVectorFieldLayerRecipe{
            .Vector = {.Domain = Runtime::GeometryElementDomain::MeshEdge,
                       .Name = "e:tangent",
                       .ValueKind = Geometry::PropertyValueKind::Vec3},
        },
    };
    const std::vector<Runtime::GeometryVectorFieldLayerRecipe> expected = recipe.VectorFields;

    MemoryIOBackend backend;
    ASSERT_TRUE(Runtime::SaveSceneDocument(source, "vectors.json", backend).has_value());
    ECS::Scene::Registry loaded;
    const auto result = Runtime::LoadSceneDocument(loaded, "vectors.json", backend);
    ASSERT_TRUE(result.has_value()) << static_cast<int>(result.error());

    const ECS::EntityHandle entity = FindEntityByName(loaded, "Mesh Entity");
    ASSERT_NE(entity, ECS::InvalidEntityHandle);
    const auto* restored = loaded.Raw().try_get<Runtime::GeometryPresentationRecipe>(entity);
    ASSERT_NE(restored, nullptr);
    ASSERT_EQ(restored->VectorFields.size(), expected.size());
    for (std::size_t i = 0u; i < expected.size(); ++i)
    {
        const auto& a = expected[i];
        const auto& b = restored->VectorFields[i];
        EXPECT_EQ(b.Vector, a.Vector) << i;
        EXPECT_EQ(b.LengthMode, a.LengthMode) << i;
        EXPECT_FLOAT_EQ(b.Length, a.Length) << i;
        EXPECT_FLOAT_EQ(b.LineWidthPx, a.LineWidthPx) << i;
        EXPECT_EQ(b.Color, a.Color) << i;
        EXPECT_EQ(b.DepthTested, a.DepthTested) << i;
        EXPECT_EQ(b.Stride, a.Stride) << i;
        EXPECT_EQ(b.MaxGlyphs, a.MaxGlyphs) << i;
        EXPECT_EQ(b.Enabled, a.Enabled) << i;
    }
    // Lane bindings are untouched by the added array.
    EXPECT_EQ(restored->Presentations.size(), 1u);
}

TEST(RuntimeSceneSerialization, InvalidVectorFieldLayerRejectsDocument)
{
    ECS::Scene::Registry source;
    const ECS::EntityHandle sourceEntity = AddGeometryPresentationEntity(source);
    source.Raw().get<Runtime::GeometryPresentationRecipe>(sourceEntity).VectorFields = {
        Runtime::GeometryVectorFieldLayerRecipe{
            .Vector = {.Domain = Runtime::GeometryElementDomain::MeshVertex,
                       .Name = "v:normal",
                       .ValueKind = Geometry::PropertyValueKind::Vec3},
        },
    };
    MemoryIOBackend backend;
    ASSERT_TRUE(Runtime::SaveSceneDocument(source, "vectors.json", backend).has_value());
    const std::string document = backend.Text("vectors.json");

    // Out-of-range width, an unknown mode, a zero stride and a non-vec3
    // property are all validation failures, not silently defaulted.
    const nlohmann::json parsed = nlohmann::json::parse(document);
    {
        ECS::Scene::Registry loaded;
        ASSERT_TRUE(Runtime::DeserializeSceneDocument(loaded, parsed.dump()).has_value());
    }
    const auto mutate = [&](const auto& edit) {
        nlohmann::json copy = parsed;
        bool edited = false;
        for (auto& entityJson : copy["entities"])
        {
            if (entityJson.contains("geometryPresentation"))
            {
                edit(entityJson["geometryPresentation"]["vectorFields"][0]);
                edited = true;
            }
        }
        ASSERT_TRUE(edited);
        ECS::Scene::Registry loaded;
        EXPECT_FALSE(Runtime::DeserializeSceneDocument(loaded, copy.dump()).has_value());
    };
    mutate([](nlohmann::json& layer) { layer["lineWidthPx"] = 64.0; });
    mutate([](nlohmann::json& layer) { layer["lengthMode"] = "Sideways"; });
    mutate([](nlohmann::json& layer) { layer["stride"] = 0; });
    mutate([](nlohmann::json& layer) { layer["length"] = -1.0; });
    mutate([](nlohmann::json& layer) { layer.erase("depthTested"); });
    mutate([](nlohmann::json& layer) { layer["property"]["valueKind"] = "ScalarFloat"; });
}

TEST(RuntimeSceneSerialization, ReaderStaysPinnedToLegacyValueKindWireStrings)
{
    // Round-tripping alone would still pass if someone "modernized" BOTH the
    // writer and the reader to the canonical Float/Double names — while
    // silently invalidating every document written by an older build. This
    // pins the reader to the legacy vocabulary: a document using the canonical
    // names must be rejected, not quietly accepted.
    ECS::Scene::Registry source;
    (void)AddGeometryPresentationEntity(source);

    MemoryIOBackend backend;
    ASSERT_TRUE(
        Runtime::SaveSceneDocument(source, "presentation.json", backend).has_value());

    std::string document = backend.Text("presentation.json");
    ASSERT_NE(document.find("\"ScalarFloat\""), std::string::npos);

    const auto replaceAll = [](std::string& text,
                               const std::string& from,
                               const std::string& to)
    {
        for (std::size_t at = text.find(from); at != std::string::npos;
             at = text.find(from, at + to.size()))
        {
            text.replace(at, from.size(), to);
        }
    };
    replaceAll(document, "\"ScalarFloat\"", "\"Float\"");
    replaceAll(document, "\"ScalarDouble\"", "\"Double\"");

    ECS::Scene::Registry loaded;
    const auto result = Runtime::DeserializeSceneDocument(loaded, document);
    EXPECT_FALSE(result.has_value())
        << "canonical value-kind names must not be accepted on the wire; the "
           "persisted format is ScalarFloat/ScalarDouble";
}

// RUNTIME-192 Slice B3 — property-domain wire format.
//
// The in-memory vocabulary is now GeometryElementDomain, whose names are
// GraphNode/PointCloudPoint. The persisted format predates it and says
// "GraphVertex"/"Point", plus a legacy "MeshSurface" that every property path
// already treated as unsupported.
TEST(RuntimeSceneSerialization, PropertyDomainKeepsLegacyWireStrings)
{
    ECS::Scene::Registry source;
    const ECS::EntityHandle entity = AddMeshEntity(source);

    const auto slotWithDomain =
        [](const Runtime::GeometryPresentationSlotSemantic semantic,
           const Runtime::GeometryElementDomain domain)
    {
        Runtime::GeometryPresentationSlotRecipe slot{};
        slot.Semantic = semantic;
        slot.SourceKind = Runtime::GeometryPresentationSourceKind::PropertyBuffer;
        slot.Property = Runtime::GeometryPropertyRef{
            .Domain = domain,
            .Name = "v:field",
            .ValueKind = Geometry::PropertyValueKind::Float,
        };
        return slot;
    };

    Runtime::GeometryPresentationRecipe bindings{};
    bindings.Shape = Runtime::GeometryPresentationShape::Mesh;
    bindings.Presentations.push_back(Runtime::GeometryPresentationBindingRecipe{
        .Key = "mesh.surface",
        .Kind = Runtime::GeometryPresentationKind::SurfaceMaterial,
        .Slots = {
            slotWithDomain(Runtime::GeometryPresentationSlotSemantic::ScalarField,
                           Runtime::GeometryElementDomain::GraphNode),
            slotWithDomain(Runtime::GeometryPresentationSlotSemantic::Albedo,
                           Runtime::GeometryElementDomain::PointCloudPoint),
            slotWithDomain(Runtime::GeometryPresentationSlotSemantic::Displacement,
                           Runtime::GeometryElementDomain::GraphHalfedge),
        },
    });
    source.Raw().emplace<Runtime::GeometryPresentationRecipe>(
        entity, std::move(bindings));

    MemoryIOBackend backend;
    ASSERT_TRUE(
        Runtime::SaveSceneDocument(source, "domains.json", backend).has_value());
    const std::string text = backend.Text("domains.json");

    EXPECT_NE(text.find("\"GraphVertex\""), std::string::npos)
        << "GraphNode must persist under its legacy name GraphVertex";
    EXPECT_NE(text.find("\"Point\""), std::string::npos)
        << "PointCloudPoint must persist under its legacy name Point";
    EXPECT_NE(text.find("\"GraphHalfedge\""), std::string::npos);
    EXPECT_EQ(text.find("\"domain\":\"GraphNode\""), std::string::npos);
    EXPECT_EQ(text.find("\"domain\":\"PointCloudPoint\""), std::string::npos);

    ECS::Scene::Registry loaded;
    ASSERT_TRUE(
        Runtime::LoadSceneDocument(loaded, "domains.json", backend).has_value());
    const ECS::EntityHandle loadedEntity = FindEntityByName(loaded, "Mesh Entity");
    ASSERT_NE(loadedEntity, ECS::InvalidEntityHandle);
    const auto* roundTripped =
        loaded.Raw().try_get<Runtime::GeometryPresentationRecipe>(loadedEntity);
    ASSERT_NE(roundTripped, nullptr);
    ASSERT_FALSE(roundTripped->Presentations.empty());
    ASSERT_EQ(roundTripped->Presentations.front().Slots.size(), 3u);
    EXPECT_EQ(roundTripped->Presentations.front().Slots[0].Property.Domain,
              Runtime::GeometryElementDomain::GraphNode);
    EXPECT_EQ(roundTripped->Presentations.front().Slots[1].Property.Domain,
              Runtime::GeometryElementDomain::PointCloudPoint);
    EXPECT_EQ(roundTripped->Presentations.front().Slots[2].Property.Domain,
              Runtime::GeometryElementDomain::GraphHalfedge);
}

// RUNTIME-318: point and line lanes are colored by the overlay only. Their
// lane configs round trip; a document naming a retired slot semantic fails.
TEST(RuntimeSceneSerialization, OverlayColoredPointAndLineLanesRoundTripAndRetiredSlotsFail)
{
    using Source = G::VisualizationConfig::ColorSource;
    ECS::Scene::Registry source;
    const ECS::EntityHandle cloud = AddPointCloudEntity(source);
    const ECS::EntityHandle graph = AddGraphEntity(source);
    G::VisualizationLaneOverrides cloudLanes{};
    cloudLanes.Points = G::VisualizationConfig{};
    cloudLanes.Points->Source = Source::PerVertexBuffer;
    cloudLanes.Points->ColorBufferName = "p:rgba";
    source.Raw().emplace<G::VisualizationLaneOverrides>(cloud, cloudLanes);
    G::VisualizationLaneOverrides graphLanes{};
    graphLanes.Edges = G::VisualizationConfig{};
    graphLanes.Edges->Source = Source::ScalarField;
    graphLanes.Edges->ScalarFieldName = "e:heat";
    graphLanes.Edges->ScalarDomain = G::VisualizationConfig::Domain::Edge;
    graphLanes.Edges->Scalar.Map = Extrinsic::Graphics::Colormap::Type::Inferno;
    graphLanes.Edges->Scalar.AutoRange = false;
    graphLanes.Edges->Scalar.RangeMin = -1.0f;
    graphLanes.Edges->Scalar.RangeMax = 2.0f;
    source.Raw().emplace<G::VisualizationLaneOverrides>(graph, graphLanes);

    MemoryIOBackend backend;
    ASSERT_TRUE(Runtime::SaveSceneDocument(source, "lanes.json", backend).has_value());
    ECS::Scene::Registry loaded;
    ASSERT_TRUE(Runtime::LoadSceneDocument(loaded, "lanes.json", backend).has_value());
    const auto& raw = loaded.Raw();
    const auto* points =
        raw.try_get<G::VisualizationLaneOverrides>(FindEntityByName(loaded, "Cloud Entity"));
    ASSERT_NE(points, nullptr);
    ASSERT_TRUE(points->Points.has_value());
    EXPECT_EQ(points->Points->Source, Source::PerVertexBuffer);
    EXPECT_EQ(points->Points->ColorBufferName, "p:rgba");
    const auto* edges =
        raw.try_get<G::VisualizationLaneOverrides>(FindEntityByName(loaded, "Graph Entity"));
    ASSERT_NE(edges, nullptr);
    ASSERT_TRUE(edges->Edges.has_value());
    EXPECT_EQ(edges->Edges->Source, Source::ScalarField);
    EXPECT_EQ(edges->Edges->ScalarFieldName, "e:heat");
    EXPECT_EQ(edges->Edges->ScalarDomain, G::VisualizationConfig::Domain::Edge);
    EXPECT_EQ(edges->Edges->Scalar.Map, Extrinsic::Graphics::Colormap::Type::Inferno);
    EXPECT_FALSE(edges->Edges->Scalar.AutoRange);
    EXPECT_FLOAT_EQ(edges->Edges->Scalar.RangeMin, -1.0f);
    EXPECT_FLOAT_EQ(edges->Edges->Scalar.RangeMax, 2.0f);

    ECS::Scene::Registry slotSource;
    const ECS::EntityHandle mesh = AddMeshEntity(slotSource);
    slotSource.Raw().emplace<Runtime::GeometryPresentationRecipe>(mesh, Runtime::GeometryPresentationRecipe{
        .Shape = Runtime::GeometryPresentationShape::Mesh,
        .Presentations = {{.Key = "mesh.surface",
                           .Slots = {{.Semantic = Runtime::GeometryPresentationSlotSemantic::Displacement}}}},
    });
    const auto document = Runtime::SerializeSceneDocument(slotSource);
    ASSERT_TRUE(document.has_value());
    for (const char* retired : {"PointColor", "PointScalarField", "LineColor", "LineScalarField"})
    {
        SCOPED_TRACE(retired);
        std::string text = *document;
        const std::size_t at = text.find("\"Displacement\"");
        ASSERT_NE(at, std::string::npos);
        text.replace(at, std::string_view{"\"Displacement\""}.size(), std::string{"\""} + retired + "\"");
        ECS::Scene::Registry rejected;
        const auto result = Runtime::DeserializeSceneDocument(rejected, text);
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error(), Core::ErrorCode::InvalidFormat);
    }
}

TEST(RuntimeSceneSerialization, LegacyMeshSurfaceDomainLoadsAsUnknown)
{
    // "MeshSurface" was a whole-surface job target that never resolved to a
    // property set; every property path already reported UnsupportedDomain for
    // it. Documents written before RUNTIME-192 may still contain it, so it must
    // load as Unknown rather than being rejected.
    ECS::Scene::Registry source;
    const ECS::EntityHandle entity = AddMeshEntity(source);

    Runtime::GeometryPresentationSlotRecipe slot{};
    slot.Semantic = Runtime::GeometryPresentationSlotSemantic::Albedo;
    slot.SourceKind = Runtime::GeometryPresentationSourceKind::PropertyBuffer;
    slot.Property = Runtime::GeometryPropertyRef{
        .Domain = Runtime::GeometryElementDomain::MeshVertex,
        .Name = "v:color",
    };

    Runtime::GeometryPresentationRecipe bindings{};
    bindings.Shape = Runtime::GeometryPresentationShape::Mesh;
    bindings.Presentations.push_back(Runtime::GeometryPresentationBindingRecipe{
        .Key = "mesh.surface",
        .Kind = Runtime::GeometryPresentationKind::SurfaceMaterial,
        .Slots = {slot},
    });
    source.Raw().emplace<Runtime::GeometryPresentationRecipe>(
        entity, std::move(bindings));

    MemoryIOBackend backend;
    ASSERT_TRUE(
        Runtime::SaveSceneDocument(source, "surface.json", backend).has_value());

    std::string document = backend.Text("surface.json");
    const std::size_t at = document.find("\"MeshVertex\"");
    ASSERT_NE(at, std::string::npos);
    document.replace(at, std::string("\"MeshVertex\"").size(), "\"MeshSurface\"");

    ECS::Scene::Registry loaded;
    const auto result = Runtime::DeserializeSceneDocument(loaded, document);
    ASSERT_TRUE(result.has_value())
        << "a legacy MeshSurface domain must still load: "
        << static_cast<int>(result.error());

    const ECS::EntityHandle loadedEntity = FindEntityByName(loaded, "Mesh Entity");
    ASSERT_NE(loadedEntity, ECS::InvalidEntityHandle);
    const auto* roundTripped =
        loaded.Raw().try_get<Runtime::GeometryPresentationRecipe>(loadedEntity);
    ASSERT_NE(roundTripped, nullptr);
    ASSERT_FALSE(roundTripped->Presentations.empty());
    ASSERT_FALSE(roundTripped->Presentations.front().Slots.empty());
    EXPECT_EQ(roundTripped->Presentations.front().Slots.front().Property.Domain,
              Runtime::GeometryElementDomain::Unknown);
}

// BUG-137 — a seam-carrying mesh keeps its UVs on the corner domain and has no
// `v:texcoord` at all. Saving only the vertex channel dropped them silently on
// round-trip, so the scene reloaded without its parameterization.
TEST(RuntimeSceneSerialization, SaveLoadRoundTripPreservesCornerDomainTexcoords)
{
    ECS::Scene::Registry source;
    const ECS::EntityHandle mesh = AddMeshEntity(source);
    auto& raw = source.Raw();

    // Replace the vertex-domain UVs with corner-domain UVs carrying a seam:
    // the two corners targeting vertex 0 hold different values.
    auto& vertices = raw.get<GS::Vertices>(mesh);
    auto vertexTexcoords = vertices.Properties.Get<glm::vec2>("v:texcoord");
    vertices.Properties.Remove(vertexTexcoords);
    ASSERT_FALSE(vertices.Properties.Exists("v:texcoord"));

    auto& halfedges = raw.get<GS::Halfedges>(mesh);
    const std::vector<glm::vec2> cornerUvs{
        {0.00f, 0.00f}, {1.00f, 0.00f}, {0.00f, 1.00f},
        {0.50f, 0.50f}, {0.25f, 0.75f}, {0.75f, 0.25f},
    };
    halfedges.Properties.GetOrAdd<glm::vec2>("h:texcoord", glm::vec2{0.0f})
        .Vector() = cornerUvs;

    auto& faces = raw.get<GS::Faces>(mesh).Properties;
    faces.GetOrAdd<std::uint32_t>("f:atlas_region", 7u).Vector()[0] = 7u;
    faces.GetOrAdd<std::uint32_t>("f:atlas_chart", 3u).Vector()[0] = 3u;
    MemoryIOBackend backend;
    auto saved = Runtime::SaveSceneDocument(source, "scene.json", backend);
    ASSERT_TRUE(saved.has_value()) << static_cast<int>(saved.error());

    const nlohmann::json parsed =
        nlohmann::json::parse(backend.Text("scene.json"));
    ASSERT_TRUE(
        parsed["entities"][0]["geometrySources"]["halfedges"]["texcoords"].is_array());
    EXPECT_EQ(
        parsed["entities"][0]["geometrySources"]["halfedges"]["texcoords"].size(),
        cornerUvs.size());
    EXPECT_FALSE(
        parsed["entities"][0]["geometrySources"]["vertices"].contains("texcoords"));

    ECS::Scene::Registry loaded;
    auto loadedResult = Runtime::LoadSceneDocument(loaded, "scene.json", backend);
    ASSERT_TRUE(loadedResult.has_value()) << static_cast<int>(loadedResult.error());

    const ECS::EntityHandle loadedMesh = FindEntityByName(loaded, "Mesh Entity");
    ASSERT_NE(loadedMesh, ECS::InvalidEntityHandle);
    const auto& loadedFaces = loaded.Raw().get<GS::Faces>(loadedMesh).Properties;
    ASSERT_TRUE(loadedFaces.Get<std::uint32_t>("f:atlas_region"));
    ASSERT_TRUE(loadedFaces.Get<std::uint32_t>("f:atlas_chart"));
    EXPECT_EQ(loadedFaces.Get<std::uint32_t>("f:atlas_region").Vector(), (std::vector<std::uint32_t>{7u}));
    EXPECT_EQ(loadedFaces.Get<std::uint32_t>("f:atlas_chart").Vector(), (std::vector<std::uint32_t>{3u}));
    const auto& loadedHalfedges = loaded.Raw().get<GS::Halfedges>(loadedMesh);
    const auto reloaded =
        loadedHalfedges.Properties.Get<glm::vec2>("h:texcoord");
    ASSERT_TRUE(reloaded.IsValid());
    ASSERT_EQ(reloaded.Vector().size(), cornerUvs.size());
    for (std::size_t i = 0; i < cornerUvs.size(); ++i)
    {
        EXPECT_FLOAT_EQ(reloaded.Vector()[i].x, cornerUvs[i].x) << "corner " << i;
        EXPECT_FLOAT_EQ(reloaded.Vector()[i].y, cornerUvs[i].y) << "corner " << i;
    }
}

// Authored normal seams stay on halfedges across persistence; reload must not
// replace them with derived vertex normals or split owning topology.
TEST(RuntimeSceneSerialization, SaveLoadRoundTripPreservesCornerDomainNormals)
{
    ECS::Scene::Registry source;
    const ECS::EntityHandle mesh = AddMeshEntity(source);
    auto& halfedges = source.Raw().get<GS::Halfedges>(mesh);
    const std::vector<glm::vec3> cornerNormals{
        {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f},
        {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
    };
    halfedges.Properties.GetOrAdd<glm::vec3>("h:normal", glm::vec3{0.0f})
        .Vector() = cornerNormals;

    MemoryIOBackend backend;
    const auto saved = Runtime::SaveSceneDocument(source, "scene.json", backend);
    ASSERT_TRUE(saved.has_value()) << static_cast<int>(saved.error());

    const nlohmann::json parsed =
        nlohmann::json::parse(backend.Text("scene.json"));
    ASSERT_TRUE(
        parsed["entities"][0]["geometrySources"]["halfedges"]["normals"].is_array());
    EXPECT_EQ(
        parsed["entities"][0]["geometrySources"]["halfedges"]["normals"].size(),
        cornerNormals.size());

    ECS::Scene::Registry loaded;
    const auto loadedResult =
        Runtime::LoadSceneDocument(loaded, "scene.json", backend);
    ASSERT_TRUE(loadedResult.has_value())
        << static_cast<int>(loadedResult.error());

    const ECS::EntityHandle loadedMesh = FindEntityByName(loaded, "Mesh Entity");
    ASSERT_NE(loadedMesh, ECS::InvalidEntityHandle);
    const auto reloaded = loaded.Raw()
                              .get<GS::Halfedges>(loadedMesh)
                              .Properties.Get<glm::vec3>("h:normal");
    ASSERT_TRUE(reloaded.IsValid());
    ASSERT_EQ(reloaded.Vector().size(), cornerNormals.size());
    for (std::size_t i = 0; i < cornerNormals.size(); ++i)
        EXPECT_EQ(reloaded.Vector()[i], cornerNormals[i]) << "corner " << i;
}

// The generated-atlas extent is persisted without its runtime UV binding,
// which load rebuilds; extents outside the shared atlas bounds, or with no
// UVs to describe, fail closed.
TEST(RuntimeSceneSerialization, GeneratedAtlasExtentRoundTripsAndRebindsToLoadedUvs)
{
    ECS::Scene::Registry source;
    const ECS::EntityHandle mesh = AddMeshEntity(source);
    ASSERT_TRUE(Runtime::PublishMeshUvAtlasExtent(source.Raw(), mesh, 1536u, 512u));

    MemoryIOBackend backend;
    ASSERT_TRUE(Runtime::SaveSceneDocument(source, "scene.json", backend).has_value());
    const nlohmann::json parsed = nlohmann::json::parse(backend.Text("scene.json"));
    EXPECT_EQ(parsed["entities"][0]["geometrySources"]["uvAtlasExtent"],
              (nlohmann::json{{"width", 1536u}, {"height", 512u}}))
        << "only the extent is persisted, never revision stamps or fingerprints";

    ECS::Scene::Registry loaded;
    ASSERT_TRUE(Runtime::LoadSceneDocument(loaded, "scene.json", backend).has_value());
    const ECS::EntityHandle loadedMesh = FindEntityByName(loaded, "Mesh Entity");
    ASSERT_NE(loadedMesh, ECS::InvalidEntityHandle);
    const auto rebound = Runtime::FindCurrentMeshUvAtlasExtent(loaded.Raw(), loadedMesh);
    ASSERT_TRUE(rebound.has_value()) << "load binds the extent to the loaded UVs";
    EXPECT_EQ(rebound->Width, 1536u);
    EXPECT_EQ(rebound->Height, 512u);

    // An edited atlas no longer has a generated extent, so none is saved.
    auto uvs = loaded.Raw().get<GS::Vertices>(loadedMesh).Properties.Get<glm::vec2>("v:texcoord");
    uvs[2] = glm::vec2{0.0f, 0.5f};
    EXPECT_FALSE(Runtime::FindCurrentMeshUvAtlasExtent(loaded.Raw(), loadedMesh).has_value());
    ASSERT_TRUE(Runtime::SaveSceneDocument(loaded, "edited.json", backend).has_value());
    EXPECT_FALSE(nlohmann::json::parse(backend.Text("edited.json"))["entities"][0]["geometrySources"]
                     .contains("uvAtlasExtent"));

    for (const nlohmann::json& bad : {nlohmann::json{{"width", 0u}, {"height", 512u}},
                                      nlohmann::json{{"width", 20000u}, {"height", 512u}},
                                      nlohmann::json{{"width", -1}, {"height", 512}},
                                      nlohmann::json{{"width", 1536u}},
                                      nlohmann::json("1024x1024")})
    {
        nlohmann::json document = parsed;
        document["entities"][0]["geometrySources"]["uvAtlasExtent"] = bad;
        ECS::Scene::Registry rejected;
        EXPECT_FALSE(Runtime::DeserializeSceneDocument(rejected, document.dump()).has_value()) << bad.dump();
    }
    nlohmann::json withoutUvs = parsed;
    withoutUvs["entities"][0]["geometrySources"]["vertices"].erase("texcoords");
    ECS::Scene::Registry rejected;
    EXPECT_FALSE(Runtime::DeserializeSceneDocument(rejected, withoutUvs.dump()).has_value())
        << "an extent needs canonical UVs to describe";
}

// RUNTIME-315: structural attribute bindings are authored scene state.
TEST(RuntimeSceneSerialization, AttributeBindingsRoundTripAndStaleSourcesLoadAsReportedFallbacks)
{
    namespace RT = Runtime;
    ECS::Scene::Registry source;
    const ECS::EntityHandle mesh = AddMeshEntity(source);
    source.Raw().get<GS::Vertices>(mesh).Properties.GetOrAdd<glm::vec3>(
        std::string{PN::kNormal}, glm::vec3{0.0f, 0.0f, 1.0f});
    (void)source.Raw().get<GS::Vertices>(mesh).Properties.GetOrAdd<glm::vec3>(
        "v:scratch", glm::vec3{1.0f, 0.0f, 0.0f});  // processed property (RUNTIME-319)
    const RT::VertexChannelBindingSet authored{
        .Position = {.Enabled = true,
                     .Property = {RT::GeometryElementDomain::MeshVertex, "v:normal",
                                  Geometry::PropertyValueKind::Vec3}},
        .Normal = {.Enabled = true,
                   .Property = {RT::GeometryElementDomain::MeshVertex, "v:scratch",
                                Geometry::PropertyValueKind::Vec3}},
        .Texcoord = {.Enabled = true,
                     .Property = {RT::GeometryElementDomain::MeshVertex, "v:texcoord",
                                  Geometry::PropertyValueKind::Vec2}},
        .BindingGeneration = 9u,
    };
    source.Raw().emplace<RT::VertexChannelBindingSet>(mesh, authored);

    MemoryIOBackend backend;
    const auto saved = RT::SaveSceneDocument(source, "bindings.json", backend);
    ASSERT_TRUE(saved.has_value());
    EXPECT_EQ(saved->Stats.AttributeBindingEntities, 1u);
    const nlohmann::json parsed = nlohmann::json::parse(backend.Text("bindings.json"));
    const auto& bindingsJson = parsed["entities"][0]["attributeBindings"];
    ASSERT_TRUE(bindingsJson.is_object());
    EXPECT_EQ(bindingsJson.size(), 3u);
    EXPECT_EQ(bindingsJson["position"]["name"], "v:normal");

    ECS::Scene::Registry loaded;
    const auto result = RT::LoadSceneDocument(loaded, "bindings.json", backend);
    ASSERT_TRUE(result.has_value()) << static_cast<int>(result.error());
    EXPECT_EQ(result->Stats.AttributeBindingEntities, 1u);
    EXPECT_EQ(result->Stats.StaleAttributeBindings, 0u);  // v:scratch is persisted
    const ECS::EntityHandle loadedMesh = FindEntityByName(loaded, "Mesh Entity");
    ASSERT_NE(loadedMesh, ECS::InvalidEntityHandle);
    const auto* bindings = loaded.Raw().try_get<RT::VertexChannelBindingSet>(loadedMesh);
    ASSERT_NE(bindings, nullptr);
    EXPECT_EQ(bindings->Position, authored.Position);
    EXPECT_EQ(bindings->Normal, authored.Normal);  // kept as authored intent
    EXPECT_EQ(bindings->Texcoord, authored.Texcoord);

    // Saving the loaded scene reproduces the same bindings document.
    const auto resaved = RT::SaveSceneDocument(loaded, "again.json", backend);
    ASSERT_TRUE(resaved.has_value());
    EXPECT_EQ(nlohmann::json::parse(backend.Text("again.json"))["entities"][0]["attributeBindings"],
              bindingsJson);

    // A source missing from the document still loads as a counted fallback.
    nlohmann::json withoutProperties = parsed;
    withoutProperties["entities"][0]["geometrySources"]["vertices"].erase("properties");
    ECS::Scene::Registry stale;
    const auto staleResult = RT::DeserializeSceneDocument(stale, withoutProperties.dump());
    ASSERT_TRUE(staleResult.has_value());
    EXPECT_EQ(staleResult->Stats.StaleAttributeBindings, 1u);
    const auto* staleBindings =
        stale.Raw().try_get<RT::VertexChannelBindingSet>(FindEntityByName(stale, "Mesh Entity"));
    ASSERT_NE(staleBindings, nullptr);
    EXPECT_EQ(staleBindings->Normal, authored.Normal);  // kept as authored intent
}

TEST(RuntimeSceneSerialization, InvalidAttributeBindingsRejectTheDocument)
{
    ECS::Scene::Registry source;
    const ECS::EntityHandle mesh = AddMeshEntity(source);
    source.Raw().emplace<Runtime::VertexChannelBindingSet>(
        mesh, Runtime::VertexChannelBindingSet{
                  .Normal = {.Enabled = true,
                             .Property = {Runtime::GeometryElementDomain::MeshVertex, "v:texcoord",
                                          Geometry::PropertyValueKind::Vec2}}});
    MemoryIOBackend backend;
    ASSERT_TRUE(Runtime::SaveSceneDocument(source, "ok.json", backend).has_value());
    nlohmann::json document = nlohmann::json::parse(backend.Text("ok.json"));

    for (const char* key : {"color", "point_size", "bogus"})
    {
        nlohmann::json bad = document;
        bad["entities"][0]["attributeBindings"] = {{key, bad["entities"][0]["attributeBindings"]["normal"]}};
        ECS::Scene::Registry loaded;
        EXPECT_FALSE(Runtime::DeserializeSceneDocument(loaded, bad.dump()).has_value()) << key;
    }
    nlohmann::json unnamed = document;
    unnamed["entities"][0]["attributeBindings"]["normal"]["name"] = "";
    ECS::Scene::Registry loaded;
    EXPECT_FALSE(Runtime::DeserializeSceneDocument(loaded, unnamed.dump()).has_value());
}

// ---------------------------------------------------------------------------
// RUNTIME-319 — processed/custom typed properties on every element domain.
// ---------------------------------------------------------------------------
namespace
{
    template <class T>
    void AddProcessedProperty(Geometry::PropertySet& properties, const std::string& name,
                              std::vector<T> values)
    {
        ASSERT_EQ(values.size(), properties.Size()) << name;
        auto property = properties.GetOrAdd<T>(name, T{});
        property.Vector() = std::move(values);
    }

    template <class T>
    void ExpectSameProperty(const Geometry::PropertySet& loaded,
                            const Geometry::PropertySet& original,
                            const std::string& name)
    {
        const auto expected = original.Get<T>(name);
        const auto actual = loaded.Get<T>(name);
        ASSERT_TRUE(expected.IsValid()) << name;
        ASSERT_TRUE(actual.IsValid()) << name << " was not restored with its value kind";
        ASSERT_EQ(actual.Vector().size(), expected.Vector().size()) << name;
        if constexpr (std::is_same_v<T, bool>)
        {
            EXPECT_EQ(actual.Vector(), expected.Vector()) << name;
        }
        else
        {
            EXPECT_EQ(std::memcmp(actual.Vector().data(), expected.Vector().data(),
                                  expected.Vector().size() * sizeof(T)),
                      0)
                << name << " is not bit-exact";
        }
    }

    struct OpaqueValue
    {
        int A{0};
    };

    constexpr float kInf = std::numeric_limits<float>::infinity();
}

TEST(RuntimeSceneSerialization, ProcessedPropertiesRoundTripBitExactlyOnEveryElementDomain)
{
    ECS::Scene::Registry source;
    const ECS::EntityHandle mesh = AddMeshEntity(source);
    const ECS::EntityHandle graph = AddGraphEntity(source);
    const ECS::EntityHandle cloud = AddPointCloudEntity(source);
    auto& raw = source.Raw();

    auto& meshVertices = raw.get<GS::Vertices>(mesh).Properties;
    AddProcessedProperty<float>(meshVertices, "v:curvature", {-0.0f, kInf, 1.0e-40f});
    AddProcessedProperty<glm::vec4>(meshVertices, "v:rgba",
                                    {{0.1f, 0.2f, 0.3f, 1.0f}, {-kInf, 0.0f, 1.0f, 2.0f}, {3.0f, 4.0f, 5.0f, 6.0f}});
    auto& meshEdges = raw.get<GS::Edges>(mesh).Properties;
    AddProcessedProperty<bool>(meshEdges, "e:feature", {true, false, true});
    AddProcessedProperty<double>(meshEdges, "e:length", {0.1, 1.0 / 3.0, std::numeric_limits<double>::max()});
    auto& meshHalfedges = raw.get<GS::Halfedges>(mesh).Properties;
    AddProcessedProperty<std::int32_t>(meshHalfedges, "h:label",
                                       {-7, std::numeric_limits<std::int32_t>::min(),
                                        std::numeric_limits<std::int32_t>::max(), 0, 1, -1});
    AddProcessedProperty<glm::vec2>(meshHalfedges, "h:uv2",
                                    {{0.0f, 1.0f}, {2.0f, 3.0f}, {4.0f, 5.0f}, {6.0f, 7.0f}, {8.0f, 9.0f}, {0.5f, 0.25f}});
    auto& meshFaces = raw.get<GS::Faces>(mesh).Properties;
    AddProcessedProperty<std::uint64_t>(meshFaces, "f:id", {std::numeric_limits<std::uint64_t>::max()});
    AddProcessedProperty<glm::vec3>(meshFaces, "f:flow", {{1.0f, -2.0f, 3.5f}});

    auto& nodes = raw.get<GS::Vertices>(graph).Properties;
    AddProcessedProperty<std::uint32_t>(nodes, "v:component", {0u, 7u, 0xFFFFFFFFu});
    AddProcessedProperty<float>(nodes, "v:heat", {0.25f, 0.5f, 0.75f});
    auto& graphEdges = raw.get<GS::Edges>(graph).Properties;
    AddProcessedProperty<double>(graphEdges, "e:weight", {-std::numeric_limits<double>::infinity(), 2.5});
    auto& graphHalfedges = raw.get<GS::Halfedges>(graph).Properties;
    AddProcessedProperty<glm::vec3>(graphHalfedges, "h:dir",
                                    {{1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}});

    auto& points = raw.get<GS::Vertices>(cloud).Properties;
    AddProcessedProperty<float>(points, "v:density", {3.0f, 4.0f});
    AddProcessedProperty<bool>(points, "v:outlier", {false, true});

    MemoryIOBackend backend;
    const auto saved = Runtime::SaveSceneDocument(source, "properties.json", backend);
    ASSERT_TRUE(saved.has_value()) << static_cast<int>(saved.error());
    // The graph's own `v:point` storage (PopulateFromGraph mirrors `v:position`
    // into it) is engine-derived and skipped, as are topology rows.
    EXPECT_EQ(saved->Stats.GeometryProperties, 14u);
    EXPECT_EQ(saved->Stats.UnpersistedGeometryProperties, 0u)
        << "topology (graph h:connectivity) is skipped silently, not counted";

    const nlohmann::json parsed = nlohmann::json::parse(backend.Text("properties.json"));
    const auto& meshTable = parsed["entities"][0]["geometrySources"]["vertices"]["properties"];
    ASSERT_TRUE(meshTable.is_array());
    ASSERT_EQ(meshTable.size(), 2u) << "dedicated streams (v:position, v:texcoord) stay out of the table";
    EXPECT_EQ(meshTable[0]["name"], "v:curvature");
    EXPECT_EQ(meshTable[0]["kind"], "ScalarFloat");
    EXPECT_EQ(meshTable[1]["name"], "v:rgba");

    ECS::Scene::Registry loaded;
    const auto result = Runtime::LoadSceneDocument(loaded, "properties.json", backend);
    ASSERT_TRUE(result.has_value()) << static_cast<int>(result.error());
    EXPECT_EQ(result->Stats.GeometryProperties, 14u);

    const auto& lraw = loaded.Raw();
    const ECS::EntityHandle lmesh = FindEntityByName(loaded, "Mesh Entity");
    const ECS::EntityHandle lgraph = FindEntityByName(loaded, "Graph Entity");
    const ECS::EntityHandle lcloud = FindEntityByName(loaded, "Cloud Entity");
    ASSERT_NE(lmesh, ECS::InvalidEntityHandle);
    ASSERT_NE(lgraph, ECS::InvalidEntityHandle);
    ASSERT_NE(lcloud, ECS::InvalidEntityHandle);

    ExpectSameProperty<float>(lraw.get<GS::Vertices>(lmesh).Properties, meshVertices, "v:curvature");
    ExpectSameProperty<glm::vec4>(lraw.get<GS::Vertices>(lmesh).Properties, meshVertices, "v:rgba");
    ExpectSameProperty<bool>(lraw.get<GS::Edges>(lmesh).Properties, meshEdges, "e:feature");
    ExpectSameProperty<double>(lraw.get<GS::Edges>(lmesh).Properties, meshEdges, "e:length");
    ExpectSameProperty<std::int32_t>(lraw.get<GS::Halfedges>(lmesh).Properties, meshHalfedges, "h:label");
    ExpectSameProperty<glm::vec2>(lraw.get<GS::Halfedges>(lmesh).Properties, meshHalfedges, "h:uv2");
    ExpectSameProperty<std::uint64_t>(lraw.get<GS::Faces>(lmesh).Properties, meshFaces, "f:id");
    ExpectSameProperty<glm::vec3>(lraw.get<GS::Faces>(lmesh).Properties, meshFaces, "f:flow");
    ExpectSameProperty<std::uint32_t>(lraw.get<GS::Vertices>(lgraph).Properties, nodes, "v:component");
    ExpectSameProperty<float>(lraw.get<GS::Vertices>(lgraph).Properties, nodes, "v:heat");
    ExpectSameProperty<double>(lraw.get<GS::Edges>(lgraph).Properties, graphEdges, "e:weight");
    ExpectSameProperty<glm::vec3>(lraw.get<GS::Halfedges>(lgraph).Properties, graphHalfedges, "h:dir");
    ExpectSameProperty<float>(lraw.get<GS::Vertices>(lcloud).Properties, points, "v:density");
    ExpectSameProperty<bool>(lraw.get<GS::Vertices>(lcloud).Properties, points, "v:outlier");

    // Restored properties are new storages with their own content revision.
    const auto restored = lraw.get<GS::Vertices>(lmesh).Properties.FindPropertyRevision("v:curvature");
    ASSERT_TRUE(restored.has_value());
    EXPECT_NE(*restored, 0u);

    // Saving the loaded scene reproduces the same document.
    ASSERT_TRUE(Runtime::SaveSceneDocument(loaded, "again.json", backend).has_value());
    EXPECT_EQ(nlohmann::json::parse(backend.Text("again.json"))["entities"], parsed["entities"]);
}

TEST(RuntimeSceneSerialization, UnpersistablePropertiesAreCountedAndSkippedOnSave)
{
    ECS::Scene::Registry source;
    const ECS::EntityHandle cloud = AddPointCloudEntity(source);
    auto& points = source.Raw().get<GS::Vertices>(cloud).Properties;
    AddProcessedProperty<float>(points, "v:broken", {1.0f, std::numeric_limits<float>::quiet_NaN()});
    (void)points.GetOrAdd<OpaqueValue>("v:opaque", OpaqueValue{});
    AddProcessedProperty<float>(points, "v:fine", {1.0f, 2.0f});

    MemoryIOBackend backend;
    const auto saved = Runtime::SaveSceneDocument(source, "skip.json", backend);
    ASSERT_TRUE(saved.has_value());
    EXPECT_EQ(saved->Stats.GeometryProperties, 1u);
    EXPECT_EQ(saved->Stats.UnpersistedGeometryProperties, 2u);
    const nlohmann::json parsed = nlohmann::json::parse(backend.Text("skip.json"));
    const auto& table = parsed["entities"][0]["geometrySources"]["vertices"]["properties"];
    ASSERT_EQ(table.size(), 1u);
    EXPECT_EQ(table[0]["name"], "v:fine");
}

TEST(RuntimeSceneSerialization, MalformedPropertyTablesRejectTheDocumentWithoutMutation)
{
    ECS::Scene::Registry source;
    const ECS::EntityHandle mesh = AddMeshEntity(source);
    AddProcessedProperty<float>(source.Raw().get<GS::Vertices>(mesh).Properties, "v:curvature",
                                {1.0f, 2.0f, 3.0f});
    AddProcessedProperty<bool>(source.Raw().get<GS::Edges>(mesh).Properties, "e:feature",
                               {true, false, true});
    MemoryIOBackend backend;
    ASSERT_TRUE(Runtime::SaveSceneDocument(source, "valid.json", backend).has_value());
    const nlohmann::json valid = nlohmann::json::parse(backend.Text("valid.json"));
    {
        ECS::Scene::Registry scene;
        ASSERT_TRUE(Runtime::DeserializeSceneDocument(scene, valid.dump()).has_value());
    }

    const auto floatPayload = [](const std::vector<float>& values)
    {
        return Core::Base64::Encode(std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t*>(values.data()), values.size() * sizeof(float)));
    };

    const auto vertexEntry = [](nlohmann::json& doc) -> nlohmann::json&
    { return doc["entities"][0]["geometrySources"]["vertices"]["properties"][0]; };
    const auto edgeEntry = [](nlohmann::json& doc) -> nlohmann::json&
    { return doc["entities"][0]["geometrySources"]["edges"]["properties"][0]; };

    std::vector<std::pair<std::string, std::function<void(nlohmann::json&)>>> cases;
    cases.emplace_back("table not an array", [&](nlohmann::json& d)
                       { d["entities"][0]["geometrySources"]["vertices"]["properties"] = nlohmann::json::object(); });
    cases.emplace_back("entry not an object", [&](nlohmann::json& d) { vertexEntry(d) = 1; });
    cases.emplace_back("missing data", [&](nlohmann::json& d) { vertexEntry(d).erase("data"); });
    cases.emplace_back("unknown key", [&](nlohmann::json& d) { vertexEntry(d)["count"] = 3; });
    cases.emplace_back("non-string name", [&](nlohmann::json& d) { vertexEntry(d)["name"] = 7; });
    cases.emplace_back("empty name", [&](nlohmann::json& d) { vertexEntry(d)["name"] = ""; });
    cases.emplace_back("topology name", [&](nlohmann::json& d) { vertexEntry(d)["name"] = "v:deleted"; });
    cases.emplace_back("dedicated stream name", [&](nlohmann::json& d) { vertexEntry(d)["name"] = "v:position"; });
    cases.emplace_back("duplicate name", [&](nlohmann::json& d)
                       {
                           const nlohmann::json copy = vertexEntry(d);
                           d["entities"][0]["geometrySources"]["vertices"]["properties"].push_back(copy);
                       });
    cases.emplace_back("unknown kind", [&](nlohmann::json& d) { vertexEntry(d)["kind"] = "Quat"; });
    cases.emplace_back("Unknown kind", [&](nlohmann::json& d) { vertexEntry(d)["kind"] = "Unknown"; });
    cases.emplace_back("kind/size mismatch", [&](nlohmann::json& d) { vertexEntry(d)["kind"] = "Vec3"; });
    cases.emplace_back("truncated payload", [&](nlohmann::json& d)
                       { auto& data = vertexEntry(d)["data"]; data = data.get<std::string>().substr(0, 8); });
    cases.emplace_back("oversized payload", [&](nlohmann::json& d)
                       { vertexEntry(d)["data"] = floatPayload({1.0f, 2.0f, 3.0f, 4.0f}); });
    cases.emplace_back("invalid base64", [&](nlohmann::json& d)
                       { auto& data = vertexEntry(d)["data"]; auto text = data.get<std::string>(); text[0] = '*'; data = text; });
    cases.emplace_back("NaN payload", [&](nlohmann::json& d)
                       { vertexEntry(d)["data"] = floatPayload({1.0f, std::numeric_limits<float>::quiet_NaN(), 3.0f}); });
    cases.emplace_back("bool byte other than 0/1", [&](nlohmann::json& d)
                       { edgeEntry(d)["data"] = "AQIB"; });  // bytes 01 02 01
    cases.emplace_back("bad entry on a later domain", [&](nlohmann::json& d)
                       { edgeEntry(d)["kind"] = "Vec4"; });

    for (const auto& [label, mutate] : cases)
    {
        nlohmann::json document = valid;
        mutate(document);
        ECS::Scene::Registry scene;
        const ECS::EntityHandle existing = ECS::Scene::CreateDefault(scene, "Existing");
        const auto result = Runtime::DeserializeSceneDocument(scene, document.dump());
        EXPECT_FALSE(result.has_value()) << label;
        if (!result.has_value())
            EXPECT_EQ(result.error(), Core::ErrorCode::InvalidFormat) << label;
        EXPECT_EQ(FindEntityByName(scene, "Existing"), existing) << label << ": the scene was mutated";
        EXPECT_EQ(FindEntityByName(scene, "Mesh Entity"), ECS::InvalidEntityHandle) << label;
    }
}

namespace
{
    // One scene with a typed property in every element domain, so a case can corrupt exactly one table.
    struct PropertyTableFixture
    {
        std::string Text{};
        nlohmann::json Document{};
    };

    PropertyTableFixture SavePropertyTableScene()
    {
        ECS::Scene::Registry source;
        const ECS::EntityHandle mesh = AddMeshEntity(source);
        const ECS::EntityHandle graph = AddGraphEntity(source);
        const ECS::EntityHandle cloud = AddPointCloudEntity(source);
        auto& raw = source.Raw();
        AddProcessedProperty<float>(raw.get<GS::Vertices>(mesh).Properties, "v:a", {1.0f, 2.0f, 3.0f});
        AddProcessedProperty<float>(raw.get<GS::Edges>(mesh).Properties, "e:a", {1.0f, 2.0f, 3.0f});
        AddProcessedProperty<float>(raw.get<GS::Halfedges>(mesh).Properties, "h:a",
                                    {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f});
        AddProcessedProperty<float>(raw.get<GS::Faces>(mesh).Properties, "f:a", {1.0f});
        AddProcessedProperty<float>(raw.get<GS::Vertices>(graph).Properties, "v:a", {1.0f, 2.0f, 3.0f});
        AddProcessedProperty<float>(raw.get<GS::Edges>(graph).Properties, "e:a", {1.0f, 2.0f});
        AddProcessedProperty<float>(raw.get<GS::Halfedges>(graph).Properties, "h:a", {1.0f, 2.0f, 3.0f, 4.0f});
        AddProcessedProperty<float>(raw.get<GS::Vertices>(cloud).Properties, "v:a", {1.0f, 2.0f});
        PropertyTableFixture out{};
        const auto text = Runtime::SerializeSceneDocument(source);
        EXPECT_TRUE(text.has_value());
        out.Text = text.value_or(std::string{});
        out.Document = nlohmann::json::parse(out.Text);
        return out;
    }

    nlohmann::json& FirstPropertyEntry(nlohmann::json& document, const std::size_t entity, const char* section)
    {
        return document["entities"][entity]["geometrySources"][section]["properties"][0];
    }
}

// RUNTIME-319 review: every domain's table rejects the same malformed entries (mesh vertices are covered above).
TEST(RuntimeSceneSerialization, EveryDomainsPropertyTableRejectsMalformedPayloads)
{
    const PropertyTableFixture valid = SavePropertyTableScene();
    {
        ECS::Scene::Registry scene;
        ASSERT_TRUE(Runtime::DeserializeSceneDocument(scene, valid.Text).has_value());
    }
    struct Table { std::size_t Entity; const char* Section; const char* Label; };
    const auto bytes = [](const std::size_t count)
    {
        return Core::Base64::Encode(std::vector<std::uint8_t>(count, 0u));
    };
    for (const Table table : {Table{0, "edges", "mesh edges"}, Table{0, "halfedges", "mesh halfedges"},
                              Table{0, "faces", "mesh faces"}, Table{1, "nodes", "graph nodes"},
                              Table{1, "edges", "graph edges"}, Table{1, "halfedges", "graph halfedges"},
                              Table{2, "vertices", "point cloud"}})
    {
        const std::string tableLabel = table.Label;
        const nlohmann::json& entry = valid.Document["entities"][table.Entity]["geometrySources"][table.Section]["properties"][0];
        const std::size_t payloadBytes = Core::Base64::DecodedSize(entry["data"].get_ref<const std::string&>()).value();

        std::vector<std::pair<std::string, std::function<void(nlohmann::json&)>>> cases;
        cases.emplace_back("payload not a multiple of four", [&](nlohmann::json& d)
                           { FirstPropertyEntry(d, table.Entity, table.Section)["data"] = bytes(payloadBytes - 1u); });
        cases.emplace_back("payload one element short", [&](nlohmann::json& d)
                           { FirstPropertyEntry(d, table.Entity, table.Section)["data"] = bytes(payloadBytes - 4u); });
        cases.emplace_back("payload one element long", [&](nlohmann::json& d)
                           { FirstPropertyEntry(d, table.Entity, table.Section)["data"] = bytes(payloadBytes + 4u); });
        cases.emplace_back("padding in the middle", [&](nlohmann::json& d)
                           { FirstPropertyEntry(d, table.Entity, table.Section)["data"] = "AAAA=AAA"; });
        cases.emplace_back("NaN payload", [&](nlohmann::json& d)
                           {
                               std::vector<float> values(payloadBytes / 4u, std::numeric_limits<float>::quiet_NaN());
                               FirstPropertyEntry(d, table.Entity, table.Section)["data"] = Core::Base64::Encode(
                                   std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(values.data()), payloadBytes));
                           });
        cases.emplace_back("duplicate entry", [&](nlohmann::json& d)
                           {
                               auto& list = d["entities"][table.Entity]["geometrySources"][table.Section]["properties"];
                               list.push_back(list[0]);
                           });
        cases.emplace_back("unknown kind", [&](nlohmann::json& d)
                           { FirstPropertyEntry(d, table.Entity, table.Section)["kind"] = "Quat"; });
        cases.emplace_back("kind does not match the size", [&](nlohmann::json& d)
                           { FirstPropertyEntry(d, table.Entity, table.Section)["kind"] = "Vec4"; });
        cases.emplace_back("not an object", [&](nlohmann::json& d)
                           { FirstPropertyEntry(d, table.Entity, table.Section) = "x"; });

        for (const auto& [label, mutate] : cases)
        {
            nlohmann::json document = valid.Document;
            mutate(document);
            ECS::Scene::Registry scene;
            const ECS::EntityHandle existing = ECS::Scene::CreateDefault(scene, "Existing");
            const auto result = Runtime::DeserializeSceneDocument(scene, document.dump());
            EXPECT_FALSE(result.has_value()) << tableLabel << ": " << label;
            EXPECT_EQ(FindEntityByName(scene, "Existing"), existing) << tableLabel << ": " << label;
            EXPECT_EQ(FindEntityByName(scene, "Mesh Entity"), ECS::InvalidEntityHandle) << tableLabel << ": " << label;
        }
    }
}

// RUNTIME-319 review: a name the engine reserves cannot be smuggled in through a table under another kind or as
// a second copy of a dedicated stream.
TEST(RuntimeSceneSerialization, CanonicalNamesKeepTheirKindAndDedicatedNamesCannotCollide)
{
    const PropertyTableFixture valid = SavePropertyTableScene();
    struct Case { std::size_t Entity; const char* Section; const char* Name; const char* Kind; const char* Label; };
    const auto elementPayload = [&](const std::size_t entity, const char* section, const std::size_t elementBytes)
    {
        const auto& entry = valid.Document["entities"][entity]["geometrySources"][section]["properties"][0];
        const std::size_t count = Core::Base64::DecodedSize(entry["data"].get_ref<const std::string&>()).value() / 4u;
        return Core::Base64::Encode(std::vector<std::uint8_t>(count * elementBytes, 0u));
    };
    for (const Case c : {
             // Engine-derived/mirrored names loaded with a foreign kind would break later typed GetOrAdd calls.
             Case{1, "nodes", "v:point", "Bool", "v:point as Bool"},
             Case{1, "nodes", "v:point", "ScalarFloat", "v:point as float"},
             Case{0, "faces", "f:normal", "Bool", "f:normal as Bool"},
             Case{0, "faces", "f:normal", "ScalarFloat", "f:normal as float"},
             Case{0, "vertices", "v:texcoord", "Vec3", "v:texcoord as Vec3"},
             Case{0, "faces", "f:atlas_region", "ScalarFloat", "f:atlas_region as float"},
             // Dedicated streams have their own array; a table copy collides with it.
             Case{0, "vertices", "v:position", "Vec3", "v:position in the table"},
             Case{0, "vertices", "v:normal", "Vec3", "v:normal in the table"},
             Case{0, "halfedges", "h:normal", "Vec3", "h:normal in the table"},
             Case{0, "halfedges", "h:to_vertex", "UInt32", "topology name"}})
    {
        nlohmann::json document = valid.Document;
        auto& list = document["entities"][c.Entity]["geometrySources"][c.Section]["properties"];
        const std::string kind = c.Kind;
        const std::size_t elementBytes = kind == "Bool" ? 1u : kind == "Vec3" ? 12u : 4u;
        list.push_back({{"name", c.Name}, {"kind", kind}, {"data", elementPayload(c.Entity, c.Section, elementBytes)}});
        ECS::Scene::Registry scene;
        EXPECT_FALSE(Runtime::DeserializeSceneDocument(scene, document.dump()).has_value()) << c.Label;
    }

    // The canonical kind itself still loads (a derived `v:point` mirror is data the engine accepts back).
    nlohmann::json accepted = valid.Document;
    accepted["entities"][1]["geometrySources"]["nodes"]["properties"].push_back(
        {{"name", "v:point"}, {"kind", "Vec3"}, {"data", elementPayload(1, "nodes", 12u)}});
    ECS::Scene::Registry scene;
    EXPECT_TRUE(Runtime::DeserializeSceneDocument(scene, accepted.dump()).has_value());
}

// RUNTIME-319 review: derived mirrors are not persisted (a stale copy would outlive the source), and a dedicated
// stream held under the wrong type is counted instead of vanishing.
TEST(RuntimeSceneSerialization, DerivedMirrorsAreSkippedAndMistypedStreamsAreCounted)
{
    ECS::Scene::Registry source;
    const ECS::EntityHandle mesh = AddMeshEntity(source);
    const ECS::EntityHandle graph = AddGraphEntity(source);
    const ECS::EntityHandle cloud = AddPointCloudEntity(source);
    auto& raw = source.Raw();

    // Stale mirrors: change the position after PopulateFromGraph copied it into `v:point`.
    ASSERT_TRUE(raw.get<GS::Vertices>(graph).Properties.Exists("v:point"));
    AddProcessedProperty<glm::vec3>(raw.get<GS::Vertices>(mesh).Properties, "v:point",
                                    {{9.0f, 9.0f, 9.0f}, {9.0f, 9.0f, 9.0f}, {9.0f, 9.0f, 9.0f}});
    AddProcessedProperty<glm::vec3>(raw.get<GS::Faces>(mesh).Properties, "f:normal", {{0.0f, 0.0f, 1.0f}});
    // A dedicated stream name held as another type is lost on save, so it is counted.
    AddProcessedProperty<glm::vec3>(raw.get<GS::Vertices>(cloud).Properties, "v:texcoord",
                                    {{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}});

    MemoryIOBackend backend;
    const auto saved = Runtime::SaveSceneDocument(source, "derived.json", backend);
    ASSERT_TRUE(saved.has_value());
    const nlohmann::json parsed = nlohmann::json::parse(backend.Text("derived.json"));
    const auto names = [&](const std::size_t entity, const char* section)
    {
        std::vector<std::string> out;
        const auto& geometry = parsed["entities"][entity]["geometrySources"][section];
        if (geometry.contains("properties"))
            for (const auto& entry : geometry["properties"]) out.push_back(entry["name"]);
        return out;
    };
    EXPECT_TRUE(names(0, "vertices").empty()) << "mesh v:point is a mirror";
    EXPECT_TRUE(names(0, "faces").empty()) << "f:normal is recomputed";
    EXPECT_TRUE(names(1, "nodes").empty()) << "graph v:point mirrors v:position";
    EXPECT_TRUE(names(2, "vertices").empty());
    EXPECT_FALSE(parsed["entities"][2]["geometrySources"]["vertices"].contains("texcoords"));

    // Only the mistyped dedicated stream is reported as lost; derived mirrors are not "lost" data.
    EXPECT_EQ(saved->Stats.UnpersistedGeometryProperties, 1u);
}

// RUNTIME-319 follow-up: a mistyped atlas-label stream is counted too; the save never fails over a property.
TEST(RuntimeSceneSerialization, MistypedAtlasLabelStreamIsCountedAndTheSaveSucceeds)
{
    ECS::Scene::Registry source;
    const ECS::EntityHandle mesh = AddMeshEntity(source);
    AddProcessedProperty<float>(source.Raw().get<GS::Faces>(mesh).Properties, "f:atlas_region", {1.0f});
    MemoryIOBackend backend;
    const auto saved = Runtime::SaveSceneDocument(source, "atlas.json", backend);
    ASSERT_TRUE(saved.has_value());
    EXPECT_EQ(saved->Stats.UnpersistedGeometryProperties, 1u);
    EXPECT_FALSE(nlohmann::json::parse(backend.Text("atlas.json"))["entities"][0]["geometrySources"]["faces"]
                     .contains("atlasRegion"));
}

TEST(RuntimeSceneSerialization, LargePropertyPayloadStaysCompactAndRoundTrips)
{
    constexpr std::size_t kPoints = 100'000u;
    ECS::Scene::Registry source;
    const ECS::EntityHandle cloud = ECS::Scene::CreateDefault(source, "Large Cloud");
    auto& vertices = source.Raw().emplace<GS::Vertices>(cloud);
    std::vector<glm::vec3> positions(kPoints);
    std::vector<glm::vec3> field(kPoints);
    for (std::size_t i = 0u; i < kPoints; ++i)
    {
        positions[i] = glm::vec3{static_cast<float>(i), 0.0f, 0.0f};
        field[i] = glm::vec3{std::sin(static_cast<float>(i)), 1.0f / static_cast<float>(i + 1u), -0.5f};
    }
    SetPositions(vertices, std::move(positions));
    AddProcessedProperty<glm::vec3>(vertices.Properties, "v:field", field);

    const auto start = std::chrono::steady_clock::now();
    const auto document = Runtime::SerializeSceneDocument(source);
    ASSERT_TRUE(document.has_value());
    const nlohmann::json parsed = nlohmann::json::parse(*document);
    const std::string& data =
        parsed["entities"][0]["geometrySources"]["vertices"]["properties"][0]["data"].get_ref<const std::string&>();
    EXPECT_EQ(data.size(), (kPoints * sizeof(glm::vec3) + 2u) / 3u * 4u)
        << "one base64 string per property, not one JSON node per element";

    ECS::Scene::Registry loaded;
    ASSERT_TRUE(Runtime::DeserializeSceneDocument(loaded, *document).has_value());
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    EXPECT_LT(seconds, 30.0) << "debug-build sanity bound for a 100k-point save+load";
    const ECS::EntityHandle lcloud = FindEntityByName(loaded, "Large Cloud");
    ASSERT_NE(lcloud, ECS::InvalidEntityHandle);
    ExpectSameProperty<glm::vec3>(loaded.Raw().get<GS::Vertices>(lcloud).Properties,
                                  vertices.Properties, "v:field");
}
