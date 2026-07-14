// ARCH-006 runtime Sandbox editor Models contract partition.
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
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.KMeansGpuJobQueue;
import Extrinsic.Runtime.MeshAttributeTextureBake;
import Extrinsic.Runtime.MeshPrimitiveViewPacker;
import Extrinsic.Runtime.ProgressiveRenderData;
import Extrinsic.Runtime.PrimitiveSelectionRefinement;
import Extrinsic.Runtime.RenderArtifactPublication;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.SandboxDefaultPolicies;
import Extrinsic.Runtime.SandboxEditorFacades;
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
constexpr std::uint32_t kInvalidIndex =
        std::numeric_limits<std::uint32_t>::max();

[[nodiscard]] bool HasDiagnostic(
        const std::vector<Runtime::SandboxEditorDiagnostic>& diagnostics,
        const Runtime::SandboxEditorDiagnosticCode code)
    {
        for (const Runtime::SandboxEditorDiagnostic& diagnostic : diagnostics)
        {
            if (diagnostic.Code == code)
                return true;
        }
        return false;
    }

[[nodiscard]] Graphics::RenderRecipeConfigContext
    MakeRenderRecipeConfigContext()
    {
        Graphics::RenderFrameInput input{};
        input.Viewport = Core::Extent2D{.Width = 1280u, .Height = 720u};
        input.Camera.Valid = true;
        return Graphics::RenderRecipeConfigContext{
            .Renderer = Graphics::MakeCurrentRendererDescriptor(),
            .BaseRecipe = Graphics::MakeCurrentRendererRecipeDescriptor(),
            .BaseViewOutput =
                Graphics::MakeCurrentRendererViewOutputRecipe(input),
            .BaseBindings = Graphics::MakeCurrentRendererBindingSet(),
        };
    }

[[nodiscard]] Runtime::SandboxEditorContext MakeRenderRecipeEditorContext(
        Graphics::RenderRecipeConfigContext& recipeContext,
        Runtime::SandboxEditorRenderRecipeEditorState& editorState,
        Runtime::RenderArtifactRegistry* artifacts = nullptr,
        const bool commandsAvailable = true)
    {
        return Runtime::SandboxEditorContext{
            .RenderRecipeContext = &recipeContext,
            .RenderRecipeEditorState = &editorState,
            .PreviewRenderRecipeDocument =
                [&recipeContext](const std::string& document,
                                 const std::string& sourceId)
                {
                    return Graphics::PreviewRenderRecipeConfig(
                        document,
                        recipeContext,
                        Graphics::RenderRecipeConfigParseOptions{
                            .SourceId = sourceId,
                        });
                },
            .RenderArtifacts = artifacts,
            .ImGuiAdapterAvailable = true,
            .RenderRecipeCommandsAvailable = commandsAvailable,
        };
    }

[[nodiscard]] Runtime::RenderArtifactDeclaration
    MakeSandboxRenderArtifact(std::string artifactId)
    {
        return Runtime::RenderArtifactDeclaration{
            .Metadata =
                Graphics::RenderArtifactMetadata{
                    .ArtifactId = std::move(artifactId),
                    .RendererId =
                        std::string{Graphics::kCurrentRendererContractId},
                    .SnapshotId = "sandbox-snapshot",
                    .ViewOutputRecipeId =
                        std::string{Graphics::kCurrentRendererDefaultViewRecipeId},
                    .SourceRevisions = {"scene:1"},
                    .Status = Graphics::RenderArtifactStatus::Available,
                    .Lifetime = Graphics::RenderArtifactLifetime::Cached,
                    .Purpose = "color",
                },
            .Kind =
                Runtime::RenderArtifactPublicationKind::CandidateProjectResult,
            .PayloadUri = "memory://sandbox-render-artifact",
            .ProducerLabel = "sandbox editor test",
        };
    }

[[nodiscard]] const Runtime::SandboxEditorRenderRecipeSlotModel*
    FindRecipeSlotRow(
        const Runtime::SandboxEditorRenderRecipeEditorModel& model,
        const std::string_view name)
    {
        const auto it = std::find_if(
            model.Slots.begin(),
            model.Slots.end(),
            [name](const Runtime::SandboxEditorRenderRecipeSlotModel& slot)
            {
                return slot.StableName == name;
            });
        return it == model.Slots.end() ? nullptr : &*it;
    }

[[nodiscard]] const Runtime::SandboxEditorRenderRecipeBindingOverrideModel*
    FindRecipeBindingRow(
        const Runtime::SandboxEditorRenderRecipeEditorModel& model,
        const std::string_view name)
    {
        const auto it = std::find_if(
            model.BindingOverrides.begin(),
            model.BindingOverrides.end(),
            [name](
                const Runtime::SandboxEditorRenderRecipeBindingOverrideModel&
                    binding)
            {
                return binding.SemanticName == name;
            });
        return it == model.BindingOverrides.end() ? nullptr : &*it;
    }

[[nodiscard]] const Runtime::SandboxEditorRenderRecipeOutputModel*
    FindRecipeOutputRow(
        const Runtime::SandboxEditorRenderRecipeEditorModel& model,
        const std::string_view name)
    {
        const auto it = std::find_if(
            model.Outputs.begin(),
            model.Outputs.end(),
            [name](const Runtime::SandboxEditorRenderRecipeOutputModel& output)
            {
                return output.Name == name;
            });
        return it == model.Outputs.end() ? nullptr : &*it;
    }

[[nodiscard]] const Runtime::SandboxEditorRenderArtifactRow*
    FindRenderArtifactRow(
        const Runtime::SandboxEditorRenderRecipeEditorModel& model,
        const std::string_view artifactId)
    {
        const auto it = std::find_if(
            model.Artifacts.begin(),
            model.Artifacts.end(),
            [artifactId](const Runtime::SandboxEditorRenderArtifactRow& row)
            {
                return row.ArtifactId == artifactId;
            });
        return it == model.Artifacts.end() ? nullptr : &*it;
    }

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

[[nodiscard]] const Runtime::SandboxEditorVisualizationPropertyInfo*
    FindVisualizationProperty(
        const std::vector<Runtime::SandboxEditorVisualizationPropertyInfo>& properties,
        const Runtime::SandboxEditorVisualizationPropertyDomain domain,
        const std::string& name)
    {
        for (const Runtime::SandboxEditorVisualizationPropertyInfo& property :
             properties)
        {
            if (property.Domain == domain && property.Name == name)
                return &property;
        }
        return nullptr;
    }

[[nodiscard]] const Runtime::SandboxEditorPropertyCatalogRow*
    FindCatalogProperty(
        const Runtime::SandboxEditorPropertyCatalogModel& catalog,
        const Runtime::SandboxEditorPropertyCatalogDomain domain,
        const std::string& name)
    {
        for (const Runtime::SandboxEditorPropertyCatalogRow& row :
             catalog.Rows)
        {
            if (row.Domain == domain && row.Name == name)
                return &row;
        }
        return nullptr;
    }

[[nodiscard]] const Runtime::SandboxEditorBoundRenderStateRow* FindBoundRow(
        const Runtime::SandboxEditorBoundRenderStateModel& bound,
        const Runtime::SandboxEditorBoundRenderStateRowKind kind,
        const Runtime::ProgressiveSlotSemantic semantic)
    {
        for (const Runtime::SandboxEditorBoundRenderStateRow& row :
             bound.Rows)
        {
            if (row.Kind == kind && row.Semantic == semantic)
                return &row;
        }
        return nullptr;
    }

