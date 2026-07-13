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
#include <imgui.h>
#include <imgui_internal.h>

#include "ProgressivePoissonReference.hpp"
#include "TestImGuiFrameScope.hpp"

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
import Extrinsic.Graphics.ImGuiOverlaySystem;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.RenderGraph;
import Extrinsic.Graphics.RenderRecipeConfig;
import Extrinsic.Graphics.RenderingContract;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Platform.Window;
import Extrinsic.RHI.Device;
import Extrinsic.Runtime.AssetImportPipeline;
import Extrinsic.Runtime.AssetIngestStateMachine;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.DerivedJobGraph;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.ImGuiAdapter;
import Extrinsic.Runtime.KMeansGpuJobQueue;
import Extrinsic.Runtime.MeshAttributeTextureBake;
import Extrinsic.Runtime.MeshPrimitiveViewPacker;
import Extrinsic.Runtime.ProgressiveRenderData;
import Extrinsic.Runtime.PrimitiveSelectionRefinement;
import Extrinsic.Runtime.RenderArtifactPublication;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.SandboxDefaultPolicies;
import Extrinsic.Runtime.SandboxEditorUi;
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

    void InstallSandboxDefaultRuntimePolicies(Runtime::Engine& engine)
    {
        (void)Runtime::RegisterSandboxDefaultRuntimePolicies(engine);
    }

    [[nodiscard]] bool ImGuiWindowExists(const std::string_view name)
    {
        const ImGuiContext* context = ImGui::GetCurrentContext();
        if (context == nullptr)
            return false;

        for (const ImGuiWindow* window : context->Windows)
        {
            if (window != nullptr &&
                std::string_view{window->Name} == name)
            {
                return true;
            }
        }
        return false;
    }

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

    [[nodiscard]] std::uint32_t SumCounts(
        const std::vector<std::uint32_t>& counts)
    {
        std::uint32_t sum = 0u;
        for (const std::uint32_t count : counts)
            sum += count;
        return sum;
    }

    [[nodiscard]] bool LogSnapshotContains(
        const Core::Log::LogSnapshot& snapshot,
        const std::string_view needle)
    {
        for (const Core::Log::LogEntry& entry : snapshot.Entries)
        {
            if (entry.Message.find(needle) != std::string::npos)
                return true;
        }
        return false;
    }

    [[nodiscard]] bool LogSnapshotContains(
        const Core::Log::LogSnapshot& snapshot,
        const Core::Log::Level level,
        const std::string_view needle)
    {
        for (const Core::Log::LogEntry& entry : snapshot.Entries)
        {
            if (entry.Lvl == level &&
                entry.Message.find(needle) != std::string::npos)
            {
                return true;
            }
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

    [[nodiscard]] std::string ValidSandboxRenderRecipeConfig()
    {
        return std::string{R"json({
  "schema": ")json"} + std::string{Graphics::kRenderRecipeConfigSchemaId} +
               R"json(",
  "version": 1,
  "rendererId": ")json" +
               std::string{Graphics::kCurrentRendererContractId} + R"json(",
  "revision": "sandbox-ui-test",
  "recipe": {
    "recipeId": "current-renderer.user-preview",
    "fixedCoreName": "Extrinsic.Graphics.FrameRecipe.Default",
    "slots": [
      {
        "name": "lighting",
        "schemaId": "intrinsic.graphics.lighting/sandbox-preview/v1",
        "defaults": "sandbox lighting defaults",
        "requiredCapabilities": ["LightingRecipe"],
        "allowedBindingRoles": ["light-snapshots", "material-table"],
        "usedBindingRoles": ["light-snapshots"],
        "validationRules": ["declared-slot-only"],
        "fallbackPolicy": "Degrade"
      }
    ]
  },
  "viewOutput": {
    "recipeId": "current-renderer.preview-output",
    "view": "Preview",
    "viewport": {"width": 640, "height": 360},
    "renderScale": 1.0,
    "target": "OffscreenTexture",
    "captureRequested": true,
    "readbackRequested": true,
    "mode": "Headless",
    "outputs": [
      {"name": "color", "kind": "Color", "format": "RGBA8_UNORM", "required": true},
      {"name": "readback", "kind": "ReadbackBuffer", "format": "Host-visible buffer", "required": false}
    ]
  },
  "bindingOverrides": [
    {
      "semanticName": "light-snapshots",
      "slot": "lighting",
      "sourceDomain": "Scene",
      "sourceIdentity": "RenderWorld.Lights.SandboxPreview",
      "sourceRevision": "sandbox-revision",
      "valueType": "Buffer",
      "valueFormat": "LightSnapshot",
      "fallbackPolicy": "Degrade"
    }
  ]
})json";
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
    [[nodiscard]] std::vector<glm::vec3> MakeRegistrationCloud()
    {
        std::vector<glm::vec3> points{};
        for (int i = 0; i < 5; ++i)
            for (int j = 0; j < 5; ++j)
                for (int k = 0; k < 3; ++k)
                    points.emplace_back(static_cast<float>(i) * 0.3f,
                                        static_cast<float>(j) * 0.4f,
                                        static_cast<float>(k) * 0.5f);
        return points;
    }

    [[nodiscard]] ECS::EntityHandle MakePointCloudEntity(
        ECS::Scene::Registry& registry,
        std::string name,
        const std::vector<glm::vec3>& positions)
    {
        const ECS::EntityHandle entity =
            MakeSelectable(registry, std::move(name));
        auto& vertices = registry.Raw().emplace<GS::Vertices>(entity);
        SetPositions(vertices, positions);
        registry.Raw().emplace<G::RenderPoints>(entity);
        return entity;
    }

    // An open grid-plane triangle mesh; its outer ring is an open boundary, so
    // texcoord-bearing boundary vertices are UV-seam vertices (UI-028).
    [[nodiscard]] Geometry::HalfedgeMesh::Mesh MakeGridPlaneMesh(const int n)
    {
        Geometry::HalfedgeMesh::Mesh mesh;
        std::vector<Geometry::VertexHandle> handles;
        handles.reserve(static_cast<std::size_t>((n + 1) * (n + 1)));
        for (int i = 0; i <= n; ++i)
            for (int j = 0; j <= n; ++j)
                handles.push_back(mesh.AddVertex(glm::vec3(
                    static_cast<float>(i), static_cast<float>(j), 0.0f)));
        const auto at = [&](const int i, const int j) {
            return handles[static_cast<std::size_t>(i * (n + 1) + j)];
        };
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j)
            {
                (void)mesh.AddTriangle(at(i, j), at(i + 1, j), at(i + 1, j + 1));
                (void)mesh.AddTriangle(at(i, j), at(i + 1, j + 1), at(i, j + 1));
            }
        return mesh;
    }

    // Per-vertex texcoords in the same (i, j) order MakeGridPlaneMesh adds
    // vertices, so they align 1:1 with the populated GeometrySources.
    [[nodiscard]] std::vector<glm::vec2> GridPlaneTexcoords(const int n)
    {
        std::vector<glm::vec2> tex;
        tex.reserve(static_cast<std::size_t>((n + 1) * (n + 1)));
        for (int i = 0; i <= n; ++i)
            for (int j = 0; j <= n; ++j)
                tex.emplace_back(static_cast<float>(i) / static_cast<float>(n),
                                 static_cast<float>(j) / static_cast<float>(n));
        return tex;
    }

    void ExpectKMeansVertexProperties(Geometry::PropertySet& properties,
                                      const std::size_t expectedCount,
                                      const bool pointCloudNames)
    {
        const std::string labelName =
            pointCloudNames ? "p:kmeans_label" : "v:kmeans_label";
        const std::string colorName =
            pointCloudNames ? "p:kmeans_color" : "v:kmeans_color";

        auto labels = properties.Get<std::uint32_t>(labelName);
        auto colors = properties.Get<glm::vec4>(colorName);
        ASSERT_TRUE(labels);
        ASSERT_TRUE(colors);
        ASSERT_EQ(labels.Vector().size(), expectedCount);
        ASSERT_EQ(colors.Vector().size(), expectedCount);
        for (std::size_t i = 0u; i < expectedCount; ++i)
        {
            EXPECT_LT(labels.Vector()[i], expectedCount);
            EXPECT_FLOAT_EQ(colors.Vector()[i].w, 1.0f);
        }

        if (pointCloudNames)
        {
            EXPECT_FALSE(properties.Get<float>("v:kmeans_label_f"));
        }
        else
        {
            auto labelFloats = properties.Get<float>("v:kmeans_label_f");
            ASSERT_TRUE(labelFloats);
            ASSERT_EQ(labelFloats.Vector().size(), expectedCount);
            for (std::size_t i = 0u; i < expectedCount; ++i)
            {
                EXPECT_FLOAT_EQ(labelFloats.Vector()[i],
                                static_cast<float>(labels.Vector()[i]));
            }
        }
    }

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

    [[nodiscard]] const Runtime::SandboxEditorTextureBakeSourceRow*
    FindTextureBakeSource(
        const Runtime::SandboxEditorTextureBakeControlsModel& model,
        const std::string& name)
    {
        for (const Runtime::SandboxEditorTextureBakeSourceRow& row :
             model.Sources)
        {
            if (row.Name == name)
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

    void AddDenoiseTetraMeshSource(ECS::Scene::Registry& registry,
                                   const ECS::EntityHandle entity)
    {
        Geometry::HalfedgeMesh::Mesh mesh =
            Geometry::HalfedgeMesh::MakeMeshTetrahedron();
        mesh.Position(Geometry::VertexHandle{0u}) +=
            glm::vec3{0.35f, -0.15f, 0.20f};
        GS::PopulateFromMesh(registry.Raw(), entity, mesh);
        registry.Raw().emplace_or_replace<G::RenderSurface>(entity);
    }

    void AddIcosahedronMeshSource(ECS::Scene::Registry& registry,
                                  const ECS::EntityHandle entity)
    {
        Geometry::HalfedgeMesh::Mesh mesh =
            Geometry::HalfedgeMesh::MakeMeshIcosahedron();
        GS::PopulateFromMesh(registry.Raw(), entity, mesh);
        registry.Raw().emplace_or_replace<G::RenderSurface>(entity);
    }

    struct MeshCounts
    {
        std::size_t Vertices{0u};
        std::size_t Faces{0u};
    };

    [[nodiscard]] MeshCounts SourceMeshCounts(
        ECS::Scene::Registry& registry,
        const ECS::EntityHandle entity)
    {
        const GS::ConstSourceView view =
            GS::BuildConstView(registry.Raw(), entity);
        return MeshCounts{
            .Vertices = view.VerticesAlive(),
            .Faces = view.FacesAlive(),
        };
    }

    void ExpectMeshCountsEqual(const MeshCounts actual,
                               const MeshCounts expected)
    {
        EXPECT_EQ(actual.Vertices, expected.Vertices);
        EXPECT_EQ(actual.Faces, expected.Faces);
    }

    [[nodiscard]] std::vector<glm::vec3> MeshVertexPositions(
        ECS::Scene::Registry& registry,
        const ECS::EntityHandle entity)
    {
        auto positions = registry.Raw()
                             .get<GS::Vertices>(entity)
                             .Properties.Get<glm::vec3>(PN::kPosition);
        if (!positions)
            return {};
        return positions.Vector();
    }

    void ExpectPositionsExactlyEqual(
        const std::vector<glm::vec3>& lhs,
        const std::vector<glm::vec3>& rhs)
    {
        ASSERT_EQ(lhs.size(), rhs.size());
        for (std::size_t i = 0u; i < lhs.size(); ++i)
        {
            EXPECT_FLOAT_EQ(lhs[i].x, rhs[i].x);
            EXPECT_FLOAT_EQ(lhs[i].y, rhs[i].y);
            EXPECT_FLOAT_EQ(lhs[i].z, rhs[i].z);
        }
    }

    [[nodiscard]] bool AnyPositionDiffers(
        const std::vector<glm::vec3>& lhs,
        const std::vector<glm::vec3>& rhs)
    {
        if (lhs.size() != rhs.size())
            return true;
        for (std::size_t i = 0u; i < lhs.size(); ++i)
        {
            if (lhs[i].x != rhs[i].x ||
                lhs[i].y != rhs[i].y ||
                lhs[i].z != rhs[i].z)
            {
                return true;
            }
        }
        return false;
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

    void AddPlanarCycleGraphSource(ECS::Scene::Registry& registry,
                                   const ECS::EntityHandle entity)
    {
        auto& raw = registry.Raw();
        auto& nodes = raw.emplace<GS::Nodes>(entity);
        SetNodePositions(nodes,
                         {
                             {0.0f, 0.0f, 0.0f},
                             {1.0f, 0.0f, 0.0f},
                             {1.0f, 1.0f, 0.0f},
                             {0.0f, 1.0f, 0.0f},
                         });
        auto& edges = raw.emplace<GS::Edges>(entity);
        SetEdges(edges, {0u, 1u, 2u, 3u}, {1u, 2u, 3u, 0u});
        raw.emplace<GS::HasGraphTopology>(entity);
        raw.emplace<G::RenderEdges>(entity);
        raw.emplace<G::RenderPoints>(entity);
    }

    [[nodiscard]] std::string ReadRepositoryTextFile(
        const std::filesystem::path& relativePath)
    {
        const std::filesystem::path path =
            std::filesystem::path{ENGINE_ROOT_DIR} / relativePath;
        std::ifstream file{path};
        if (!file)
            return {};
        return std::string{std::istreambuf_iterator<char>{file},
                           std::istreambuf_iterator<char>{}};
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

    void AttachDerivedJobCommands(
        Runtime::SandboxEditorContext& context,
        Runtime::DerivedJobRegistry& jobs)
    {
        context.DerivedJobCommands.Submit =
            [&jobs](Runtime::DerivedJobDesc desc)
            {
                return jobs.Submit(std::move(desc));
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

    class FakeWindow final : public Plat::IWindow
    {
    public:
        FakeWindow(const int width, const int height)
            : m_Extent{width, height}
            , m_Framebuffer{width, height}
        {
        }

        void PollEvents() override {}
        [[nodiscard]] bool ShouldClose() const override { return false; }
        [[nodiscard]] bool IsMinimized() const override { return false; }
        [[nodiscard]] bool WasResized() const override { return false; }
        void AcknowledgeResize() override {}
        [[nodiscard]] bool ConsumeInputActivity() override { return false; }
        [[nodiscard]] Plat::Extent2D GetWindowExtent() const override { return m_Extent; }
        [[nodiscard]] Plat::Extent2D GetFramebufferExtent() const override { return m_Framebuffer; }
        [[nodiscard]] void* GetNativeHandle() const override { return nullptr; }
        void Listen(EventCallbackFn) override {}
        [[nodiscard]] std::vector<Plat::Event> DrainEvents() override { return {}; }
        void OnUpdate() override {}
        void WaitForEventsTimeout(double) override {}
        void SetClipboardText(std::string_view text) override { m_Clipboard = std::string(text); }
        [[nodiscard]] std::string GetClipboardText() const override { return m_Clipboard; }
        void SetCursorMode(Plat::CursorMode mode) override { m_CursorMode = mode; }
        [[nodiscard]] Plat::CursorMode GetCursorMode() const override { return m_CursorMode; }

    private:
        Plat::Extent2D m_Extent{};
        Plat::Extent2D m_Framebuffer{};
        std::string m_Clipboard{};
        Plat::CursorMode m_CursorMode{Plat::CursorMode::Normal};
    };

    class OneFrameApplication final : public Runtime::IApplication
    {
    public:
        void OnInitialize(Runtime::Engine&) override {}
        void OnSimTick(Runtime::Engine&, double) override {}
        void OnVariableTick(Runtime::Engine& engine, double, double) override
        {
            engine.RequestExit();
        }
        void OnShutdown(Runtime::Engine&) override {}
    };

    class PassiveApplication final : public Runtime::IApplication
    {
    public:
        void OnInitialize(Runtime::Engine&) override {}
        void OnSimTick(Runtime::Engine&, double) override {}
        void OnVariableTick(Runtime::Engine&, double, double) override {}
        void OnShutdown(Runtime::Engine&) override {}
    };

    class WaitForAssetImportEventApplication final : public Runtime::IApplication
    {
    public:
        explicit WaitForAssetImportEventApplication(std::uint32_t maxFrames)
            : m_MaxFrames(maxFrames)
        {
        }

        void OnInitialize(Runtime::Engine&) override {}
        void OnSimTick(Runtime::Engine&, double) override {}
        void OnVariableTick(Runtime::Engine& engine, double, double) override
        {
            ++m_ObservedFrames;
            if (engine.GetAssetImportPipeline().GetLastAssetImportEvent().has_value() ||
                m_ObservedFrames >= m_MaxFrames)
            {
                engine.RequestExit();
            }
        }
        void OnShutdown(Runtime::Engine&) override {}

    private:
        std::uint32_t m_MaxFrames{1u};
        std::uint32_t m_ObservedFrames{0u};
    };

    class FixedFrameApplication final : public Runtime::IApplication
    {
    public:
        explicit FixedFrameApplication(std::uint32_t maxFrames)
            : m_MaxFrames(maxFrames)
        {
        }

        void OnInitialize(Runtime::Engine&) override {}
        void OnSimTick(Runtime::Engine&, double) override {}
        void OnVariableTick(Runtime::Engine& engine, double, double) override
        {
            ++m_ObservedFrames;
            if (m_ObservedFrames >= m_MaxFrames)
            {
                engine.RequestExit();
            }
        }
        void OnShutdown(Runtime::Engine&) override {}

    private:
        std::uint32_t m_MaxFrames{1u};
        std::uint32_t m_ObservedFrames{0u};
    };

    class WaitForConditionApplication final : public Runtime::IApplication
    {
    public:
        explicit WaitForConditionApplication(
            std::function<bool(Runtime::Engine&)> ready,
            std::uint32_t maxFrames = 512u)
            : m_Ready(std::move(ready))
            , m_MaxFrames(maxFrames)
        {
        }

        void OnInitialize(Runtime::Engine&) override {}
        void OnSimTick(Runtime::Engine&, double) override {}
        void OnVariableTick(Runtime::Engine& engine, double, double) override
        {
            ++m_ObservedFrames;
            if ((m_Ready && m_Ready(engine)) || m_ObservedFrames >= m_MaxFrames)
            {
                engine.RequestExit();
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        void OnShutdown(Runtime::Engine&) override {}

    private:
        std::function<bool(Runtime::Engine&)> m_Ready{};
        std::uint32_t m_MaxFrames{1u};
        std::uint32_t m_ObservedFrames{0u};
    };

    [[nodiscard]] Extrinsic::Core::Config::EngineConfig HeadlessConfig()
    {
        Extrinsic::Core::Config::EngineConfig config{};
        config.ReferenceScene.Enabled = false;
        config.Camera.Enabled = false;
        config.Window.Backend = Core::Config::WindowBackend::Null;
        return config;
    }

    [[nodiscard]] bool MeshHasVertexProperty(
        Runtime::Engine& engine,
        const ECS::EntityHandle entity,
        const std::string_view propertyName)
    {
        if (!engine.GetScene().IsValid(entity))
        {
            return false;
        }

        auto& raw = engine.GetScene().Raw();
        const GS::ConstSourceView view = GS::BuildConstView(raw, entity);
        return view.Valid() &&
            view.ActiveDomain == GS::Domain::Mesh &&
            view.VertexSource != nullptr &&
            view.VertexSource->Properties.Exists(propertyName);
    }

    [[nodiscard]] bool DirectMeshPostProcessReady(
        Runtime::Engine& engine,
        const ECS::EntityHandle entity)
    {
        return MeshHasVertexProperty(engine, entity, "v:texcoord") &&
            MeshHasVertexProperty(engine, entity, "v:normal") &&
            engine.GetObjectSpaceNormalBakeQueueDiagnosticsForTest()
                .NonOperationalNoOps > 0u;
    }

    void ExpectMeshVertexNormalsNear(
        Runtime::Engine& engine,
        const ECS::EntityHandle entity,
        const std::span<const glm::vec3> expected)
    {
        ASSERT_TRUE(engine.GetScene().IsValid(entity));
        auto& raw = engine.GetScene().Raw();
        const GS::ConstSourceView view = GS::BuildConstView(raw, entity);
        ASSERT_TRUE(view.Valid());
        ASSERT_EQ(view.ActiveDomain, GS::Domain::Mesh);
        ASSERT_NE(view.VertexSource, nullptr);

        const auto normals = view.VertexSource->Properties.Get<glm::vec3>(PN::kNormal);
        ASSERT_TRUE(normals);
        ASSERT_EQ(normals.Vector().size(), expected.size());
        for (std::size_t i = 0; i < expected.size(); ++i)
        {
            EXPECT_NEAR(normals.Vector()[i].x, expected[i].x, 1.0e-5f) << i;
            EXPECT_NEAR(normals.Vector()[i].y, expected[i].y, 1.0e-5f) << i;
            EXPECT_NEAR(normals.Vector()[i].z, expected[i].z, 1.0e-5f) << i;
        }
    }

    void ExpectFiniteUnitNormal(const glm::vec3 normal,
                                const float epsilon = 1.0e-4f)
    {
        EXPECT_TRUE(std::isfinite(normal.x));
        EXPECT_TRUE(std::isfinite(normal.y));
        EXPECT_TRUE(std::isfinite(normal.z));
        EXPECT_NEAR(glm::length(normal), 1.0f, epsilon);
    }

    struct TmpFile
    {
        std::filesystem::path Path;

        TmpFile(std::string_view name, std::string_view contents)
            : Path(std::filesystem::temp_directory_path() / std::string(name))
        {
            std::ofstream os(Path);
            os << contents;
        }

        ~TmpFile()
        {
            std::error_code ec;
            std::filesystem::remove(Path, ec);
        }
    };

    [[nodiscard]] std::optional<ECS::EntityHandle> FindFirstEntityWithDomain(
        ECS::Scene::Registry& registry,
        const GS::Domain domain)
    {
        auto& raw = registry.Raw();
        std::optional<ECS::EntityHandle> found{};
        raw.view<entt::entity>().each([&](const ECS::EntityHandle entity)
        {
            if (!raw.all_of<Sel::SelectableTag>(entity))
                return;
            const GS::ConstSourceView source = GS::BuildConstView(raw, entity);
            if (source.ActiveDomain == domain)
                found = entity;
        });
        return found;
    }

    [[nodiscard]] std::size_t CountEntitiesWithDomain(
        ECS::Scene::Registry& registry,
        const GS::Domain domain)
    {
        std::size_t count = 0u;
        auto& raw = registry.Raw();
        raw.view<entt::entity>().each([&](const ECS::EntityHandle entity)
        {
            if (!raw.all_of<Sel::SelectableTag>(entity))
                return;
            const GS::ConstSourceView source = GS::BuildConstView(raw, entity);
            if (source.ActiveDomain == domain)
                ++count;
        });
        return count;
    }

    [[nodiscard]] std::string_view SourceRange(
        const std::string& source,
        const std::string_view beginMarker,
        const std::string_view endMarker)
    {
        const std::size_t begin = source.find(beginMarker);
        if (begin == std::string::npos)
            return {};
        const std::size_t end = source.find(endMarker, begin + beginMarker.size());
        if (end == std::string::npos || end <= begin)
            return {};
        return std::string_view{source.data() + begin, end - begin};
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

TEST(SandboxEditorUi, DefaultDrawStartsWithOnlyMenuBarVisible)
{
    TestSupport::ImGuiFrameScope imguiFrame;
    const Runtime::SandboxEditorPanelFrame frame =
        Runtime::BuildSandboxEditorPanelFrame(Runtime::SandboxEditorContext{});

    Runtime::DrawSandboxEditorPanelFrame(frame);

    EXPECT_TRUE(ImGuiWindowExists("##MainMenuBar"));

    constexpr std::array<std::string_view, 34> kClosedByDefaultWindows{{
        "Sandbox Editor",
        "Scene Hierarchy",
        "Inspector",
        "Selection Details",
        "File / Scene",
        "File / Import",
        "Frame Graph",
        "Camera / Render",
        "Geometry Visualization",
        "ICP Registration",
        "PointCloud / Appearance",
        "PointCloud / Properties",
        "PointCloud / Selection",
        "PointCloud / Processing / K-Means",
        "PointCloud / Processing / Vertices / Normals",
        "PointCloud / Processing / Remove Outliers",
        "PointCloud / Processing / Progressive Poisson",
        "PointCloud / Processing / Consolidate",
        "Graph / Appearance",
        "Graph / Properties",
        "Graph / Selection",
        "Graph / Processing / K-Means",
        "Graph / Processing / Vertices / Normals",
        "Mesh / Appearance",
        "Mesh / Properties",
        "Mesh / Selection",
        "Mesh / Processing / K-Means",
        "Mesh / Processing / Denoise",
        "Mesh / Processing / Curvature",
        "Mesh / Processing / Remesh",
        "Mesh / Processing / Subdivide",
        "Mesh / Processing / Simplify",
        "Mesh / Processing / Vertices / Normals",
        "Mesh / Processing / Progressive Poisson",
    }};

    for (const std::string_view title : kClosedByDefaultWindows)
    {
        EXPECT_FALSE(ImGuiWindowExists(title)) << std::string(title);
    }
}

TEST(SandboxEditorUi, DomainMenusUseAppearanceAndFocusedProcessingWindows)
{
    const std::string editorSource =
        ReadRepositoryTextFile("src/runtime/Editor/Runtime.SandboxEditorUi.cpp");
    ASSERT_FALSE(editorSource.empty());

    const std::string_view appearanceWindow = SourceRange(
        editorSource,
        "void DrawDomainRenderWindow(",
        "void DrawDomainVisualizationControls(");
    ASSERT_FALSE(appearanceWindow.empty());
    EXPECT_NE(appearanceWindow.find("DrawDomainVisualizationControls(model, context);"),
              std::string_view::npos);
    EXPECT_NE(appearanceWindow.find("DrawPropertyBindingTargets"),
              std::string_view::npos);
    EXPECT_NE(appearanceWindow.find("DrawVertexChannelBindingTargets"),
              std::string_view::npos);
    EXPECT_NE(appearanceWindow.find("DrawTextureBakeControls"),
              std::string_view::npos);

    const std::string_view propertyWindow = SourceRange(
        editorSource,
        "void DrawDomainPropertyWindow(",
        "void DrawRenderHintStatus(");
    ASSERT_FALSE(propertyWindow.empty());
    EXPECT_NE(propertyWindow.find("DrawPropertyCatalogRows"),
              std::string_view::npos);
    EXPECT_EQ(propertyWindow.find("DrawDomainVisualizationControls"),
              std::string_view::npos);
    EXPECT_EQ(propertyWindow.find("DrawPropertyBindingTargets"),
              std::string_view::npos);
    EXPECT_EQ(propertyWindow.find("DrawVertexChannelBindingTargets"),
              std::string_view::npos);
    EXPECT_EQ(propertyWindow.find("DrawTextureBakeControls"),
              std::string_view::npos);
    EXPECT_EQ(propertyWindow.find("DrawRenderHintStatus"),
              std::string_view::npos);

    EXPECT_EQ(editorSource.find("DomainWindowSection::Visualization"),
              std::string::npos);
    EXPECT_NE(editorSource.find("DrawDomainVisualizationControls(model, context);"),
              std::string::npos);
    EXPECT_NE(editorSource.find("ProcessingKMeans"), std::string::npos);
    EXPECT_NE(editorSource.find("ProcessingDenoise"), std::string::npos);
    EXPECT_NE(editorSource.find("ProcessingCurvature"), std::string::npos);
    EXPECT_NE(editorSource.find("ProcessingRemesh"), std::string::npos);
    EXPECT_NE(editorSource.find("ProcessingSubdivide"), std::string::npos);
    EXPECT_NE(editorSource.find("ProcessingSimplify"), std::string::npos);
    EXPECT_NE(editorSource.find("ProcessingMeshVertexNormals"),
              std::string::npos);
    EXPECT_NE(editorSource.find("ProcessingGraphVertexNormals"),
              std::string::npos);
    EXPECT_NE(editorSource.find("ProcessingPointCloudVertexNormals"),
              std::string::npos);
    EXPECT_NE(editorSource.find("ProcessingPointCloudOutlierRemoval"),
              std::string::npos);
    EXPECT_NE(editorSource.find("ProcessingPointCloudConsolidation"),
              std::string::npos);
    EXPECT_NE(editorSource.find("ProcessingProgressivePoisson"),
              std::string::npos);
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

TEST(SandboxEditorUi, FileImportCommandRoutesThroughRuntimeOwnedSurface)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);

    bool commandObserved = false;
    Runtime::SandboxEditorFileImportCommand observedCommand{};
    context.AssetImportCommands =
        Runtime::SandboxEditorAssetImportCommandSurface{
            .Import =
                [&](const Runtime::SandboxEditorFileImportCommand& command)
                {
                    commandObserved = true;
                    observedCommand = command;
                    return Runtime::SandboxEditorFileImportResult{
                        .Status = Runtime::SandboxEditorCommandStatus::Applied,
                        .Asset = Assets::AssetId{7u, 2u},
                        .PayloadKind = Assets::AssetPayloadKind::ModelScene,
                        .PrimitiveEntitiesCreated = 1u,
                        .EmbeddedTextureAssetsCreated = 2u,
                        .TextureUploadRequests = 3u,
                        .MaterializedModelScene = true,
                        .Message = "Imported fake model.",
                    };
                },
        };
    context.PendingAssetImportPath = "assets/models/Duck.gltf";
    context.PendingAssetImportPayloadKind = Assets::AssetPayloadKind::Graph;

    Runtime::SandboxEditorPanelFrame frame =
        Runtime::BuildSandboxEditorPanelFrame(context);
    EXPECT_TRUE(frame.FileImport.Enabled);
    EXPECT_EQ(frame.FileImport.PendingPath, "assets/models/Duck.gltf");
    EXPECT_EQ(frame.FileImport.PayloadKind, Assets::AssetPayloadKind::Graph);
    EXPECT_FALSE(HasDiagnostic(
        frame.FileImport.Diagnostics,
        Runtime::SandboxEditorDiagnosticCode::AssetImportUnavailable));

    const Runtime::SandboxEditorFileImportResult result =
        Runtime::ApplySandboxEditorFileImportCommand(
            context,
            Runtime::SandboxEditorFileImportCommand{
                .Path = "assets/models/Duck.gltf",
                .PayloadKind = Assets::AssetPayloadKind::Unknown,
            });

    EXPECT_TRUE(commandObserved);
    EXPECT_EQ(observedCommand.Path, "assets/models/Duck.gltf");
    EXPECT_EQ(observedCommand.PayloadKind, Assets::AssetPayloadKind::Unknown);
    EXPECT_TRUE(result.Succeeded());
    EXPECT_EQ(result.Asset, (Assets::AssetId{7u, 2u}));
    EXPECT_EQ(result.PayloadKind, Assets::AssetPayloadKind::ModelScene);
    EXPECT_EQ(result.PrimitiveEntitiesCreated, 1u);
    EXPECT_EQ(result.EmbeddedTextureAssetsCreated, 2u);
    EXPECT_EQ(result.TextureUploadRequests, 3u);
    EXPECT_TRUE(result.MaterializedModelScene);
    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorCommandStatus(result.Status),
                 "Applied");

    context.LastAssetImportResult = &result;
    frame = Runtime::BuildSandboxEditorPanelFrame(context);
    ASSERT_TRUE(frame.FileImport.LastResult.has_value());
    EXPECT_EQ(frame.FileImport.StatusText, "Imported fake model.");
    EXPECT_FALSE(HasDiagnostic(
        frame.FileImport.Diagnostics,
        Runtime::SandboxEditorDiagnosticCode::AssetImportFailed));

    Runtime::SandboxEditorContext missingSurface =
        MakeContext(registry, selection);
    const Runtime::SandboxEditorFileImportResult missing =
        Runtime::ApplySandboxEditorFileImportCommand(
            missingSurface,
            Runtime::SandboxEditorFileImportCommand{
                .Path = "assets/models/Duck.gltf",
            });
    EXPECT_EQ(missing.Status,
              Runtime::SandboxEditorCommandStatus::MissingAssetImportCommands);
    EXPECT_EQ(missing.Error, Core::ErrorCode::InvalidState);
    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorCommandStatus(
                     missing.Status),
                 "MissingAssetImportCommands");

    const Runtime::SandboxEditorFileImportResult empty =
        Runtime::ApplySandboxEditorFileImportCommand(
            context,
            Runtime::SandboxEditorFileImportCommand{});
    EXPECT_EQ(empty.Status,
              Runtime::SandboxEditorCommandStatus::AssetImportFailed);
    EXPECT_EQ(empty.Error, Core::ErrorCode::InvalidPath);
    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorDiagnosticCode(
                     Runtime::SandboxEditorDiagnosticCode::AssetImportFailed),
                 "AssetImportFailed");

    context.LastAssetImportResult = &empty;
    frame = Runtime::BuildSandboxEditorPanelFrame(context);
    EXPECT_TRUE(HasDiagnostic(
        frame.FileImport.Diagnostics,
        Runtime::SandboxEditorDiagnosticCode::AssetImportFailed));
}

TEST(SandboxEditorUi, FileImportCommandTreatsAsyncPendingAsNonFailure)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);

    const Runtime::RuntimeAssetIngestHandle queuedHandle{42u, 7u};
    context.AssetImportCommands =
        Runtime::SandboxEditorAssetImportCommandSurface{
            .Import =
                [queuedHandle](const Runtime::SandboxEditorFileImportCommand&)
                {
                    return Runtime::SandboxEditorFileImportResult{
                        .Status = Runtime::SandboxEditorCommandStatus::Pending,
                        .Operation = queuedHandle,
                        .PayloadKind = Assets::AssetPayloadKind::Texture2D,
                    };
                },
        };

    const Runtime::SandboxEditorFileImportResult result =
        Runtime::ApplySandboxEditorFileImportCommand(
            context,
            Runtime::SandboxEditorFileImportCommand{
                .Path = "assets/textures/albedo.png",
                .PayloadKind = Assets::AssetPayloadKind::Unknown,
            });

    EXPECT_EQ(result.Status, Runtime::SandboxEditorCommandStatus::Pending);
    EXPECT_FALSE(result.Succeeded());
    EXPECT_EQ(result.Operation, queuedHandle);
    EXPECT_EQ(result.Error, Core::ErrorCode::Success);
    EXPECT_EQ(result.PayloadKind, Assets::AssetPayloadKind::Texture2D);
    EXPECT_NE(result.Message.find("Queued"), std::string::npos);

    context.LastAssetImportResult = &result;
    Runtime::SandboxEditorPanelFrame frame =
        Runtime::BuildSandboxEditorPanelFrame(context);
    ASSERT_TRUE(frame.FileImport.LastResult.has_value());
    EXPECT_EQ(frame.FileImport.LastResult->Status,
              Runtime::SandboxEditorCommandStatus::Pending);
    EXPECT_EQ(frame.FileImport.StatusText, result.Message);
    EXPECT_FALSE(HasDiagnostic(
        frame.FileImport.Diagnostics,
        Runtime::SandboxEditorDiagnosticCode::AssetImportFailed));
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

TEST(SandboxEditorUi, SceneFileCommandRoutesThroughRuntimeOwnedSurface)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);

    bool saveObserved = false;
    bool loadObserved = false;
    bool newObserved = false;
    bool closeObserved = false;
    Runtime::SandboxEditorSceneFileCommand observedSave{};
    Runtime::SandboxEditorSceneFileCommand observedLoad{};
    context.SceneFileCommands = Runtime::SandboxEditorSceneFileCommandSurface{
        .New =
            [&]()
            {
                newObserved = true;
                return Runtime::SandboxEditorSceneFileResult{
                    .Status = Runtime::SandboxEditorCommandStatus::Applied,
                    .Operation = Runtime::SandboxEditorSceneFileOperation::New,
                    .Message = "Created fake scene.",
                };
            },
        .Save =
            [&](const Runtime::SandboxEditorSceneFileCommand& command)
            {
                saveObserved = true;
                observedSave = command;
                return Runtime::SandboxEditorSceneFileResult{
                    .Status = Runtime::SandboxEditorCommandStatus::Applied,
                    .Operation = Runtime::SandboxEditorSceneFileOperation::Save,
                    .Stats = Runtime::SceneSerializationStats{
                        .Entities = 3u,
                        .MeshEntities = 1u,
                        .GraphEntities = 1u,
                        .PointCloudEntities = 1u,
                    },
                    .Message = "Saved fake scene.",
                };
            },
        .Load =
            [&](const Runtime::SandboxEditorSceneFileCommand& command)
            {
                loadObserved = true;
                observedLoad = command;
                return Runtime::SandboxEditorSceneFileResult{
                    .Status = Runtime::SandboxEditorCommandStatus::Applied,
                    .Operation = Runtime::SandboxEditorSceneFileOperation::Load,
                    .Stats = Runtime::SceneSerializationStats{.Entities = 2u},
                    .Message = "Loaded fake scene.",
                };
            },
        .Close =
            [&]()
            {
                closeObserved = true;
                return Runtime::SandboxEditorSceneFileResult{
                    .Status = Runtime::SandboxEditorCommandStatus::Applied,
                    .Operation = Runtime::SandboxEditorSceneFileOperation::Close,
                    .Message = "Closed fake scene.",
                };
            },
    };
    context.PendingSceneFilePath = "scene.extrinsic.json";

    Runtime::SandboxEditorPanelFrame frame =
        Runtime::BuildSandboxEditorPanelFrame(context);
    EXPECT_TRUE(frame.SceneFile.Enabled);
    EXPECT_TRUE(frame.SceneFile.LifecycleEnabled);
    EXPECT_TRUE(frame.SceneFile.CanNew);
    EXPECT_TRUE(frame.SceneFile.CanClose);
    EXPECT_TRUE(frame.SceneFile.CanSave);
    EXPECT_TRUE(frame.SceneFile.CanOpen);
    EXPECT_TRUE(frame.SceneFile.PathEntryEnabled);
    EXPECT_FALSE(frame.SceneFile.NativeDialogsAvailable);
    EXPECT_TRUE(frame.SceneFile.FileDialogBoundaryText.find("Native file dialogs") !=
                std::string::npos);
    EXPECT_EQ(frame.SceneFile.PendingPath, "scene.extrinsic.json");
    EXPECT_FALSE(HasDiagnostic(
        frame.SceneFile.Diagnostics,
        Runtime::SandboxEditorDiagnosticCode::SceneFileUnavailable));

    const Runtime::SandboxEditorSceneFileResult save =
        Runtime::ApplySandboxEditorSceneSaveCommand(
            context,
            Runtime::SandboxEditorSceneFileCommand{
                .Path = "scene.extrinsic.json",
            });
    EXPECT_TRUE(saveObserved);
    EXPECT_EQ(observedSave.Path, "scene.extrinsic.json");
    EXPECT_TRUE(save.Succeeded());
    EXPECT_EQ(save.Stats.Entities, 3u);
    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorCommandStatus(save.Status),
                 "Applied");

    const Runtime::SandboxEditorSceneFileResult created =
        Runtime::ApplySandboxEditorNewSceneCommand(context);
    EXPECT_TRUE(newObserved);
    EXPECT_TRUE(created.Succeeded());
    EXPECT_EQ(created.Operation, Runtime::SandboxEditorSceneFileOperation::New);
    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorCommandStatus(
                     Runtime::SandboxEditorCommandStatus::SceneNewFailed),
                 "SceneNewFailed");

    const Runtime::SandboxEditorSceneFileResult load =
        Runtime::ApplySandboxEditorSceneLoadCommand(
            context,
            Runtime::SandboxEditorSceneFileCommand{
                .Path = "scene.extrinsic.json",
            });
    EXPECT_TRUE(loadObserved);
    EXPECT_EQ(observedLoad.Path, "scene.extrinsic.json");
    EXPECT_TRUE(load.Succeeded());
    EXPECT_EQ(load.Stats.Entities, 2u);

    const Runtime::SandboxEditorSceneFileResult closed =
        Runtime::ApplySandboxEditorCloseSceneCommand(context);
    EXPECT_TRUE(closeObserved);
    EXPECT_TRUE(closed.Succeeded());
    EXPECT_EQ(closed.Operation, Runtime::SandboxEditorSceneFileOperation::Close);
    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorCommandStatus(
                     Runtime::SandboxEditorCommandStatus::SceneCloseFailed),
                 "SceneCloseFailed");

    context.LastSceneFileResult = &load;
    frame = Runtime::BuildSandboxEditorPanelFrame(context);
    ASSERT_TRUE(frame.SceneFile.LastResult.has_value());
    EXPECT_EQ(frame.SceneFile.StatusText, "Loaded fake scene.");
    EXPECT_FALSE(HasDiagnostic(
        frame.SceneFile.Diagnostics,
        Runtime::SandboxEditorDiagnosticCode::SceneFileFailed));

    Runtime::SandboxEditorContext missingSurface =
        MakeContext(registry, selection);
    const Runtime::SandboxEditorSceneFileResult missing =
        Runtime::ApplySandboxEditorSceneSaveCommand(
            missingSurface,
            Runtime::SandboxEditorSceneFileCommand{
                .Path = "scene.extrinsic.json",
            });
    EXPECT_EQ(missing.Status,
              Runtime::SandboxEditorCommandStatus::MissingSceneFileCommands);
    EXPECT_EQ(missing.Error, Core::ErrorCode::InvalidState);
    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorCommandStatus(
                     missing.Status),
                 "MissingSceneFileCommands");

    const Runtime::SandboxEditorSceneFileResult emptySave =
        Runtime::ApplySandboxEditorSceneSaveCommand(
            context,
            Runtime::SandboxEditorSceneFileCommand{});
    EXPECT_EQ(emptySave.Status,
              Runtime::SandboxEditorCommandStatus::SceneSaveFailed);
    EXPECT_EQ(emptySave.Error, Core::ErrorCode::InvalidPath);
    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorDiagnosticCode(
                     Runtime::SandboxEditorDiagnosticCode::SceneFileFailed),
                 "SceneFileFailed");

    context.LastSceneFileResult = &emptySave;
    frame = Runtime::BuildSandboxEditorPanelFrame(context);
    EXPECT_TRUE(HasDiagnostic(
        frame.SceneFile.Diagnostics,
        Runtime::SandboxEditorDiagnosticCode::SceneFileFailed));
}

TEST(SandboxEditorUi, SceneLoadCommandTreatsAsyncPendingAsNonFailure)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);

    const Runtime::StreamingTaskHandle queuedTask{42u, 7u};
    context.SceneFileCommands = Runtime::SandboxEditorSceneFileCommandSurface{
        .Save =
            [](const Runtime::SandboxEditorSceneFileCommand&)
            {
                return Runtime::SandboxEditorSceneFileResult{
                    .Status = Runtime::SandboxEditorCommandStatus::Applied,
                    .Operation = Runtime::SandboxEditorSceneFileOperation::Save,
                };
            },
        .Load =
            [queuedTask](const Runtime::SandboxEditorSceneFileCommand&)
            {
                return Runtime::SandboxEditorSceneFileResult{
                    .Status = Runtime::SandboxEditorCommandStatus::Pending,
                    .Operation = Runtime::SandboxEditorSceneFileOperation::Load,
                    .Task = queuedTask,
                };
            },
    };

    const Runtime::SandboxEditorSceneFileResult result =
        Runtime::ApplySandboxEditorSceneLoadCommand(
            context,
            Runtime::SandboxEditorSceneFileCommand{
                .Path = "scene.extrinsic.json",
            });

    EXPECT_EQ(result.Status, Runtime::SandboxEditorCommandStatus::Pending);
    EXPECT_FALSE(result.Succeeded());
    EXPECT_EQ(result.Operation, Runtime::SandboxEditorSceneFileOperation::Load);
    EXPECT_EQ(result.Task, queuedTask);
    EXPECT_EQ(result.Error, Core::ErrorCode::Success);
    EXPECT_NE(result.Message.find("Queued scene open"), std::string::npos);

    context.LastSceneFileResult = &result;
    Runtime::SandboxEditorPanelFrame frame =
        Runtime::BuildSandboxEditorPanelFrame(context);
    ASSERT_TRUE(frame.SceneFile.LastResult.has_value());
    EXPECT_EQ(frame.SceneFile.LastResult->Status,
              Runtime::SandboxEditorCommandStatus::Pending);
    EXPECT_EQ(frame.SceneFile.StatusText, result.Message);
    EXPECT_FALSE(HasDiagnostic(
        frame.SceneFile.Diagnostics,
        Runtime::SandboxEditorDiagnosticCode::SceneFileFailed));
}

TEST(SandboxEditorUi, SceneSaveCommandTreatsAsyncPendingAsNonFailure)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);

    const Runtime::StreamingTaskHandle queuedTask{43u, 7u};
    context.SceneFileCommands = Runtime::SandboxEditorSceneFileCommandSurface{
        .Save =
            [queuedTask](const Runtime::SandboxEditorSceneFileCommand&)
            {
                return Runtime::SandboxEditorSceneFileResult{
                    .Status = Runtime::SandboxEditorCommandStatus::Pending,
                    .Operation = Runtime::SandboxEditorSceneFileOperation::Save,
                    .Task = queuedTask,
                };
            },
        .Load =
            [](const Runtime::SandboxEditorSceneFileCommand&)
            {
                return Runtime::SandboxEditorSceneFileResult{
                    .Status = Runtime::SandboxEditorCommandStatus::Applied,
                    .Operation = Runtime::SandboxEditorSceneFileOperation::Load,
                };
            },
    };

    const Runtime::SandboxEditorSceneFileResult result =
        Runtime::ApplySandboxEditorSceneSaveCommand(
            context,
            Runtime::SandboxEditorSceneFileCommand{
                .Path = "scene.extrinsic.json",
            });

    EXPECT_EQ(result.Status, Runtime::SandboxEditorCommandStatus::Pending);
    EXPECT_FALSE(result.Succeeded());
    EXPECT_EQ(result.Operation, Runtime::SandboxEditorSceneFileOperation::Save);
    EXPECT_EQ(result.Task, queuedTask);
    EXPECT_EQ(result.Error, Core::ErrorCode::Success);
    EXPECT_NE(result.Message.find("Queued scene save"), std::string::npos);

    context.LastSceneFileResult = &result;
    Runtime::SandboxEditorPanelFrame frame =
        Runtime::BuildSandboxEditorPanelFrame(context);
    ASSERT_TRUE(frame.SceneFile.LastResult.has_value());
    EXPECT_EQ(frame.SceneFile.LastResult->Status,
              Runtime::SandboxEditorCommandStatus::Pending);
    EXPECT_EQ(frame.SceneFile.StatusText, result.Message);
    EXPECT_FALSE(HasDiagnostic(
        frame.SceneFile.Diagnostics,
        Runtime::SandboxEditorDiagnosticCode::SceneFileFailed));
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

TEST(SandboxEditorUi, ExtrinsicSandboxAppStaysRuntimeOnly)
{
    const std::string sandboxModule =
        ReadRepositoryTextFile("src/app/Sandbox/Sandbox.cppm");
    const std::string sandboxMain =
        ReadRepositoryTextFile("src/app/Sandbox/main.cpp");
    const std::string sandboxCMake =
        ReadRepositoryTextFile("src/app/Sandbox/CMakeLists.txt");

    ASSERT_FALSE(sandboxModule.empty());
    ASSERT_FALSE(sandboxMain.empty());
    ASSERT_FALSE(sandboxCMake.empty());

    EXPECT_NE(sandboxModule.find("import Extrinsic.Runtime.Engine;"),
              std::string::npos);
    EXPECT_NE(sandboxModule.find("import Extrinsic.Runtime.SandboxEditorUi;"),
              std::string::npos);
    EXPECT_NE(sandboxMain.find("import Extrinsic.Runtime.Engine;"),
              std::string::npos);
    EXPECT_NE(sandboxMain.find("import Extrinsic.Sandbox;"),
              std::string::npos);

    for (const char* forbidden :
         {
             "import Extrinsic.Asset",
             "import Extrinsic.Core",
             "import Extrinsic.ECS",
             "import Extrinsic.Graphics",
             "import Extrinsic.Platform",
             "import Extrinsic.RHI",
             "import Extrinsic.Backends",
         })
    {
        EXPECT_EQ(sandboxModule.find(forbidden), std::string::npos)
            << forbidden;
        EXPECT_EQ(sandboxMain.find(forbidden), std::string::npos)
            << forbidden;
    }

    EXPECT_NE(sandboxCMake.find("target_link_libraries(ExtrinsicSandbox"),
              std::string::npos);
    EXPECT_NE(sandboxCMake.find("ExtrinsicRuntime"), std::string::npos);
    EXPECT_EQ(sandboxCMake.find("ExtrinsicGraphics"), std::string::npos);
    EXPECT_EQ(sandboxCMake.find("ExtrinsicPlatform"), std::string::npos);
    EXPECT_EQ(sandboxCMake.find("ExtrinsicRHI"), std::string::npos);
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

    const Domain consolidation =
        Runtime::GetSandboxEditorSupportedGeometryProcessingDomains(
            Algorithm::PointCloudConsolidation);
    EXPECT_TRUE(Runtime::HasAnySandboxEditorGeometryProcessingDomain(
        consolidation,
        Domain::PointCloudPoints));
    EXPECT_FALSE(Runtime::HasAnySandboxEditorGeometryProcessingDomain(
        consolidation,
        Domain::MeshVertices));
    EXPECT_FALSE(Runtime::HasAnySandboxEditorGeometryProcessingDomain(
        consolidation,
        Domain::GraphVertices));
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
    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorGeometryProcessingAlgorithm(
                     Algorithm::PointCloudConsolidation),
                 "Point Cloud Consolidation");
}

TEST(SandboxEditorUi, GeometryProcessingMenusExposeDomainElementSubmenus)
{
    using Domain = Runtime::SandboxEditorGeometryProcessingDomain;

    const std::vector<Runtime::SandboxEditorGeometryProcessingMenuItem> mesh =
        Runtime::GetSandboxEditorGeometryProcessingMenuItems(
            Runtime::SandboxEditorDomainWindowKind::Mesh);
    ASSERT_EQ(mesh.size(), 3u);
    EXPECT_EQ(mesh[0].Domain, Domain::MeshVertices);
    EXPECT_STREQ(mesh[0].Label, "Vertices");
    EXPECT_TRUE(mesh[0].HasNormalsMethod);
    EXPECT_TRUE(mesh[0].HasDenoiseMethod);
    EXPECT_TRUE(mesh[0].HasCurvatureMethod);
    EXPECT_TRUE(mesh[0].HasRemeshMethod);
    EXPECT_TRUE(mesh[0].HasSubdivideMethod);
    EXPECT_TRUE(mesh[0].HasSimplifyMethod);
    EXPECT_EQ(mesh[1].Domain, Domain::MeshEdges);
    EXPECT_STREQ(mesh[1].Label, "Edges");
    EXPECT_FALSE(mesh[1].HasNormalsMethod);
    EXPECT_FALSE(mesh[1].HasDenoiseMethod);
    EXPECT_FALSE(mesh[1].HasCurvatureMethod);
    EXPECT_FALSE(mesh[1].HasRemeshMethod);
    EXPECT_FALSE(mesh[1].HasSubdivideMethod);
    EXPECT_FALSE(mesh[1].HasSimplifyMethod);
    EXPECT_EQ(mesh[2].Domain, Domain::MeshFaces);
    EXPECT_STREQ(mesh[2].Label, "Faces");
    EXPECT_FALSE(mesh[2].HasNormalsMethod);
    EXPECT_FALSE(mesh[2].HasDenoiseMethod);
    EXPECT_FALSE(mesh[2].HasCurvatureMethod);
    EXPECT_FALSE(mesh[2].HasRemeshMethod);
    EXPECT_FALSE(mesh[2].HasSubdivideMethod);
    EXPECT_FALSE(mesh[2].HasSimplifyMethod);

    const std::vector<Runtime::SandboxEditorGeometryProcessingMenuItem> graph =
        Runtime::GetSandboxEditorGeometryProcessingMenuItems(
            Runtime::SandboxEditorDomainWindowKind::Graph);
    ASSERT_EQ(graph.size(), 3u);
    EXPECT_EQ(graph[0].Domain, Domain::GraphVertices);
    EXPECT_STREQ(graph[0].Label, "Vertices");
    EXPECT_TRUE(graph[0].HasNormalsMethod);
    EXPECT_FALSE(graph[0].HasDenoiseMethod);
    EXPECT_FALSE(graph[0].HasCurvatureMethod);
    EXPECT_FALSE(graph[0].HasRemeshMethod);
    EXPECT_FALSE(graph[0].HasSubdivideMethod);
    EXPECT_EQ(graph[1].Domain, Domain::GraphEdges);
    EXPECT_STREQ(graph[1].Label, "Edges");
    EXPECT_FALSE(graph[1].HasNormalsMethod);
    EXPECT_FALSE(graph[1].HasDenoiseMethod);
    EXPECT_FALSE(graph[1].HasCurvatureMethod);
    EXPECT_FALSE(graph[1].HasRemeshMethod);
    EXPECT_FALSE(graph[1].HasSubdivideMethod);
    EXPECT_EQ(graph[2].Domain, Domain::GraphHalfedges);
    EXPECT_STREQ(graph[2].Label, "Halfedges");
    EXPECT_FALSE(graph[2].HasNormalsMethod);
    EXPECT_FALSE(graph[2].HasDenoiseMethod);
    EXPECT_FALSE(graph[2].HasCurvatureMethod);
    EXPECT_FALSE(graph[2].HasRemeshMethod);
    EXPECT_FALSE(graph[2].HasSubdivideMethod);

    const std::vector<Runtime::SandboxEditorGeometryProcessingMenuItem> cloud =
        Runtime::GetSandboxEditorGeometryProcessingMenuItems(
            Runtime::SandboxEditorDomainWindowKind::PointCloud);
    ASSERT_EQ(cloud.size(), 1u);
    EXPECT_EQ(cloud[0].Domain, Domain::PointCloudPoints);
    EXPECT_STREQ(cloud[0].Label, "Vertices");
    EXPECT_TRUE(cloud[0].HasNormalsMethod);
    EXPECT_FALSE(cloud[0].HasDenoiseMethod);
    EXPECT_FALSE(cloud[0].HasCurvatureMethod);
    EXPECT_FALSE(cloud[0].HasRemeshMethod);
    EXPECT_FALSE(cloud[0].HasSubdivideMethod);
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
    ASSERT_EQ(cloudEntries.size(), 12u);
    EXPECT_EQ(cloudEntries[0].Algorithm, Algorithm::KMeans);
    EXPECT_EQ(cloudEntries[1].Algorithm, Algorithm::NormalEstimation);
    EXPECT_EQ(cloudEntries[2].Algorithm, Algorithm::Registration);
    EXPECT_EQ(cloudEntries[6].Algorithm, Algorithm::ProgressivePoissonSampling);
    EXPECT_EQ(cloudEntries[10].Algorithm, Algorithm::SurfaceReconstruction);
    EXPECT_EQ(cloudEntries[11].Algorithm, Algorithm::PointCloudConsolidation);
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

TEST(SandboxEditorUi, KMeansCommandPublishesMeshGraphAndPointCloudProperties)
{
    using Domain = Runtime::SandboxEditorGeometryProcessingDomain;

    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);

    const ECS::EntityHandle mesh = MakeSelectable(registry, "Mesh");
    AddTriangleMeshSource(registry, mesh);
    const Runtime::SandboxEditorKMeansResult meshResult =
        Runtime::ApplySandboxEditorKMeansCommand(
            context,
            Runtime::SandboxEditorKMeansCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(mesh),
                .Domain = Domain::MeshVertices,
                .ClusterCount = 2u,
                .MaxIterations = 8u,
                .Seed = 7u,
            });
    ASSERT_TRUE(meshResult.Succeeded());
    EXPECT_EQ(meshResult.Domain, Domain::MeshVertices);
    EXPECT_EQ(meshResult.LabelCount, 3u);
    EXPECT_EQ(meshResult.ClusterCount, 2u);
    EXPECT_EQ(meshResult.RequestedBackend,
              Runtime::SandboxEditorKMeansBackend::CpuReference);
    EXPECT_EQ(meshResult.ActualBackend,
              Runtime::SandboxEditorKMeansBackend::CpuReference);
    EXPECT_EQ(meshResult.RequestedBackendId, "cpu_reference");
    EXPECT_EQ(meshResult.BackendId, "cpu_reference");
    EXPECT_FALSE(meshResult.FellBackToCpu);
    EXPECT_TRUE(meshResult.BackendFallbackReason.empty());
    EXPECT_NE(meshResult.Message.find("requested cpu_reference"),
              std::string::npos);
    EXPECT_EQ(meshResult.Error, Core::ErrorCode::Success);
    ExpectKMeansVertexProperties(
        registry.Raw().get<GS::Vertices>(mesh).Properties,
        3u,
        false);
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));

    const ECS::EntityHandle graph = MakeSelectable(registry, "Graph");
    AddGraphSource(registry, graph);
    const Runtime::SandboxEditorKMeansResult graphResult =
        Runtime::ApplySandboxEditorKMeansCommand(
            context,
            Runtime::SandboxEditorKMeansCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(graph),
                .Domain = Domain::GraphVertices,
                .ClusterCount = 2u,
                .MaxIterations = 8u,
                .Seed = 11u,
                .UseHierarchicalInitialization = false,
            });
    ASSERT_TRUE(graphResult.Succeeded());
    EXPECT_EQ(graphResult.Domain, Domain::GraphVertices);
    EXPECT_EQ(graphResult.LabelCount, 3u);
    ExpectKMeansVertexProperties(
        registry.Raw().get<GS::Nodes>(graph).Properties,
        3u,
        false);
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(graph));

    const ECS::EntityHandle cloud = MakeSelectable(registry, "Cloud");
    AddPointCloudSource(registry, cloud, 4u);
    SetPositions(registry.Raw().get<GS::Vertices>(cloud),
                 {
                     {0.0f, 0.0f, 0.0f},
                     {0.1f, 0.0f, 0.0f},
                     {2.0f, 0.0f, 0.0f},
                     {2.1f, 0.0f, 0.0f},
                 });
    const Runtime::SandboxEditorKMeansResult cloudResult =
        Runtime::ApplySandboxEditorKMeansCommand(
            context,
            Runtime::SandboxEditorKMeansCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(cloud),
                .Domain = Domain::PointCloudPoints,
                .ClusterCount = 2u,
                .MaxIterations = 8u,
                .Seed = 13u,
            });
    ASSERT_TRUE(cloudResult.Succeeded());
    EXPECT_EQ(cloudResult.Domain, Domain::PointCloudPoints);
    EXPECT_EQ(cloudResult.LabelCount, 4u);
    ExpectKMeansVertexProperties(
        registry.Raw().get<GS::Vertices>(cloud).Properties,
        4u,
        true);
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(cloud));

    ASSERT_TRUE(selection.SetSelectedEntity(registry, cloud));
    context.LastKMeansResult = &cloudResult;
    const Runtime::SandboxEditorDomainWindowModel model =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::PointCloud);
    ASSERT_TRUE(model.Processing.LastKMeansResult.has_value());
    EXPECT_TRUE(model.Processing.LastKMeansResult->Succeeded());
    EXPECT_EQ(model.Processing.LastKMeansResult->LabelCount, 4u);
}

TEST(SandboxEditorUi, KMeansVulkanRequestFallsBackToCpuReference)
{
    using Domain = Runtime::SandboxEditorGeometryProcessingDomain;

    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Tests::MockDevice device;
    device.Operational = false;
    Runtime::SandboxEditorContext context =
        MakeContext(registry, selection, true, nullptr, &device);

    const ECS::EntityHandle cloud = MakeSelectable(registry, "Cloud");
    AddPointCloudSource(registry, cloud, 4u);
    SetPositions(registry.Raw().get<GS::Vertices>(cloud),
                 {
                     {0.0f, 0.0f, 0.0f},
                     {0.1f, 0.0f, 0.0f},
                     {2.0f, 0.0f, 0.0f},
                     {2.1f, 0.0f, 0.0f},
                 });

    const Runtime::SandboxEditorKMeansResult result =
        Runtime::ApplySandboxEditorKMeansCommand(
            context,
            Runtime::SandboxEditorKMeansCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(cloud),
                .Domain = Domain::PointCloudPoints,
                .ClusterCount = 2u,
                .MaxIterations = 8u,
                .Seed = 13u,
                .Backend =
                    Runtime::SandboxEditorKMeansBackend::VulkanCompute,
            });

    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_EQ(result.RequestedBackend,
              Runtime::SandboxEditorKMeansBackend::VulkanCompute);
    EXPECT_EQ(result.ActualBackend,
              Runtime::SandboxEditorKMeansBackend::CpuReference);
    EXPECT_EQ(result.RequestedBackendId, "gpu_vulkan_compute");
    EXPECT_EQ(result.BackendId, "cpu_reference");
    EXPECT_TRUE(result.FellBackToCpu);
    EXPECT_NE(result.BackendFallbackReason.find("not operational"),
              std::string::npos);
    EXPECT_NE(result.Message.find("requested gpu_vulkan_compute"),
              std::string::npos);
    EXPECT_NE(result.Message.find("actual cpu_reference"), std::string::npos);
    ExpectKMeansVertexProperties(
        registry.Raw().get<GS::Vertices>(cloud).Properties,
        4u,
        true);
}

TEST(SandboxEditorUi, KMeansVulkanRequestQueuesGpuJobWhenSurfaceAccepts)
{
    using Domain = Runtime::SandboxEditorGeometryProcessingDomain;

    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);

    const ECS::EntityHandle cloud = MakeSelectable(registry, "Cloud");
    AddPointCloudSource(registry, cloud, 4u);
    SetPositions(registry.Raw().get<GS::Vertices>(cloud),
                 {
                     {0.0f, 0.0f, 0.0f},
                     {0.1f, 0.0f, 0.0f},
                     {2.0f, 0.0f, 0.0f},
                     {2.1f, 0.0f, 0.0f},
                 });

    std::optional<Runtime::RuntimeKMeansGpuJobRequest> submitted{};
    context.KMeansGpuCommands.Submit =
        [&submitted](Runtime::RuntimeKMeansGpuJobRequest request)
        {
            request.Sequence = 77u;
            submitted = request;
            return Runtime::RuntimeKMeansGpuJobSubmission{
                .Status = Runtime::RuntimeKMeansGpuJobStatus::Accepted,
                .Sequence = request.Sequence,
            };
        };
    context.KMeansGpuCommands.ConsumeCompleted =
        []() -> std::optional<Runtime::RuntimeKMeansGpuJobResult>
        {
            return std::nullopt;
        };

    const Runtime::SandboxEditorKMeansResult result =
        Runtime::ApplySandboxEditorKMeansCommand(
            context,
            Runtime::SandboxEditorKMeansCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(cloud),
                .Domain = Domain::PointCloudPoints,
                .ClusterCount = 2u,
                .MaxIterations = 8u,
                .Seed = 13u,
                .Backend =
                    Runtime::SandboxEditorKMeansBackend::VulkanCompute,
            });

    ASSERT_TRUE(submitted.has_value());
    EXPECT_EQ(submitted->StableEntityId,
              Runtime::SelectionController::ToStableEntityId(cloud));
    EXPECT_EQ(submitted->DomainTag,
              static_cast<std::uint32_t>(Domain::PointCloudPoints));
    EXPECT_EQ(submitted->Points.size(), 4u);
    EXPECT_EQ(submitted->Params.Compute, Geometry::KMeans::Backend::GPU);
    EXPECT_EQ(submitted->Params.ClusterCount, 2u);
    EXPECT_EQ(submitted->Params.MaxIterations, 8u);

    EXPECT_EQ(result.Status, Runtime::SandboxEditorCommandStatus::Pending);
    EXPECT_EQ(result.RequestedBackend,
              Runtime::SandboxEditorKMeansBackend::VulkanCompute);
    EXPECT_EQ(result.ActualBackend,
              Runtime::SandboxEditorKMeansBackend::VulkanCompute);
    EXPECT_EQ(result.RequestedBackendId, "gpu_vulkan_compute");
    EXPECT_EQ(result.BackendId, "gpu_vulkan_compute");
    EXPECT_FALSE(result.FellBackToCpu);
    EXPECT_TRUE(result.BackendFallbackReason.empty());
    EXPECT_NE(result.Message.find("queued"), std::string::npos);
    EXPECT_FALSE(registry.Raw()
                     .get<GS::Vertices>(cloud)
                     .Properties.Get<std::uint32_t>("p:kmeans_label"));

    context.LastKMeansResult = &result;
    ASSERT_TRUE(selection.SetSelectedEntity(registry, cloud));
    const Runtime::SandboxEditorDomainWindowModel model =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::PointCloud);
    ASSERT_TRUE(model.Processing.LastKMeansResult.has_value());
    EXPECT_EQ(model.Processing.LastKMeansResult->Status,
              Runtime::SandboxEditorCommandStatus::Pending);
    EXPECT_FALSE(HasDiagnostic(
        model.Processing.Diagnostics,
        Runtime::SandboxEditorDiagnosticCode::GeometryProcessingFailed));
}

TEST(SandboxEditorUi, KMeansCpuRequestQueuesDerivedJobAndPublishesOnApply)
{
    using Domain = Runtime::SandboxEditorGeometryProcessingDomain;

    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    Runtime::StreamingExecutor executor{};
    Runtime::DerivedJobRegistry jobs{executor};
    context.DerivedJobCommands.Submit =
        [&jobs](Runtime::DerivedJobDesc desc)
        {
            return jobs.Submit(std::move(desc));
        };
    std::optional<Runtime::SandboxEditorKMeansResult> completedResult{};
    context.MethodResultSinks.KMeans =
        [&completedResult](Runtime::SandboxEditorKMeansResult result)
        {
            completedResult = std::move(result);
        };
    context.DerivedJobs = nullptr;

    const ECS::EntityHandle cloud = MakeSelectable(registry, "Cloud");
    AddPointCloudSource(registry, cloud, 4u);
    SetPositions(registry.Raw().get<GS::Vertices>(cloud),
                 {
                     {0.0f, 0.0f, 0.0f},
                     {0.1f, 0.0f, 0.0f},
                     {2.0f, 0.0f, 0.0f},
                     {2.1f, 0.0f, 0.0f},
                 });

    const Runtime::SandboxEditorKMeansResult result =
        Runtime::ApplySandboxEditorKMeansCommand(
            context,
            Runtime::SandboxEditorKMeansCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(cloud),
                .Domain = Domain::PointCloudPoints,
                .ClusterCount = 2u,
                .MaxIterations = 8u,
                .Seed = 13u,
                .Backend =
                    Runtime::SandboxEditorKMeansBackend::CpuReference,
            });

    EXPECT_EQ(result.Status, Runtime::SandboxEditorCommandStatus::Pending);
    EXPECT_NE(result.Message.find("queued"), std::string::npos);
    EXPECT_FALSE(registry.Raw()
                     .get<GS::Vertices>(cloud)
                     .Properties.Get<std::uint32_t>("p:kmeans_label"));

    Runtime::DerivedJobQueueSnapshot queued = jobs.SnapshotAll();
    ASSERT_EQ(queued.Entries.size(), 1u);
    EXPECT_EQ(queued.Entries[0].Name, "Sandbox.KMeans.CPU");
    EXPECT_EQ(queued.Entries[0].Status, Runtime::DerivedJobStatus::Queued);

    jobs.Pump(1u);
    jobs.DrainCompletions();
    EXPECT_FALSE(registry.Raw()
                     .get<GS::Vertices>(cloud)
                     .Properties.Get<std::uint32_t>("p:kmeans_label"));

    Runtime::DerivedJobQueueSnapshot ready = jobs.SnapshotAll();
    ASSERT_EQ(ready.Entries.size(), 1u);
    EXPECT_EQ(ready.Entries[0].Status, Runtime::DerivedJobStatus::Applying);

    EXPECT_EQ(jobs.ApplyMainThreadResults(1u), 1u);
    Runtime::DerivedJobQueueSnapshot done = jobs.SnapshotAll();
    ASSERT_EQ(done.Entries.size(), 1u);
    EXPECT_EQ(done.Entries[0].Status, Runtime::DerivedJobStatus::Complete);
    ASSERT_TRUE(completedResult.has_value());
    EXPECT_TRUE(completedResult->Succeeded()) << completedResult->Message;
    EXPECT_EQ(completedResult->LabelCount, 4u);
    ExpectKMeansVertexProperties(
        registry.Raw().get<GS::Vertices>(cloud).Properties,
        4u,
        true);
}

TEST(SandboxEditorUi, KMeansCpuDuplicateSubmitUsesExistingActiveJob)
{
    using Domain = Runtime::SandboxEditorGeometryProcessingDomain;

    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    Runtime::StreamingExecutor executor{};
    Runtime::DerivedJobRegistry jobs{executor};
    AttachDerivedJobCommands(context, jobs);

    const ECS::EntityHandle cloud = MakeSelectable(registry, "Cloud");
    AddPointCloudSource(registry, cloud, 4u);
    SetPositions(registry.Raw().get<GS::Vertices>(cloud),
                 {
                     {0.0f, 0.0f, 0.0f},
                     {0.1f, 0.0f, 0.0f},
                     {2.0f, 0.0f, 0.0f},
                     {2.1f, 0.0f, 0.0f},
                 });

    const Runtime::SandboxEditorKMeansCommand command{
        .StableEntityId =
            Runtime::SelectionController::ToStableEntityId(cloud),
        .Domain = Domain::PointCloudPoints,
        .ClusterCount = 2u,
        .MaxIterations = 8u,
        .Seed = 13u,
        .Backend = Runtime::SandboxEditorKMeansBackend::CpuReference,
    };

    const Runtime::SandboxEditorKMeansResult first =
        Runtime::ApplySandboxEditorKMeansCommand(context, command);
    ASSERT_EQ(first.Status, Runtime::SandboxEditorCommandStatus::Pending);

    Runtime::DerivedJobQueueSnapshot queued = jobs.SnapshotAll();
    ASSERT_EQ(queued.Entries.size(), 1u);
    context.DerivedJobs = &queued;

    const Runtime::SandboxEditorKMeansResult duplicate =
        Runtime::ApplySandboxEditorKMeansCommand(context, command);
    EXPECT_EQ(duplicate.Status, Runtime::SandboxEditorCommandStatus::Pending);
    EXPECT_NE(duplicate.Message.find("already has an active"),
              std::string::npos);
    EXPECT_NE(duplicate.Message.find("job 0:1"), std::string::npos);
    EXPECT_EQ(jobs.SnapshotAll().Entries.size(), 1u);

    jobs.Pump(1u);
    jobs.DrainCompletions();
    EXPECT_EQ(jobs.ApplyMainThreadResults(1u), 1u);

    Runtime::DerivedJobQueueSnapshot complete = jobs.SnapshotAll();
    ASSERT_EQ(complete.Entries.size(), 1u);
    EXPECT_EQ(complete.Entries[0].Status, Runtime::DerivedJobStatus::Complete);
    context.DerivedJobs = &complete;

    const Runtime::SandboxEditorKMeansResult rerun =
        Runtime::ApplySandboxEditorKMeansCommand(context, command);
    EXPECT_EQ(rerun.Status, Runtime::SandboxEditorCommandStatus::Pending);
    Runtime::DerivedJobQueueSnapshot afterRerun = jobs.SnapshotAll();
    ASSERT_EQ(afterRerun.Entries.size(), 2u);
    EXPECT_EQ(afterRerun.Entries[1].Status, Runtime::DerivedJobStatus::Queued);
}

TEST(SandboxEditorUi, KMeansCpuDerivedJobDiscardsStaleTargetBeforeApply)
{
    using Domain = Runtime::SandboxEditorGeometryProcessingDomain;

    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    Runtime::StreamingExecutor executor{};
    Runtime::DerivedJobRegistry jobs{executor};
    context.DerivedJobCommands.Submit =
        [&jobs](Runtime::DerivedJobDesc desc)
        {
            return jobs.Submit(std::move(desc));
        };
    bool completedSinkCalled = false;
    context.MethodResultSinks.KMeans =
        [&completedSinkCalled](Runtime::SandboxEditorKMeansResult)
        {
            completedSinkCalled = true;
        };

    const ECS::EntityHandle cloud = MakeSelectable(registry, "Cloud");
    AddPointCloudSource(registry, cloud, 4u);
    SetPositions(registry.Raw().get<GS::Vertices>(cloud),
                 {
                     {0.0f, 0.0f, 0.0f},
                     {0.1f, 0.0f, 0.0f},
                     {2.0f, 0.0f, 0.0f},
                     {2.1f, 0.0f, 0.0f},
                 });

    const Runtime::SandboxEditorKMeansResult result =
        Runtime::ApplySandboxEditorKMeansCommand(
            context,
            Runtime::SandboxEditorKMeansCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(cloud),
                .Domain = Domain::PointCloudPoints,
                .ClusterCount = 2u,
                .MaxIterations = 8u,
                .Seed = 13u,
                .Backend =
                    Runtime::SandboxEditorKMeansBackend::CpuReference,
            });
    ASSERT_EQ(result.Status, Runtime::SandboxEditorCommandStatus::Pending);

    SetPositions(registry.Raw().get<GS::Vertices>(cloud),
                 {
                     {10.0f, 0.0f, 0.0f},
                     {11.0f, 0.0f, 0.0f},
                     {12.0f, 0.0f, 0.0f},
                     {13.0f, 0.0f, 0.0f},
                 });

    jobs.Pump(1u);
    jobs.DrainCompletions();
    EXPECT_EQ(jobs.ApplyMainThreadResults(1u), 1u);

    Runtime::DerivedJobQueueSnapshot done = jobs.SnapshotAll();
    ASSERT_EQ(done.Entries.size(), 1u);
    EXPECT_EQ(done.Entries[0].Status,
              Runtime::DerivedJobStatus::StaleDiscarded);
    EXPECT_NE(done.Entries[0].Diagnostic.find(
                  "StaleSourcePropertyGeneration"),
              std::string::npos);
    EXPECT_FALSE(completedSinkCalled);
    EXPECT_FALSE(registry.Raw()
                     .get<GS::Vertices>(cloud)
                     .Properties.Get<std::uint32_t>("p:kmeans_label"));
}

TEST(SandboxEditorUi, ProgressivePoissonCommandPublishesPointPropertiesAndVisualization)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);

    const ECS::EntityHandle cloud = MakeSelectable(registry, "PoissonCloud");
    AddPointCloudSource(registry, cloud, 8u);
    const std::vector<glm::vec3> positions{
        {0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {1.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
        {1.0f, 0.0f, 1.0f},
        {0.0f, 1.0f, 1.0f},
        {1.0f, 1.0f, 1.0f},
    };
    SetPositions(registry.Raw().get<GS::Vertices>(cloud), positions);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, cloud));

    const Runtime::SandboxEditorProgressivePoissonConfig config{
        .Dimension = 3u,
        .GridWidth = 2u,
        .MaxLevels = 4u,
        .HashLoadFactor = 0.5f,
        .RadiusAlpha = -1.0f,
        .RandomizeGridOrigin = false,
        .GridOriginSeed = 17u,
        .ShuffleWithinLevels = true,
        .ShuffleSeed = 23u,
        .PrefixCount = 3u,
        .Channel = Runtime::SandboxEditorProgressivePoissonChannel::Phase,
    };

    const Runtime::SandboxEditorProgressivePoissonResult result =
        Runtime::ApplySandboxEditorProgressivePoissonCommand(
            context,
            Runtime::SandboxEditorProgressivePoissonCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(cloud),
                .Config = config,
            });

    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_EQ(result.InputCount, positions.size());
    EXPECT_GT(result.AcceptedCount, 0u);
    EXPECT_EQ(result.PrefixCount, std::min(3u, result.AcceptedCount));
    EXPECT_EQ(result.Channel,
              Runtime::SandboxEditorProgressivePoissonChannel::Phase);
    EXPECT_STREQ(
        Runtime::DebugNameForSandboxEditorProgressivePoissonChannel(
            result.Channel),
        "Phase");
    EXPECT_EQ(result.BackendId, PPR::kBackendId);
    EXPECT_EQ(result.BackendDisplayName, "CPU reference");
    EXPECT_EQ(result.RequestedBackend,
              Runtime::SandboxEditorProgressivePoissonBackend::CpuReference);
    EXPECT_EQ(result.ActualBackend,
              Runtime::SandboxEditorProgressivePoissonBackend::CpuReference);
    EXPECT_EQ(result.RequestedBackendId, PPR::kBackendId);
    EXPECT_FALSE(result.FellBackToCpu);
    EXPECT_TRUE(result.BackendFallbackReason.empty());
    EXPECT_EQ(result.LevelAcceptedCounts.size(), result.LevelCount);
    EXPECT_EQ(SumCounts(result.LevelAcceptedCounts), result.AcceptedCount);
    EXPECT_NE(result.Message.find(PPR::kBackendId), std::string::npos);

    Geometry::PropertySet& properties =
        registry.Raw().get<GS::Vertices>(cloud).Properties;
    const auto levels = properties.Get<float>("p:poisson_level");
    const auto phases = properties.Get<float>("p:poisson_phase");
    const auto splats = properties.Get<float>("p:poisson_splat_radius");
    const auto visible = properties.Get<float>("p:poisson_prefix_visible");
    ASSERT_TRUE(levels);
    ASSERT_TRUE(phases);
    ASSERT_TRUE(splats);
    ASSERT_TRUE(visible);
    ASSERT_EQ(levels.Vector().size(), positions.size());
    ASSERT_EQ(phases.Vector().size(), positions.size());
    ASSERT_EQ(splats.Vector().size(), positions.size());
    ASSERT_EQ(visible.Vector().size(), positions.size());

    std::size_t acceptedFromProperties = 0u;
    std::size_t visibleFromProperties = 0u;
    for (std::size_t i = 0u; i < positions.size(); ++i)
    {
        if (levels.Vector()[i] >= 0.0f)
        {
            ++acceptedFromProperties;
            EXPECT_GE(phases.Vector()[i], 0.0f);
            EXPECT_LT(phases.Vector()[i], 8.0f);
            EXPECT_GT(splats.Vector()[i], 0.0f);
        }
        if (visible.Vector()[i] > 0.5f)
            ++visibleFromProperties;
    }
    EXPECT_EQ(acceptedFromProperties, result.AcceptedCount);
    EXPECT_EQ(visibleFromProperties, result.PrefixCount);
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(cloud));
    EXPECT_TRUE(registry.Raw().all_of<G::RenderPoints>(cloud));

    ASSERT_TRUE(registry.Raw().all_of<G::VisualizationConfig>(cloud));
    const G::VisualizationConfig& vis =
        registry.Raw().get<G::VisualizationConfig>(cloud);
    EXPECT_EQ(vis.Source, G::VisualizationConfig::ColorSource::ScalarField);
    EXPECT_EQ(vis.ScalarDomain, G::VisualizationConfig::Domain::Vertex);
    EXPECT_EQ(vis.ScalarFieldName, "p:poisson_phase");

    context.LastProgressivePoissonResult = &result;
    const Runtime::SandboxEditorDomainWindowModel model =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::PointCloud);
    EXPECT_TRUE(model.Processing.PointCloudProgressivePoissonAvailable);
    ASSERT_TRUE(model.Processing.LastProgressivePoissonResult.has_value());
    EXPECT_TRUE(model.Processing.LastProgressivePoissonResult->Succeeded());
    EXPECT_EQ(model.Processing.LastProgressivePoissonResult->AcceptedCount,
              result.AcceptedCount);
}

TEST(SandboxEditorUi, ProgressivePoissonVulkanRequestFallsBackToCpuReference)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Tests::MockDevice device;
    device.Operational = false;
    Runtime::SandboxEditorContext context =
        MakeContext(registry, selection, true, nullptr, &device);

    const ECS::EntityHandle cloud = MakeSelectable(registry, "PoissonCloud");
    AddPointCloudSource(registry, cloud, 6u);
    const std::vector<glm::vec3> positions{
        {0.0f, 0.0f, 0.0f},
        {0.25f, 0.0f, 0.0f},
        {0.5f, 0.5f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {1.0f, 1.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
    };
    SetPositions(registry.Raw().get<GS::Vertices>(cloud), positions);

    Runtime::SandboxEditorProgressivePoissonConfig config{};
    config.Dimension = 2u;
    config.GridWidth = 3u;
    config.MaxLevels = 5u;
    config.HashLoadFactor = 0.75f;
    config.RadiusAlpha = 0.4f;
    config.RandomizeGridOrigin = false;
    config.ShuffleWithinLevels = false;
    config.PrefixCount = 3u;
    config.Channel = Runtime::SandboxEditorProgressivePoissonChannel::Level;
    config.Backend =
        Runtime::SandboxEditorProgressivePoissonBackend::VulkanCompute;

    const Runtime::SandboxEditorProgressivePoissonResult result =
        Runtime::ApplySandboxEditorProgressivePoissonCommand(
            context,
            Runtime::SandboxEditorProgressivePoissonCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(cloud),
                .Config = config,
            });

    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_EQ(result.RequestedBackend,
              Runtime::SandboxEditorProgressivePoissonBackend::VulkanCompute);
    EXPECT_EQ(result.ActualBackend,
              Runtime::SandboxEditorProgressivePoissonBackend::CpuReference);
    EXPECT_EQ(result.RequestedBackendId, "gpu_vulkan_compute");
    EXPECT_EQ(result.BackendId, PPR::kBackendId);
    EXPECT_TRUE(result.FellBackToCpu);
    EXPECT_NE(result.BackendFallbackReason.find("not operational"),
              std::string::npos);
    EXPECT_NE(result.Message.find("requested gpu_vulkan_compute"),
              std::string::npos);
    EXPECT_NE(result.Message.find("actual cpu_reference"),
              std::string::npos);

    PPR::Config directConfig{};
    directConfig.Dimension = config.Dimension;
    directConfig.GridWidth = config.GridWidth;
    directConfig.MaxLevels = config.MaxLevels;
    directConfig.HashLoadFactor = config.HashLoadFactor;
    directConfig.RadiusAlpha = config.RadiusAlpha;
    directConfig.RandomizeGridOrigin = config.RandomizeGridOrigin;
    directConfig.GridOriginSeed = config.GridOriginSeed;
    directConfig.ShuffleWithinLevels = config.ShuffleWithinLevels;
    directConfig.ShuffleSeed = config.ShuffleSeed;
    const PPR::Result direct = PPR::Compute(positions, directConfig);
    ASSERT_EQ(direct.Diag.Code, PPR::ValidationCode::Valid);
    EXPECT_EQ(result.AcceptedCount, direct.Diag.AcceptedCount);
    EXPECT_EQ(result.LevelAcceptedCounts, direct.Diag.LevelCounts);
    EXPECT_EQ(result.PrefixCount, std::min(3u, direct.Diag.AcceptedCount));
}

TEST(SandboxEditorUi, ProgressivePoissonCommandMatchesDirectMethodConfig)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);

    const ECS::EntityHandle cloud = MakeSelectable(registry, "PoissonCloud");
    AddPointCloudSource(registry, cloud, 6u);
    const std::vector<glm::vec3> positions{
        {0.0f, 0.0f, 0.0f},
        {0.25f, 0.0f, 0.0f},
        {0.5f, 0.5f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {1.0f, 1.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
    };
    SetPositions(registry.Raw().get<GS::Vertices>(cloud), positions);

    const Runtime::SandboxEditorProgressivePoissonConfig config{
        .Dimension = 2u,
        .GridWidth = 3u,
        .MaxLevels = 5u,
        .HashLoadFactor = 0.75f,
        .RadiusAlpha = 0.4f,
        .RandomizeGridOrigin = true,
        .GridOriginSeed = 19u,
        .ShuffleWithinLevels = false,
        .ShuffleSeed = 29u,
        .PrefixCount = 0u,
        .Channel = Runtime::SandboxEditorProgressivePoissonChannel::Level,
    };

    const Runtime::SandboxEditorProgressivePoissonResult result =
        Runtime::ApplySandboxEditorProgressivePoissonCommand(
            context,
            Runtime::SandboxEditorProgressivePoissonCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(cloud),
                .Config = config,
            });
    ASSERT_TRUE(result.Succeeded()) << result.Message;

    PPR::Config directConfig{};
    directConfig.Dimension = config.Dimension;
    directConfig.GridWidth = config.GridWidth;
    directConfig.MaxLevels = config.MaxLevels;
    directConfig.HashLoadFactor = config.HashLoadFactor;
    directConfig.RadiusAlpha = config.RadiusAlpha;
    directConfig.RandomizeGridOrigin = config.RandomizeGridOrigin;
    directConfig.GridOriginSeed = config.GridOriginSeed;
    directConfig.ShuffleWithinLevels = config.ShuffleWithinLevels;
    directConfig.ShuffleSeed = config.ShuffleSeed;
    const PPR::Result direct = PPR::Compute(positions, directConfig);
    ASSERT_EQ(direct.Diag.Code, PPR::ValidationCode::Valid);

    EXPECT_EQ(result.InputCount, direct.Diag.InputCount);
    EXPECT_EQ(result.AcceptedCount, direct.Diag.AcceptedCount);
    EXPECT_EQ(result.PrefixCount, direct.Diag.AcceptedCount);
    EXPECT_EQ(result.LevelCount, direct.Diag.LevelCounts.size());
    EXPECT_EQ(result.LevelAcceptedCounts, direct.Diag.LevelCounts);
    EXPECT_EQ(result.BackendId, PPR::kBackendId);
    EXPECT_EQ(result.BackendDisplayName, "CPU reference");
    EXPECT_FLOAT_EQ(result.BaseRadius, direct.BaseRadius);
    EXPECT_FLOAT_EQ(result.UsedAlpha, direct.Diag.UsedAlpha);

    Geometry::PropertySet& properties =
        registry.Raw().get<GS::Vertices>(cloud).Properties;
    const auto levels = properties.Get<float>("p:poisson_level");
    ASSERT_TRUE(levels);
    ASSERT_EQ(levels.Vector().size(), positions.size());

    std::vector<float> expectedLevels(positions.size(), -1.0f);
    for (std::size_t level = 0u; level + 1u < direct.LevelOffsets.size(); ++level)
    {
        for (std::uint32_t rank = direct.LevelOffsets[level];
             rank < direct.LevelOffsets[level + 1u];
             ++rank)
        {
            expectedLevels[direct.Order[rank]] = static_cast<float>(level);
        }
    }
    EXPECT_EQ(levels.Vector(), expectedLevels);
}

TEST(SandboxEditorUi, ProgressivePoissonCpuRequestQueuesDerivedJobAndPublishesOnApply)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    Runtime::StreamingExecutor executor{};
    Runtime::DerivedJobRegistry jobs{executor};
    context.DerivedJobCommands.Submit =
        [&jobs](Runtime::DerivedJobDesc desc)
        {
            return jobs.Submit(std::move(desc));
        };
    std::optional<Runtime::SandboxEditorProgressivePoissonResult>
        completedResult{};
    context.MethodResultSinks.ProgressivePoisson =
        [&completedResult](Runtime::SandboxEditorProgressivePoissonResult result)
        {
            completedResult = std::move(result);
        };

    const ECS::EntityHandle cloud = MakeSelectable(registry, "PoissonCloud");
    AddPointCloudSource(registry, cloud, 6u);
    const std::vector<glm::vec3> positions{
        {0.0f, 0.0f, 0.0f},
        {0.25f, 0.0f, 0.0f},
        {0.5f, 0.5f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {1.0f, 1.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
    };
    SetPositions(registry.Raw().get<GS::Vertices>(cloud), positions);

    const Runtime::SandboxEditorProgressivePoissonConfig config{
        .Dimension = 2u,
        .GridWidth = 3u,
        .MaxLevels = 5u,
        .HashLoadFactor = 0.75f,
        .RadiusAlpha = 0.4f,
        .RandomizeGridOrigin = true,
        .GridOriginSeed = 19u,
        .ShuffleWithinLevels = false,
        .ShuffleSeed = 29u,
        .PrefixCount = 3u,
        .Channel = Runtime::SandboxEditorProgressivePoissonChannel::Phase,
    };

    const Runtime::SandboxEditorProgressivePoissonResult result =
        Runtime::ApplySandboxEditorProgressivePoissonCommand(
            context,
            Runtime::SandboxEditorProgressivePoissonCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(cloud),
                .Config = config,
            });

    EXPECT_EQ(result.Status, Runtime::SandboxEditorCommandStatus::Pending);
    EXPECT_NE(result.Message.find("queued"), std::string::npos);
    EXPECT_FALSE(registry.Raw()
                     .get<GS::Vertices>(cloud)
                     .Properties.Get<float>("p:poisson_phase"));

    Runtime::DerivedJobQueueSnapshot queued = jobs.SnapshotAll();
    ASSERT_EQ(queued.Entries.size(), 1u);
    EXPECT_EQ(queued.Entries[0].Name, "Sandbox.ProgressivePoisson.CPU");
    EXPECT_EQ(queued.Entries[0].Status, Runtime::DerivedJobStatus::Queued);

    jobs.Pump(1u);
    jobs.DrainCompletions();
    EXPECT_FALSE(registry.Raw()
                     .get<GS::Vertices>(cloud)
                     .Properties.Get<float>("p:poisson_phase"));

    EXPECT_EQ(jobs.ApplyMainThreadResults(1u), 1u);
    Runtime::DerivedJobQueueSnapshot done = jobs.SnapshotAll();
    ASSERT_EQ(done.Entries.size(), 1u);
    EXPECT_EQ(done.Entries[0].Status, Runtime::DerivedJobStatus::Complete);
    ASSERT_TRUE(completedResult.has_value());
    EXPECT_TRUE(completedResult->Succeeded()) << completedResult->Message;
    EXPECT_EQ(completedResult->InputCount, positions.size());

    PPR::Config directConfig{};
    directConfig.Dimension = config.Dimension;
    directConfig.GridWidth = config.GridWidth;
    directConfig.MaxLevels = config.MaxLevels;
    directConfig.HashLoadFactor = config.HashLoadFactor;
    directConfig.RadiusAlpha = config.RadiusAlpha;
    directConfig.RandomizeGridOrigin = config.RandomizeGridOrigin;
    directConfig.GridOriginSeed = config.GridOriginSeed;
    directConfig.ShuffleWithinLevels = config.ShuffleWithinLevels;
    directConfig.ShuffleSeed = config.ShuffleSeed;
    const PPR::Result direct = PPR::Compute(positions, directConfig);
    ASSERT_EQ(direct.Diag.Code, PPR::ValidationCode::Valid);
    EXPECT_EQ(completedResult->AcceptedCount, direct.Diag.AcceptedCount);
    EXPECT_EQ(completedResult->LevelAcceptedCounts, direct.Diag.LevelCounts);

    const auto phases =
        registry.Raw()
            .get<GS::Vertices>(cloud)
            .Properties.Get<float>("p:poisson_phase");
    ASSERT_TRUE(phases);
    EXPECT_EQ(phases.Vector().size(), positions.size());
}

TEST(SandboxEditorUi, ProgressivePoissonCpuDerivedJobDiscardsStalePointCloudBeforeApply)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    Runtime::StreamingExecutor executor{};
    Runtime::DerivedJobRegistry jobs{executor};
    context.DerivedJobCommands.Submit =
        [&jobs](Runtime::DerivedJobDesc desc)
        {
            return jobs.Submit(std::move(desc));
        };
    bool completedSinkCalled = false;
    context.MethodResultSinks.ProgressivePoisson =
        [&completedSinkCalled](Runtime::SandboxEditorProgressivePoissonResult)
        {
            completedSinkCalled = true;
        };

    const ECS::EntityHandle cloud = MakeSelectable(registry, "PoissonCloud");
    AddPointCloudSource(registry, cloud, 4u);
    SetPositions(registry.Raw().get<GS::Vertices>(cloud),
                 {
                     {0.0f, 0.0f, 0.0f},
                     {0.25f, 0.0f, 0.0f},
                     {1.0f, 0.0f, 0.0f},
                     {1.0f, 1.0f, 0.0f},
                 });

    const Runtime::SandboxEditorProgressivePoissonResult result =
        Runtime::ApplySandboxEditorProgressivePoissonCommand(
            context,
            Runtime::SandboxEditorProgressivePoissonCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(cloud),
                .Config = Runtime::SandboxEditorProgressivePoissonConfig{
                    .Dimension = 2u,
                    .GridWidth = 3u,
                    .MaxLevels = 5u,
                    .HashLoadFactor = 0.75f,
                    .RadiusAlpha = 0.4f,
                    .RandomizeGridOrigin = false,
                    .ShuffleWithinLevels = false,
                    .PrefixCount = 0u,
                },
            });
    ASSERT_EQ(result.Status, Runtime::SandboxEditorCommandStatus::Pending);

    SetPositions(registry.Raw().get<GS::Vertices>(cloud),
                 {
                     {10.0f, 0.0f, 0.0f},
                     {11.0f, 0.0f, 0.0f},
                     {12.0f, 0.0f, 0.0f},
                     {13.0f, 0.0f, 0.0f},
                 });

    jobs.Pump(1u);
    jobs.DrainCompletions();
    EXPECT_EQ(jobs.ApplyMainThreadResults(1u), 1u);

    Runtime::DerivedJobQueueSnapshot done = jobs.SnapshotAll();
    ASSERT_EQ(done.Entries.size(), 1u);
    EXPECT_EQ(done.Entries[0].Status,
              Runtime::DerivedJobStatus::StaleDiscarded);
    EXPECT_NE(done.Entries[0].Diagnostic.find(
                  "StaleSourcePropertyGeneration"),
              std::string::npos);
    EXPECT_FALSE(completedSinkCalled);
    EXPECT_FALSE(registry.Raw()
                     .get<GS::Vertices>(cloud)
                     .Properties.Get<float>("p:poisson_level"));
}

TEST(SandboxEditorUi, ProgressivePoissonMeshCpuRequestQueuesDerivedJobAndPublishesOnApply)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    Runtime::StreamingExecutor executor{};
    Runtime::DerivedJobRegistry jobs{executor};
    context.DerivedJobCommands.Submit =
        [&jobs](Runtime::DerivedJobDesc desc)
        {
            return jobs.Submit(std::move(desc));
        };
    std::optional<Runtime::SandboxEditorProgressivePoissonResult>
        completedResult{};
    context.MethodResultSinks.ProgressivePoisson =
        [&completedResult](Runtime::SandboxEditorProgressivePoissonResult result)
        {
            completedResult = std::move(result);
        };

    const ECS::EntityHandle mesh = MakeSelectable(registry, "PoissonMesh");
    AddTriangleMeshSource(registry, mesh);
    registry.Raw().emplace<G::RenderSurface>(mesh);

    const Runtime::SandboxEditorProgressivePoissonConfig config{
        .Dimension = 2u,
        .GridWidth = 3u,
        .MaxLevels = 5u,
        .HashLoadFactor = 0.75f,
        .RadiusAlpha = 0.4f,
        .RandomizeGridOrigin = false,
        .GridOriginSeed = 31u,
        .ShuffleWithinLevels = true,
        .ShuffleSeed = 37u,
        .PrefixCount = 7u,
        .Channel = Runtime::SandboxEditorProgressivePoissonChannel::Level,
        .MeshSurfaceSampleCount = 24u,
        .MeshSurfaceSampleSeed = 41u,
        .MeshSurfaceMinTriangleArea = 1.0e-14,
        .MeshSurfaceInterpolateNormals = true,
    };

    const Runtime::SandboxEditorProgressivePoissonResult result =
        Runtime::ApplySandboxEditorProgressivePoissonCommand(
            context,
            Runtime::SandboxEditorProgressivePoissonCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(mesh),
                .Config = config,
            });

    EXPECT_EQ(result.Status, Runtime::SandboxEditorCommandStatus::Pending);
    EXPECT_TRUE(result.MeshSurfaceSamplingUsed);
    EXPECT_TRUE(registry.Raw().all_of<G::RenderSurface>(mesh));
    EXPECT_TRUE(registry.Raw().all_of<GS::Faces>(mesh));

    jobs.Pump(1u);
    jobs.DrainCompletions();
    EXPECT_TRUE(registry.Raw().all_of<G::RenderSurface>(mesh));
    EXPECT_TRUE(registry.Raw().all_of<GS::Faces>(mesh));

    EXPECT_EQ(jobs.ApplyMainThreadResults(1u), 1u);
    Runtime::DerivedJobQueueSnapshot done = jobs.SnapshotAll();
    ASSERT_EQ(done.Entries.size(), 1u);
    EXPECT_EQ(done.Entries[0].Status, Runtime::DerivedJobStatus::Complete);
    ASSERT_TRUE(completedResult.has_value());
    EXPECT_TRUE(completedResult->Succeeded()) << completedResult->Message;
    EXPECT_TRUE(completedResult->MeshSurfaceSamplingUsed);
    EXPECT_EQ(completedResult->MeshSurfaceSampleCount, 24u);
    EXPECT_EQ(completedResult->MeshSurfaceAcceptedTriangleCount, 1u);

    const GS::ConstSourceView view = GS::BuildConstView(registry.Raw(), mesh);
    const GS::SourceAvailability availability =
        GS::BuildSourceAvailability(view);
    EXPECT_EQ(availability.ProvenanceDomain, GS::Domain::PointCloud);
    EXPECT_FALSE(registry.Raw().all_of<G::RenderSurface>(mesh));
    ASSERT_NE(view.VertexSource, nullptr);
    EXPECT_EQ(view.FaceSource, nullptr);

    const auto positions =
        view.VertexSource->Properties.Get<glm::vec3>(PN::kPosition);
    const auto levels =
        view.VertexSource->Properties.Get<float>("p:poisson_level");
    ASSERT_TRUE(positions);
    ASSERT_TRUE(levels);
    EXPECT_EQ(positions.Vector().size(), 24u);
    EXPECT_EQ(levels.Vector().size(), 24u);
}

TEST(SandboxEditorUi, ProgressivePoissonConfigCommandRoutesThroughConfigFacade)
{
    Runtime::RuntimeEngineConfigControlState controlState{};
    controlState.ActiveConfig = Core::Config::EngineConfig{};

    Runtime::SandboxEditorContext configContext{};
    configContext.EngineConfigControlState = &controlState;
    configContext.EngineConfigCommandsAvailable = true;

    int previewCalls = 0;
    int applyCalls = 0;
    configContext.PreviewEngineConfigDocument =
        [&](const std::string& document, const std::string& sourceId)
    {
        ++previewCalls;
        return Core::Config::PreviewEngineConfig(
            document,
            controlState.ActiveConfig,
            Core::Config::EngineConfigParseOptions{.SourceId = sourceId});
    };
    configContext.ApplyEngineConfigHotSubset =
        [&](const Core::Config::EngineConfigLoadResult& loadResult)
    {
        ++applyCalls;
        Runtime::RuntimeEngineConfigApplyResult apply{
            .Source = Runtime::RuntimeConfigControlSource::Editor,
            .LoadResult = loadResult,
        };
        if (!Core::Config::IsConfigUsable(loadResult))
        {
            apply.Status =
                Runtime::RuntimeEngineConfigApplyStatus::Rejected;
            return apply;
        }
        controlState.ActiveConfig = loadResult.Preview.Config;
        apply.Status = Runtime::RuntimeEngineConfigApplyStatus::Applied;
        apply.EngineConfigApplied = true;
        apply.SandboxProgressivePoissonChanged = true;
        return apply;
    };

    Core::Config::ProgressivePoissonPlaygroundConfig config{};
    config.Dimension = 2u;
    config.GridWidth = 3u;
    config.MaxLevels = 5u;
    config.HashLoadFactor = 0.75;
    config.RadiusAlpha = 0.4;
    config.RandomizeGridOrigin = true;
    config.GridOriginSeed = 19u;
    config.ShuffleWithinLevels = false;
    config.ShuffleSeed = 29u;
    config.PrefixCount = 3u;
    config.Channel = Core::Config::ProgressivePoissonPlaygroundChannel::Phase;
    config.Backend = Core::Config::ProgressivePoissonPlaygroundBackend::VulkanCompute;
    config.AutoRunOnEdit = true;
    config.DebounceSeconds = 0.2;

    const Runtime::SandboxEditorProgressivePoissonConfigResult configResult =
        Runtime::ApplySandboxEditorProgressivePoissonConfigCommand(
            configContext,
            Runtime::SandboxEditorProgressivePoissonConfigCommand{
                .Config = config,
                .SourceId = "test-progressive-poisson-config",
            });

    ASSERT_TRUE(configResult.Succeeded()) << configResult.Message;
    EXPECT_EQ(previewCalls, 1);
    EXPECT_EQ(applyCalls, 1);
    EXPECT_EQ(controlState.ActiveConfig.Sandbox.ProgressivePoisson.GridWidth,
              3u);
    EXPECT_EQ(controlState.ActiveConfig.Sandbox.ProgressivePoisson.Channel,
              Core::Config::ProgressivePoissonPlaygroundChannel::Phase);
    EXPECT_EQ(controlState.ActiveConfig.Sandbox.ProgressivePoisson.Backend,
              Core::Config::ProgressivePoissonPlaygroundBackend::VulkanCompute);

    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext commandContext =
        MakeContext(registry, selection);

    const ECS::EntityHandle cloud = MakeSelectable(registry, "PoissonCloud");
    AddPointCloudSource(registry, cloud, 6u);
    const std::vector<glm::vec3> positions{
        {0.0f, 0.0f, 0.0f},
        {0.25f, 0.0f, 0.0f},
        {0.5f, 0.5f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {1.0f, 1.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
    };
    SetPositions(registry.Raw().get<GS::Vertices>(cloud), positions);

    const Runtime::SandboxEditorProgressivePoissonConfig runtimeConfig =
        Runtime::MakeSandboxEditorProgressivePoissonConfig(
            controlState.ActiveConfig.Sandbox.ProgressivePoisson);
    EXPECT_EQ(runtimeConfig.Backend,
              Runtime::SandboxEditorProgressivePoissonBackend::VulkanCompute);
    const Runtime::SandboxEditorProgressivePoissonResult result =
        Runtime::ApplySandboxEditorProgressivePoissonCommand(
            commandContext,
            Runtime::SandboxEditorProgressivePoissonCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(cloud),
                .Config = runtimeConfig,
            });
    ASSERT_TRUE(result.Succeeded()) << result.Message;

    PPR::Config directConfig{};
    directConfig.Dimension = runtimeConfig.Dimension;
    directConfig.GridWidth = runtimeConfig.GridWidth;
    directConfig.MaxLevels = runtimeConfig.MaxLevels;
    directConfig.HashLoadFactor = runtimeConfig.HashLoadFactor;
    directConfig.RadiusAlpha = runtimeConfig.RadiusAlpha;
    directConfig.RandomizeGridOrigin = runtimeConfig.RandomizeGridOrigin;
    directConfig.GridOriginSeed = runtimeConfig.GridOriginSeed;
    directConfig.ShuffleWithinLevels = runtimeConfig.ShuffleWithinLevels;
    directConfig.ShuffleSeed = runtimeConfig.ShuffleSeed;
    const PPR::Result direct = PPR::Compute(positions, directConfig);
    ASSERT_EQ(direct.Diag.Code, PPR::ValidationCode::Valid);
    EXPECT_EQ(result.AcceptedCount, direct.Diag.AcceptedCount);
    EXPECT_EQ(result.PrefixCount, std::min(3u, direct.Diag.AcceptedCount));
    EXPECT_EQ(result.LevelCount, direct.Diag.LevelCounts.size());
    EXPECT_EQ(result.LevelAcceptedCounts, direct.Diag.LevelCounts);
    EXPECT_EQ(result.BackendId, PPR::kBackendId);
    EXPECT_EQ(result.BackendDisplayName, "CPU reference");
    EXPECT_EQ(result.RequestedBackend,
              Runtime::SandboxEditorProgressivePoissonBackend::VulkanCompute);
    EXPECT_EQ(result.ActualBackend,
              Runtime::SandboxEditorProgressivePoissonBackend::CpuReference);
    EXPECT_TRUE(result.FellBackToCpu);
    EXPECT_NE(result.BackendFallbackReason.find("no RHI device"),
              std::string::npos);
    EXPECT_FLOAT_EQ(result.BaseRadius, direct.BaseRadius);

    Geometry::PropertySet& properties =
        registry.Raw().get<GS::Vertices>(cloud).Properties;
    const auto phases = properties.Get<float>("p:poisson_phase");
    const auto visible = properties.Get<float>("p:poisson_prefix_visible");
    ASSERT_TRUE(phases);
    ASSERT_TRUE(visible);
    ASSERT_EQ(phases.Vector().size(), positions.size());
    ASSERT_EQ(visible.Vector().size(), positions.size());
    std::size_t visibleCount = 0u;
    for (float value : visible.Vector())
    {
        if (value > 0.5f)
        {
            ++visibleCount;
        }
    }
    EXPECT_EQ(visibleCount, result.PrefixCount);
}

TEST(SandboxEditorUi, ProgressivePoissonCommandSamplesMeshSurfaceToPointCloud)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);

    const ECS::EntityHandle mesh = MakeSelectable(registry, "PoissonMesh");
    AddTriangleMeshSource(registry, mesh);
    registry.Raw().emplace<G::RenderSurface>(mesh);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));

    const Runtime::SandboxEditorProgressivePoissonConfig config{
        .Dimension = 2u,
        .GridWidth = 3u,
        .MaxLevels = 5u,
        .HashLoadFactor = 0.75f,
        .RadiusAlpha = 0.4f,
        .RandomizeGridOrigin = false,
        .GridOriginSeed = 31u,
        .ShuffleWithinLevels = true,
        .ShuffleSeed = 37u,
        .PrefixCount = 7u,
        .Channel = Runtime::SandboxEditorProgressivePoissonChannel::Level,
        .MeshSurfaceSampleCount = 32u,
        .MeshSurfaceSampleSeed = 41u,
        .MeshSurfaceMinTriangleArea = 1.0e-14,
        .MeshSurfaceInterpolateNormals = true,
    };

    const Runtime::SandboxEditorProgressivePoissonResult result =
        Runtime::ApplySandboxEditorProgressivePoissonCommand(
            context,
            Runtime::SandboxEditorProgressivePoissonCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(mesh),
                .Config = config,
            });

    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_TRUE(result.MeshSurfaceSamplingUsed);
    EXPECT_EQ(result.MeshSurfaceSampleCount, 32u);
    EXPECT_EQ(result.MeshSurfaceTotalFaceCount, 1u);
    EXPECT_EQ(result.MeshSurfaceAcceptedTriangleCount, 1u);
    EXPECT_EQ(result.MeshSurfaceRejectedFaceCount, 0u);
    EXPECT_GT(result.MeshSurfaceArea, 0.0);
    EXPECT_EQ(result.InputCount, 32u);
    EXPECT_GT(result.AcceptedCount, 0u);
    EXPECT_EQ(result.PrefixCount, std::min(7u, result.AcceptedCount));

    const GS::ConstSourceView view =
        GS::BuildConstView(registry.Raw(), mesh);
    const GS::SourceAvailability availability =
        GS::BuildSourceAvailability(view);
    EXPECT_EQ(availability.ProvenanceDomain, GS::Domain::PointCloud);
    ASSERT_NE(view.VertexSource, nullptr);
    EXPECT_EQ(view.HalfedgeSource, nullptr);
    EXPECT_EQ(view.FaceSource, nullptr);

    const Geometry::ConstPropertySet properties{
        view.VertexSource->Properties};
    const auto positions = properties.Get<glm::vec3>(PN::kPosition);
    const auto normals = properties.Get<glm::vec3>(PN::kNormal);
    const auto levels = properties.Get<float>("p:poisson_level");
    const auto phases = properties.Get<float>("p:poisson_phase");
    const auto splats = properties.Get<float>("p:poisson_splat_radius");
    const auto visible = properties.Get<float>("p:poisson_prefix_visible");
    ASSERT_TRUE(positions);
    ASSERT_TRUE(normals);
    ASSERT_TRUE(levels);
    ASSERT_TRUE(phases);
    ASSERT_TRUE(splats);
    ASSERT_TRUE(visible);
    ASSERT_EQ(positions.Vector().size(), 32u);
    EXPECT_EQ(normals.Vector().size(), 32u);
    EXPECT_EQ(levels.Vector().size(), 32u);
    EXPECT_EQ(phases.Vector().size(), 32u);
    EXPECT_EQ(splats.Vector().size(), 32u);
    EXPECT_EQ(visible.Vector().size(), 32u);

    PPR::Config directConfig{};
    directConfig.Dimension = config.Dimension;
    directConfig.GridWidth = config.GridWidth;
    directConfig.MaxLevels = config.MaxLevels;
    directConfig.HashLoadFactor = config.HashLoadFactor;
    directConfig.RadiusAlpha = config.RadiusAlpha;
    directConfig.RandomizeGridOrigin = config.RandomizeGridOrigin;
    directConfig.GridOriginSeed = config.GridOriginSeed;
    directConfig.ShuffleWithinLevels = config.ShuffleWithinLevels;
    directConfig.ShuffleSeed = config.ShuffleSeed;
    const PPR::Result direct =
        PPR::Compute(positions.Vector(), directConfig);
    ASSERT_EQ(direct.Diag.Code, PPR::ValidationCode::Valid);
    EXPECT_EQ(result.AcceptedCount, direct.Diag.AcceptedCount);
    EXPECT_EQ(result.LevelCount, direct.Diag.LevelCounts.size());
    EXPECT_EQ(result.LevelAcceptedCounts, direct.Diag.LevelCounts);
    EXPECT_EQ(result.BackendId, PPR::kBackendId);
    EXPECT_EQ(result.BackendDisplayName, "CPU reference");
    EXPECT_FLOAT_EQ(result.BaseRadius, direct.BaseRadius);

    EXPECT_FALSE(registry.Raw().all_of<G::RenderSurface>(mesh));
    EXPECT_TRUE(registry.Raw().all_of<G::RenderPoints>(mesh));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::GpuDirty>(mesh));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(mesh));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));

    ASSERT_TRUE(registry.Raw().all_of<G::VisualizationConfig>(mesh));
    const G::VisualizationConfig& vis =
        registry.Raw().get<G::VisualizationConfig>(mesh);
    EXPECT_EQ(vis.Source, G::VisualizationConfig::ColorSource::ScalarField);
    EXPECT_EQ(vis.ScalarDomain, G::VisualizationConfig::Domain::Vertex);
    EXPECT_EQ(vis.ScalarFieldName, "p:poisson_level");
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

TEST(SandboxEditorUi, MeshDenoiseCommandPublishesPositionsAndSupportsUndoRedo)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;

    const ECS::EntityHandle mesh = MakeSelectable(registry, "DenoiseMesh");
    AddDenoiseTetraMeshSource(registry, mesh);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);
    const std::vector<glm::vec3> original =
        MeshVertexPositions(registry, mesh);
    ASSERT_EQ(original.size(), 4u);

    const Runtime::SandboxEditorMeshDenoiseResult result =
        Runtime::ApplySandboxEditorMeshDenoiseCommand(
            context,
            Runtime::SandboxEditorMeshDenoiseCommand{
                .StableEntityId = stableId,
                .NormalIterations = 2u,
                .VertexIterations = 3u,
                .SigmaSpatial = 0.0,
                .SigmaRange = 0.0,
                .PreserveBoundary = true,
            });

    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_EQ(result.DenoiseStatus, Smooth::DenoiseStatus::Success);
    EXPECT_EQ(result.VertexSlotCount, original.size());
    EXPECT_EQ(result.WrittenCount, original.size());
    EXPECT_EQ(result.SkippedDeletedVertexCount, 0u);
    EXPECT_EQ(result.NormalIterations, 2u);
    EXPECT_EQ(result.VertexIterations, 3u);
    EXPECT_GT(result.ProcessedFaceCount, 0u);
    EXPECT_GT(result.MovedVertexCount, 0u);
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(mesh));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexNormals>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::GpuDirty>(mesh));
    EXPECT_TRUE(history.IsDirty());

    const std::vector<glm::vec3> denoised =
        MeshVertexPositions(registry, mesh);
    ASSERT_TRUE(AnyPositionDiffers(original, denoised));
    for (const glm::vec3 position : denoised)
    {
        EXPECT_TRUE(std::isfinite(position.x));
        EXPECT_TRUE(std::isfinite(position.y));
        EXPECT_TRUE(std::isfinite(position.z));
    }

    EXPECT_EQ(history.Undo().Status,
              Runtime::EditorCommandHistoryStatus::Undone);
    ExpectPositionsExactlyEqual(MeshVertexPositions(registry, mesh), original);
    EXPECT_EQ(history.Redo().Status,
              Runtime::EditorCommandHistoryStatus::Redone);
    ExpectPositionsExactlyEqual(MeshVertexPositions(registry, mesh), denoised);

    context.LastMeshDenoiseResult = &result;
    const Runtime::SandboxEditorDomainWindowModel model =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::Mesh);
    EXPECT_TRUE(model.Processing.MeshDenoiseAvailable);
    ASSERT_TRUE(model.Processing.LastMeshDenoiseResult.has_value());
    EXPECT_TRUE(model.Processing.LastMeshDenoiseResult->Succeeded());
    EXPECT_EQ(model.Processing.LastMeshDenoiseResult->WrittenCount, 4u);
}

TEST(SandboxEditorUi, MeshDenoiseRequestQueuesDerivedJobAndPublishesOnApply)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;
    Runtime::StreamingExecutor executor{};
    Runtime::DerivedJobRegistry jobs{executor};
    AttachDerivedJobCommands(context, jobs);
    std::optional<Runtime::SandboxEditorMeshDenoiseResult> completedResult{};
    context.MethodResultSinks.MeshDenoise =
        [&completedResult](Runtime::SandboxEditorMeshDenoiseResult result)
        {
            completedResult = std::move(result);
        };

    const ECS::EntityHandle mesh = MakeSelectable(registry, "DenoiseMesh");
    AddDenoiseTetraMeshSource(registry, mesh);
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);
    const std::vector<glm::vec3> original =
        MeshVertexPositions(registry, mesh);

    const Runtime::SandboxEditorMeshDenoiseResult result =
        Runtime::ApplySandboxEditorMeshDenoiseCommand(
            context,
            Runtime::SandboxEditorMeshDenoiseCommand{
                .StableEntityId = stableId,
                .NormalIterations = 2u,
                .VertexIterations = 3u,
                .SigmaSpatial = 0.0,
                .SigmaRange = 0.0,
                .PreserveBoundary = true,
            });

    EXPECT_EQ(result.Status, Runtime::SandboxEditorCommandStatus::Pending);
    EXPECT_NE(result.Message.find("queued"), std::string::npos);
    ExpectPositionsExactlyEqual(MeshVertexPositions(registry, mesh), original);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));

    Runtime::DerivedJobQueueSnapshot queued = jobs.SnapshotAll();
    ASSERT_EQ(queued.Entries.size(), 1u);
    EXPECT_EQ(queued.Entries[0].Name, "Sandbox.MeshDenoise.CPU");
    EXPECT_EQ(queued.Entries[0].Status, Runtime::DerivedJobStatus::Queued);

    jobs.Pump(1u);
    jobs.DrainCompletions();
    ExpectPositionsExactlyEqual(MeshVertexPositions(registry, mesh), original);
    EXPECT_FALSE(completedResult.has_value());

    EXPECT_EQ(jobs.ApplyMainThreadResults(1u), 1u);
    Runtime::DerivedJobQueueSnapshot done = jobs.SnapshotAll();
    ASSERT_EQ(done.Entries.size(), 1u);
    EXPECT_EQ(done.Entries[0].Status, Runtime::DerivedJobStatus::Complete);
    ASSERT_TRUE(completedResult.has_value());
    EXPECT_TRUE(completedResult->Succeeded()) << completedResult->Message;
    EXPECT_EQ(completedResult->DenoiseStatus, Smooth::DenoiseStatus::Success);
    EXPECT_EQ(completedResult->VertexSlotCount, original.size());
    EXPECT_GT(completedResult->MovedVertexCount, 0u);
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(mesh));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));
    EXPECT_TRUE(history.IsDirty());
    EXPECT_TRUE(AnyPositionDiffers(original, MeshVertexPositions(registry, mesh)));
}

TEST(SandboxEditorUi, MeshDenoiseDerivedJobDiscardsStaleMeshBeforeApply)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    Runtime::StreamingExecutor executor{};
    Runtime::DerivedJobRegistry jobs{executor};
    AttachDerivedJobCommands(context, jobs);
    bool completedSinkCalled = false;
    context.MethodResultSinks.MeshDenoise =
        [&completedSinkCalled](Runtime::SandboxEditorMeshDenoiseResult)
        {
            completedSinkCalled = true;
        };

    const ECS::EntityHandle mesh = MakeSelectable(registry, "StaleDenoiseMesh");
    AddDenoiseTetraMeshSource(registry, mesh);
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);

    const Runtime::SandboxEditorMeshDenoiseResult result =
        Runtime::ApplySandboxEditorMeshDenoiseCommand(
            context,
            Runtime::SandboxEditorMeshDenoiseCommand{
                .StableEntityId = stableId,
                .NormalIterations = 2u,
                .VertexIterations = 3u,
                .SigmaSpatial = 0.0,
                .SigmaRange = 0.0,
                .PreserveBoundary = true,
            });
    ASSERT_EQ(result.Status, Runtime::SandboxEditorCommandStatus::Pending);

    SetPositions(registry.Raw().get<GS::Vertices>(mesh),
                 {
                     {10.0f, 0.0f, 0.0f},
                     {11.0f, 0.0f, 0.0f},
                     {12.0f, 0.0f, 0.0f},
                     {13.0f, 0.0f, 0.0f},
                 });

    jobs.Pump(1u);
    jobs.DrainCompletions();
    EXPECT_EQ(jobs.ApplyMainThreadResults(1u), 1u);

    Runtime::DerivedJobQueueSnapshot done = jobs.SnapshotAll();
    ASSERT_EQ(done.Entries.size(), 1u);
    EXPECT_EQ(done.Entries[0].Status,
              Runtime::DerivedJobStatus::StaleDiscarded);
    EXPECT_NE(done.Entries[0].Diagnostic.find(
                  "StaleSourcePropertyGeneration"),
              std::string::npos);
    EXPECT_FALSE(completedSinkCalled);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(mesh));
}

namespace
{
    // Two dense 5x5 grid clusters plus three far isolated outliers (appended
    // last) — a deterministic UI-027 outlier-removal fixture mirroring the
    // GEOM-016 unit fixture.
    [[nodiscard]] std::vector<glm::vec3> MakeOutlierClusterPositions()
    {
        std::vector<glm::vec3> positions;
        const auto appendGrid = [&positions](const glm::vec3 origin)
        {
            for (int y = 0; y < 5; ++y)
                for (int x = 0; x < 5; ++x)
                    positions.push_back(
                        origin + glm::vec3(static_cast<float>(x) * 0.05f,
                                           static_cast<float>(y) * 0.05f,
                                           0.0f));
        };
        appendGrid(glm::vec3{0.0f});
        appendGrid(glm::vec3{2.0f, 0.0f, 0.0f});
        positions.push_back(glm::vec3{10.0f, 10.0f, 10.0f});
        positions.push_back(glm::vec3{-8.0f, 5.0f, -3.0f});
        positions.push_back(glm::vec3{12.0f, -7.0f, 4.0f});
        return positions;
    }

    [[nodiscard]] std::size_t PointCloudPositionCount(
        ECS::Scene::Registry& registry,
        const ECS::EntityHandle entity)
    {
        auto pos = registry.Raw()
                       .get<GS::Vertices>(entity)
                       .Properties.Get<glm::vec3>(PN::kPosition);
        return pos ? pos.Vector().size() : 0u;
    }
}

TEST(SandboxEditorUi, PointCloudOutlierRemovalStatisticalPublishesKeptPointsWithUndoRedo)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;

    const std::vector<glm::vec3> positions = MakeOutlierClusterPositions();
    const std::size_t originalCount = positions.size();
    ASSERT_EQ(originalCount, 53u);

    const ECS::EntityHandle cloud = MakeSelectable(registry, "OutlierCloud");
    AddPointCloudSource(registry, cloud, originalCount);
    SetPositions(registry.Raw().get<GS::Vertices>(cloud), positions);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, cloud));
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(cloud);
    ASSERT_EQ(PointCloudPositionCount(registry, cloud), originalCount);

    const Runtime::SandboxEditorPointCloudOutlierRemovalResult result =
        Runtime::ApplySandboxEditorPointCloudOutlierRemovalCommand(
            context,
            Runtime::SandboxEditorPointCloudOutlierRemovalCommand{
                .StableEntityId = stableId,
                .Method =
                    Runtime::SandboxEditorPointCloudOutlierMethod::Statistical,
                .KNeighbors = 8u,
                .StdDevMultiplier = 1.0f,
            });

    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_EQ(result.OriginalCount, originalCount);
    EXPECT_GE(result.RejectedCount, 2u);
    EXPECT_EQ(result.KeptCount + result.RejectedCount, originalCount);
    EXPECT_LT(result.KeptCount, originalCount);
    // The published point GeometrySources reflect exactly the kept points.
    EXPECT_EQ(PointCloudPositionCount(registry, cloud), result.KeptCount);
    EXPECT_TRUE(registry.Raw().all_of<Dirty::GpuDirty>(cloud));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(cloud));
    EXPECT_TRUE(history.IsDirty());

    // Undo restores the original point set; redo re-applies the removal.
    EXPECT_EQ(history.Undo().Status,
              Runtime::EditorCommandHistoryStatus::Undone);
    EXPECT_EQ(PointCloudPositionCount(registry, cloud), originalCount);
    EXPECT_EQ(history.Redo().Status,
              Runtime::EditorCommandHistoryStatus::Redone);
    EXPECT_EQ(PointCloudPositionCount(registry, cloud), result.KeptCount);

    context.LastPointCloudOutlierRemovalResult = &result;
    const Runtime::SandboxEditorDomainWindowModel model =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::PointCloud);
    EXPECT_TRUE(model.Processing.PointCloudOutlierRemovalAvailable);
    ASSERT_TRUE(
        model.Processing.LastPointCloudOutlierRemovalResult.has_value());
    EXPECT_TRUE(
        model.Processing.LastPointCloudOutlierRemovalResult->Succeeded());
}

TEST(SandboxEditorUi,
     PointCloudOutlierRemovalRequestQueuesDerivedJobAndPublishesOnApply)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;
    Runtime::StreamingExecutor executor{};
    Runtime::DerivedJobRegistry jobs{executor};
    AttachDerivedJobCommands(context, jobs);
    std::optional<Runtime::SandboxEditorPointCloudOutlierRemovalResult>
        completedResult{};
    context.MethodResultSinks.PointCloudOutlierRemoval =
        [&completedResult](
            Runtime::SandboxEditorPointCloudOutlierRemovalResult result)
        {
            completedResult = std::move(result);
        };

    const std::vector<glm::vec3> positions = MakeOutlierClusterPositions();
    const std::size_t originalCount = positions.size();
    const ECS::EntityHandle cloud =
        MakeSelectable(registry, "QueuedOutlierCloud");
    AddPointCloudSource(registry, cloud, originalCount);
    SetPositions(registry.Raw().get<GS::Vertices>(cloud), positions);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, cloud));

    const Runtime::SandboxEditorPointCloudOutlierRemovalResult queued =
        Runtime::ApplySandboxEditorPointCloudOutlierRemovalCommand(
            context,
            Runtime::SandboxEditorPointCloudOutlierRemovalCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(cloud),
                .Method =
                    Runtime::SandboxEditorPointCloudOutlierMethod::Statistical,
                .KNeighbors = 8u,
                .StdDevMultiplier = 1.0f,
            });

    EXPECT_EQ(queued.Status, Runtime::SandboxEditorCommandStatus::Pending);
    EXPECT_EQ(queued.OriginalCount, originalCount);
    EXPECT_EQ(queued.KeptCount, originalCount);
    EXPECT_EQ(PointCloudPositionCount(registry, cloud), originalCount);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(cloud));

    Runtime::DerivedJobQueueSnapshot pending = jobs.SnapshotAll();
    ASSERT_EQ(pending.Entries.size(), 1u);
    EXPECT_EQ(pending.Entries[0].Name,
              "Sandbox.PointCloudOutlierRemoval.CPU");
    EXPECT_EQ(pending.Entries[0].Status, Runtime::DerivedJobStatus::Queued);

    jobs.Pump(1u);
    jobs.DrainCompletions();
    EXPECT_FALSE(completedResult.has_value());
    EXPECT_EQ(PointCloudPositionCount(registry, cloud), originalCount);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(cloud));

    EXPECT_EQ(jobs.ApplyMainThreadResults(1u), 1u);
    Runtime::DerivedJobQueueSnapshot done = jobs.SnapshotAll();
    ASSERT_EQ(done.Entries.size(), 1u);
    EXPECT_EQ(done.Entries[0].Status, Runtime::DerivedJobStatus::Complete);
    ASSERT_TRUE(completedResult.has_value());
    EXPECT_TRUE(completedResult->Succeeded()) << completedResult->Message;
    EXPECT_EQ(completedResult->OriginalCount, originalCount);
    EXPECT_GE(completedResult->RejectedCount, 2u);
    EXPECT_EQ(completedResult->KeptCount + completedResult->RejectedCount,
              originalCount);
    EXPECT_EQ(PointCloudPositionCount(registry, cloud),
              completedResult->KeptCount);
    EXPECT_TRUE(registry.Raw().all_of<Dirty::GpuDirty>(cloud));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(cloud));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(cloud));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexNormals>(cloud));
    EXPECT_TRUE(history.IsDirty());

    EXPECT_EQ(history.Undo().Status,
              Runtime::EditorCommandHistoryStatus::Undone);
    EXPECT_EQ(PointCloudPositionCount(registry, cloud), originalCount);
    EXPECT_EQ(history.Redo().Status,
              Runtime::EditorCommandHistoryStatus::Redone);
    EXPECT_EQ(PointCloudPositionCount(registry, cloud),
              completedResult->KeptCount);
}

TEST(SandboxEditorUi, PointCloudOutlierRemovalDerivedJobDiscardsStaleSource)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    Runtime::StreamingExecutor executor{};
    Runtime::DerivedJobRegistry jobs{executor};
    AttachDerivedJobCommands(context, jobs);
    bool completedSinkCalled = false;
    context.MethodResultSinks.PointCloudOutlierRemoval =
        [&completedSinkCalled](
            Runtime::SandboxEditorPointCloudOutlierRemovalResult)
        {
            completedSinkCalled = true;
        };

    const std::vector<glm::vec3> positions = MakeOutlierClusterPositions();
    const std::size_t originalCount = positions.size();
    const ECS::EntityHandle cloud =
        MakeSelectable(registry, "StaleOutlierCloud");
    AddPointCloudSource(registry, cloud, originalCount);
    SetPositions(registry.Raw().get<GS::Vertices>(cloud), positions);

    const Runtime::SandboxEditorPointCloudOutlierRemovalResult queued =
        Runtime::ApplySandboxEditorPointCloudOutlierRemovalCommand(
            context,
            Runtime::SandboxEditorPointCloudOutlierRemovalCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(cloud),
                .Method =
                    Runtime::SandboxEditorPointCloudOutlierMethod::Statistical,
                .KNeighbors = 8u,
                .StdDevMultiplier = 1.0f,
            });
    ASSERT_EQ(queued.Status, Runtime::SandboxEditorCommandStatus::Pending);

    std::vector<glm::vec3> stalePositions = positions;
    for (glm::vec3& position : stalePositions)
        position.x += 100.0f;
    SetPositions(registry.Raw().get<GS::Vertices>(cloud), stalePositions);

    jobs.Pump(1u);
    jobs.DrainCompletions();
    EXPECT_EQ(jobs.ApplyMainThreadResults(1u), 1u);
    Runtime::DerivedJobQueueSnapshot done = jobs.SnapshotAll();
    ASSERT_EQ(done.Entries.size(), 1u);
    EXPECT_EQ(done.Entries[0].Status,
              Runtime::DerivedJobStatus::StaleDiscarded);
    EXPECT_NE(done.Entries[0].Diagnostic.find(
                  "StaleSourcePropertyGeneration"),
              std::string::npos);
    EXPECT_FALSE(completedSinkCalled);
    EXPECT_EQ(PointCloudPositionCount(registry, cloud), originalCount);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(cloud));
}

TEST(SandboxEditorUi, PointCloudOutlierRemovalRadiusPublishesAndFailsClosed)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;

    const std::vector<glm::vec3> positions = MakeOutlierClusterPositions();
    const std::size_t originalCount = positions.size();
    const ECS::EntityHandle cloud = MakeSelectable(registry, "RadiusCloud");
    AddPointCloudSource(registry, cloud, originalCount);
    SetPositions(registry.Raw().get<GS::Vertices>(cloud), positions);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, cloud));
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(cloud);

    const Runtime::SandboxEditorPointCloudOutlierRemovalResult radius =
        Runtime::ApplySandboxEditorPointCloudOutlierRemovalCommand(
            context,
            Runtime::SandboxEditorPointCloudOutlierRemovalCommand{
                .StableEntityId = stableId,
                .Method =
                    Runtime::SandboxEditorPointCloudOutlierMethod::Radius,
                .SearchRadius = 0.15f,
                .MinNeighbors = 3u,
            });
    ASSERT_TRUE(radius.Succeeded()) << radius.Message;
    EXPECT_GE(radius.RejectedCount, 2u);
    EXPECT_EQ(PointCloudPositionCount(registry, cloud), radius.KeptCount);

    // Fail-closed: non-positive radius is rejected before any mutation.
    const Runtime::SandboxEditorPointCloudOutlierRemovalResult badRadius =
        Runtime::ApplySandboxEditorPointCloudOutlierRemovalCommand(
            context,
            Runtime::SandboxEditorPointCloudOutlierRemovalCommand{
                .StableEntityId = stableId,
                .Method =
                    Runtime::SandboxEditorPointCloudOutlierMethod::Radius,
                .SearchRadius = 0.0f,
                .MinNeighbors = 3u,
            });
    EXPECT_EQ(badRadius.Status,
              Runtime::SandboxEditorCommandStatus::InvalidProcessingParameters);

    // Missing scene fails closed.
    const Runtime::SandboxEditorPointCloudOutlierRemovalResult missingScene =
        Runtime::ApplySandboxEditorPointCloudOutlierRemovalCommand(
            Runtime::SandboxEditorContext{},
            Runtime::SandboxEditorPointCloudOutlierRemovalCommand{
                .StableEntityId = stableId,
                .KNeighbors = 8u,
            });
    EXPECT_EQ(missingScene.Status,
              Runtime::SandboxEditorCommandStatus::MissingScene);

    // A mesh entity is the wrong domain for point-cloud outlier removal.
    const ECS::EntityHandle mesh = MakeSelectable(registry, "WrongDomainMesh");
    AddDenoiseTetraMeshSource(registry, mesh);
    const Runtime::SandboxEditorPointCloudOutlierRemovalResult wrongDomain =
        Runtime::ApplySandboxEditorPointCloudOutlierRemovalCommand(
            context,
            Runtime::SandboxEditorPointCloudOutlierRemovalCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(mesh),
                .KNeighbors = 8u,
            });
    EXPECT_EQ(wrongDomain.Status,
              Runtime::SandboxEditorCommandStatus::UnsupportedGeometryDomain);
}

TEST(SandboxEditorUi, PointCloudOutlierRemovalPreservesSurvivingPointProperties)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;

    const std::vector<glm::vec3> positions = MakeOutlierClusterPositions();
    const std::size_t originalCount = positions.size();  // 53: 50 inliers + 3 outliers
    const ECS::EntityHandle cloud = MakeSelectable(registry, "LabeledCloud");
    AddPointCloudSource(registry, cloud, originalCount);
    SetPositions(registry.Raw().get<GS::Vertices>(cloud), positions);

    // A non-built-in per-point property: every inlier carries the sentinel 7.0,
    // the two trailing outliers carry 99.0. removal.Filtered would drop this
    // property entirely; the command must compact it onto the kept points.
    {
        auto labels = registry.Raw()
                          .get<GS::Vertices>(cloud)
                          .Properties.GetOrAdd<float>("v:label", 0.0f);
        ASSERT_EQ(labels.Vector().size(), originalCount);
        for (std::size_t i = 0; i < originalCount; ++i)
            labels.Vector()[i] = (i + 3u >= originalCount) ? 99.0f : 7.0f;
    }
    ASSERT_TRUE(selection.SetSelectedEntity(registry, cloud));

    const Runtime::SandboxEditorPointCloudOutlierRemovalResult result =
        Runtime::ApplySandboxEditorPointCloudOutlierRemovalCommand(
            context,
            Runtime::SandboxEditorPointCloudOutlierRemovalCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(cloud),
                .Method =
                    Runtime::SandboxEditorPointCloudOutlierMethod::Statistical,
                .KNeighbors = 8u,
                .StdDevMultiplier = 1.0f,
            });
    ASSERT_TRUE(result.Succeeded()) << result.Message;

    // The "v:label" property must survive removal (bug: previously dropped).
    auto survived = registry.Raw()
                        .get<GS::Vertices>(cloud)
                        .Properties.Get<float>("v:label");
    ASSERT_TRUE(survived) << "v:label property was dropped after outlier removal";
    EXPECT_EQ(survived.Vector().size(), result.KeptCount);
    EXPECT_EQ(PointCloudPositionCount(registry, cloud), result.KeptCount);
    // All kept points are the inliers, so every surviving label is the sentinel.
    for (const float label : survived.Vector())
        EXPECT_FLOAT_EQ(label, 7.0f);
}

TEST(SandboxEditorUi, PointCloudOutlierRemovalRespectsDeletedSlots)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;

    // 53 live points plus one trailing slot marked deleted (a far position that
    // would look like an outlier if it were wrongly treated as live).
    std::vector<glm::vec3> positions = MakeOutlierClusterPositions();
    const std::size_t liveCount = positions.size();  // 53
    positions.push_back(glm::vec3{50.0f, 50.0f, 50.0f});
    const std::size_t slotCount = positions.size();  // 54

    const ECS::EntityHandle cloud = MakeSelectable(registry, "DeletedSlotCloud");
    AddPointCloudSource(registry, cloud, slotCount);
    SetPositions(registry.Raw().get<GS::Vertices>(cloud), positions);
    {
        auto& vertices = registry.Raw().get<GS::Vertices>(cloud);
        auto deleted =
            vertices.Properties.GetOrAdd<bool>("p:deleted", false);
        ASSERT_EQ(deleted.Vector().size(), slotCount);
        deleted.Vector()[slotCount - 1u] = true;  // last slot is dead
        vertices.NumDeleted = 1u;
    }
    ASSERT_TRUE(selection.SetSelectedEntity(registry, cloud));

    const Runtime::SandboxEditorPointCloudOutlierRemovalResult result =
        Runtime::ApplySandboxEditorPointCloudOutlierRemovalCommand(
            context,
            Runtime::SandboxEditorPointCloudOutlierRemovalCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(cloud),
                .Method =
                    Runtime::SandboxEditorPointCloudOutlierMethod::Statistical,
                .KNeighbors = 8u,
                .StdDevMultiplier = 1.0f,
            });
    ASSERT_TRUE(result.Succeeded()) << result.Message;

    // The deleted slot is excluded: counts reflect the live point set only, and
    // the dead point is never resurrected into the published cloud.
    EXPECT_EQ(result.OriginalCount, liveCount);
    EXPECT_EQ(result.KeptCount + result.RejectedCount, liveCount);
    EXPECT_EQ(PointCloudPositionCount(registry, cloud), result.KeptCount);
    EXPECT_LE(result.KeptCount, liveCount);
    EXPECT_EQ(registry.Raw().get<GS::Vertices>(cloud).NumDeleted, 0u);
}

TEST(SandboxEditorUi, PointCloudConsolidationPublishesProjectedPointsWithUndoRedo)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;

    const std::vector<glm::vec3> positions = MakeOutlierClusterPositions();
    const std::size_t originalCount = positions.size();
    ASSERT_EQ(originalCount, 53u);

    const ECS::EntityHandle cloud =
        MakeSelectable(registry, "ConsolidateCloud");
    AddPointCloudSource(registry, cloud, originalCount);
    SetPositions(registry.Raw().get<GS::Vertices>(cloud), positions);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, cloud));
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(cloud);
    ASSERT_EQ(PointCloudPositionCount(registry, cloud), originalCount);

    // 20% of the 53 live points rounds to an 11-point projected set.
    const std::size_t expectedTarget = 11u;
    const Runtime::SandboxEditorPointCloudConsolidationResult result =
        Runtime::ApplySandboxEditorPointCloudConsolidationCommand(
            context,
            Runtime::SandboxEditorPointCloudConsolidationCommand{
                .StableEntityId = stableId,
                .SupportRadius = 0.0f,  // derive 4x average spacing
                .RepulsionWeight = 0.45f,
                .Iterations = 4u,
                .TargetPercentage = 20.0f,
                .Seed = 42u,
                .Variant = 0u,
            });

    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_EQ(result.InputPointCount, originalCount);
    EXPECT_EQ(result.ProjectedPointCount, expectedTarget);
    EXPECT_EQ(result.IterationsRun, 4u);
    // The published point GeometrySources reflect exactly the projected points.
    EXPECT_EQ(PointCloudPositionCount(registry, cloud), expectedTarget);
    EXPECT_TRUE(registry.Raw().all_of<Dirty::GpuDirty>(cloud));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(cloud));
    EXPECT_TRUE(history.IsDirty());

    // Undo restores the original point set; redo re-applies the projection.
    EXPECT_EQ(history.Undo().Status,
              Runtime::EditorCommandHistoryStatus::Undone);
    EXPECT_EQ(PointCloudPositionCount(registry, cloud), originalCount);
    EXPECT_EQ(history.Redo().Status,
              Runtime::EditorCommandHistoryStatus::Redone);
    EXPECT_EQ(PointCloudPositionCount(registry, cloud), expectedTarget);

    context.LastPointCloudConsolidationResult = &result;
    const Runtime::SandboxEditorDomainWindowModel model =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::PointCloud);
    EXPECT_TRUE(model.Processing.PointCloudConsolidationAvailable);
    ASSERT_TRUE(
        model.Processing.LastPointCloudConsolidationResult.has_value());
    EXPECT_TRUE(
        model.Processing.LastPointCloudConsolidationResult->Succeeded());
}

TEST(SandboxEditorUi,
     PointCloudConsolidationRequestQueuesDerivedJobAndPublishesOnApply)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;
    Runtime::StreamingExecutor executor{};
    Runtime::DerivedJobRegistry jobs{executor};
    AttachDerivedJobCommands(context, jobs);
    std::optional<Runtime::SandboxEditorPointCloudConsolidationResult>
        completedResult{};
    context.MethodResultSinks.PointCloudConsolidation =
        [&completedResult](
            Runtime::SandboxEditorPointCloudConsolidationResult result)
        {
            completedResult = std::move(result);
        };

    const std::vector<glm::vec3> positions = MakeOutlierClusterPositions();
    const std::size_t originalCount = positions.size();
    const std::size_t expectedTarget = 11u;  // round(20% of 53)
    const ECS::EntityHandle cloud =
        MakeSelectable(registry, "QueuedConsolidateCloud");
    AddPointCloudSource(registry, cloud, originalCount);
    SetPositions(registry.Raw().get<GS::Vertices>(cloud), positions);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, cloud));

    const Runtime::SandboxEditorPointCloudConsolidationResult queued =
        Runtime::ApplySandboxEditorPointCloudConsolidationCommand(
            context,
            Runtime::SandboxEditorPointCloudConsolidationCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(cloud),
                .SupportRadius = 0.0f,
                .RepulsionWeight = 0.45f,
                .Iterations = 4u,
                .TargetPercentage = 20.0f,
                .Seed = 42u,
                .Variant = 0u,
            });

    EXPECT_EQ(queued.Status, Runtime::SandboxEditorCommandStatus::Pending);
    EXPECT_EQ(queued.InputPointCount, originalCount);
    EXPECT_EQ(PointCloudPositionCount(registry, cloud), originalCount);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(cloud));

    Runtime::DerivedJobQueueSnapshot pending = jobs.SnapshotAll();
    ASSERT_EQ(pending.Entries.size(), 1u);
    EXPECT_EQ(pending.Entries[0].Name,
              "Sandbox.PointCloudConsolidation.CPU");
    EXPECT_EQ(pending.Entries[0].Status, Runtime::DerivedJobStatus::Queued);

    jobs.Pump(1u);
    jobs.DrainCompletions();
    EXPECT_FALSE(completedResult.has_value());
    EXPECT_EQ(PointCloudPositionCount(registry, cloud), originalCount);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(cloud));

    EXPECT_EQ(jobs.ApplyMainThreadResults(1u), 1u);
    Runtime::DerivedJobQueueSnapshot done = jobs.SnapshotAll();
    ASSERT_EQ(done.Entries.size(), 1u);
    EXPECT_EQ(done.Entries[0].Status, Runtime::DerivedJobStatus::Complete);
    ASSERT_TRUE(completedResult.has_value());
    EXPECT_TRUE(completedResult->Succeeded()) << completedResult->Message;
    EXPECT_EQ(completedResult->InputPointCount, originalCount);
    EXPECT_EQ(completedResult->ProjectedPointCount, expectedTarget);
    EXPECT_EQ(PointCloudPositionCount(registry, cloud),
              completedResult->ProjectedPointCount);
    EXPECT_TRUE(registry.Raw().all_of<Dirty::GpuDirty>(cloud));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(cloud));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(cloud));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexNormals>(cloud));
    EXPECT_TRUE(history.IsDirty());

    EXPECT_EQ(history.Undo().Status,
              Runtime::EditorCommandHistoryStatus::Undone);
    EXPECT_EQ(PointCloudPositionCount(registry, cloud), originalCount);
    EXPECT_EQ(history.Redo().Status,
              Runtime::EditorCommandHistoryStatus::Redone);
    EXPECT_EQ(PointCloudPositionCount(registry, cloud),
              completedResult->ProjectedPointCount);
}

TEST(SandboxEditorUi, PointCloudConsolidationFailsClosedForInvalidInputs)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;

    // Missing scene fails closed.
    const Runtime::SandboxEditorPointCloudConsolidationResult missingScene =
        Runtime::ApplySandboxEditorPointCloudConsolidationCommand(
            Runtime::SandboxEditorContext{},
            Runtime::SandboxEditorPointCloudConsolidationCommand{
                .StableEntityId = 1u,
            });
    EXPECT_EQ(missingScene.Status,
              Runtime::SandboxEditorCommandStatus::MissingScene);

    // A mesh entity is the wrong domain for point-cloud consolidation.
    const ECS::EntityHandle mesh =
        MakeSelectable(registry, "WrongDomainConsolidateMesh");
    AddDenoiseTetraMeshSource(registry, mesh);
    const Runtime::SandboxEditorPointCloudConsolidationResult wrongDomain =
        Runtime::ApplySandboxEditorPointCloudConsolidationCommand(
            context,
            Runtime::SandboxEditorPointCloudConsolidationCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(mesh),
            });
    EXPECT_EQ(wrongDomain.Status,
              Runtime::SandboxEditorCommandStatus::UnsupportedGeometryDomain);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(mesh));

    // Fail-closed: invalid parameters are rejected before any mutation.
    const std::vector<glm::vec3> positions = MakeOutlierClusterPositions();
    const std::size_t originalCount = positions.size();
    const ECS::EntityHandle cloud =
        MakeSelectable(registry, "InvalidParamConsolidateCloud");
    AddPointCloudSource(registry, cloud, originalCount);
    SetPositions(registry.Raw().get<GS::Vertices>(cloud), positions);
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(cloud);

    const Runtime::SandboxEditorPointCloudConsolidationResult badRepulsion =
        Runtime::ApplySandboxEditorPointCloudConsolidationCommand(
            context,
            Runtime::SandboxEditorPointCloudConsolidationCommand{
                .StableEntityId = stableId,
                .RepulsionWeight = 0.75f,
            });
    EXPECT_EQ(badRepulsion.Status,
              Runtime::SandboxEditorCommandStatus::InvalidProcessingParameters);

    const Runtime::SandboxEditorPointCloudConsolidationResult badIterations =
        Runtime::ApplySandboxEditorPointCloudConsolidationCommand(
            context,
            Runtime::SandboxEditorPointCloudConsolidationCommand{
                .StableEntityId = stableId,
                .Iterations = 0u,
            });
    EXPECT_EQ(badIterations.Status,
              Runtime::SandboxEditorCommandStatus::InvalidProcessingParameters);

    EXPECT_EQ(PointCloudPositionCount(registry, cloud), originalCount);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(cloud));
    EXPECT_FALSE(history.IsDirty());
}

TEST(SandboxEditorUi, MeshDenoiseCommandFailsClosedForInvalidTargetsAndUnavailableKernel)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);

    const Runtime::SandboxEditorMeshDenoiseResult missingScene =
        Runtime::ApplySandboxEditorMeshDenoiseCommand(
            Runtime::SandboxEditorContext{},
            Runtime::SandboxEditorMeshDenoiseCommand{
                .StableEntityId = 1u,
            });
    EXPECT_EQ(missingScene.Status,
              Runtime::SandboxEditorCommandStatus::MissingScene);
    EXPECT_EQ(missingScene.Error, Core::ErrorCode::InvalidState);

    const ECS::EntityHandle cloud = MakeSelectable(registry, "CloudWrongDomain");
    AddPointCloudSource(registry, cloud, 3u);
    SetPositions(registry.Raw().get<GS::Vertices>(cloud),
                 {
                     {0.0f, 0.0f, 0.0f},
                     {1.0f, 0.0f, 0.0f},
                     {0.0f, 1.0f, 0.0f},
                 });
    const Runtime::SandboxEditorMeshDenoiseResult wrongDomain =
        Runtime::ApplySandboxEditorMeshDenoiseCommand(
            context,
            Runtime::SandboxEditorMeshDenoiseCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(cloud),
            });
    EXPECT_EQ(wrongDomain.Status,
              Runtime::SandboxEditorCommandStatus::UnsupportedGeometryDomain);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(cloud));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(cloud));

    const ECS::EntityHandle mesh = MakeSelectable(registry, "DenoiseFailMesh");
    AddDenoiseTetraMeshSource(registry, mesh);
    const std::vector<glm::vec3> before =
        MeshVertexPositions(registry, mesh);
    const Runtime::SandboxEditorMeshDenoiseResult invalidParams =
        Runtime::ApplySandboxEditorMeshDenoiseCommand(
            context,
            Runtime::SandboxEditorMeshDenoiseCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(mesh),
                .NormalIterations = 0u,
            });
    EXPECT_EQ(invalidParams.Status,
              Runtime::SandboxEditorCommandStatus::InvalidProcessingParameters);
    ExpectPositionsExactlyEqual(MeshVertexPositions(registry, mesh), before);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));

    context.MeshDenoiseKernelAvailable = false;
    const Runtime::SandboxEditorMeshDenoiseResult unavailable =
        Runtime::ApplySandboxEditorMeshDenoiseCommand(
            context,
            Runtime::SandboxEditorMeshDenoiseCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(mesh),
            });
    EXPECT_EQ(unavailable.Status,
              Runtime::SandboxEditorCommandStatus::GeometryProcessingFailed);
    EXPECT_EQ(unavailable.Error, Core::ErrorCode::InvalidState);
    ExpectPositionsExactlyEqual(MeshVertexPositions(registry, mesh), before);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));

    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));
    const Runtime::SandboxEditorDomainWindowModel unavailableModel =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::Mesh);
    EXPECT_FALSE(unavailableModel.Processing.MeshDenoiseAvailable);
}

TEST(SandboxEditorUi, MeshCurvatureCommandPublishesCanonicalPropertiesAndSupportsUndoRedo)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;

    const ECS::EntityHandle mesh = MakeSelectable(registry, "CurvatureMesh");
    AddDenoiseTetraMeshSource(registry, mesh);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);
    auto& properties = registry.Raw().get<GS::Vertices>(mesh).Properties;
    ASSERT_FALSE(properties.Exists(PN::kMeanCurvature));
    ASSERT_FALSE(properties.Exists(PN::kGaussianCurvature));
    ASSERT_FALSE(properties.Exists(PN::kPrincipalDir1));
    ASSERT_FALSE(properties.Exists(PN::kPrincipalDir2));

    const Runtime::SandboxEditorMeshCurvatureResult result =
        Runtime::ApplySandboxEditorMeshCurvatureCommand(
            context,
            Runtime::SandboxEditorMeshCurvatureCommand{
                .StableEntityId = stableId,
                .Output = Runtime::SandboxEditorMeshCurvatureOutput::All,
                .PublishPrincipalDirections = true,
            });

    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_EQ(result.VertexSlotCount, 4u);
    EXPECT_EQ(result.ScalarPropertyCount, 2u);
    EXPECT_EQ(result.ScalarWrittenCount, 8u);
    EXPECT_EQ(result.DirectionPropertyCount, 2u);
    EXPECT_EQ(result.DirectionWrittenCount, 8u);
    EXPECT_EQ(result.NonFiniteScalarCount, 0u);
    EXPECT_EQ(result.NonFiniteDirectionCount, 0u);
    EXPECT_TRUE(result.DirectionsRequested);
    EXPECT_TRUE(result.DirectionsAvailable);
    EXPECT_TRUE(result.DirectionsPublished);
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexNormals>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::GpuDirty>(mesh));
    EXPECT_TRUE(history.IsDirty());
    EXPECT_TRUE(history.CanUndo());

    const auto mean = properties.Get<double>(PN::kMeanCurvature);
    const auto gaussian = properties.Get<double>(PN::kGaussianCurvature);
    const auto dir1 = properties.Get<glm::vec3>(PN::kPrincipalDir1);
    const auto dir2 = properties.Get<glm::vec3>(PN::kPrincipalDir2);
    ASSERT_TRUE(mean.IsValid());
    ASSERT_TRUE(gaussian.IsValid());
    ASSERT_TRUE(dir1.IsValid());
    ASSERT_TRUE(dir2.IsValid());
    ASSERT_EQ(mean.Vector().size(), 4u);
    ASSERT_EQ(gaussian.Vector().size(), 4u);
    ASSERT_EQ(dir1.Vector().size(), 4u);
    ASSERT_EQ(dir2.Vector().size(), 4u);
    for (std::size_t i = 0u; i < 4u; ++i)
    {
        EXPECT_TRUE(std::isfinite(mean.Vector()[i]));
        EXPECT_TRUE(std::isfinite(gaussian.Vector()[i]));
        EXPECT_TRUE(std::isfinite(dir1.Vector()[i].x));
        EXPECT_TRUE(std::isfinite(dir1.Vector()[i].y));
        EXPECT_TRUE(std::isfinite(dir1.Vector()[i].z));
        EXPECT_TRUE(std::isfinite(dir2.Vector()[i].x));
        EXPECT_TRUE(std::isfinite(dir2.Vector()[i].y));
        EXPECT_TRUE(std::isfinite(dir2.Vector()[i].z));
    }

    EXPECT_EQ(history.Undo().Status,
              Runtime::EditorCommandHistoryStatus::Undone);
    EXPECT_FALSE(properties.Exists(PN::kMeanCurvature));
    EXPECT_FALSE(properties.Exists(PN::kGaussianCurvature));
    EXPECT_FALSE(properties.Exists(PN::kPrincipalDir1));
    EXPECT_FALSE(properties.Exists(PN::kPrincipalDir2));
    EXPECT_EQ(history.Redo().Status,
              Runtime::EditorCommandHistoryStatus::Redone);
    EXPECT_TRUE(properties.Get<double>(PN::kMeanCurvature).IsValid());
    EXPECT_TRUE(properties.Get<double>(PN::kGaussianCurvature).IsValid());
    EXPECT_TRUE(properties.Get<glm::vec3>(PN::kPrincipalDir1).IsValid());
    EXPECT_TRUE(properties.Get<glm::vec3>(PN::kPrincipalDir2).IsValid());

    context.LastMeshCurvatureResult = &result;
    const Runtime::SandboxEditorDomainWindowModel model =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::Mesh);
    EXPECT_TRUE(model.Processing.MeshCurvatureAvailable);
    EXPECT_TRUE(model.Processing.MeshCurvatureDirectionsAvailable);
    ASSERT_TRUE(model.Processing.LastMeshCurvatureResult.has_value());
    EXPECT_TRUE(model.Processing.LastMeshCurvatureResult->Succeeded());
    EXPECT_EQ(model.Processing.LastMeshCurvatureResult->ScalarWrittenCount, 8u);
}

TEST(SandboxEditorUi, MeshCurvatureRequestQueuesDerivedJobAndPublishesOnApply)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;
    Runtime::StreamingExecutor executor{};
    Runtime::DerivedJobRegistry jobs{executor};
    AttachDerivedJobCommands(context, jobs);
    std::optional<Runtime::SandboxEditorMeshCurvatureResult> completedResult{};
    context.MethodResultSinks.MeshCurvature =
        [&completedResult](Runtime::SandboxEditorMeshCurvatureResult result)
        {
            completedResult = std::move(result);
        };

    const ECS::EntityHandle mesh = MakeSelectable(registry, "QueuedCurvature");
    AddDenoiseTetraMeshSource(registry, mesh);
    auto& properties = registry.Raw().get<GS::Vertices>(mesh).Properties;
    ASSERT_FALSE(properties.Exists(PN::kMeanCurvature));
    ASSERT_FALSE(properties.Exists(PN::kGaussianCurvature));
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);

    const Runtime::SandboxEditorMeshCurvatureResult result =
        Runtime::ApplySandboxEditorMeshCurvatureCommand(
            context,
            Runtime::SandboxEditorMeshCurvatureCommand{
                .StableEntityId = stableId,
                .Output = Runtime::SandboxEditorMeshCurvatureOutput::All,
                .PublishPrincipalDirections = true,
            });

    EXPECT_EQ(result.Status, Runtime::SandboxEditorCommandStatus::Pending);
    EXPECT_EQ(result.VertexSlotCount, 4u);
    EXPECT_TRUE(result.DirectionsRequested);
    EXPECT_TRUE(result.DirectionsAvailable);
    EXPECT_NE(result.Message.find("queued"), std::string::npos);
    EXPECT_FALSE(properties.Exists(PN::kMeanCurvature));
    EXPECT_FALSE(properties.Exists(PN::kGaussianCurvature));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));

    Runtime::DerivedJobQueueSnapshot queued = jobs.SnapshotAll();
    ASSERT_EQ(queued.Entries.size(), 1u);
    EXPECT_EQ(queued.Entries[0].Name, "Sandbox.MeshCurvature.CPU");
    EXPECT_EQ(queued.Entries[0].Status, Runtime::DerivedJobStatus::Queued);

    jobs.Pump(1u);
    jobs.DrainCompletions();
    EXPECT_FALSE(completedResult.has_value());
    EXPECT_FALSE(properties.Exists(PN::kMeanCurvature));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));

    EXPECT_EQ(jobs.ApplyMainThreadResults(1u), 1u);
    Runtime::DerivedJobQueueSnapshot done = jobs.SnapshotAll();
    ASSERT_EQ(done.Entries.size(), 1u);
    EXPECT_EQ(done.Entries[0].Status, Runtime::DerivedJobStatus::Complete);
    ASSERT_TRUE(completedResult.has_value());
    EXPECT_TRUE(completedResult->Succeeded()) << completedResult->Message;
    EXPECT_EQ(completedResult->VertexSlotCount, 4u);
    EXPECT_EQ(completedResult->ScalarPropertyCount, 2u);
    EXPECT_EQ(completedResult->ScalarWrittenCount, 8u);
    EXPECT_EQ(completedResult->DirectionPropertyCount, 2u);
    EXPECT_EQ(completedResult->DirectionWrittenCount, 8u);
    EXPECT_TRUE(completedResult->DirectionsPublished);
    EXPECT_TRUE(properties.Get<double>(PN::kMeanCurvature).IsValid());
    EXPECT_TRUE(properties.Get<double>(PN::kGaussianCurvature).IsValid());
    EXPECT_TRUE(properties.Get<glm::vec3>(PN::kPrincipalDir1).IsValid());
    EXPECT_TRUE(properties.Get<glm::vec3>(PN::kPrincipalDir2).IsValid());
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));
    EXPECT_TRUE(history.IsDirty());
    ASSERT_TRUE(history.CanUndo());
    EXPECT_EQ(history.Undo().Status,
              Runtime::EditorCommandHistoryStatus::Undone);
    EXPECT_FALSE(properties.Exists(PN::kMeanCurvature));
    EXPECT_FALSE(properties.Exists(PN::kGaussianCurvature));
    EXPECT_FALSE(properties.Exists(PN::kPrincipalDir1));
    EXPECT_FALSE(properties.Exists(PN::kPrincipalDir2));
}

TEST(SandboxEditorUi, MeshCurvatureDerivedJobDiscardsStalePropertiesBeforeApply)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    Runtime::StreamingExecutor executor{};
    Runtime::DerivedJobRegistry jobs{executor};
    AttachDerivedJobCommands(context, jobs);
    bool completedSinkCalled = false;
    context.MethodResultSinks.MeshCurvature =
        [&completedSinkCalled](Runtime::SandboxEditorMeshCurvatureResult)
        {
            completedSinkCalled = true;
        };

    const ECS::EntityHandle mesh =
        MakeSelectable(registry, "StaleCurvatureProperties");
    AddDenoiseTetraMeshSource(registry, mesh);
    auto& properties = registry.Raw().get<GS::Vertices>(mesh).Properties;
    auto mean = properties.GetOrAdd<double>(
        std::string{PN::kMeanCurvature},
        0.0);
    ASSERT_TRUE(mean.IsValid());
    ASSERT_EQ(mean.Vector().size(), 4u);
    for (double& value : mean.Vector())
        value = 1.0;

    const Runtime::SandboxEditorMeshCurvatureResult result =
        Runtime::ApplySandboxEditorMeshCurvatureCommand(
            context,
            Runtime::SandboxEditorMeshCurvatureCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(mesh),
                .Output = Runtime::SandboxEditorMeshCurvatureOutput::All,
                .PublishPrincipalDirections = true,
            });
    ASSERT_EQ(result.Status, Runtime::SandboxEditorCommandStatus::Pending);

    auto currentMean = properties.Get<double>(PN::kMeanCurvature);
    ASSERT_TRUE(currentMean.IsValid());
    for (double& value : currentMean.Vector())
        value = 2.0;

    jobs.Pump(1u);
    jobs.DrainCompletions();
    EXPECT_EQ(jobs.ApplyMainThreadResults(1u), 1u);

    Runtime::DerivedJobQueueSnapshot done = jobs.SnapshotAll();
    ASSERT_EQ(done.Entries.size(), 1u);
    EXPECT_EQ(done.Entries[0].Status,
              Runtime::DerivedJobStatus::StaleDiscarded);
    EXPECT_NE(done.Entries[0].Diagnostic.find(
                  "StaleSourcePropertyGeneration"),
              std::string::npos);
    EXPECT_FALSE(completedSinkCalled);
    currentMean = properties.Get<double>(PN::kMeanCurvature);
    ASSERT_TRUE(currentMean.IsValid());
    for (const double value : currentMean.Vector())
        EXPECT_DOUBLE_EQ(value, 2.0);
    EXPECT_FALSE(properties.Exists(PN::kGaussianCurvature));
    EXPECT_FALSE(properties.Exists(PN::kPrincipalDir1));
    EXPECT_FALSE(properties.Exists(PN::kPrincipalDir2));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));
}

TEST(SandboxEditorUi, MeshCurvatureCommandFallsBackToScalarOnlyWhenDirectionsUnavailable)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.MeshCurvatureDirectionsAvailable = false;

    const ECS::EntityHandle mesh =
        MakeSelectable(registry, "CurvatureScalarOnlyMesh");
    AddDenoiseTetraMeshSource(registry, mesh);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));

    const Runtime::SandboxEditorMeshCurvatureResult result =
        Runtime::ApplySandboxEditorMeshCurvatureCommand(
            context,
            Runtime::SandboxEditorMeshCurvatureCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(mesh),
                .Output = Runtime::SandboxEditorMeshCurvatureOutput::All,
                .PublishPrincipalDirections = true,
            });

    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_TRUE(result.DirectionsRequested);
    EXPECT_FALSE(result.DirectionsAvailable);
    EXPECT_FALSE(result.DirectionsPublished);
    EXPECT_EQ(result.ScalarPropertyCount, 2u);
    EXPECT_EQ(result.ScalarWrittenCount, 8u);
    EXPECT_EQ(result.DirectionWrittenCount, 0u);
    EXPECT_NE(result.Message.find("not published"), std::string::npos);

    auto& properties = registry.Raw().get<GS::Vertices>(mesh).Properties;
    EXPECT_TRUE(properties.Get<double>(PN::kMeanCurvature).IsValid());
    EXPECT_TRUE(properties.Get<double>(PN::kGaussianCurvature).IsValid());
    EXPECT_FALSE(properties.Exists(PN::kPrincipalDir1));
    EXPECT_FALSE(properties.Exists(PN::kPrincipalDir2));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::GpuDirty>(mesh));

    const Runtime::SandboxEditorDomainWindowModel model =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::Mesh);
    EXPECT_TRUE(model.Processing.MeshCurvatureAvailable);
    EXPECT_FALSE(model.Processing.MeshCurvatureDirectionsAvailable);
}

TEST(SandboxEditorUi, MeshCurvatureCommandFailsClosedForInvalidTargetsAndConflicts)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);

    const Runtime::SandboxEditorMeshCurvatureResult missingScene =
        Runtime::ApplySandboxEditorMeshCurvatureCommand(
            Runtime::SandboxEditorContext{},
            Runtime::SandboxEditorMeshCurvatureCommand{
                .StableEntityId = 1u,
            });
    EXPECT_EQ(missingScene.Status,
              Runtime::SandboxEditorCommandStatus::MissingScene);
    EXPECT_EQ(missingScene.Error, Core::ErrorCode::InvalidState);

    const ECS::EntityHandle cloud =
        MakeSelectable(registry, "CurvatureWrongDomain");
    AddPointCloudSource(registry, cloud, 3u);
    const Runtime::SandboxEditorMeshCurvatureResult wrongDomain =
        Runtime::ApplySandboxEditorMeshCurvatureCommand(
            context,
            Runtime::SandboxEditorMeshCurvatureCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(cloud),
            });
    EXPECT_EQ(wrongDomain.Status,
              Runtime::SandboxEditorCommandStatus::UnsupportedGeometryDomain);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(cloud));

    const ECS::EntityHandle mesh =
        MakeSelectable(registry, "CurvatureUnavailableMesh");
    AddDenoiseTetraMeshSource(registry, mesh);
    context.MeshCurvatureKernelAvailable = false;
    const Runtime::SandboxEditorMeshCurvatureResult unavailable =
        Runtime::ApplySandboxEditorMeshCurvatureCommand(
            context,
            Runtime::SandboxEditorMeshCurvatureCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(mesh),
            });
    EXPECT_EQ(unavailable.Status,
              Runtime::SandboxEditorCommandStatus::GeometryProcessingFailed);
    EXPECT_EQ(unavailable.Error, Core::ErrorCode::InvalidState);
    EXPECT_FALSE(registry.Raw().get<GS::Vertices>(mesh).Properties.Exists(
        PN::kMeanCurvature));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));

    context.MeshCurvatureKernelAvailable = true;
    auto& properties = registry.Raw().get<GS::Vertices>(mesh).Properties;
    (void)properties.Add<float>(std::string{PN::kMeanCurvature}, 0.0f);
    const Runtime::SandboxEditorMeshCurvatureResult conflict =
        Runtime::ApplySandboxEditorMeshCurvatureCommand(
            context,
            Runtime::SandboxEditorMeshCurvatureCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(mesh),
            });
    EXPECT_EQ(conflict.Status,
              Runtime::SandboxEditorCommandStatus::GeometryProcessingFailed);
    EXPECT_EQ(conflict.Error, Core::ErrorCode::TypeMismatch);
    EXPECT_FALSE(properties.Exists(PN::kGaussianCurvature));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));
}

TEST(SandboxEditorUi, MeshRemeshCommandReplacesTopologyAndSupportsUndoRedo)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;

    const ECS::EntityHandle mesh = MakeSelectable(registry, "UniformRemesh");
    AddIcosahedronMeshSource(registry, mesh);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);
    const MeshCounts before = SourceMeshCounts(registry, mesh);
    ASSERT_GT(before.Vertices, 0u);
    ASSERT_GT(before.Faces, 0u);

    const Runtime::SandboxEditorMeshRemeshResult uniform =
        Runtime::ApplySandboxEditorMeshRemeshCommand(
            context,
            Runtime::SandboxEditorMeshRemeshCommand{
                .StableEntityId = stableId,
                .Mode = Runtime::SandboxEditorMeshRemeshMode::Uniform,
                .SizingLaw =
                    Runtime::SandboxEditorMeshRemeshSizingLaw::MeanCurvature,
                .Iterations = 1u,
                .TargetEdgeLength = 0.35,
                .PreserveBoundary = false,
                .ProjectToSurface = true,
            });

    ASSERT_TRUE(uniform.Succeeded()) << uniform.Message;
    EXPECT_EQ(uniform.Mode, Runtime::SandboxEditorMeshRemeshMode::Uniform);
    EXPECT_TRUE(uniform.ProjectToSurface);
    EXPECT_EQ(uniform.IterationsRequested, 1u);
    EXPECT_EQ(uniform.IterationsPerformed, 1u);
    EXPECT_EQ(uniform.InputVertexCount, before.Vertices);
    EXPECT_EQ(uniform.InputFaceCount, before.Faces);
    EXPECT_GT(uniform.OutputVertexCount, before.Vertices);
    EXPECT_GT(uniform.OutputFaceCount, before.Faces);
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(mesh));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyEdgeTopology>(mesh));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyFaceTopology>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::GpuDirty>(mesh));
    EXPECT_TRUE(history.IsDirty());
    EXPECT_TRUE(history.CanUndo());

    const MeshCounts afterUniform = SourceMeshCounts(registry, mesh);
    EXPECT_EQ(afterUniform.Vertices, uniform.OutputVertexCount);
    EXPECT_EQ(afterUniform.Faces, uniform.OutputFaceCount);
    EXPECT_EQ(history.Undo().Status,
              Runtime::EditorCommandHistoryStatus::Undone);
    ExpectMeshCountsEqual(SourceMeshCounts(registry, mesh), before);
    EXPECT_EQ(history.Redo().Status,
              Runtime::EditorCommandHistoryStatus::Redone);
    ExpectMeshCountsEqual(SourceMeshCounts(registry, mesh), afterUniform);

    context.LastMeshRemeshResult = &uniform;
    const Runtime::SandboxEditorDomainWindowModel model =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::Mesh);
    EXPECT_TRUE(model.Processing.MeshRemeshAvailable);
    ASSERT_TRUE(model.Processing.LastMeshRemeshResult.has_value());
    EXPECT_TRUE(model.Processing.LastMeshRemeshResult->Succeeded());
    EXPECT_EQ(model.Processing.LastMeshRemeshResult->OutputFaceCount,
              uniform.OutputFaceCount);

    const ECS::EntityHandle adaptiveMesh =
        MakeSelectable(registry, "AdaptiveRemesh");
    AddIcosahedronMeshSource(registry, adaptiveMesh);
    const Runtime::SandboxEditorMeshRemeshResult adaptive =
        Runtime::ApplySandboxEditorMeshRemeshCommand(
            context,
            Runtime::SandboxEditorMeshRemeshCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(adaptiveMesh),
                .Mode = Runtime::SandboxEditorMeshRemeshMode::Adaptive,
                .SizingLaw =
                    Runtime::SandboxEditorMeshRemeshSizingLaw::ErrorBoundedTaubin,
                .Iterations = 1u,
                .TargetEdgeLength = 0.35,
                .ApproximationError = 0.01,
                .PreserveBoundary = false,
                .ProjectToSurface = false,
            });
    ASSERT_TRUE(adaptive.Succeeded()) << adaptive.Message;
    EXPECT_EQ(adaptive.Mode, Runtime::SandboxEditorMeshRemeshMode::Adaptive);
    EXPECT_EQ(adaptive.SizingLaw,
              Runtime::SandboxEditorMeshRemeshSizingLaw::ErrorBoundedTaubin);
    EXPECT_EQ(adaptive.IterationsPerformed, 1u);
    EXPECT_GT(adaptive.OutputVertexCount, 0u);
    EXPECT_GT(adaptive.OutputFaceCount, 0u);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::GpuDirty>(adaptiveMesh));
}

TEST(SandboxEditorUi, MeshRemeshRequestQueuesDerivedJobAndPublishesOnApply)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;
    Runtime::StreamingExecutor executor{};
    Runtime::DerivedJobRegistry jobs{executor};
    AttachDerivedJobCommands(context, jobs);
    std::optional<Runtime::SandboxEditorMeshRemeshResult> completedResult{};
    context.MethodResultSinks.MeshRemesh =
        [&completedResult](Runtime::SandboxEditorMeshRemeshResult result)
        {
            completedResult = std::move(result);
        };

    const ECS::EntityHandle mesh = MakeSelectable(registry, "QueuedRemesh");
    AddIcosahedronMeshSource(registry, mesh);
    const MeshCounts before = SourceMeshCounts(registry, mesh);
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);

    const Runtime::SandboxEditorMeshRemeshResult result =
        Runtime::ApplySandboxEditorMeshRemeshCommand(
            context,
            Runtime::SandboxEditorMeshRemeshCommand{
                .StableEntityId = stableId,
                .Mode = Runtime::SandboxEditorMeshRemeshMode::Uniform,
                .SizingLaw =
                    Runtime::SandboxEditorMeshRemeshSizingLaw::MeanCurvature,
                .Iterations = 1u,
                .TargetEdgeLength = 0.35,
                .PreserveBoundary = false,
                .ProjectToSurface = false,
            });

    EXPECT_EQ(result.Status, Runtime::SandboxEditorCommandStatus::Pending);
    EXPECT_EQ(result.InputVertexCount, before.Vertices);
    EXPECT_EQ(result.InputFaceCount, before.Faces);
    ExpectMeshCountsEqual(SourceMeshCounts(registry, mesh), before);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyEdgeTopology>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyFaceTopology>(mesh));

    Runtime::DerivedJobQueueSnapshot queued = jobs.SnapshotAll();
    ASSERT_EQ(queued.Entries.size(), 1u);
    EXPECT_EQ(queued.Entries[0].Name, "Sandbox.MeshRemesh.CPU");
    EXPECT_EQ(queued.Entries[0].Status, Runtime::DerivedJobStatus::Queued);

    jobs.Pump(1u);
    jobs.DrainCompletions();
    ExpectMeshCountsEqual(SourceMeshCounts(registry, mesh), before);
    EXPECT_FALSE(completedResult.has_value());

    EXPECT_EQ(jobs.ApplyMainThreadResults(1u), 1u);
    Runtime::DerivedJobQueueSnapshot done = jobs.SnapshotAll();
    ASSERT_EQ(done.Entries.size(), 1u);
    EXPECT_EQ(done.Entries[0].Status, Runtime::DerivedJobStatus::Complete);
    ASSERT_TRUE(completedResult.has_value());
    EXPECT_TRUE(completedResult->Succeeded()) << completedResult->Message;
    EXPECT_EQ(completedResult->InputFaceCount, before.Faces);
    EXPECT_GT(completedResult->OutputFaceCount, before.Faces);
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyEdgeTopology>(mesh));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyFaceTopology>(mesh));
    EXPECT_TRUE(history.IsDirty());
    EXPECT_EQ(SourceMeshCounts(registry, mesh).Faces,
              completedResult->OutputFaceCount);
}

TEST(SandboxEditorUi, MeshSubdivideCommandReplacesTopologyForAllOperatorsAndSupportsUndoRedo)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;

    const ECS::EntityHandle loopMesh = MakeSelectable(registry, "LoopSubdivide");
    AddDenoiseTetraMeshSource(registry, loopMesh);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, loopMesh));
    const MeshCounts beforeLoop = SourceMeshCounts(registry, loopMesh);
    const std::uint32_t loopStableId =
        Runtime::SelectionController::ToStableEntityId(loopMesh);

    const Runtime::SandboxEditorMeshSubdivideResult loop =
        Runtime::ApplySandboxEditorMeshSubdivideCommand(
            context,
            Runtime::SandboxEditorMeshSubdivideCommand{
                .StableEntityId = loopStableId,
                .Operator = Runtime::SandboxEditorMeshSubdivideOperator::Loop,
                .Iterations = 1u,
                .PreserveLoopFeatureEdges = true,
            });

    ASSERT_TRUE(loop.Succeeded()) << loop.Message;
    EXPECT_EQ(loop.Operator,
              Runtime::SandboxEditorMeshSubdivideOperator::Loop);
    EXPECT_TRUE(loop.PreserveLoopFeatureEdges);
    EXPECT_EQ(loop.IterationsPerformed, 1u);
    EXPECT_EQ(loop.InputVertexCount, beforeLoop.Vertices);
    EXPECT_EQ(loop.InputFaceCount, beforeLoop.Faces);
    EXPECT_GT(loop.OutputVertexCount, beforeLoop.Vertices);
    EXPECT_GT(loop.OutputFaceCount, beforeLoop.Faces);
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(loopMesh));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(loopMesh));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyEdgeTopology>(loopMesh));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyFaceTopology>(loopMesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::GpuDirty>(loopMesh));
    EXPECT_TRUE(history.IsDirty());

    const MeshCounts afterLoop = SourceMeshCounts(registry, loopMesh);
    EXPECT_EQ(history.Undo().Status,
              Runtime::EditorCommandHistoryStatus::Undone);
    ExpectMeshCountsEqual(SourceMeshCounts(registry, loopMesh), beforeLoop);
    EXPECT_EQ(history.Redo().Status,
              Runtime::EditorCommandHistoryStatus::Redone);
    ExpectMeshCountsEqual(SourceMeshCounts(registry, loopMesh), afterLoop);

    context.LastMeshSubdivideResult = &loop;
    const Runtime::SandboxEditorDomainWindowModel model =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::Mesh);
    EXPECT_TRUE(model.Processing.MeshSubdivideAvailable);
    ASSERT_TRUE(model.Processing.LastMeshSubdivideResult.has_value());
    EXPECT_TRUE(model.Processing.LastMeshSubdivideResult->Succeeded());
    EXPECT_EQ(model.Processing.LastMeshSubdivideResult->OutputFaceCount,
              loop.OutputFaceCount);

    const ECS::EntityHandle catmullMesh =
        MakeSelectable(registry, "CatmullClarkSubdivide");
    AddDenoiseTetraMeshSource(registry, catmullMesh);
    const Runtime::SandboxEditorMeshSubdivideResult catmull =
        Runtime::ApplySandboxEditorMeshSubdivideCommand(
            context,
            Runtime::SandboxEditorMeshSubdivideCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(catmullMesh),
                .Operator =
                    Runtime::SandboxEditorMeshSubdivideOperator::CatmullClark,
                .Iterations = 1u,
            });
    ASSERT_TRUE(catmull.Succeeded()) << catmull.Message;
    EXPECT_EQ(catmull.Operator,
              Runtime::SandboxEditorMeshSubdivideOperator::CatmullClark);
    EXPECT_GT(catmull.OutputVertexCount, catmull.InputVertexCount);
    EXPECT_GT(catmull.OutputFaceCount, catmull.InputFaceCount);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::GpuDirty>(catmullMesh));

    const ECS::EntityHandle sqrt3Mesh =
        MakeSelectable(registry, "Sqrt3Subdivide");
    AddDenoiseTetraMeshSource(registry, sqrt3Mesh);
    const Runtime::SandboxEditorMeshSubdivideResult sqrt3 =
        Runtime::ApplySandboxEditorMeshSubdivideCommand(
            context,
            Runtime::SandboxEditorMeshSubdivideCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(sqrt3Mesh),
                .Operator = Runtime::SandboxEditorMeshSubdivideOperator::Sqrt3,
                .Iterations = 1u,
            });
    ASSERT_TRUE(sqrt3.Succeeded()) << sqrt3.Message;
    EXPECT_EQ(sqrt3.Operator,
              Runtime::SandboxEditorMeshSubdivideOperator::Sqrt3);
    EXPECT_GT(sqrt3.OutputVertexCount, sqrt3.InputVertexCount);
    EXPECT_GT(sqrt3.OutputFaceCount, sqrt3.InputFaceCount);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::GpuDirty>(sqrt3Mesh));
}

TEST(SandboxEditorUi, MeshSubdivideRequestQueuesDerivedJobAndPublishesOnApply)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;
    Runtime::StreamingExecutor executor{};
    Runtime::DerivedJobRegistry jobs{executor};
    AttachDerivedJobCommands(context, jobs);
    std::optional<Runtime::SandboxEditorMeshSubdivideResult> completedResult{};
    context.MethodResultSinks.MeshSubdivide =
        [&completedResult](Runtime::SandboxEditorMeshSubdivideResult result)
        {
            completedResult = std::move(result);
        };

    const ECS::EntityHandle mesh = MakeSelectable(registry, "QueuedSubdivide");
    AddDenoiseTetraMeshSource(registry, mesh);
    const MeshCounts before = SourceMeshCounts(registry, mesh);
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);

    const Runtime::SandboxEditorMeshSubdivideResult result =
        Runtime::ApplySandboxEditorMeshSubdivideCommand(
            context,
            Runtime::SandboxEditorMeshSubdivideCommand{
                .StableEntityId = stableId,
                .Operator = Runtime::SandboxEditorMeshSubdivideOperator::Loop,
                .Iterations = 1u,
            });

    EXPECT_EQ(result.Status, Runtime::SandboxEditorCommandStatus::Pending);
    EXPECT_EQ(result.InputVertexCount, before.Vertices);
    EXPECT_EQ(result.InputFaceCount, before.Faces);
    EXPECT_NE(result.Message.find("queued"), std::string::npos);
    ExpectMeshCountsEqual(SourceMeshCounts(registry, mesh), before);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyEdgeTopology>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyFaceTopology>(mesh));

    Runtime::DerivedJobQueueSnapshot queued = jobs.SnapshotAll();
    ASSERT_EQ(queued.Entries.size(), 1u);
    EXPECT_EQ(queued.Entries[0].Name, "Sandbox.MeshSubdivide.CPU");
    EXPECT_EQ(queued.Entries[0].Status, Runtime::DerivedJobStatus::Queued);

    jobs.Pump(1u);
    jobs.DrainCompletions();
    EXPECT_FALSE(completedResult.has_value());
    ExpectMeshCountsEqual(SourceMeshCounts(registry, mesh), before);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyEdgeTopology>(mesh));

    EXPECT_EQ(jobs.ApplyMainThreadResults(1u), 1u);
    Runtime::DerivedJobQueueSnapshot done = jobs.SnapshotAll();
    ASSERT_EQ(done.Entries.size(), 1u);
    EXPECT_EQ(done.Entries[0].Status, Runtime::DerivedJobStatus::Complete);
    ASSERT_TRUE(completedResult.has_value());
    EXPECT_TRUE(completedResult->Succeeded()) << completedResult->Message;
    EXPECT_EQ(completedResult->InputFaceCount, before.Faces);
    EXPECT_GT(completedResult->OutputFaceCount, before.Faces);
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(mesh));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyEdgeTopology>(mesh));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyFaceTopology>(mesh));
    EXPECT_TRUE(history.IsDirty());
    EXPECT_EQ(SourceMeshCounts(registry, mesh).Faces,
              completedResult->OutputFaceCount);
    ASSERT_TRUE(history.CanUndo());
    EXPECT_EQ(history.Undo().Status,
              Runtime::EditorCommandHistoryStatus::Undone);
    ExpectMeshCountsEqual(SourceMeshCounts(registry, mesh), before);
}

TEST(SandboxEditorUi, MeshSubdivideDerivedJobDiscardsStaleMeshBeforeApply)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    Runtime::StreamingExecutor executor{};
    Runtime::DerivedJobRegistry jobs{executor};
    AttachDerivedJobCommands(context, jobs);
    bool completedSinkCalled = false;
    context.MethodResultSinks.MeshSubdivide =
        [&completedSinkCalled](Runtime::SandboxEditorMeshSubdivideResult)
        {
            completedSinkCalled = true;
        };

    const ECS::EntityHandle mesh = MakeSelectable(registry, "StaleSubdivide");
    AddDenoiseTetraMeshSource(registry, mesh);
    const MeshCounts before = SourceMeshCounts(registry, mesh);
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);

    const Runtime::SandboxEditorMeshSubdivideResult result =
        Runtime::ApplySandboxEditorMeshSubdivideCommand(
            context,
            Runtime::SandboxEditorMeshSubdivideCommand{
                .StableEntityId = stableId,
                .Operator = Runtime::SandboxEditorMeshSubdivideOperator::Loop,
                .Iterations = 1u,
            });
    ASSERT_EQ(result.Status, Runtime::SandboxEditorCommandStatus::Pending);

    const std::vector<glm::vec3> stalePositions{
        glm::vec3{2.0f, 0.0f, 0.0f},
        glm::vec3{0.0f, 2.0f, 0.0f},
        glm::vec3{0.0f, 0.0f, 2.0f},
        glm::vec3{2.0f, 2.0f, 2.0f},
    };
    SetPositions(registry.Raw().get<GS::Vertices>(mesh), stalePositions);

    jobs.Pump(1u);
    jobs.DrainCompletions();
    EXPECT_EQ(jobs.ApplyMainThreadResults(1u), 1u);

    Runtime::DerivedJobQueueSnapshot done = jobs.SnapshotAll();
    ASSERT_EQ(done.Entries.size(), 1u);
    EXPECT_EQ(done.Entries[0].Status,
              Runtime::DerivedJobStatus::StaleDiscarded);
    EXPECT_NE(done.Entries[0].Diagnostic.find(
                  "StaleSourcePropertyGeneration"),
              std::string::npos);
    EXPECT_FALSE(completedSinkCalled);
    ExpectMeshCountsEqual(SourceMeshCounts(registry, mesh), before);
    ExpectPositionsExactlyEqual(MeshVertexPositions(registry, mesh),
                                stalePositions);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyEdgeTopology>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyFaceTopology>(mesh));
}

TEST(SandboxEditorUi, MeshSimplifyCommandReducesFaceCountAndSupportsUndoRedo)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;

    const ECS::EntityHandle mesh = MakeSelectable(registry, "SimplifyMesh");
    AddIcosahedronMeshSource(registry, mesh);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);
    const MeshCounts before = SourceMeshCounts(registry, mesh);
    ASSERT_GT(before.Faces, 12u);

    const Runtime::SandboxEditorMeshSimplifyResult simplified =
        Runtime::ApplySandboxEditorMeshSimplifyCommand(
            context,
            Runtime::SandboxEditorMeshSimplifyCommand{
                .StableEntityId = stableId,
                .Metric = Runtime::SandboxEditorMeshSimplifyMetric::FA_QEM,
                .TargetFaces = 12u,
                .PreserveBoundary = false,
            });

    ASSERT_TRUE(simplified.Succeeded()) << simplified.Message;
    EXPECT_EQ(simplified.Metric,
              Runtime::SandboxEditorMeshSimplifyMetric::FA_QEM);
    EXPECT_EQ(simplified.TargetFaces, 12u);
    EXPECT_EQ(simplified.InputVertexCount, before.Vertices);
    EXPECT_EQ(simplified.InputFaceCount, before.Faces);
    EXPECT_LT(simplified.OutputFaceCount, before.Faces);
    EXPECT_GT(simplified.CollapseCount, 0u);
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(mesh));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyEdgeTopology>(mesh));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyFaceTopology>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::GpuDirty>(mesh));
    EXPECT_TRUE(history.IsDirty());
    EXPECT_TRUE(history.CanUndo());

    const MeshCounts afterSimplify = SourceMeshCounts(registry, mesh);
    EXPECT_EQ(afterSimplify.Vertices, simplified.OutputVertexCount);
    EXPECT_EQ(afterSimplify.Faces, simplified.OutputFaceCount);
    EXPECT_EQ(history.Undo().Status,
              Runtime::EditorCommandHistoryStatus::Undone);
    ExpectMeshCountsEqual(SourceMeshCounts(registry, mesh), before);
    EXPECT_EQ(history.Redo().Status,
              Runtime::EditorCommandHistoryStatus::Redone);
    ExpectMeshCountsEqual(SourceMeshCounts(registry, mesh), afterSimplify);

    context.LastMeshSimplifyResult = &simplified;
    const Runtime::SandboxEditorDomainWindowModel model =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::Mesh);
    EXPECT_TRUE(model.Processing.MeshSimplifyAvailable);
    ASSERT_TRUE(model.Processing.LastMeshSimplifyResult.has_value());
    EXPECT_TRUE(model.Processing.LastMeshSimplifyResult->Succeeded());
    EXPECT_EQ(model.Processing.LastMeshSimplifyResult->OutputFaceCount,
              simplified.OutputFaceCount);

    const ECS::EntityHandle classicalMesh =
        MakeSelectable(registry, "SimplifyClassical");
    AddIcosahedronMeshSource(registry, classicalMesh);
    const Runtime::SandboxEditorMeshSimplifyResult classical =
        Runtime::ApplySandboxEditorMeshSimplifyCommand(
            context,
            Runtime::SandboxEditorMeshSimplifyCommand{
                .StableEntityId = Runtime::SelectionController::ToStableEntityId(
                    classicalMesh),
                .Metric =
                    Runtime::SandboxEditorMeshSimplifyMetric::ClassicalQEM,
                .TargetFaces = 12u,
                .PreserveBoundary = false,
            });
    ASSERT_TRUE(classical.Succeeded()) << classical.Message;
    EXPECT_EQ(classical.Metric,
              Runtime::SandboxEditorMeshSimplifyMetric::ClassicalQEM);
    EXPECT_LT(classical.OutputFaceCount, before.Faces);
    EXPECT_EQ(classical.SharpFeatureVerticesPinned, 0u);
    EXPECT_EQ(classical.SeamVerticesPinned, 0u);
}

TEST(SandboxEditorUi, MeshSimplifyRequestQueuesDerivedJobAndPublishesOnApply)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;
    Runtime::StreamingExecutor executor{};
    Runtime::DerivedJobRegistry jobs{executor};
    AttachDerivedJobCommands(context, jobs);
    std::optional<Runtime::SandboxEditorMeshSimplifyResult> completedResult{};
    context.MethodResultSinks.MeshSimplify =
        [&completedResult](Runtime::SandboxEditorMeshSimplifyResult result)
        {
            completedResult = std::move(result);
        };

    const ECS::EntityHandle mesh = MakeSelectable(registry, "QueuedSimplify");
    AddIcosahedronMeshSource(registry, mesh);
    const MeshCounts before = SourceMeshCounts(registry, mesh);
    ASSERT_GT(before.Faces, 12u);
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);

    const Runtime::SandboxEditorMeshSimplifyResult result =
        Runtime::ApplySandboxEditorMeshSimplifyCommand(
            context,
            Runtime::SandboxEditorMeshSimplifyCommand{
                .StableEntityId = stableId,
                .Metric = Runtime::SandboxEditorMeshSimplifyMetric::FA_QEM,
                .TargetFaces = 12u,
                .PreserveBoundary = false,
            });

    EXPECT_EQ(result.Status, Runtime::SandboxEditorCommandStatus::Pending);
    EXPECT_EQ(result.InputVertexCount, before.Vertices);
    EXPECT_EQ(result.InputFaceCount, before.Faces);
    ExpectMeshCountsEqual(SourceMeshCounts(registry, mesh), before);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyEdgeTopology>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyFaceTopology>(mesh));

    Runtime::DerivedJobQueueSnapshot queued = jobs.SnapshotAll();
    ASSERT_EQ(queued.Entries.size(), 1u);
    EXPECT_EQ(queued.Entries[0].Name, "Sandbox.MeshSimplify.CPU");
    EXPECT_EQ(queued.Entries[0].Status, Runtime::DerivedJobStatus::Queued);

    jobs.Pump(1u);
    jobs.DrainCompletions();
    ExpectMeshCountsEqual(SourceMeshCounts(registry, mesh), before);
    EXPECT_FALSE(completedResult.has_value());

    EXPECT_EQ(jobs.ApplyMainThreadResults(1u), 1u);
    Runtime::DerivedJobQueueSnapshot done = jobs.SnapshotAll();
    ASSERT_EQ(done.Entries.size(), 1u);
    EXPECT_EQ(done.Entries[0].Status, Runtime::DerivedJobStatus::Complete);
    ASSERT_TRUE(completedResult.has_value());
    EXPECT_TRUE(completedResult->Succeeded()) << completedResult->Message;
    EXPECT_EQ(completedResult->InputFaceCount, before.Faces);
    EXPECT_LT(completedResult->OutputFaceCount, before.Faces);
    EXPECT_GT(completedResult->CollapseCount, 0u);
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyEdgeTopology>(mesh));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyFaceTopology>(mesh));
    EXPECT_TRUE(history.IsDirty());
    EXPECT_EQ(SourceMeshCounts(registry, mesh).Faces,
              completedResult->OutputFaceCount);
}

TEST(SandboxEditorUi, MeshSimplifyCommandFailsClosedForInvalidTargetsAndUnavailableKernel)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);

    const ECS::EntityHandle mesh = MakeSelectable(registry, "SimplifyGuard");
    AddIcosahedronMeshSource(registry, mesh);
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);

    const Runtime::SandboxEditorMeshSimplifyResult invalid =
        Runtime::ApplySandboxEditorMeshSimplifyCommand(
            context,
            Runtime::SandboxEditorMeshSimplifyCommand{
                .StableEntityId = stableId,
                .TargetFaces = 0u,
                .MaxError = 0.0,
            });
    EXPECT_EQ(invalid.Status,
              Runtime::SandboxEditorCommandStatus::InvalidProcessingParameters);
    EXPECT_FALSE(invalid.Succeeded());

    const Runtime::SandboxEditorMeshSimplifyResult stale =
        Runtime::ApplySandboxEditorMeshSimplifyCommand(
            context,
            Runtime::SandboxEditorMeshSimplifyCommand{
                .StableEntityId = stableId + 4242u,
                .TargetFaces = 8u,
            });
    EXPECT_EQ(stale.Status, Runtime::SandboxEditorCommandStatus::StaleEntity);

    context.MeshSimplifyKernelAvailable = false;
    const Runtime::SandboxEditorMeshSimplifyResult unavailable =
        Runtime::ApplySandboxEditorMeshSimplifyCommand(
            context,
            Runtime::SandboxEditorMeshSimplifyCommand{
                .StableEntityId = stableId,
                .TargetFaces = 8u,
            });
    EXPECT_EQ(unavailable.Status,
              Runtime::SandboxEditorCommandStatus::GeometryProcessingFailed);
}

TEST(SandboxEditorUi, MeshSimplifyPreservesUvSeamsWhenTexcoordsPresent)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);

    constexpr int kGrid = 4;
    Geometry::HalfedgeMesh::Mesh grid = MakeGridPlaneMesh(kGrid);
    const ECS::EntityHandle mesh = MakeSelectable(registry, "TexturedGrid");
    GS::PopulateFromMesh(registry.Raw(), mesh, grid);
    registry.Raw().emplace<G::RenderSurface>(mesh);
    // The GeometrySources must carry the texcoords the command forwards into the
    // scratch halfedge mesh so FA_QEM can pin the boundary UV-seam vertices.
    SetTexcoords(registry.Raw().get<GS::Vertices>(mesh),
                 GridPlaneTexcoords(kGrid));

    const MeshCounts before = SourceMeshCounts(registry, mesh);
    ASSERT_GT(before.Faces, 4u);
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);

    const Runtime::SandboxEditorMeshSimplifyResult result =
        Runtime::ApplySandboxEditorMeshSimplifyCommand(
            context,
            Runtime::SandboxEditorMeshSimplifyCommand{
                .StableEntityId = stableId,
                .Metric = Runtime::SandboxEditorMeshSimplifyMetric::FA_QEM,
                .TargetFaces = 4u,
                .PreserveBoundary = false,  // seams pinned by PreserveUvSeams
                .PreserveSharpFeatures = true,
                .PreserveUvSeams = true,
            });

    ASSERT_TRUE(result.Succeeded()) << result.Message;
    // Without forwarding v:texcoord the scratch mesh carries no texcoord and
    // SeamVerticesPinned would be 0; the fix forwards it so the boundary UV-seam
    // vertices are pinned.
    EXPECT_GT(result.SeamVerticesPinned, 0u);
    EXPECT_LT(result.OutputFaceCount, before.Faces);
}

TEST(SandboxEditorUi, MeshTopologyProcessingCommandsFailClosedForInvalidTargetsAndUnavailableKernels)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);

    const Runtime::SandboxEditorMeshRemeshResult missingRemeshScene =
        Runtime::ApplySandboxEditorMeshRemeshCommand(
            Runtime::SandboxEditorContext{},
            Runtime::SandboxEditorMeshRemeshCommand{
                .StableEntityId = 1u,
            });
    EXPECT_EQ(missingRemeshScene.Status,
              Runtime::SandboxEditorCommandStatus::MissingScene);
    EXPECT_EQ(missingRemeshScene.Error, Core::ErrorCode::InvalidState);

    const Runtime::SandboxEditorMeshSubdivideResult missingSubdivideScene =
        Runtime::ApplySandboxEditorMeshSubdivideCommand(
            Runtime::SandboxEditorContext{},
            Runtime::SandboxEditorMeshSubdivideCommand{
                .StableEntityId = 1u,
            });
    EXPECT_EQ(missingSubdivideScene.Status,
              Runtime::SandboxEditorCommandStatus::MissingScene);
    EXPECT_EQ(missingSubdivideScene.Error, Core::ErrorCode::InvalidState);

    const ECS::EntityHandle cloud =
        MakeSelectable(registry, "TopologyWrongDomain");
    AddPointCloudSource(registry, cloud, 3u);
    const std::uint32_t cloudStableId =
        Runtime::SelectionController::ToStableEntityId(cloud);
    const Runtime::SandboxEditorMeshRemeshResult wrongRemeshDomain =
        Runtime::ApplySandboxEditorMeshRemeshCommand(
            context,
            Runtime::SandboxEditorMeshRemeshCommand{
                .StableEntityId = cloudStableId,
            });
    EXPECT_EQ(wrongRemeshDomain.Status,
              Runtime::SandboxEditorCommandStatus::UnsupportedGeometryDomain);
    const Runtime::SandboxEditorMeshSubdivideResult wrongSubdivideDomain =
        Runtime::ApplySandboxEditorMeshSubdivideCommand(
            context,
            Runtime::SandboxEditorMeshSubdivideCommand{
                .StableEntityId = cloudStableId,
            });
    EXPECT_EQ(wrongSubdivideDomain.Status,
              Runtime::SandboxEditorCommandStatus::UnsupportedGeometryDomain);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyEdgeTopology>(cloud));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyFaceTopology>(cloud));

    const ECS::EntityHandle mesh = MakeSelectable(registry, "TopologyFailMesh");
    AddIcosahedronMeshSource(registry, mesh);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));
    const std::uint32_t meshStableId =
        Runtime::SelectionController::ToStableEntityId(mesh);
    const MeshCounts before = SourceMeshCounts(registry, mesh);

    const Runtime::SandboxEditorMeshRemeshResult invalidRemesh =
        Runtime::ApplySandboxEditorMeshRemeshCommand(
            context,
            Runtime::SandboxEditorMeshRemeshCommand{
                .StableEntityId = meshStableId,
                .Iterations = 0u,
            });
    EXPECT_EQ(invalidRemesh.Status,
              Runtime::SandboxEditorCommandStatus::InvalidProcessingParameters);
    ExpectMeshCountsEqual(SourceMeshCounts(registry, mesh), before);

    const Runtime::SandboxEditorMeshSubdivideResult invalidSubdivide =
        Runtime::ApplySandboxEditorMeshSubdivideCommand(
            context,
            Runtime::SandboxEditorMeshSubdivideCommand{
                .StableEntityId = meshStableId,
                .Iterations = 0u,
            });
    EXPECT_EQ(invalidSubdivide.Status,
              Runtime::SandboxEditorCommandStatus::InvalidProcessingParameters);
    ExpectMeshCountsEqual(SourceMeshCounts(registry, mesh), before);

    context.MeshRemeshAdaptiveKernelAvailable = false;
    const Runtime::SandboxEditorMeshRemeshResult unavailableAdaptive =
        Runtime::ApplySandboxEditorMeshRemeshCommand(
            context,
            Runtime::SandboxEditorMeshRemeshCommand{
                .StableEntityId = meshStableId,
                .Mode = Runtime::SandboxEditorMeshRemeshMode::Adaptive,
                .Iterations = 1u,
                .TargetEdgeLength = 0.35,
            });
    EXPECT_EQ(unavailableAdaptive.Status,
              Runtime::SandboxEditorCommandStatus::GeometryProcessingFailed);
    EXPECT_EQ(unavailableAdaptive.Error, Core::ErrorCode::InvalidState);
    ExpectMeshCountsEqual(SourceMeshCounts(registry, mesh), before);

    context.MeshRemeshAdaptiveKernelAvailable = true;
    context.MeshRemeshErrorBoundedSizingAvailable = false;
    const Runtime::SandboxEditorMeshRemeshResult unavailableSizing =
        Runtime::ApplySandboxEditorMeshRemeshCommand(
            context,
            Runtime::SandboxEditorMeshRemeshCommand{
                .StableEntityId = meshStableId,
                .Mode = Runtime::SandboxEditorMeshRemeshMode::Adaptive,
                .SizingLaw =
                    Runtime::SandboxEditorMeshRemeshSizingLaw::ErrorBoundedTaubin,
                .Iterations = 1u,
                .TargetEdgeLength = 0.35,
                .ApproximationError = 0.01,
            });
    EXPECT_EQ(unavailableSizing.Status,
              Runtime::SandboxEditorCommandStatus::GeometryProcessingFailed);
    ExpectMeshCountsEqual(SourceMeshCounts(registry, mesh), before);

    context.MeshRemeshErrorBoundedSizingAvailable = true;
    context.MeshSubdivideSqrt3KernelAvailable = false;
    const Runtime::SandboxEditorMeshSubdivideResult unavailableSqrt3 =
        Runtime::ApplySandboxEditorMeshSubdivideCommand(
            context,
            Runtime::SandboxEditorMeshSubdivideCommand{
                .StableEntityId = meshStableId,
                .Operator = Runtime::SandboxEditorMeshSubdivideOperator::Sqrt3,
                .Iterations = 1u,
            });
    EXPECT_EQ(unavailableSqrt3.Status,
              Runtime::SandboxEditorCommandStatus::GeometryProcessingFailed);
    EXPECT_EQ(unavailableSqrt3.Error, Core::ErrorCode::InvalidState);
    ExpectMeshCountsEqual(SourceMeshCounts(registry, mesh), before);

    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyEdgeTopology>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyFaceTopology>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::GpuDirty>(mesh));

    context.MeshRemeshUniformKernelAvailable = false;
    context.MeshRemeshAdaptiveKernelAvailable = false;
    context.MeshSubdivideLoopKernelAvailable = false;
    context.MeshSubdivideCatmullClarkKernelAvailable = false;
    const Runtime::SandboxEditorDomainWindowModel unavailableModel =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::Mesh);
    EXPECT_FALSE(unavailableModel.Processing.MeshRemeshAvailable);
    EXPECT_FALSE(unavailableModel.Processing.MeshRemeshUniformAvailable);
    EXPECT_FALSE(unavailableModel.Processing.MeshRemeshAdaptiveAvailable);
    EXPECT_FALSE(unavailableModel.Processing.MeshSubdivideAvailable);
    EXPECT_FALSE(unavailableModel.Processing.MeshSubdivideLoopAvailable);
    EXPECT_FALSE(unavailableModel.Processing.MeshSubdivideCatmullClarkAvailable);
    EXPECT_FALSE(unavailableModel.Processing.MeshSubdivideSqrt3Available);
}

TEST(SandboxEditorUi, MeshVertexNormalsCommandPublishesCanonicalNormalsForAllWeightings)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;

    const ECS::EntityHandle mesh = MakeSelectable(registry, "NormalMesh");
    AddTriangleMeshSource(registry, mesh);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));

    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);

    constexpr std::array<GN::AveragingMode, 4> kWeightings{{
        GN::AveragingMode::UniformFace,
        GN::AveragingMode::AreaWeighted,
        GN::AveragingMode::AngleWeighted,
        GN::AveragingMode::MaxWeighted,
    }};

    Runtime::SandboxEditorMeshVertexNormalsResult lastResult{};
    for (const GN::AveragingMode weighting : kWeightings)
    {
        registry.Raw().remove<Dirty::GpuDirty,
                              Dirty::DirtyVertexPositions,
                              Dirty::DirtyVertexAttributes,
                              Dirty::DirtyVertexTexcoords,
                              Dirty::DirtyVertexNormals,
                              Dirty::DirtyVertexColors,
                              Dirty::DirtyFaceTopology,
                              Dirty::DirtyEdgeTopology>(mesh);

        lastResult = Runtime::ApplySandboxEditorMeshVertexNormalsCommand(
            context,
            Runtime::SandboxEditorMeshVertexNormalsCommand{
                .StableEntityId = stableId,
                .Weighting = weighting,
            });

        ASSERT_TRUE(lastResult.Succeeded())
            << lastResult.Message;
        EXPECT_EQ(lastResult.NormalStatus, GN::RecomputeStatus::Success);
        EXPECT_EQ(lastResult.Weighting, weighting);
        EXPECT_EQ(lastResult.VertexSlotCount, 3u);
        EXPECT_EQ(lastResult.WrittenCount, 3u);
        EXPECT_EQ(lastResult.ProcessedFaceCount, 1u);
        EXPECT_EQ(lastResult.FallbackVertexCount, 0u);
        EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexNormals>(mesh));
        EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));
        EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexTexcoords>(mesh));
        EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexColors>(mesh));
        EXPECT_FALSE(registry.Raw().all_of<Dirty::GpuDirty>(mesh));
        EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(mesh));
        EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyFaceTopology>(mesh));
        EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyEdgeTopology>(mesh));

        auto normals = registry.Raw()
                           .get<GS::Vertices>(mesh)
                           .Properties.Get<glm::vec3>(PN::kNormal);
        ASSERT_TRUE(normals);
        ASSERT_EQ(normals.Vector().size(), 3u);
        for (const glm::vec3 normal : normals.Vector())
        {
            EXPECT_NEAR(normal.x, 0.0f, 1.0e-5f);
            EXPECT_NEAR(normal.y, 0.0f, 1.0e-5f);
            EXPECT_NEAR(normal.z, 1.0f, 1.0e-5f);
        }
    }

    EXPECT_TRUE(history.IsDirty());
    context.LastMeshVertexNormalsResult = &lastResult;
    const Runtime::SandboxEditorDomainWindowModel model =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::Mesh);
    ASSERT_TRUE(model.Processing.MeshVertexNormalsAvailable);
    ASSERT_TRUE(model.Processing.LastMeshVertexNormalsResult.has_value());
    EXPECT_TRUE(model.Processing.LastMeshVertexNormalsResult->Succeeded());
    EXPECT_EQ(model.Processing.LastMeshVertexNormalsResult->WrittenCount, 3u);
}

TEST(SandboxEditorUi, MeshVertexNormalsRequestQueuesDerivedJobAndPublishesOnApply)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;
    Runtime::StreamingExecutor executor{};
    Runtime::DerivedJobRegistry jobs{executor};
    AttachDerivedJobCommands(context, jobs);
    std::optional<Runtime::SandboxEditorMeshVertexNormalsResult>
        completedResult{};
    context.MethodResultSinks.MeshVertexNormals =
        [&completedResult](
            Runtime::SandboxEditorMeshVertexNormalsResult result)
        {
            completedResult = std::move(result);
        };

    const ECS::EntityHandle mesh = MakeSelectable(registry, "QueuedNormalsMesh");
    AddTriangleMeshSource(registry, mesh);
    auto& properties = registry.Raw().get<GS::Vertices>(mesh).Properties;
    ASSERT_FALSE(properties.Exists(PN::kNormal));
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);

    const Runtime::SandboxEditorMeshVertexNormalsResult result =
        Runtime::ApplySandboxEditorMeshVertexNormalsCommand(
            context,
            Runtime::SandboxEditorMeshVertexNormalsCommand{
                .StableEntityId = stableId,
                .Weighting = GN::AveragingMode::AreaWeighted,
            });

    EXPECT_EQ(result.Status, Runtime::SandboxEditorCommandStatus::Pending);
    EXPECT_EQ(result.VertexSlotCount, 3u);
    EXPECT_NE(result.Message.find("queued"), std::string::npos);
    EXPECT_FALSE(properties.Exists(PN::kNormal));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexNormals>(mesh));

    Runtime::DerivedJobQueueSnapshot queued = jobs.SnapshotAll();
    ASSERT_EQ(queued.Entries.size(), 1u);
    EXPECT_EQ(queued.Entries[0].Name, "Sandbox.MeshVertexNormals.CPU");
    EXPECT_EQ(queued.Entries[0].Status, Runtime::DerivedJobStatus::Queued);

    jobs.Pump(1u);
    jobs.DrainCompletions();
    EXPECT_FALSE(completedResult.has_value());
    EXPECT_FALSE(properties.Exists(PN::kNormal));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexNormals>(mesh));

    EXPECT_EQ(jobs.ApplyMainThreadResults(1u), 1u);
    Runtime::DerivedJobQueueSnapshot done = jobs.SnapshotAll();
    ASSERT_EQ(done.Entries.size(), 1u);
    EXPECT_EQ(done.Entries[0].Status, Runtime::DerivedJobStatus::Complete);
    ASSERT_TRUE(completedResult.has_value());
    EXPECT_TRUE(completedResult->Succeeded()) << completedResult->Message;
    EXPECT_EQ(completedResult->NormalStatus, GN::RecomputeStatus::Success);
    EXPECT_EQ(completedResult->VertexSlotCount, 3u);
    EXPECT_EQ(completedResult->WrittenCount, 3u);
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexNormals>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));
    EXPECT_TRUE(history.IsDirty());

    auto normals = properties.Get<glm::vec3>(PN::kNormal);
    ASSERT_TRUE(normals);
    ASSERT_EQ(normals.Vector().size(), 3u);
    for (const glm::vec3 normal : normals.Vector())
    {
        EXPECT_NEAR(normal.x, 0.0f, 1.0e-5f);
        EXPECT_NEAR(normal.y, 0.0f, 1.0e-5f);
        EXPECT_NEAR(normal.z, 1.0f, 1.0e-5f);
    }
}

TEST(SandboxEditorUi,
     GraphAndPointCloudVertexNormalsRequestsQueueDerivedJobsAndPublishOnApply)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;
    Runtime::StreamingExecutor executor{};
    Runtime::DerivedJobRegistry jobs{executor};
    AttachDerivedJobCommands(context, jobs);
    std::optional<Runtime::SandboxEditorGraphVertexNormalsResult>
        completedGraph{};
    std::optional<Runtime::SandboxEditorPointCloudVertexNormalsResult>
        completedCloud{};
    context.MethodResultSinks.GraphVertexNormals =
        [&completedGraph](
            Runtime::SandboxEditorGraphVertexNormalsResult result)
        {
            completedGraph = std::move(result);
        };
    context.MethodResultSinks.PointCloudVertexNormals =
        [&completedCloud](
            Runtime::SandboxEditorPointCloudVertexNormalsResult result)
        {
            completedCloud = std::move(result);
        };

    const ECS::EntityHandle graph = MakeSelectable(registry, "QueuedGraphNormals");
    AddPlanarCycleGraphSource(registry, graph);
    auto& graphProperties = registry.Raw().get<GS::Nodes>(graph).Properties;
    ASSERT_FALSE(graphProperties.Exists(PN::kNormal));

    const Runtime::SandboxEditorGraphVertexNormalsResult graphResult =
        Runtime::ApplySandboxEditorGraphVertexNormalsCommand(
            context,
            Runtime::SandboxEditorGraphVertexNormalsCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(graph),
                .FallbackNormal = glm::vec3{0.0f, 0.0f, 1.0f},
                .OrientTowardFallback = true,
            });

    EXPECT_EQ(graphResult.Status,
              Runtime::SandboxEditorCommandStatus::Pending);
    EXPECT_EQ(graphResult.VertexSlotCount, 4u);
    EXPECT_EQ(graphResult.EdgeSlotCount, 4u);
    EXPECT_FALSE(graphProperties.Exists(PN::kNormal));

    Runtime::DerivedJobQueueSnapshot queued = jobs.SnapshotAll();
    ASSERT_EQ(queued.Entries.size(), 1u);
    EXPECT_EQ(queued.Entries[0].Name, "Sandbox.GraphVertexNormals.CPU");
    EXPECT_EQ(queued.Entries[0].Status, Runtime::DerivedJobStatus::Queued);

    jobs.Pump(1u);
    jobs.DrainCompletions();
    EXPECT_FALSE(completedGraph.has_value());
    EXPECT_FALSE(graphProperties.Exists(PN::kNormal));
    EXPECT_EQ(jobs.ApplyMainThreadResults(1u), 1u);
    ASSERT_TRUE(completedGraph.has_value());
    EXPECT_TRUE(completedGraph->Succeeded()) << completedGraph->Message;
    EXPECT_EQ(completedGraph->WrittenCount, 4u);
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexNormals>(graph));
    auto graphNormals = graphProperties.Get<glm::vec3>(PN::kNormal);
    ASSERT_TRUE(graphNormals);
    ASSERT_EQ(graphNormals.Vector().size(), 4u);
    for (const glm::vec3 normal : graphNormals.Vector())
    {
        ExpectFiniteUnitNormal(normal);
        EXPECT_GT(normal.z, 0.9f);
    }

    const ECS::EntityHandle cloud = MakeSelectable(registry, "QueuedCloudNormals");
    AddPointCloudSource(registry, cloud, 9u);
    SetPositions(registry.Raw().get<GS::Vertices>(cloud),
                 {
                     {-1.0f, -1.0f, 0.0f},
                     {0.0f, -1.0f, 0.0f},
                     {1.0f, -1.0f, 0.0f},
                     {-1.0f, 0.0f, 0.0f},
                     {0.0f, 0.0f, 0.0f},
                     {1.0f, 0.0f, 0.0f},
                     {-1.0f, 1.0f, 0.0f},
                     {0.0f, 1.0f, 0.0f},
                     {1.0f, 1.0f, 0.0f},
                 });
    auto& cloudProperties = registry.Raw().get<GS::Vertices>(cloud).Properties;
    ASSERT_FALSE(cloudProperties.Exists(PN::kNormal));

    const Runtime::SandboxEditorPointCloudVertexNormalsResult cloudResult =
        Runtime::ApplySandboxEditorPointCloudVertexNormalsCommand(
            context,
            Runtime::SandboxEditorPointCloudVertexNormalsCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(cloud),
                .KNeighbors = 4u,
                .MinimumNeighbors = 2u,
                .UseRadiusSearch = false,
                .Orientation = PCN::OrientationMode::MinimumSpanningTree,
                .FallbackNormal = glm::vec3{0.0f, 0.0f, 1.0f},
            });

    EXPECT_EQ(cloudResult.Status,
              Runtime::SandboxEditorCommandStatus::Pending);
    EXPECT_EQ(cloudResult.PointSlotCount, 9u);
    EXPECT_FALSE(cloudProperties.Exists(PN::kNormal));

    queued = jobs.SnapshotAll();
    ASSERT_EQ(queued.Entries.size(), 2u);
    EXPECT_EQ(queued.Entries[1].Name, "Sandbox.PointCloudVertexNormals.CPU");
    EXPECT_EQ(queued.Entries[1].Status, Runtime::DerivedJobStatus::Queued);

    jobs.Pump(1u);
    jobs.DrainCompletions();
    EXPECT_FALSE(completedCloud.has_value());
    EXPECT_FALSE(cloudProperties.Exists(PN::kNormal));
    EXPECT_EQ(jobs.ApplyMainThreadResults(1u), 1u);
    ASSERT_TRUE(completedCloud.has_value());
    EXPECT_TRUE(completedCloud->Succeeded()) << completedCloud->Message;
    EXPECT_EQ(completedCloud->PointSlotCount, 9u);
    EXPECT_EQ(completedCloud->WrittenCount, 9u);
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexNormals>(cloud));
    auto cloudNormals = cloudProperties.Get<glm::vec3>(PN::kNormal);
    ASSERT_TRUE(cloudNormals);
    ASSERT_EQ(cloudNormals.Vector().size(), 9u);
    for (const glm::vec3 normal : cloudNormals.Vector())
    {
        ExpectFiniteUnitNormal(normal);
        EXPECT_GT(normal.z, 0.5f);
    }
    EXPECT_TRUE(history.IsDirty());
}

TEST(SandboxEditorUi, VertexNormalsDerivedJobsDiscardStaleSourcesBeforeApply)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;

    {
        Runtime::SandboxEditorContext context = MakeContext(registry, selection);
        Runtime::StreamingExecutor executor{};
        Runtime::DerivedJobRegistry jobs{executor};
        AttachDerivedJobCommands(context, jobs);
        bool completedSinkCalled = false;
        context.MethodResultSinks.MeshVertexNormals =
            [&completedSinkCalled](
                Runtime::SandboxEditorMeshVertexNormalsResult)
            {
                completedSinkCalled = true;
            };

        const ECS::EntityHandle mesh =
            MakeSelectable(registry, "StaleMeshNormals");
        AddTriangleMeshSource(registry, mesh);
        const Runtime::SandboxEditorMeshVertexNormalsResult result =
            Runtime::ApplySandboxEditorMeshVertexNormalsCommand(
                context,
                Runtime::SandboxEditorMeshVertexNormalsCommand{
                    .StableEntityId =
                        Runtime::SelectionController::ToStableEntityId(mesh),
                });
        ASSERT_EQ(result.Status, Runtime::SandboxEditorCommandStatus::Pending);

        SetPositions(registry.Raw().get<GS::Vertices>(mesh),
                     {
                         {10.0f, 0.0f, 0.0f},
                         {11.0f, 0.0f, 0.0f},
                         {12.0f, 0.0f, 0.0f},
                     });

        jobs.Pump(1u);
        jobs.DrainCompletions();
        EXPECT_EQ(jobs.ApplyMainThreadResults(1u), 1u);
        Runtime::DerivedJobQueueSnapshot done = jobs.SnapshotAll();
        ASSERT_EQ(done.Entries.size(), 1u);
        EXPECT_EQ(done.Entries[0].Status,
                  Runtime::DerivedJobStatus::StaleDiscarded);
        EXPECT_NE(done.Entries[0].Diagnostic.find(
                      "StaleSourcePropertyGeneration"),
                  std::string::npos);
        EXPECT_FALSE(completedSinkCalled);
        EXPECT_FALSE(registry.Raw().get<GS::Vertices>(mesh)
                         .Properties.Exists(PN::kNormal));
        EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexNormals>(mesh));
    }

    {
        Runtime::SandboxEditorContext context = MakeContext(registry, selection);
        Runtime::StreamingExecutor executor{};
        Runtime::DerivedJobRegistry jobs{executor};
        AttachDerivedJobCommands(context, jobs);
        bool completedSinkCalled = false;
        context.MethodResultSinks.GraphVertexNormals =
            [&completedSinkCalled](
                Runtime::SandboxEditorGraphVertexNormalsResult)
            {
                completedSinkCalled = true;
            };

        const ECS::EntityHandle graph =
            MakeSelectable(registry, "StaleGraphNormals");
        AddPlanarCycleGraphSource(registry, graph);
        const Runtime::SandboxEditorGraphVertexNormalsResult result =
            Runtime::ApplySandboxEditorGraphVertexNormalsCommand(
                context,
                Runtime::SandboxEditorGraphVertexNormalsCommand{
                    .StableEntityId =
                        Runtime::SelectionController::ToStableEntityId(graph),
                });
        ASSERT_EQ(result.Status, Runtime::SandboxEditorCommandStatus::Pending);

        SetNodePositions(registry.Raw().get<GS::Nodes>(graph),
                         {
                             {10.0f, 0.0f, 0.0f},
                             {11.0f, 0.0f, 0.0f},
                             {12.0f, 0.0f, 0.0f},
                             {13.0f, 0.0f, 0.0f},
                         });

        jobs.Pump(1u);
        jobs.DrainCompletions();
        EXPECT_EQ(jobs.ApplyMainThreadResults(1u), 1u);
        Runtime::DerivedJobQueueSnapshot done = jobs.SnapshotAll();
        ASSERT_EQ(done.Entries.size(), 1u);
        EXPECT_EQ(done.Entries[0].Status,
                  Runtime::DerivedJobStatus::StaleDiscarded);
        EXPECT_NE(done.Entries[0].Diagnostic.find(
                      "StaleSourcePropertyGeneration"),
                  std::string::npos);
        EXPECT_FALSE(completedSinkCalled);
        EXPECT_FALSE(registry.Raw().get<GS::Nodes>(graph)
                         .Properties.Exists(PN::kNormal));
        EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexNormals>(graph));
    }

    {
        Runtime::SandboxEditorContext context = MakeContext(registry, selection);
        Runtime::StreamingExecutor executor{};
        Runtime::DerivedJobRegistry jobs{executor};
        AttachDerivedJobCommands(context, jobs);
        bool completedSinkCalled = false;
        context.MethodResultSinks.PointCloudVertexNormals =
            [&completedSinkCalled](
                Runtime::SandboxEditorPointCloudVertexNormalsResult)
            {
                completedSinkCalled = true;
            };

        const ECS::EntityHandle cloud =
            MakeSelectable(registry, "StaleCloudNormals");
        AddPointCloudSource(registry, cloud, 4u);
        SetPositions(registry.Raw().get<GS::Vertices>(cloud),
                     {
                         {0.0f, 0.0f, 0.0f},
                         {1.0f, 0.0f, 0.0f},
                         {0.0f, 1.0f, 0.0f},
                         {1.0f, 1.0f, 0.0f},
                     });
        const Runtime::SandboxEditorPointCloudVertexNormalsResult result =
            Runtime::ApplySandboxEditorPointCloudVertexNormalsCommand(
                context,
                Runtime::SandboxEditorPointCloudVertexNormalsCommand{
                    .StableEntityId =
                        Runtime::SelectionController::ToStableEntityId(cloud),
                    .KNeighbors = 3u,
                    .MinimumNeighbors = 2u,
                });
        ASSERT_EQ(result.Status, Runtime::SandboxEditorCommandStatus::Pending);

        SetPositions(registry.Raw().get<GS::Vertices>(cloud),
                     {
                         {10.0f, 0.0f, 0.0f},
                         {11.0f, 0.0f, 0.0f},
                         {12.0f, 0.0f, 0.0f},
                         {13.0f, 0.0f, 0.0f},
                     });

        jobs.Pump(1u);
        jobs.DrainCompletions();
        EXPECT_EQ(jobs.ApplyMainThreadResults(1u), 1u);
        Runtime::DerivedJobQueueSnapshot done = jobs.SnapshotAll();
        ASSERT_EQ(done.Entries.size(), 1u);
        EXPECT_EQ(done.Entries[0].Status,
                  Runtime::DerivedJobStatus::StaleDiscarded);
        EXPECT_NE(done.Entries[0].Diagnostic.find(
                      "StaleSourcePropertyGeneration"),
                  std::string::npos);
        EXPECT_FALSE(completedSinkCalled);
        EXPECT_FALSE(registry.Raw().get<GS::Vertices>(cloud)
                         .Properties.Exists(PN::kNormal));
        EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexNormals>(cloud));
    }
}

TEST(SandboxEditorUi, MeshVertexNormalsCommandSurvivesPendingDirectMeshPostProcess)
{
    TmpFile meshFile(
        "runtime_mesh_normals_postprocess_race.obj",
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "vt 0 0\n"
        "vt 1 0\n"
        "vt 0 1\n"
        "vn 1 0 0\n"
        "vn 1 0 0\n"
        "vn 1 0 0\n"
        "f 1/1/1 2/2/2 3/3/3\n");

    std::optional<ECS::EntityHandle> meshEntity{};
    std::optional<std::uint32_t> stableId{};
    Runtime::Engine engine(
        HeadlessConfig(),
        std::make_unique<WaitForConditionApplication>(
            [&meshEntity](Runtime::Engine& runningEngine)
            {
                return meshEntity.has_value() &&
                    DirectMeshPostProcessReady(runningEngine, *meshEntity);
            },
            128u));
    engine.Initialize();
    InstallSandboxDefaultRuntimePolicies(engine);

    auto imported = engine.GetAssetImportPipeline().ImportAssetFromPath(Runtime::RuntimeAssetImportRequest{
        .Path = meshFile.Path.string(),
        .PayloadKind = Assets::AssetPayloadKind::Mesh,
    });
    ASSERT_TRUE(imported.has_value()) << static_cast<int>(imported.error());

    meshEntity = FindFirstEntityWithDomain(engine.GetScene(), GS::Domain::Mesh);
    ASSERT_TRUE(meshEntity.has_value());
    stableId = Runtime::SelectionController::ToStableEntityId(*meshEntity);

    ExpectMeshVertexNormalsNear(
        engine,
        *meshEntity,
        std::array{
            glm::vec3{1.0f, 0.0f, 0.0f},
            glm::vec3{1.0f, 0.0f, 0.0f},
            glm::vec3{1.0f, 0.0f, 0.0f},
        });

    Runtime::SandboxEditorContext context =
        MakeContext(engine.GetScene(), engine.GetSelectionController());
    const Runtime::SandboxEditorMeshVertexNormalsResult recomputed =
        Runtime::ApplySandboxEditorMeshVertexNormalsCommand(
            context,
            Runtime::SandboxEditorMeshVertexNormalsCommand{
                .StableEntityId = *stableId,
                .Weighting = GN::AveragingMode::AreaWeighted,
            });
    ASSERT_TRUE(recomputed.Succeeded()) << recomputed.Message;
    ExpectMeshVertexNormalsNear(
        engine,
        *meshEntity,
        std::array{
            glm::vec3{0.0f, 0.0f, 1.0f},
            glm::vec3{0.0f, 0.0f, 1.0f},
            glm::vec3{0.0f, 0.0f, 1.0f},
        });

    engine.Run();

    const std::optional<Graphics::MaterialTextureAssetBindings> bindings =
        engine.GetMaterialTextureAssetBindingsForTest(*stableId);
    if (bindings.has_value())
    {
        EXPECT_FALSE(bindings->Normal.IsValid());
    }
    const auto& diagnostics =
        engine.GetObjectSpaceNormalBakeQueueDiagnosticsForTest();
    EXPECT_EQ(diagnostics.NonOperationalNoOps, 1u);
    EXPECT_EQ(engine.GetPendingObjectSpaceNormalBakeCountForTest(), 0u);
    ExpectMeshVertexNormalsNear(
        engine,
        *meshEntity,
        std::array{
            glm::vec3{0.0f, 0.0f, 1.0f},
            glm::vec3{0.0f, 0.0f, 1.0f},
            glm::vec3{0.0f, 0.0f, 1.0f},
        });

    engine.Shutdown();
}

TEST(SandboxEditorUi, MeshVertexNormalsCommandFailsClosedForInvalidTargets)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);

    const Runtime::SandboxEditorMeshVertexNormalsCommand validShape{
        .StableEntityId = 1u,
    };

    const Runtime::SandboxEditorMeshVertexNormalsResult missingScene =
        Runtime::ApplySandboxEditorMeshVertexNormalsCommand(
            Runtime::SandboxEditorContext{},
            validShape);
    EXPECT_EQ(missingScene.Status,
              Runtime::SandboxEditorCommandStatus::MissingScene);
    EXPECT_EQ(missingScene.Error, Core::ErrorCode::InvalidState);

    const Runtime::SandboxEditorMeshVertexNormalsResult stale =
        Runtime::ApplySandboxEditorMeshVertexNormalsCommand(
            context,
            Runtime::SandboxEditorMeshVertexNormalsCommand{
                .StableEntityId = std::numeric_limits<std::uint32_t>::max(),
            });
    EXPECT_EQ(stale.Status, Runtime::SandboxEditorCommandStatus::StaleEntity);
    EXPECT_EQ(stale.Error, Core::ErrorCode::ResourceNotFound);

    const ECS::EntityHandle cloud = MakeSelectable(registry, "Cloud");
    AddPointCloudSource(registry, cloud, 3u);
    const Runtime::SandboxEditorMeshVertexNormalsResult wrongDomain =
        Runtime::ApplySandboxEditorMeshVertexNormalsCommand(
            context,
            Runtime::SandboxEditorMeshVertexNormalsCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(cloud),
            });
    EXPECT_EQ(wrongDomain.Status,
              Runtime::SandboxEditorCommandStatus::UnsupportedGeometryDomain);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(cloud));

    const ECS::EntityHandle mesh = MakeSelectable(registry, "TypeConflictMesh");
    AddTriangleMeshSource(registry, mesh);
    const auto conflictingNormals = registry.Raw()
                                        .get<GS::Vertices>(mesh)
                                        .Properties.GetOrAdd<float>(
                                            std::string{PN::kNormal},
                                            0.0f);
    ASSERT_TRUE(conflictingNormals);
    const Runtime::SandboxEditorMeshVertexNormalsResult conflict =
        Runtime::ApplySandboxEditorMeshVertexNormalsCommand(
            context,
            Runtime::SandboxEditorMeshVertexNormalsCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(mesh),
            });
    EXPECT_EQ(conflict.Status,
              Runtime::SandboxEditorCommandStatus::GeometryProcessingFailed);
    EXPECT_EQ(conflict.NormalStatus, GN::RecomputeStatus::PropertyTypeConflict);
    EXPECT_EQ(conflict.Error, Core::ErrorCode::TypeMismatch);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));

    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));
    context.LastMeshVertexNormalsResult = &conflict;
    const Runtime::SandboxEditorDomainWindowModel model =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::Mesh);
    EXPECT_TRUE(HasDiagnostic(
        model.Processing.Diagnostics,
        Runtime::SandboxEditorDiagnosticCode::GeometryProcessingFailed));
}

TEST(SandboxEditorUi, GraphAndPointCloudVertexNormalsCommandsPublishCanonicalNormals)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;

    const ECS::EntityHandle graph = MakeSelectable(registry, "NormalGraph");
    AddPlanarCycleGraphSource(registry, graph);
    const std::uint32_t graphStableId =
        Runtime::SelectionController::ToStableEntityId(graph);

    const Runtime::SandboxEditorGraphVertexNormalsResult graphResult =
        Runtime::ApplySandboxEditorGraphVertexNormalsCommand(
            context,
            Runtime::SandboxEditorGraphVertexNormalsCommand{
                .StableEntityId = graphStableId,
                .FallbackNormal = glm::vec3{0.0f, 0.0f, 1.0f},
                .OrientTowardFallback = true,
            });

    ASSERT_TRUE(graphResult.Succeeded()) << graphResult.Message;
    EXPECT_EQ(graphResult.NormalStatus, GVN::RecomputeStatus::Success);
    EXPECT_EQ(graphResult.VertexSlotCount, 4u);
    EXPECT_EQ(graphResult.EdgeSlotCount, 4u);
    EXPECT_EQ(graphResult.WrittenCount, 4u);
    EXPECT_EQ(graphResult.ValidNormalVertexCount, 4u);
    EXPECT_EQ(graphResult.InvalidEdgeCount, 0u);
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexNormals>(graph));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(graph));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::GpuDirty>(graph));

    auto graphNormals = registry.Raw()
                            .get<GS::Nodes>(graph)
                            .Properties.Get<glm::vec3>(PN::kNormal);
    ASSERT_TRUE(graphNormals);
    ASSERT_EQ(graphNormals.Vector().size(), 4u);
    for (const glm::vec3 normal : graphNormals.Vector())
    {
        ExpectFiniteUnitNormal(normal);
        EXPECT_GT(normal.z, 0.9f);
    }

    ASSERT_TRUE(selection.SetSelectedEntity(registry, graph));
    context.LastGraphVertexNormalsResult = &graphResult;
    const Runtime::SandboxEditorDomainWindowModel graphModel =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::Graph);
    ASSERT_TRUE(graphModel.Processing.GraphVertexNormalsAvailable);
    ASSERT_TRUE(graphModel.Processing.LastGraphVertexNormalsResult.has_value());
    EXPECT_TRUE(
        graphModel.Processing.LastGraphVertexNormalsResult->Succeeded());
    EXPECT_EQ(
        graphModel.Processing.LastGraphVertexNormalsResult->WrittenCount,
        4u);

    const ECS::EntityHandle cloud = MakeSelectable(registry, "NormalCloud");
    AddPointCloudSource(registry, cloud, 9u);
    SetPositions(registry.Raw().get<GS::Vertices>(cloud),
                 {
                     {-1.0f, -1.0f, 0.0f},
                     {0.0f, -1.0f, 0.0f},
                     {1.0f, -1.0f, 0.0f},
                     {-1.0f, 0.0f, 0.0f},
                     {0.0f, 0.0f, 0.0f},
                     {1.0f, 0.0f, 0.0f},
                     {-1.0f, 1.0f, 0.0f},
                     {0.0f, 1.0f, 0.0f},
                     {1.0f, 1.0f, 0.0f},
                 });
    auto deletedPoints = registry.Raw()
                             .get<GS::Vertices>(cloud)
                             .Properties.GetOrAdd<bool>("p:deleted", false);
    ASSERT_EQ(deletedPoints.Vector().size(), 9u);
    deletedPoints.Vector()[8] = true;
    const std::uint32_t cloudStableId =
        Runtime::SelectionController::ToStableEntityId(cloud);

    const Runtime::SandboxEditorPointCloudVertexNormalsResult cloudResult =
        Runtime::ApplySandboxEditorPointCloudVertexNormalsCommand(
            context,
            Runtime::SandboxEditorPointCloudVertexNormalsCommand{
                .StableEntityId = cloudStableId,
                .KNeighbors = 4u,
                .MinimumNeighbors = 2u,
                .UseRadiusSearch = false,
                .Orientation = PCN::OrientationMode::MinimumSpanningTree,
                .FallbackNormal = glm::vec3{0.0f, 0.0f, 1.0f},
            });

    ASSERT_TRUE(cloudResult.Succeeded()) << cloudResult.Message;
    EXPECT_EQ(cloudResult.NormalStatus, PCN::RecomputeStatus::Success);
    EXPECT_EQ(cloudResult.Backend, PCN::NeighborhoodBackend::KDTree);
    EXPECT_EQ(cloudResult.PointSlotCount, 9u);
    EXPECT_EQ(cloudResult.WrittenCount, 8u);
    EXPECT_EQ(cloudResult.SkippedDeletedPointCount, 1u);
    EXPECT_EQ(cloudResult.KNeighbors, 4u);
    EXPECT_EQ(cloudResult.MinimumNeighbors, 2u);
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexNormals>(cloud));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(cloud));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::GpuDirty>(cloud));

    auto cloudNormals = registry.Raw()
                            .get<GS::Vertices>(cloud)
                            .Properties.Get<glm::vec3>(PN::kNormal);
    ASSERT_TRUE(cloudNormals);
    ASSERT_EQ(cloudNormals.Vector().size(), 9u);
    for (const glm::vec3 normal : cloudNormals.Vector())
    {
        ExpectFiniteUnitNormal(normal);
        EXPECT_GT(normal.z, 0.5f);
    }

    ASSERT_TRUE(selection.SetSelectedEntity(registry, cloud));
    context.LastGraphVertexNormalsResult = nullptr;
    context.LastPointCloudVertexNormalsResult = &cloudResult;
    const Runtime::SandboxEditorDomainWindowModel cloudModel =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::PointCloud);
    ASSERT_TRUE(cloudModel.Processing.PointCloudVertexNormalsAvailable);
    ASSERT_TRUE(
        cloudModel.Processing.LastPointCloudVertexNormalsResult.has_value());
    EXPECT_TRUE(
        cloudModel.Processing.LastPointCloudVertexNormalsResult->Succeeded());
    EXPECT_EQ(
        cloudModel.Processing.LastPointCloudVertexNormalsResult->WrittenCount,
        8u);
    EXPECT_EQ(cloudModel.Processing.LastPointCloudVertexNormalsResult
                  ->SkippedDeletedPointCount,
              1u);
    EXPECT_TRUE(history.IsDirty());
}

TEST(SandboxEditorUi, GraphAndPointCloudVertexNormalsCommandsFailClosedForInvalidTargets)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);

    const Runtime::SandboxEditorGraphVertexNormalsResult missingGraphScene =
        Runtime::ApplySandboxEditorGraphVertexNormalsCommand(
            Runtime::SandboxEditorContext{},
            Runtime::SandboxEditorGraphVertexNormalsCommand{
                .StableEntityId = 1u,
            });
    EXPECT_EQ(missingGraphScene.Status,
              Runtime::SandboxEditorCommandStatus::MissingScene);
    EXPECT_EQ(missingGraphScene.Error, Core::ErrorCode::InvalidState);

    const ECS::EntityHandle cloudWrongDomain =
        MakeSelectable(registry, "CloudWrongDomain");
    AddPointCloudSource(registry, cloudWrongDomain, 3u);
    SetPositions(registry.Raw().get<GS::Vertices>(cloudWrongDomain),
                 {
                     {0.0f, 0.0f, 0.0f},
                     {1.0f, 0.0f, 0.0f},
                     {0.0f, 1.0f, 0.0f},
                 });
    const Runtime::SandboxEditorGraphVertexNormalsResult graphWrongDomain =
        Runtime::ApplySandboxEditorGraphVertexNormalsCommand(
            context,
            Runtime::SandboxEditorGraphVertexNormalsCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(
                        cloudWrongDomain),
            });
    EXPECT_EQ(graphWrongDomain.Status,
              Runtime::SandboxEditorCommandStatus::UnsupportedGeometryDomain);
    EXPECT_FALSE(
        registry.Raw().all_of<Dirty::DirtyVertexNormals>(cloudWrongDomain));

    const Runtime::SandboxEditorPointCloudVertexNormalsResult invalidPointParams =
        Runtime::ApplySandboxEditorPointCloudVertexNormalsCommand(
            context,
            Runtime::SandboxEditorPointCloudVertexNormalsCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(
                        cloudWrongDomain),
                .KNeighbors = 0u,
            });
    EXPECT_EQ(invalidPointParams.Status,
              Runtime::SandboxEditorCommandStatus::InvalidProcessingParameters);
    EXPECT_FALSE(registry.Raw().get<GS::Vertices>(cloudWrongDomain)
                     .Properties.Exists(PN::kNormal));

    const ECS::EntityHandle graphConflict =
        MakeSelectable(registry, "GraphConflict");
    AddPlanarCycleGraphSource(registry, graphConflict);
    auto graphConflictNormals = registry.Raw()
                                    .get<GS::Nodes>(graphConflict)
                                    .Properties.GetOrAdd<float>(
                                        std::string{PN::kNormal},
                                        0.0f);
    ASSERT_TRUE(graphConflictNormals);
    const Runtime::SandboxEditorGraphVertexNormalsResult graphConflictResult =
        Runtime::ApplySandboxEditorGraphVertexNormalsCommand(
            context,
            Runtime::SandboxEditorGraphVertexNormalsCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(
                        graphConflict),
            });
    EXPECT_EQ(graphConflictResult.Status,
              Runtime::SandboxEditorCommandStatus::GeometryProcessingFailed);
    EXPECT_EQ(graphConflictResult.NormalStatus,
              GVN::RecomputeStatus::PropertyTypeConflict);
    EXPECT_EQ(graphConflictResult.Error, Core::ErrorCode::TypeMismatch);
    EXPECT_FALSE(
        registry.Raw().all_of<Dirty::DirtyVertexNormals>(graphConflict));
    EXPECT_TRUE(registry.Raw()
                    .get<GS::Nodes>(graphConflict)
                    .Properties.Get<float>(PN::kNormal));

    const ECS::EntityHandle meshWrongDomain =
        MakeSelectable(registry, "MeshWrongDomain");
    AddTriangleMeshSource(registry, meshWrongDomain);
    const Runtime::SandboxEditorPointCloudVertexNormalsResult pointWrongDomain =
        Runtime::ApplySandboxEditorPointCloudVertexNormalsCommand(
            context,
            Runtime::SandboxEditorPointCloudVertexNormalsCommand{
                .StableEntityId =
                    Runtime::SelectionController::ToStableEntityId(
                        meshWrongDomain),
            });
    EXPECT_EQ(pointWrongDomain.Status,
              Runtime::SandboxEditorCommandStatus::UnsupportedGeometryDomain);
    EXPECT_FALSE(
        registry.Raw().all_of<Dirty::DirtyVertexNormals>(meshWrongDomain));

    const ECS::EntityHandle cloudConflict =
        MakeSelectable(registry, "CloudConflict");
    AddPointCloudSource(registry, cloudConflict, 4u);
    SetPositions(registry.Raw().get<GS::Vertices>(cloudConflict),
                 {
                     {0.0f, 0.0f, 0.0f},
                     {1.0f, 0.0f, 0.0f},
                     {0.0f, 1.0f, 0.0f},
                     {1.0f, 1.0f, 0.0f},
                 });
    auto pointConflictNormals = registry.Raw()
                                    .get<GS::Vertices>(cloudConflict)
                                    .Properties.GetOrAdd<float>(
                                        std::string{PN::kNormal},
                                        0.0f);
    ASSERT_TRUE(pointConflictNormals);
    const Runtime::SandboxEditorPointCloudVertexNormalsResult
        pointConflictResult =
            Runtime::ApplySandboxEditorPointCloudVertexNormalsCommand(
                context,
                Runtime::SandboxEditorPointCloudVertexNormalsCommand{
                    .StableEntityId =
                        Runtime::SelectionController::ToStableEntityId(
                            cloudConflict),
                    .KNeighbors = 3u,
                    .MinimumNeighbors = 2u,
                });
    EXPECT_EQ(pointConflictResult.Status,
              Runtime::SandboxEditorCommandStatus::GeometryProcessingFailed);
    EXPECT_EQ(pointConflictResult.NormalStatus,
              PCN::RecomputeStatus::PropertyTypeConflict);
    EXPECT_EQ(pointConflictResult.Error, Core::ErrorCode::TypeMismatch);
    EXPECT_FALSE(
        registry.Raw().all_of<Dirty::DirtyVertexNormals>(cloudConflict));
    EXPECT_TRUE(registry.Raw()
                    .get<GS::Vertices>(cloudConflict)
                    .Properties.Get<float>(PN::kNormal));
}

TEST(SandboxEditorUi, KMeansCommandFailsClosedForInvalidTargetsAndInputs)
{
    using Domain = Runtime::SandboxEditorGeometryProcessingDomain;

    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);

    const Runtime::SandboxEditorKMeansCommand validShape{
        .StableEntityId = 1u,
        .Domain = Domain::PointCloudPoints,
        .ClusterCount = 2u,
        .MaxIterations = 8u,
    };

    const Runtime::SandboxEditorKMeansResult missingScene =
        Runtime::ApplySandboxEditorKMeansCommand(
            Runtime::SandboxEditorContext{},
            validShape);
    EXPECT_EQ(missingScene.Status,
              Runtime::SandboxEditorCommandStatus::MissingScene);
    EXPECT_EQ(missingScene.Error, Core::ErrorCode::InvalidState);

    const Runtime::SandboxEditorKMeansResult invalidParameters =
        Runtime::ApplySandboxEditorKMeansCommand(
            context,
            Runtime::SandboxEditorKMeansCommand{
                .StableEntityId = 1u,
                .Domain = Domain::MeshEdges,
                .ClusterCount = 0u,
                .MaxIterations = 0u,
            });
    EXPECT_EQ(invalidParameters.Status,
              Runtime::SandboxEditorCommandStatus::InvalidProcessingParameters);
    EXPECT_EQ(invalidParameters.Error, Core::ErrorCode::InvalidArgument);
    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorCommandStatus(
                     invalidParameters.Status),
                 "InvalidProcessingParameters");

    const Runtime::SandboxEditorKMeansResult stale =
        Runtime::ApplySandboxEditorKMeansCommand(
            context,
            Runtime::SandboxEditorKMeansCommand{
                .StableEntityId = std::numeric_limits<std::uint32_t>::max(),
                .Domain = Domain::PointCloudPoints,
                .ClusterCount = 2u,
                .MaxIterations = 8u,
            });
    EXPECT_EQ(stale.Status, Runtime::SandboxEditorCommandStatus::StaleEntity);
    EXPECT_EQ(stale.Error, Core::ErrorCode::ResourceNotFound);

    const ECS::EntityHandle cloud = MakeSelectable(registry, "Cloud");
    AddPointCloudSource(registry, cloud, 3u);
    const std::uint32_t cloudStableId =
        Runtime::SelectionController::ToStableEntityId(cloud);
    const Runtime::SandboxEditorKMeansResult wrongDomain =
        Runtime::ApplySandboxEditorKMeansCommand(
            context,
            Runtime::SandboxEditorKMeansCommand{
                .StableEntityId = cloudStableId,
                .Domain = Domain::MeshVertices,
                .ClusterCount = 2u,
                .MaxIterations = 8u,
            });
    EXPECT_EQ(wrongDomain.Status,
              Runtime::SandboxEditorCommandStatus::UnsupportedGeometryDomain);

    const Runtime::SandboxEditorKMeansResult missingPositions =
        Runtime::ApplySandboxEditorKMeansCommand(
            context,
            Runtime::SandboxEditorKMeansCommand{
                .StableEntityId = cloudStableId,
                .Domain = Domain::PointCloudPoints,
                .ClusterCount = 2u,
                .MaxIterations = 8u,
            });
    EXPECT_EQ(missingPositions.Status,
              Runtime::SandboxEditorCommandStatus::InvalidProcessingParameters);

    SetPositions(registry.Raw().get<GS::Vertices>(cloud),
                 {
                     {0.0f, 0.0f, 0.0f},
                     {std::numeric_limits<float>::infinity(), 0.0f, 0.0f},
                     {1.0f, 0.0f, 0.0f},
                 });
    const Runtime::SandboxEditorKMeansResult nonFinite =
        Runtime::ApplySandboxEditorKMeansCommand(
            context,
            Runtime::SandboxEditorKMeansCommand{
                .StableEntityId = cloudStableId,
                .Domain = Domain::PointCloudPoints,
                .ClusterCount = 2u,
                .MaxIterations = 8u,
            });
    EXPECT_EQ(nonFinite.Status,
              Runtime::SandboxEditorCommandStatus::InvalidProcessingParameters);

    context.LastKMeansResult = &nonFinite;
    ASSERT_TRUE(selection.SetSelectedEntity(registry, cloud));
    const Runtime::SandboxEditorDomainWindowModel model =
        Runtime::BuildSandboxEditorDomainWindowModel(
            context,
            Runtime::SandboxEditorDomainWindowKind::PointCloud);
    EXPECT_TRUE(HasDiagnostic(
        model.Processing.Diagnostics,
        Runtime::SandboxEditorDiagnosticCode::GeometryProcessingFailed));
    EXPECT_STREQ(Runtime::DebugNameForSandboxEditorDiagnosticCode(
                     Runtime::SandboxEditorDiagnosticCode::GeometryProcessingFailed),
                 "GeometryProcessingFailed");
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

TEST(SandboxEditorUi, UvRegenerationCommandRepairsSelectedMeshTexcoords)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;

    const ECS::EntityHandle mesh = MakeSelectable(registry, "UvRepairMesh");
    AddTriangleMeshSource(registry, mesh);
    auto& vertices = registry.Raw().get<GS::Vertices>(mesh);
    auto texcoords = vertices.Properties.Get<glm::vec2>("v:texcoord");
    ASSERT_TRUE(texcoords);
    texcoords[1] = glm::vec2{
        std::numeric_limits<float>::quiet_NaN(),
        0.0f,
    };
    vertices.Properties.GetOrAdd<glm::vec4>("v:paint", glm::vec4{1.0f})
        .Vector() = {
            glm::vec4{1.0f, 0.0f, 0.0f, 1.0f},
            glm::vec4{0.0f, 1.0f, 0.0f, 1.0f},
            glm::vec4{0.0f, 0.0f, 1.0f, 1.0f},
        };
    auto& faces = registry.Raw().get<GS::Faces>(mesh);
    faces.Properties.GetOrAdd<std::uint32_t>("f:material", 0u).Vector() = {7u};

    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;

    const Runtime::SandboxEditorPanelFrame before =
        Runtime::BuildSandboxEditorPanelFrame(context);
    ASSERT_TRUE(before.Inspector.TextureBake.HasSelectedEntity);
    EXPECT_TRUE(before.Inspector.TextureBake.Uv.UvRegenerationAvailable);
    EXPECT_TRUE(before.Inspector.TextureBake.Uv.HasTexcoords);
    EXPECT_TRUE(before.Inspector.TextureBake.Uv.TexcoordCountMatchesVertices);
    EXPECT_FALSE(before.Inspector.TextureBake.Uv.TexcoordsFinite);
    EXPECT_FALSE(before.Inspector.TextureBake.CanBake);

    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);
    const Runtime::SandboxEditorUvRegenerationCommandResult result =
        Runtime::ApplySandboxEditorUvRegenerationCommand(
            context,
            Runtime::SandboxEditorUvRegenerationCommand{
                .StableEntityId = stableId,
                .Resolution = 64u,
                .Padding = 2u,
            });

    ASSERT_EQ(result.Status, Runtime::SandboxEditorCommandStatus::Applied);
    EXPECT_EQ(result.UvStatus, Geometry::UvAtlas::UvAtlasStatus::Success);
    EXPECT_EQ(result.Provenance, Geometry::UvAtlas::UvAtlasProvenance::Generated);
    EXPECT_GT(result.AtlasWidth, 0u);
    EXPECT_GT(result.AtlasHeight, 0u);
    EXPECT_TRUE(history.IsDirty());
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(mesh));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyEdgeTopology>(mesh));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyFaceTopology>(mesh));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::GpuDirty>(mesh));

    const GS::ConstSourceView repaired = GS::BuildConstView(registry.Raw(), mesh);
    ASSERT_EQ(repaired.ActiveDomain, GS::Domain::Mesh);
    ASSERT_NE(repaired.VertexSource, nullptr);
    const auto repairedTexcoords =
        repaired.VertexSource->Properties.Get<glm::vec2>("v:texcoord");
    ASSERT_TRUE(repairedTexcoords);
    ASSERT_EQ(repairedTexcoords.Vector().size(), repaired.VerticesAlive());
    for (const glm::vec2 uv : repairedTexcoords.Vector())
    {
        EXPECT_TRUE(std::isfinite(uv.x));
        EXPECT_TRUE(std::isfinite(uv.y));
    }
    const auto repairedPaint =
        repaired.VertexSource->Properties.Get<glm::vec4>("v:paint");
    ASSERT_TRUE(repairedPaint);
    EXPECT_EQ(repairedPaint.Vector().size(), repairedTexcoords.Vector().size());

    ASSERT_NE(repaired.FaceSource, nullptr);
    const auto repairedMaterial =
        repaired.FaceSource->Properties.Get<std::uint32_t>("f:material");
    ASSERT_TRUE(repairedMaterial);
    ASSERT_FALSE(repairedMaterial.Vector().empty());
    EXPECT_EQ(repairedMaterial[0], 7u);

    const Runtime::SandboxEditorPanelFrame after =
        Runtime::BuildSandboxEditorPanelFrame(context);
    EXPECT_TRUE(after.Inspector.TextureBake.Uv.TexcoordsFinite);
    EXPECT_TRUE(after.Inspector.TextureBake.Uv.CheckerPreviewAvailable);
}

TEST(SandboxEditorUi, UvRegenerationRequestQueuesDerivedJobAndPublishesOnApply)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;
    Runtime::StreamingExecutor executor{};
    Runtime::DerivedJobRegistry jobs{executor};
    AttachDerivedJobCommands(context, jobs);
    std::optional<Runtime::SandboxEditorUvRegenerationCommandResult>
        completedResult{};
    context.MethodResultSinks.UvRegeneration =
        [&completedResult](
            Runtime::SandboxEditorUvRegenerationCommandResult result)
        {
            completedResult = std::move(result);
        };

    const ECS::EntityHandle mesh =
        MakeSelectable(registry, "QueuedUvRepairMesh");
    AddTriangleMeshSource(registry, mesh);
    auto& vertices = registry.Raw().get<GS::Vertices>(mesh);
    auto texcoords = vertices.Properties.Get<glm::vec2>("v:texcoord");
    ASSERT_TRUE(texcoords);
    texcoords[1] = glm::vec2{
        std::numeric_limits<float>::quiet_NaN(),
        0.0f,
    };

    const auto texcoordsFinite =
        [&registry, mesh]()
        {
            const GS::ConstSourceView view =
                GS::BuildConstView(registry.Raw(), mesh);
            if (view.VertexSource == nullptr)
                return false;
            const auto uv =
                view.VertexSource->Properties.Get<glm::vec2>("v:texcoord");
            if (!uv)
                return false;
            for (const glm::vec2 value : uv.Vector())
            {
                if (!std::isfinite(value.x) || !std::isfinite(value.y))
                    return false;
            }
            return true;
        };

    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);
    const Runtime::SandboxEditorUvRegenerationCommandResult result =
        Runtime::ApplySandboxEditorUvRegenerationCommand(
            context,
            Runtime::SandboxEditorUvRegenerationCommand{
                .StableEntityId = stableId,
                .Resolution = 64u,
                .Padding = 2u,
            });

    EXPECT_EQ(result.Status, Runtime::SandboxEditorCommandStatus::Pending);
    EXPECT_NE(result.Diagnostic.find("queued"), std::string::npos);
    EXPECT_FALSE(texcoordsFinite());
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyEdgeTopology>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyFaceTopology>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::GpuDirty>(mesh));

    Runtime::DerivedJobQueueSnapshot queued = jobs.SnapshotAll();
    ASSERT_EQ(queued.Entries.size(), 1u);
    EXPECT_EQ(queued.Entries[0].Name, "Sandbox.UvRegeneration.CPU");
    EXPECT_EQ(queued.Entries[0].Status, Runtime::DerivedJobStatus::Queued);

    jobs.Pump(1u);
    jobs.DrainCompletions();
    EXPECT_FALSE(completedResult.has_value());
    EXPECT_FALSE(texcoordsFinite());

    EXPECT_EQ(jobs.ApplyMainThreadResults(1u), 1u);
    Runtime::DerivedJobQueueSnapshot done = jobs.SnapshotAll();
    ASSERT_EQ(done.Entries.size(), 1u);
    EXPECT_EQ(done.Entries[0].Status, Runtime::DerivedJobStatus::Complete);
    ASSERT_TRUE(completedResult.has_value());
    EXPECT_TRUE(completedResult->Succeeded()) << completedResult->Diagnostic;
    EXPECT_EQ(completedResult->UvStatus,
              Geometry::UvAtlas::UvAtlasStatus::Success);
    EXPECT_EQ(completedResult->Provenance,
              Geometry::UvAtlas::UvAtlasProvenance::Generated);
    EXPECT_TRUE(texcoordsFinite());
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(mesh));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyEdgeTopology>(mesh));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::DirtyFaceTopology>(mesh));
    EXPECT_TRUE(registry.Raw().all_of<Dirty::GpuDirty>(mesh));
    EXPECT_TRUE(history.IsDirty());

    ASSERT_TRUE(history.CanUndo());
    EXPECT_EQ(history.Undo().Status,
              Runtime::EditorCommandHistoryStatus::Undone);
    EXPECT_FALSE(texcoordsFinite());
    EXPECT_EQ(history.Redo().Status,
              Runtime::EditorCommandHistoryStatus::Redone);
    EXPECT_TRUE(texcoordsFinite());
}

TEST(SandboxEditorUi, UvRegenerationDuplicateSubmitUsesExistingActiveJob)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    Runtime::StreamingExecutor executor{};
    Runtime::DerivedJobRegistry jobs{executor};
    AttachDerivedJobCommands(context, jobs);

    const ECS::EntityHandle mesh =
        MakeSelectable(registry, "QueuedUvDuplicateMesh");
    AddTriangleMeshSource(registry, mesh);
    auto& vertices = registry.Raw().get<GS::Vertices>(mesh);
    auto texcoords = vertices.Properties.Get<glm::vec2>("v:texcoord");
    ASSERT_TRUE(texcoords);
    texcoords[1] = glm::vec2{
        std::numeric_limits<float>::quiet_NaN(),
        0.0f,
    };

    const Runtime::SandboxEditorUvRegenerationCommand command{
        .StableEntityId =
            Runtime::SelectionController::ToStableEntityId(mesh),
        .Resolution = 64u,
        .Padding = 2u,
    };

    const Runtime::SandboxEditorUvRegenerationCommandResult first =
        Runtime::ApplySandboxEditorUvRegenerationCommand(context, command);
    ASSERT_EQ(first.Status, Runtime::SandboxEditorCommandStatus::Pending);

    Runtime::DerivedJobQueueSnapshot queued = jobs.SnapshotAll();
    ASSERT_EQ(queued.Entries.size(), 1u);
    context.DerivedJobs = &queued;

    const Runtime::SandboxEditorUvRegenerationCommandResult duplicate =
        Runtime::ApplySandboxEditorUvRegenerationCommand(context, command);
    EXPECT_EQ(duplicate.Status, Runtime::SandboxEditorCommandStatus::Pending);
    EXPECT_NE(duplicate.Diagnostic.find("already has an active"),
              std::string::npos);
    EXPECT_NE(duplicate.Diagnostic.find("job 0:1"), std::string::npos);
    EXPECT_EQ(jobs.SnapshotAll().Entries.size(), 1u);

    jobs.Pump(1u);
    jobs.DrainCompletions();
    EXPECT_EQ(jobs.ApplyMainThreadResults(1u), 1u);

    Runtime::DerivedJobQueueSnapshot complete = jobs.SnapshotAll();
    ASSERT_EQ(complete.Entries.size(), 1u);
    EXPECT_EQ(complete.Entries[0].Status, Runtime::DerivedJobStatus::Complete);
    context.DerivedJobs = &complete;

    const Runtime::SandboxEditorUvRegenerationCommandResult rerun =
        Runtime::ApplySandboxEditorUvRegenerationCommand(context, command);
    EXPECT_EQ(rerun.Status, Runtime::SandboxEditorCommandStatus::Pending);
    Runtime::DerivedJobQueueSnapshot afterRerun = jobs.SnapshotAll();
    ASSERT_EQ(afterRerun.Entries.size(), 2u);
    EXPECT_EQ(afterRerun.Entries[1].Status, Runtime::DerivedJobStatus::Queued);
}

TEST(SandboxEditorUi, UvRegenerationDerivedJobDiscardsStaleSource)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    Runtime::StreamingExecutor executor{};
    Runtime::DerivedJobRegistry jobs{executor};
    AttachDerivedJobCommands(context, jobs);
    bool completedSinkCalled = false;
    context.MethodResultSinks.UvRegeneration =
        [&completedSinkCalled](
            Runtime::SandboxEditorUvRegenerationCommandResult)
        {
            completedSinkCalled = true;
        };

    const ECS::EntityHandle mesh =
        MakeSelectable(registry, "StaleUvRepairMesh");
    AddTriangleMeshSource(registry, mesh);
    auto& vertices = registry.Raw().get<GS::Vertices>(mesh);
    auto texcoords = vertices.Properties.Get<glm::vec2>("v:texcoord");
    ASSERT_TRUE(texcoords);
    texcoords[1] = glm::vec2{
        std::numeric_limits<float>::quiet_NaN(),
        0.0f,
    };

    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);
    const Runtime::SandboxEditorUvRegenerationCommandResult result =
        Runtime::ApplySandboxEditorUvRegenerationCommand(
            context,
            Runtime::SandboxEditorUvRegenerationCommand{
                .StableEntityId = stableId,
                .Resolution = 64u,
                .Padding = 2u,
            });
    ASSERT_EQ(result.Status, Runtime::SandboxEditorCommandStatus::Pending);

    SetPositions(vertices,
                 {
                     {0.0f, 0.0f, 0.0f},
                     {1.25f, 0.0f, 0.0f},
                     {0.0f, 1.0f, 0.0f},
                 });

    jobs.Pump(1u);
    jobs.DrainCompletions();
    EXPECT_EQ(jobs.ApplyMainThreadResults(1u), 1u);

    Runtime::DerivedJobQueueSnapshot done = jobs.SnapshotAll();
    ASSERT_EQ(done.Entries.size(), 1u);
    EXPECT_EQ(done.Entries[0].Status,
              Runtime::DerivedJobStatus::StaleDiscarded);
    EXPECT_NE(done.Entries[0].Diagnostic.find(
                  "StaleSourcePropertyGeneration"),
              std::string::npos);
    EXPECT_FALSE(completedSinkCalled);
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexPositions>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyVertexAttributes>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyEdgeTopology>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::DirtyFaceTopology>(mesh));
    EXPECT_FALSE(registry.Raw().all_of<Dirty::GpuDirty>(mesh));

    const GS::ConstSourceView stale = GS::BuildConstView(registry.Raw(), mesh);
    ASSERT_NE(stale.VertexSource, nullptr);
    const auto staleTexcoords =
        stale.VertexSource->Properties.Get<glm::vec2>("v:texcoord");
    ASSERT_TRUE(staleTexcoords);
    ASSERT_GT(staleTexcoords.Vector().size(), 1u);
    EXPECT_FALSE(std::isfinite(staleTexcoords[1].x));
}

TEST(SandboxEditorUi, UvRegenerationPanelModelTracksDerivedJobStateThroughCache)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorSelectedModelCache cache{};
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.SelectedModelCache = &cache;
    Runtime::StreamingExecutor executor{};
    Runtime::DerivedJobRegistry jobs{executor};
    AttachDerivedJobCommands(context, jobs);

    const ECS::EntityHandle mesh =
        MakeSelectable(registry, "CachedUvJobMesh");
    AddTriangleMeshSource(registry, mesh);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);

    Runtime::SandboxEditorPanelFrame frame =
        Runtime::BuildSandboxEditorPanelFrame(context);
    ASSERT_TRUE(frame.Inspector.HasEntity);
    EXPECT_FALSE(frame.Inspector.TextureBake.Uv.UvRegenerationJob.has_value());

    const Runtime::SandboxEditorUvRegenerationCommandResult result =
        Runtime::ApplySandboxEditorUvRegenerationCommand(
            context,
            Runtime::SandboxEditorUvRegenerationCommand{
                .StableEntityId = stableId,
                .Resolution = 64u,
                .Padding = 2u,
            });
    ASSERT_EQ(result.Status, Runtime::SandboxEditorCommandStatus::Pending);

    Runtime::DerivedJobQueueSnapshot queued = jobs.SnapshotAll();
    context.DerivedJobs = &queued;
    frame = Runtime::BuildSandboxEditorPanelFrame(context);
    ASSERT_TRUE(frame.Inspector.TextureBake.Uv.UvRegenerationJob.has_value());
    EXPECT_EQ(frame.Inspector.TextureBake.Uv.UvRegenerationJob->Status,
              Runtime::DerivedJobStatus::Queued);
    EXPECT_EQ(frame.Inspector.TextureBake.Uv.UvRegenerationJob->Key.OutputName,
              "uv_regeneration");

    jobs.Pump(1u);
    jobs.DrainCompletions();
    Runtime::DerivedJobQueueSnapshot applying = jobs.SnapshotAll();
    context.DerivedJobs = &applying;
    frame = Runtime::BuildSandboxEditorPanelFrame(context);
    ASSERT_TRUE(frame.Inspector.TextureBake.Uv.UvRegenerationJob.has_value());
    EXPECT_EQ(frame.Inspector.TextureBake.Uv.UvRegenerationJob->Status,
              Runtime::DerivedJobStatus::Applying);

    EXPECT_EQ(jobs.ApplyMainThreadResults(1u), 1u);
    Runtime::DerivedJobQueueSnapshot complete = jobs.SnapshotAll();
    context.DerivedJobs = &complete;
    frame = Runtime::BuildSandboxEditorPanelFrame(context);
    ASSERT_TRUE(frame.Inspector.TextureBake.Uv.UvRegenerationJob.has_value());
    EXPECT_EQ(frame.Inspector.TextureBake.Uv.UvRegenerationJob->Status,
              Runtime::DerivedJobStatus::Complete);
    EXPECT_TRUE(frame.Inspector.TextureBake.Uv.TexcoordsFinite);

    const Runtime::SandboxEditorSelectedModelCacheStats stats = cache.Stats();
    EXPECT_GE(stats.SelectedAnalysisCacheMisses, 4u);
}

TEST(SandboxEditorUi, TextureBakeControlsReportUvSourcesAndRouteCommand)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Assets::AssetService assets;
    Tests::MockDevice device;
    device.Operational = true;

    const ECS::EntityHandle mesh = MakeSelectable(registry, "TextureBakeMesh");
    AddTriangleMeshSource(registry, mesh);
    auto& vertices = registry.Raw().get<GS::Vertices>(mesh);
    vertices.Properties.GetOrAdd<glm::vec4>("v:paint", glm::vec4{1.0f})
        .Vector() = {
            glm::vec4{1.0f, 0.0f, 0.0f, 1.0f},
            glm::vec4{0.0f, 1.0f, 0.0f, 1.0f},
            glm::vec4{0.0f, 0.0f, 1.0f, 1.0f},
        };
    registry.Raw().emplace<Runtime::ProgressivePresentationBindings>(
        mesh,
        MakeProgressiveMeshPresentationBindings());

    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));
    Runtime::SandboxEditorContext context =
        MakeContext(registry, selection, true, nullptr, &device);
    context.CommandHistory = &history;
    context.AssetService = &assets;

    const Runtime::SandboxEditorPanelFrame frame =
        Runtime::BuildSandboxEditorPanelFrame(context);
    const Runtime::SandboxEditorTextureBakeControlsModel& bake =
        frame.Inspector.TextureBake;
    ASSERT_TRUE(bake.HasSelectedEntity);
    EXPECT_TRUE(bake.IsMesh);
    EXPECT_TRUE(bake.Uv.HasTexcoords);
    EXPECT_TRUE(bake.Uv.TexcoordCountMatchesVertices);
    EXPECT_TRUE(bake.Uv.TexcoordsFinite);
    EXPECT_TRUE(bake.HasRuntimeBakeCommand);
    EXPECT_TRUE(bake.CanBake);
    EXPECT_TRUE(bake.Uv.UvRegenerationAvailable);
    EXPECT_TRUE(bake.Uv.UvRegenerationDisabledReason.empty());

    const Runtime::SandboxEditorTextureBakeSourceRow* paint =
        FindTextureBakeSource(bake, "v:paint");
    ASSERT_NE(paint, nullptr);
    EXPECT_TRUE(paint->Bakeable);
    EXPECT_EQ(paint->BakeDomain, Runtime::ProgressiveGeometryDomain::MeshVertex);
    EXPECT_EQ(paint->ExpectedValueKind, Runtime::ProgressivePropertyValueKind::Vec4);

    const Runtime::SandboxEditorTextureBakeSourceRow* position =
        FindTextureBakeSource(bake, std::string{PN::kPosition});
    ASSERT_NE(position, nullptr);
    EXPECT_FALSE(position->Bakeable);
    EXPECT_EQ(position->Category,
              Runtime::SandboxEditorTextureBakeSourceCategory::Connectivity);
    EXPECT_FALSE(position->DisabledReason.empty());

    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);
    const Runtime::SandboxEditorTextureBakeCommandResult result =
        Runtime::ApplySandboxEditorTextureBakeCommand(
            context,
            Runtime::SandboxEditorTextureBakeCommand{
                .StableEntityId = stableId,
                .PresentationKey = "mesh.surface",
                .TargetSemantic = Runtime::ProgressiveSlotSemantic::Albedo,
                .SourceDomain = Runtime::ProgressiveGeometryDomain::MeshVertex,
                .ExpectedValueKind = Runtime::ProgressivePropertyValueKind::Vec4,
                .PropertyName = "v:paint",
                .Encoder = Runtime::MeshAttributeTextureBakeEncoder::RgbaColor,
                .Width = 4u,
                .Height = 4u,
                .GeneratedKey = "paint",
                .BindGeneratedTexture = true,
            });

    ASSERT_EQ(result.Status, Runtime::SandboxEditorCommandStatus::Applied);
    EXPECT_EQ(result.BakeStatus,
              Runtime::SelectedMeshTextureBakeStatus::Success);
    ASSERT_TRUE(result.GeneratedTexture.IsValid());
    EXPECT_TRUE(result.BoundGeneratedTexture);
    EXPECT_TRUE(history.IsDirty());

    const auto texture =
        assets.Read<Assets::AssetTexture2DPayload>(result.GeneratedTexture);
    ASSERT_TRUE(texture.has_value());
    ASSERT_EQ(texture->size(), 1u);
    EXPECT_EQ((*texture)[0].Metadata.Width, 4u);

    const auto& bindings =
        registry.Raw().get<Runtime::ProgressivePresentationBindings>(mesh);
    const auto* presentation =
        Runtime::FindPresentationBinding(bindings, "mesh.surface");
    ASSERT_NE(presentation, nullptr);
    const auto* albedo =
        Runtime::FindSlotBinding(*presentation,
                                 Runtime::ProgressiveSlotSemantic::Albedo);
    ASSERT_NE(albedo, nullptr);
    EXPECT_EQ(albedo->SourceKind,
              Runtime::ProgressiveSlotSourceKind::GeneratedTextureAsset);
    EXPECT_EQ(albedo->GeneratedTexture, result.GeneratedTexture);
}

TEST(SandboxEditorUi, AttachedEngineContextWiresTextureBakeServiceAndDevice)
{
    const std::string editorSource =
        ReadRepositoryTextFile("src/runtime/Editor/Runtime.SandboxEditorUi.cpp");
    ASSERT_FALSE(editorSource.empty());

    const std::string_view contextBuilder = SourceRange(
        editorSource,
        "[[nodiscard]] SandboxEditorContext BuildContextFromEngine(Engine& engine)",
        "[[nodiscard]] std::string ProgressOverlayText(");
    ASSERT_FALSE(contextBuilder.empty());
    EXPECT_NE(contextBuilder.find(".AssetService = &engine.GetAssetService(),"),
              std::string_view::npos);
    EXPECT_NE(contextBuilder.find(".Device = &engine.GetDevice(),"),
              std::string_view::npos);
}

TEST(SandboxEditorUi, TextureBakeRequiresOperationalGpuBackend)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Assets::AssetService assets;
    Tests::MockDevice device;
    device.Operational = false;

    const ECS::EntityHandle mesh = MakeSelectable(registry, "TextureBakeMesh");
    AddTriangleMeshSource(registry, mesh);
    auto& vertices = registry.Raw().get<GS::Vertices>(mesh);
    vertices.Properties.GetOrAdd<glm::vec4>("v:paint", glm::vec4{1.0f})
        .Vector() = {
            glm::vec4{1.0f, 0.0f, 0.0f, 1.0f},
            glm::vec4{0.0f, 1.0f, 0.0f, 1.0f},
            glm::vec4{0.0f, 0.0f, 1.0f, 1.0f},
        };
    registry.Raw().emplace<Runtime::ProgressivePresentationBindings>(
        mesh,
        MakeProgressiveMeshPresentationBindings());

    ASSERT_TRUE(selection.SetSelectedEntity(registry, mesh));
    Runtime::SandboxEditorContext context =
        MakeContext(registry, selection, true, nullptr, &device);
    context.AssetService = &assets;

    const Runtime::SandboxEditorPanelFrame frame =
        Runtime::BuildSandboxEditorPanelFrame(context);
    const Runtime::SandboxEditorTextureBakeControlsModel& bake =
        frame.Inspector.TextureBake;
    ASSERT_TRUE(bake.HasSelectedEntity);
    EXPECT_TRUE(bake.IsMesh);
    EXPECT_TRUE(bake.HasRuntimeBakeCommand);
    EXPECT_TRUE(bake.Uv.HasTexcoords);
    EXPECT_TRUE(bake.Uv.TexcoordsFinite);
    EXPECT_FALSE(bake.CanBake);
    EXPECT_EQ(bake.DisabledReason,
              "texture baking requires an operational GPU backend");

    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(mesh);
    const Runtime::SandboxEditorTextureBakeCommandResult result =
        Runtime::ApplySandboxEditorTextureBakeCommand(
            context,
            Runtime::SandboxEditorTextureBakeCommand{
                .StableEntityId = stableId,
                .PresentationKey = "mesh.surface",
                .TargetSemantic = Runtime::ProgressiveSlotSemantic::Albedo,
                .SourceDomain = Runtime::ProgressiveGeometryDomain::MeshVertex,
                .ExpectedValueKind = Runtime::ProgressivePropertyValueKind::Vec4,
                .PropertyName = "v:paint",
                .Encoder = Runtime::MeshAttributeTextureBakeEncoder::RgbaColor,
                .Width = 4u,
                .Height = 4u,
                .GeneratedKey = "paint",
                .BindGeneratedTexture = true,
            });

    EXPECT_EQ(result.Status,
              Runtime::SandboxEditorCommandStatus::InvalidVisualizationProperty);
    EXPECT_EQ(result.BakeStatus,
              Runtime::SelectedMeshTextureBakeStatus::CommandFailed);
    EXPECT_NE(result.Diagnostic.find("operational GPU"), std::string::npos);
}

// UI-032 — preset buttons switch the scalar source but must preserve styling
// (colormap, isoline width/color, highlight isovalues) already configured on
// the target lane, and the styling fields must round-trip through the config
// command onto the component.
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
    engine.Initialize();

    ECS::Scene::Registry& scene = engine.GetScene();
    Runtime::SelectionController& selection = engine.GetSelectionController();
    const ECS::EntityHandle graph = MakeSelectable(scene, "Graph");
    AddGraphSource(scene, graph);
    auto& raw = scene.Raw();
    raw.remove<G::RenderEdges>(graph);

    Runtime::RenderExtractionCache extraction;
    const Runtime::RuntimeRenderExtractionStats first =
        extraction.ExtractAndSubmit(scene,
                                    engine.GetRenderer(),
                                    &engine.GetGpuAssetCache());
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
                                    &engine.GetGpuAssetCache());
    EXPECT_EQ(second.GraphGeometryUploads, 0u);
    EXPECT_EQ(second.GraphGeometryReuploads, 1u);
    EXPECT_EQ(second.GraphGeometryReleases, 1u);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
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

TEST(SandboxEditorUi, AdapterCallbackDrawsDeterministicMenuOnlyFrame)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorPanelFrame captured{};

    FakeWindow window(1280, 720);
    Extrinsic::Graphics::ImGuiOverlaySystem overlay;
    Runtime::ImGuiAdapter adapter(window, overlay);
    ASSERT_TRUE(adapter.Initialize());

    adapter.SetEditorCallback(
        [&]
        {
            captured = Runtime::BuildSandboxEditorPanelFrame(
                MakeContext(registry, selection));
            Runtime::DrawSandboxEditorPanelFrame(captured);
        });

    adapter.BeginFrame(1.0 / 60.0);
    adapter.EndFrame();
    adapter.BeginFrame(1.0 / 60.0);
    adapter.EndFrame();

    EXPECT_EQ(adapter.GetDiagnostics().EditorCallbackInvocations, 2u);
    EXPECT_EQ(adapter.GetDiagnostics().FramesProduced, 2u);
    EXPECT_GE(adapter.GetDiagnostics().LastDrawListCount, 1u);
    EXPECT_FALSE(captured.FileImport.Enabled);
    EXPECT_TRUE(HasDiagnostic(captured.FileImport.Diagnostics,
                              Runtime::SandboxEditorDiagnosticCode::AssetImportUnavailable));
}

TEST(SandboxEditorUi, EngineImportFacadeReportsMissingFile)
{
    Runtime::Engine engine(HeadlessConfig(), std::make_unique<OneFrameApplication>());
    engine.Initialize();

    auto imported = engine.GetAssetImportPipeline().ImportAssetFromPath(
        Runtime::RuntimeAssetImportRequest{
            .Path = "/tmp/intrinsicengine-ui-001-missing.gltf",
        });
    EXPECT_FALSE(imported.has_value());
    EXPECT_EQ(imported.error(), Core::ErrorCode::FileNotFound);

    engine.Shutdown();
}

TEST(SandboxEditorUi, EngineImportFacadeMaterializesStandaloneGeometryDomains)
{
    TmpFile meshFile(
        "runtime_dragdrop_import_mesh.obj",
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "vt 0 0\n"
        "vt 1 0\n"
        "vt 0 1\n"
        "f 1/1 2/2 3/3\n");
    TmpFile graphFile(
        "runtime_dragdrop_import_graph.tgf",
        "1 0 0 0 first\n"
        "2 1 0 0 second\n"
        "#\n"
        "1 2 1.0 edge\n");
    TmpFile cloudFile(
        "runtime_dragdrop_import_cloud.xyz",
        "0 0 0\n"
        "1 2 3\n");

    Runtime::Engine engine(HeadlessConfig(), std::make_unique<OneFrameApplication>());
    engine.Initialize();
    InstallSandboxDefaultRuntimePolicies(engine);

    auto mesh = engine.GetAssetImportPipeline().ImportAssetFromPath(
        Runtime::RuntimeAssetImportRequest{
            .Path = meshFile.Path.string(),
            .PayloadKind = Assets::AssetPayloadKind::Mesh,
        });
    ASSERT_TRUE(mesh.has_value()) << static_cast<int>(mesh.error());
    EXPECT_TRUE(mesh->Asset.IsValid());
    EXPECT_EQ(mesh->PayloadKind, Assets::AssetPayloadKind::Mesh);
    EXPECT_EQ(mesh->PrimitiveEntitiesCreated, 1u);

    auto graph = engine.GetAssetImportPipeline().ImportAssetFromPath(
        Runtime::RuntimeAssetImportRequest{
            .Path = graphFile.Path.string(),
            .PayloadKind = Assets::AssetPayloadKind::Graph,
        });
    ASSERT_TRUE(graph.has_value()) << static_cast<int>(graph.error());
    EXPECT_TRUE(graph->Asset.IsValid());
    EXPECT_EQ(graph->PayloadKind, Assets::AssetPayloadKind::Graph);
    EXPECT_EQ(graph->PrimitiveEntitiesCreated, 1u);

    auto cloud = engine.GetAssetImportPipeline().ImportAssetFromPath(
        Runtime::RuntimeAssetImportRequest{
            .Path = cloudFile.Path.string(),
            .PayloadKind = Assets::AssetPayloadKind::PointCloud,
        });
    ASSERT_TRUE(cloud.has_value()) << static_cast<int>(cloud.error());
    EXPECT_TRUE(cloud->Asset.IsValid());
    EXPECT_EQ(cloud->PayloadKind, Assets::AssetPayloadKind::PointCloud);
    EXPECT_EQ(cloud->PrimitiveEntitiesCreated, 1u);

    EXPECT_EQ(CountEntitiesWithDomain(engine.GetScene(), GS::Domain::Mesh), 1u);
    EXPECT_EQ(CountEntitiesWithDomain(engine.GetScene(), GS::Domain::Graph), 1u);
    EXPECT_EQ(CountEntitiesWithDomain(engine.GetScene(), GS::Domain::PointCloud), 1u);

    auto& raw = engine.GetScene().Raw();
    const std::optional<ECS::EntityHandle> meshEntity =
        FindFirstEntityWithDomain(engine.GetScene(), GS::Domain::Mesh);
    ASSERT_TRUE(meshEntity.has_value());
    EXPECT_TRUE(raw.all_of<G::RenderSurface>(*meshEntity));
    EXPECT_TRUE(raw.all_of<G::VisualizationConfig>(*meshEntity));
    ASSERT_TRUE((raw.all_of<ECSC::Culling::Local::Bounds,
                            ECSC::Culling::World::Bounds>(*meshEntity)));
    const auto& meshWorld =
        raw.get<ECSC::Culling::World::Bounds>(*meshEntity);
    EXPECT_NEAR(meshWorld.WorldBoundingSphere.Center.x, 0.5f, 1.0e-5f);
    EXPECT_NEAR(meshWorld.WorldBoundingSphere.Center.y, 0.5f, 1.0e-5f);
    EXPECT_GT(meshWorld.WorldBoundingSphere.Radius, 0.5f);

    const std::optional<ECS::EntityHandle> graphEntity =
        FindFirstEntityWithDomain(engine.GetScene(), GS::Domain::Graph);
    ASSERT_TRUE(graphEntity.has_value());
    EXPECT_TRUE(raw.all_of<G::RenderEdges>(*graphEntity));
    EXPECT_TRUE(raw.all_of<G::RenderPoints>(*graphEntity));
    EXPECT_TRUE(raw.all_of<G::VisualizationConfig>(*graphEntity));
    ASSERT_TRUE((raw.all_of<ECSC::Culling::Local::Bounds,
                            ECSC::Culling::World::Bounds>(*graphEntity)));
    const auto& graphWorld =
        raw.get<ECSC::Culling::World::Bounds>(*graphEntity);
    EXPECT_NEAR(graphWorld.WorldBoundingSphere.Center.x, 0.5f, 1.0e-5f);
    EXPECT_NEAR(graphWorld.WorldBoundingSphere.Center.y, 0.0f, 1.0e-5f);
    EXPECT_GT(graphWorld.WorldBoundingSphere.Radius, 0.4f);

    const std::optional<ECS::EntityHandle> cloudEntity =
        FindFirstEntityWithDomain(engine.GetScene(), GS::Domain::PointCloud);
    ASSERT_TRUE(cloudEntity.has_value());
    EXPECT_TRUE(raw.all_of<G::RenderPoints>(*cloudEntity));
    EXPECT_TRUE(raw.all_of<G::VisualizationConfig>(*cloudEntity));
    ASSERT_TRUE((raw.all_of<ECSC::Culling::Local::Bounds,
                            ECSC::Culling::World::Bounds>(*cloudEntity)));
    const auto& cloudWorld =
        raw.get<ECSC::Culling::World::Bounds>(*cloudEntity);
    EXPECT_NEAR(cloudWorld.WorldBoundingSphere.Center.x, 0.5f, 1.0e-5f);
    EXPECT_NEAR(cloudWorld.WorldBoundingSphere.Center.y, 1.0f, 1.0e-5f);
    EXPECT_NEAR(cloudWorld.WorldBoundingSphere.Center.z, 1.5f, 1.0e-5f);
    EXPECT_GT(cloudWorld.WorldBoundingSphere.Radius, 1.8f);

    const std::optional<Runtime::RuntimeAssetImportEvent>& lastEvent =
        engine.GetAssetImportPipeline().GetLastAssetImportEvent();
    ASSERT_TRUE(lastEvent.has_value());
    EXPECT_TRUE(lastEvent->Succeeded());
    ASSERT_TRUE(lastEvent->Result.has_value());
    EXPECT_EQ(lastEvent->Result->PayloadKind, Assets::AssetPayloadKind::PointCloud);
    EXPECT_NE(lastEvent->Path.find("runtime_dragdrop_import_cloud.xyz"),
              std::string::npos);

    engine.Shutdown();
}

TEST(SandboxEditorUi, EngineImportFacadeMaterializesNonManifoldObjAsRenderableMesh)
{
    TmpFile meshFile(
        "runtime_dragdrop_import_nonmanifold.obj",
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "v 0 -1 0\n"
        "v 0.5 0 1\n"
        "vt 0 0\n"
        "vt 1 0\n"
        "vt 0 1\n"
        "vt 0 -1\n"
        "vt 0.5 0.5\n"
        "f 1/1 2/2 3/3\n"
        "f 2/2 1/1 4/4\n"
        "f 1/1 2/2 5/5\n");

    std::optional<ECS::EntityHandle> meshEntity{};
    Runtime::Engine engine(
        HeadlessConfig(),
        std::make_unique<WaitForConditionApplication>(
            [&meshEntity](Runtime::Engine& runningEngine)
            {
                return meshEntity.has_value() &&
                    DirectMeshPostProcessReady(runningEngine, *meshEntity);
            }));
    engine.Initialize();
    InstallSandboxDefaultRuntimePolicies(engine);

    auto mesh = engine.GetAssetImportPipeline().ImportAssetFromPath(
        Runtime::RuntimeAssetImportRequest{
            .Path = meshFile.Path.string(),
            .PayloadKind = Assets::AssetPayloadKind::Mesh,
        });
    ASSERT_TRUE(mesh.has_value()) << static_cast<int>(mesh.error());
    EXPECT_TRUE(mesh->Asset.IsValid());
    EXPECT_EQ(mesh->PayloadKind, Assets::AssetPayloadKind::Mesh);
    EXPECT_EQ(mesh->PrimitiveEntitiesCreated, 1u);
    EXPECT_EQ(CountEntitiesWithDomain(engine.GetScene(), GS::Domain::Mesh), 1u);

    auto& raw = engine.GetScene().Raw();
    meshEntity = FindFirstEntityWithDomain(engine.GetScene(), GS::Domain::Mesh);
    ASSERT_TRUE(meshEntity.has_value());
    EXPECT_TRUE((raw.all_of<ECSC::MetaData,
                            ECSC::Hierarchy::Component,
                            ECSC::Transform::Component,
                            ECSC::Transform::WorldMatrix,
                            Sel::SelectableTag,
                            G::RenderSurface,
                            G::VisualizationConfig,
                            GS::Vertices,
                            GS::Edges,
                            GS::Halfedges,
                            GS::Faces>(*meshEntity)));
    EXPECT_EQ(raw.get<G::RenderSurface>(*meshEntity).Domain,
              G::RenderSurface::SourceDomain::Vertex);

    engine.Run();
    ASSERT_TRUE(DirectMeshPostProcessReady(engine, *meshEntity));

    Runtime::RenderExtractionCache extraction;
    const auto stats = extraction.ExtractAndSubmit(engine.GetScene(),
                                                   engine.GetRenderer(),
                                                   &engine.GetGpuAssetCache());
    EXPECT_EQ(stats.CandidateRenderableCount, 1u);
    EXPECT_EQ(stats.MeshGeometryUploads, 1u);
    EXPECT_EQ(stats.MeshGeometryFailedPack, 0u);
    EXPECT_EQ(stats.MeshGeometryMissingPositions, 0u);
    EXPECT_EQ(stats.MeshGeometryInvalidTopology, 0u);
    EXPECT_GE(engine.GetRenderer().GetGpuWorld().GetLiveGeometryCount(), 1u);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(SandboxEditorUi, EngineImportFacadeMaterializesObjWithoutAuthoredTexcoordsAsRenderableMesh)
{
    TmpFile meshFile(
        "runtime_dragdrop_import_missing_uv.obj",
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f 1 2 3\n");

    std::optional<ECS::EntityHandle> meshEntity{};
    Runtime::Engine engine(
        HeadlessConfig(),
        std::make_unique<WaitForConditionApplication>(
            [&meshEntity](Runtime::Engine& runningEngine)
            {
                return meshEntity.has_value() &&
                    DirectMeshPostProcessReady(runningEngine, *meshEntity);
            }));
    engine.Initialize();
    InstallSandboxDefaultRuntimePolicies(engine);

    auto mesh = engine.GetAssetImportPipeline().ImportAssetFromPath(
        Runtime::RuntimeAssetImportRequest{
            .Path = meshFile.Path.string(),
            .PayloadKind = Assets::AssetPayloadKind::Mesh,
        });
    ASSERT_TRUE(mesh.has_value()) << static_cast<int>(mesh.error());
    EXPECT_TRUE(mesh->Asset.IsValid());
    EXPECT_EQ(mesh->PayloadKind, Assets::AssetPayloadKind::Mesh);
    EXPECT_EQ(mesh->PrimitiveEntitiesCreated, 1u);

    meshEntity = FindFirstEntityWithDomain(engine.GetScene(), GS::Domain::Mesh);
    ASSERT_TRUE(meshEntity.has_value());

    engine.Run();
    ASSERT_TRUE(DirectMeshPostProcessReady(engine, *meshEntity));

    auto& raw = engine.GetScene().Raw();
    ASSERT_TRUE(raw.all_of<G::RenderSurface>(*meshEntity));
    const GS::ConstSourceView view = GS::BuildConstView(raw, *meshEntity);
    ASSERT_TRUE(view.Valid());
    ASSERT_NE(view.VertexSource, nullptr);
    const auto texcoords = view.VertexSource->Properties.Get<glm::vec2>("v:texcoord");
    ASSERT_TRUE(texcoords);
    ASSERT_EQ(texcoords.Vector().size(), 3u);

    bool sawNonZeroTexcoord = false;
    for (const glm::vec2 uv : texcoords.Vector())
    {
        EXPECT_TRUE(std::isfinite(uv.x));
        EXPECT_TRUE(std::isfinite(uv.y));
        sawNonZeroTexcoord = sawNonZeroTexcoord ||
            std::abs(uv.x) > 1.0e-6f ||
            std::abs(uv.y) > 1.0e-6f;
    }
    EXPECT_TRUE(sawNonZeroTexcoord);

    Runtime::RenderExtractionCache extraction;
    const auto stats = extraction.ExtractAndSubmit(engine.GetScene(),
                                                   engine.GetRenderer(),
                                                   &engine.GetGpuAssetCache());
    EXPECT_EQ(stats.CandidateRenderableCount, 1u);
    EXPECT_EQ(stats.MeshGeometryUploads, 1u);
    EXPECT_EQ(stats.MeshGeometryFailedPack, 0u);
    EXPECT_EQ(stats.MeshGeometryMissingTexcoords, 0u);
    EXPECT_GE(engine.GetRenderer().GetGpuWorld().GetLiveGeometryCount(), 1u);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}

TEST(SandboxEditorUi, DroppedFilePathsRouteAmbiguousPlyThroughRuntimeImportFacade)
{
    TmpFile cloudFile(
        "runtime_dragdrop_event_cloud.ply",
        "ply\n"
        "format ascii 1.0\n"
        "element vertex 3\n"
        "property float x\n"
        "property float y\n"
        "property float z\n"
        "end_header\n"
        "0 0 0\n"
        "1 0 0\n"
        "2 0 0\n");

    Runtime::Engine engine(
        HeadlessConfig(),
        std::make_unique<WaitForAssetImportEventApplication>(128u));
    engine.Initialize();
    InstallSandboxDefaultRuntimePolicies(engine);

    Runtime::SandboxEditorUi ui;
    ui.Attach(engine);

    const std::vector<std::string> droppedPaths{cloudFile.Path.string()};
    engine.GetAssetImportPipeline().ImportDroppedFilePaths(droppedPaths);
    EXPECT_EQ(CountEntitiesWithDomain(engine.GetScene(), GS::Domain::PointCloud), 0u);
    EXPECT_FALSE(engine.GetAssetImportPipeline().GetLastAssetImportEvent().has_value());

    ASSERT_FALSE(engine.GetWindow().ShouldClose())
        << "explicit Null window backend must keep Engine::Run() drivable on headless hosts";

    engine.Run();

    EXPECT_EQ(CountEntitiesWithDomain(engine.GetScene(), GS::Domain::PointCloud), 1u);
    const std::optional<Runtime::RuntimeAssetImportEvent>& lastEvent =
        engine.GetAssetImportPipeline().GetLastAssetImportEvent();
    ASSERT_TRUE(lastEvent.has_value());
    EXPECT_TRUE(lastEvent->Succeeded());
    ASSERT_TRUE(lastEvent->Result.has_value());
    EXPECT_EQ(lastEvent->Result->PayloadKind, Assets::AssetPayloadKind::PointCloud);

    ASSERT_TRUE(ui.GetLastFrame().FileImport.LastResult.has_value());
    EXPECT_TRUE(ui.GetLastFrame().FileImport.LastResult->Succeeded());
    EXPECT_EQ(ui.GetLastFrame().FileImport.LastResult->PayloadKind,
              Assets::AssetPayloadKind::PointCloud);
    EXPECT_FALSE(HasDiagnostic(ui.GetLastFrame().FileImport.Diagnostics,
                               Runtime::SandboxEditorDiagnosticCode::AssetImportFailed));

    ui.Detach();
    engine.Shutdown();
}

TEST(SandboxEditorUi, DuplicateDroppedGeometryImportUsesSingleIngestRecord)
{
    TmpFile meshFile(
        "runtime_duplicate_drop_mesh.obj",
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f 1 2 3\n");

    Runtime::Engine engine(
        HeadlessConfig(),
        std::make_unique<FixedFrameApplication>(128u));
    engine.Initialize();
    InstallSandboxDefaultRuntimePolicies(engine);

    const std::vector<std::string> droppedPaths{
        meshFile.Path.string(),
        meshFile.Path.string(),
    };
    engine.GetAssetImportPipeline().ImportDroppedFilePaths(droppedPaths);

    std::vector<Runtime::RuntimeAssetIngestRecord> records =
        engine.GetAssetImportPipeline().GetAssetIngestRecordsForTest();
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].Request.Source,
              Runtime::RuntimeAssetIngestSource::DroppedFile);
    EXPECT_EQ(records[0].Request.Path, meshFile.Path.string());
    EXPECT_EQ(records[0].Phase, Runtime::RuntimeAssetIngestPhase::Decoding);

    const std::optional<Runtime::RuntimeAssetImportEvent>& duplicateEvent =
        engine.GetAssetImportPipeline().GetLastAssetImportEvent();
    ASSERT_TRUE(duplicateEvent.has_value());
    EXPECT_FALSE(duplicateEvent->Succeeded());
    EXPECT_EQ(duplicateEvent->Error, Core::ErrorCode::ResourceBusy);
    EXPECT_EQ(duplicateEvent->IngestDiagnostic,
              Runtime::RuntimeAssetIngestDiagnostic::DuplicateActiveRequest);

    ASSERT_FALSE(engine.GetWindow().ShouldClose())
        << "explicit Null window backend must keep Engine::Run() drivable on headless hosts";

    engine.Run();

    EXPECT_EQ(CountEntitiesWithDomain(engine.GetScene(), GS::Domain::Mesh), 1u);
    records = engine.GetAssetImportPipeline().GetAssetIngestRecordsForTest();
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].Phase, Runtime::RuntimeAssetIngestPhase::Complete);
    EXPECT_EQ(records[0].Diagnostic, Runtime::RuntimeAssetIngestDiagnostic::None);
    ASSERT_TRUE(records[0].Result.has_value());
    EXPECT_EQ(records[0].Result->PrimitiveEntitiesCreated, 1u);

    const std::optional<Runtime::RuntimeAssetImportEvent>& lastEvent =
        engine.GetAssetImportPipeline().GetLastAssetImportEvent();
    ASSERT_TRUE(lastEvent.has_value());
    EXPECT_TRUE(lastEvent->Succeeded());
    EXPECT_EQ(lastEvent->IngestDiagnostic,
              Runtime::RuntimeAssetIngestDiagnostic::None);

    engine.Shutdown();
}

TEST(SandboxEditorUi, DroppedFileQueuePreservesOrderDiagnosticsAndClearCompleted)
{
    TmpFile meshFile(
        "runtime_queue_drop_mesh.obj",
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f 1 2 3\n");
    const std::filesystem::path missingFile =
        std::filesystem::temp_directory_path() / "runtime_queue_missing.obj";
    std::filesystem::remove(missingFile);

    Runtime::Engine engine(
        HeadlessConfig(),
        std::make_unique<FixedFrameApplication>(128u));
    engine.Initialize();
    InstallSandboxDefaultRuntimePolicies(engine);

    const std::vector<std::string> droppedPaths{
        meshFile.Path.string(),
        missingFile.string(),
    };
    engine.GetAssetImportPipeline().ImportDroppedFilePaths(droppedPaths);

    Runtime::RuntimeAssetImportQueueSnapshot queue =
        engine.GetAssetImportPipeline().GetAssetImportQueueSnapshot();
    ASSERT_EQ(queue.Entries.size(), 2u);
    EXPECT_EQ(queue.ActiveCount, 2u);
    EXPECT_EQ(queue.Entries[0].SourcePath, meshFile.Path.string());
    EXPECT_EQ(queue.Entries[1].SourcePath, missingFile.string());
    EXPECT_EQ(queue.Entries[0].Stage,
              Runtime::RuntimeAssetImportQueueStage::Decoding);
    EXPECT_TRUE(queue.Entries[0].CanCancel);

    ASSERT_FALSE(engine.GetWindow().ShouldClose())
        << "explicit Null window backend must keep Engine::Run() drivable on headless hosts";
    engine.Run();

    queue = engine.GetAssetImportPipeline().GetAssetImportQueueSnapshot();
    ASSERT_EQ(queue.Entries.size(), 2u);
    EXPECT_EQ(queue.ActiveCount, 0u);
    EXPECT_EQ(queue.TerminalCount, 2u);
    EXPECT_TRUE(queue.CanClearCompleted);
    EXPECT_EQ(queue.Entries[0].SourcePath, meshFile.Path.string());
    EXPECT_EQ(queue.Entries[0].TerminalStatus,
              Runtime::RuntimeAssetImportQueueTerminalStatus::Complete);
    EXPECT_EQ(queue.Entries[1].SourcePath, missingFile.string());
    EXPECT_EQ(queue.Entries[1].TerminalStatus,
              Runtime::RuntimeAssetImportQueueTerminalStatus::Failed);
    EXPECT_FALSE(queue.Entries[1].DiagnosticText.empty());
    EXPECT_EQ(CountEntitiesWithDomain(engine.GetScene(), GS::Domain::Mesh), 1u);

    EXPECT_EQ(engine.GetAssetImportPipeline().ClearCompletedAssetImports(), 2u);
    queue = engine.GetAssetImportPipeline().GetAssetImportQueueSnapshot();
    EXPECT_TRUE(queue.Entries.empty());

    engine.Shutdown();
}

TEST(SandboxEditorUi, DroppedGeometryQueueCancellationPreventsMainThreadApply)
{
    TmpFile meshFile(
        "runtime_queue_cancel_mesh.obj",
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f 1 2 3\n");

    Runtime::Engine engine(
        HeadlessConfig(),
        std::make_unique<FixedFrameApplication>(16u));
    engine.Initialize();

    const std::vector<std::string> droppedPaths{meshFile.Path.string()};
    engine.GetAssetImportPipeline().ImportDroppedFilePaths(droppedPaths);

    Runtime::RuntimeAssetImportQueueSnapshot queue =
        engine.GetAssetImportPipeline().GetAssetImportQueueSnapshot();
    ASSERT_EQ(queue.Entries.size(), 1u);
    EXPECT_TRUE(queue.Entries[0].CanCancel);
    EXPECT_TRUE(engine.GetAssetImportPipeline().CancelAssetImport(queue.Entries[0].Operation).has_value());

    queue = engine.GetAssetImportPipeline().GetAssetImportQueueSnapshot();
    ASSERT_EQ(queue.Entries.size(), 1u);
    EXPECT_EQ(queue.Entries[0].TerminalStatus,
              Runtime::RuntimeAssetImportQueueTerminalStatus::Cancelled);
    EXPECT_FALSE(queue.Entries[0].CanCancel);
    EXPECT_NE(queue.Entries[0].DiagnosticText.find("Cancelled"),
              std::string::npos);

    ASSERT_FALSE(engine.GetWindow().ShouldClose())
        << "explicit Null window backend must keep Engine::Run() drivable on headless hosts";
    engine.Run();

    EXPECT_EQ(CountEntitiesWithDomain(engine.GetScene(), GS::Domain::Mesh), 0u);

    engine.Shutdown();
}

TEST(SandboxEditorUi, DroppedGeometryAssetReimportReloadsSameAssetWithoutDuplicateEntity)
{
    TmpFile meshFile(
        "runtime_drop_reimport_mesh.obj",
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f 1 2 3\n");

    Runtime::Engine engine(
        HeadlessConfig(),
        std::make_unique<WaitForAssetImportEventApplication>(128u));
    engine.Initialize();
    InstallSandboxDefaultRuntimePolicies(engine);

    const std::vector<std::string> droppedPaths{meshFile.Path.string()};
    engine.GetAssetImportPipeline().ImportDroppedFilePaths(droppedPaths);

    ASSERT_FALSE(engine.GetWindow().ShouldClose())
        << "explicit Null window backend must keep Engine::Run() drivable on headless hosts";

    engine.Run();

    const std::optional<Runtime::RuntimeAssetImportEvent>& droppedEvent =
        engine.GetAssetImportPipeline().GetLastAssetImportEvent();
    ASSERT_TRUE(droppedEvent.has_value());
    ASSERT_TRUE(droppedEvent->Succeeded());
    ASSERT_TRUE(droppedEvent->Result.has_value());
    const Assets::AssetId droppedAsset = droppedEvent->Result->Asset;
    ASSERT_TRUE(droppedAsset.IsValid());
    EXPECT_EQ(CountEntitiesWithDomain(engine.GetScene(), GS::Domain::Mesh), 1u);
    const auto firstTicket =
        engine.GetAssetService().GetPayloadTicket(droppedAsset);
    ASSERT_TRUE(firstTicket.has_value());

    {
        std::ofstream out(meshFile.Path, std::ios::binary | std::ios::trunc);
        out << "v 0 0 0\n"
               "v 1 0 0\n"
               "v 0 1 0\n"
               "v 0 0 1\n"
               "f 1 2 3\n"
               "f 1 3 4\n";
    }

    auto reimported = engine.GetAssetImportPipeline().ReimportAsset(Runtime::RuntimeAssetReimportRequest{
        .Asset = droppedAsset,
    });
    ASSERT_TRUE(reimported.has_value()) << static_cast<int>(reimported.error());
    EXPECT_EQ(reimported->Asset, droppedAsset);
    EXPECT_EQ(reimported->PayloadKind, Assets::AssetPayloadKind::Mesh);
    EXPECT_EQ(reimported->PrimitiveEntitiesCreated, 0u);
    EXPECT_EQ(CountEntitiesWithDomain(engine.GetScene(), GS::Domain::Mesh), 1u);

    const auto secondTicket =
        engine.GetAssetService().GetPayloadTicket(droppedAsset);
    ASSERT_TRUE(secondTicket.has_value());
    EXPECT_EQ(secondTicket->slot, firstTicket->slot);
    EXPECT_GT(secondTicket->generation, firstTicket->generation);

    const std::vector<Runtime::RuntimeAssetIngestRecord> records =
        engine.GetAssetImportPipeline().GetAssetIngestRecordsForTest();
    ASSERT_EQ(records.size(), 2u);
    EXPECT_EQ(records[0].Request.Source,
              Runtime::RuntimeAssetIngestSource::DroppedFile);
    EXPECT_EQ(records[0].Phase, Runtime::RuntimeAssetIngestPhase::Complete);
    EXPECT_EQ(records[1].Request.Source,
              Runtime::RuntimeAssetIngestSource::Reimport);
    EXPECT_EQ(records[1].Request.ExistingAsset, droppedAsset);
    EXPECT_EQ(records[1].Phase, Runtime::RuntimeAssetIngestPhase::Complete);
    EXPECT_EQ(records[1].Diagnostic, Runtime::RuntimeAssetIngestDiagnostic::None);

    const std::optional<Runtime::RuntimeAssetImportEvent>& lastEvent =
        engine.GetAssetImportPipeline().GetLastAssetImportEvent();
    ASSERT_TRUE(lastEvent.has_value());
    EXPECT_TRUE(lastEvent->Succeeded());
    EXPECT_EQ(lastEvent->IngestDiagnostic,
              Runtime::RuntimeAssetIngestDiagnostic::None);
    ASSERT_TRUE(lastEvent->Result.has_value());
    EXPECT_EQ(lastEvent->Result->Asset, droppedAsset);

    engine.Shutdown();
}

TEST(SandboxEditorUi, PlatformDropEventImportsObjMeshSelectsItAndEnablesRenderComponents)
{
    TmpFile meshFile(
        "runtime_platform_drop_mesh.obj",
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "vt 0 0\n"
        "vt 1 0\n"
        "vt 0 1\n"
        "f 1/1 2/2 3/3\n");

    Runtime::Engine engine(
        HeadlessConfig(),
        std::make_unique<WaitForAssetImportEventApplication>(128u));
    engine.Initialize();
    InstallSandboxDefaultRuntimePolicies(engine);

    Runtime::SandboxEditorUi ui;
    ui.Attach(engine);

    engine.DispatchPlatformEventForTest(Plat::WindowDropEvent{
        .Paths = {meshFile.Path.string()},
    });

    ASSERT_FALSE(engine.GetWindow().ShouldClose())
        << "explicit Null window backend must keep Engine::Run() drivable on headless hosts";

    engine.Run();

    EXPECT_EQ(CountEntitiesWithDomain(engine.GetScene(), GS::Domain::Mesh), 1u);
    const std::optional<Runtime::RuntimeAssetImportEvent>& lastEvent =
        engine.GetAssetImportPipeline().GetLastAssetImportEvent();
    ASSERT_TRUE(lastEvent.has_value());
    EXPECT_TRUE(lastEvent->Succeeded());
    ASSERT_TRUE(lastEvent->Result.has_value());
    EXPECT_EQ(lastEvent->Result->PayloadKind, Assets::AssetPayloadKind::Mesh);

    const std::optional<ECS::EntityHandle> meshEntity =
        FindFirstEntityWithDomain(engine.GetScene(), GS::Domain::Mesh);
    ASSERT_TRUE(meshEntity.has_value());
    const std::uint32_t stableId =
        Runtime::SelectionController::ToStableEntityId(*meshEntity);
    const auto selectedIds = engine.GetSelectionController().SelectedStableIds();
    ASSERT_EQ(selectedIds.size(), 1u);
    EXPECT_EQ(selectedIds[0], stableId);

    Runtime::SandboxEditorContext commandContext =
        MakeContext(engine.GetScene(), engine.GetSelectionController());

    EXPECT_EQ(Runtime::ApplySandboxEditorPrimitiveViewCommand(
                  commandContext,
                  Runtime::SandboxEditorPrimitiveViewCommand{
                      .StableEntityId = stableId,
                      .SetEdgeView = true,
                      .EnableEdgeView = true,
                      .SetVertexView = true,
                      .EnableVertexView = true,
                      .SetVertexRenderMode = true,
                      .VertexRenderMode =
                          Runtime::MeshVertexViewRenderMode::ImpostorSphere,
                      .SetVertexPointRadius = true,
                      .VertexPointRadiusPx = 10.0f,
                  }),
              Runtime::SandboxEditorCommandStatus::Applied);

    auto& raw = engine.GetScene().Raw();
    ASSERT_TRUE(raw.all_of<G::RenderEdges>(*meshEntity));
    ASSERT_TRUE(raw.all_of<G::RenderPoints>(*meshEntity));
    const G::RenderPoints& points = raw.get<G::RenderPoints>(*meshEntity);
    EXPECT_EQ(points.Type, G::RenderPoints::RenderType::Sphere);
    ASSERT_NE(std::get_if<float>(&points.SizeSource), nullptr);
    EXPECT_FLOAT_EQ(*std::get_if<float>(&points.SizeSource), 10.0f);

    Runtime::RenderExtractionCache extraction;
    const Runtime::RuntimeRenderExtractionStats stats =
        extraction.ExtractAndSubmit(engine.GetScene(),
                                    engine.GetRenderer(),
                                    &engine.GetGpuAssetCache());
    EXPECT_EQ(stats.MeshGeometryUploads, 1u);
    EXPECT_EQ(stats.MeshEdgeViewUploads, 1u);
    EXPECT_EQ(stats.MeshVertexViewUploads, 1u);
    const auto sidecar = extraction.FindRenderableSidecarForTest(stableId);
    ASSERT_TRUE(sidecar.has_value());
    const auto config = engine.GetRenderer()
                            .GetGpuWorld()
                            .GetEntityConfigForTest(sidecar->MeshVertexViewInstance);
    EXPECT_EQ(config.Point.PointMode, 1u);
    EXPECT_FLOAT_EQ(config.Point.PointSize, 10.0f);

    extraction.Shutdown(engine.GetRenderer());
    ui.Detach();
    engine.Shutdown();
}

TEST(SandboxEditorUi, PlatformDropNoUvObjUploadsRawSurfaceBeforeDeferredPostProcess)
{
    TmpFile meshFile(
        "runtime_platform_drop_no_uv_mesh.obj",
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f 1 2 3\n");

    Runtime::Engine engine(
        HeadlessConfig(),
        std::make_unique<WaitForAssetImportEventApplication>(128u));
    engine.Initialize();
    InstallSandboxDefaultRuntimePolicies(engine);

    engine.DispatchPlatformEventForTest(Plat::WindowDropEvent{
        .Paths = {meshFile.Path.string()},
    });

    ASSERT_FALSE(engine.GetWindow().ShouldClose())
        << "explicit Null window backend must keep Engine::Run() drivable on headless hosts";

    engine.Run();

    EXPECT_EQ(CountEntitiesWithDomain(engine.GetScene(), GS::Domain::Mesh), 1u);
    const std::optional<Runtime::RuntimeAssetImportEvent>& lastEvent =
        engine.GetAssetImportPipeline().GetLastAssetImportEvent();
    ASSERT_TRUE(lastEvent.has_value());
    EXPECT_TRUE(lastEvent->Succeeded());
    ASSERT_TRUE(lastEvent->Result.has_value());
    EXPECT_EQ(lastEvent->Result->PayloadKind, Assets::AssetPayloadKind::Mesh);
    EXPECT_EQ(lastEvent->Result->PrimitiveEntitiesCreated, 1u);

    const Runtime::RuntimeRenderExtractionStats& stats =
        engine.GetLastRenderExtractionStats();
    EXPECT_EQ(stats.CandidateRenderableCount, 1u);
    EXPECT_EQ(stats.MeshGeometryUploads, 1u);
    EXPECT_EQ(stats.MeshGeometryMissingTexcoords, 1u);
    EXPECT_EQ(stats.MeshGeometryNonFiniteTexcoords, 0u);
    EXPECT_EQ(stats.MeshGeometryFailedPack, 0u);
    EXPECT_EQ(stats.MeshGeometryInvalidTopology, 0u);
    EXPECT_GE(engine.GetRenderer().GetGpuWorld().GetLiveGeometryCount(), 1u);

    engine.Shutdown();
}

TEST(SandboxEditorUi, DroppedFileImportFailureLogsDiagnostics)
{
    const std::filesystem::path missingMeshPath =
        std::filesystem::temp_directory_path() /
        "runtime_platform_drop_missing_mesh.obj";
    std::error_code ec;
    std::filesystem::remove(missingMeshPath, ec);

    Runtime::Engine engine(
        HeadlessConfig(),
        std::make_unique<WaitForAssetImportEventApplication>(128u));
    engine.Initialize();

    Core::Log::ClearEntries();

    engine.DispatchPlatformEventForTest(Plat::WindowDropEvent{
        .Paths = {missingMeshPath.string()},
    });

    const Core::Log::LogSnapshot queuedLogs = Core::Log::TakeSnapshot();
    EXPECT_TRUE(LogSnapshotContains(queuedLogs, "File drop received"))
        << "The platform drop boundary must log receipt before deferred import work completes.";
    EXPECT_TRUE(LogSnapshotContains(queuedLogs, "Queued dropped geometry import"))
        << "Dropped geometry imports must log that they were queued off the platform polling path.";
    EXPECT_FALSE(engine.GetAssetImportPipeline().GetLastAssetImportEvent().has_value());

    ASSERT_FALSE(engine.GetWindow().ShouldClose())
        << "explicit Null window backend must keep Engine::Run() drivable on headless hosts";

    engine.Run();

    const std::optional<Runtime::RuntimeAssetImportEvent>& lastEvent =
        engine.GetAssetImportPipeline().GetLastAssetImportEvent();
    ASSERT_TRUE(lastEvent.has_value());
    EXPECT_FALSE(lastEvent->Succeeded());
    EXPECT_EQ(lastEvent->RequestedPayloadKind, Assets::AssetPayloadKind::Mesh);
    EXPECT_EQ(lastEvent->Error, Core::ErrorCode::FileNotFound);

    const Core::Log::LogSnapshot completedLogs = Core::Log::TakeSnapshot();
    EXPECT_TRUE(LogSnapshotContains(completedLogs, "Asset import failed"));
    EXPECT_TRUE(LogSnapshotContains(completedLogs, "FileNotFound"));
    EXPECT_TRUE(LogSnapshotContains(completedLogs, "Mesh"));
    EXPECT_TRUE(LogSnapshotContains(completedLogs,
                                    missingMeshPath.filename().string()));

    engine.Shutdown();
}

TEST(SandboxEditorUi, PlatformDropEventImportsOffMesh)
{
    TmpFile meshFile(
        "runtime_platform_drop_mesh.off",
        "OFF\n"
        "3 1 3\n"
        "0 0 0\n"
        "1 0 0\n"
        "0 1 0\n"
        "3 0 1 2\n");

    Runtime::Engine engine(
        HeadlessConfig(),
        std::make_unique<WaitForAssetImportEventApplication>(128u));
    engine.Initialize();
    InstallSandboxDefaultRuntimePolicies(engine);

    engine.DispatchPlatformEventForTest(Plat::WindowDropEvent{
        .Paths = {meshFile.Path.string()},
    });

    ASSERT_FALSE(engine.GetWindow().ShouldClose())
        << "explicit Null window backend must keep Engine::Run() drivable on headless hosts";

    engine.Run();

    EXPECT_EQ(CountEntitiesWithDomain(engine.GetScene(), GS::Domain::Mesh), 1u);
    const std::optional<Runtime::RuntimeAssetImportEvent>& lastEvent =
        engine.GetAssetImportPipeline().GetLastAssetImportEvent();
    ASSERT_TRUE(lastEvent.has_value());
    EXPECT_TRUE(lastEvent->Succeeded());
    ASSERT_TRUE(lastEvent->Result.has_value());
    EXPECT_EQ(lastEvent->Result->PayloadKind, Assets::AssetPayloadKind::Mesh);

    const std::optional<ECS::EntityHandle> meshEntity =
        FindFirstEntityWithDomain(engine.GetScene(), GS::Domain::Mesh);
    ASSERT_TRUE(meshEntity.has_value());
    const auto selectedIds = engine.GetSelectionController().SelectedStableIds();
    ASSERT_EQ(selectedIds.size(), 1u);
    EXPECT_EQ(selectedIds[0],
              Runtime::SelectionController::ToStableEntityId(*meshEntity));

    engine.Shutdown();
}

TEST(SandboxEditorUi, ConfiguredBackendBornClosedLogsZeroFrameRunDiagnostic)
{
    const std::string engineSource =
        ReadRepositoryTextFile("src/runtime/Runtime.Engine.cpp");
    ASSERT_FALSE(engineSource.empty());
    EXPECT_NE(engineSource.find("m_Window && m_Window->ShouldClose()"),
              std::string::npos);
    EXPECT_NE(engineSource.find("Platform window initialized closed"),
              std::string::npos);
    EXPECT_NE(engineSource.find("Engine::Run() will execute zero frames"),
              std::string::npos);
}

TEST(SandboxEditorUi, PlatformCloseEventStopsEngineRunState)
{
    Runtime::Engine engine(HeadlessConfig(), std::make_unique<PassiveApplication>());
    engine.Initialize();

    ASSERT_TRUE(engine.IsRunning());
    ASSERT_FALSE(engine.GetWindow().ShouldClose())
        << "explicit Null window backend must keep Engine::Run() drivable on headless hosts";

    Core::Log::ClearEntries();
    engine.DispatchPlatformEventForTest(Plat::WindowCloseEvent{});

    const Core::Log::LogSnapshot closeLogs = Core::Log::TakeSnapshot();
    EXPECT_TRUE(LogSnapshotContains(closeLogs,
                                    Core::Log::Level::Info,
                                    "Window close requested"))
        << "Sandbox window close must leave an [INFO] close breadcrumb.";

    engine.Run();

    EXPECT_FALSE(engine.IsRunning());

    engine.Shutdown();
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

TEST(SandboxEditorUi, RenderRecipeEditorDraftValidationPreviewActivationAndCancel)
{
    Graphics::RenderRecipeConfigContext recipeContext =
        MakeRenderRecipeConfigContext();
    Runtime::SandboxEditorRenderRecipeEditorState editorState{};
    Runtime::SandboxEditorContext context = MakeRenderRecipeEditorContext(
        recipeContext,
        editorState);
    const std::string validDocument = ValidSandboxRenderRecipeConfig();

    Runtime::SandboxEditorRenderRecipeCommandResult result =
        Runtime::ApplySandboxEditorRenderRecipeCommand(
            context,
            Runtime::SandboxEditorRenderRecipeCommand{
                .Kind = Runtime::SandboxEditorRenderRecipeCommandKind::UpdateDraft,
                .Document = validDocument,
                .SourceId = "valid-preview.json",
                .Debounced = true,
            });
    ASSERT_TRUE(result.Succeeded());
    EXPECT_EQ(result.Status,
              Runtime::SandboxEditorRenderRecipeCommandStatus::Debounced);
    EXPECT_EQ(editorState.DraftState,
              Runtime::SandboxEditorRenderRecipeDraftState::Debounced);
    EXPECT_EQ(editorState.DraftRevision, 1u);

    result = Runtime::ApplySandboxEditorRenderRecipeCommand(
        context,
        Runtime::SandboxEditorRenderRecipeCommand{
            .Kind = Runtime::SandboxEditorRenderRecipeCommandKind::ValidateDraft,
            .Document = "{not valid json",
            .SourceId = "invalid-preview.json",
        });
    EXPECT_FALSE(result.Succeeded());
    EXPECT_EQ(result.Status,
              Runtime::SandboxEditorRenderRecipeCommandStatus::ValidationFailed);
    EXPECT_EQ(editorState.DraftState,
              Runtime::SandboxEditorRenderRecipeDraftState::Rejected);
    EXPECT_FALSE(result.RecipeDiagnostics.empty());

    result = Runtime::ApplySandboxEditorRenderRecipeCommand(
        context,
        Runtime::SandboxEditorRenderRecipeCommand{
            .Kind = Runtime::SandboxEditorRenderRecipeCommandKind::PreviewDraft,
            .Document = std::string{R"json({
  "schema": ")json"} + std::string{Graphics::kRenderRecipeConfigSchemaId} +
                        R"json(",
  "version": 1,
  "rendererId": ")json" +
                        std::string{Graphics::kCurrentRendererContractId} +
                        R"json(",
  "recipe": {"slots": [{"name": "ray-traced-gi"}]}
})json",
            .SourceId = "unsupported-preview.json",
        });
    EXPECT_FALSE(result.Succeeded());
    EXPECT_EQ(result.Status,
              Runtime::SandboxEditorRenderRecipeCommandStatus::PreviewFailed);
    EXPECT_EQ(editorState.DraftState,
              Runtime::SandboxEditorRenderRecipeDraftState::Rejected);

    result = Runtime::ApplySandboxEditorRenderRecipeCommand(
        context,
        Runtime::SandboxEditorRenderRecipeCommand{
            .Kind = Runtime::SandboxEditorRenderRecipeCommandKind::PreviewDraft,
            .Document = validDocument,
            .SourceId = "valid-preview.json",
        });
    ASSERT_TRUE(result.Succeeded());
    EXPECT_EQ(result.Status,
              Runtime::SandboxEditorRenderRecipeCommandStatus::Previewed);
    EXPECT_EQ(editorState.DraftState,
              Runtime::SandboxEditorRenderRecipeDraftState::Previewed);
    ASSERT_TRUE(editorState.HasLastPreview);
    EXPECT_TRUE(Graphics::IsConfigUsable(editorState.LastPreview));

    Runtime::SandboxEditorRenderRecipeEditorModel model =
        Runtime::BuildSandboxEditorRenderRecipeEditorModel(context);
    EXPECT_TRUE(model.CanActivate);
    EXPECT_EQ(model.DraftRecipeId, "current-renderer.user-preview");

    result = Runtime::ApplySandboxEditorRenderRecipeCommand(
        context,
        Runtime::SandboxEditorRenderRecipeCommand{
            .Kind =
                Runtime::SandboxEditorRenderRecipeCommandKind::ActivatePreview,
        });
    ASSERT_TRUE(result.Succeeded());
    EXPECT_EQ(result.Status,
              Runtime::SandboxEditorRenderRecipeCommandStatus::Activated);
    EXPECT_EQ(editorState.DraftState,
              Runtime::SandboxEditorRenderRecipeDraftState::Activated);
    EXPECT_TRUE(editorState.HasActiveOverride);
    EXPECT_EQ(editorState.ActiveRevision, 1u);

    model = Runtime::BuildSandboxEditorRenderRecipeEditorModel(context);
    EXPECT_EQ(model.ActiveRecipeId, "current-renderer.user-preview");

    result = Runtime::ApplySandboxEditorRenderRecipeCommand(
        context,
        Runtime::SandboxEditorRenderRecipeCommand{
            .Kind = Runtime::SandboxEditorRenderRecipeCommandKind::CancelDraft,
        });
    ASSERT_TRUE(result.Succeeded());
    EXPECT_EQ(result.Status,
              Runtime::SandboxEditorRenderRecipeCommandStatus::Canceled);
    EXPECT_EQ(editorState.DraftState,
              Runtime::SandboxEditorRenderRecipeDraftState::Canceled);
    EXPECT_TRUE(editorState.DraftDocument.empty());
    EXPECT_FALSE(editorState.HasLastPreview);

    model = Runtime::BuildSandboxEditorRenderRecipeEditorModel(context);
    EXPECT_FALSE(model.CanCancel);
}

TEST(SandboxEditorUi, RenderRecipeEditorUnchangedDraftIsNoOp)
{
    Graphics::RenderRecipeConfigContext recipeContext =
        MakeRenderRecipeConfigContext();
    Runtime::SandboxEditorRenderRecipeEditorState editorState{};
    Runtime::SandboxEditorContext context = MakeRenderRecipeEditorContext(
        recipeContext,
        editorState);
    const std::string validDocument = ValidSandboxRenderRecipeConfig();

    Runtime::SandboxEditorRenderRecipeCommandResult result =
        Runtime::ApplySandboxEditorRenderRecipeCommand(
            context,
            Runtime::SandboxEditorRenderRecipeCommand{
                .Kind = Runtime::SandboxEditorRenderRecipeCommandKind::UpdateDraft,
                .Document = validDocument,
                .SourceId = "stable-draft.json",
            });
    ASSERT_TRUE(result.Succeeded());
    EXPECT_EQ(result.Status,
              Runtime::SandboxEditorRenderRecipeCommandStatus::DraftUpdated);
    EXPECT_EQ(editorState.DraftRevision, 1u);

    result = Runtime::ApplySandboxEditorRenderRecipeCommand(
        context,
        Runtime::SandboxEditorRenderRecipeCommand{
            .Kind = Runtime::SandboxEditorRenderRecipeCommandKind::UpdateDraft,
            .Document = validDocument,
            .SourceId = "stable-draft.json",
        });
    EXPECT_TRUE(result.Succeeded());
    EXPECT_EQ(result.Status,
              Runtime::SandboxEditorRenderRecipeCommandStatus::NoChange);
    EXPECT_EQ(editorState.DraftRevision, 1u);
}

TEST(SandboxEditorUi, RenderRecipeEditorArtifactPublishAndApplyUseRegistry)
{
    Graphics::RenderRecipeConfigContext recipeContext =
        MakeRenderRecipeConfigContext();
    Runtime::SandboxEditorRenderRecipeEditorState editorState{};
    Runtime::RenderArtifactRegistry artifacts;
    ASSERT_TRUE(artifacts.RegisterArtifact(
                            MakeSandboxRenderArtifact("sandbox-candidate"))
                    .Succeeded());
    Runtime::SandboxEditorContext context = MakeRenderRecipeEditorContext(
        recipeContext,
        editorState,
        &artifacts);

    Runtime::SandboxEditorRenderRecipeCommandResult result =
        Runtime::ApplySandboxEditorRenderRecipeCommand(
            context,
            Runtime::SandboxEditorRenderRecipeCommand{
                .Kind =
                    Runtime::SandboxEditorRenderRecipeCommandKind::PublishArtifact,
                .ArtifactId = "sandbox-candidate",
                .Provenance = "sandbox editor test",
            });
    ASSERT_TRUE(result.Succeeded());
    EXPECT_EQ(result.Status,
              Runtime::SandboxEditorRenderRecipeCommandStatus::Published);
    EXPECT_EQ(result.ArtifactState,
              Runtime::RenderArtifactPublicationState::Published);

    Runtime::SandboxEditorRenderRecipeEditorModel model =
        Runtime::BuildSandboxEditorRenderRecipeEditorModel(context);
    const Runtime::SandboxEditorRenderArtifactRow* artifact =
        FindRenderArtifactRow(model, "sandbox-candidate");
    ASSERT_NE(artifact, nullptr);
    EXPECT_FALSE(artifact->CanPublish);
    EXPECT_TRUE(artifact->CanApply);

    result = Runtime::ApplySandboxEditorRenderRecipeCommand(
        context,
        Runtime::SandboxEditorRenderRecipeCommand{
            .Kind =
                Runtime::SandboxEditorRenderRecipeCommandKind::ApplyArtifact,
            .ArtifactId = "sandbox-candidate",
            .Provenance = "sandbox editor test",
            .ProjectTarget = "scene.preview.accepted",
        });
    ASSERT_TRUE(result.Succeeded());
    EXPECT_EQ(result.Status,
              Runtime::SandboxEditorRenderRecipeCommandStatus::Applied);
    EXPECT_TRUE(result.ProjectMutationAuthorized);
    EXPECT_EQ(result.ArtifactState,
              Runtime::RenderArtifactPublicationState::Applied);

    Runtime::SandboxEditorContext missingRegistry =
        MakeRenderRecipeEditorContext(recipeContext, editorState, nullptr);
    result = Runtime::ApplySandboxEditorRenderRecipeCommand(
        missingRegistry,
        Runtime::SandboxEditorRenderRecipeCommand{
            .Kind =
                Runtime::SandboxEditorRenderRecipeCommandKind::PublishArtifact,
            .ArtifactId = "sandbox-candidate",
            .Provenance = "sandbox editor test",
        });
    EXPECT_FALSE(result.Succeeded());
    EXPECT_EQ(result.Status,
              Runtime::SandboxEditorRenderRecipeCommandStatus::MissingArtifactRegistry);
}

TEST(SandboxEditorUi, EngineAttachmentRegistersEditorCallback)
{
    Runtime::Engine engine(HeadlessConfig(), std::make_unique<OneFrameApplication>());
    engine.Initialize();

    Runtime::SandboxEditorUi ui;
    ui.Attach(engine);
    EXPECT_TRUE(ui.IsAttached());

    ASSERT_FALSE(engine.GetWindow().ShouldClose())
        << "explicit Null window backend must keep Engine::Run() drivable on headless hosts";

    engine.Run();

    EXPECT_GE(engine.GetImGuiAdapter().GetDiagnostics().EditorCallbackInvocations, 1u);
    EXPECT_TRUE(ui.GetLastFrame().FileImport.Enabled);
    EXPECT_FALSE(HasDiagnostic(ui.GetLastFrame().FileImport.Diagnostics,
                               Runtime::SandboxEditorDiagnosticCode::AssetImportUnavailable));

    ui.Detach();
    engine.Shutdown();
}

TEST(SandboxEditorUi, RegistrationCommandAlignsSourceOntoTargetAndSupportsUndoRedo)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;

    const std::vector<glm::vec3> target = MakeRegistrationCloud();
    const glm::vec3 offset{0.05f, -0.03f, 0.02f};
    std::vector<glm::vec3> sourcePoints{};
    sourcePoints.reserve(target.size());
    for (const glm::vec3& p : target)
        sourcePoints.push_back(p + offset);

    const ECS::EntityHandle source =
        MakePointCloudEntity(registry, "ICP Source", sourcePoints);
    const ECS::EntityHandle targetEntity =
        MakePointCloudEntity(registry, "ICP Target", target);
    const std::uint32_t sourceId =
        Runtime::SelectionController::ToStableEntityId(source);
    const std::uint32_t targetId =
        Runtime::SelectionController::ToStableEntityId(targetEntity);
    const glm::vec3 originalPosition =
        registry.Raw().get<ECSC::Transform::Component>(source).Position;

    const Runtime::SandboxEditorRegistrationResult result =
        Runtime::ApplySandboxEditorRegistrationCommand(
            context,
            Runtime::SandboxEditorRegistrationCommand{
                .SourceStableEntityId = sourceId,
                .TargetStableEntityId = targetId,
                .Variant = Runtime::SandboxEditorICPVariant::PointToPoint,
                .MaxIterations = 60u,
                .InlierRatio = 1.0,
                .TrajectoryStep = 1000u,
            });

    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_TRUE(result.HasResult);
    EXPECT_EQ(result.SourcePointCount, sourcePoints.size());
    EXPECT_EQ(result.TargetPointCount, target.size());
    EXPECT_GT(result.IterationsPerformed, 0u);
    EXPECT_EQ(result.TrajectoryLength, result.IterationsPerformed);
    EXPECT_EQ(result.AppliedStep, result.TrajectoryLength);
    EXPECT_LT(result.FinalRMSE, 1.0e-3);

    // ICP recovers the transform mapping source (= target + offset) back onto
    // target: a pure translation by -offset with identity rotation.
    const ECSC::Transform::Component& aligned =
        registry.Raw().get<ECSC::Transform::Component>(source);
    EXPECT_NEAR(aligned.Position.x, -offset.x, 1.0e-2f);
    EXPECT_NEAR(aligned.Position.y, -offset.y, 1.0e-2f);
    EXPECT_NEAR(aligned.Position.z, -offset.z, 1.0e-2f);
    EXPECT_NEAR(std::abs(aligned.Rotation.w), 1.0f, 1.0e-2f);

    ASSERT_TRUE(history.CanUndo());
    EXPECT_EQ(history.Undo().Status,
              Runtime::EditorCommandHistoryStatus::Undone);
    const glm::vec3 undone =
        registry.Raw().get<ECSC::Transform::Component>(source).Position;
    EXPECT_NEAR(undone.x, originalPosition.x, 1.0e-5f);
    EXPECT_NEAR(undone.y, originalPosition.y, 1.0e-5f);
    EXPECT_NEAR(undone.z, originalPosition.z, 1.0e-5f);
    EXPECT_EQ(history.Redo().Status,
              Runtime::EditorCommandHistoryStatus::Redone);
    EXPECT_NEAR(
        registry.Raw().get<ECSC::Transform::Component>(source).Position.x,
        -offset.x, 1.0e-2f);
}

TEST(SandboxEditorUi, RegistrationRequestQueuesDerivedJobAndPublishesOnApply)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    context.CommandHistory = &history;
    Runtime::StreamingExecutor executor{};
    Runtime::DerivedJobRegistry jobs{executor};
    AttachDerivedJobCommands(context, jobs);
    std::optional<Runtime::SandboxEditorRegistrationResult> completedResult{};
    context.MethodResultSinks.Registration =
        [&completedResult](Runtime::SandboxEditorRegistrationResult result)
        {
            completedResult = std::move(result);
        };

    const std::vector<glm::vec3> target = MakeRegistrationCloud();
    const glm::vec3 offset{0.05f, -0.03f, 0.02f};
    std::vector<glm::vec3> sourcePoints{};
    sourcePoints.reserve(target.size());
    for (const glm::vec3& p : target)
        sourcePoints.push_back(p + offset);

    const ECS::EntityHandle source =
        MakePointCloudEntity(registry, "Queued ICP Source", sourcePoints);
    const ECS::EntityHandle targetEntity =
        MakePointCloudEntity(registry, "Queued ICP Target", target);
    const std::uint32_t sourceId =
        Runtime::SelectionController::ToStableEntityId(source);
    const std::uint32_t targetId =
        Runtime::SelectionController::ToStableEntityId(targetEntity);
    const ECSC::Transform::Component before =
        registry.Raw().get<ECSC::Transform::Component>(source);

    const Runtime::SandboxEditorRegistrationResult result =
        Runtime::ApplySandboxEditorRegistrationCommand(
            context,
            Runtime::SandboxEditorRegistrationCommand{
                .SourceStableEntityId = sourceId,
                .TargetStableEntityId = targetId,
                .Variant = Runtime::SandboxEditorICPVariant::PointToPoint,
                .MaxIterations = 60u,
                .InlierRatio = 1.0,
                .TrajectoryStep = 1000u,
            });

    EXPECT_EQ(result.Status, Runtime::SandboxEditorCommandStatus::Pending);
    EXPECT_EQ(result.SourcePointCount, sourcePoints.size());
    EXPECT_EQ(result.TargetPointCount, target.size());
    EXPECT_NE(result.Message.find("queued"), std::string::npos);
    const ECSC::Transform::Component& pending =
        registry.Raw().get<ECSC::Transform::Component>(source);
    EXPECT_NEAR(pending.Position.x, before.Position.x, 1.0e-6f);
    EXPECT_NEAR(pending.Position.y, before.Position.y, 1.0e-6f);
    EXPECT_NEAR(pending.Position.z, before.Position.z, 1.0e-6f);
    EXPECT_FALSE(registry.Raw().all_of<ECSC::Transform::IsDirtyTag>(source));

    Runtime::DerivedJobQueueSnapshot queued = jobs.SnapshotAll();
    ASSERT_EQ(queued.Entries.size(), 1u);
    EXPECT_EQ(queued.Entries[0].Name, "Sandbox.RegistrationICP.CPU");
    EXPECT_EQ(queued.Entries[0].Status, Runtime::DerivedJobStatus::Queued);

    jobs.Pump(1u);
    jobs.DrainCompletions();
    EXPECT_FALSE(completedResult.has_value());
    EXPECT_NEAR(
        registry.Raw().get<ECSC::Transform::Component>(source).Position.x,
        before.Position.x,
        1.0e-6f);

    EXPECT_EQ(jobs.ApplyMainThreadResults(1u), 1u);
    Runtime::DerivedJobQueueSnapshot done = jobs.SnapshotAll();
    ASSERT_EQ(done.Entries.size(), 1u);
    EXPECT_EQ(done.Entries[0].Status, Runtime::DerivedJobStatus::Complete);
    ASSERT_TRUE(completedResult.has_value());
    EXPECT_TRUE(completedResult->Succeeded()) << completedResult->Message;
    EXPECT_TRUE(completedResult->HasResult);
    EXPECT_GT(completedResult->IterationsPerformed, 0u);
    EXPECT_EQ(completedResult->AppliedStep,
              completedResult->TrajectoryLength);
    EXPECT_LT(completedResult->FinalRMSE, 1.0e-3);

    const ECSC::Transform::Component& aligned =
        registry.Raw().get<ECSC::Transform::Component>(source);
    EXPECT_NEAR(aligned.Position.x, -offset.x, 1.0e-2f);
    EXPECT_NEAR(aligned.Position.y, -offset.y, 1.0e-2f);
    EXPECT_NEAR(aligned.Position.z, -offset.z, 1.0e-2f);
    EXPECT_TRUE(registry.Raw().all_of<ECSC::Transform::IsDirtyTag>(source));
    EXPECT_TRUE(history.IsDirty());
    ASSERT_TRUE(history.CanUndo());
    EXPECT_EQ(history.Undo().Status,
              Runtime::EditorCommandHistoryStatus::Undone);
    const ECSC::Transform::Component& undone =
        registry.Raw().get<ECSC::Transform::Component>(source);
    EXPECT_NEAR(undone.Position.x, before.Position.x, 1.0e-6f);
    EXPECT_NEAR(undone.Position.y, before.Position.y, 1.0e-6f);
    EXPECT_NEAR(undone.Position.z, before.Position.z, 1.0e-6f);
}

TEST(SandboxEditorUi, RegistrationDerivedJobDiscardsStaleSourceBeforeApply)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
    Runtime::StreamingExecutor executor{};
    Runtime::DerivedJobRegistry jobs{executor};
    AttachDerivedJobCommands(context, jobs);
    bool completedSinkCalled = false;
    context.MethodResultSinks.Registration =
        [&completedSinkCalled](Runtime::SandboxEditorRegistrationResult)
        {
            completedSinkCalled = true;
        };

    const std::vector<glm::vec3> target = MakeRegistrationCloud();
    const glm::vec3 offset{0.05f, -0.03f, 0.02f};
    std::vector<glm::vec3> sourcePoints{};
    sourcePoints.reserve(target.size());
    for (const glm::vec3& p : target)
        sourcePoints.push_back(p + offset);

    const ECS::EntityHandle source =
        MakePointCloudEntity(registry, "Stale ICP Source", sourcePoints);
    const ECS::EntityHandle targetEntity =
        MakePointCloudEntity(registry, "Stale ICP Target", target);
    const std::uint32_t sourceId =
        Runtime::SelectionController::ToStableEntityId(source);
    const std::uint32_t targetId =
        Runtime::SelectionController::ToStableEntityId(targetEntity);

    const Runtime::SandboxEditorRegistrationResult result =
        Runtime::ApplySandboxEditorRegistrationCommand(
            context,
            Runtime::SandboxEditorRegistrationCommand{
                .SourceStableEntityId = sourceId,
                .TargetStableEntityId = targetId,
                .Variant = Runtime::SandboxEditorICPVariant::PointToPoint,
                .MaxIterations = 60u,
                .InlierRatio = 1.0,
                .TrajectoryStep = 1000u,
            });
    ASSERT_EQ(result.Status, Runtime::SandboxEditorCommandStatus::Pending);

    std::vector<glm::vec3> staleSourcePoints = sourcePoints;
    for (glm::vec3& point : staleSourcePoints)
        point += glm::vec3{10.0f, 0.0f, 0.0f};
    SetPositions(registry.Raw().get<GS::Vertices>(source), staleSourcePoints);

    jobs.Pump(1u);
    jobs.DrainCompletions();
    EXPECT_EQ(jobs.ApplyMainThreadResults(1u), 1u);

    Runtime::DerivedJobQueueSnapshot done = jobs.SnapshotAll();
    ASSERT_EQ(done.Entries.size(), 1u);
    EXPECT_EQ(done.Entries[0].Status,
              Runtime::DerivedJobStatus::StaleDiscarded);
    EXPECT_NE(done.Entries[0].Diagnostic.find(
                  "StaleSourcePropertyGeneration"),
              std::string::npos);
    EXPECT_FALSE(completedSinkCalled);
    const ECSC::Transform::Component& transform =
        registry.Raw().get<ECSC::Transform::Component>(source);
    EXPECT_NEAR(transform.Position.x, 0.0f, 1.0e-6f);
    EXPECT_NEAR(transform.Position.y, 0.0f, 1.0e-6f);
    EXPECT_NEAR(transform.Position.z, 0.0f, 1.0e-6f);
    EXPECT_FALSE(registry.Raw().all_of<ECSC::Transform::IsDirtyTag>(source));
}

TEST(SandboxEditorUi, RegistrationCommandFailsClosedForInvalidSelectionAndParameters)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);

    const std::vector<glm::vec3> cloud = MakeRegistrationCloud();
    const ECS::EntityHandle source =
        MakePointCloudEntity(registry, "Src", cloud);
    const ECS::EntityHandle target =
        MakePointCloudEntity(registry, "Tgt", cloud);
    const ECS::EntityHandle mesh = MakeSelectable(registry, "Mesh");
    AddIcosahedronMeshSource(registry, mesh);
    const std::uint32_t sourceId =
        Runtime::SelectionController::ToStableEntityId(source);
    const std::uint32_t targetId =
        Runtime::SelectionController::ToStableEntityId(target);
    const std::uint32_t meshId =
        Runtime::SelectionController::ToStableEntityId(mesh);

    EXPECT_EQ(
        Runtime::ApplySandboxEditorRegistrationCommand(
            context,
            Runtime::SandboxEditorRegistrationCommand{
                .SourceStableEntityId = sourceId,
                .TargetStableEntityId = sourceId,
            })
            .Status,
        Runtime::SandboxEditorCommandStatus::InvalidProcessingParameters);

    EXPECT_EQ(
        Runtime::ApplySandboxEditorRegistrationCommand(
            context,
            Runtime::SandboxEditorRegistrationCommand{
                .SourceStableEntityId = sourceId,
                .TargetStableEntityId = targetId,
                .MaxIterations = 0u,
            })
            .Status,
        Runtime::SandboxEditorCommandStatus::InvalidProcessingParameters);

    EXPECT_EQ(
        Runtime::ApplySandboxEditorRegistrationCommand(
            context,
            Runtime::SandboxEditorRegistrationCommand{
                .SourceStableEntityId = sourceId,
                .TargetStableEntityId = targetId + 9999u,
            })
            .Status,
        Runtime::SandboxEditorCommandStatus::StaleEntity);

    EXPECT_EQ(
        Runtime::ApplySandboxEditorRegistrationCommand(
            context,
            Runtime::SandboxEditorRegistrationCommand{
                .SourceStableEntityId = sourceId,
                .TargetStableEntityId = meshId,
            })
            .Status,
        Runtime::SandboxEditorCommandStatus::UnsupportedGeometryDomain);
}

TEST(SandboxEditorUi, RegistrationCommandAlignsAcrossEntityTransforms)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);

    // Identical local clouds, but the target is translated in the scene. ICP run
    // on raw local arrays would return identity and leave the source at the
    // origin; running in world space must drive the source onto the target.
    const std::vector<glm::vec3> cloud = MakeRegistrationCloud();
    const ECS::EntityHandle source =
        MakePointCloudEntity(registry, "Src", cloud);
    const ECS::EntityHandle target =
        MakePointCloudEntity(registry, "Tgt", cloud);
    const glm::vec3 targetWorldOffset{1.0f, 0.5f, -0.5f};
    registry.Raw().get<ECSC::Transform::Component>(target).Position =
        targetWorldOffset;

    const std::uint32_t sourceId =
        Runtime::SelectionController::ToStableEntityId(source);
    const std::uint32_t targetId =
        Runtime::SelectionController::ToStableEntityId(target);

    const Runtime::SandboxEditorRegistrationResult result =
        Runtime::ApplySandboxEditorRegistrationCommand(
            context,
            Runtime::SandboxEditorRegistrationCommand{
                .SourceStableEntityId = sourceId,
                .TargetStableEntityId = targetId,
                .Variant = Runtime::SandboxEditorICPVariant::PointToPoint,
                .MaxIterations = 60u,
                .InlierRatio = 1.0,
                .TrajectoryStep = 1000u,
            });

    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_LT(result.FinalRMSE, 1.0e-3);
    const ECSC::Transform::Component& aligned =
        registry.Raw().get<ECSC::Transform::Component>(source);
    EXPECT_NEAR(aligned.Position.x, targetWorldOffset.x, 1.0e-2f);
    EXPECT_NEAR(aligned.Position.y, targetWorldOffset.y, 1.0e-2f);
    EXPECT_NEAR(aligned.Position.z, targetWorldOffset.z, 1.0e-2f);
    EXPECT_NEAR(std::abs(aligned.Rotation.w), 1.0f, 1.0e-2f);
}
