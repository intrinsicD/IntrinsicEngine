// ARCH-006 runtime Sandbox editor Visualization contract partition.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include <entt/entity/entity.hpp>
#include <gtest/gtest.h>
#include <glm/gtc/quaternion.hpp>
#include "ProgressivePoissonReference.hpp"

import Extrinsic.Asset.ImportRouter;
import Extrinsic.Asset.ModelTexturePayload;
import Extrinsic.Asset.Registry;
import Extrinsic.Asset.Service;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Config.Window;
import Extrinsic.Core.Error;
import Extrinsic.Core.Geometry2D;
import Extrinsic.Core.Logging;
import Extrinsic.ECS.Component.Culling.Local;
import Extrinsic.ECS.Component.Culling.World;
import Extrinsic.ECS.Component.Hierarchy;
import Extrinsic.ECS.Component.MetaData;
import Extrinsic.ECS.Component.SpatialDebugBinding;
import Extrinsic.ECS.Component.StableId;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Extrinsic.ECS.Components.Selection;
import Extrinsic.ECS.Hierarchy.Mutation;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.Colormap;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Graphics.Material;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.CurrentRendererContractAdapter;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.RenderGraph;
import Extrinsic.Graphics.RenderRecipeConfig;
import Extrinsic.Graphics.RenderingContract;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Platform.Input;
import Extrinsic.Platform.Window;
import Extrinsic.RHI.Device;
import Extrinsic.Runtime.AssetImportPipeline;
import Extrinsic.Runtime.AssetIngestStateMachine;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.DerivedJobGraph;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EditorPropertyWidgets;
import Extrinsic.Runtime.EditorWindowRegistry;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.AssetWorkflowModule;
import Extrinsic.Runtime.SceneDocumentModule;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.MeshAttributeTextureBake;
import Extrinsic.Runtime.MeshPrimitiveViewPacker;
import Extrinsic.Runtime.ProgressiveRenderData;
import Extrinsic.Runtime.PrimitiveSelectionRefinement;
import Extrinsic.Runtime.RenderArtifactPublication;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.SandboxDefaultPolicies;
import Extrinsic.Runtime.SandboxEditorFacades;
import Extrinsic.Runtime.SceneInteractionModule;
import Extrinsic.Runtime.SceneSerialization;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.SelectedMeshTextureBake;
import Extrinsic.Runtime.StreamingExecutor;
import Extrinsic.Runtime.VertexAttributeBinding;
import Extrinsic.Runtime.VertexChannelBindings;
import Geometry.Graph.Vertex.Normals;
import Geometry.HalfedgeMesh;
import Geometry.HalfedgeMesh.Builder;
import Geometry.HalfedgeMesh.Vertices.Normals;
import Geometry.KMeans;
import Geometry.PointCloud.Normals;
import Geometry.Properties;
import Geometry.Smoothing;
import Geometry.UvAtlas;

#include "MockRHI.hpp"

namespace Runtime = Extrinsic::Runtime;
namespace Assets = Extrinsic::Assets;
namespace Core = Extrinsic::Core;
namespace ECS = Extrinsic::ECS;
namespace ECSC = Extrinsic::ECS::Components;
namespace Dirty = Extrinsic::ECS::Components::DirtyTags;
namespace GS = Extrinsic::ECS::Components::GeometrySources;
namespace Sel = Extrinsic::ECS::Components::Selection;
namespace G = Extrinsic::Graphics::Components;
namespace Graphics = Extrinsic::Graphics;
namespace Plat = Extrinsic::Platform;
namespace PN = Extrinsic::ECS::Components::GeometrySources::PropertyNames;
namespace GN = Geometry::HalfedgeMesh::VertexNormals;
namespace GVN = Geometry::Graph::VertexNormals;
namespace PCN = Geometry::PointCloud::Normals;
namespace Smooth = Geometry::Smoothing;
namespace PPR = Intrinsic::Methods::Geometry::ProgressivePoissonReference;
namespace Tests = Extrinsic::Tests;

namespace
{
    template <typename T>
    [[nodiscard]] T& RequiredEngineService(
        Extrinsic::Runtime::Engine& engine)
    {
        T* const service = engine.Services().Find<T>();
        EXPECT_NE(service, nullptr);
        return *service;
    }

constexpr std::uint32_t kInvalidIndex =
        std::numeric_limits<std::uint32_t>::max();

[[nodiscard]] ECS::EntityHandle MakeSelectable(
        ECS::Scene::Registry& registry,
        std::string name)
    {
        const ECS::EntityHandle entity = registry.Create();
        auto& raw = registry.Raw();
        raw.emplace<ECSC::MetaData>(entity, std::move(name));
        raw.emplace<ECSC::Transform::Component>(entity);
        raw.emplace<ECSC::Transform::WorldMatrix>(entity);
        raw.emplace<Sel::SelectableTag>(entity);
        return entity;
    }

void AddPointCloudSource(ECS::Scene::Registry& registry,
                             const ECS::EntityHandle entity,
                             const std::size_t pointCount)
    {
        auto& vertices = registry.Raw().emplace<GS::Vertices>(entity);
        vertices.Properties.Resize(pointCount);
        registry.Raw().emplace<G::RenderPoints>(entity);
    }

void SetNodePositions(GS::Nodes& nodes,
                          const std::vector<glm::vec3>& positions)
    {
        nodes.Properties.Resize(positions.size());
        auto pos = nodes.Properties.GetOrAdd<glm::vec3>(
            std::string{PN::kPosition},
            glm::vec3{0.0f});
        pos.Vector() = positions;
    }

void SetPositions(GS::Vertices& vertices,
                      const std::vector<glm::vec3>& positions)
    {
        vertices.Properties.Resize(positions.size());
        auto pos = vertices.Properties.GetOrAdd<glm::vec3>(
            std::string{PN::kPosition},
            glm::vec3{0.0f});
        pos.Vector() = positions;
    }

void SetTexcoords(GS::Vertices& vertices,
                      const std::vector<glm::vec2>& texcoords)
    {
        auto uv = vertices.Properties.GetOrAdd<glm::vec2>(
            "v:texcoord",
            glm::vec2{0.0f});
        uv.Vector() = texcoords;
    }

    // A small deterministic, asymmetric point lattice — distinct extents per axis
    // give ICP a well-conditioned correspondence problem (UI-029).

[[nodiscard]] const Runtime::SandboxEditorVertexChannelBindingTargetModel*
    FindVertexChannelTarget(
        const Runtime::SandboxEditorPropertyCatalogModel& catalog,
        const Runtime::VertexChannel channel)
    {
        for (const Runtime::SandboxEditorVertexChannelBindingTargetModel& target :
             catalog.VertexChannelTargets)
        {
            if (target.Channel == channel)
                return &target;
        }
        return nullptr;
    }

void SetVec3Property(Geometry::PropertySet& properties,
                         const std::string& name,
                         const std::vector<glm::vec3>& values)
    {
        auto prop = properties.GetOrAdd<glm::vec3>(name, glm::vec3{0.0f});
        prop.Vector() = values;
    }

void SetVec4Property(Geometry::PropertySet& properties,
                         const std::string& name,
                         const std::vector<glm::vec4>& values)
    {
        auto prop = properties.GetOrAdd<glm::vec4>(name, glm::vec4{1.0f});
        prop.Vector() = values;
    }

void SetFloatProperty(Geometry::PropertySet& properties,
                          const std::string& name,
                          const std::vector<float>& values)
    {
        auto prop = properties.GetOrAdd<float>(name, 0.0f);
        prop.Vector() = values;
    }

void SetEdges(GS::Edges& edges,
                  const std::vector<std::uint32_t>& v0,
                  const std::vector<std::uint32_t>& v1)
    {
        edges.Properties.Resize(v0.size());
        auto p0 = edges.Properties.GetOrAdd<std::uint32_t>(
            std::string{PN::kEdgeV0},
            0u);
        auto p1 = edges.Properties.GetOrAdd<std::uint32_t>(
            std::string{PN::kEdgeV1},
            0u);
        p0.Vector() = v0;
        p1.Vector() = v1;
    }

void SetHalfedges(GS::Halfedges& halfedges,
                      const std::vector<std::uint32_t>& toVertex,
                      const std::vector<std::uint32_t>& next,
                      const std::vector<std::uint32_t>& face)
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
        to.Vector() = toVertex;
        nx.Vector() = next;
        fa.Vector() = face;
    }

void SetFaces(GS::Faces& faces,
                  const std::vector<std::uint32_t>& faceHalfedge)
    {
        faces.Properties.Resize(faceHalfedge.size());
        auto halfedge = faces.Properties.GetOrAdd<std::uint32_t>(
            std::string{PN::kFaceHalfedge},
            kInvalidIndex);
        halfedge.Vector() = faceHalfedge;
    }

void AddTriangleMeshSource(ECS::Scene::Registry& registry,
                               const ECS::EntityHandle entity)
    {
        auto& raw = registry.Raw();
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
    }

