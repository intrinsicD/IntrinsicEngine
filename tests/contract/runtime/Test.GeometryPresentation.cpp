#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <glm/glm.hpp>

import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Scene.Bootstrap;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.GeometryPresentation;

namespace ECS = Extrinsic::ECS;
namespace GS = Extrinsic::ECS::Components::GeometrySources;
namespace Runtime = Extrinsic::Runtime;

namespace
{
    constexpr std::uint32_t kInvalidIndex =
        std::numeric_limits<std::uint32_t>::max();

    void SetPositions(GS::Vertices& vertices,
                      const std::vector<glm::vec3>& positions)
    {
        vertices.Properties.Resize(positions.size());
        vertices.Properties
            .GetOrAdd<glm::vec3>("v:position", glm::vec3{0.0f})
            .Vector() = positions;
    }

    ECS::EntityHandle AddMesh(ECS::Scene::Registry& scene)
    {
        const ECS::EntityHandle entity =
            ECS::Scene::CreateDefault(scene, "presentation mesh");
        auto& raw = scene.Raw();
        auto& vertices = raw.emplace<GS::Vertices>(entity);
        SetPositions(vertices,
                     {{0.0f, 0.0f, 0.0f},
                      {1.0f, 0.0f, 0.0f},
                      {0.0f, 1.0f, 0.0f}});
        vertices.Properties
            .GetOrAdd<glm::vec3>("v:normal", glm::vec3{0.0f, 0.0f, 1.0f})
            .Vector() = {{0.0f, 0.0f, 1.0f},
                         {0.0f, 0.0f, 1.0f},
                         {0.0f, 0.0f, 1.0f}};
        vertices.Properties
            .GetOrAdd<float>("v:heat", 0.0f)
            .Vector() = {0.0f, 0.5f, 1.0f};

        auto& edges = raw.emplace<GS::Edges>(entity);
        edges.Properties.Resize(3u);
        edges.Properties
            .GetOrAdd<std::uint32_t>("e:v0", 0u)
            .Vector() = {0u, 1u, 2u};
        edges.Properties
            .GetOrAdd<std::uint32_t>("e:v1", 0u)
            .Vector() = {1u, 2u, 0u};

        auto& halfedges = raw.emplace<GS::Halfedges>(entity);
        halfedges.Properties.Resize(6u);
        halfedges.Properties
            .GetOrAdd<std::uint32_t>("h:to_vertex", kInvalidIndex)
            .Vector() = {1u, 2u, 0u, 0u, 2u, 1u};
        halfedges.Properties
            .GetOrAdd<std::uint32_t>("h:next", kInvalidIndex)
            .Vector() = {1u, 2u, 0u, 5u, 3u, 4u};
        halfedges.Properties
            .GetOrAdd<std::uint32_t>("h:face", kInvalidIndex)
            .Vector() = {0u, 0u, 0u,
                         kInvalidIndex, kInvalidIndex, kInvalidIndex};

        auto& faces = raw.emplace<GS::Faces>(entity);
        faces.Properties.Resize(1u);
        faces.Properties
            .GetOrAdd<std::uint32_t>("f:halfedge", kInvalidIndex)
            .Vector() = {0u};
        raw.emplace<GS::HasMeshTopology>(entity);
        return entity;
    }

    ECS::EntityHandle AddGraph(ECS::Scene::Registry& scene)
    {
        const ECS::EntityHandle entity =
            ECS::Scene::CreateDefault(scene, "presentation graph");
        auto& raw = scene.Raw();
        auto& nodes = raw.emplace<GS::Nodes>(entity);
        nodes.Properties.Resize(2u);
        nodes.Properties
            .GetOrAdd<glm::vec3>("v:position", glm::vec3{0.0f})
            .Vector() = {{0.0f, 0.0f, 0.0f},
                         {1.0f, 0.0f, 0.0f}};
        nodes.Properties
            .GetOrAdd<glm::vec4>("v:color", glm::vec4{1.0f})
            .Vector() = {{1.0f, 0.0f, 0.0f, 1.0f},
                         {0.0f, 1.0f, 0.0f, 1.0f}};

        auto& edges = raw.emplace<GS::Edges>(entity);
        edges.Properties.Resize(1u);
        edges.Properties
            .GetOrAdd<std::uint32_t>("e:v0", 0u)
            .Vector() = {0u};
        edges.Properties
            .GetOrAdd<std::uint32_t>("e:v1", 0u)
            .Vector() = {1u};
        edges.Properties
            .GetOrAdd<glm::vec4>("e:color", glm::vec4{1.0f})
            .Vector() = {{0.0f, 0.0f, 1.0f, 1.0f}};
        raw.emplace<GS::HasGraphTopology>(entity);
        return entity;
    }

