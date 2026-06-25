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

#include "TestImGuiFrameScope.hpp"

import Extrinsic.Asset.ImportRouter;
import Extrinsic.Asset.ModelTexturePayload;
import Extrinsic.Asset.Registry;
import Extrinsic.Asset.Service;
import Extrinsic.Core.Config.Engine;
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
import Extrinsic.ECS.Components.Selection;
import Extrinsic.ECS.Hierarchy.Mutation;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
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
import Extrinsic.Runtime.AssetIngestStateMachine;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.DerivedJobGraph;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.ImGuiAdapter;
import Extrinsic.Runtime.MeshAttributeTextureBake;
import Extrinsic.Runtime.MeshPrimitiveViewPacker;
import Extrinsic.Runtime.ProgressiveRenderData;
import Extrinsic.Runtime.PrimitiveSelectionRefinement;
import Extrinsic.Runtime.RenderArtifactPublication;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.SandboxEditorUi;
import Extrinsic.Runtime.SceneSerialization;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.SelectedMeshTextureBake;
import Extrinsic.Runtime.VertexAttributeBinding;
import Extrinsic.Runtime.VertexChannelBindings;
import Geometry.HalfedgeMesh.Vertices.Normals;
import Geometry.Properties;
import Geometry.UvAtlas;

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