[[nodiscard]] const Runtime::SandboxEditorBoundRenderStateRow* FindBoundRowLabel(
        const Runtime::SandboxEditorBoundRenderStateModel& bound,
        const Runtime::SandboxEditorBoundRenderStateRowKind kind,
        const std::string& label)
    {
        for (const Runtime::SandboxEditorBoundRenderStateRow& row :
             bound.Rows)
        {
            if (row.Kind == kind && row.Label == label)
                return &row;
        }
        return nullptr;
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

[[nodiscard]] const Runtime::SandboxEditorProgressiveSlotModel*
    FindProgressiveSlot(
        const Runtime::SandboxEditorProgressiveRenderDataModel& model,
        const Runtime::ProgressiveSlotSemantic semantic)
    {
        for (const Runtime::SandboxEditorProgressiveSlotModel& slot :
             model.Slots)
        {
            if (slot.Semantic == semantic)
                return &slot;
        }
        return nullptr;
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

[[nodiscard]] Runtime::SandboxEditorModelBuildRequest
    MakeNoSandboxEditorModelBuildRequest()
    {
        Runtime::SandboxEditorModelBuildRequest request{};
        request.Hierarchy = false;
        request.Inspector = false;
        request.Selection = false;
        request.Document = false;
        request.SceneFile = false;
        request.FileImport = false;
        request.AssetImportQueue = false;
        request.RenderGraph = false;
        request.RenderRecipe = false;
        request.CameraRender = false;
        request.Visualization = false;
        return request;
    }

[[nodiscard]] Runtime::SandboxEditorModelBuildRequest
    MakeOnlyInspectorModelBuildRequest()
    {
        Runtime::SandboxEditorModelBuildRequest request =
            MakeNoSandboxEditorModelBuildRequest();
        request.Inspector = true;
        return request;
    }

[[nodiscard]] Runtime::SandboxEditorModelBuildRequest
    MakeOnlyVisualizationModelBuildRequest()
    {
        Runtime::SandboxEditorModelBuildRequest request =
            MakeNoSandboxEditorModelBuildRequest();
        request.Visualization = true;
        return request;
    }
}
TEST(SandboxEditorUi, EmptyContextProducesDeterministicDisabledDiagnostics)
{
    const Runtime::SandboxEditorContext context{};
    const Runtime::SandboxEditorPanelFrame frame =
        Runtime::BuildSandboxEditorPanelFrame(context);

    EXPECT_TRUE(HasDiagnostic(frame.Diagnostics,
                              Runtime::SandboxEditorDiagnosticCode::MissingScene));
    EXPECT_TRUE(HasDiagnostic(frame.Diagnostics,
                              Runtime::SandboxEditorDiagnosticCode::MissingSelectionController));
    EXPECT_TRUE(HasDiagnostic(frame.Diagnostics,
                              Runtime::SandboxEditorDiagnosticCode::MissingImGuiAdapter));
    EXPECT_FALSE(frame.SceneFile.Enabled);
    EXPECT_TRUE(HasDiagnostic(frame.SceneFile.Diagnostics,
                              Runtime::SandboxEditorDiagnosticCode::SceneFileUnavailable));
    EXPECT_FALSE(frame.FileImport.Enabled);
    EXPECT_TRUE(HasDiagnostic(frame.FileImport.Diagnostics,
                              Runtime::SandboxEditorDiagnosticCode::AssetImportUnavailable));
    EXPECT_FALSE(frame.RenderGraph.Enabled);
    EXPECT_TRUE(HasDiagnostic(frame.RenderGraph.Diagnostics,
                              Runtime::SandboxEditorDiagnosticCode::RenderGraphStatsUnavailable));
    EXPECT_TRUE(HasDiagnostic(frame.Inspector.Diagnostics,
                              Runtime::SandboxEditorDiagnosticCode::MissingScene));
}
TEST(SandboxEditorUi, RenderGraphPanelModelCopiesRendererStats)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);

    Graphics::RenderGraphFrameStats stats{};
    stats.Compile.Succeeded = true;
    stats.Compile.PassCount = 14u;
    stats.Compile.CulledPassCount = 2u;
    stats.Compile.ResourceCount = 9u;
    stats.Compile.BarrierCount = 17u;
    stats.Compile.QueueHandoffEdgeCount = 3u;
    stats.Compile.CrossQueueTimelineEdgeCount = 4u;
    stats.Compile.CrossQueueTimelineSignalCount = 5u;
    stats.Compile.CrossQueueTimelineWaitCount = 6u;
    stats.Compile.CrossQueueOwnershipTransferCount = 7u;
    stats.Compile.TransientMemoryEstimateBytes = 4096u;
    stats.Compile.TimeMicros = 123u;
    stats.Execute.Succeeded = true;
    stats.Execute.DeviceOperational = true;
    stats.Execute.TimeMicros = 456u;
    stats.CommandRecords.Recorded = 2u;
    stats.CommandRecords.Skipped = 1u;
    stats.CommandRecords.SkippedNonOperational = 0u;
    stats.CommandRecords.SkippedUnavailable = 1u;
    stats.CommandRecords.Passes.push_back(
        Graphics::RenderGraphCommandPassStats{
            .Name = "DepthPrepass",
            .Id = Graphics::FramePassId{11u},
            .Status = Graphics::RenderCommandPassStatus::Recorded,
        });
    stats.CommandRecords.Passes.push_back(
        Graphics::RenderGraphCommandPassStats{
            .Name = "DebugViewPass",
            .Id = Graphics::FramePassId{},
            .Status = Graphics::RenderCommandPassStatus::SkippedUnavailable,
        });
    stats.AsyncComputeUtilizedFrames = 1u;
    stats.DebugDump = "passes:\\n  DepthPrepass -> DebugViewPass";
    stats.Diagnostic = "Frame graph diagnostic.";
    stats.LifecycleDiagnostic = "Renderer lifecycle diagnostic.";
    context.RenderGraphStats = &stats;

    const Runtime::SandboxEditorPanelFrame frame =
        Runtime::BuildSandboxEditorPanelFrame(context);

    EXPECT_TRUE(frame.RenderGraph.Enabled);
    EXPECT_TRUE(frame.RenderGraph.CompileSucceeded);
    EXPECT_TRUE(frame.RenderGraph.ExecuteSucceeded);
    EXPECT_TRUE(frame.RenderGraph.DeviceOperational);
    EXPECT_EQ(frame.RenderGraph.PassCount, 14u);
    EXPECT_EQ(frame.RenderGraph.CulledPassCount, 2u);
    EXPECT_EQ(frame.RenderGraph.ResourceCount, 9u);
    EXPECT_EQ(frame.RenderGraph.BarrierCount, 17u);
    EXPECT_EQ(frame.RenderGraph.QueueHandoffEdgeCount, 3u);
    EXPECT_EQ(frame.RenderGraph.CrossQueueTimelineEdgeCount, 4u);
    EXPECT_EQ(frame.RenderGraph.CrossQueueTimelineSignalCount, 5u);
    EXPECT_EQ(frame.RenderGraph.CrossQueueTimelineWaitCount, 6u);
    EXPECT_EQ(frame.RenderGraph.CrossQueueOwnershipTransferCount, 7u);
    EXPECT_EQ(frame.RenderGraph.TransientMemoryEstimateBytes, 4096u);
    EXPECT_EQ(frame.RenderGraph.CompileTimeMicros, 123u);
    EXPECT_EQ(frame.RenderGraph.ExecuteTimeMicros, 456u);
    EXPECT_EQ(frame.RenderGraph.CommandPassesRecorded, 2u);
    EXPECT_EQ(frame.RenderGraph.CommandPassesSkipped, 1u);
    EXPECT_EQ(frame.RenderGraph.CommandPassesSkippedNonOperational, 0u);
    EXPECT_EQ(frame.RenderGraph.CommandPassesSkippedUnavailable, 1u);
    EXPECT_EQ(frame.RenderGraph.AsyncComputeUtilizedFrames, 1u);
    EXPECT_EQ(frame.RenderGraph.DebugDump,
              "passes:\\n  DepthPrepass -> DebugViewPass");
    EXPECT_EQ(frame.RenderGraph.StatusText, "Frame graph diagnostic.");
    EXPECT_EQ(frame.RenderGraph.LifecycleDiagnostic,
              "Renderer lifecycle diagnostic.");
    ASSERT_EQ(frame.RenderGraph.CommandPasses.size(), 2u);
    EXPECT_EQ(frame.RenderGraph.CommandPasses[0].Name, "DepthPrepass");
    EXPECT_TRUE(frame.RenderGraph.CommandPasses[0].HasTypedId);
    EXPECT_EQ(frame.RenderGraph.CommandPasses[0].TypedId, 11u);
    EXPECT_EQ(frame.RenderGraph.CommandPasses[0].Status, "Recorded");
    EXPECT_EQ(frame.RenderGraph.CommandPasses[1].Name, "DebugViewPass");
    EXPECT_FALSE(frame.RenderGraph.CommandPasses[1].HasTypedId);
    EXPECT_EQ(frame.RenderGraph.CommandPasses[1].Status,
              "SkippedUnavailable");
}
TEST(SandboxEditorUi, AssetImportQueueModelCopiesRowsProgressAndCommands)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);

    const auto now = std::chrono::steady_clock::now();
    const Runtime::RuntimeAssetIngestHandle activeHandle{3u, 1u};
    const Runtime::RuntimeAssetIngestHandle failedHandle{4u, 1u};

    context.AssetImportQueue = Runtime::RuntimeAssetImportQueueSnapshot{
        .Entries = {
            Runtime::RuntimeAssetImportQueueEntry{
                .Operation = activeHandle,
                .Sequence = 10u,
                .Source = Runtime::RuntimeAssetIngestSource::DroppedFile,
                .SourcePath = "/tmp/mesh.obj",
                .PathBasename = "mesh.obj",
                .PayloadKind = Assets::AssetPayloadKind::Mesh,
                .Stage = Runtime::RuntimeAssetImportQueueStage::Decoding,
                .TerminalStatus =
                    Runtime::RuntimeAssetImportQueueTerminalStatus::None,
                .EnqueuedAt = now - std::chrono::seconds(3),
                .StartedAt = now - std::chrono::seconds(2),
                .LastUpdatedAt = now - std::chrono::seconds(1),
                .ProgressDeterminate = false,
                .NormalizedProgress = 0.45f,
                .StageText = "Decoding",
                .CanCancel = true,
            },
            Runtime::RuntimeAssetImportQueueEntry{
                .Operation = failedHandle,
                .Sequence = 11u,
                .Source = Runtime::RuntimeAssetIngestSource::ManualImport,
                .SourcePath = "/tmp/missing.obj",
                .PathBasename = "missing.obj",
                .PayloadKind = Assets::AssetPayloadKind::Mesh,
                .Stage = Runtime::RuntimeAssetImportQueueStage::Failed,
                .TerminalStatus =
                    Runtime::RuntimeAssetImportQueueTerminalStatus::Failed,
                .EnqueuedAt = now - std::chrono::seconds(5),
                .FinishedAt = now - std::chrono::seconds(4),
                .LastUpdatedAt = now - std::chrono::seconds(4),
                .ProgressDeterminate = true,
                .NormalizedProgress = 1.0f,
                .StageText = "Failed",
                .DiagnosticText = "MissingFile: FileNotFound",
                .CanCancel = false,
                .CancelDisabledReason =
                    "Import has already reached a terminal state.",
            },
        },
        .ActiveCount = 1u,
        .TerminalCount = 1u,
        .CanClearCompleted = true,
    };

    std::size_t clearObserved = 0u;
    Runtime::RuntimeAssetIngestHandle cancelled{};
    context.AssetImportQueueCommands =
        Runtime::SandboxEditorAssetImportQueueCommandSurface{
            .ClearCompleted =
                [&clearObserved]()
                {
                    clearObserved = 1u;
                    return 1u;
                },
            .Cancel =
                [&cancelled](Runtime::RuntimeAssetIngestHandle operation)
                {
                    cancelled = operation;
                    return Core::Ok();
                },
        };

    const Runtime::SandboxEditorPanelFrame frame =
        Runtime::BuildSandboxEditorPanelFrame(context);
    EXPECT_EQ(frame.AssetImportQueue.ActiveCount, 1u);
    EXPECT_EQ(frame.AssetImportQueue.TerminalCount, 1u);
    EXPECT_TRUE(frame.AssetImportQueue.CanClearCompleted);
    ASSERT_EQ(frame.AssetImportQueue.Rows.size(), 2u);
    EXPECT_EQ(frame.AssetImportQueue.Rows[0].Operation, activeHandle);
    EXPECT_EQ(frame.AssetImportQueue.Rows[0].PathBasename, "mesh.obj");
    EXPECT_EQ(frame.AssetImportQueue.Rows[0].Stage,
              Runtime::RuntimeAssetImportQueueStage::Decoding);
    EXPECT_FALSE(frame.AssetImportQueue.Rows[0].ProgressDeterminate);
    EXPECT_TRUE(frame.AssetImportQueue.Rows[0].CanCancel);
    EXPECT_GE(frame.AssetImportQueue.Rows[0].ElapsedSeconds, 0.0);
    EXPECT_EQ(frame.AssetImportQueue.Rows[1].TerminalStatus,
              Runtime::RuntimeAssetImportQueueTerminalStatus::Failed);
    EXPECT_EQ(frame.AssetImportQueue.Rows[1].DiagnosticText,
              "MissingFile: FileNotFound");
    EXPECT_FALSE(frame.AssetImportQueue.Rows[1].CanCancel);
    EXPECT_FALSE(HasDiagnostic(
        frame.AssetImportQueue.Diagnostics,
        Runtime::SandboxEditorDiagnosticCode::AssetImportUnavailable));

    ASSERT_TRUE(context.AssetImportQueueCommands.CancelAvailable());
    EXPECT_TRUE(context.AssetImportQueueCommands.Cancel(activeHandle).has_value());
    EXPECT_EQ(cancelled, activeHandle);
    ASSERT_TRUE(context.AssetImportQueueCommands.ClearAvailable());
    EXPECT_EQ(context.AssetImportQueueCommands.ClearCompleted(), 1u);
    EXPECT_EQ(clearObserved, 1u);
}
TEST(SandboxEditorUi, DocumentModelReportsRuntimeHistoryDirtyState)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    int value = 0;

    ASSERT_TRUE(history.Execute(
        Runtime::EditorCommandRecord{
            .Label = "Edit Value",
            .Redo = [&value]()
            {
                value = 1;
                return Runtime::EditorCommandHistoryStatus::Applied;
            },
            .Undo = [&value]()
            {
                value = 0;
                return Runtime::EditorCommandHistoryStatus::Applied;
            },
        }).Succeeded());

    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;
    Runtime::SandboxEditorPanelFrame frame =
        Runtime::BuildSandboxEditorPanelFrame(context);

    EXPECT_TRUE(frame.Document.HistoryAvailable);
    EXPECT_TRUE(frame.Document.Dirty);
    EXPECT_TRUE(frame.Document.CanUndo);
    EXPECT_FALSE(frame.Document.CanRedo);
    EXPECT_EQ(frame.Document.UndoLabel, "Edit Value");
    EXPECT_TRUE(frame.Document.StatusText.find("unsaved") != std::string::npos);
    EXPECT_TRUE(frame.Document.Diagnostics.empty());

    history.MarkSaved("scene.extrinsic.json");
    frame = Runtime::BuildSandboxEditorPanelFrame(context);
    EXPECT_FALSE(frame.Document.Dirty);
    EXPECT_TRUE(frame.Document.HasActivePath);
    EXPECT_EQ(frame.Document.ActivePath, "scene.extrinsic.json");
}
TEST(SandboxEditorUi, HierarchyInspectorModelReportsSelectionRenderHintsAndDomain)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;

    const ECS::EntityHandle first = MakeSelectable(registry, "Cloud A");
    const ECS::EntityHandle second = MakeSelectable(registry, "Cloud B");
    registry.Raw().emplace<ECSC::StableId>(first, ECSC::StableId{1u, 2u});
    AddPointCloudSource(registry, first, 3u);
    AddPointCloudSource(registry, second, 1u);

    auto& raw = registry.Raw();
    auto& transform = raw.get<ECSC::Transform::Component>(first);
    transform.Position = glm::vec3{1.0f, 2.0f, 3.0f};
    transform.Rotation = glm::quat{0.5f, 0.5f, 0.5f, 0.5f};
    transform.Scale = glm::vec3{2.0f, 3.0f, 4.0f};
    raw.get<ECSC::Transform::WorldMatrix>(first).Matrix[3] =
        glm::vec4{7.0f, 8.0f, 9.0f, 1.0f};

    auto& surface = raw.emplace<G::RenderSurface>(first);
    surface.Domain = G::RenderSurface::SourceDomain::Face;
    auto& lines = raw.emplace<G::RenderEdges>(first);
    lines.Domain = G::RenderEdges::SourceDomain::Edge;
    lines.WidthSource = 2.5f;
    auto& points = raw.get<G::RenderPoints>(first);
    points.Type = G::RenderPoints::RenderType::Surfel;
    points.SizeSource = std::string{"v:radius"};

    ASSERT_TRUE(selection.SetSelectedEntity(registry, first));

    const Runtime::SandboxEditorPanelFrame frame =
        Runtime::BuildSandboxEditorPanelFrame(MakeContext(registry, selection));

    ASSERT_EQ(frame.Hierarchy.size(), 2u);
    const auto selected = std::find_if(
        frame.Hierarchy.begin(),
        frame.Hierarchy.end(),
        [first](const Runtime::SandboxEditorEntityRow& row)
        {
            return row.Entity == first;
        });
    ASSERT_NE(selected, frame.Hierarchy.end());
    EXPECT_EQ(selected->Name, "Cloud A");
    EXPECT_TRUE(selected->Selectable);
    EXPECT_TRUE(selected->Selected);
    EXPECT_TRUE(selected->HasDurableStableId);

    ASSERT_TRUE(frame.Inspector.HasEntity);
    EXPECT_EQ(frame.Inspector.Entity.Entity, first);
    EXPECT_EQ(frame.Inspector.Entity.Name, "Cloud A");
    EXPECT_TRUE(frame.Inspector.Transform.HasLocalTransform);
    EXPECT_TRUE(frame.Inspector.Transform.HasWorldTransform);
    EXPECT_FLOAT_EQ(frame.Inspector.Transform.LocalPosition.x, 1.0f);
    EXPECT_FLOAT_EQ(frame.Inspector.Transform.LocalPosition.y, 2.0f);
    EXPECT_FLOAT_EQ(frame.Inspector.Transform.LocalPosition.z, 3.0f);
    EXPECT_FLOAT_EQ(frame.Inspector.Transform.LocalRotation.w, 0.5f);
    EXPECT_FLOAT_EQ(frame.Inspector.Transform.LocalScale.x, 2.0f);
    EXPECT_FLOAT_EQ(frame.Inspector.Transform.LocalScale.y, 3.0f);
    EXPECT_FLOAT_EQ(frame.Inspector.Transform.LocalScale.z, 4.0f);
    EXPECT_FLOAT_EQ(frame.Inspector.Transform.WorldPosition.x, 7.0f);
    EXPECT_FLOAT_EQ(frame.Inspector.Transform.WorldPosition.y, 8.0f);
    EXPECT_FLOAT_EQ(frame.Inspector.Transform.WorldPosition.z, 9.0f);
    EXPECT_TRUE(frame.Inspector.RenderHints.HasRenderSurface);
    EXPECT_EQ(frame.Inspector.RenderHints.SurfaceDomain, "Face");
    EXPECT_TRUE(frame.Inspector.RenderHints.HasRenderEdges);
    EXPECT_EQ(frame.Inspector.RenderHints.EdgeDomain, "Edge");
    EXPECT_TRUE(frame.Inspector.RenderHints.HasUniformEdgeWidth);
    EXPECT_FLOAT_EQ(frame.Inspector.RenderHints.UniformEdgeWidth, 2.5f);
    EXPECT_TRUE(frame.Inspector.RenderHints.HasRenderPoints);
    EXPECT_EQ(frame.Inspector.RenderHints.PointRenderType, "Surfel");
    EXPECT_TRUE(frame.Inspector.RenderHints.HasNamedPointSize);
    EXPECT_EQ(frame.Inspector.RenderHints.PointSizeName, "v:radius");
    EXPECT_EQ(frame.Inspector.Geometry.Domain, GS::Domain::PointCloud);
    EXPECT_TRUE(frame.Inspector.Geometry.Valid);
    EXPECT_EQ(frame.Inspector.Geometry.VertexCount, 3u);
    EXPECT_TRUE(Runtime::HasAnySandboxEditorGeometryProcessingDomain(
        frame.Inspector.Processing.Domains,
        Runtime::SandboxEditorGeometryProcessingDomain::PointCloudPoints));
    EXPECT_FALSE(frame.Inspector.Processing.HasEditableSurfaceMesh);

    ASSERT_EQ(frame.Selection.SelectedStableIds.size(), 1u);
    EXPECT_EQ(frame.Selection.SelectedStableIds[0],
              Runtime::SelectionController::ToStableEntityId(first));
    ASSERT_EQ(frame.Selection.SelectedEntities.size(), 1u);
    EXPECT_EQ(frame.Selection.SelectedEntities[0].Name, "Cloud A");
    EXPECT_FALSE(frame.FileImport.Enabled);
}
TEST(SandboxEditorUi, HiddenPanelBuildRequestSkipsSelectedEntityModels)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;

    const ECS::EntityHandle mesh = MakeSelectable(registry, "Mesh");
    AddTriangleMeshSource(registry, mesh);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));

    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    const Runtime::SandboxEditorPanelFrame frame =
        Runtime::BuildSandboxEditorPanelFrame(
            context,
            MakeNoSandboxEditorModelBuildRequest());

    EXPECT_TRUE(frame.Hierarchy.empty());
    EXPECT_FALSE(frame.Inspector.HasEntity);
    EXPECT_TRUE(frame.Selection.SelectedStableIds.empty());
    EXPECT_TRUE(frame.Visualization.Properties.empty());

    const Runtime::SandboxEditorModelBuildStats& stats =
        frame.ModelBuildStats;
    EXPECT_EQ(stats.HierarchyModelBuilds, 0u);
    EXPECT_EQ(stats.InspectorModelBuilds, 0u);
    EXPECT_EQ(stats.SelectionModelBuilds, 0u);
    EXPECT_EQ(stats.PropertyCatalogModelBuilds, 0u);
    EXPECT_EQ(stats.VertexChannelTargetBuilds, 0u);
    EXPECT_EQ(stats.VertexChannelResolverScans, 0u);
    EXPECT_EQ(stats.VertexChannelScratchAllocations, 0u);
    EXPECT_EQ(stats.VertexChannelScratchBytes, 0u);
    EXPECT_EQ(stats.ProgressiveModelBuilds, 0u);
    EXPECT_EQ(stats.BoundStateModelBuilds, 0u);
    EXPECT_EQ(stats.UvDiagnosticsModelBuilds, 0u);
    EXPECT_EQ(stats.UvDiagnosticsTexcoordElementsScanned, 0u);
    EXPECT_EQ(stats.TextureBakeModelBuilds, 0u);
    EXPECT_EQ(stats.TextureBakeSourceRowsEnumerated, 0u);
    EXPECT_EQ(stats.VisualizationModelBuilds, 0u);
    EXPECT_EQ(stats.InspectorModelBuildTimeNs, 0u);
    EXPECT_EQ(stats.SelectedAnalysisModelBuildTimeNs, 0u);
    EXPECT_EQ(stats.PropertyCatalogModelBuildTimeNs, 0u);
    EXPECT_EQ(stats.VertexChannelValidationTimeNs, 0u);
    EXPECT_EQ(stats.UvDiagnosticsModelBuildTimeNs, 0u);
    EXPECT_EQ(stats.TextureBakeModelBuildTimeNs, 0u);
    EXPECT_EQ(stats.VisualizationModelBuildTimeNs, 0u);
    EXPECT_EQ(stats.DomainWindowModelBuildTimeNs, 0u);
}
TEST(SandboxEditorUi, InspectorOnlyBuildRequestAvoidsSiblingPanelWork)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;

    const ECS::EntityHandle mesh = MakeSelectable(registry, "Mesh");
    AddTriangleMeshSource(registry, mesh);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));

    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    const Runtime::SandboxEditorPanelFrame frame =
        Runtime::BuildSandboxEditorPanelFrame(
            context,
            MakeOnlyInspectorModelBuildRequest());

    ASSERT_TRUE(frame.Inspector.HasEntity);
    EXPECT_TRUE(frame.Hierarchy.empty());
    EXPECT_TRUE(frame.Selection.SelectedStableIds.empty());
    EXPECT_TRUE(frame.Visualization.Properties.empty());

    const Runtime::SandboxEditorModelBuildStats& stats =
        frame.ModelBuildStats;
    EXPECT_EQ(stats.HierarchyModelBuilds, 0u);
    EXPECT_EQ(stats.InspectorModelBuilds, 1u);
    EXPECT_EQ(stats.SelectionModelBuilds, 0u);
    EXPECT_EQ(stats.PropertyCatalogModelBuilds, 1u);
    EXPECT_EQ(stats.VertexChannelTargetBuilds, 2u);
    EXPECT_GT(stats.VertexChannelResolverScans, 0u);
    EXPECT_GT(stats.VertexChannelScratchAllocations, 0u);
    EXPECT_GT(stats.VertexChannelScratchBytes, 0u);
    EXPECT_EQ(stats.ProgressiveModelBuilds, 1u);
    EXPECT_EQ(stats.BoundStateModelBuilds, 1u);
    EXPECT_EQ(stats.UvDiagnosticsModelBuilds, 1u);
    EXPECT_GT(stats.UvDiagnosticsTexcoordElementsScanned, 0u);
    EXPECT_EQ(stats.TextureBakeModelBuilds, 1u);
    EXPECT_GT(stats.TextureBakeSourceRowsEnumerated, 0u);
    EXPECT_EQ(stats.VisualizationModelBuilds, 0u);
    EXPECT_GT(stats.PanelFrameModelBuildTimeNs, 0u);
    EXPECT_GT(stats.InspectorModelBuildTimeNs, 0u);
    EXPECT_GT(stats.SelectedAnalysisModelBuildTimeNs, 0u);
    EXPECT_GT(stats.PropertyCatalogModelBuildTimeNs, 0u);
    EXPECT_GT(stats.VertexChannelValidationTimeNs, 0u);
    EXPECT_GT(stats.UvDiagnosticsModelBuildTimeNs, 0u);
    EXPECT_GT(stats.TextureBakeModelBuildTimeNs, 0u);
    EXPECT_EQ(stats.VisualizationModelBuildTimeNs, 0u);
    EXPECT_EQ(stats.DomainWindowModelBuildTimeNs, 0u);
}
TEST(SandboxEditorUi, DomainWindowBuildReportsTimingDiagnostics)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;

    const ECS::EntityHandle mesh = MakeSelectable(registry, "Mesh");
    AddTriangleMeshSource(registry, mesh);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));

    Runtime::SandboxEditorModelBuildStats stats{};
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.ModelBuildStats = &stats;
    context.VisualizationCommandsAvailable = true;

    const Runtime::SandboxEditorDomainWindowModel model =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::Mesh);

    ASSERT_TRUE(model.HasSelectedEntity);
    EXPECT_EQ(stats.DomainWindowModelBuilds, 1u);
    EXPECT_EQ(stats.PropertyCatalogModelBuilds, 1u);
    EXPECT_EQ(stats.VisualizationModelBuilds, 1u);
    EXPECT_GT(stats.DomainWindowModelBuildTimeNs, 0u);
    EXPECT_GT(stats.SelectedAnalysisModelBuildTimeNs, 0u);
    EXPECT_GT(stats.PropertyCatalogModelBuildTimeNs, 0u);
    EXPECT_GT(stats.VisualizationModelBuildTimeNs, 0u);
}
TEST(SandboxEditorUi, SelectedModelCacheReusesInspectorAnalysis)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;

    const ECS::EntityHandle mesh = MakeSelectable(registry, "Mesh");
    AddTriangleMeshSource(registry, mesh);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));

    Runtime::SandboxEditorSelectedModelCache cache{};
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.SelectedModelCache = &cache;

    const Runtime::SandboxEditorPanelFrame first =
        Runtime::BuildSandboxEditorPanelFrame(
            context,
            MakeOnlyInspectorModelBuildRequest());

    ASSERT_TRUE(first.Inspector.HasEntity);
    EXPECT_EQ(first.ModelBuildStats.InspectorModelBuilds, 1u);
    EXPECT_EQ(first.ModelBuildStats.SelectedAnalysisCacheMisses, 1u);
    EXPECT_EQ(first.ModelBuildStats.SelectedAnalysisCacheHits, 0u);
    EXPECT_EQ(first.ModelBuildStats.PropertyCatalogModelBuilds, 1u);
    EXPECT_EQ(first.ModelBuildStats.VertexChannelTargetBuilds, 2u);
    EXPECT_GT(first.ModelBuildStats.VertexChannelResolverScans, 0u);
    EXPECT_GT(first.ModelBuildStats.VertexChannelScratchAllocations, 0u);
    EXPECT_GT(first.ModelBuildStats.VertexChannelScratchBytes, 0u);
    EXPECT_EQ(first.ModelBuildStats.ProgressiveModelBuilds, 1u);
    EXPECT_EQ(first.ModelBuildStats.BoundStateModelBuilds, 1u);
    EXPECT_EQ(first.ModelBuildStats.UvDiagnosticsModelBuilds, 1u);
    EXPECT_GT(first.ModelBuildStats.UvDiagnosticsTexcoordElementsScanned, 0u);
    EXPECT_EQ(first.ModelBuildStats.TextureBakeModelBuilds, 1u);
    EXPECT_GT(first.ModelBuildStats.TextureBakeSourceRowsEnumerated, 0u);
    EXPECT_GT(first.ModelBuildStats.InspectorModelBuildTimeNs, 0u);
    EXPECT_GT(first.ModelBuildStats.SelectedAnalysisModelBuildTimeNs, 0u);
    EXPECT_GT(first.ModelBuildStats.PropertyCatalogModelBuildTimeNs, 0u);
    EXPECT_GT(first.ModelBuildStats.VertexChannelValidationTimeNs, 0u);
    EXPECT_GT(first.ModelBuildStats.UvDiagnosticsModelBuildTimeNs, 0u);
    EXPECT_GT(first.ModelBuildStats.TextureBakeModelBuildTimeNs, 0u);

    const Runtime::SandboxEditorPanelFrame second =
        Runtime::BuildSandboxEditorPanelFrame(
            context,
            MakeOnlyInspectorModelBuildRequest());

    ASSERT_TRUE(second.Inspector.HasEntity);
    EXPECT_EQ(second.ModelBuildStats.InspectorModelBuilds, 1u);
    EXPECT_EQ(second.ModelBuildStats.SelectedAnalysisCacheMisses, 0u);
    EXPECT_EQ(second.ModelBuildStats.SelectedAnalysisCacheHits, 1u);
    EXPECT_EQ(second.ModelBuildStats.PropertyCatalogModelBuilds, 0u);
    EXPECT_EQ(second.ModelBuildStats.VertexChannelTargetBuilds, 0u);
    EXPECT_EQ(second.ModelBuildStats.VertexChannelResolverScans, 0u);
    EXPECT_EQ(second.ModelBuildStats.VertexChannelScratchAllocations, 0u);
    EXPECT_EQ(second.ModelBuildStats.VertexChannelScratchBytes, 0u);
    EXPECT_EQ(second.ModelBuildStats.ProgressiveModelBuilds, 0u);
    EXPECT_EQ(second.ModelBuildStats.BoundStateModelBuilds, 0u);
    EXPECT_EQ(second.ModelBuildStats.UvDiagnosticsModelBuilds, 0u);
    EXPECT_EQ(second.ModelBuildStats.UvDiagnosticsTexcoordElementsScanned, 0u);
    EXPECT_EQ(second.ModelBuildStats.TextureBakeModelBuilds, 0u);
    EXPECT_EQ(second.ModelBuildStats.TextureBakeSourceRowsEnumerated, 0u);
    EXPECT_GT(second.ModelBuildStats.InspectorModelBuildTimeNs, 0u);
    EXPECT_EQ(second.ModelBuildStats.SelectedAnalysisModelBuildTimeNs, 0u);
    EXPECT_EQ(second.ModelBuildStats.PropertyCatalogModelBuildTimeNs, 0u);
    EXPECT_EQ(second.ModelBuildStats.VertexChannelValidationTimeNs, 0u);
    EXPECT_EQ(second.ModelBuildStats.UvDiagnosticsModelBuildTimeNs, 0u);
    EXPECT_EQ(second.ModelBuildStats.TextureBakeModelBuildTimeNs, 0u);
    EXPECT_EQ(second.Inspector.PropertyCatalog.Rows.size(),
              first.Inspector.PropertyCatalog.Rows.size());

    const Runtime::SandboxEditorSelectedModelCacheStats cacheStats =
        cache.Stats();
    EXPECT_EQ(cacheStats.SelectedAnalysisCacheMisses, 1u);
    EXPECT_EQ(cacheStats.SelectedAnalysisCacheHits, 1u);
    EXPECT_EQ(cacheStats.Entries, 1u);
}
TEST(SandboxEditorUi, SelectedModelCachePartitionsAnalysisByVisibleWindow)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;

    const ECS::EntityHandle mesh = MakeSelectable(registry, "Mesh");
    AddTriangleMeshSource(registry, mesh);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));

    Runtime::SandboxEditorSelectedModelCache cache{};
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.SelectedModelCache = &cache;

    const Runtime::SandboxEditorPanelFrame inspector =
        Runtime::BuildSandboxEditorPanelFrame(
            context,
            MakeOnlyInspectorModelBuildRequest());
    ASSERT_TRUE(inspector.Inspector.HasEntity);
    EXPECT_EQ(inspector.ModelBuildStats.SelectedAnalysisCacheMisses, 1u);
    EXPECT_EQ(inspector.ModelBuildStats.SelectedAnalysisCacheHits, 0u);

    auto buildDomain =
        [&](const Runtime::SandboxEditorDomainWindowKind kind,
            Runtime::SandboxEditorModelBuildStats& stats)
    {
        stats = Runtime::SandboxEditorModelBuildStats{};
        context.ModelBuildStats = &stats;
        return Runtime::BuildSandboxEditorDomainWindowModel(context, kind);
    };

    Runtime::SandboxEditorModelBuildStats meshStats{};
    const Runtime::SandboxEditorDomainWindowModel meshModel =
        buildDomain(Runtime::SandboxEditorDomainWindowKind::Mesh, meshStats);
    ASSERT_TRUE(meshModel.HasSelectedEntity);
    EXPECT_TRUE(meshModel.DomainMatches);
    EXPECT_EQ(meshStats.SelectedAnalysisCacheMisses, 1u);
    EXPECT_EQ(meshStats.SelectedAnalysisCacheHits, 0u);
    EXPECT_EQ(meshStats.PropertyCatalogModelBuilds, 1u);
    EXPECT_GT(meshStats.SelectedAnalysisModelBuildTimeNs, 0u);

    Runtime::SandboxEditorModelBuildStats cachedMeshStats{};
    const Runtime::SandboxEditorDomainWindowModel cachedMesh =
        buildDomain(Runtime::SandboxEditorDomainWindowKind::Mesh,
                    cachedMeshStats);
    ASSERT_TRUE(cachedMesh.HasSelectedEntity);
    EXPECT_TRUE(cachedMesh.DomainMatches);
    EXPECT_EQ(cachedMeshStats.SelectedAnalysisCacheMisses, 0u);
    EXPECT_EQ(cachedMeshStats.SelectedAnalysisCacheHits, 1u);
    EXPECT_EQ(cachedMeshStats.PropertyCatalogModelBuilds, 0u);
    EXPECT_EQ(cachedMeshStats.BoundStateModelBuilds, 0u);
    EXPECT_EQ(cachedMeshStats.TextureBakeModelBuilds, 0u);
    EXPECT_EQ(cachedMeshStats.SelectedAnalysisModelBuildTimeNs, 0u);

    Runtime::SandboxEditorModelBuildStats graphStats{};
    const Runtime::SandboxEditorDomainWindowModel graphModel =
        buildDomain(Runtime::SandboxEditorDomainWindowKind::Graph, graphStats);
    ASSERT_TRUE(graphModel.HasSelectedEntity);
    EXPECT_FALSE(graphModel.DomainMatches);
    EXPECT_EQ(graphStats.SelectedAnalysisCacheMisses, 1u);
    EXPECT_EQ(graphStats.SelectedAnalysisCacheHits, 0u);
    EXPECT_EQ(graphStats.PropertyCatalogModelBuilds, 1u);

    Runtime::SandboxEditorModelBuildStats cachedGraphStats{};
    const Runtime::SandboxEditorDomainWindowModel cachedGraph =
        buildDomain(Runtime::SandboxEditorDomainWindowKind::Graph,
                    cachedGraphStats);
    ASSERT_TRUE(cachedGraph.HasSelectedEntity);
    EXPECT_FALSE(cachedGraph.DomainMatches);
    EXPECT_EQ(cachedGraphStats.SelectedAnalysisCacheMisses, 0u);
    EXPECT_EQ(cachedGraphStats.SelectedAnalysisCacheHits, 1u);
    EXPECT_EQ(cachedGraphStats.PropertyCatalogModelBuilds, 0u);
    EXPECT_EQ(cachedGraphStats.SelectedAnalysisModelBuildTimeNs, 0u);

    Runtime::SandboxEditorModelBuildStats pointStats{};
    const Runtime::SandboxEditorDomainWindowModel pointModel =
        buildDomain(Runtime::SandboxEditorDomainWindowKind::PointCloud,
                    pointStats);
    ASSERT_TRUE(pointModel.HasSelectedEntity);
    EXPECT_FALSE(pointModel.DomainMatches);
    EXPECT_EQ(pointStats.SelectedAnalysisCacheMisses, 1u);
    EXPECT_EQ(pointStats.SelectedAnalysisCacheHits, 0u);
    EXPECT_EQ(pointStats.PropertyCatalogModelBuilds, 1u);

    Runtime::SandboxEditorModelBuildStats cachedPointStats{};
    const Runtime::SandboxEditorDomainWindowModel cachedPoint =
        buildDomain(Runtime::SandboxEditorDomainWindowKind::PointCloud,
                    cachedPointStats);
    ASSERT_TRUE(cachedPoint.HasSelectedEntity);
    EXPECT_FALSE(cachedPoint.DomainMatches);
    EXPECT_EQ(cachedPointStats.SelectedAnalysisCacheMisses, 0u);
    EXPECT_EQ(cachedPointStats.SelectedAnalysisCacheHits, 1u);
    EXPECT_EQ(cachedPointStats.PropertyCatalogModelBuilds, 0u);
    EXPECT_EQ(cachedPointStats.SelectedAnalysisModelBuildTimeNs, 0u);

    context.ModelBuildStats = nullptr;
    const Runtime::SandboxEditorSelectedModelCacheStats cacheStats =
        cache.Stats();
    EXPECT_EQ(cacheStats.SelectedAnalysisCacheMisses, 4u);
    EXPECT_EQ(cacheStats.SelectedAnalysisCacheHits, 3u);
    EXPECT_EQ(cacheStats.Entries, 4u);
}
TEST(SandboxEditorUi, SelectedModelCacheInvalidatesOnSelectionGeneration)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;

    const ECS::EntityHandle mesh = MakeSelectable(registry, "Mesh");
    AddTriangleMeshSource(registry, mesh);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));

    Runtime::SandboxEditorSelectedModelCache cache{};
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.SelectedModelCache = &cache;

    const Runtime::SandboxEditorPanelFrame first =
        Runtime::BuildSandboxEditorPanelFrame(
            context,
            MakeOnlyInspectorModelBuildRequest());
    ASSERT_TRUE(first.Inspector.HasEntity);
    EXPECT_EQ(first.ModelBuildStats.SelectedAnalysisCacheMisses, 1u);

    const Runtime::SandboxEditorPanelFrame cached =
        Runtime::BuildSandboxEditorPanelFrame(
            context,
            MakeOnlyInspectorModelBuildRequest());
    ASSERT_TRUE(cached.Inspector.HasEntity);
    EXPECT_EQ(cached.ModelBuildStats.SelectedAnalysisCacheHits, 1u);
    EXPECT_EQ(cached.ModelBuildStats.PropertyCatalogModelBuilds, 0u);

    selection.ClearSelection(registry);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));

    const Runtime::SandboxEditorPanelFrame reselected =
        Runtime::BuildSandboxEditorPanelFrame(
            context,
            MakeOnlyInspectorModelBuildRequest());
    ASSERT_TRUE(reselected.Inspector.HasEntity);
    EXPECT_EQ(reselected.ModelBuildStats.SelectedAnalysisCacheMisses, 1u);
    EXPECT_EQ(reselected.ModelBuildStats.SelectedAnalysisCacheHits, 0u);
    EXPECT_EQ(reselected.ModelBuildStats.PropertyCatalogModelBuilds, 1u);

    const Runtime::SandboxEditorSelectedModelCacheStats cacheStats =
        cache.Stats();
    EXPECT_EQ(cacheStats.SelectedAnalysisCacheMisses, 2u);
    EXPECT_EQ(cacheStats.SelectedAnalysisCacheHits, 1u);
    EXPECT_EQ(cacheStats.Entries, 1u);
}
TEST(SandboxEditorUi, SelectedModelCacheInvalidatesOnPrimitiveGeneration)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;

    const ECS::EntityHandle mesh = MakeSelectable(registry, "Mesh");
    AddTriangleMeshSource(registry, mesh);
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);
    std::optional<Runtime::PrimitiveSelectionResult> primitive{
        Runtime::PrimitiveSelectionResult{
            .Status = Runtime::PrimitiveRefineStatus::Success,
            .EntityId = stableId,
            .StableId = stableId,
            .Domain = GS::Domain::Mesh,
            .Kind = Runtime::RefinedPrimitiveKind::Vertex,
            .FaceId = Runtime::kInvalidPrimitiveIndex,
            .EdgeId = Runtime::kInvalidPrimitiveIndex,
            .VertexId = 0u,
            .PointId = Runtime::kInvalidPrimitiveIndex,
        }};
    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));

    Runtime::SandboxEditorSelectedModelCache cache{};
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.SelectedModelCache = &cache;
    context.LastRefinedPrimitive = &primitive;
    context.LastRefinedPrimitiveGeneration = 1u;

    const Runtime::SandboxEditorPanelFrame first =
        Runtime::BuildSandboxEditorPanelFrame(
            context,
            MakeOnlyInspectorModelBuildRequest());
    ASSERT_TRUE(first.Inspector.HasEntity);
    EXPECT_EQ(first.ModelBuildStats.SelectedAnalysisCacheMisses, 1u);

    const Runtime::SandboxEditorPanelFrame cached =
        Runtime::BuildSandboxEditorPanelFrame(
            context,
            MakeOnlyInspectorModelBuildRequest());
    ASSERT_TRUE(cached.Inspector.HasEntity);
    EXPECT_EQ(cached.ModelBuildStats.SelectedAnalysisCacheHits, 1u);
    EXPECT_EQ(cached.ModelBuildStats.PropertyCatalogModelBuilds, 0u);

    primitive->VertexId = 1u;
    context.LastRefinedPrimitiveGeneration = 2u;

    const Runtime::SandboxEditorPanelFrame refined =
        Runtime::BuildSandboxEditorPanelFrame(
            context,
            MakeOnlyInspectorModelBuildRequest());
    ASSERT_TRUE(refined.Inspector.HasEntity);
    EXPECT_EQ(refined.ModelBuildStats.SelectedAnalysisCacheMisses, 1u);
    EXPECT_EQ(refined.ModelBuildStats.SelectedAnalysisCacheHits, 0u);
    EXPECT_EQ(refined.ModelBuildStats.PropertyCatalogModelBuilds, 1u);

    const Runtime::SandboxEditorSelectedModelCacheStats cacheStats =
        cache.Stats();
    EXPECT_EQ(cacheStats.SelectedAnalysisCacheMisses, 2u);
    EXPECT_EQ(cacheStats.SelectedAnalysisCacheHits, 1u);
    EXPECT_EQ(cacheStats.Entries, 1u);
}
TEST(SandboxEditorUi, SelectedModelCacheInvalidatesOnProgressiveBindingGeneration)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;

    const ECS::EntityHandle mesh = MakeSelectable(registry, "ProgressiveMesh");
    AddTriangleMeshSource(registry, mesh);
    registry.Raw().emplace<Runtime::ProgressivePresentationBindings>(
        mesh,
        MakeProgressiveMeshPresentationBindings());
    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));

    Runtime::SandboxEditorSelectedModelCache cache{};
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.SelectedModelCache = &cache;

    const Runtime::SandboxEditorPanelFrame first =
        Runtime::BuildSandboxEditorPanelFrame(
            context,
            MakeOnlyInspectorModelBuildRequest());
    ASSERT_TRUE(first.Inspector.HasEntity);
    ASSERT_TRUE(first.Inspector.Progressive.HasBindings);
    EXPECT_EQ(first.Inspector.Progressive.BindingGeneration, 7u);
    EXPECT_EQ(first.ModelBuildStats.SelectedAnalysisCacheMisses, 1u);
    EXPECT_EQ(first.ModelBuildStats.ProgressiveModelBuilds, 1u);

    const Runtime::SandboxEditorPanelFrame cached =
        Runtime::BuildSandboxEditorPanelFrame(
            context,
            MakeOnlyInspectorModelBuildRequest());
    ASSERT_TRUE(cached.Inspector.HasEntity);
    EXPECT_EQ(cached.ModelBuildStats.SelectedAnalysisCacheHits, 1u);
    EXPECT_EQ(cached.ModelBuildStats.ProgressiveModelBuilds, 0u);
    EXPECT_EQ(cached.Inspector.Progressive.BindingGeneration, 7u);

    auto& bindings =
        registry.Raw().get<Runtime::ProgressivePresentationBindings>(mesh);
    bindings.BindingGeneration = 8u;
    ASSERT_FALSE(bindings.Presentations.empty());
    ASSERT_GE(bindings.Presentations.front().Slots.size(), 2u);
    bindings.Presentations.front().Slots[1].Readiness =
        Runtime::ProgressiveReadinessState::Failed;
    bindings.Presentations.front().Slots[1].LastDiagnostic =
        "binding generation changed";

    const Runtime::SandboxEditorPanelFrame changed =
        Runtime::BuildSandboxEditorPanelFrame(
            context,
            MakeOnlyInspectorModelBuildRequest());
    ASSERT_TRUE(changed.Inspector.HasEntity);
    ASSERT_TRUE(changed.Inspector.Progressive.HasBindings);
    EXPECT_EQ(changed.ModelBuildStats.SelectedAnalysisCacheMisses, 1u);
    EXPECT_EQ(changed.ModelBuildStats.SelectedAnalysisCacheHits, 0u);
    EXPECT_EQ(changed.ModelBuildStats.ProgressiveModelBuilds, 1u);
    EXPECT_EQ(changed.Inspector.Progressive.BindingGeneration, 8u);

    const Runtime::SandboxEditorProgressiveSlotModel* normal =
        FindProgressiveSlot(changed.Inspector.Progressive,
                            Runtime::ProgressiveSlotSemantic::Normal);
    ASSERT_NE(normal, nullptr);
    EXPECT_EQ(normal->Readiness, Runtime::ProgressiveReadinessState::Failed);
    EXPECT_NE(normal->Diagnostic.find("binding generation changed"),
              std::string::npos);

    const Runtime::SandboxEditorSelectedModelCacheStats cacheStats =
        cache.Stats();
    EXPECT_EQ(cacheStats.SelectedAnalysisCacheMisses, 2u);
    EXPECT_EQ(cacheStats.SelectedAnalysisCacheHits, 1u);
    EXPECT_EQ(cacheStats.Entries, 1u);
}
TEST(SandboxEditorUi, SelectedModelCacheInvalidatesOnGeometryMetadataSignature)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;

    const ECS::EntityHandle mesh = MakeSelectable(registry, "Mesh");
    AddTriangleMeshSource(registry, mesh);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));

    Runtime::SandboxEditorSelectedModelCache cache{};
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.SelectedModelCache = &cache;

    const Runtime::SandboxEditorPanelFrame first =
        Runtime::BuildSandboxEditorPanelFrame(
            context,
            MakeOnlyInspectorModelBuildRequest());
    ASSERT_TRUE(first.Inspector.HasEntity);
    EXPECT_EQ(first.ModelBuildStats.SelectedAnalysisCacheMisses, 1u);
    EXPECT_EQ(first.ModelBuildStats.PropertyCatalogModelBuilds, 1u);
    EXPECT_EQ(FindCatalogProperty(first.Inspector.PropertyCatalog,
                                  Runtime::SandboxEditorPropertyCatalogDomain::
                                      MeshVertices,
                                  "v:temperature"),
              nullptr);

    const Runtime::SandboxEditorPanelFrame cached =
        Runtime::BuildSandboxEditorPanelFrame(
            context,
            MakeOnlyInspectorModelBuildRequest());
    ASSERT_TRUE(cached.Inspector.HasEntity);
    EXPECT_EQ(cached.ModelBuildStats.SelectedAnalysisCacheHits, 1u);
    EXPECT_EQ(cached.ModelBuildStats.PropertyCatalogModelBuilds, 0u);

    auto& vertices = registry.Raw().get<GS::Vertices>(mesh);
    auto temperature =
        vertices.Properties.GetOrAdd<float>("v:temperature", 0.0f);
    ASSERT_EQ(temperature.Vector().size(), 3u);
    temperature.Vector()[1] = 42.0f;

    const Runtime::SandboxEditorPanelFrame changed =
        Runtime::BuildSandboxEditorPanelFrame(
            context,
            MakeOnlyInspectorModelBuildRequest());
    ASSERT_TRUE(changed.Inspector.HasEntity);
    EXPECT_EQ(changed.ModelBuildStats.SelectedAnalysisCacheMisses, 1u);
    EXPECT_EQ(changed.ModelBuildStats.SelectedAnalysisCacheHits, 0u);
    EXPECT_EQ(changed.ModelBuildStats.PropertyCatalogModelBuilds, 1u);
    EXPECT_NE(FindCatalogProperty(changed.Inspector.PropertyCatalog,
                                  Runtime::SandboxEditorPropertyCatalogDomain::
                                      MeshVertices,
                                  "v:temperature"),
              nullptr);

    const Runtime::SandboxEditorSelectedModelCacheStats cacheStats =
        cache.Stats();
    EXPECT_EQ(cacheStats.SelectedAnalysisCacheMisses, 2u);
    EXPECT_EQ(cacheStats.SelectedAnalysisCacheHits, 1u);
    EXPECT_EQ(cacheStats.Entries, 1u);
}
TEST(SandboxEditorUi, SelectedModelCacheInvalidatesOnRenderHintSignature)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;

    const ECS::EntityHandle mesh = MakeSelectable(registry, "Mesh");
    AddTriangleMeshSource(registry, mesh);
    auto& edges = registry.Raw().emplace<G::RenderEdges>(mesh);
    edges.Domain = G::RenderEdges::SourceDomain::Edge;
    edges.WidthSource = 1.0f;
    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));

    Runtime::SandboxEditorSelectedModelCache cache{};
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.SelectedModelCache = &cache;

    const Runtime::SandboxEditorPanelFrame first =
        Runtime::BuildSandboxEditorPanelFrame(
            context,
            MakeOnlyInspectorModelBuildRequest());
    ASSERT_TRUE(first.Inspector.HasEntity);
    EXPECT_EQ(first.ModelBuildStats.SelectedAnalysisCacheMisses, 1u);
    EXPECT_EQ(first.ModelBuildStats.BoundStateModelBuilds, 1u);

    const Runtime::SandboxEditorPanelFrame cached =
        Runtime::BuildSandboxEditorPanelFrame(
            context,
            MakeOnlyInspectorModelBuildRequest());
    ASSERT_TRUE(cached.Inspector.HasEntity);
    EXPECT_EQ(cached.ModelBuildStats.SelectedAnalysisCacheHits, 1u);
    EXPECT_EQ(cached.ModelBuildStats.BoundStateModelBuilds, 0u);

    registry.Raw().get<G::RenderEdges>(mesh).WidthSource = 3.5f;

    const Runtime::SandboxEditorPanelFrame changed =
        Runtime::BuildSandboxEditorPanelFrame(
            context,
            MakeOnlyInspectorModelBuildRequest());
    ASSERT_TRUE(changed.Inspector.HasEntity);
    EXPECT_EQ(changed.ModelBuildStats.SelectedAnalysisCacheMisses, 1u);
    EXPECT_EQ(changed.ModelBuildStats.SelectedAnalysisCacheHits, 0u);
    EXPECT_EQ(changed.ModelBuildStats.BoundStateModelBuilds, 1u);
    const Runtime::SandboxEditorBoundRenderStateRow* edgeHint =
        FindBoundRowLabel(
            changed.Inspector.BoundState,
            Runtime::SandboxEditorBoundRenderStateRowKind::RenderHint,
            "Edge render hint");
    ASSERT_NE(edgeHint, nullptr);
    EXPECT_TRUE(edgeHint->Enabled);
    EXPECT_EQ(edgeHint->SourceDescription, "uniform:3.500000");

    const Runtime::SandboxEditorSelectedModelCacheStats cacheStats =
        cache.Stats();
    EXPECT_EQ(cacheStats.SelectedAnalysisCacheMisses, 2u);
    EXPECT_EQ(cacheStats.SelectedAnalysisCacheHits, 1u);
    EXPECT_EQ(cacheStats.Entries, 1u);
}
TEST(SandboxEditorUi, SelectedModelCacheReusesVisualizationModel)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;

    const ECS::EntityHandle mesh = MakeSelectable(registry, "Mesh");
    AddTriangleMeshSource(registry, mesh);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));

    Runtime::SandboxEditorSelectedModelCache cache{};
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.SelectedModelCache = &cache;
    context.VisualizationCommandsAvailable = true;

    const Runtime::SandboxEditorPanelFrame first =
        Runtime::BuildSandboxEditorPanelFrame(
            context,
            MakeOnlyVisualizationModelBuildRequest());

    ASSERT_TRUE(first.Visualization.HasSelectedEntity);
    EXPECT_EQ(first.ModelBuildStats.VisualizationModelBuilds, 1u);
    EXPECT_EQ(first.ModelBuildStats.VisualizationModelCacheMisses, 1u);
    EXPECT_EQ(first.ModelBuildStats.VisualizationModelCacheHits, 0u);
    EXPECT_GT(first.ModelBuildStats.PanelFrameModelBuildTimeNs, 0u);
    EXPECT_GT(first.ModelBuildStats.VisualizationModelBuildTimeNs, 0u);

    const Runtime::SandboxEditorPanelFrame second =
        Runtime::BuildSandboxEditorPanelFrame(
            context,
            MakeOnlyVisualizationModelBuildRequest());

    ASSERT_TRUE(second.Visualization.HasSelectedEntity);
    EXPECT_EQ(second.ModelBuildStats.VisualizationModelBuilds, 0u);
    EXPECT_EQ(second.ModelBuildStats.VisualizationModelCacheMisses, 0u);
    EXPECT_EQ(second.ModelBuildStats.VisualizationModelCacheHits, 1u);
    EXPECT_GT(second.ModelBuildStats.PanelFrameModelBuildTimeNs, 0u);
    EXPECT_EQ(second.ModelBuildStats.VisualizationModelBuildTimeNs, 0u);
    EXPECT_EQ(second.Visualization.Properties.size(),
              first.Visualization.Properties.size());

    const Runtime::SandboxEditorSelectedModelCacheStats cacheStats =
        cache.Stats();
    EXPECT_EQ(cacheStats.VisualizationModelCacheMisses, 1u);
    EXPECT_EQ(cacheStats.VisualizationModelCacheHits, 1u);
    EXPECT_EQ(cacheStats.Entries, 1u);
}
TEST(SandboxEditorUi, VisualizationModelCacheInvalidatesOnGeometryMetadataSignature)
{
    using Domain = Runtime::SandboxEditorVisualizationPropertyDomain;

    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;

    const ECS::EntityHandle mesh = MakeSelectable(registry, "Mesh");
    AddTriangleMeshSource(registry, mesh);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));

    Runtime::SandboxEditorSelectedModelCache cache{};
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.SelectedModelCache = &cache;
    context.VisualizationCommandsAvailable = true;

    const Runtime::SandboxEditorPanelFrame first =
        Runtime::BuildSandboxEditorPanelFrame(
            context,
            MakeOnlyVisualizationModelBuildRequest());
    ASSERT_TRUE(first.Visualization.HasSelectedEntity);
    EXPECT_EQ(first.ModelBuildStats.VisualizationModelCacheMisses, 1u);
    EXPECT_EQ(first.ModelBuildStats.VisualizationModelBuilds, 1u);
    EXPECT_EQ(FindVisualizationProperty(first.Visualization.Properties,
                                        Domain::MeshVertices,
                                        "v:temperature"),
              nullptr);

    const Runtime::SandboxEditorPanelFrame cached =
        Runtime::BuildSandboxEditorPanelFrame(
            context,
            MakeOnlyVisualizationModelBuildRequest());
    ASSERT_TRUE(cached.Visualization.HasSelectedEntity);
    EXPECT_EQ(cached.ModelBuildStats.VisualizationModelCacheHits, 1u);
    EXPECT_EQ(cached.ModelBuildStats.VisualizationModelBuilds, 0u);

    auto& vertices = registry.Raw().get<GS::Vertices>(mesh);
    auto temperature =
        vertices.Properties.GetOrAdd<float>("v:temperature", 0.0f);
    ASSERT_EQ(temperature.Vector().size(), 3u);
    temperature.Vector()[2] = 7.0f;

    const Runtime::SandboxEditorPanelFrame changed =
        Runtime::BuildSandboxEditorPanelFrame(
            context,
            MakeOnlyVisualizationModelBuildRequest());
    ASSERT_TRUE(changed.Visualization.HasSelectedEntity);
    EXPECT_EQ(changed.ModelBuildStats.VisualizationModelCacheMisses, 1u);
    EXPECT_EQ(changed.ModelBuildStats.VisualizationModelCacheHits, 0u);
    EXPECT_EQ(changed.ModelBuildStats.VisualizationModelBuilds, 1u);
    EXPECT_NE(FindVisualizationProperty(changed.Visualization.Properties,
                                        Domain::MeshVertices,
                                        "v:temperature"),
              nullptr);

    const Runtime::SandboxEditorSelectedModelCacheStats cacheStats =
        cache.Stats();
    EXPECT_EQ(cacheStats.VisualizationModelCacheMisses, 2u);
    EXPECT_EQ(cacheStats.VisualizationModelCacheHits, 1u);
    EXPECT_EQ(cacheStats.Entries, 1u);
}
TEST(SandboxEditorUi, VisualizationModelCacheInvalidatesOnConfigSignature)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;

    const ECS::EntityHandle mesh = MakeSelectable(registry, "Mesh");
    AddTriangleMeshSource(registry, mesh);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));

    Runtime::SandboxEditorSelectedModelCache cache{};
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.SelectedModelCache = &cache;
    context.VisualizationCommandsAvailable = true;

    const Runtime::SandboxEditorPanelFrame first =
        Runtime::BuildSandboxEditorPanelFrame(
            context,
            MakeOnlyVisualizationModelBuildRequest());
    ASSERT_TRUE(first.Visualization.HasSelectedEntity);
    EXPECT_FALSE(first.Visualization.Visualization.HasConfig);
    EXPECT_EQ(first.ModelBuildStats.VisualizationModelCacheMisses, 1u);
    EXPECT_EQ(first.ModelBuildStats.VisualizationModelBuilds, 1u);

    const Runtime::SandboxEditorPanelFrame cached =
        Runtime::BuildSandboxEditorPanelFrame(
            context,
            MakeOnlyVisualizationModelBuildRequest());
    ASSERT_TRUE(cached.Visualization.HasSelectedEntity);
    EXPECT_EQ(cached.ModelBuildStats.VisualizationModelCacheHits, 1u);
    EXPECT_EQ(cached.ModelBuildStats.VisualizationModelBuilds, 0u);

    G::VisualizationConfig config{};
    config.Source = G::VisualizationConfig::ColorSource::UniformColor;
    config.Color = glm::vec4{0.125f, 0.5f, 0.875f, 1.0f};
    config.ScalarFieldName = "curvature";
    config.ScalarDomain = G::VisualizationConfig::Domain::Face;
    config.Scalar.AutoRange = false;
    config.Scalar.RangeMin = -1.0f;
    config.Scalar.RangeMax = 2.0f;
    config.Scalar.BinCount = 7u;
    config.Scalar.Isolines.Num = 5u;
    config.Scalar.Isolines.Width = 2.25f;
    registry.Raw().emplace_or_replace<G::VisualizationConfig>(mesh, config);

    const Runtime::SandboxEditorPanelFrame changed =
        Runtime::BuildSandboxEditorPanelFrame(
            context,
            MakeOnlyVisualizationModelBuildRequest());
    ASSERT_TRUE(changed.Visualization.HasSelectedEntity);
    EXPECT_EQ(changed.ModelBuildStats.VisualizationModelCacheMisses, 1u);
    EXPECT_EQ(changed.ModelBuildStats.VisualizationModelCacheHits, 0u);
    EXPECT_EQ(changed.ModelBuildStats.VisualizationModelBuilds, 1u);
    ASSERT_TRUE(changed.Visualization.Visualization.HasConfig);
    EXPECT_EQ(changed.Visualization.Visualization.Source,
              G::VisualizationConfig::ColorSource::UniformColor);
    EXPECT_FLOAT_EQ(changed.Visualization.Visualization.Color.x, 0.125f);
    EXPECT_FLOAT_EQ(changed.Visualization.Visualization.Color.y, 0.5f);
    EXPECT_FLOAT_EQ(changed.Visualization.Visualization.Color.z, 0.875f);
    EXPECT_EQ(changed.Visualization.Visualization.ScalarFieldName,
              "curvature");
    EXPECT_EQ(changed.Visualization.Visualization.ScalarDomain,
              G::VisualizationConfig::Domain::Face);
    EXPECT_FALSE(changed.Visualization.Visualization.ScalarAutoRange);
    EXPECT_FLOAT_EQ(changed.Visualization.Visualization.ScalarRangeMin,
                    -1.0f);
    EXPECT_FLOAT_EQ(changed.Visualization.Visualization.ScalarRangeMax,
                    2.0f);
    EXPECT_EQ(changed.Visualization.Visualization.ScalarBinCount, 7u);
    EXPECT_EQ(changed.Visualization.Visualization.IsolineCount, 5u);

    const Runtime::SandboxEditorSelectedModelCacheStats cacheStats =
        cache.Stats();
    EXPECT_EQ(cacheStats.VisualizationModelCacheMisses, 2u);
    EXPECT_EQ(cacheStats.VisualizationModelCacheHits, 1u);
    EXPECT_EQ(cacheStats.Entries, 1u);
}
TEST(SandboxEditorUi, VisualizationModelCacheInvalidatesOnLaneOverrideSignature)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;

    const ECS::EntityHandle mesh = MakeSelectable(registry, "Mesh");
    AddTriangleMeshSource(registry, mesh);
    registry.Raw().emplace<G::RenderSurface>(mesh);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));

    Runtime::SandboxEditorSelectedModelCache cache{};
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.SelectedModelCache = &cache;
    context.VisualizationCommandsAvailable = true;

    const Runtime::SandboxEditorDomainWindowModel first =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::Mesh);
    ASSERT_TRUE(first.HasSelectedEntity);
    EXPECT_EQ(first.Visualization.Target,
              Runtime::SandboxEditorVisualizationTarget::Surface);
    EXPECT_FALSE(first.Visualization.Visualization.HasConfig);

    Runtime::SandboxEditorSelectedModelCacheStats cacheStats = cache.Stats();
    EXPECT_EQ(cacheStats.VisualizationModelCacheMisses, 1u);
    EXPECT_EQ(cacheStats.VisualizationModelCacheHits, 0u);

    const Runtime::SandboxEditorDomainWindowModel cached =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::Mesh);
    ASSERT_TRUE(cached.HasSelectedEntity);
    EXPECT_FALSE(cached.Visualization.Visualization.HasConfig);
    cacheStats = cache.Stats();
    EXPECT_EQ(cacheStats.VisualizationModelCacheMisses, 1u);
    EXPECT_EQ(cacheStats.VisualizationModelCacheHits, 1u);

    G::VisualizationConfig surfaceConfig{};
    surfaceConfig.Source = G::VisualizationConfig::ColorSource::UniformColor;
    surfaceConfig.Color = glm::vec4{0.25f, 0.5f, 0.75f, 1.0f};
    G::VisualizationLaneOverrides overrides{};
    overrides.Surface = surfaceConfig;
    registry.Raw().emplace_or_replace<G::VisualizationLaneOverrides>(
        mesh,
        overrides);

    const Runtime::SandboxEditorDomainWindowModel changed =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::Mesh);
    ASSERT_TRUE(changed.HasSelectedEntity);
    ASSERT_TRUE(changed.Visualization.Visualization.HasConfig);
    EXPECT_EQ(changed.Visualization.Visualization.Source,
              G::VisualizationConfig::ColorSource::UniformColor);
    EXPECT_FLOAT_EQ(changed.Visualization.Visualization.Color.x, 0.25f);
    EXPECT_FLOAT_EQ(changed.Visualization.Visualization.Color.y, 0.5f);
    EXPECT_FLOAT_EQ(changed.Visualization.Visualization.Color.z, 0.75f);

    cacheStats = cache.Stats();
    EXPECT_EQ(cacheStats.VisualizationModelCacheMisses, 2u);
    EXPECT_EQ(cacheStats.VisualizationModelCacheHits, 1u);
}
TEST(SandboxEditorUi, VisualizationModelCacheInvalidatesOnSpatialDebugSignature)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;

    const ECS::EntityHandle mesh = MakeSelectable(registry, "Mesh");
    AddTriangleMeshSource(registry, mesh);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));

    Runtime::SandboxEditorSelectedModelCache cache{};
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.SelectedModelCache = &cache;
    context.VisualizationCommandsAvailable = true;

    const Runtime::SandboxEditorPanelFrame first =
        Runtime::BuildSandboxEditorPanelFrame(
            context,
            MakeOnlyVisualizationModelBuildRequest());
    ASSERT_TRUE(first.Visualization.HasSelectedEntity);
    EXPECT_FALSE(first.Visualization.SpatialDebug.HasBinding);
    EXPECT_EQ(first.ModelBuildStats.VisualizationModelCacheMisses, 1u);
    EXPECT_EQ(first.ModelBuildStats.VisualizationModelBuilds, 1u);

    const Runtime::SandboxEditorPanelFrame cached =
        Runtime::BuildSandboxEditorPanelFrame(
            context,
            MakeOnlyVisualizationModelBuildRequest());
    ASSERT_TRUE(cached.Visualization.HasSelectedEntity);
    EXPECT_EQ(cached.ModelBuildStats.VisualizationModelCacheHits, 1u);
    EXPECT_EQ(cached.ModelBuildStats.VisualizationModelBuilds, 0u);

    registry.Raw().emplace_or_replace<ECSC::SpatialDebugBinding>(
        mesh,
        ECSC::SpatialDebugBinding{
            .Kind = ECSC::SpatialDebugGeometryKind::Octree,
            .RegistryKey = 0xBEEFu,
            .LeafOnly = true,
            .OccupancyOnly = true,
            .MaxDepth = 12u,
        });

    const Runtime::SandboxEditorPanelFrame changed =
        Runtime::BuildSandboxEditorPanelFrame(
            context,
            MakeOnlyVisualizationModelBuildRequest());
    ASSERT_TRUE(changed.Visualization.HasSelectedEntity);
    EXPECT_EQ(changed.ModelBuildStats.VisualizationModelCacheMisses, 1u);
    EXPECT_EQ(changed.ModelBuildStats.VisualizationModelCacheHits, 0u);
    EXPECT_EQ(changed.ModelBuildStats.VisualizationModelBuilds, 1u);
    ASSERT_TRUE(changed.Visualization.SpatialDebug.HasBinding);
    EXPECT_EQ(changed.Visualization.SpatialDebug.Kind,
              ECSC::SpatialDebugGeometryKind::Octree);
    EXPECT_EQ(changed.Visualization.SpatialDebug.RegistryKey, 0xBEEFu);
    EXPECT_TRUE(changed.Visualization.SpatialDebug.LeafOnly);
    EXPECT_TRUE(changed.Visualization.SpatialDebug.OccupancyOnly);
    EXPECT_EQ(changed.Visualization.SpatialDebug.MaxDepth, 12u);

    const Runtime::SandboxEditorSelectedModelCacheStats cacheStats =
        cache.Stats();
    EXPECT_EQ(cacheStats.VisualizationModelCacheMisses, 2u);
    EXPECT_EQ(cacheStats.VisualizationModelCacheHits, 1u);
    EXPECT_EQ(cacheStats.Entries, 1u);
}
TEST(SandboxEditorUi, VisualizationModelCacheInvalidatesOnAdapterBindingRevision)
{
    using Binding = Runtime::RenderExtractionCache::VisualizationAdapterBinding;
    using Kind = Runtime::RenderExtractionCache::VisualizationAdapterBindingKind;

    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;

    const ECS::EntityHandle mesh = MakeSelectable(registry, "Mesh");
    AddTriangleMeshSource(registry, mesh);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);

    std::optional<Binding> storedBinding{};
    Runtime::SandboxEditorSelectedModelCache cache{};
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.SelectedModelCache = &cache;
    context.VisualizationCommandsAvailable = true;
    context.VisualizationAdapterBindings =
        Runtime::SandboxEditorVisualizationAdapterBindingCommandSurface{
            .GetBinding =
                [&](const std::uint32_t queriedStableId) -> std::optional<Binding>
                {
                    if (queriedStableId != stableId)
                        return std::nullopt;
                    return storedBinding;
                },
            .SetBinding =
                [&](std::uint32_t, Binding binding)
                {
                    storedBinding = std::move(binding);
                },
            .ClearBinding =
                [&](std::uint32_t)
                {
                    storedBinding.reset();
                },
        };
    context.VisualizationAdapterBindingRevision = 1u;

    const Runtime::SandboxEditorPanelFrame first =
        Runtime::BuildSandboxEditorPanelFrame(
            context,
            MakeOnlyVisualizationModelBuildRequest());
    ASSERT_TRUE(first.Visualization.HasSelectedEntity);
    EXPECT_EQ(first.ModelBuildStats.VisualizationModelCacheMisses, 1u);
    EXPECT_EQ(first.ModelBuildStats.VisualizationModelBuilds, 1u);

    const Runtime::SandboxEditorPanelFrame cached =
        Runtime::BuildSandboxEditorPanelFrame(
            context,
            MakeOnlyVisualizationModelBuildRequest());
    ASSERT_TRUE(cached.Visualization.HasSelectedEntity);
    EXPECT_EQ(cached.ModelBuildStats.VisualizationModelCacheHits, 1u);
    EXPECT_EQ(cached.ModelBuildStats.VisualizationModelBuilds, 0u);

    Runtime::VisualizationAdapterOptions options{};
    options.OutputName = "velocity_glyphs";
    storedBinding = Binding{
        .AdapterKey = 0xF00Du,
        .BufferBDA = 0xAABB'2000u,
        .Kind = Kind::VectorField,
        .Options = options,
    };
    context.VisualizationAdapterBindingRevision = 2u;

    const Runtime::SandboxEditorPanelFrame changed =
        Runtime::BuildSandboxEditorPanelFrame(
            context,
            MakeOnlyVisualizationModelBuildRequest());
    ASSERT_TRUE(changed.Visualization.HasSelectedEntity);
    EXPECT_EQ(changed.ModelBuildStats.VisualizationModelCacheMisses, 1u);
    EXPECT_EQ(changed.ModelBuildStats.VisualizationModelCacheHits, 0u);
    EXPECT_EQ(changed.ModelBuildStats.VisualizationModelBuilds, 1u);
    ASSERT_TRUE(changed.Visualization.AdapterBinding.HasBinding);
    EXPECT_EQ(changed.Visualization.AdapterBinding.AdapterKey, 0xF00Du);
    EXPECT_EQ(changed.Visualization.AdapterBinding.Options.OutputName,
              "velocity_glyphs");

    const Runtime::SandboxEditorSelectedModelCacheStats cacheStats =
        cache.Stats();
    EXPECT_EQ(cacheStats.VisualizationModelCacheMisses, 2u);
    EXPECT_EQ(cacheStats.VisualizationModelCacheHits, 1u);
}
TEST(SandboxEditorUi, GeometryProcessingSupportedDomainsMatchPromotedEditorContract)
{
    using Algorithm = Runtime::SandboxEditorGeometryProcessingAlgorithm;
    using Domain = Runtime::SandboxEditorGeometryProcessingDomain;

    const Domain kmeans =
        Runtime::GetSandboxEditorSupportedGeometryProcessingDomains(
            Algorithm::KMeans);
    EXPECT_TRUE(Runtime::HasAnySandboxEditorGeometryProcessingDomain(
        kmeans,
        Domain::MeshVertices));
    EXPECT_TRUE(Runtime::HasAnySandboxEditorGeometryProcessingDomain(
        kmeans,
        Domain::GraphVertices));
    EXPECT_TRUE(Runtime::HasAnySandboxEditorGeometryProcessingDomain(
        kmeans,
        Domain::PointCloudPoints));
    EXPECT_FALSE(Runtime::HasAnySandboxEditorGeometryProcessingDomain(
        kmeans,
        Domain::MeshEdges));

    const Domain normals =
        Runtime::GetSandboxEditorSupportedGeometryProcessingDomains(
            Algorithm::NormalEstimation);
    EXPECT_TRUE(Runtime::HasAnySandboxEditorGeometryProcessingDomain(
        normals,
        Domain::MeshVertices));
    EXPECT_TRUE(Runtime::HasAnySandboxEditorGeometryProcessingDomain(
        normals,
        Domain::GraphVertices));
    EXPECT_TRUE(Runtime::HasAnySandboxEditorGeometryProcessingDomain(
        normals,
        Domain::PointCloudPoints));
    EXPECT_FALSE(Runtime::HasAnySandboxEditorGeometryProcessingDomain(
        normals,
        Domain::MeshEdges));

    const Domain denoise =
        Runtime::GetSandboxEditorSupportedGeometryProcessingDomains(
            Algorithm::MeshDenoise);
    EXPECT_TRUE(Runtime::HasAnySandboxEditorGeometryProcessingDomain(
        denoise,
        Domain::MeshVertices));
    EXPECT_FALSE(Runtime::HasAnySandboxEditorGeometryProcessingDomain(
        denoise,
        Domain::MeshEdges));
    EXPECT_FALSE(Runtime::HasAnySandboxEditorGeometryProcessingDomain(
        denoise,
        Domain::GraphVertices));
    EXPECT_FALSE(Runtime::HasAnySandboxEditorGeometryProcessingDomain(
        denoise,
        Domain::PointCloudPoints));

    const Domain curvature =
        Runtime::GetSandboxEditorSupportedGeometryProcessingDomains(
            Algorithm::Curvature);
    EXPECT_TRUE(Runtime::HasAnySandboxEditorGeometryProcessingDomain(
        curvature,
        Domain::MeshVertices));
    EXPECT_FALSE(Runtime::HasAnySandboxEditorGeometryProcessingDomain(
        curvature,
        Domain::MeshEdges));
    EXPECT_FALSE(Runtime::HasAnySandboxEditorGeometryProcessingDomain(
        curvature,
        Domain::GraphVertices));
    EXPECT_FALSE(Runtime::HasAnySandboxEditorGeometryProcessingDomain(
        curvature,
        Domain::PointCloudPoints));

    const Domain smoothing =
        Runtime::GetSandboxEditorSupportedGeometryProcessingDomains(
            Algorithm::Smoothing);
    EXPECT_TRUE(Runtime::HasAnySandboxEditorGeometryProcessingDomain(
        smoothing,
        Domain::MeshVertices));
    EXPECT_TRUE(Runtime::HasAnySandboxEditorGeometryProcessingDomain(
        smoothing,
        Domain::MeshEdges));
    EXPECT_TRUE(Runtime::HasAnySandboxEditorGeometryProcessingDomain(
        smoothing,
        Domain::MeshHalfedges));
    EXPECT_TRUE(Runtime::HasAnySandboxEditorGeometryProcessingDomain(
        smoothing,
        Domain::MeshFaces));
    EXPECT_FALSE(Runtime::HasAnySandboxEditorGeometryProcessingDomain(
        smoothing,
        Domain::GraphVertices));

    EXPECT_TRUE(Runtime::SupportsSandboxEditorGeometryProcessingDomain(
        Algorithm::ShortestPath,
        Domain::GraphVertices));
    EXPECT_FALSE(Runtime::SupportsSandboxEditorGeometryProcessingDomain(
        Algorithm::ShortestPath,
        Domain::PointCloudPoints));
    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorGeometryProcessingDomain(
                     Domain::GraphVertices),
                 "Graph Nodes");
    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorGeometryProcessingAlgorithm(
                     Algorithm::VectorHeat),
                 "Vector Heat Method");
    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorGeometryProcessingAlgorithm(
                     Algorithm::MeshDenoise),
                 "Mesh Denoise");
    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorGeometryProcessingAlgorithm(
                     Algorithm::Curvature),
                 "Curvature");
}
TEST(SandboxEditorUi, GeometrySourcesReportProcessingCapabilitiesAndStableEntries)
{
    using Algorithm = Runtime::SandboxEditorGeometryProcessingAlgorithm;
    using Domain = Runtime::SandboxEditorGeometryProcessingDomain;

    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;

    const ECS::EntityHandle mesh = MakeSelectable(registry, "Mesh");
    AddTriangleMeshSource(registry, mesh);

    const Runtime::SandboxEditorGeometryProcessingCapabilities meshCaps =
        Runtime::GetSandboxEditorGeometryProcessingCapabilities(registry, mesh);
    EXPECT_TRUE(meshCaps.HasEditableSurfaceMesh);
    EXPECT_TRUE(Runtime::HasAnySandboxEditorGeometryProcessingDomain(
        meshCaps.Domains,
        Domain::MeshVertices));
    EXPECT_TRUE(Runtime::HasAnySandboxEditorGeometryProcessingDomain(
        meshCaps.Domains,
        Domain::MeshEdges));
    EXPECT_TRUE(Runtime::HasAnySandboxEditorGeometryProcessingDomain(
        meshCaps.Domains,
        Domain::MeshHalfedges));
    EXPECT_TRUE(Runtime::HasAnySandboxEditorGeometryProcessingDomain(
        meshCaps.Domains,
        Domain::MeshFaces));
    EXPECT_FALSE(Runtime::HasAnySandboxEditorGeometryProcessingDomain(
        meshCaps.Domains,
        Domain::GraphVertices));

    const std::vector<Runtime::SandboxEditorGeometryProcessingEntry> meshEntries =
        Runtime::ResolveSandboxEditorGeometryProcessingEntries(meshCaps);
    ASSERT_EQ(meshEntries.size(), 15u);
    EXPECT_EQ(meshEntries[0].Algorithm, Algorithm::KMeans);
    EXPECT_EQ(meshEntries[1].Algorithm, Algorithm::NormalEstimation);
    EXPECT_EQ(meshEntries[2].Algorithm, Algorithm::MeshDenoise);
    EXPECT_EQ(meshEntries[3].Algorithm, Algorithm::Curvature);
    EXPECT_EQ(meshEntries[4].Algorithm, Algorithm::ProgressivePoissonSampling);
    EXPECT_EQ(meshEntries[5].Algorithm, Algorithm::ShortestPath);
    EXPECT_EQ(meshEntries[6].Algorithm, Algorithm::VectorHeat);
    EXPECT_EQ(meshEntries[7].Algorithm, Algorithm::Parameterization);
    EXPECT_EQ(meshEntries[8].Algorithm, Algorithm::ConvexHull);
    EXPECT_EQ(meshEntries[9].Algorithm, Algorithm::BooleanCSG);
    EXPECT_EQ(meshEntries[10].Algorithm, Algorithm::Remeshing);
    EXPECT_EQ(meshEntries[14].Algorithm, Algorithm::Repair);

    const std::vector<Domain> meshKMeans =
        Runtime::GetAvailableSandboxEditorKMeansDomains(registry, mesh);
    ASSERT_EQ(meshKMeans.size(), 1u);
    EXPECT_EQ(meshKMeans[0], Domain::MeshVertices);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    const Runtime::SandboxEditorDomainWindowModel meshModel =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::Mesh);
    EXPECT_TRUE(meshModel.Processing.MeshDenoiseAvailable);
    EXPECT_TRUE(meshModel.Processing.MeshCurvatureAvailable);
    EXPECT_TRUE(meshModel.Processing.MeshCurvatureDirectionsAvailable);
    EXPECT_TRUE(meshModel.Processing.MeshRemeshAvailable);
    EXPECT_TRUE(meshModel.Processing.MeshRemeshUniformAvailable);
    EXPECT_TRUE(meshModel.Processing.MeshRemeshAdaptiveAvailable);
    EXPECT_TRUE(meshModel.Processing.MeshRemeshProjectToSurfaceAvailable);
    EXPECT_TRUE(meshModel.Processing.MeshRemeshErrorBoundedSizingAvailable);
    EXPECT_TRUE(meshModel.Processing.MeshSubdivideAvailable);
    EXPECT_TRUE(meshModel.Processing.MeshSubdivideLoopAvailable);
    EXPECT_TRUE(meshModel.Processing.MeshSubdivideCatmullClarkAvailable);
    EXPECT_TRUE(meshModel.Processing.MeshSubdivideSqrt3Available);
    EXPECT_TRUE(meshModel.Processing.MeshSubdivideLoopFeatureEdgesAvailable);
    EXPECT_TRUE(meshModel.Processing.MeshSimplifyAvailable);
    EXPECT_TRUE(meshModel.Processing.MeshVertexNormalsAvailable);
    EXPECT_TRUE(meshModel.Processing.MeshProgressivePoissonAvailable);
    EXPECT_FALSE(meshModel.Processing.GraphVertexNormalsAvailable);
    EXPECT_FALSE(meshModel.Processing.PointCloudVertexNormalsAvailable);

    const ECS::EntityHandle graph = MakeSelectable(registry, "Graph");
    AddGraphSource(registry, graph);
    const Runtime::SandboxEditorGeometryProcessingCapabilities graphCaps =
        Runtime::GetSandboxEditorGeometryProcessingCapabilities(registry, graph);
    EXPECT_FALSE(graphCaps.HasEditableSurfaceMesh);
    EXPECT_TRUE(Runtime::HasAnySandboxEditorGeometryProcessingDomain(
        graphCaps.Domains,
        Domain::GraphVertices));
    EXPECT_TRUE(Runtime::HasAnySandboxEditorGeometryProcessingDomain(
        graphCaps.Domains,
        Domain::GraphEdges));
    EXPECT_FALSE(Runtime::HasAnySandboxEditorGeometryProcessingDomain(
        graphCaps.Domains,
        Domain::GraphHalfedges));
    EXPECT_FALSE(Runtime::HasAnySandboxEditorGeometryProcessingDomain(
        graphCaps.Domains,
        Domain::MeshVertices));
    const std::vector<Runtime::SandboxEditorGeometryProcessingEntry> graphEntries =
        Runtime::ResolveSandboxEditorGeometryProcessingEntries(registry, graph);
    ASSERT_EQ(graphEntries.size(), 3u);
    EXPECT_EQ(graphEntries[0].Algorithm, Algorithm::KMeans);
    EXPECT_EQ(graphEntries[1].Algorithm, Algorithm::NormalEstimation);
    EXPECT_EQ(graphEntries[2].Algorithm, Algorithm::ShortestPath);
    const std::vector<Domain> graphKMeans =
        Runtime::GetAvailableSandboxEditorKMeansDomains(registry, graph);
    ASSERT_EQ(graphKMeans.size(), 1u);
    EXPECT_EQ(graphKMeans[0], Domain::GraphVertices);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, graph));
    const Runtime::SandboxEditorDomainWindowModel graphModel =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::Graph);
    EXPECT_FALSE(graphModel.Processing.MeshDenoiseAvailable);
    EXPECT_FALSE(graphModel.Processing.MeshCurvatureAvailable);
    EXPECT_FALSE(graphModel.Processing.MeshCurvatureDirectionsAvailable);
    EXPECT_FALSE(graphModel.Processing.MeshVertexNormalsAvailable);
    EXPECT_FALSE(graphModel.Processing.MeshProgressivePoissonAvailable);
    EXPECT_TRUE(graphModel.Processing.GraphVertexNormalsAvailable);
    EXPECT_FALSE(graphModel.Processing.PointCloudVertexNormalsAvailable);

    const ECS::EntityHandle cloud = MakeSelectable(registry, "Cloud");
    AddPointCloudSource(registry, cloud, 5u);
    const Runtime::SandboxEditorGeometryProcessingCapabilities cloudCaps =
        Runtime::GetSandboxEditorGeometryProcessingCapabilities(registry, cloud);
    EXPECT_FALSE(cloudCaps.HasEditableSurfaceMesh);
    EXPECT_TRUE(Runtime::HasAnySandboxEditorGeometryProcessingDomain(
        cloudCaps.Domains,
        Domain::PointCloudPoints));
    EXPECT_FALSE(Runtime::HasAnySandboxEditorGeometryProcessingDomain(
        cloudCaps.Domains,
        Domain::MeshVertices));
    const std::vector<Runtime::SandboxEditorGeometryProcessingEntry> cloudEntries =
        Runtime::ResolveSandboxEditorGeometryProcessingEntries(registry, cloud);
    ASSERT_EQ(cloudEntries.size(), 11u);
    EXPECT_EQ(cloudEntries[0].Algorithm, Algorithm::KMeans);
    EXPECT_EQ(cloudEntries[1].Algorithm, Algorithm::NormalEstimation);
    EXPECT_EQ(cloudEntries[2].Algorithm, Algorithm::Registration);
    EXPECT_EQ(cloudEntries[6].Algorithm, Algorithm::ProgressivePoissonSampling);
    EXPECT_EQ(cloudEntries[10].Algorithm, Algorithm::SurfaceReconstruction);
    const std::vector<Domain> cloudKMeans =
        Runtime::GetAvailableSandboxEditorKMeansDomains(registry, cloud);
    ASSERT_EQ(cloudKMeans.size(), 1u);
    EXPECT_EQ(cloudKMeans[0], Domain::PointCloudPoints);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, cloud));
    const Runtime::SandboxEditorDomainWindowModel cloudModel =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::PointCloud);
    EXPECT_FALSE(cloudModel.Processing.MeshDenoiseAvailable);
    EXPECT_FALSE(cloudModel.Processing.MeshCurvatureAvailable);
    EXPECT_FALSE(cloudModel.Processing.MeshCurvatureDirectionsAvailable);
    EXPECT_FALSE(cloudModel.Processing.MeshVertexNormalsAvailable);
    EXPECT_FALSE(cloudModel.Processing.MeshProgressivePoissonAvailable);
    EXPECT_FALSE(cloudModel.Processing.GraphVertexNormalsAvailable);
    EXPECT_TRUE(cloudModel.Processing.PointCloudVertexNormalsAvailable);
    EXPECT_TRUE(cloudModel.Processing.PointCloudProgressivePoissonAvailable);

    const ECS::EntityHandle empty = MakeSelectable(registry, "Empty");
    const Runtime::SandboxEditorGeometryProcessingCapabilities emptyCaps =
        Runtime::GetSandboxEditorGeometryProcessingCapabilities(registry, empty);
    EXPECT_FALSE(emptyCaps.HasAny());
    EXPECT_TRUE(Runtime::ResolveSandboxEditorGeometryProcessingEntries(
                    registry,
                    empty)
                    .empty());
}
TEST(SandboxEditorUi, VisualizationModelEnumeratesPromotedGeometryProperties)
{
    using Domain = Runtime::SandboxEditorVisualizationPropertyDomain;
    using Kind = Runtime::SandboxEditorVisualizationPropertyValueKind;

    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    const ECS::EntityHandle mesh = MakeSelectable(registry, "PropertyMesh");
    AddTriangleMeshSource(registry, mesh);
    auto& meshVertices = registry.Raw().get<GS::Vertices>(mesh);
    meshVertices.Properties.GetOrAdd<float>("v:temperature", 0.0f)
        .Vector() = {0.0f, 0.5f, 1.0f};
    meshVertices.Properties.GetOrAdd<glm::vec4>("v:kmeans_color", glm::vec4{1.0f})
        .Vector() = {glm::vec4{1.0f}, glm::vec4{0.0f, 1.0f, 0.0f, 1.0f}, glm::vec4{0.0f, 0.0f, 1.0f, 1.0f}};
    meshVertices.Properties.GetOrAdd<std::uint32_t>("v:kmeans_label", 0u)
        .Vector() = {0u, 1u, 1u};
    meshVertices.Properties.GetOrAdd<float>("v:kmeans_label_f", 0.0f)
        .Vector() = {0.0f, 1.0f, 1.0f};
    meshVertices.Properties.GetOrAdd<glm::vec3>("v:normal", glm::vec3{0.0f, 0.0f, 1.0f})
        .Vector() = {glm::vec3{0.0f, 0.0f, 1.0f}, glm::vec3{0.0f, 0.0f, 1.0f}, glm::vec3{0.0f, 0.0f, 1.0f}};

    auto& meshEdges = registry.Raw().get<GS::Edges>(mesh);
    meshEdges.Properties.GetOrAdd<double>("e:weight", 0.0)
        .Vector() = {1.0, 2.0, 3.0};
    meshEdges.Properties.GetOrAdd<glm::vec4>("e:debug_color", glm::vec4{1.0f})
        .Vector() = {glm::vec4{1.0f}, glm::vec4{1.0f}, glm::vec4{1.0f}};

    auto& meshFaces = registry.Raw().get<GS::Faces>(mesh);
    meshFaces.Properties.GetOrAdd<float>("f:curvature", 0.0f)
        .Vector() = {0.25f};

    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.VisualizationCommandsAvailable = true;

    const Runtime::SandboxEditorPanelFrame meshFrame =
        Runtime::BuildSandboxEditorPanelFrame(context);
    const auto& properties = meshFrame.Visualization.Properties;

    EXPECT_EQ(FindVisualizationProperty(properties, Domain::MeshVertices, "v:position"),
              nullptr);
    ASSERT_NE(FindVisualizationProperty(properties, Domain::MeshVertices, "v:temperature"),
              nullptr);
    const auto* scalar =
        FindVisualizationProperty(properties, Domain::MeshVertices, "v:temperature");
    EXPECT_EQ(scalar->ValueKind, Kind::ScalarFloat);
    EXPECT_TRUE(scalar->ScalarPresetAvailable);
    EXPECT_TRUE(scalar->IsolinePresetAvailable);
    EXPECT_FALSE(scalar->ColorBufferPresetAvailable);

    const auto* color =
        FindVisualizationProperty(properties, Domain::MeshVertices, "v:kmeans_color");
    ASSERT_NE(color, nullptr);
    EXPECT_EQ(color->ValueKind, Kind::Vec4);
    EXPECT_TRUE(color->ColorBufferPresetAvailable);
    EXPECT_FALSE(color->ScalarPresetAvailable);

    const auto* label =
        FindVisualizationProperty(properties, Domain::MeshVertices, "v:kmeans_label");
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->ValueKind, Kind::UInt32);
    EXPECT_FALSE(label->ScalarPresetAvailable);
    EXPECT_FALSE(label->ColorBufferPresetAvailable);

    const auto* normal =
        FindVisualizationProperty(properties, Domain::MeshVertices, "v:normal");
    ASSERT_NE(normal, nullptr);
    EXPECT_TRUE(normal->VectorFieldCandidate);
    EXPECT_FALSE(normal->ColorBufferPresetAvailable);

    const auto* edgeWeight =
        FindVisualizationProperty(properties, Domain::MeshEdges, "e:weight");
    ASSERT_NE(edgeWeight, nullptr);
    EXPECT_EQ(edgeWeight->ValueKind, Kind::ScalarDouble);
    EXPECT_TRUE(edgeWeight->ScalarPresetAvailable);
    EXPECT_EQ(FindVisualizationProperty(properties,
                                        Domain::MeshEdges,
                                        std::string{GS::PropertyNames::kEdgeV0}),
              nullptr);
    EXPECT_EQ(FindVisualizationProperty(properties,
                                        Domain::MeshEdges,
                                        std::string{GS::PropertyNames::kEdgeV1}),
              nullptr);

    const auto* faceScalar =
        FindVisualizationProperty(properties, Domain::MeshFaces, "f:curvature");
    ASSERT_NE(faceScalar, nullptr);
    EXPECT_EQ(faceScalar->ElementCount, 1u);

    const ECS::EntityHandle graph = MakeSelectable(registry, "PropertyGraph");
    AddGraphSource(registry, graph);
    auto& graphNodes = registry.Raw().get<GS::Nodes>(graph);
    graphNodes.Properties.GetOrAdd<float>("v:centrality", 0.0f)
        .Vector() = {0.0f, 1.0f, 2.0f};
    ASSERT_TRUE(selection.SetSelectedEntity(registry, graph));
    const Runtime::SandboxEditorPanelFrame graphFrame =
        Runtime::BuildSandboxEditorPanelFrame(context);
    EXPECT_NE(FindVisualizationProperty(graphFrame.Visualization.Properties,
                                        Domain::GraphVertices,
                                        "v:centrality"),
              nullptr);
    EXPECT_EQ(FindVisualizationProperty(graphFrame.Visualization.Properties,
                                        Domain::GraphEdges,
                                        std::string{GS::PropertyNames::kEdgeV0}),
              nullptr);

    const ECS::EntityHandle cloud = MakeSelectable(registry, "PropertyCloud");
    AddPointCloudSource(registry, cloud, 2u);
    auto& cloudVertices = registry.Raw().get<GS::Vertices>(cloud);
    cloudVertices.Properties.GetOrAdd<glm::vec4>("p:kmeans_color", glm::vec4{1.0f})
        .Vector() = {glm::vec4{1.0f}, glm::vec4{0.0f, 1.0f, 0.0f, 1.0f}};
    ASSERT_TRUE(selection.SetSelectedEntity(registry, cloud));
    const Runtime::SandboxEditorDomainWindowModel cloudModel =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::PointCloud);
    EXPECT_NE(FindVisualizationProperty(cloudModel.Visualization.Properties,
                                        Domain::PointCloudPoints,
                                        "p:kmeans_color"),
              nullptr);

    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorVisualizationPropertyDomain(
                     Domain::PointCloudPoints),
                 "PointCloudPoints");
    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorVisualizationPropertyValueKind(
                     Kind::Vec4),
                 "Vec4");
}
TEST(SandboxEditorUi, PropertyCatalogListsAllMeshPropertiesAndPreviewsSelection)
{
    using Domain = Runtime::SandboxEditorPropertyCatalogDomain;
    using Kind = Runtime::SandboxEditorPropertyCatalogValueKind;

    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    const ECS::EntityHandle mesh = MakeSelectable(registry, "CatalogMesh");
    AddTriangleMeshSource(registry, mesh);
    auto& vertices = registry.Raw().get<GS::Vertices>(mesh);
    vertices.Properties.GetOrAdd<glm::vec3>("v:normal", glm::vec3{0.0f, 0.0f, 1.0f})
        .Vector() = {
            glm::vec3{0.0f, 0.0f, 1.0f},
            glm::vec3{0.0f, 1.0f, 0.0f},
            glm::vec3{1.0f, 0.0f, 0.0f},
        };
    vertices.Properties.GetOrAdd<float>("v:temperature", 0.0f)
        .Vector() = {0.0f, 0.5f, 1.0f};
    vertices.Properties.GetOrAdd<int>("v:unsupported_int", 0)
        .Vector() = {1, 2, 3};

    auto& edges = registry.Raw().get<GS::Edges>(mesh);
    edges.Properties.GetOrAdd<double>("e:weight", 0.0)
        .Vector() = {1.0, 2.0, 3.0};

    auto& faces = registry.Raw().get<GS::Faces>(mesh);
    faces.Properties.GetOrAdd<glm::vec4>("f:debug_color", glm::vec4{1.0f})
        .Vector() = {glm::vec4{0.1f, 0.2f, 0.3f, 1.0f}};

    registry.Raw().emplace<Runtime::ProgressivePresentationBindings>(
        mesh,
        MakeProgressiveMeshPresentationBindings());

    Runtime::PrimitiveSelectionResult primitive{};
    primitive.Status = Runtime::PrimitiveRefineStatus::Success;
    primitive.EntityId = Runtime::SelectionController::ToStableEntityId(mesh);
    primitive.StableId = Runtime::SelectionController::ToStableEntityId(mesh);
    primitive.Domain = GS::Domain::Mesh;
    primitive.Kind = Runtime::RefinedPrimitiveKind::Vertex;
    primitive.VertexId = 1u;
    primitive.EdgeId = 2u;
    primitive.FaceId = 0u;
    const std::optional<Runtime::PrimitiveSelectionResult> lastPrimitive{primitive};

    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));
    Runtime::SandboxEditorContext context =
        MakeContext(registry, selection, true, &lastPrimitive);

    const Runtime::SandboxEditorPanelFrame frame =
        Runtime::BuildSandboxEditorPanelFrame(context);
    ASSERT_TRUE(frame.Inspector.HasEntity);
    const Runtime::SandboxEditorPropertyCatalogModel& catalog =
        frame.Inspector.PropertyCatalog;
    EXPECT_TRUE(catalog.HasSelectedEntity);
    EXPECT_EQ(catalog.SelectedDomain, GS::Domain::Mesh);
    EXPECT_FALSE(catalog.Rows.empty());

    const auto* position =
        FindCatalogProperty(catalog, Domain::MeshVertices, std::string{PN::kPosition});
    ASSERT_NE(position, nullptr);
    EXPECT_EQ(position->ValueKind, Kind::Vec3);
    EXPECT_TRUE(position->Internal);
    EXPECT_TRUE(position->Connectivity);
    EXPECT_TRUE(position->Bindable);
    EXPECT_TRUE(position->Preview.HasValue);
    EXPECT_EQ(position->Preview.ElementIndex, 1u);

    const auto* texcoord =
        FindCatalogProperty(catalog, Domain::MeshVertices, "v:texcoord");
    ASSERT_NE(texcoord, nullptr);
    EXPECT_EQ(texcoord->ValueKind, Kind::Vec2);
    EXPECT_EQ(texcoord->ComponentCount, 2u);

    const auto* unsupported =
        FindCatalogProperty(catalog, Domain::MeshVertices, "v:unsupported_int");
    ASSERT_NE(unsupported, nullptr);
    EXPECT_EQ(unsupported->ValueKind, Kind::Unknown);
    EXPECT_FALSE(unsupported->Supported);
    EXPECT_FALSE(unsupported->UnsupportedReason.empty());

    const auto* edgeV0 =
        FindCatalogProperty(catalog, Domain::MeshEdges, std::string{PN::kEdgeV0});
    ASSERT_NE(edgeV0, nullptr);
    EXPECT_TRUE(edgeV0->Connectivity);

    EXPECT_NE(FindCatalogProperty(catalog,
                                  Domain::MeshHalfedges,
                                  std::string{PN::kHalfedgeToVertex}),
              nullptr);
    EXPECT_NE(FindCatalogProperty(catalog,
                                  Domain::MeshFaces,
                                  std::string{PN::kFaceHalfedge}),
              nullptr);

    const auto* faceColor =
        FindCatalogProperty(catalog, Domain::MeshFaces, "f:debug_color");
    ASSERT_NE(faceColor, nullptr);
    EXPECT_EQ(faceColor->ValueKind, Kind::Vec4);
    EXPECT_TRUE(faceColor->Preview.HasValue);
    EXPECT_EQ(faceColor->Preview.ElementIndex, 0u);

    ASSERT_FALSE(catalog.BindingTargets.empty());
    const auto normalTarget = std::find_if(
        catalog.BindingTargets.begin(),
        catalog.BindingTargets.end(),
        [](const Runtime::SandboxEditorPropertyBindingTargetModel& target)
        {
            return target.Semantic == Runtime::ProgressiveSlotSemantic::Normal;
        });
    ASSERT_NE(normalTarget, catalog.BindingTargets.end());
    EXPECT_EQ(normalTarget->RequiredDomain,
              Runtime::ProgressiveGeometryDomain::MeshVertex);
    EXPECT_EQ(normalTarget->ExpectedValueKind,
              Runtime::ProgressivePropertyValueKind::Vec3);
    ASSERT_FALSE(normalTarget->Options.empty());
    EXPECT_TRUE(normalTarget->Options.front().Compatible);

    const Runtime::SandboxEditorDomainWindowModel meshWindow =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::Mesh);
    EXPECT_EQ(meshWindow.PropertyCatalog.Rows.size(), catalog.Rows.size());
    EXPECT_FALSE(meshWindow.BoundState.Rows.empty());
    EXPECT_NE(FindBoundRow(meshWindow.BoundState,
                           Runtime::SandboxEditorBoundRenderStateRowKind::ProgressiveSlot,
                           Runtime::ProgressiveSlotSemantic::Normal),
              nullptr);

    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorPropertyCatalogDomain(
                     Domain::MeshHalfedges),
                 "MeshHalfedges");
    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorPropertyCatalogValueKind(
                     Kind::Vec2),
                 "Vec2");
}
TEST(SandboxEditorUi, PropertyCatalogReportsGraphAndPointCloudDomains)
{
    using Domain = Runtime::SandboxEditorPropertyCatalogDomain;
    using Kind = Runtime::SandboxEditorPropertyCatalogValueKind;

    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);

    const ECS::EntityHandle graph = MakeSelectable(registry, "CatalogGraph");
    AddGraphSource(registry, graph);
    auto& graphNodes = registry.Raw().get<GS::Nodes>(graph);
    graphNodes.Properties.GetOrAdd<float>("v:centrality", 0.0f)
        .Vector() = {0.0f, 1.0f, 2.0f};
    auto& graphEdges = registry.Raw().get<GS::Edges>(graph);
    graphEdges.Properties.GetOrAdd<glm::vec4>("e:color", glm::vec4{1.0f})
        .Vector() = {glm::vec4{1.0f}, glm::vec4{0.0f, 1.0f, 0.0f, 1.0f}};

    ASSERT_TRUE(selection.SetSelectedEntity(registry, graph));
    Runtime::SandboxEditorPanelFrame frame =
        Runtime::BuildSandboxEditorPanelFrame(context);
    const Runtime::SandboxEditorPropertyCatalogModel& graphCatalog =
        frame.Inspector.PropertyCatalog;
    EXPECT_NE(FindCatalogProperty(graphCatalog, Domain::GraphVertices, "v:centrality"),
              nullptr);
    const auto* graphEdgeColor =
        FindCatalogProperty(graphCatalog, Domain::GraphEdges, "e:color");
    ASSERT_NE(graphEdgeColor, nullptr);
    EXPECT_EQ(graphEdgeColor->ValueKind, Kind::Vec4);
    EXPECT_NE(FindCatalogProperty(graphCatalog,
                                  Domain::GraphEdges,
                                  std::string{PN::kEdgeV0}),
              nullptr);

    const ECS::EntityHandle cloud = MakeSelectable(registry, "CatalogCloud");
    AddPointCloudSource(registry, cloud, 2u);
    auto& cloudVertices = registry.Raw().get<GS::Vertices>(cloud);
    cloudVertices.Properties.GetOrAdd<glm::vec4>("p:kmeans_color", glm::vec4{1.0f})
        .Vector() = {glm::vec4{1.0f}, glm::vec4{0.0f, 1.0f, 0.0f, 1.0f}};
    cloudVertices.Properties.GetOrAdd<std::uint32_t>("p:label", 0u)
        .Vector() = {7u, 9u};

    ASSERT_TRUE(selection.SetSelectedEntity(registry, cloud));
    frame = Runtime::BuildSandboxEditorPanelFrame(context);
    const Runtime::SandboxEditorPropertyCatalogModel& cloudCatalog =
        frame.Inspector.PropertyCatalog;
    const auto* pointColor =
        FindCatalogProperty(cloudCatalog,
                            Domain::PointCloudPoints,
                            "p:kmeans_color");
    ASSERT_NE(pointColor, nullptr);
    EXPECT_EQ(pointColor->ValueKind, Kind::Vec4);
    EXPECT_TRUE(pointColor->Generated);
    EXPECT_NE(FindCatalogProperty(cloudCatalog,
                                  Domain::PointCloudPoints,
                                  "p:label"),
              nullptr);
}
TEST(SandboxEditorUi, VisualizationPresetPreservesConfiguredScalarStyling)
{
    using Domain = Runtime::SandboxEditorVisualizationPropertyDomain;
    using Preset = Runtime::SandboxEditorVisualizationPropertyPreset;
    using Target = Runtime::SandboxEditorVisualizationTarget;

    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    const ECS::EntityHandle mesh = MakeSelectable(registry, "StylingMesh");
    AddTriangleMeshSource(registry, mesh);
    auto& vertices = registry.Raw().get<GS::Vertices>(mesh);
    vertices.Properties.GetOrAdd<float>("v:temperature", 0.0f)
        .Vector() = {0.0f, 0.5f, 1.0f};
    vertices.Properties.GetOrAdd<double>("v:mean_curvature", 0.0)
        .Vector() = {-0.02, 0.004, 0.05};

    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.VisualizationCommandsAvailable = true;

    // Configure a styled scalar field on the surface lane.
    Runtime::SandboxEditorVisualizationConfigCommand styled{
        .StableEntityId = stableId,
        .Target = Target::Surface,
        .EnableConfig = true,
        .Source = G::VisualizationConfig::ColorSource::ScalarField,
        .ScalarFieldName = "v:temperature",
        .ScalarDomain = G::VisualizationConfig::Domain::Vertex,
        .ScalarAutoRange = true,
        .IsolineCount = 6u,
        .ScalarColormap = Graphics::Colormap::Type::Jet,
        .IsolineWidth = 3.0f,
        .IsolineColor = {1.0f, 0.0f, 0.0f, 1.0f},
    };
    styled.IsolineValues[0] = 0.25f;
    styled.IsolineValues[1] = 0.75f;
    styled.IsolineValueCount = 2u;
    EXPECT_EQ(Runtime::ApplySandboxEditorVisualizationConfigCommand(
                  context,
                  styled),
              Runtime::SandboxEditorCommandStatus::Applied);

    {
        ASSERT_TRUE(registry.Raw().all_of<G::VisualizationLaneOverrides>(mesh));
        const auto& overrides =
            registry.Raw().get<G::VisualizationLaneOverrides>(mesh);
        ASSERT_TRUE(overrides.Surface.has_value());
        EXPECT_EQ(overrides.Surface->Scalar.Map, Graphics::Colormap::Type::Jet);
        EXPECT_FLOAT_EQ(overrides.Surface->Scalar.Isolines.Width, 3.0f);
        EXPECT_EQ(overrides.Surface->Scalar.Isolines.ValueCount, 2u);
        EXPECT_FLOAT_EQ(overrides.Surface->Scalar.Isolines.Values[0], 0.25f);
        EXPECT_FLOAT_EQ(overrides.Surface->Scalar.Isolines.Values[1], 0.75f);
    }

    // Switching the property through the Scalar preset must keep the styling.
    EXPECT_EQ(Runtime::ApplySandboxEditorVisualizationPropertyCommand(
                  context,
                  Runtime::SandboxEditorVisualizationPropertyCommand{
                      .StableEntityId = stableId,
                      .Target = Target::Surface,
                      .Domain = Domain::MeshVertices,
                      .Preset = Preset::Scalar,
                      .PropertyName = "v:mean_curvature",
                  }),
              Runtime::SandboxEditorCommandStatus::Applied);

    ASSERT_TRUE(registry.Raw().all_of<G::VisualizationLaneOverrides>(mesh));
    const auto& overrides =
        registry.Raw().get<G::VisualizationLaneOverrides>(mesh);
    ASSERT_TRUE(overrides.Surface.has_value());
    const G::VisualizationConfig& lane = *overrides.Surface;
    EXPECT_EQ(lane.Source, G::VisualizationConfig::ColorSource::ScalarField);
    EXPECT_EQ(lane.ScalarFieldName, "v:mean_curvature");
    EXPECT_EQ(lane.Scalar.Isolines.Num, 0u);
    EXPECT_EQ(lane.Scalar.Map, Graphics::Colormap::Type::Jet);
    EXPECT_FLOAT_EQ(lane.Scalar.Isolines.Width, 3.0f);
    EXPECT_FLOAT_EQ(lane.Scalar.Isolines.Color.x, 1.0f);
    EXPECT_EQ(lane.Scalar.Isolines.ValueCount, 2u);
    EXPECT_FLOAT_EQ(lane.Scalar.Isolines.Values[0], 0.25f);
    EXPECT_FLOAT_EQ(lane.Scalar.Isolines.Values[1], 0.75f);
}
TEST(SandboxEditorUi, DomainWindowModelsReportSelectedMeshGraphAndPointCloudState)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;

    const ECS::EntityHandle mesh = MakeSelectable(registry, "Mesh");
    AddTriangleMeshSource(registry, mesh);
    registry.Raw().emplace<G::RenderSurface>(mesh);
    registry.Raw().emplace<ECSC::SpatialDebugBinding>(
        mesh,
        ECSC::SpatialDebugBinding{
            .Kind = ECSC::SpatialDebugGeometryKind::Bvh,
            .RegistryKey = 77u,
        });
    G::VisualizationConfig meshVisualization{};
    meshVisualization.Source = G::VisualizationConfig::ColorSource::UniformColor;
    meshVisualization.Color = glm::vec4{1.0f};
    registry.Raw().emplace<G::VisualizationConfig>(mesh, meshVisualization);

    const ECS::EntityHandle graph = MakeSelectable(registry, "Graph");
    AddGraphSource(registry, graph);

    const ECS::EntityHandle cloud = MakeSelectable(registry, "Cloud");
    AddPointCloudSource(registry, cloud, 4u);

    const std::uint32_t meshStableId =
        Runtime::SelectionController::ToStableEntityId(mesh);
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.VisualizationCommandsAvailable = true;

    std::optional<Runtime::PrimitiveSelectionResult> primitive{
        Runtime::PrimitiveSelectionResult{
            .Status = Runtime::PrimitiveRefineStatus::Success,
            .EntityId = meshStableId,
            .StableId = meshStableId,
            .Domain = GS::Domain::Mesh,
            .Kind = Runtime::RefinedPrimitiveKind::Face,
            .FaceId = 0u,
            .EdgeId = Runtime::kInvalidPrimitiveIndex,
            .VertexId = 2u,
            .PointId = Runtime::kInvalidPrimitiveIndex,
        }};
    context.LastRefinedPrimitive = &primitive;

    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));
    Runtime::SandboxEditorDomainWindowModel meshModel =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::Mesh);
    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorDomainWindowKind(
                     meshModel.Kind),
                 "Mesh");
    EXPECT_EQ(meshModel.ExpectedDomain, GS::Domain::Mesh);
    EXPECT_TRUE(meshModel.HasSelectedEntity);
    EXPECT_EQ(meshModel.SelectedEntity.Name, "Mesh");
    EXPECT_EQ(meshModel.SelectedStableId, meshStableId);
    EXPECT_EQ(meshModel.SelectedDomain, GS::Domain::Mesh);
    EXPECT_TRUE(meshModel.DomainMatches);
    EXPECT_TRUE(meshModel.RenderHints.HasRenderSurface);
    EXPECT_FALSE(meshModel.PrimitiveViewControlsAvailable);
    EXPECT_FALSE(meshModel.HasPrimitiveViewSettings);
    EXPECT_TRUE(meshModel.VisualizationControlsAvailable);
    EXPECT_TRUE(meshModel.Visualization.HasSelectedEntity);
    EXPECT_TRUE(meshModel.Visualization.SpatialDebug.HasBinding);
    EXPECT_EQ(meshModel.Visualization.SpatialDebug.RegistryKey, 77u);
    EXPECT_TRUE(meshModel.Visualization.Visualization.HasConfig);
    ASSERT_TRUE(meshModel.Primitive.HasPrimitive);
    EXPECT_TRUE(meshModel.Primitive.HasFaceId);
    EXPECT_TRUE(meshModel.Primitive.HasVertexId);
    ASSERT_TRUE(meshModel.Processing.HasSelectedEntity);
    ASSERT_EQ(meshModel.Processing.KMeansDomains.size(), 1u);
    EXPECT_EQ(meshModel.Processing.KMeansDomains[0],
              Runtime::SandboxEditorGeometryProcessingDomain::MeshVertices);
    ASSERT_FALSE(meshModel.Processing.Entries.empty());
    EXPECT_EQ(meshModel.Processing.Entries[0].Algorithm,
              Runtime::SandboxEditorGeometryProcessingAlgorithm::KMeans);

    const Runtime::SandboxEditorDomainWindowModel graphWhileMeshSelected =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::Graph);
    EXPECT_FALSE(graphWhileMeshSelected.DomainMatches);
    EXPECT_FALSE(graphWhileMeshSelected.Processing.HasSelectedEntity);
    EXPECT_TRUE(graphWhileMeshSelected.Processing.Entries.empty());
    EXPECT_TRUE(HasDiagnostic(
        graphWhileMeshSelected.Diagnostics,
        Runtime::SandboxEditorDiagnosticCode::UnsupportedGeometryDomain));

    const std::uint32_t graphStableId =
        Runtime::SelectionController::ToStableEntityId(graph);
    primitive = Runtime::PrimitiveSelectionResult{
        .Status = Runtime::PrimitiveRefineStatus::Success,
        .EntityId = graphStableId,
        .StableId = graphStableId,
        .Domain = GS::Domain::Graph,
        .Kind = Runtime::RefinedPrimitiveKind::Edge,
        .FaceId = Runtime::kInvalidPrimitiveIndex,
        .EdgeId = 1u,
        .VertexId = 2u,
        .PointId = Runtime::kInvalidPrimitiveIndex,
    };
    ASSERT_TRUE(selection.SetSelectedEntity(registry, graph));
    const Runtime::SandboxEditorDomainWindowModel graphModel =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::Graph);
    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorDomainWindowKind(
                     graphModel.Kind),
                 "Graph");
    EXPECT_EQ(graphModel.SelectedDomain, GS::Domain::Graph);
    EXPECT_TRUE(graphModel.DomainMatches);
    EXPECT_TRUE(graphModel.RenderHints.HasRenderEdges);
    EXPECT_TRUE(graphModel.RenderHints.HasRenderPoints);
    EXPECT_FALSE(graphModel.PrimitiveViewControlsAvailable);
    ASSERT_TRUE(graphModel.Primitive.HasPrimitive);
    EXPECT_TRUE(graphModel.Primitive.HasEdgeId);
    EXPECT_TRUE(graphModel.Primitive.HasVertexId);
    ASSERT_TRUE(graphModel.Processing.HasSelectedEntity);
    ASSERT_EQ(graphModel.Processing.KMeansDomains.size(), 1u);
    EXPECT_EQ(graphModel.Processing.KMeansDomains[0],
              Runtime::SandboxEditorGeometryProcessingDomain::GraphVertices);

    const std::uint32_t cloudStableId =
        Runtime::SelectionController::ToStableEntityId(cloud);
    primitive = Runtime::PrimitiveSelectionResult{
        .Status = Runtime::PrimitiveRefineStatus::Success,
        .EntityId = cloudStableId,
        .StableId = cloudStableId,
        .Domain = GS::Domain::PointCloud,
        .Kind = Runtime::RefinedPrimitiveKind::Point,
        .FaceId = Runtime::kInvalidPrimitiveIndex,
        .EdgeId = Runtime::kInvalidPrimitiveIndex,
        .VertexId = Runtime::kInvalidPrimitiveIndex,
        .PointId = 3u,
    };
    ASSERT_TRUE(selection.SetSelectedEntity(registry, cloud));
    const Runtime::SandboxEditorDomainWindowModel cloudModel =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::PointCloud);
    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorDomainWindowKind(
                     cloudModel.Kind),
                 "PointCloud");
    EXPECT_EQ(cloudModel.SelectedDomain, GS::Domain::PointCloud);
    EXPECT_TRUE(cloudModel.DomainMatches);
    EXPECT_TRUE(cloudModel.RenderHints.HasRenderPoints);
    ASSERT_TRUE(cloudModel.Primitive.HasPrimitive);
    EXPECT_TRUE(cloudModel.Primitive.HasPointId);
    ASSERT_TRUE(cloudModel.Processing.HasSelectedEntity);
    ASSERT_EQ(cloudModel.Processing.KMeansDomains.size(), 1u);
    EXPECT_EQ(cloudModel.Processing.KMeansDomains[0],
              Runtime::SandboxEditorGeometryProcessingDomain::PointCloudPoints);
}
TEST(SandboxEditorUi, DomainVisualizationTargetsFollowLaneSourcePresence)
{
    using Target = Runtime::SandboxEditorVisualizationTarget;
    using Domain = Runtime::SandboxEditorVisualizationPropertyDomain;

    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;

    const ECS::EntityHandle mesh = MakeSelectable(registry, "Mesh");
    AddTriangleMeshSource(registry, mesh);
    registry.Raw().emplace<G::RenderPoints>(mesh);
    auto& meshVertices = registry.Raw().get<GS::Vertices>(mesh);
    meshVertices.Properties.GetOrAdd<float>("v:temperature", 0.0f)
        .Vector() = {0.0f, 0.5f, 1.0f};

    const ECS::EntityHandle graph = MakeSelectable(registry, "Graph");
    AddGraphSource(registry, graph);
    auto& graphNodes = registry.Raw().get<GS::Nodes>(graph);
    graphNodes.Properties.GetOrAdd<float>("v:weight", 0.0f)
        .Vector() = {1.0f, 2.0f, 3.0f};

    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.VisualizationCommandsAvailable = true;

    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));
    const Runtime::SandboxEditorDomainWindowModel meshPointModel =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::PointCloud);
    EXPECT_EQ(meshPointModel.SelectedDomain, GS::Domain::Mesh);
    EXPECT_FALSE(meshPointModel.DomainMatches);
    EXPECT_TRUE(meshPointModel.VisualizationTargetAvailable);
    EXPECT_EQ(meshPointModel.VisualizationTarget, Target::Points);
    EXPECT_EQ(meshPointModel.Visualization.Target, Target::Points);
    EXPECT_TRUE(meshPointModel.Visualization.TargetAvailable);
    EXPECT_NE(FindVisualizationProperty(meshPointModel.Visualization.Properties,
                                        Domain::MeshVertices,
                                        "v:temperature"),
              nullptr);

    const ECS::EntityHandle wireMesh = MakeSelectable(registry, "Wire Mesh");
    AddTriangleMeshSource(registry, wireMesh);
    registry.Raw().remove<GS::Edges>(wireMesh);
    registry.Raw().emplace<G::RenderEdges>(wireMesh);
    auto& wireFaces = registry.Raw().get<GS::Faces>(wireMesh);
    wireFaces.Properties.GetOrAdd<float>("f:temperature", 0.0f)
        .Vector() = {1.0f};

    ASSERT_TRUE(selection.SetSelectedEntity(registry, wireMesh));
    const Runtime::SandboxEditorDomainWindowModel wireMeshEdgeModel =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::Graph);
    EXPECT_EQ(wireMeshEdgeModel.SelectedDomain, GS::Domain::Unknown);
    EXPECT_FALSE(wireMeshEdgeModel.DomainMatches);
    EXPECT_TRUE(wireMeshEdgeModel.VisualizationTargetAvailable);
    EXPECT_EQ(wireMeshEdgeModel.VisualizationTarget, Target::Edges);

    ASSERT_TRUE(selection.SetSelectedEntity(registry, graph));
    const Runtime::SandboxEditorDomainWindowModel graphPointModel =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::PointCloud);
    EXPECT_EQ(graphPointModel.SelectedDomain, GS::Domain::Graph);
    EXPECT_FALSE(graphPointModel.DomainMatches);
    EXPECT_TRUE(graphPointModel.VisualizationTargetAvailable);
    EXPECT_EQ(graphPointModel.VisualizationTarget, Target::Points);
    EXPECT_EQ(graphPointModel.Visualization.Target, Target::Points);
    EXPECT_NE(FindVisualizationProperty(graphPointModel.Visualization.Properties,
                                        Domain::GraphVertices,
                                        "v:weight"),
              nullptr);

    const Runtime::SandboxEditorDomainWindowModel graphEdgeModel =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::Graph);
    EXPECT_TRUE(graphEdgeModel.DomainMatches);
    EXPECT_TRUE(graphEdgeModel.VisualizationTargetAvailable);
    EXPECT_EQ(graphEdgeModel.VisualizationTarget, Target::Edges);
    EXPECT_EQ(graphEdgeModel.Visualization.Target, Target::Edges);
    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorVisualizationTarget(
                     graphEdgeModel.VisualizationTarget),
                 "Edges");
}
TEST(SandboxEditorUi, DomainWindowModelsReportNoSelectionStaleAndWrongDomain)
{
    const Runtime::SandboxEditorDomainWindowModel missingScene =
        Runtime::BuildSandboxEditorDomainWindowModel(
            Runtime::SandboxEditorContext{},
            Runtime::SandboxEditorDomainWindowKind::Mesh);
    EXPECT_TRUE(HasDiagnostic(
        missingScene.Diagnostics,
        Runtime::SandboxEditorDiagnosticCode::MissingScene));
    EXPECT_FALSE(missingScene.HasSelectedEntity);

    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;

    Runtime::SandboxEditorContext missingSelection = MakeContext(registry, selection);
    missingSelection.Selection = nullptr;
    const Runtime::SandboxEditorDomainWindowModel missingSelectionModel =
        Runtime::BuildSandboxEditorDomainWindowModel(
            missingSelection,
            Runtime::SandboxEditorDomainWindowKind::Mesh);
    EXPECT_TRUE(HasDiagnostic(
        missingSelectionModel.Diagnostics,
        Runtime::SandboxEditorDiagnosticCode::MissingSelectionController));

    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    const Runtime::SandboxEditorDomainWindowModel noSelection =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::Mesh);
    EXPECT_TRUE(HasDiagnostic(
        noSelection.Diagnostics,
        Runtime::SandboxEditorDiagnosticCode::NoSelectedEntity));
    EXPECT_FALSE(noSelection.DomainMatches);

    const ECS::EntityHandle stale = MakeSelectable(registry, "Stale");
    ASSERT_TRUE(selection.SetSelectedEntity(registry, stale));
    registry.Raw().destroy(stale);
    const Runtime::SandboxEditorDomainWindowModel staleSelection =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::Mesh);
    EXPECT_TRUE(HasDiagnostic(
        staleSelection.Diagnostics,
        Runtime::SandboxEditorDiagnosticCode::NoSelectedEntity));
    EXPECT_FALSE(staleSelection.HasSelectedEntity);

    selection.ClearSelection(registry);
    const ECS::EntityHandle cloud = MakeSelectable(registry, "Cloud");
    AddPointCloudSource(registry, cloud, 2u);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, cloud));
    const Runtime::SandboxEditorDomainWindowModel wrongDomain =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::Mesh);
    EXPECT_TRUE(wrongDomain.HasSelectedEntity);
    EXPECT_EQ(wrongDomain.ExpectedDomain, GS::Domain::Mesh);
    EXPECT_EQ(wrongDomain.SelectedDomain, GS::Domain::PointCloud);
    EXPECT_FALSE(wrongDomain.DomainMatches);
    EXPECT_TRUE(HasDiagnostic(
        wrongDomain.Diagnostics,
        Runtime::SandboxEditorDiagnosticCode::UnsupportedGeometryDomain));
}
TEST(SandboxEditorUi, SelectionDetailsModelReportsHoveredEntityAndPrimitiveIds)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    const ECS::EntityHandle selected = MakeSelectable(registry, "SelectedMesh");
    const ECS::EntityHandle hovered = MakeSelectable(registry, "HoveredMesh");
    ASSERT_TRUE(selection.SetSelectedEntity(registry, selected));

    selection.RequestHoverPick(10u, 20u);
    const std::optional<Runtime::PendingSelectionPick> pending =
        selection.ConsumePendingPick();
    ASSERT_TRUE(pending.has_value());
    selection.ConsumeHit(registry,
                         Runtime::SelectionController::ToStableEntityId(hovered),
                         pending->Sequence);

    std::optional<Runtime::PrimitiveSelectionResult> primitive{
        Runtime::PrimitiveSelectionResult{
            .Status = Runtime::PrimitiveRefineStatus::Success,
            .EntityId = Runtime::SelectionController::ToStableEntityId(selected),
            .StableId = Runtime::SelectionController::ToStableEntityId(selected),
            .Domain = GS::Domain::Mesh,
            .Kind = Runtime::RefinedPrimitiveKind::Face,
            .FaceId = 12u,
            .EdgeId = 3u,
            .VertexId = 4u,
            .PointId = Runtime::kInvalidPrimitiveIndex,
            .HasHitPosition = true,
            .LocalHit = glm::vec3{1.0f, 2.0f, 3.0f},
            .WorldHit = glm::vec3{4.0f, 5.0f, 6.0f},
        }};

    const Runtime::SandboxEditorPanelFrame frame =
        Runtime::BuildSandboxEditorPanelFrame(
            MakeContext(registry, selection, true, &primitive));

    ASSERT_EQ(frame.Selection.SelectedEntities.size(), 1u);
    EXPECT_EQ(frame.Selection.SelectedEntities[0].Entity, selected);
    ASSERT_TRUE(frame.Selection.HasHovered);
    ASSERT_TRUE(frame.Selection.HasHoveredEntity);
    EXPECT_EQ(frame.Selection.HoveredEntity.Entity, hovered);
    ASSERT_TRUE(frame.Selection.Primitive.HasPrimitive);
    EXPECT_TRUE(frame.Selection.Primitive.HasFaceId);
    EXPECT_TRUE(frame.Selection.Primitive.HasEdgeId);
    EXPECT_TRUE(frame.Selection.Primitive.HasVertexId);
    EXPECT_FALSE(frame.Selection.Primitive.HasPointId);
    EXPECT_EQ(frame.Selection.Primitive.Primitive.Kind,
              Runtime::RefinedPrimitiveKind::Face);
    EXPECT_EQ(frame.Selection.Primitive.Primitive.FaceId, 12u);
    EXPECT_FLOAT_EQ(frame.Selection.Primitive.Primitive.WorldHit.z, 6.0f);
    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorPrimitiveKind(
                     frame.Selection.Primitive.Primitive.Kind),
                 "Face");
}
TEST(SandboxEditorUi, ProgressiveInspectorReportsSlotsPropertiesAndJobs)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    const ECS::EntityHandle mesh = MakeSelectable(registry, "ProgressiveMesh");
    AddTriangleMeshSource(registry, mesh);
    auto& vertices = registry.Raw().get<GS::Vertices>(mesh);
    vertices.Properties.GetOrAdd<glm::vec3>("v:normal", glm::vec3{0.0f, 0.0f, 1.0f})
        .Vector() = {
            glm::vec3{0.0f, 0.0f, 1.0f},
            glm::vec3{0.0f, 0.0f, 1.0f},
            glm::vec3{0.0f, 0.0f, 1.0f},
        };
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

    const Runtime::DerivedJobHandle dependencyHandle{99u, 1u};
    Runtime::DerivedJobQueueSnapshot jobs{};
    const std::vector<Runtime::DerivedJobStatus> statuses{
        Runtime::DerivedJobStatus::Blocked,
        Runtime::DerivedJobStatus::Queued,
        Runtime::DerivedJobStatus::Running,
        Runtime::DerivedJobStatus::Applying,
        Runtime::DerivedJobStatus::Complete,
        Runtime::DerivedJobStatus::Failed,
        Runtime::DerivedJobStatus::Cancelled,
        Runtime::DerivedJobStatus::StaleDiscarded,
    };
    for (std::size_t i = 0u; i < statuses.size(); ++i)
    {
        jobs.Entries.push_back(Runtime::DerivedJobSnapshot{
            .Handle = Runtime::DerivedJobHandle{
                static_cast<std::uint32_t>(10u + i),
                1u},
            .Key = Runtime::DerivedJobKey{
                .EntityId = stableId,
                .Domain = Runtime::ProgressiveGeometryDomain::MeshVertex,
                .OutputSemantic = Runtime::ProgressiveSlotSemantic::Normal,
                .BindingGeneration = 7u,
                .OutputName = "normal",
            },
            .Name = "progressive job",
            .Status = statuses[i],
            .Dependencies = {
                Runtime::DerivedJobDependency{
                    .Job = dependencyHandle,
                    .Reason = "normal requires uv",
                },
            },
            .NormalizedProgress = static_cast<float>(i) /
                                  static_cast<float>(statuses.size()),
            .Diagnostic = i == 5u ? "failed bake" : std::string{},
        });
    }

    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.DerivedJobs = &jobs;

    const Runtime::SandboxEditorPanelFrame frame =
        Runtime::BuildSandboxEditorPanelFrame(context);

    ASSERT_TRUE(frame.Inspector.HasEntity);
    const Runtime::SandboxEditorProgressiveRenderDataModel& progressive =
        frame.Inspector.Progressive;
    ASSERT_TRUE(progressive.HasBindings);
    EXPECT_EQ(progressive.Shape, Runtime::ProgressiveEntityShape::MeshLeaf);
    EXPECT_EQ(progressive.BindingGeneration, 7u);
    EXPECT_EQ(progressive.Slots.size(), 2u);
    EXPECT_EQ(progressive.Jobs.size(), statuses.size());
    EXPECT_EQ(progressive.Jobs[0].Status, Runtime::DerivedJobStatus::Blocked);
    ASSERT_EQ(progressive.Jobs[0].Dependencies.size(), 1u);
    EXPECT_EQ(progressive.Jobs[0].Dependencies[0].Reason, "normal requires uv");
    EXPECT_EQ(progressive.Jobs[5].Diagnostic, "failed bake");

    const Runtime::SandboxEditorProgressiveSlotModel* normal =
        FindProgressiveSlot(progressive, Runtime::ProgressiveSlotSemantic::Normal);
    ASSERT_NE(normal, nullptr);
    EXPECT_EQ(normal->Readiness, Runtime::ProgressiveReadinessState::Pending);
    EXPECT_EQ(normal->Property.PropertyName, "v:normal");
    ASSERT_FALSE(normal->PropertyOptions.empty());
    EXPECT_TRUE(normal->PropertyOptions.front().Compatible);

    const auto disabled = std::find_if(
        normal->PropertyOptions.begin(),
        normal->PropertyOptions.end(),
        [](const Runtime::SandboxEditorProgressivePropertyOptionModel& option)
        {
            return option.Descriptor.PropertyName == "v:temperature";
        });
    ASSERT_NE(disabled, normal->PropertyOptions.end());
    EXPECT_FALSE(disabled->Compatible);
    EXPECT_FALSE(disabled->DisabledReason.empty());

    const Runtime::SandboxEditorBoundRenderStateModel& bound =
        frame.Inspector.BoundState;
    EXPECT_TRUE(bound.HasSelectedEntity);
    EXPECT_EQ(bound.SelectedStableId, stableId);
    EXPECT_EQ(bound.BindingGeneration, progressive.BindingGeneration);
    EXPECT_GE(bound.Rows.size(), progressive.Slots.size() + progressive.Jobs.size());

    const Runtime::SandboxEditorBoundRenderStateRow* normalBound =
        FindBoundRow(bound,
                     Runtime::SandboxEditorBoundRenderStateRowKind::ProgressiveSlot,
                     Runtime::ProgressiveSlotSemantic::Normal);
    ASSERT_NE(normalBound, nullptr);
    EXPECT_EQ(normalBound->SourceKind,
              Runtime::ProgressiveSlotSourceKind::PropertyBake);
    EXPECT_EQ(normalBound->Readiness, Runtime::ProgressiveReadinessState::Pending);
    EXPECT_EQ(normalBound->Property.PropertyName, "v:normal");
    EXPECT_TRUE(normalBound->HasCatalogMatch);
    ASSERT_TRUE(normalBound->CatalogRowIndex.has_value());
    EXPECT_EQ(frame.Inspector.PropertyCatalog.Rows[*normalBound->CatalogRowIndex].Name,
              "v:normal");

    const auto failedJob = std::find_if(
        bound.Rows.begin(),
        bound.Rows.end(),
        [](const Runtime::SandboxEditorBoundRenderStateRow& row)
        {
            return row.Kind ==
                       Runtime::SandboxEditorBoundRenderStateRowKind::DerivedJob &&
                   row.JobStatus == Runtime::DerivedJobStatus::Failed;
        });
    ASSERT_NE(failedJob, bound.Rows.end());
    EXPECT_EQ(failedJob->Diagnostic, "failed bake");
    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorBoundRenderStateRowKind(
                     Runtime::SandboxEditorBoundRenderStateRowKind::DerivedJob),
                 "DerivedJob");
}
TEST(SandboxEditorUi, ProgressiveInspectorInfersGraphPointCloudAndComposition)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;

    const ECS::EntityHandle graph = MakeSelectable(registry, "ProgressiveGraph");
    AddGraphSource(registry, graph);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, graph));
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    Runtime::SandboxEditorPanelFrame frame =
        Runtime::BuildSandboxEditorPanelFrame(context);
    EXPECT_EQ(frame.Inspector.Progressive.Shape,
              Runtime::ProgressiveEntityShape::GraphLeaf);
    const Runtime::SandboxEditorBoundRenderStateRow* graphBake =
        FindBoundRowLabel(
            frame.Inspector.BoundState,
            Runtime::SandboxEditorBoundRenderStateRowKind::DisabledCommand,
            "Texture bake");
    ASSERT_NE(graphBake, nullptr);
    EXPECT_FALSE(graphBake->Enabled);
    EXPECT_FALSE(graphBake->DisabledReason.empty());

    const ECS::EntityHandle cloud = MakeSelectable(registry, "ProgressiveCloud");
    AddPointCloudSource(registry, cloud, 3u);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, cloud));
    frame = Runtime::BuildSandboxEditorPanelFrame(context);
    EXPECT_EQ(frame.Inspector.Progressive.Shape,
              Runtime::ProgressiveEntityShape::PointCloudLeaf);
    const Runtime::SandboxEditorBoundRenderStateRow* cloudBake =
        FindBoundRowLabel(
            frame.Inspector.BoundState,
            Runtime::SandboxEditorBoundRenderStateRowKind::DisabledCommand,
            "Texture bake");
    ASSERT_NE(cloudBake, nullptr);
    EXPECT_FALSE(cloudBake->Enabled);

    const ECS::EntityHandle parent = MakeSelectable(registry, "ProgressiveModel");
    const ECS::EntityHandle child = MakeSelectable(registry, "ProgressiveChild");
    AddTriangleMeshSource(registry, child);
    auto& childVertices = registry.Raw().get<GS::Vertices>(child);
    childVertices.Properties.GetOrAdd<glm::vec3>("v:normal", glm::vec3{0.0f, 0.0f, 1.0f})
        .Vector() = {
            glm::vec3{0.0f, 0.0f, 1.0f},
            glm::vec3{0.0f, 0.0f, 1.0f},
            glm::vec3{0.0f, 0.0f, 1.0f},
        };
    registry.Raw().emplace<Runtime::ProgressivePresentationBindings>(
        child,
        MakeProgressiveMeshPresentationBindings());
    ECS::Hierarchy::Attach(registry.Raw(), child, parent);

    Runtime::DerivedJobQueueSnapshot jobs{};
    jobs.Entries.push_back(Runtime::DerivedJobSnapshot{
        .Handle = Runtime::DerivedJobHandle{77u, 1u},
        .Key = Runtime::DerivedJobKey{
            .EntityId = Runtime::SelectionController::ToStableEntityId(child),
            .Domain = Runtime::ProgressiveGeometryDomain::MeshVertex,
            .OutputSemantic = Runtime::ProgressiveSlotSemantic::Normal,
            .BindingGeneration = 7u,
            .OutputName = "normal",
        },
        .Name = "child normal bake",
        .Status = Runtime::DerivedJobStatus::Running,
    });
    context.DerivedJobs = &jobs;
    ASSERT_TRUE(selection.SetSelectedEntity(registry, parent));
    frame = Runtime::BuildSandboxEditorPanelFrame(context);

    EXPECT_EQ(frame.Inspector.Progressive.Shape,
              Runtime::ProgressiveEntityShape::Composition);
    EXPECT_TRUE(frame.Inspector.Progressive.Composition.HasChildren);
    EXPECT_EQ(frame.Inspector.Progressive.Composition.ChildCount, 1u);
    EXPECT_EQ(frame.Inspector.Progressive.Composition.ChildBindingsCount, 1u);
    EXPECT_EQ(frame.Inspector.Progressive.Composition.ChildPendingSlotCount, 1u);
    EXPECT_EQ(frame.Inspector.Progressive.Composition.ChildJobCount, 1u);
    EXPECT_EQ(frame.Inspector.Progressive.Composition.ChildActiveJobCount, 1u);
    const Runtime::SandboxEditorBoundRenderStateRow* composition =
        FindBoundRowLabel(
            frame.Inspector.BoundState,
            Runtime::SandboxEditorBoundRenderStateRowKind::CompositionSummary,
            "Composition summary");
    ASSERT_NE(composition, nullptr);
    EXPECT_EQ(composition->Readiness,
              Runtime::ProgressiveReadinessState::Pending);
    EXPECT_EQ(frame.Inspector.BoundState.Composition.ChildCount, 1u);
}
TEST(SandboxEditorUi, RenderRecipeEditorModelListsDeclaredRecipeControls)
{
    Graphics::RenderRecipeConfigContext recipeContext =
        MakeRenderRecipeConfigContext();
    Runtime::SandboxEditorRenderRecipeEditorState editorState{};
    Runtime::RenderArtifactRegistry artifacts;
    ASSERT_TRUE(artifacts.RegisterArtifact(
                            MakeSandboxRenderArtifact("sandbox-artifact"))
                    .Succeeded());

    Runtime::SandboxEditorContext context = MakeRenderRecipeEditorContext(
        recipeContext,
        editorState,
        &artifacts);
    const Runtime::SandboxEditorPanelFrame frame =
        Runtime::BuildSandboxEditorPanelFrame(context);
    const Runtime::SandboxEditorRenderRecipeEditorModel& model =
        frame.RenderRecipe;

    ASSERT_TRUE(model.Available);
    EXPECT_EQ(model.RendererId, Graphics::kCurrentRendererContractId);
    EXPECT_EQ(model.ActiveRecipeId, Graphics::kCurrentRendererDefaultRecipeId);
    EXPECT_FALSE(model.CanValidate);
    EXPECT_FALSE(model.CanPreview);
    EXPECT_FALSE(model.CanActivate);

    const Runtime::SandboxEditorRenderRecipeSlotModel* fixedCore =
        FindRecipeSlotRow(model, "default-frame-core");
    ASSERT_NE(fixedCore, nullptr);
    EXPECT_TRUE(fixedCore->DeclaredByRenderer);
    EXPECT_FALSE(fixedCore->Editable);
    EXPECT_EQ(fixedCore->Kind, Graphics::RecipeSlotKind::FixedCore);

    const Runtime::SandboxEditorRenderRecipeSlotModel* lighting =
        FindRecipeSlotRow(model, "lighting");
    ASSERT_NE(lighting, nullptr);
    EXPECT_TRUE(lighting->DeclaredByRenderer);
    EXPECT_TRUE(lighting->Editable);
    EXPECT_EQ(lighting->Kind, Graphics::RecipeSlotKind::Extension);

    const Runtime::SandboxEditorRenderRecipeBindingOverrideModel* material =
        FindRecipeBindingRow(model, "material-table");
    ASSERT_NE(material, nullptr);
    EXPECT_TRUE(material->Required);
    EXPECT_FALSE(material->Editable);

    const Runtime::SandboxEditorRenderRecipeBindingOverrideModel* lights =
        FindRecipeBindingRow(model, "light-snapshots");
    ASSERT_NE(lights, nullptr);
    EXPECT_FALSE(lights->Required);
    EXPECT_TRUE(lights->Editable);
    EXPECT_EQ(lights->Slot, "lighting");

    ASSERT_NE(FindRecipeOutputRow(model, "color"), nullptr);

    const Runtime::SandboxEditorRenderArtifactRow* artifact =
        FindRenderArtifactRow(model, "sandbox-artifact");
    ASSERT_NE(artifact, nullptr);
    EXPECT_TRUE(artifact->CanPublish);
    EXPECT_FALSE(artifact->CanApply);
    EXPECT_EQ(artifact->Status,
              Runtime::RenderArtifactUiStatus::Unpublished);
}