    ECS::EntityHandle AddPointCloud(ECS::Scene::Registry& scene)
    {
        const ECS::EntityHandle entity =
            ECS::Scene::CreateDefault(scene, "presentation point cloud");
        auto& vertices = scene.Raw().emplace<GS::Vertices>(entity);
        SetPositions(vertices,
                     {{0.0f, 0.0f, 0.0f},
                      {0.0f, 1.0f, 0.0f}});
        vertices.Properties
            .GetOrAdd<glm::vec4>("v:color", glm::vec4{1.0f})
            .Vector() = {{1.0f, 0.0f, 0.0f, 1.0f},
                         {0.0f, 1.0f, 0.0f, 1.0f}};
        return entity;
    }

    Runtime::GeometryPresentationSlotRecipe PropertySlot(
        const Runtime::GeometryPresentationSlotSemantic semantic,
        const Runtime::GeometryElementDomain domain,
        std::string name,
        const Geometry::PropertyValueKind kind)
    {
        return Runtime::GeometryPresentationSlotRecipe{
            .Semantic = semantic,
            .SourceKind =
                Runtime::GeometryPresentationSourceKind::PropertyBuffer,
            .Property = Runtime::GeometryPropertyRef{
                .Domain = domain,
                .Name = std::move(name),
                .ValueKind = kind,
            },
        };
    }
}

TEST(GeometryPresentation,
     RecipeIntentAndRuntimeStatusProjectIntoGenerationQualifiedSnapshot)
{
    ECS::Scene::Registry scene{};
    const ECS::EntityHandle mesh = AddMesh(scene);

    Runtime::GeometryPresentationSlotRecipe albedo{};
    albedo.Semantic =
        Runtime::GeometryPresentationSlotSemantic::Albedo;
    albedo.SourceKind =
        Runtime::GeometryPresentationSourceKind::UniformDefault;
    albedo.UniformDefault.Vector = {0.2f, 0.4f, 0.6f, 1.0f};

    Runtime::GeometryPresentationSlotRecipe normal{};
    normal.Semantic = Runtime::GeometryPresentationSlotSemantic::Normal;
    normal.SourceKind =
        Runtime::GeometryPresentationSourceKind::PropertyBake;
    normal.Property = Runtime::GeometryPropertyRef{
        .Domain = Runtime::GeometryElementDomain::MeshVertex,
        .Name = "v:normal",
        .ValueKind = Geometry::PropertyValueKind::Vec3,
    };

    const Runtime::GeometryPresentationRecipe recipe{
        .Shape = Runtime::GeometryPresentationShape::Mesh,
        .Lanes = {Runtime::GeometryPresentationLaneRecipe{
            .Lane = Runtime::GeometryRenderLane::Surface,
            .PresentationKey = "mesh.surface",
        }},
        .Presentations = {Runtime::GeometryPresentationBindingRecipe{
            .Key = "mesh.surface",
            .Kind = Runtime::GeometryPresentationKind::SurfaceMaterial,
            .Slots = {albedo, normal},
        }},
    };
    const Runtime::GeometryPresentationRuntimeState state{
        .RecipeGeneration = 4u,
        .Slots = {Runtime::GeometryPresentationSlotStatus{
            .PresentationKey = "mesh.surface",
            .Semantic = Runtime::GeometryPresentationSlotSemantic::Normal,
            .Readiness = Runtime::GeometryPresentationReadiness::Pending,
            .GeneratedTexture = Extrinsic::Assets::AssetId{42u, 1u},
            .Provenance =
                Runtime::GeometryPresentationProvenance::PropertyBinding,
            .SourceGeneration = 8u,
            .OutputGeneration = 3u,
            .Diagnostic = "rebake queued",
        }},
    };

    const Runtime::GeometryPresentationSnapshot snapshot =
        Runtime::BuildGeometryPresentationSnapshot(
            GS::BuildConstView(scene.Raw(), mesh),
            recipe,
            state,
            9u);

    EXPECT_EQ(snapshot.Shape,
              Runtime::GeometryPresentationShape::Mesh);
    EXPECT_EQ(snapshot.RecipeGeneration, 4u);
    EXPECT_EQ(snapshot.ObservedSourceGeneration, 9u);
    ASSERT_EQ(snapshot.Slots.size(), 2u);
    EXPECT_TRUE(snapshot.Slots[0].UsesUniformDefault);
    EXPECT_EQ(snapshot.Slots[0].Readiness,
              Runtime::GeometryPresentationReadiness::DefaultValue);

    const Runtime::GeometryPresentationSlotSnapshot& projectedNormal =
        snapshot.Slots[1];
    EXPECT_TRUE(projectedNormal.PropertyResolution.Resolved());
    EXPECT_EQ(projectedNormal.Readiness,
              Runtime::GeometryPresentationReadiness::Stale);
    EXPECT_TRUE(projectedNormal.TextureReady);
    EXPECT_TRUE(projectedNormal.PreviousOutputRetained);
    EXPECT_EQ(projectedNormal.Fallback,
              Runtime::GeometryPresentationFallback::
                  PreviousGeneratedTexture);
    EXPECT_EQ(projectedNormal.SourceGeneration, 8u);
    EXPECT_EQ(projectedNormal.OutputGeneration, 3u);
    EXPECT_NE(projectedNormal.Diagnostic.find("source generation is stale"),
              std::string::npos);
    EXPECT_NE(projectedNormal.Diagnostic.find("rebake queued"),
              std::string::npos);
}

