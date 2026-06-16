#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <glm/glm.hpp>

import Extrinsic.Asset.Registry;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Scene.Bootstrap;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Runtime.ProgressivePresentationExtraction;
import Extrinsic.Runtime.ProgressiveRenderData;
import Geometry.Properties;

namespace ECS = Extrinsic::ECS;
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

    void AttachMesh(ECS::Scene::Registry& scene, const ECS::EntityHandle entity)
    {
        auto& raw = scene.Raw();
        auto& vertices = raw.emplace<GS::Vertices>(entity);
        SetPositions(vertices, {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}});
        vertices.Properties.GetOrAdd<glm::vec3>("v:normal", glm::vec3{0, 0, 1}).Vector() =
            {{0, 0, 1}, {0, 0, 1}, {0, 0, 1}};
        vertices.Properties.GetOrAdd<glm::vec2>("v:texcoord", glm::vec2{0}).Vector() =
            {{0, 0}, {1, 0}, {0, 1}};

        auto& edges = raw.emplace<GS::Edges>(entity);
        edges.Properties.Resize(3u);
        edges.Properties.GetOrAdd<std::uint32_t>(std::string{PN::kEdgeV0}, 0u).Vector() =
            {0u, 1u, 2u};
        edges.Properties.GetOrAdd<std::uint32_t>(std::string{PN::kEdgeV1}, 0u).Vector() =
            {1u, 2u, 0u};

        auto& halfedges = raw.emplace<GS::Halfedges>(entity);
        halfedges.Properties.Resize(6u);
        halfedges.Properties.GetOrAdd<std::uint32_t>(
            std::string{PN::kHalfedgeToVertex}, kInvalidIndex).Vector() =
            {1u, 2u, 0u, 0u, 2u, 1u};
        halfedges.Properties.GetOrAdd<std::uint32_t>(
            std::string{PN::kHalfedgeNext}, kInvalidIndex).Vector() =
            {1u, 2u, 0u, 5u, 3u, 4u};
        halfedges.Properties.GetOrAdd<std::uint32_t>(
            std::string{PN::kHalfedgeFace}, kInvalidIndex).Vector() =
            {0u, 0u, 0u, kInvalidIndex, kInvalidIndex, kInvalidIndex};

        auto& faces = raw.emplace<GS::Faces>(entity);
        faces.Properties.Resize(1u);
        faces.Properties.GetOrAdd<std::uint32_t>(
            std::string{PN::kFaceHalfedge}, kInvalidIndex).Vector() = {0u};
        raw.emplace<GS::HasMeshTopology>(entity);
        raw.emplace<G::RenderSurface>(entity);
    }

    ECS::EntityHandle AddGraph(ECS::Scene::Registry& scene)
    {
        const ECS::EntityHandle entity = ECS::Scene::CreateDefault(scene, "presentation graph");
        auto& raw = scene.Raw();
        auto& nodes = raw.emplace<GS::Nodes>(entity);
        nodes.Properties.Resize(2u);
        nodes.Properties.GetOrAdd<glm::vec3>(std::string{PN::kPosition}, glm::vec3{0}).Vector() =
            {{0, 0, 0}, {1, 0, 0}};
        nodes.Properties.GetOrAdd<glm::vec4>("v:color", glm::vec4{1}).Vector() =
            {{1, 0, 0, 1}, {0, 1, 0, 1}};
        auto& edges = raw.emplace<GS::Edges>(entity);
        edges.Properties.Resize(1u);
        edges.Properties.GetOrAdd<std::uint32_t>(std::string{PN::kEdgeV0}, 0u).Vector() = {0u};
        edges.Properties.GetOrAdd<std::uint32_t>(std::string{PN::kEdgeV1}, 0u).Vector() = {1u};
        edges.Properties.GetOrAdd<glm::vec4>("e:color", glm::vec4{1}).Vector() =
            {{0, 0, 1, 1}};
        raw.emplace<GS::HasGraphTopology>(entity);
        raw.emplace<G::RenderEdges>(entity);
        raw.emplace<G::RenderPoints>(entity);
        return entity;
    }

    Runtime::ProgressiveSlotBinding PropertyBufferSlot(
        const Runtime::ProgressiveSlotSemantic semantic,
        const Runtime::ProgressiveGeometryDomain domain,
        const char* name,
        const Runtime::ProgressivePropertyValueKind kind,
        const std::size_t count)
    {
        Runtime::ProgressiveSlotBinding slot{};
        slot.Semantic = semantic;
        slot.SourceKind = Runtime::ProgressiveSlotSourceKind::PropertyBuffer;
        slot.Property = Runtime::ProgressivePropertyBindingDescriptor{
            .Domain = domain,
            .PropertyName = name,
            .ExpectedValueKind = kind,
            .ExpectedElementCount = count,
        };
        return slot;
    }
}

