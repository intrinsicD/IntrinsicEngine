#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <gtest/gtest.h>
#include <entt/entity/registry.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

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
import Extrinsic.Runtime.SceneSerialization;
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
    ASSERT_EQ(parsed["version"].get<std::uint32_t>(), 2u);
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
    EXPECT_FALSE(overrides.Surface.has_value());
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

    auto badGeometry = Runtime::DeserializeSceneDocument(
        scene,
        R"({"version":2,"entities":[{"id":0,"geometrySources":{"domain":"Mesh"}}]})");
    EXPECT_FALSE(badGeometry.has_value());
    EXPECT_EQ(badGeometry.error(), Core::ErrorCode::InvalidFormat);
}

TEST(RuntimeSceneSerialization, MalformedGraphTopologyFailsClosed)
{
    const nlohmann::json valid = nlohmann::json::parse(
        R"({"version":2,"entities":[{"id":0,"geometrySources":{"domain":"Graph","nodes":{"deleted":0,"positions":[[0,0,0],[1,0,0]]},"halfedges":{"toVertex":[1,0],"next":[1,0],"prev":[1,0]},"edges":{"deleted":0,"v0":[0],"v1":[1]}}}]})");

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
// unconstrained kind as Unknown; the reader still accepts the legacy
// expectedValueKind:"Any" spelling.
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
    EXPECT_EQ(text.find("\"expectedValueKind\":\"Float\""), std::string::npos);
    EXPECT_EQ(text.find("\"kind\":\"Double\""), std::string::npos);
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
            slotWithDomain(Runtime::GeometryPresentationSlotSemantic::LineWidth,
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