TEST(GeometryPresentation,
     GraphPointAndLineSlotsUseCanonicalPropertyReferences)
{
    ECS::Scene::Registry scene{};
    const ECS::EntityHandle graph = AddGraph(scene);
    const Runtime::GeometryPresentationRecipe recipe{
        .Shape = Runtime::GeometryPresentationShape::Graph,
        .Lanes = {
            Runtime::GeometryPresentationLaneRecipe{
                .Lane = Runtime::GeometryRenderLane::Points,
                .PresentationKey = "graph.points",
            },
            Runtime::GeometryPresentationLaneRecipe{
                .Lane = Runtime::GeometryRenderLane::Edges,
                .PresentationKey = "graph.edges",
            },
        },
        .Presentations = {
            Runtime::GeometryPresentationBindingRecipe{
                .Key = "graph.points",
                .Kind =
                    Runtime::GeometryPresentationKind::PointPresentation,
                .Slots = {PropertySlot(
                    Runtime::GeometryPresentationSlotSemantic::PointColor,
                    Runtime::GeometryElementDomain::GraphNode,
                    "v:color",
                    Geometry::PropertyValueKind::Vec4)},
            },
            Runtime::GeometryPresentationBindingRecipe{
                .Key = "graph.edges",
                .Kind =
                    Runtime::GeometryPresentationKind::LinePresentation,
                .Slots = {PropertySlot(
                    Runtime::GeometryPresentationSlotSemantic::LineColor,
                    Runtime::GeometryElementDomain::GraphEdge,
                    "e:color",
                    Geometry::PropertyValueKind::Vec4)},
            },
        },
    };

    const Runtime::GeometryPresentationSnapshot snapshot =
        Runtime::BuildGeometryPresentationSnapshot(
            GS::BuildConstView(scene.Raw(), graph),
            recipe,
            Runtime::GeometryPresentationRuntimeState{
                .RecipeGeneration = 7u,
            },
            11u);

    ASSERT_EQ(snapshot.Slots.size(), 2u);
    EXPECT_EQ(snapshot.Stats.PropertyBufferReadyCount, 2u);
    EXPECT_EQ(snapshot.Slots[0].Property,
              (Runtime::GeometryPropertyRef{
                  .Domain = Runtime::GeometryElementDomain::GraphNode,
                  .Name = "v:color",
                  .ValueKind = Geometry::PropertyValueKind::Vec4,
              }));
    EXPECT_EQ(snapshot.Slots[1].Property.Domain,
              Runtime::GeometryElementDomain::GraphEdge);
    EXPECT_EQ(snapshot.Slots[0].PropertyResolution.ObservedSourceGeneration,
              11u);
    EXPECT_TRUE(snapshot.Slots[0].PropertyBufferReady);
    EXPECT_TRUE(snapshot.Slots[1].PropertyBufferReady);
}

