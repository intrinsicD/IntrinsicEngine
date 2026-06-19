#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Scene.Bootstrap;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Runtime.ProgressiveRenderData;
import Extrinsic.Runtime.SceneSerialization;
import Geometry.Properties;

namespace ECS = Extrinsic::ECS;
namespace ECSC = Extrinsic::ECS::Components;
namespace GS = Extrinsic::ECS::Components::GeometrySources;
namespace G = Extrinsic::Graphics::Components;
namespace Runtime = Extrinsic::Runtime;
namespace PN = Extrinsic::ECS::Components::GeometrySources::PropertyNames;

namespace
{
    constexpr std::uint32_t kInvalidIndex = std::numeric_limits<std::uint32_t>::max();

    void SetPositions(GS::Vertices& vertices, const std::vector<glm::vec3>& positions)
    {
        vertices.Properties.Resize(positions.size());
        auto property = vertices.Properties.GetOrAdd<glm::vec3>(
            std::string{PN::kPosition},
            glm::vec3{0.0f});
        property.Vector() = positions;
    }

    void SetNodePositions(GS::Nodes& nodes, const std::vector<glm::vec3>& positions)
    {
        nodes.Properties.Resize(positions.size());
        auto property = nodes.Properties.GetOrAdd<glm::vec3>(
            std::string{PN::kPosition},
            glm::vec3{0.0f});
        property.Vector() = positions;
    }

    void SetEdges(GS::Edges& edges,
                  const std::vector<std::uint32_t>& v0,
                  const std::vector<std::uint32_t>& v1)
    {
        edges.Properties.Resize(v0.size());
        auto p0 = edges.Properties.GetOrAdd<std::uint32_t>(std::string{PN::kEdgeV0}, 0u);
        auto p1 = edges.Properties.GetOrAdd<std::uint32_t>(std::string{PN::kEdgeV1}, 0u);
        p0.Vector() = v0;
        p1.Vector() = v1;
    }

    void SetHalfedges(GS::Halfedges& halfedges)
    {
        halfedges.Properties.Resize(6u);
        auto toVertex = halfedges.Properties.GetOrAdd<std::uint32_t>(
            std::string{PN::kHalfedgeToVertex},
            kInvalidIndex);
        auto next = halfedges.Properties.GetOrAdd<std::uint32_t>(
            std::string{PN::kHalfedgeNext},
            kInvalidIndex);
        auto face = halfedges.Properties.GetOrAdd<std::uint32_t>(
            std::string{PN::kHalfedgeFace},
            kInvalidIndex);
        toVertex.Vector() = {1u, 2u, 0u, 0u, 2u, 1u};
        next.Vector() = {1u, 2u, 0u, 5u, 3u, 4u};
        face.Vector() = {0u, 0u, 0u, kInvalidIndex, kInvalidIndex, kInvalidIndex};
    }

    void SetFaces(GS::Faces& faces)
    {
        faces.Properties.Resize(1u);
        auto halfedge = faces.Properties.GetOrAdd<std::uint32_t>(
            std::string{PN::kFaceHalfedge},
            kInvalidIndex);
        halfedge.Vector() = {0u};
    }

    ECS::EntityHandle AddMesh(ECS::Scene::Registry& scene)
    {
        ECS::EntityHandle entity = ECS::Scene::CreateDefault(scene, "progressive mesh");
        auto& raw = scene.Raw();
        auto& vertices = raw.emplace<GS::Vertices>(entity);
        SetPositions(vertices, {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}});
        vertices.Properties.GetOrAdd<glm::vec3>("v:normal", glm::vec3{0, 0, 1}).Vector() =
            {{0, 0, 1}, {0, 0, 1}, {0, 0, 1}};
        vertices.Properties.GetOrAdd<float>("v:heat", 0.0f).Vector() = {0.0f, 0.5f, 1.0f};
        vertices.Properties.GetOrAdd<std::uint32_t>("v:label", 0u).Vector() = {1u, 2u, 3u};
        vertices.Properties.GetOrAdd<std::string>("v:name", std::string{}).Vector() = {"a", "b", "c"};