[[nodiscard]] Runtime::ProgressivePresentationBindings
    MakeProgressiveMeshPresentationBindings()
    {
        Runtime::ProgressiveSlotBinding albedo{};
        albedo.Semantic = Runtime::ProgressiveSlotSemantic::Albedo;
        albedo.SourceKind = Runtime::ProgressiveSlotSourceKind::UniformDefault;
        albedo.UniformDefault = Runtime::ProgressiveDefaultValue{
            .Kind = Runtime::ProgressivePropertyValueKind::Vec4,
            .Vector = glm::vec4{0.2f, 0.4f, 0.8f, 1.0f},
        };
        albedo.Readiness = Runtime::ProgressiveReadinessState::DefaultValue;
        albedo.Provenance =
            Runtime::ProgressiveGeneratedOutputProvenance::UniformDefault;

        Runtime::ProgressiveSlotBinding normal{};
        normal.Semantic = Runtime::ProgressiveSlotSemantic::Normal;
        normal.SourceKind = Runtime::ProgressiveSlotSourceKind::PropertyBake;
        normal.Property = Runtime::ProgressivePropertyBindingDescriptor{
            .Domain = Runtime::ProgressiveGeometryDomain::MeshVertex,
            .PropertyName = "v:normal",
            .ExpectedValueKind = Runtime::ProgressivePropertyValueKind::Vec3,
            .ExpectedElementCount = 3u,
        };
        normal.Readiness = Runtime::ProgressiveReadinessState::Pending;
        normal.GeneratedPolicy =
            Runtime::ProgressiveGeneratedOutputPolicy::DeterministicChildAsset;
        normal.Provenance =
            Runtime::ProgressiveGeneratedOutputProvenance::PropertyBinding;
        normal.LastDiagnostic = "waiting for normal bake";

        return Runtime::ProgressivePresentationBindings{
            .Shape = Runtime::ProgressiveEntityShape::MeshLeaf,
            .Lanes = {
                Runtime::ProgressiveRenderLaneBinding{
                    .Lane = Runtime::ProgressiveRenderLane::Surface,
                    .PresentationKey = "mesh.surface",
                },
            },
            .Presentations = {
                Runtime::ProgressivePresentationBinding{
                    .Key = "mesh.surface",
                    .Kind = Runtime::ProgressivePresentationKind::SurfaceMaterial,
                    .Slots = {albedo, normal},
                },
            },
            .BindingGeneration = 7u,
        };
    }

void AddGraphSource(ECS::Scene::Registry& registry,
                        const ECS::EntityHandle entity)
    {
        auto& raw = registry.Raw();
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
        raw.emplace<G::RenderPoints>(entity);
    }

[[nodiscard]] Runtime::SandboxEditorContext MakeContext(
        ECS::Scene::Registry& registry,
        Runtime::SelectionController& selection,
        const bool imguiAvailable = true,
        const std::optional<Runtime::PrimitiveSelectionResult>* lastPrimitive = nullptr,
        Extrinsic::RHI::IDevice* device = nullptr)
    {
        return Runtime::SandboxEditorContext{
            .Scene = &registry,
            .Selection = &selection,
            .LastRefinedPrimitive = lastPrimitive,
            .Device = device,
            .ImGuiAdapterAvailable = imguiAvailable,
            .AssetImportCommandsAvailable = false,
            .CameraRenderCommandsAvailable = false,
            .VisualizationCommandsAvailable = false,
        };
    }

class PassiveApplication final : public Runtime::IApplication
    {
    public:
        void OnInitialize(Runtime::Engine&) override {}
        void OnSimTick(Runtime::Engine&, double) override {}
        void OnVariableTick(Runtime::Engine&, double, double) override {}
        void OnShutdown(Runtime::Engine&) override {}
    };