TEST(GeometryPresentation,
     PropertyOptionsAreDeterministicCompatibleFirstCanonicalRefs)
{
    ECS::Scene::Registry scene{};
    const ECS::EntityHandle mesh = AddMesh(scene);

    const auto options =
        Runtime::EnumerateGeometryPresentationPropertyOptions(
            GS::BuildConstView(scene.Raw(), mesh),
            Runtime::GeometryElementDomain::MeshVertex,
            Geometry::PropertyValueKind::Vec3,
            23u);

    ASSERT_GE(options.size(), 3u);
    EXPECT_TRUE(options[0].Compatible);
    EXPECT_TRUE(options[1].Compatible);
    EXPECT_LT(options[0].Property.Name, options[1].Property.Name);
    EXPECT_EQ(options[0].SourceGeneration, 23u);
    EXPECT_EQ(options.back().Property.Name, "v:heat");
    EXPECT_FALSE(options.back().Compatible);
    EXPECT_FALSE(options.back().DisabledReason.empty());
}

TEST(GeometryPresentation,
     PointCloudAndProceduralShapesUseTheSameProjection)
{
    ECS::Scene::Registry scene{};
    const ECS::EntityHandle cloud = AddPointCloud(scene);
    const Runtime::GeometryPresentationRecipe cloudRecipe{
        .Shape = Runtime::GeometryPresentationShape::PointCloud,
        .Lanes = {Runtime::GeometryPresentationLaneRecipe{
            .Lane = Runtime::GeometryRenderLane::Points,
            .PresentationKey = "cloud.points",
        }},
        .Presentations = {Runtime::GeometryPresentationBindingRecipe{
            .Key = "cloud.points",
            .Kind = Runtime::GeometryPresentationKind::PointPresentation,
            .Slots = {PropertySlot(
                Runtime::GeometryPresentationSlotSemantic::PointColor,
                Runtime::GeometryElementDomain::PointCloudPoint,
                "v:color",
                Geometry::PropertyValueKind::Vec4)},
        }},
    };
    const auto cloudSnapshot = Runtime::BuildGeometryPresentationSnapshot(
        GS::BuildConstView(scene.Raw(), cloud),
        cloudRecipe);
    ASSERT_EQ(cloudSnapshot.Slots.size(), 1u);
    EXPECT_TRUE(cloudSnapshot.Slots[0].PropertyBufferReady);

    const Runtime::GeometryPresentationRecipe proceduralRecipe{
        .Shape = Runtime::GeometryPresentationShape::Procedural,
        .Lanes = {Runtime::GeometryPresentationLaneRecipe{
            .Lane = Runtime::GeometryRenderLane::Surface,
            .PresentationKey = "procedural.surface",
        }},
        .Presentations = {Runtime::GeometryPresentationBindingRecipe{
            .Key = "procedural.surface",
            .Kind = Runtime::GeometryPresentationKind::SurfaceMaterial,
            .Slots = {Runtime::GeometryPresentationSlotRecipe{}},
        }},
    };
    const auto proceduralSnapshot =
        Runtime::BuildGeometryPresentationSnapshot(
            GS::ConstSourceView{},
            proceduralRecipe);
    ASSERT_EQ(proceduralSnapshot.Slots.size(), 1u);
    EXPECT_EQ(proceduralSnapshot.Shape,
              Runtime::GeometryPresentationShape::Procedural);
    EXPECT_TRUE(proceduralSnapshot.Slots[0].UsesUniformDefault);
    EXPECT_EQ(proceduralSnapshot.Slots[0].Readiness,
              Runtime::GeometryPresentationReadiness::DefaultValue);
}

TEST(GeometryPresentation, LegacyShapeSpellingsRemainReadCompatible)
{
    Runtime::GeometryPresentationShape shape{
        Runtime::GeometryPresentationShape::Unknown};
    EXPECT_TRUE(Runtime::TryParseGeometryPresentationShape("MeshLeaf",
                                                           shape));
    EXPECT_EQ(shape, Runtime::GeometryPresentationShape::Mesh);
    EXPECT_TRUE(Runtime::TryParseGeometryPresentationShape("GraphLeaf",
                                                           shape));
    EXPECT_EQ(shape, Runtime::GeometryPresentationShape::Graph);
    EXPECT_TRUE(Runtime::TryParseGeometryPresentationShape("PointCloudLeaf",
                                                           shape));
    EXPECT_EQ(shape, Runtime::GeometryPresentationShape::PointCloud);
    EXPECT_FALSE(Runtime::TryParseGeometryPresentationShape("Bogus", shape));
}