        auto& edges = raw.emplace<GS::Edges>(entity);
        SetEdges(edges, {0u, 1u, 2u}, {1u, 2u, 0u});
        auto& halfedges = raw.emplace<GS::Halfedges>(entity);
        SetHalfedges(halfedges);
        auto& faces = raw.emplace<GS::Faces>(entity);
        SetFaces(faces);
        faces.Properties.GetOrAdd<float>("f:heat", 0.0f).Vector() = {4.0f};
        raw.emplace<GS::HasMeshTopology>(entity);
        raw.emplace<G::RenderSurface>(entity);
        raw.emplace<G::RenderEdges>(entity);
        raw.emplace<G::RenderPoints>(entity);
        return entity;
    }

    ECS::EntityHandle AddGraph(ECS::Scene::Registry& scene)
    {
        ECS::EntityHandle entity = ECS::Scene::CreateDefault(scene, "progressive graph");
        auto& raw = scene.Raw();
        auto& nodes = raw.emplace<GS::Nodes>(entity);
        SetNodePositions(nodes, {{0, 0, 0}, {1, 0, 0}});
        nodes.Properties.GetOrAdd<glm::vec4>("v:color", glm::vec4{1}).Vector() =
            {{1, 0, 0, 1}, {0, 1, 0, 1}};
        auto& edges = raw.emplace<GS::Edges>(entity);
        SetEdges(edges, {0u}, {1u});
        edges.Properties.GetOrAdd<glm::vec4>("e:color", glm::vec4{1}).Vector() =
            {{0, 0, 1, 1}};
        raw.emplace<GS::HasGraphTopology>(entity);
        raw.emplace<G::RenderEdges>(entity);
        raw.emplace<G::RenderPoints>(entity);
        return entity;
    }

    ECS::EntityHandle AddCloud(ECS::Scene::Registry& scene)
    {
        ECS::EntityHandle entity = ECS::Scene::CreateDefault(scene, "progressive cloud");
        auto& raw = scene.Raw();
        auto& vertices = raw.emplace<GS::Vertices>(entity);
        SetPositions(vertices, {{0, 0, 0}, {0, 1, 0}});
        vertices.Properties.GetOrAdd<float>("v:size", 1.0f).Vector() = {2.0f, 4.0f};
        vertices.Properties.GetOrAdd<glm::vec3>("v:normal", glm::vec3{0, 1, 0}).Vector() =
            {{0, 1, 0}, {0, 1, 0}};
        raw.emplace<G::RenderPoints>(entity);
        return entity;
    }

    Runtime::ProgressivePresentationBindings MakeMeshBindings()
    {
        Runtime::ProgressiveSlotBinding normal{};
        normal.Semantic = Runtime::ProgressiveSlotSemantic::Normal;
        normal.SourceKind = Runtime::ProgressiveSlotSourceKind::PropertyBake;
        normal.Property = Runtime::ProgressivePropertyBindingDescriptor{
            .Domain = Runtime::ProgressiveGeometryDomain::MeshVertex,
            .PropertyName = "v:normal",
            .ExpectedValueKind = Runtime::ProgressivePropertyValueKind::Vec3,
            .ExpectedElementCount = 3u,
            .SourceGeneration = 12u,
        };
        normal.GeneratedTexture = Extrinsic::Assets::AssetId{7u, 2u};
        normal.GeneratedPolicy = Runtime::ProgressiveGeneratedOutputPolicy::DeterministicChildAsset;
        normal.Provenance = Runtime::ProgressiveGeneratedOutputProvenance::PropertyBinding;
        normal.Readiness = Runtime::ProgressiveReadinessState::Ready;
        normal.LastDiagnostic = "transient-ready-state";

        Runtime::ProgressiveSlotBinding color{};
        color.Semantic = Runtime::ProgressiveSlotSemantic::Albedo;
        color.SourceKind = Runtime::ProgressiveSlotSourceKind::UniformDefault;
        color.UniformDefault.Kind = Runtime::ProgressivePropertyValueKind::Vec4;
        color.UniformDefault.Vector = glm::vec4{0.2f, 0.4f, 0.6f, 1.0f};

        return Runtime::ProgressivePresentationBindings{
            .Shape = Runtime::ProgressiveEntityShape::MeshLeaf,
            .Lanes = {Runtime::ProgressiveRenderLaneBinding{
                .Lane = Runtime::ProgressiveRenderLane::Surface,
                .PresentationKey = "mesh.surface",
            }},
            .Presentations = {Runtime::ProgressivePresentationBinding{
                .Key = "mesh.surface",
                .Kind = Runtime::ProgressivePresentationKind::SurfaceMaterial,
                .Slots = {normal, color},
            }},
            .BindingGeneration = 99u,
        };
    }
}