[[nodiscard]] Extrinsic::Core::Config::EngineConfig HeadlessConfig()
    {
        Extrinsic::Core::Config::EngineConfig config{};
        config.Simulation.WorkerThreadCount = 1u;
        config.ReferenceScene.Enabled = false;
        config.Camera.Enabled = false;
        config.Window.Backend = Core::Config::WindowBackend::Null;
        return config;
    }
}
TEST(SandboxEditorUi, VertexChannelBindingCommandRebindsNormalsForMeshGraphAndPointCloud)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);

    const auto exercise =
        [&](const ECS::EntityHandle entity,
            const Runtime::SandboxEditorDomainWindowKind windowKind,
            const Runtime::SandboxEditorPropertyCatalogDomain catalogDomain,
            Geometry::PropertySet& properties,
            const std::size_t expectedCount)
        {
            SetVec3Property(
                properties,
                "v:custom_normal",
                std::vector<glm::vec3>(expectedCount, glm::vec3{1.0f, 0.0f, 0.0f}));
            SetFloatProperty(
                properties,
                "v:temperature",
                std::vector<float>(expectedCount, 0.5f));

            ASSERT_TRUE(selection.SetSelectedEntity(registry, entity));
            Runtime::SandboxEditorPanelFrame frame =
                Runtime::BuildSandboxEditorPanelFrame(context);
            const auto* normalTarget = FindVertexChannelTarget(
                frame.Inspector.PropertyCatalog,
                Runtime::VertexChannel::Normal);
            ASSERT_NE(normalTarget, nullptr);
            ASSERT_EQ(frame.Inspector.PropertyCatalog.SelectedStableId,
                      Runtime::SelectionController::ToStableEntityId(entity));

            const auto customOption = std::find_if(
                normalTarget->Options.begin(),
                normalTarget->Options.end(),
                [catalogDomain](const Runtime::SandboxEditorVertexChannelBindingOptionModel& option)
                {
                    return option.Domain == catalogDomain &&
                           option.PropertyName == "v:custom_normal";
                });
            ASSERT_NE(customOption, normalTarget->Options.end());
            EXPECT_TRUE(customOption->Compatible);
            EXPECT_EQ(customOption->ElementCount, expectedCount);

            registry.Raw().remove<Dirty::DirtyVertexAttributes,
                                  Dirty::DirtyVertexNormals>(entity);
            const Runtime::SandboxEditorCommandStatus status =
                Runtime::ApplySandboxEditorVertexChannelBindingCommand(
                    context,
                    Runtime::SandboxEditorVertexChannelBindingCommand{
                        .StableEntityId =
                            Runtime::SelectionController::ToStableEntityId(entity),
                        .Channel = Runtime::VertexChannel::Normal,
                        .EnableBinding = true,
                        .PropertyName = "v:custom_normal",
                    });
            EXPECT_EQ(status, Runtime::SandboxEditorCommandStatus::Applied);

            const auto* bindings =
                registry.Raw().try_get<Runtime::VertexChannelBindingSet>(entity);
            ASSERT_NE(bindings, nullptr);
            EXPECT_TRUE(bindings->Normal.Enabled);
            EXPECT_EQ(bindings->Normal.SourceType,
                      Runtime::AttributeSourceType::Vec3);
            EXPECT_EQ(bindings->Normal.SourceProperty, "v:custom_normal");
            EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexNormals>(entity));
            EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(entity));

            const Runtime::SandboxEditorCommandStatus invalid =
                Runtime::ApplySandboxEditorVertexChannelBindingCommand(
                    context,
                    Runtime::SandboxEditorVertexChannelBindingCommand{
                        .StableEntityId =
                            Runtime::SelectionController::ToStableEntityId(entity),
                        .Channel = Runtime::VertexChannel::Normal,
                        .EnableBinding = true,
                        .PropertyName = "v:temperature",
                    });
            EXPECT_EQ(invalid,
                      Runtime::SandboxEditorCommandStatus::InvalidVertexChannelBinding);

            const Runtime::SandboxEditorDomainWindowModel model =
                Runtime::BuildSandboxEditorDomainWindowModel(context, windowKind);
            const auto* domainTarget = FindVertexChannelTarget(
                model.PropertyCatalog,
                Runtime::VertexChannel::Normal);
            ASSERT_NE(domainTarget, nullptr);
            EXPECT_TRUE(domainTarget->HasBinding);
            EXPECT_EQ(domainTarget->Binding.SourceProperty, "v:custom_normal");
        };

    const ECS::EntityHandle mesh = MakeSelectable(registry, "Mesh");
    AddTriangleMeshSource(registry, mesh);
    exercise(mesh,
             Runtime::SandboxEditorDomainWindowKind::Mesh,
             Runtime::SandboxEditorPropertyCatalogDomain::MeshVertices,
             registry.Raw().get<GS::Vertices>(mesh).Properties,
             3u);

    const ECS::EntityHandle graph = MakeSelectable(registry, "Graph");
    AddGraphSource(registry, graph);
    exercise(graph,
             Runtime::SandboxEditorDomainWindowKind::Graph,
             Runtime::SandboxEditorPropertyCatalogDomain::GraphVertices,
             registry.Raw().get<GS::Nodes>(graph).Properties,
             3u);

    const ECS::EntityHandle cloud = MakeSelectable(registry, "Cloud");
    AddPointCloudSource(registry, cloud, 3u);
    SetPositions(registry.Raw().get<GS::Vertices>(cloud),
                 {
                     {0.0f, 0.0f, 0.0f},
                     {1.0f, 0.0f, 0.0f},
                     {0.0f, 1.0f, 0.0f},
                 });
    exercise(cloud,
             Runtime::SandboxEditorDomainWindowKind::PointCloud,
             Runtime::SandboxEditorPropertyCatalogDomain::PointCloudPoints,
             registry.Raw().get<GS::Vertices>(cloud).Properties,
             3u);
}
TEST(SandboxEditorUi, VertexChannelBindingCommandBindsColorAndDisablesCleanly)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);

    const ECS::EntityHandle mesh = MakeSelectable(registry, "ColorMesh");
    AddTriangleMeshSource(registry, mesh);
    auto& properties = registry.Raw().get<GS::Vertices>(mesh).Properties;
    SetVec4Property(properties,
                    "v:paint",
                    {
                        {1.0f, 0.0f, 0.0f, 1.0f},
                        {0.0f, 1.0f, 0.0f, 1.0f},
                        {0.0f, 0.0f, 1.0f, 1.0f},
                    });
    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));

    Runtime::SandboxEditorPanelFrame frame =
        Runtime::BuildSandboxEditorPanelFrame(context);
    const auto* colorTarget = FindVertexChannelTarget(
        frame.Inspector.PropertyCatalog,
        Runtime::VertexChannel::Color);
    ASSERT_NE(colorTarget, nullptr);
    const auto paintOption = std::find_if(
        colorTarget->Options.begin(),
        colorTarget->Options.end(),
        [](const Runtime::SandboxEditorVertexChannelBindingOptionModel& option)
        {
            return option.PropertyName == "v:paint";
        });
    ASSERT_NE(paintOption, colorTarget->Options.end());
    EXPECT_TRUE(paintOption->Compatible);

    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);
    EXPECT_EQ(
        Runtime::ApplySandboxEditorVertexChannelBindingCommand(
            context,
            Runtime::SandboxEditorVertexChannelBindingCommand{
                .StableEntityId = stableId,
                .Channel = Runtime::VertexChannel::Color,
                .EnableBinding = true,
                .PropertyName = "v:paint",
            }),
        Runtime::SandboxEditorCommandStatus::Applied);
    const auto* bindings =
        registry.Raw().try_get<Runtime::VertexChannelBindingSet>(mesh);
    ASSERT_NE(bindings, nullptr);
    EXPECT_TRUE(bindings->Color.Enabled);
    EXPECT_EQ(bindings->Color.SourceType, Runtime::AttributeSourceType::Vec4);
    EXPECT_EQ(bindings->Color.SourceProperty, "v:paint");
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexColors>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));

    registry.Raw().remove<Dirty::DirtyVertexColors>(mesh);

    EXPECT_EQ(
        Runtime::ApplySandboxEditorVertexChannelBindingCommand(
            context,
            Runtime::SandboxEditorVertexChannelBindingCommand{
                .StableEntityId = stableId,
                .Channel = Runtime::VertexChannel::Color,
                .EnableBinding = false,
            }),
        Runtime::SandboxEditorCommandStatus::Applied);
    EXPECT_FALSE(registry.Raw().all_of<Runtime::VertexChannelBindingSet>(mesh));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexColors>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));
}
TEST(SandboxEditorUi, VisualizationPropertyPresetCommandRoutesThroughConfig)
{
    using Domain = Runtime::SandboxEditorVisualizationPropertyDomain;
    using Preset = Runtime::SandboxEditorVisualizationPropertyPreset;

    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    const ECS::EntityHandle mesh = MakeSelectable(registry, "PresetMesh");
    AddTriangleMeshSource(registry, mesh);
    auto& vertices = registry.Raw().get<GS::Vertices>(mesh);
    vertices.Properties.GetOrAdd<float>("v:temperature", 0.0f)
        .Vector() = {0.0f, 0.5f, 1.0f};
    vertices.Properties.GetOrAdd<glm::vec4>("v:kmeans_color", glm::vec4{1.0f})
        .Vector() = {glm::vec4{1.0f}, glm::vec4{0.0f, 1.0f, 0.0f, 1.0f}, glm::vec4{0.0f, 0.0f, 1.0f, 1.0f}};
    vertices.Properties.GetOrAdd<std::uint32_t>("v:kmeans_label", 0u)
        .Vector() = {0u, 1u, 1u};
    auto& edges = registry.Raw().get<GS::Edges>(mesh);
    edges.Properties.GetOrAdd<glm::vec4>("e:debug_color", glm::vec4{1.0f})
        .Vector() = {glm::vec4{1.0f}, glm::vec4{1.0f}, glm::vec4{1.0f}};

    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.VisualizationCommandsAvailable = true;

    EXPECT_EQ(Runtime::ApplySandboxEditorVisualizationPropertyCommand(
                  context,
                  Runtime::SandboxEditorVisualizationPropertyCommand{
                      .StableEntityId = stableId,
                      .Domain = Domain::MeshVertices,
                      .Preset = Preset::Scalar,
                      .PropertyName = "v:temperature",
                      .ScalarAutoRange = false,
                      .ScalarRangeMin = -1.0f,
                      .ScalarRangeMax = 2.0f,
                      .ScalarBinCount = 8u,
                  }),
              Runtime::SandboxEditorCommandStatus::Applied);
    ASSERT_TRUE(registry.Raw().all_of<G::VisualizationConfig>(mesh));
    const auto& scalar = registry.Raw().get<G::VisualizationConfig>(mesh);
    EXPECT_EQ(scalar.Source, G::VisualizationConfig::ColorSource::ScalarField);
    EXPECT_EQ(scalar.ScalarFieldName, "v:temperature");
    EXPECT_EQ(scalar.ScalarDomain, G::VisualizationConfig::Domain::Vertex);
    EXPECT_FALSE(scalar.Scalar.AutoRange);
    EXPECT_FLOAT_EQ(scalar.Scalar.RangeMin, -1.0f);
    EXPECT_FLOAT_EQ(scalar.Scalar.RangeMax, 2.0f);
    EXPECT_EQ(scalar.Scalar.BinCount, 8u);

    EXPECT_EQ(Runtime::ApplySandboxEditorVisualizationPropertyCommand(
                  context,
                  Runtime::SandboxEditorVisualizationPropertyCommand{
                      .StableEntityId = stableId,
                      .Domain = Domain::MeshVertices,
                      .Preset = Preset::Scalar,
                      .PropertyName = "v:temperature",
                      .ScalarAutoRange = false,
                      .ScalarRangeMin = -1.0f,
                      .ScalarRangeMax = 2.0f,
                      .ScalarBinCount = 8u,
                  }),
              Runtime::SandboxEditorCommandStatus::NoChange);

    EXPECT_EQ(Runtime::ApplySandboxEditorVisualizationPropertyCommand(
                  context,
                  Runtime::SandboxEditorVisualizationPropertyCommand{
                      .StableEntityId = stableId,
                      .Domain = Domain::MeshVertices,
                      .Preset = Preset::Isoline,
                      .PropertyName = "v:temperature",
                      .IsolineCount = 6u,
                  }),
              Runtime::SandboxEditorCommandStatus::Applied);
    const auto& isoline = registry.Raw().get<G::VisualizationConfig>(mesh);
    EXPECT_EQ(isoline.Source, G::VisualizationConfig::ColorSource::ScalarField);
    EXPECT_EQ(isoline.ScalarFieldName, "v:temperature");
    EXPECT_EQ(isoline.Scalar.Isolines.Num, 6u);

    EXPECT_EQ(Runtime::ApplySandboxEditorVisualizationPropertyCommand(
                  context,
                  Runtime::SandboxEditorVisualizationPropertyCommand{
                      .StableEntityId = stableId,
                      .Domain = Domain::MeshVertices,
                      .Preset = Preset::ColorBuffer,
                      .PropertyName = "v:kmeans_color",
                  }),
              Runtime::SandboxEditorCommandStatus::Applied);
    const auto& vertexColor = registry.Raw().get<G::VisualizationConfig>(mesh);
    EXPECT_EQ(vertexColor.Source,
              G::VisualizationConfig::ColorSource::PerVertexBuffer);
    EXPECT_EQ(vertexColor.ColorBufferName, "v:kmeans_color");

    EXPECT_EQ(Runtime::ApplySandboxEditorVisualizationPropertyCommand(
                  context,
                  Runtime::SandboxEditorVisualizationPropertyCommand{
                      .StableEntityId = stableId,
                      .Domain = Domain::MeshEdges,
                      .Preset = Preset::ColorBuffer,
                      .PropertyName = "e:debug_color",
                  }),
              Runtime::SandboxEditorCommandStatus::Applied);
    const auto& edgeColor = registry.Raw().get<G::VisualizationConfig>(mesh);
    EXPECT_EQ(edgeColor.Source,
              G::VisualizationConfig::ColorSource::PerEdgeBuffer);
    EXPECT_EQ(edgeColor.ColorBufferName, "e:debug_color");

    const G::VisualizationConfig invalidBaseline = edgeColor;
    const auto expectVisualizationConfigUnchanged =
        [&registry, mesh, &invalidBaseline]()
        {
            const auto& current =
                registry.Raw().get<G::VisualizationConfig>(mesh);
            EXPECT_EQ(current.Source, invalidBaseline.Source);
            EXPECT_EQ(current.ColorBufferName, invalidBaseline.ColorBufferName);
            EXPECT_EQ(current.ScalarFieldName, invalidBaseline.ScalarFieldName);
            EXPECT_EQ(current.ScalarDomain, invalidBaseline.ScalarDomain);
            EXPECT_EQ(current.Scalar.AutoRange, invalidBaseline.Scalar.AutoRange);
            EXPECT_FLOAT_EQ(current.Scalar.RangeMin,
                            invalidBaseline.Scalar.RangeMin);
            EXPECT_FLOAT_EQ(current.Scalar.RangeMax,
                            invalidBaseline.Scalar.RangeMax);
            EXPECT_EQ(current.Scalar.BinCount,
                      invalidBaseline.Scalar.BinCount);
            EXPECT_EQ(current.Scalar.Isolines.Num,
                      invalidBaseline.Scalar.Isolines.Num);
        };

    EXPECT_EQ(Runtime::ApplySandboxEditorVisualizationPropertyCommand(
                  context,
                  Runtime::SandboxEditorVisualizationPropertyCommand{
                      .StableEntityId = stableId,
                      .Domain = Domain::GraphVertices,
                      .Preset = Preset::Scalar,
                      .PropertyName = "v:temperature",
                  }),
              Runtime::SandboxEditorCommandStatus::UnsupportedGeometryDomain);

    EXPECT_EQ(Runtime::ApplySandboxEditorVisualizationPropertyCommand(
                  context,
                  Runtime::SandboxEditorVisualizationPropertyCommand{
                      .StableEntityId = stableId,
                      .Domain = Domain::MeshVertices,
                      .Preset = Preset::Scalar,
                      .PropertyName = "v:position",
                  }),
              Runtime::SandboxEditorCommandStatus::InvalidVisualizationProperty);

    EXPECT_EQ(Runtime::ApplySandboxEditorVisualizationPropertyCommand(
                  context,
                  Runtime::SandboxEditorVisualizationPropertyCommand{
                      .StableEntityId = stableId,
                      .Domain = Domain::MeshVertices,
                      .Preset = Preset::Scalar,
                      .PropertyName = "v:kmeans_label",
                  }),
              Runtime::SandboxEditorCommandStatus::InvalidVisualizationProperty);

    EXPECT_EQ(Runtime::ApplySandboxEditorVisualizationPropertyCommand(
                  context,
                  Runtime::SandboxEditorVisualizationPropertyCommand{
                      .StableEntityId = stableId,
                      .Domain = Domain::MeshVertices,
                      .Preset = Preset::Scalar,
                      .PropertyName = "missing",
                  }),
              Runtime::SandboxEditorCommandStatus::InvalidVisualizationProperty);

    EXPECT_EQ(Runtime::ApplySandboxEditorVisualizationPropertyCommand(
                  context,
                  Runtime::SandboxEditorVisualizationPropertyCommand{
                      .StableEntityId = stableId,
                      .Domain = Domain::MeshVertices,
                      .Preset = Preset::Scalar,
                      .PropertyName = "v:temperature",
                      .ScalarAutoRange = false,
                      .ScalarRangeMin = 2.0f,
                      .ScalarRangeMax = -1.0f,
                  }),
              Runtime::SandboxEditorCommandStatus::InvalidVisualizationProperty);

    EXPECT_EQ(Runtime::ApplySandboxEditorVisualizationPropertyCommand(
                  context,
                  Runtime::SandboxEditorVisualizationPropertyCommand{
                      .StableEntityId =
                          std::numeric_limits<std::uint32_t>::max(),
                      .Domain = Domain::MeshVertices,
                      .Preset = Preset::Scalar,
                      .PropertyName = "v:temperature",
                  }),
              Runtime::SandboxEditorCommandStatus::StaleEntity);

    Runtime::SandboxEditorContext unavailable = context;
    unavailable.VisualizationCommandsAvailable = false;
    EXPECT_EQ(Runtime::ApplySandboxEditorVisualizationPropertyCommand(
                  unavailable,
                  Runtime::SandboxEditorVisualizationPropertyCommand{
                      .StableEntityId = stableId,
                      .Domain = Domain::MeshVertices,
                      .Preset = Preset::Scalar,
                      .PropertyName = "v:temperature",
                  }),
              Runtime::SandboxEditorCommandStatus::MissingVisualizationCommands);
    expectVisualizationConfigUnchanged();

    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorVisualizationPropertyPreset(
                     Preset::ColorBuffer),
                 "ColorBuffer");
    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorCommandStatus(
                     Runtime::SandboxEditorCommandStatus::InvalidVisualizationProperty),
                 "InvalidVisualizationProperty");
}
TEST(SandboxEditorUi, RenderHintCommandEditsDomainComponentsAndHistory)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;

    auto& raw = registry.Raw();

    const ECS::EntityHandle mesh = MakeSelectable(registry, "Mesh");
    AddTriangleMeshSource(registry, mesh);
    const std::uint32_t meshStableId =
        Runtime::SelectionController::ToStableEntityId(mesh);
    EXPECT_EQ(Runtime::ApplySandboxEditorRenderHintCommand(
                  context,
                  Runtime::SandboxEditorRenderHintCommand{
                      .StableEntityId = meshStableId,
                      .SetSurface = true,
                      .EnableSurface = true,
                      .SurfaceDomain = G::RenderSurface::SourceDomain::Face,
                  }),
              Runtime::SandboxEditorCommandStatus::Applied);
    ASSERT_TRUE(raw.all_of<G::RenderSurface>(mesh));
    EXPECT_EQ(raw.get<G::RenderSurface>(mesh).Domain,
              G::RenderSurface::SourceDomain::Face);
    EXPECT_TRUE(history.Snapshot().Dirty);

    EXPECT_TRUE(history.Undo().Succeeded());
    EXPECT_FALSE(raw.all_of<G::RenderSurface>(mesh));
    EXPECT_TRUE(history.Redo().Succeeded());
    ASSERT_TRUE(raw.all_of<G::RenderSurface>(mesh));
    EXPECT_EQ(raw.get<G::RenderSurface>(mesh).Domain,
              G::RenderSurface::SourceDomain::Face);

    EXPECT_EQ(Runtime::ApplySandboxEditorRenderHintCommand(
                  context,
                  Runtime::SandboxEditorRenderHintCommand{
                      .StableEntityId = meshStableId,
                      .SetEdges = true,
                      .EnableEdges = true,
                      .EdgeDomain = G::RenderEdges::SourceDomain::Edge,
                      .SetUniformEdgeWidth = true,
                      .UniformEdgeWidth = 3.0f,
                  }),
              Runtime::SandboxEditorCommandStatus::Applied);
    ASSERT_TRUE(raw.all_of<G::RenderEdges>(mesh));
    const G::RenderEdges& meshEdges = raw.get<G::RenderEdges>(mesh);
    EXPECT_EQ(meshEdges.Domain, G::RenderEdges::SourceDomain::Edge);
    ASSERT_NE(std::get_if<float>(&meshEdges.WidthSource), nullptr);
    EXPECT_FLOAT_EQ(*std::get_if<float>(&meshEdges.WidthSource), 3.0f);

    EXPECT_EQ(Runtime::ApplySandboxEditorRenderHintCommand(
                  context,
                  Runtime::SandboxEditorRenderHintCommand{
                      .StableEntityId = meshStableId,
                      .SetPoints = true,
                      .EnablePoints = true,
                      .PointType = G::RenderPoints::RenderType::Surfel,
                      .SetUniformPointSize = true,
                      .UniformPointSize = 7.0f,
                  }),
              Runtime::SandboxEditorCommandStatus::Applied);
    ASSERT_TRUE(raw.all_of<G::RenderPoints>(mesh));
    const G::RenderPoints& meshPoints = raw.get<G::RenderPoints>(mesh);
    EXPECT_EQ(meshPoints.Type, G::RenderPoints::RenderType::Surfel);
    ASSERT_NE(std::get_if<float>(&meshPoints.SizeSource), nullptr);
    EXPECT_FLOAT_EQ(*std::get_if<float>(&meshPoints.SizeSource), 7.0f);

    const ECS::EntityHandle graph = MakeSelectable(registry, "Graph");
    AddGraphSource(registry, graph);
    const std::uint32_t graphStableId =
        Runtime::SelectionController::ToStableEntityId(graph);
    EXPECT_EQ(Runtime::ApplySandboxEditorRenderHintCommand(
                  context,
                  Runtime::SandboxEditorRenderHintCommand{
                      .StableEntityId = graphStableId,
                      .SetEdges = true,
                      .EnableEdges = false,
                  }),
              Runtime::SandboxEditorCommandStatus::Applied);
    EXPECT_FALSE(raw.all_of<G::RenderEdges>(graph));
    EXPECT_TRUE(history.Undo().Succeeded());
    EXPECT_TRUE(raw.all_of<G::RenderEdges>(graph));

    EXPECT_EQ(Runtime::ApplySandboxEditorRenderHintCommand(
                  context,
                  Runtime::SandboxEditorRenderHintCommand{
                      .StableEntityId = graphStableId,
                      .PointType = G::RenderPoints::RenderType::Flat,
                      .SetPointRenderType = true,
                      .SetUniformPointSize = true,
                      .UniformPointSize = 0.25f,
                  }),
              Runtime::SandboxEditorCommandStatus::Applied);
    ASSERT_TRUE(raw.all_of<G::RenderPoints>(graph));
    const G::RenderPoints& graphPoints = raw.get<G::RenderPoints>(graph);
    EXPECT_EQ(graphPoints.Type, G::RenderPoints::RenderType::Flat);
    ASSERT_NE(std::get_if<float>(&graphPoints.SizeSource), nullptr);
    EXPECT_FLOAT_EQ(*std::get_if<float>(&graphPoints.SizeSource), 0.25f);

    ASSERT_TRUE(selection.SetSelectedEntity(registry, graph));
    const Runtime::SandboxEditorDomainWindowModel graphModel =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::Graph);
    EXPECT_TRUE(graphModel.RenderHints.HasRenderPoints);
    EXPECT_EQ(graphModel.RenderHints.PointRenderType, "Flat");
    EXPECT_EQ(graphModel.RenderHints.PointRenderTypeValue,
              G::RenderPoints::RenderType::Flat);
    EXPECT_TRUE(graphModel.RenderHints.HasUniformPointSize);
    EXPECT_FLOAT_EQ(graphModel.RenderHints.UniformPointSize, 0.25f);

    const ECS::EntityHandle cloud = MakeSelectable(registry, "Cloud");
    AddPointCloudSource(registry, cloud, 3u);
    const std::uint32_t cloudStableId =
        Runtime::SelectionController::ToStableEntityId(cloud);
    EXPECT_EQ(Runtime::ApplySandboxEditorRenderHintCommand(
                  context,
                  Runtime::SandboxEditorRenderHintCommand{
                      .StableEntityId = cloudStableId,
                      .SetEdges = true,
                      .EnableEdges = true,
                  }),
              Runtime::SandboxEditorCommandStatus::UnsupportedGeometryDomain);
    EXPECT_EQ(Runtime::ApplySandboxEditorRenderHintCommand(
                  context,
                  Runtime::SandboxEditorRenderHintCommand{
                      .StableEntityId = cloudStableId,
                      .SetPoints = true,
                      .EnablePoints = false,
                  }),
              Runtime::SandboxEditorCommandStatus::Applied);
    EXPECT_FALSE(raw.all_of<G::RenderPoints>(cloud));
    EXPECT_EQ(Runtime::ApplySandboxEditorRenderHintCommand(
                  context,
                  Runtime::SandboxEditorRenderHintCommand{
                      .StableEntityId = cloudStableId,
                      .SetPoints = true,
                      .EnablePoints = true,
                      .PointType = G::RenderPoints::RenderType::Surfel,
                      .SetUniformPointSize = true,
                      .UniformPointSize = 0.05f,
                  }),
              Runtime::SandboxEditorCommandStatus::Applied);
    ASSERT_TRUE(raw.all_of<G::RenderPoints>(cloud));
    const G::RenderPoints& cloudPoints = raw.get<G::RenderPoints>(cloud);
    EXPECT_EQ(cloudPoints.Type, G::RenderPoints::RenderType::Surfel);
    ASSERT_NE(std::get_if<float>(&cloudPoints.SizeSource), nullptr);
    EXPECT_FLOAT_EQ(*std::get_if<float>(&cloudPoints.SizeSource), 0.05f);
}
TEST(SandboxEditorUi, RenderHintCommandRepackagesGraphLaneResidency)
{
    Runtime::Engine engine(HeadlessConfig(), std::make_unique<PassiveApplication>());
    engine.EmplaceModule<Runtime::SceneInteractionModule>();
    engine.EmplaceModule<Runtime::SceneDocumentModule>();
    engine.EmplaceModule<Runtime::AssetWorkflowModule>();
    engine.Initialize();

    ECS::Scene::Registry& scene = *engine.Worlds().Get(engine.ActiveWorld());
    Runtime::SelectionController& selection =
        *engine.Services().Find<Runtime::SelectionController>();
    const ECS::EntityHandle graph = MakeSelectable(scene, "Graph");
    AddGraphSource(scene, graph);
    auto& raw = scene.Raw();
    raw.remove<G::RenderEdges>(graph);

    Runtime::RenderExtractionCache extraction;
    const Runtime::RuntimeRenderExtractionStats first =
        extraction.ExtractAndSubmit(scene,
                                    engine.GetRenderer(),
                                    &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));
    EXPECT_EQ(first.GraphGeometryUploads, 1u);
    EXPECT_EQ(first.GraphGeometryReuploads, 0u);

    Runtime::SandboxEditorContext context = MakeContext(scene, selection);
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(graph);
    EXPECT_EQ(Runtime::ApplySandboxEditorRenderHintCommand(
                  context,
                  Runtime::SandboxEditorRenderHintCommand{
                      .StableEntityId = stableId,
                      .SetEdges = true,
                      .EnableEdges = true,
                      .EdgeDomain = G::RenderEdges::SourceDomain::Vertex,
                  }),
              Runtime::SandboxEditorCommandStatus::Applied);
    ASSERT_TRUE(raw.all_of<G::RenderEdges>(graph));

    const Runtime::RuntimeRenderExtractionStats second =
        extraction.ExtractAndSubmit(scene,
                                    engine.GetRenderer(),
                                    &RequiredEngineService<Extrinsic::Graphics::GpuAssetCache>(engine));
    EXPECT_EQ(second.GraphGeometryUploads, 0u);
    EXPECT_EQ(second.GraphGeometryReuploads, 1u);
    EXPECT_EQ(second.GraphGeometryReleases, 1u);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}