TEST(ProgressivePresentationExtraction, MeshDefaultsAndGeneratedTextureStateDoNotBlock)
{
    ECS::Scene::Registry scene;
    const ECS::EntityHandle entity = ECS::Scene::CreateDefault(scene, "presentation mesh");
    AttachMesh(scene, entity);

    Runtime::ProgressiveSlotBinding albedo{};
    albedo.Semantic = Runtime::ProgressiveSlotSemantic::Albedo;
    albedo.SourceKind = Runtime::ProgressiveSlotSourceKind::UniformDefault;
    albedo.UniformDefault.Vector = {0.1f, 0.2f, 0.3f, 1.0f};

    Runtime::ProgressiveSlotBinding normal{};
    normal.Semantic = Runtime::ProgressiveSlotSemantic::Normal;
    normal.SourceKind = Runtime::ProgressiveSlotSourceKind::PropertyBake;
    normal.Property = Runtime::ProgressivePropertyBindingDescriptor{
        .Domain = Runtime::ProgressiveGeometryDomain::MeshVertex,
        .PropertyName = "v:normal",
        .ExpectedValueKind = Runtime::ProgressivePropertyValueKind::Vec3,
        .ExpectedElementCount = 3u,
    };
    normal.GeneratedTexture = Extrinsic::Assets::AssetId{42u, 1u};
    normal.Readiness = Runtime::ProgressiveReadinessState::Pending;

    const Runtime::ProgressivePresentationBindings bindings{
        .Shape = Runtime::ProgressiveEntityShape::MeshLeaf,
        .Lanes = {Runtime::ProgressiveRenderLaneBinding{
            .Lane = Runtime::ProgressiveRenderLane::Surface,
            .PresentationKey = "mesh.surface",
        }},
        .Presentations = {Runtime::ProgressivePresentationBinding{
            .Key = "mesh.surface",
            .Kind = Runtime::ProgressivePresentationKind::SurfaceMaterial,
            .Slots = {albedo, normal},
        }},
    };

    const auto snapshot = Runtime::BuildProgressivePresentationSnapshot(
        GS::BuildConstView(scene.Raw(), entity),
        bindings);

    ASSERT_EQ(snapshot.Slots.size(), 2u);
    EXPECT_EQ(snapshot.Stats.DefaultSlotCount, 1u);
    EXPECT_EQ(snapshot.Stats.ReadyTextureSlotCount, 1u);
    EXPECT_EQ(snapshot.Stats.PreviousOutputRetainedCount, 1u);
    EXPECT_TRUE(snapshot.Slots[1].TextureReady);
    EXPECT_TRUE(snapshot.Slots[1].PreviousOutputRetained);
    EXPECT_NE(snapshot.Slots[1].Diagnostic.find("previous generated texture retained"),
              std::string::npos);
}

TEST(ProgressivePresentationExtraction, GraphVertexAndEdgePropertyBuffersResolveSeparately)
{
    ECS::Scene::Registry scene;
    const ECS::EntityHandle graph = AddGraph(scene);

    const Runtime::ProgressivePresentationBindings bindings{
        .Shape = Runtime::ProgressiveEntityShape::GraphLeaf,
        .Lanes = {
            Runtime::ProgressiveRenderLaneBinding{
                .Lane = Runtime::ProgressiveRenderLane::Points,
                .PresentationKey = "graph.points",
            },
            Runtime::ProgressiveRenderLaneBinding{
                .Lane = Runtime::ProgressiveRenderLane::Edges,
                .PresentationKey = "graph.edges",
            },
        },
        .Presentations = {
            Runtime::ProgressivePresentationBinding{
                .Key = "graph.points",
                .Kind = Runtime::ProgressivePresentationKind::PointPresentation,
                .Slots = {PropertyBufferSlot(Runtime::ProgressiveSlotSemantic::PointColor,
                                             Runtime::ProgressiveGeometryDomain::GraphVertex,
                                             "v:color",
                                             Runtime::ProgressivePropertyValueKind::Vec4,
                                             2u)},
            },
            Runtime::ProgressivePresentationBinding{
                .Key = "graph.edges",
                .Kind = Runtime::ProgressivePresentationKind::LinePresentation,
                .Slots = {PropertyBufferSlot(Runtime::ProgressiveSlotSemantic::LineColor,
                                             Runtime::ProgressiveGeometryDomain::GraphEdge,
                                             "e:color",
                                             Runtime::ProgressivePropertyValueKind::Vec4,
                                             1u)},
            },
        },
    };

    const auto snapshot = Runtime::BuildProgressivePresentationSnapshot(
        GS::BuildConstView(scene.Raw(), graph),
        bindings);

    ASSERT_EQ(snapshot.Slots.size(), 2u);
    EXPECT_EQ(snapshot.Stats.PropertyBufferReadyCount, 2u);
    EXPECT_EQ(snapshot.Slots[0].Property.Domain, Runtime::ProgressiveGeometryDomain::GraphVertex);
    EXPECT_EQ(snapshot.Slots[1].Property.Domain, Runtime::ProgressiveGeometryDomain::GraphEdge);
    EXPECT_TRUE(snapshot.Slots[0].PropertyBufferReady);
    EXPECT_TRUE(snapshot.Slots[1].PropertyBufferReady);
}