namespace
{
    constexpr std::uint32_t kInvalidIndex =
        std::numeric_limits<std::uint32_t>::max();

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
        const std::optional<Runtime::PrimitiveSelectionResult>* lastPrimitive = nullptr)
    {
        return Runtime::SandboxEditorContext{
            .Scene = &registry,
            .Selection = &selection,
            .LastRefinedPrimitive = lastPrimitive,
            .ImGuiAdapterAvailable = imguiAvailable,
            .AssetImportCommandsAvailable = false,
            .CameraRenderCommandsAvailable = false,
            .VisualizationCommandsAvailable = false,
        };
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
            if (engine.GetLastAssetImportEvent().has_value() ||
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
            MeshHasVertexProperty(engine, entity, "v:normal");
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

    constexpr std::array<std::string_view, 24> kClosedByDefaultWindows{{
        "Sandbox Editor",
        "Scene Hierarchy",
        "Inspector",
        "Selection Details",
        "File / Scene",
        "File / Import",
        "Frame Graph",
        "Camera / Render",
        "Geometry Visualization",
        "PointCloud / Render",
        "PointCloud / Properties",
        "PointCloud / Visualization",
        "PointCloud / Selection",
        "PointCloud / Processing",
        "Graph / Render",
        "Graph / Properties",
        "Graph / Visualization",
        "Graph / Selection",
        "Graph / Processing",
        "Mesh / Render",
        "Mesh / Properties",
        "Mesh / Visualization",
        "Mesh / Selection",
        "Mesh / Processing",
    }};

    for (const std::string_view title : kClosedByDefaultWindows)
    {
        EXPECT_FALSE(ImGuiWindowExists(title)) << std::string(title);
    }
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
        Domain::PointCloudPoints));
    EXPECT_FALSE(Runtime::HasAnySandboxEditorGeometryProcessingDomain(
        normals,
        Domain::MeshEdges));

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
    EXPECT_EQ(mesh[1].Domain, Domain::MeshEdges);
    EXPECT_STREQ(mesh[1].Label, "Edges");
    EXPECT_FALSE(mesh[1].HasNormalsMethod);
    EXPECT_EQ(mesh[2].Domain, Domain::MeshFaces);
    EXPECT_STREQ(mesh[2].Label, "Faces");
    EXPECT_FALSE(mesh[2].HasNormalsMethod);

    const std::vector<Runtime::SandboxEditorGeometryProcessingMenuItem> graph =
        Runtime::GetSandboxEditorGeometryProcessingMenuItems(
            Runtime::SandboxEditorDomainWindowKind::Graph);
    ASSERT_EQ(graph.size(), 3u);
    EXPECT_EQ(graph[0].Domain, Domain::GraphVertices);
    EXPECT_STREQ(graph[0].Label, "Vertices");
    EXPECT_FALSE(graph[0].HasNormalsMethod);
    EXPECT_EQ(graph[1].Domain, Domain::GraphEdges);
    EXPECT_STREQ(graph[1].Label, "Edges");
    EXPECT_EQ(graph[2].Domain, Domain::GraphHalfedges);
    EXPECT_STREQ(graph[2].Label, "Halfedges");

    const std::vector<Runtime::SandboxEditorGeometryProcessingMenuItem> cloud =
        Runtime::GetSandboxEditorGeometryProcessingMenuItems(
            Runtime::SandboxEditorDomainWindowKind::PointCloud);
    ASSERT_EQ(cloud.size(), 1u);
    EXPECT_EQ(cloud[0].Domain, Domain::PointCloudPoints);
    EXPECT_STREQ(cloud[0].Label, "Vertices");
    EXPECT_FALSE(cloud[0].HasNormalsMethod);
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
    ASSERT_EQ(meshEntries.size(), 12u);
    EXPECT_EQ(meshEntries[0].Algorithm, Algorithm::KMeans);
    EXPECT_EQ(meshEntries[1].Algorithm, Algorithm::NormalEstimation);
    EXPECT_EQ(meshEntries[2].Algorithm, Algorithm::ShortestPath);
    EXPECT_EQ(meshEntries[3].Algorithm, Algorithm::VectorHeat);
    EXPECT_EQ(meshEntries[4].Algorithm, Algorithm::Parameterization);
    EXPECT_EQ(meshEntries[5].Algorithm, Algorithm::ConvexHull);
    EXPECT_EQ(meshEntries[6].Algorithm, Algorithm::BooleanCSG);
    EXPECT_EQ(meshEntries[7].Algorithm, Algorithm::Remeshing);
    EXPECT_EQ(meshEntries[11].Algorithm, Algorithm::Repair);

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
    EXPECT_TRUE(meshModel.Processing.MeshVertexNormalsAvailable);

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
    ASSERT_EQ(graphEntries.size(), 2u);
    EXPECT_EQ(graphEntries[0].Algorithm, Algorithm::KMeans);
    EXPECT_EQ(graphEntries[1].Algorithm, Algorithm::ShortestPath);
    const std::vector<Domain> graphKMeans =
        Runtime::GetAvailableSandboxEditorKMeansDomains(registry, graph);
    ASSERT_EQ(graphKMeans.size(), 1u);
    EXPECT_EQ(graphKMeans[0], Domain::GraphVertices);

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
    ASSERT_EQ(cloudEntries.size(), 8u);
    EXPECT_EQ(cloudEntries[0].Algorithm, Algorithm::KMeans);
    EXPECT_EQ(cloudEntries[1].Algorithm, Algorithm::NormalEstimation);
    EXPECT_EQ(cloudEntries[2].Algorithm, Algorithm::Registration);
    EXPECT_EQ(cloudEntries[7].Algorithm, Algorithm::SurfaceReconstruction);
    const std::vector<Domain> cloudKMeans =
        Runtime::GetAvailableSandboxEditorKMeansDomains(registry, cloud);
    ASSERT_EQ(cloudKMeans.size(), 1u);
    EXPECT_EQ(cloudKMeans[0], Domain::PointCloudPoints);

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

    std::optional<std::uint32_t> stableId{};
    Runtime::Engine engine(
        HeadlessConfig(),
        std::make_unique<WaitForConditionApplication>(
            [&stableId](Runtime::Engine& runningEngine)
            {
                if (!stableId.has_value())
                    return false;
                const std::optional<Graphics::MaterialTextureAssetBindings> bindings =
                    runningEngine.GetMaterialTextureAssetBindingsForTest(*stableId);
                return bindings.has_value() && bindings->Normal.IsValid();
            },
            128u));
    engine.Initialize();

    auto imported = engine.ImportAssetFromPath(Runtime::RuntimeAssetImportRequest{
        .Path = meshFile.Path.string(),
        .PayloadKind = Assets::AssetPayloadKind::Mesh,
    });
    ASSERT_TRUE(imported.has_value()) << static_cast<int>(imported.error());

    const std::optional<ECS::EntityHandle> meshEntity =
        FindFirstEntityWithDomain(engine.GetScene(), GS::Domain::Mesh);
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
    ASSERT_TRUE(bindings.has_value());
    ASSERT_TRUE(bindings->Normal.IsValid());
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