TEST(SandboxEditorUi, SelectEntityCommandRoutesThroughSelectionController)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    const ECS::EntityHandle first = MakeSelectable(registry, "First");
    const ECS::EntityHandle second = MakeSelectable(registry, "Second");
    ASSERT_TRUE(selection.SetSelectedEntity(registry, first));

    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    EXPECT_TRUE(Runtime::SelectSandboxEditorEntity(
        context,
        Runtime::SelectionController::ToStableEntityId(second)));

    EXPECT_FALSE(selection.IsSelected(first));
    EXPECT_TRUE(selection.IsSelected(second));
    EXPECT_FALSE(registry.Raw().all_of<Sel::SelectedTag>(first));
    EXPECT_TRUE(registry.Raw().all_of<Sel::SelectedTag>(second));
}
TEST(SandboxEditorUi, TransformEditCommandMutatesLocalTransformAndMarksDirty)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    const ECS::EntityHandle entity = MakeSelectable(registry, "Editable");
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(entity);

    EXPECT_EQ(Runtime::ApplySandboxEditorTransformEdit(
                  context,
                  Runtime::SandboxEditorTransformEditCommand{
                      .StableEntityId = stableId,
                  }),
              Runtime::SandboxEditorCommandStatus::NoChange);
    EXPECT_FALSE(registry.Raw().all_of<ECSC::Transform::IsDirtyTag>(entity));

    const Runtime::SandboxEditorCommandStatus status =
        Runtime::ApplySandboxEditorTransformEdit(
            context,
            Runtime::SandboxEditorTransformEditCommand{
                .StableEntityId = stableId,
                .SetPosition = true,
                .Position = glm::vec3{4.0f, 5.0f, 6.0f},
                .SetRotation = true,
                .Rotation = glm::quat{0.5f, 0.5f, 0.5f, 0.5f},
                .SetScale = true,
                .Scale = glm::vec3{2.0f, 2.5f, 3.0f},
            });

    EXPECT_EQ(status, Runtime::SandboxEditorCommandStatus::Applied);
    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorCommandStatus(status),
                 "Applied");
    const auto& transform =
        registry.Raw().get<ECSC::Transform::Component>(entity);
    EXPECT_FLOAT_EQ(transform.Position.x, 4.0f);
    EXPECT_FLOAT_EQ(transform.Position.y, 5.0f);
    EXPECT_FLOAT_EQ(transform.Position.z, 6.0f);
    EXPECT_FLOAT_EQ(transform.Rotation.w, 0.5f);
    EXPECT_FLOAT_EQ(transform.Scale.x, 2.0f);
    EXPECT_FLOAT_EQ(transform.Scale.y, 2.5f);
    EXPECT_FLOAT_EQ(transform.Scale.z, 3.0f);
    EXPECT_TRUE(registry.Raw().all_of<ECSC::Transform::IsDirtyTag>(entity));

    Runtime::SandboxEditorContext missingSelection = context;
    missingSelection.Selection = nullptr;
    EXPECT_EQ(Runtime::ApplySandboxEditorTransformEdit(
                  missingSelection,
                  Runtime::SandboxEditorTransformEditCommand{
                      .StableEntityId = stableId,
                      .SetPosition = true,
                      .Position = glm::vec3{1.0f},
                  }),
              Runtime::SandboxEditorCommandStatus::MissingSelectionController);
}
TEST(SandboxEditorUi, CameraControllerCommandReplacesMainController)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::CameraControllerRegistry cameraControllers;
    cameraControllers.Register(
        Runtime::CameraControllerSlot::Main,
        Runtime::CreateCameraController(
            Extrinsic::Core::Config::CameraControllerKind::Orbit));

    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.CameraControllers = &cameraControllers;
    context.CameraViewport = Extrinsic::Core::Extent2D{640, 40};

    Runtime::SandboxEditorPanelFrame frame =
        Runtime::BuildSandboxEditorPanelFrame(context);
    ASSERT_TRUE(frame.CameraRender.CameraControlsAvailable);
    ASSERT_TRUE(frame.CameraRender.HasMainCameraController);
    EXPECT_EQ(frame.CameraRender.MainCameraControllerKind,
              Extrinsic::Core::Config::CameraControllerKind::Orbit);

    const Runtime::SandboxEditorCommandStatus status =
        Runtime::ApplySandboxEditorCameraControllerCommand(
            context,
            Runtime::SandboxEditorCameraControllerCommand{
                .Kind = Extrinsic::Core::Config::CameraControllerKind::Fly,
            });

    EXPECT_EQ(status, Runtime::SandboxEditorCommandStatus::Applied);
    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorCameraControllerKind(
                     cameraControllers.Resolve(Runtime::CameraControllerSlot::Main)
                         .Kind()),
                 "Fly");

    EXPECT_EQ(Runtime::ApplySandboxEditorCameraControllerCommand(
                  context,
                  Runtime::SandboxEditorCameraControllerCommand{
                      .Kind = Extrinsic::Core::Config::CameraControllerKind::Fly,
                  }),
              Runtime::SandboxEditorCommandStatus::NoChange);

    Runtime::SandboxEditorContext missingRegistry = context;
    missingRegistry.CameraControllers = nullptr;
    EXPECT_EQ(Runtime::ApplySandboxEditorCameraControllerCommand(
                  missingRegistry,
                  Runtime::SandboxEditorCameraControllerCommand{}),
              Runtime::SandboxEditorCommandStatus::MissingCameraControllerRegistry);
}
TEST(SandboxEditorUi, PrimitiveViewCommandTranslatesToRenderHintComponents)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    auto& raw = registry.Raw();
    const ECS::EntityHandle mesh = MakeSelectable(registry, "Mesh");
    AddTriangleMeshSource(registry, mesh);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));

    const std::uint32_t meshStableId =
        Runtime::SelectionController::ToStableEntityId(mesh);

    Runtime::SandboxEditorContext context = MakeContext(registry, selection);

    EXPECT_EQ(Runtime::ApplySandboxEditorPrimitiveViewCommand(
                  context,
                  Runtime::SandboxEditorPrimitiveViewCommand{
                      .StableEntityId = meshStableId,
                      .SetEdgeView = true,
                      .EnableEdgeView = true,
                      .SetVertexView = true,
                      .EnableVertexView = true,
                      .SetVertexRenderMode = true,
                      .VertexRenderMode =
                          Runtime::MeshVertexViewRenderMode::SurfaceAlignedCircle,
                      .SetVertexPointRadius = true,
                      .VertexPointRadiusPx = 11.0f,
                  }),
              Runtime::SandboxEditorCommandStatus::Applied);
    ASSERT_TRUE(raw.all_of<G::RenderEdges>(mesh));
    ASSERT_TRUE(raw.all_of<G::RenderPoints>(mesh));
    const G::RenderPoints& points = raw.get<G::RenderPoints>(mesh);
    EXPECT_EQ(points.Type, G::RenderPoints::RenderType::Surfel);
    ASSERT_NE(std::get_if<float>(&points.SizeSource), nullptr);
    EXPECT_FLOAT_EQ(*std::get_if<float>(&points.SizeSource), 11.0f);

    EXPECT_EQ(Runtime::ApplySandboxEditorPrimitiveViewCommand(
                  context,
                  Runtime::SandboxEditorPrimitiveViewCommand{
                      .StableEntityId = meshStableId,
                      .SetVertexPointRadius = true,
                      .VertexPointRadiusPx = 0.0f,
                  }),
              Runtime::SandboxEditorCommandStatus::InvalidProcessingParameters);
    EXPECT_TRUE(raw.all_of<G::RenderPoints>(mesh));

    EXPECT_EQ(Runtime::ApplySandboxEditorPrimitiveViewCommand(
                  context,
                  Runtime::SandboxEditorPrimitiveViewCommand{
                      .StableEntityId = meshStableId,
                      .SetEdgeView = true,
                      .EnableEdgeView = false,
                      .SetVertexView = true,
                      .EnableVertexView = false,
                  }),
              Runtime::SandboxEditorCommandStatus::Applied);
    EXPECT_FALSE(raw.all_of<G::RenderEdges>(mesh));
    EXPECT_FALSE(raw.all_of<G::RenderPoints>(mesh));

    EXPECT_EQ(Runtime::ApplySandboxEditorPrimitiveViewCommand(
                  context,
                  Runtime::SandboxEditorPrimitiveViewCommand{}),
              Runtime::SandboxEditorCommandStatus::NoChange);

    const ECS::EntityHandle vertexOnlyMesh =
        MakeSelectable(registry, "Vertex Only Mesh");
    raw.emplace<GS::HasMeshTopology>(vertexOnlyMesh);
    auto& vertexOnlySource = raw.emplace<GS::Vertices>(vertexOnlyMesh);
    SetPositions(vertexOnlySource, {
        {0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
    });
    const std::uint32_t vertexOnlyStableId =
        Runtime::SelectionController::ToStableEntityId(vertexOnlyMesh);
    EXPECT_EQ(Runtime::ApplySandboxEditorPrimitiveViewCommand(
                  context,
                  Runtime::SandboxEditorPrimitiveViewCommand{
                      .StableEntityId = vertexOnlyStableId,
                      .SetVertexView = true,
                      .EnableVertexView = true,
                      .SetVertexRenderMode = true,
                      .VertexRenderMode =
                          Runtime::MeshVertexViewRenderMode::FlatCircle,
                      .SetVertexPointRadius = true,
                      .VertexPointRadiusPx = 7.0f,
                  }),
              Runtime::SandboxEditorCommandStatus::Applied);
    ASSERT_TRUE(raw.all_of<G::RenderPoints>(vertexOnlyMesh));
    EXPECT_FALSE(raw.all_of<G::RenderEdges>(vertexOnlyMesh));
    const G::RenderPoints& vertexOnlyPoints =
        raw.get<G::RenderPoints>(vertexOnlyMesh);
    EXPECT_EQ(vertexOnlyPoints.Type, G::RenderPoints::RenderType::Flat);
    ASSERT_NE(std::get_if<float>(&vertexOnlyPoints.SizeSource), nullptr);
    EXPECT_FLOAT_EQ(*std::get_if<float>(&vertexOnlyPoints.SizeSource), 7.0f);

    EXPECT_EQ(Runtime::ApplySandboxEditorPrimitiveViewCommand(
                  context,
                  Runtime::SandboxEditorPrimitiveViewCommand{
                      .StableEntityId = vertexOnlyStableId,
                      .SetEdgeView = true,
                      .EnableEdgeView = true,
                  }),
              Runtime::SandboxEditorCommandStatus::UnsupportedGeometryDomain);

    const ECS::EntityHandle cloud = MakeSelectable(registry, "Cloud");
    AddPointCloudSource(registry, cloud, 2u);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, cloud));
    const std::uint32_t cloudStableId =
        Runtime::SelectionController::ToStableEntityId(cloud);
    EXPECT_EQ(Runtime::ApplySandboxEditorPrimitiveViewCommand(
                  context,
                  Runtime::SandboxEditorPrimitiveViewCommand{
                      .StableEntityId = cloudStableId,
                      .SetEdgeView = true,
                      .EnableEdgeView = true,
                  }),
              Runtime::SandboxEditorCommandStatus::UnsupportedGeometryDomain);
}
TEST(SandboxEditorUi, SpatialDebugBindingCommandRoutesThroughSelectedEntity)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    const ECS::EntityHandle mesh = MakeSelectable(registry, "DebugMesh");
    AddTriangleMeshSource(registry, mesh);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);

    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.VisualizationCommandsAvailable = true;

    Runtime::SandboxEditorPanelFrame frame =
        Runtime::BuildSandboxEditorPanelFrame(context);
    ASSERT_TRUE(frame.Visualization.GeometryDomainControlsAvailable);
    ASSERT_TRUE(frame.Visualization.HasSelectedEntity);
    EXPECT_FALSE(frame.Visualization.SpatialDebug.HasBinding);

    const Runtime::SandboxEditorSpatialDebugBindingCommand enable{
        .StableEntityId = stableId,
        .EnableBinding = true,
        .Kind = ECSC::SpatialDebugGeometryKind::Octree,
        .RegistryKey = 42u,
        .LeafOnly = true,
        .OccupancyOnly = true,
        .MaxDepth = 7u,
    };

    EXPECT_EQ(Runtime::ApplySandboxEditorSpatialDebugBindingCommand(
                  context,
                  enable),
              Runtime::SandboxEditorCommandStatus::Applied);
    ASSERT_TRUE(registry.Raw().all_of<ECSC::SpatialDebugBinding>(mesh));
    const auto& binding =
        registry.Raw().get<ECSC::SpatialDebugBinding>(mesh);
    EXPECT_EQ(binding.Kind, ECSC::SpatialDebugGeometryKind::Octree);
    EXPECT_EQ(binding.RegistryKey, 42u);
    EXPECT_TRUE(binding.LeafOnly);
    EXPECT_TRUE(binding.OccupancyOnly);
    EXPECT_EQ(binding.MaxDepth, 7u);
    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorSpatialDebugKind(
                     binding.Kind),
                 "Octree");

    frame = Runtime::BuildSandboxEditorPanelFrame(context);
    ASSERT_TRUE(frame.Visualization.SpatialDebug.HasBinding);
    EXPECT_EQ(frame.Visualization.SpatialDebug.RegistryKey, 42u);

    EXPECT_EQ(Runtime::ApplySandboxEditorSpatialDebugBindingCommand(
                  context,
                  enable),
              Runtime::SandboxEditorCommandStatus::NoChange);

    EXPECT_EQ(Runtime::ApplySandboxEditorSpatialDebugBindingCommand(
                  context,
                  Runtime::SandboxEditorSpatialDebugBindingCommand{
                      .StableEntityId = stableId,
                      .EnableBinding = false,
                  }),
              Runtime::SandboxEditorCommandStatus::Applied);
    EXPECT_FALSE(registry.Raw().all_of<ECSC::SpatialDebugBinding>(mesh));

    Runtime::SandboxEditorContext unavailable = context;
    unavailable.VisualizationCommandsAvailable = false;
    EXPECT_EQ(Runtime::ApplySandboxEditorSpatialDebugBindingCommand(
                  unavailable,
                  enable),
              Runtime::SandboxEditorCommandStatus::MissingVisualizationCommands);
}
TEST(SandboxEditorUi, VisualizationConfigCommandRoutesThroughSelectedEntity)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    const ECS::EntityHandle mesh = MakeSelectable(registry, "VisualMesh");
    AddTriangleMeshSource(registry, mesh);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);

    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.VisualizationCommandsAvailable = true;

    const Runtime::SandboxEditorVisualizationConfigCommand scalar{
        .StableEntityId = stableId,
        .EnableConfig = true,
        .Source = G::VisualizationConfig::ColorSource::ScalarField,
        .ScalarFieldName = "curvature",
        .ScalarDomain = G::VisualizationConfig::Domain::Face,
        .ScalarAutoRange = false,
        .ScalarRangeMin = -1.0f,
        .ScalarRangeMax = 2.0f,
        .ScalarBinCount = 4u,
        .IsolineCount = 8u,
    };

    EXPECT_EQ(Runtime::ApplySandboxEditorVisualizationConfigCommand(
                  context,
                  scalar),
              Runtime::SandboxEditorCommandStatus::Applied);
    ASSERT_TRUE(registry.Raw().all_of<G::VisualizationConfig>(mesh));
    const auto& config = registry.Raw().get<G::VisualizationConfig>(mesh);
    EXPECT_EQ(config.Source, G::VisualizationConfig::ColorSource::ScalarField);
    EXPECT_EQ(config.ScalarFieldName, "curvature");
    EXPECT_EQ(config.ScalarDomain, G::VisualizationConfig::Domain::Face);
    EXPECT_FALSE(config.Scalar.AutoRange);
    EXPECT_FLOAT_EQ(config.Scalar.RangeMin, -1.0f);
    EXPECT_FLOAT_EQ(config.Scalar.RangeMax, 2.0f);
    EXPECT_EQ(config.Scalar.BinCount, 4u);
    EXPECT_EQ(config.Scalar.Isolines.Num, 8u);
    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorVisualizationColorSource(
                     config.Source),
                 "ScalarField");
    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorVisualizationDomain(
                     config.ScalarDomain),
                 "Face");

    Runtime::SandboxEditorPanelFrame frame =
        Runtime::BuildSandboxEditorPanelFrame(context);
    ASSERT_TRUE(frame.Visualization.Visualization.HasConfig);
    EXPECT_EQ(frame.Visualization.Visualization.Source,
              G::VisualizationConfig::ColorSource::ScalarField);
    EXPECT_EQ(frame.Visualization.Visualization.ScalarFieldName, "curvature");
    EXPECT_EQ(frame.Visualization.Visualization.IsolineCount, 8u);

    EXPECT_EQ(Runtime::ApplySandboxEditorVisualizationConfigCommand(
                  context,
                  scalar),
              Runtime::SandboxEditorCommandStatus::NoChange);

    const Runtime::SandboxEditorVisualizationConfigCommand uniform{
        .StableEntityId = stableId,
        .EnableConfig = true,
        .Source = G::VisualizationConfig::ColorSource::UniformColor,
        .Color = glm::vec4{0.125f, 0.5f, 0.875f, 1.0f},
        .ScalarFieldName = "curvature",
        .ScalarDomain = G::VisualizationConfig::Domain::Face,
        .ScalarAutoRange = false,
        .ScalarRangeMin = -1.0f,
        .ScalarRangeMax = 2.0f,
        .ScalarBinCount = 4u,
        .IsolineCount = 8u,
    };

    EXPECT_EQ(Runtime::ApplySandboxEditorVisualizationConfigCommand(
                  context,
                  uniform),
              Runtime::SandboxEditorCommandStatus::Applied);
    ASSERT_TRUE(registry.Raw().all_of<G::VisualizationConfig>(mesh));
    const auto& uniformConfig = registry.Raw().get<G::VisualizationConfig>(mesh);
    EXPECT_EQ(uniformConfig.Source,
              G::VisualizationConfig::ColorSource::UniformColor);
    EXPECT_FLOAT_EQ(uniformConfig.Color.x, 0.125f);
    EXPECT_FLOAT_EQ(uniformConfig.Color.y, 0.5f);
    EXPECT_FLOAT_EQ(uniformConfig.Color.z, 0.875f);
    EXPECT_FLOAT_EQ(uniformConfig.Color.w, 1.0f);
    EXPECT_EQ(uniformConfig.ScalarFieldName, "curvature");
    EXPECT_EQ(uniformConfig.ScalarDomain, G::VisualizationConfig::Domain::Face);
    EXPECT_FALSE(uniformConfig.Scalar.AutoRange);
    EXPECT_FLOAT_EQ(uniformConfig.Scalar.RangeMin, -1.0f);
    EXPECT_FLOAT_EQ(uniformConfig.Scalar.RangeMax, 2.0f);
    EXPECT_EQ(uniformConfig.Scalar.BinCount, 4u);
    EXPECT_EQ(uniformConfig.Scalar.Isolines.Num, 8u);

    frame = Runtime::BuildSandboxEditorPanelFrame(context);
    ASSERT_TRUE(frame.Visualization.Visualization.HasConfig);
    EXPECT_EQ(frame.Visualization.Visualization.Source,
              G::VisualizationConfig::ColorSource::UniformColor);
    EXPECT_FLOAT_EQ(frame.Visualization.Visualization.Color.x, 0.125f);
    EXPECT_FLOAT_EQ(frame.Visualization.Visualization.Color.y, 0.5f);
    EXPECT_FLOAT_EQ(frame.Visualization.Visualization.Color.z, 0.875f);
    EXPECT_FLOAT_EQ(frame.Visualization.Visualization.Color.w, 1.0f);

    EXPECT_EQ(Runtime::ApplySandboxEditorVisualizationConfigCommand(
                  context,
                  uniform),
              Runtime::SandboxEditorCommandStatus::NoChange);

    EXPECT_EQ(Runtime::ApplySandboxEditorVisualizationConfigCommand(
                  context,
                  Runtime::SandboxEditorVisualizationConfigCommand{
                      .StableEntityId = stableId,
                      .EnableConfig = false,
                  }),
              Runtime::SandboxEditorCommandStatus::Applied);
    EXPECT_FALSE(registry.Raw().all_of<G::VisualizationConfig>(mesh));

    Runtime::SandboxEditorContext unavailable = context;
    unavailable.VisualizationCommandsAvailable = false;
    EXPECT_EQ(Runtime::ApplySandboxEditorVisualizationConfigCommand(
                  unavailable,
                  scalar),
              Runtime::SandboxEditorCommandStatus::MissingVisualizationCommands);
}
TEST(SandboxEditorUi, VisualizationConfigCommandTargetsPointLaneOverride)
{
    using Target = Runtime::SandboxEditorVisualizationTarget;

    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    const ECS::EntityHandle mesh = MakeSelectable(registry, "VisualMesh");
    AddTriangleMeshSource(registry, mesh);
    registry.Raw().emplace<G::RenderPoints>(mesh);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);

    G::VisualizationConfig base{};
    base.Source = G::VisualizationConfig::ColorSource::UniformColor;
    base.Color = glm::vec4{1.0f, 0.0f, 0.0f, 1.0f};
    registry.Raw().emplace<G::VisualizationConfig>(mesh, base);

    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;
    context.VisualizationCommandsAvailable = true;

    const Runtime::SandboxEditorVisualizationConfigCommand pointUniform{
        .StableEntityId = stableId,
        .Target = Target::Points,
        .EnableConfig = true,
        .Source = G::VisualizationConfig::ColorSource::UniformColor,
        .Color = glm::vec4{0.0f, 0.8f, 0.2f, 1.0f},
    };

    EXPECT_EQ(Runtime::ApplySandboxEditorVisualizationConfigCommand(
                  context,
                  pointUniform),
              Runtime::SandboxEditorCommandStatus::Applied);
    const auto& defaultConfig = registry.Raw().get<G::VisualizationConfig>(mesh);
    EXPECT_EQ(defaultConfig.Color, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
    ASSERT_TRUE(registry.Raw().all_of<G::VisualizationLaneOverrides>(mesh));
    const auto& overrides =
        registry.Raw().get<G::VisualizationLaneOverrides>(mesh);
    ASSERT_TRUE(overrides.Points.has_value());
    EXPECT_EQ(overrides.Points->Source,
              G::VisualizationConfig::ColorSource::UniformColor);
    EXPECT_EQ(overrides.Points->Color, glm::vec4(0.0f, 0.8f, 0.2f, 1.0f));
    EXPECT_FALSE(overrides.Surface.has_value());
    EXPECT_FALSE(overrides.Edges.has_value());

    const Runtime::SandboxEditorDomainWindowModel pointModel =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::PointCloud);
    ASSERT_TRUE(pointModel.Visualization.Visualization.HasConfig);
    EXPECT_EQ(pointModel.Visualization.Visualization.Color,
              glm::vec4(0.0f, 0.8f, 0.2f, 1.0f));
    EXPECT_EQ(pointModel.Visualization.Target, Target::Points);

    EXPECT_EQ(history.Undo().Status,
              Runtime::EditorCommandHistoryStatus::Undone);
    EXPECT_FALSE(registry.Raw().all_of<G::VisualizationLaneOverrides>(mesh));
    EXPECT_EQ(registry.Raw().get<G::VisualizationConfig>(mesh).Color,
              glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));

    EXPECT_EQ(history.Redo().Status,
              Runtime::EditorCommandHistoryStatus::Redone);
    ASSERT_TRUE(registry.Raw().all_of<G::VisualizationLaneOverrides>(mesh));
    EXPECT_TRUE(registry.Raw()
                    .get<G::VisualizationLaneOverrides>(mesh)
                    .Points.has_value());

    EXPECT_EQ(Runtime::ApplySandboxEditorVisualizationConfigCommand(
                  context,
                  Runtime::SandboxEditorVisualizationConfigCommand{
                      .StableEntityId = stableId,
                      .Target = Target::Points,
                      .EnableConfig = false,
                  }),
              Runtime::SandboxEditorCommandStatus::Applied);
    EXPECT_FALSE(registry.Raw().all_of<G::VisualizationLaneOverrides>(mesh));
    EXPECT_TRUE(registry.Raw().all_of<G::VisualizationConfig>(mesh));
}
TEST(SandboxEditorUi, VisualizationAdapterBindingCommandRoutesThroughRuntimeSurface)
{
    using Binding = Runtime::RenderExtractionCache::VisualizationAdapterBinding;
    using Kind = Runtime::RenderExtractionCache::VisualizationAdapterBindingKind;

    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    const ECS::EntityHandle mesh = MakeSelectable(registry, "AdapterMesh");
    AddTriangleMeshSource(registry, mesh);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);

    std::optional<Binding> storedBinding{};
    std::uint32_t storedStableId{0u};
    std::uint32_t setCount{0u};
    std::uint32_t clearCount{0u};

    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.VisualizationCommandsAvailable = true;
    context.VisualizationAdapterBindings =
        Runtime::SandboxEditorVisualizationAdapterBindingCommandSurface{
            .GetBinding =
                [&](const std::uint32_t queriedStableId) -> std::optional<Binding>
                {
                    if (storedBinding.has_value() &&
                        storedStableId == queriedStableId)
                    {
                        return storedBinding;
                    }
                    return std::nullopt;
                },
            .SetBinding =
                [&](const std::uint32_t targetStableId, Binding binding)
                {
                    storedStableId = targetStableId;
                    storedBinding = std::move(binding);
                    ++setCount;
                },
            .ClearBinding =
                [&](const std::uint32_t targetStableId)
                {
                    if (storedStableId == targetStableId)
                    {
                        storedBinding.reset();
                    }
                    ++clearCount;
                },
        };

    Runtime::VisualizationAdapterOptions options{};
    options.SourceName = "velocity";
    options.OutputName = "velocity_glyphs";
    options.PositionBufferBDA = 0xAABB'1000u;
    options.VectorBufferBDA = 0xAABB'2000u;
    options.VectorScale = 2.0f;
    options.DepthTested = false;

    const Runtime::SandboxEditorVisualizationAdapterBindingCommand bindVector{
        .StableEntityId = stableId,
        .EnableBinding = true,
        .AdapterKey = 0xF00Du,
        .BufferBDA = 0xAABB'2000u,
        .Kind = Kind::VectorField,
        .Options = options,
    };

    EXPECT_EQ(Runtime::ApplySandboxEditorVisualizationAdapterBindingCommand(
                  context,
                  bindVector),
              Runtime::SandboxEditorCommandStatus::Applied);
    ASSERT_TRUE(storedBinding.has_value());
    EXPECT_EQ(storedStableId, stableId);
    EXPECT_EQ(storedBinding->AdapterKey, 0xF00Du);
    EXPECT_EQ(storedBinding->Kind, Kind::VectorField);
    EXPECT_EQ(storedBinding->Options.SourceName, "velocity");
    EXPECT_EQ(storedBinding->Options.VectorBufferBDA, 0xAABB'2000u);
    EXPECT_FALSE(storedBinding->Options.DepthTested);
    EXPECT_EQ(setCount, 1u);
    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorVisualizationAdapterBindingKind(
                     storedBinding->Kind),
                 "VectorField");

    Runtime::SandboxEditorPanelFrame frame =
        Runtime::BuildSandboxEditorPanelFrame(context);
    ASSERT_TRUE(frame.Visualization.AdapterBindingControlsAvailable);
    ASSERT_TRUE(frame.Visualization.AdapterBinding.HasBinding);
    EXPECT_EQ(frame.Visualization.AdapterBinding.AdapterKey, 0xF00Du);
    EXPECT_EQ(frame.Visualization.AdapterBinding.Kind, Kind::VectorField);
    EXPECT_EQ(frame.Visualization.AdapterBinding.Options.OutputName,
              "velocity_glyphs");

    EXPECT_EQ(Runtime::ApplySandboxEditorVisualizationAdapterBindingCommand(
                  context,
                  bindVector),
              Runtime::SandboxEditorCommandStatus::NoChange);
    EXPECT_EQ(setCount, 1u);

    EXPECT_EQ(Runtime::ApplySandboxEditorVisualizationAdapterBindingCommand(
                  context,
                  Runtime::SandboxEditorVisualizationAdapterBindingCommand{
                      .StableEntityId = stableId,
                      .EnableBinding = false,
                  }),
              Runtime::SandboxEditorCommandStatus::Applied);
    EXPECT_FALSE(storedBinding.has_value());
    EXPECT_EQ(clearCount, 1u);

    EXPECT_EQ(Runtime::ApplySandboxEditorVisualizationAdapterBindingCommand(
                  context,
                  Runtime::SandboxEditorVisualizationAdapterBindingCommand{
                      .StableEntityId = stableId,
                      .EnableBinding = false,
                  }),
              Runtime::SandboxEditorCommandStatus::NoChange);
    EXPECT_EQ(clearCount, 1u);

    Runtime::SandboxEditorContext missingSurface = context;
    missingSurface.VisualizationAdapterBindings = {};
    EXPECT_EQ(Runtime::ApplySandboxEditorVisualizationAdapterBindingCommand(
                  missingSurface,
                  bindVector),
              Runtime::SandboxEditorCommandStatus::MissingVisualizationCommands);

    const ECS::EntityHandle empty = MakeSelectable(registry, "NoGeometry");
    EXPECT_EQ(Runtime::ApplySandboxEditorVisualizationAdapterBindingCommand(
                  context,
                  Runtime::SandboxEditorVisualizationAdapterBindingCommand{
                      .StableEntityId =
                          Runtime::SelectionController::ToStableEntityId(empty),
                      .EnableBinding = true,
                      .AdapterKey = 0xF00Du,
                      .Kind = Kind::Scalar,
                  }),
              Runtime::SandboxEditorCommandStatus::UnsupportedGeometryDomain);

    EXPECT_EQ(Runtime::ApplySandboxEditorVisualizationAdapterBindingCommand(
                  context,
                  Runtime::SandboxEditorVisualizationAdapterBindingCommand{
                      .StableEntityId = std::numeric_limits<std::uint32_t>::max(),
                      .EnableBinding = true,
                      .AdapterKey = 0xF00Du,
                  }),
              Runtime::SandboxEditorCommandStatus::StaleEntity);
}
TEST(SandboxEditorUi, ProgressiveSlotCommandsUseCommandHistory)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    const ECS::EntityHandle mesh = MakeSelectable(registry, "ProgressiveCommands");
    AddTriangleMeshSource(registry, mesh);
    auto& vertices = registry.Raw().get<GS::Vertices>(mesh);
    vertices.Properties.GetOrAdd<glm::vec4>("v:paint", glm::vec4{1.0f})
        .Vector() = {
            glm::vec4{1.0f, 0.0f, 0.0f, 1.0f},
            glm::vec4{0.0f, 1.0f, 0.0f, 1.0f},
            glm::vec4{0.0f, 0.0f, 1.0f, 1.0f},
        };
    vertices.Properties.GetOrAdd<float>("v:temperature", 0.0f)
        .Vector() = {0.0f, 0.5f, 1.0f};
    registry.Raw().emplace<Runtime::ProgressivePresentationBindings>(
        mesh,
        MakeProgressiveMeshPresentationBindings());
    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));

    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;

    const Runtime::ProgressiveDefaultValue newColor{
        .Kind = Runtime::ProgressivePropertyValueKind::Vec4,
        .Vector = glm::vec4{0.9f, 0.1f, 0.2f, 1.0f},
    };
    EXPECT_EQ(Runtime::ApplySandboxEditorProgressiveSlotDefaultCommand(
                  context,
                  Runtime::SandboxEditorProgressiveSlotDefaultCommand{
                      .StableEntityId = stableId,
                      .PresentationKey = "mesh.surface",
                      .Semantic = Runtime::ProgressiveSlotSemantic::Albedo,
                      .Value = newColor,
                  }),
              Runtime::SandboxEditorCommandStatus::Applied);
    EXPECT_TRUE(history.IsDirty());
    auto& bindings =
        registry.Raw().get<Runtime::ProgressivePresentationBindings>(mesh);
    EXPECT_EQ(bindings.BindingGeneration, 8u);
    auto* presentation =
        Runtime::FindPresentationBinding(bindings, "mesh.surface");
    ASSERT_NE(presentation, nullptr);
    auto* albedo =
        Runtime::FindSlotBinding(*presentation,
                                 Runtime::ProgressiveSlotSemantic::Albedo);
    ASSERT_NE(albedo, nullptr);
    EXPECT_EQ(albedo->SourceKind,
              Runtime::ProgressiveSlotSourceKind::UniformDefault);
    EXPECT_EQ(albedo->UniformDefault.Vector, newColor.Vector);

    EXPECT_EQ(history.Undo().Status,
              Runtime::EditorCommandHistoryStatus::Undone);
    const auto& restored =
        registry.Raw().get<Runtime::ProgressivePresentationBindings>(mesh);
    EXPECT_EQ(restored.BindingGeneration, 7u);

    EXPECT_EQ(Runtime::ApplySandboxEditorProgressiveSlotPropertyCommand(
                  context,
                  Runtime::SandboxEditorProgressiveSlotPropertyCommand{
                      .StableEntityId = stableId,
                      .PresentationKey = "mesh.surface",
                      .Semantic = Runtime::ProgressiveSlotSemantic::Albedo,
                      .SourceKind =
                          Runtime::ProgressiveSlotSourceKind::PropertyBake,
                      .Domain = Runtime::ProgressiveGeometryDomain::MeshVertex,
                      .ExpectedValueKind =
                          Runtime::ProgressivePropertyValueKind::Vec4,
                      .PropertyName = "v:paint",
                  }),
              Runtime::SandboxEditorCommandStatus::Applied);
    auto& propertyBindings =
        registry.Raw().get<Runtime::ProgressivePresentationBindings>(mesh);
    EXPECT_EQ(propertyBindings.BindingGeneration, 8u);
    presentation = Runtime::FindPresentationBinding(propertyBindings, "mesh.surface");
    ASSERT_NE(presentation, nullptr);
    albedo =
        Runtime::FindSlotBinding(*presentation,
                                 Runtime::ProgressiveSlotSemantic::Albedo);
    ASSERT_NE(albedo, nullptr);
    EXPECT_EQ(albedo->SourceKind,
              Runtime::ProgressiveSlotSourceKind::PropertyBake);
    EXPECT_EQ(albedo->Property.PropertyName, "v:paint");
    EXPECT_EQ(albedo->Readiness, Runtime::ProgressiveReadinessState::Pending);

    EXPECT_EQ(Runtime::ApplySandboxEditorProgressiveSlotPropertyCommand(
                  context,
                  Runtime::SandboxEditorProgressiveSlotPropertyCommand{
                      .StableEntityId = stableId,
                      .PresentationKey = "mesh.surface",
                      .Semantic = Runtime::ProgressiveSlotSemantic::Albedo,
                      .SourceKind =
                          Runtime::ProgressiveSlotSourceKind::PropertyBake,
                      .Domain = Runtime::ProgressiveGeometryDomain::MeshVertex,
                      .ExpectedValueKind =
                          Runtime::ProgressivePropertyValueKind::Vec4,
                      .PropertyName = "v:temperature",
                  }),
              Runtime::SandboxEditorCommandStatus::InvalidVisualizationProperty);
}