TEST(ProgressiveRenderData, ResolvesMeshGraphAndPointCloudProperties)
{
    ECS::Scene::Registry scene;
    const ECS::EntityHandle mesh = AddMesh(scene);
    const ECS::EntityHandle graph = AddGraph(scene);
    const ECS::EntityHandle cloud = AddCloud(scene);

    const auto meshView = GS::BuildConstView(scene.Raw(), mesh);
    const auto graphView = GS::BuildConstView(scene.Raw(), graph);
    const auto cloudView = GS::BuildConstView(scene.Raw(), cloud);

    auto meshNormal = Runtime::ResolvePropertyBinding(
        meshView,
        Runtime::ProgressivePropertyBindingDescriptor{
            .Domain = Runtime::ProgressiveGeometryDomain::MeshVertex,
            .PropertyName = "v:normal",
            .ExpectedValueKind = Runtime::ProgressivePropertyValueKind::Vec3,
            .ExpectedElementCount = 3u,
        });
    EXPECT_TRUE(meshNormal.Compatible());
    EXPECT_EQ(meshNormal.ActualValueKind, Runtime::ProgressivePropertyValueKind::Vec3);

    auto meshFace = Runtime::ResolvePropertyBinding(
        meshView,
        Runtime::ProgressivePropertyBindingDescriptor{
            .Domain = Runtime::ProgressiveGeometryDomain::MeshFace,
            .PropertyName = "f:heat",
            .ExpectedValueKind = Runtime::ProgressivePropertyValueKind::ScalarFloat,
            .ExpectedElementCount = 1u,
        });
    EXPECT_TRUE(meshFace.Compatible());

    auto graphEdge = Runtime::ResolvePropertyBinding(
        graphView,
        Runtime::ProgressivePropertyBindingDescriptor{
            .Domain = Runtime::ProgressiveGeometryDomain::GraphEdge,
            .PropertyName = "e:color",
            .ExpectedValueKind = Runtime::ProgressivePropertyValueKind::Vec4,
            .ExpectedElementCount = 1u,
        });
    EXPECT_TRUE(graphEdge.Compatible());

    auto pointSize = Runtime::ResolvePropertyBinding(
        cloudView,
        Runtime::ProgressivePropertyBindingDescriptor{
            .Domain = Runtime::ProgressiveGeometryDomain::Point,
            .PropertyName = "v:size",
            .ExpectedValueKind = Runtime::ProgressivePropertyValueKind::ScalarFloat,
            .ExpectedElementCount = 2u,
        });
    EXPECT_TRUE(pointSize.Compatible());

    auto wrongDomain = Runtime::ResolvePropertyBinding(
        graphView,
        Runtime::ProgressivePropertyBindingDescriptor{
            .Domain = Runtime::ProgressiveGeometryDomain::Point,
            .PropertyName = "v:size",
            .ExpectedValueKind = Runtime::ProgressivePropertyValueKind::ScalarFloat,
        });
    EXPECT_EQ(wrongDomain.Status, Runtime::ProgressivePropertyResolutionStatus::DomainUnavailable);
}

