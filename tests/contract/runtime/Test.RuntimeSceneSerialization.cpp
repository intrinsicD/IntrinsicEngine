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
import Extrinsic.ECS.Component.SpatialDebugBinding;
import Extrinsic.ECS.Component.StableId;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.Selection;
import Extrinsic.ECS.Hierarchy.Mutation;
import Extrinsic.ECS.Scene.Bootstrap;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.Colormap;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.Runtime.SceneSerialization;
import Geometry.Properties;

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

    void SetNodePositions(GS::Nodes& nodes,
                          std::vector<glm::vec3> positions)
    {
        nodes.Properties.Resize(positions.size());
        auto property = nodes.Properties.GetOrAdd<glm::vec3>(
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
        auto& nodes = raw.emplace<GS::Nodes>(entity);
        SetNodePositions(nodes,
                         {
                             {0.0f, 0.0f, 0.0f},
                             {1.0f, 0.0f, 0.0f},
                             {2.0f, 0.0f, 0.0f},
                         });
        auto& edges = raw.emplace<GS::Edges>(entity);
        SetEdges(edges, {0u, 1u}, {1u, 2u});
        raw.emplace<GS::HasGraphTopology>(entity);
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
    ASSERT_EQ(parsed["version"].get<std::uint32_t>(), 1u);
    ASSERT_EQ(parsed["entities"].size(), 3u);
    EXPECT_EQ(parsed["stats"]["renderHintEntities"].get<std::uint32_t>(), 3u);
    ASSERT_TRUE(parsed["entities"][0]["render"]["visualization"].is_object());
    EXPECT_EQ(parsed["entities"][0]["render"]["visualization"]["source"].get<std::string>(),
              "ScalarField");
    ASSERT_TRUE(parsed["entities"][0]["geometrySources"]["vertices"]["texcoords"].is_array());
    EXPECT_EQ(parsed["entities"][0]["geometrySources"]["vertices"]["texcoords"].size(), 3u);

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
    ASSERT_NE(graphView.NodeSource, nullptr);
    EXPECT_EQ(graphView.NodeSource->Properties.Get<glm::vec3>(PN::kPosition).Vector().size(), 3u);
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
    raw.emplace<ECSC::SpatialDebugBinding>(entity);
    raw.emplace<AssetInstance::Source>(entity, AssetInstance::Source{.AssetId = 42u});

    MemoryIOBackend backend;
    auto saved = Runtime::SaveSceneDocument(source, "unsupported.json", backend);
    ASSERT_TRUE(saved.has_value()) << static_cast<int>(saved.error());
    EXPECT_EQ(saved->Stats.Entities, 1u);
    EXPECT_EQ(saved->Stats.UnsupportedPersistenceEntities, 1u);
    EXPECT_EQ(saved->Stats.UnsupportedLightEntities, 1u);
    EXPECT_EQ(saved->Stats.UnsupportedShadowEntities, 1u);
    EXPECT_EQ(saved->Stats.UnsupportedPhysicsEntities, 1u);
    EXPECT_EQ(saved->Stats.UnsupportedSpatialDebugEntities, 1u);
    EXPECT_EQ(saved->Stats.UnsupportedAssetInstanceEntities, 1u);

    const nlohmann::json parsed = nlohmann::json::parse(backend.Text("unsupported.json"));
    EXPECT_EQ(parsed["stats"]["unsupportedPersistenceEntities"].get<std::uint32_t>(), 1u);
    EXPECT_EQ(parsed["stats"]["unsupportedLightEntities"].get<std::uint32_t>(), 1u);
    EXPECT_EQ(parsed["stats"]["unsupportedShadowEntities"].get<std::uint32_t>(), 1u);
    EXPECT_EQ(parsed["stats"]["unsupportedPhysicsEntities"].get<std::uint32_t>(), 1u);
    EXPECT_EQ(parsed["stats"]["unsupportedSpatialDebugEntities"].get<std::uint32_t>(), 1u);
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
    EXPECT_FALSE(loadedRaw.any_of<ECSC::SpatialDebugBinding>(loadedEntity));
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
        R"({"version":2,"entities":[]})");
    EXPECT_FALSE(unsupportedVersion.has_value());
    EXPECT_EQ(unsupportedVersion.error(), Core::ErrorCode::InvalidFormat);

    auto badGeometry = Runtime::DeserializeSceneDocument(
        scene,
        R"({"version":1,"entities":[{"id":0,"geometrySources":{"domain":"Mesh"}}]})");
    EXPECT_FALSE(badGeometry.has_value());
    EXPECT_EQ(badGeometry.error(), Core::ErrorCode::InvalidFormat);
}