TEST(SandboxEditorUi, TextureBakeControlsReportUvSourcesAndRouteCommand)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    Runtime::EditorCommandHistory history;
    Assets::AssetService assets;

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
    Runtime::SandboxEditorContext context = MakeContext(registry, selection);
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

    auto imported = engine.ImportAssetFromPath(
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

    auto mesh = engine.ImportAssetFromPath(
        Runtime::RuntimeAssetImportRequest{
            .Path = meshFile.Path.string(),
            .PayloadKind = Assets::AssetPayloadKind::Mesh,
        });
    ASSERT_TRUE(mesh.has_value()) << static_cast<int>(mesh.error());
    EXPECT_TRUE(mesh->Asset.IsValid());
    EXPECT_EQ(mesh->PayloadKind, Assets::AssetPayloadKind::Mesh);
    EXPECT_EQ(mesh->PrimitiveEntitiesCreated, 1u);

    auto graph = engine.ImportAssetFromPath(
        Runtime::RuntimeAssetImportRequest{
            .Path = graphFile.Path.string(),
            .PayloadKind = Assets::AssetPayloadKind::Graph,
        });
    ASSERT_TRUE(graph.has_value()) << static_cast<int>(graph.error());
    EXPECT_TRUE(graph->Asset.IsValid());
    EXPECT_EQ(graph->PayloadKind, Assets::AssetPayloadKind::Graph);
    EXPECT_EQ(graph->PrimitiveEntitiesCreated, 1u);

    auto cloud = engine.ImportAssetFromPath(
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
        engine.GetLastAssetImportEvent();
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

    auto mesh = engine.ImportAssetFromPath(
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

    auto mesh = engine.ImportAssetFromPath(
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

    Runtime::SandboxEditorUi ui;
    ui.Attach(engine);

    const std::vector<std::string> droppedPaths{cloudFile.Path.string()};
    engine.ImportDroppedFilePaths(droppedPaths);
    EXPECT_EQ(CountEntitiesWithDomain(engine.GetScene(), GS::Domain::PointCloud), 0u);
    EXPECT_FALSE(engine.GetLastAssetImportEvent().has_value());

    ASSERT_FALSE(engine.GetWindow().ShouldClose())
        << "explicit Null window backend must keep Engine::Run() drivable on headless hosts";

    engine.Run();

    EXPECT_EQ(CountEntitiesWithDomain(engine.GetScene(), GS::Domain::PointCloud), 1u);
    const std::optional<Runtime::RuntimeAssetImportEvent>& lastEvent =
        engine.GetLastAssetImportEvent();
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

    const std::vector<std::string> droppedPaths{
        meshFile.Path.string(),
        meshFile.Path.string(),
    };
    engine.ImportDroppedFilePaths(droppedPaths);

    std::vector<Runtime::RuntimeAssetIngestRecord> records =
        engine.GetAssetIngestRecordsForTest();
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].Request.Source,
              Runtime::RuntimeAssetIngestSource::DroppedFile);
    EXPECT_EQ(records[0].Request.Path, meshFile.Path.string());
    EXPECT_EQ(records[0].Phase, Runtime::RuntimeAssetIngestPhase::Decoding);

    const std::optional<Runtime::RuntimeAssetImportEvent>& duplicateEvent =
        engine.GetLastAssetImportEvent();
    ASSERT_TRUE(duplicateEvent.has_value());
    EXPECT_FALSE(duplicateEvent->Succeeded());
    EXPECT_EQ(duplicateEvent->Error, Core::ErrorCode::ResourceBusy);
    EXPECT_EQ(duplicateEvent->IngestDiagnostic,
              Runtime::RuntimeAssetIngestDiagnostic::DuplicateActiveRequest);

    ASSERT_FALSE(engine.GetWindow().ShouldClose())
        << "explicit Null window backend must keep Engine::Run() drivable on headless hosts";

    engine.Run();

    EXPECT_EQ(CountEntitiesWithDomain(engine.GetScene(), GS::Domain::Mesh), 1u);
    records = engine.GetAssetIngestRecordsForTest();
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].Phase, Runtime::RuntimeAssetIngestPhase::Complete);
    EXPECT_EQ(records[0].Diagnostic, Runtime::RuntimeAssetIngestDiagnostic::None);
    ASSERT_TRUE(records[0].Result.has_value());
    EXPECT_EQ(records[0].Result->PrimitiveEntitiesCreated, 1u);

    const std::optional<Runtime::RuntimeAssetImportEvent>& lastEvent =
        engine.GetLastAssetImportEvent();
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

    const std::vector<std::string> droppedPaths{
        meshFile.Path.string(),
        missingFile.string(),
    };
    engine.ImportDroppedFilePaths(droppedPaths);

    Runtime::RuntimeAssetImportQueueSnapshot queue =
        engine.GetAssetImportQueueSnapshot();
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

    queue = engine.GetAssetImportQueueSnapshot();
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

    EXPECT_EQ(engine.ClearCompletedAssetImports(), 2u);
    queue = engine.GetAssetImportQueueSnapshot();
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
    engine.ImportDroppedFilePaths(droppedPaths);

    Runtime::RuntimeAssetImportQueueSnapshot queue =
        engine.GetAssetImportQueueSnapshot();
    ASSERT_EQ(queue.Entries.size(), 1u);
    EXPECT_TRUE(queue.Entries[0].CanCancel);
    EXPECT_TRUE(engine.CancelAssetImport(queue.Entries[0].Operation).has_value());

    queue = engine.GetAssetImportQueueSnapshot();
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

    const std::vector<std::string> droppedPaths{meshFile.Path.string()};
    engine.ImportDroppedFilePaths(droppedPaths);

    ASSERT_FALSE(engine.GetWindow().ShouldClose())
        << "explicit Null window backend must keep Engine::Run() drivable on headless hosts";

    engine.Run();

    const std::optional<Runtime::RuntimeAssetImportEvent>& droppedEvent =
        engine.GetLastAssetImportEvent();
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

    auto reimported = engine.ReimportAsset(Runtime::RuntimeAssetReimportRequest{
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
        engine.GetAssetIngestRecordsForTest();
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
        engine.GetLastAssetImportEvent();
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
        engine.GetLastAssetImportEvent();
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

    engine.DispatchPlatformEventForTest(Plat::WindowDropEvent{
        .Paths = {meshFile.Path.string()},
    });

    ASSERT_FALSE(engine.GetWindow().ShouldClose())
        << "explicit Null window backend must keep Engine::Run() drivable on headless hosts";

    engine.Run();

    EXPECT_EQ(CountEntitiesWithDomain(engine.GetScene(), GS::Domain::Mesh), 1u);
    const std::optional<Runtime::RuntimeAssetImportEvent>& lastEvent =
        engine.GetLastAssetImportEvent();
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
    EXPECT_FALSE(engine.GetLastAssetImportEvent().has_value());

    ASSERT_FALSE(engine.GetWindow().ShouldClose())
        << "explicit Null window backend must keep Engine::Run() drivable on headless hosts";

    engine.Run();

    const std::optional<Runtime::RuntimeAssetImportEvent>& lastEvent =
        engine.GetLastAssetImportEvent();
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

    engine.DispatchPlatformEventForTest(Plat::WindowDropEvent{
        .Paths = {meshFile.Path.string()},
    });

    ASSERT_FALSE(engine.GetWindow().ShouldClose())
        << "explicit Null window backend must keep Engine::Run() drivable on headless hosts";

    engine.Run();

    EXPECT_EQ(CountEntitiesWithDomain(engine.GetScene(), GS::Domain::Mesh), 1u);
    const std::optional<Runtime::RuntimeAssetImportEvent>& lastEvent =
        engine.GetLastAssetImportEvent();
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

    engine.DispatchPlatformEventForTest(Plat::WindowCloseEvent{});
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