TEST(ProgressiveRenderData, PropertyPickerOrdersCompatibleOptionsFirstAndExplainsDisabled)
{
    ECS::Scene::Registry scene;
    const ECS::EntityHandle mesh = AddMesh(scene);
    const auto meshView = GS::BuildConstView(scene.Raw(), mesh);

    const auto options = Runtime::EnumeratePropertyOptions(
        meshView,
        Runtime::ProgressiveGeometryDomain::MeshVertex,
        Runtime::ProgressivePropertyValueKind::Vec3,
        3u,
        5u);

    ASSERT_GE(options.size(), 4u);
    EXPECT_TRUE(options[0].Compatible);
    EXPECT_EQ(options[0].Descriptor.PropertyName, "v:normal");
    bool sawHeatMismatch = false;
    bool sawUnsupported = false;
    for (const auto& option : options)
    {
        if (option.Descriptor.PropertyName == "v:heat")
        {
            sawHeatMismatch = true;
            EXPECT_FALSE(option.Compatible);
            EXPECT_EQ(option.ActualValueKind, Runtime::ProgressivePropertyValueKind::ScalarFloat);
            EXPECT_FALSE(option.DisabledReason.empty());
        }
        if (option.Descriptor.PropertyName == "v:name")
        {
            sawUnsupported = true;
            EXPECT_FALSE(option.Compatible);
            EXPECT_EQ(option.ActualValueKind, Runtime::ProgressivePropertyValueKind::Unknown);
            EXPECT_FALSE(option.DisabledReason.empty());
        }
    }
    EXPECT_TRUE(sawHeatMismatch);
    EXPECT_TRUE(sawUnsupported);

    const auto stale = Runtime::ResolvePropertyBinding(
        meshView,
        Runtime::ProgressivePropertyBindingDescriptor{
            .Domain = Runtime::ProgressiveGeometryDomain::MeshVertex,
            .PropertyName = "v:normal",
            .ExpectedValueKind = Runtime::ProgressivePropertyValueKind::Vec3,
            .ExpectedElementCount = 3u,
            .SourceGeneration = 4u,
        },
        5u);
    EXPECT_EQ(stale.Status, Runtime::ProgressivePropertyResolutionStatus::StaleGeneration);

    const auto missing = Runtime::ResolvePropertyBinding(
        meshView,
        Runtime::ProgressivePropertyBindingDescriptor{
            .Domain = Runtime::ProgressiveGeometryDomain::MeshVertex,
            .PropertyName = "v:missing",
            .ExpectedValueKind = Runtime::ProgressivePropertyValueKind::Vec3,
        });
    EXPECT_EQ(missing.Status, Runtime::ProgressivePropertyResolutionStatus::MissingProperty);
}

TEST(ProgressiveRenderData, PartialMeshProvenanceUsesAvailableSourceProperties)
{
    ECS::Scene::Registry scene;
    const ECS::EntityHandle entity = ECS::Scene::CreateDefault(scene, "partial mesh");
    auto& raw = scene.Raw();

    auto& vertices = raw.emplace<GS::Vertices>(entity);
    SetPositions(vertices, {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}});
    vertices.Properties.GetOrAdd<float>("v:heat", 0.0f).Vector() = {1.0f, 2.0f, 3.0f};

    auto& faces = raw.emplace<GS::Faces>(entity);
    SetFaces(faces);
    faces.Properties.GetOrAdd<float>("f:heat", 0.0f).Vector() = {4.0f};
    raw.emplace<GS::HasMeshTopology>(entity);

    const auto view = GS::BuildConstView(raw, entity);
    ASSERT_EQ(view.ActiveDomain, GS::Domain::Unknown);

    const auto vertex = Runtime::ResolvePropertyBinding(
        view,
        Runtime::ProgressivePropertyBindingDescriptor{
            .Domain = Runtime::ProgressiveGeometryDomain::MeshVertex,
            .PropertyName = "v:heat",
            .ExpectedValueKind = Runtime::ProgressivePropertyValueKind::ScalarFloat,
            .ExpectedElementCount = 3u,
        });
    EXPECT_TRUE(vertex.Compatible());

    const auto face = Runtime::ResolvePropertyBinding(
        view,
        Runtime::ProgressivePropertyBindingDescriptor{
            .Domain = Runtime::ProgressiveGeometryDomain::MeshFace,
            .PropertyName = "f:heat",
            .ExpectedValueKind = Runtime::ProgressivePropertyValueKind::ScalarFloat,
            .ExpectedElementCount = 1u,
        });
    EXPECT_TRUE(face.Compatible());

    const auto halfedge = Runtime::ResolvePropertyBinding(
        view,
        Runtime::ProgressivePropertyBindingDescriptor{
            .Domain = Runtime::ProgressiveGeometryDomain::MeshHalfedge,
            .PropertyName = "h:to_vertex",
            .ExpectedValueKind = Runtime::ProgressivePropertyValueKind::UInt32,
        });
    EXPECT_EQ(halfedge.Status, Runtime::ProgressivePropertyResolutionStatus::DomainUnavailable);
}

TEST(ProgressiveRenderData, SceneSerializationPersistsBindingsButDropsTransientReadiness)
{
    ECS::Scene::Registry source;
    const ECS::EntityHandle mesh = AddMesh(source);
    source.Raw().emplace_or_replace<Runtime::ProgressivePresentationBindings>(
        mesh,
        MakeMeshBindings());

    auto saved = Runtime::SerializeSceneDocument(source);
    ASSERT_TRUE(saved.has_value()) << static_cast<int>(saved.error());
    const nlohmann::json parsed = nlohmann::json::parse(*saved);
    ASSERT_EQ(parsed["stats"]["progressiveRenderDataEntities"].get<std::uint32_t>(), 1u);
    ASSERT_TRUE(parsed["entities"][0]["progressiveRenderData"].is_object());
    const auto& slotJson =
        parsed["entities"][0]["progressiveRenderData"]["presentations"][0]["slots"][0];
    EXPECT_EQ(slotJson["semantic"].get<std::string>(), "Normal");
    EXPECT_EQ(slotJson["sourceKind"].get<std::string>(), "PropertyBake");
    EXPECT_FALSE(slotJson.contains("readiness"));
    EXPECT_FALSE(slotJson.contains("lastDiagnostic"));
    EXPECT_EQ(slotJson["generatedTexture"]["index"].get<std::uint32_t>(), 7u);
    EXPECT_EQ(slotJson["generatedTexture"]["generation"].get<std::uint32_t>(), 2u);

    ECS::Scene::Registry loaded;
    auto loadedResult = Runtime::DeserializeSceneDocument(loaded, *saved);
    ASSERT_TRUE(loadedResult.has_value()) << static_cast<int>(loadedResult.error());
    EXPECT_EQ(loadedResult->Stats.ProgressiveRenderDataEntities, 1u);
    const auto progressiveView = loaded.Raw().view<Runtime::ProgressivePresentationBindings>();
    ASSERT_FALSE(progressiveView.empty());
    const ECS::EntityHandle loadedMesh = progressiveView.front();
    ASSERT_NE(loadedMesh, ECS::InvalidEntityHandle);
    ASSERT_TRUE(loaded.Raw().all_of<G::RenderSurface>(loadedMesh));
    const auto& bindings = loaded.Raw().get<Runtime::ProgressivePresentationBindings>(loadedMesh);
    EXPECT_EQ(bindings.Shape, Runtime::ProgressiveEntityShape::MeshLeaf);
    EXPECT_EQ(bindings.BindingGeneration, 99u);
    const auto* lane = Runtime::FindLaneBinding(bindings, Runtime::ProgressiveRenderLane::Surface);
    ASSERT_NE(lane, nullptr);
    EXPECT_EQ(lane->PresentationKey, "mesh.surface");
    const auto* presentation = Runtime::FindPresentationBinding(bindings, "mesh.surface");
    ASSERT_NE(presentation, nullptr);
    const auto* normal = Runtime::FindSlotBinding(*presentation, Runtime::ProgressiveSlotSemantic::Normal);
    ASSERT_NE(normal, nullptr);
    EXPECT_EQ(normal->Property.PropertyName, "v:normal");
    EXPECT_EQ(normal->GeneratedTexture, (Extrinsic::Assets::AssetId{7u, 2u}));
    EXPECT_EQ(normal->Readiness, Runtime::ProgressiveReadinessState::Pending);
    EXPECT_TRUE(normal->LastDiagnostic.empty());
}
