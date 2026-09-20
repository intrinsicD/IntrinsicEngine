#include "EditorFeatureTestContext.hpp"

#include <utility>

import Extrinsic.ECS.Component.MetaData;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.Components.Selection;
import Extrinsic.Graphics.Component.RenderGeometry;

namespace Intrinsic::Tests
{
    std::function<void()> EditorFeatureTestContext::MakeWorkspaceSnapshotCacheInvalidator() const
    {
        Runtime::EditorSelectedModelCache* const cache = SelectedModelCache;
        if (cache == nullptr)
            return {};
        return [cache] { cache->Clear(); };
    }

    EditorFeatureTestContext::operator Runtime::EditorSceneEditingContext() const
    {
        return Runtime::EditorSceneEditingContext{
            .Scene = Scene,
            .World = World,
            .Selection = Selection,
            .CommandHistory = CommandHistory,
            .LastRefinedPrimitive = LastRefinedPrimitive,
            .LastRefinedPrimitiveGeneration = LastRefinedPrimitiveGeneration,
            .CameraControllers = CameraControllers,
            .CameraViewport = CameraViewport,
            .AssetImportCommands = AssetImportCommands,
            .AssetImportQueueCommands = AssetImportQueueCommands,
            .SceneFileCommands = SceneFileCommands,
            .PrimitiveViewCommands = PrimitiveViewCommands,
            .AssetImportQueue = AssetImportQueue,
            .PendingAssetImportPath = PendingAssetImportPath,
            .PendingSceneFilePath = PendingSceneFilePath,
            .PendingAssetImportPayloadKind = PendingAssetImportPayloadKind,
            .LastAssetImportResult = LastAssetImportResult,
            .LastSceneFileResult = LastSceneFileResult,
            .AttachmentActive = AttachmentActive,
            .InvalidateWorkspaceSnapshotCache = MakeWorkspaceSnapshotCacheInvalidator(),
            .ImGuiAdapterAvailable = ImGuiAdapterAvailable,
            .AssetImportCommandsAvailable = AssetImportCommandsAvailable,
            .SceneFileCommandsAvailable = SceneFileCommandsAvailable,
            .CameraRenderCommandsAvailable = CameraRenderCommandsAvailable,
        };
    }

    EditorFeatureTestContext::operator Runtime::EditorProcessingContext() const
    {
        return Runtime::EditorProcessingContext{
            .Scene = Scene,
            .World = World,
            .CommandHistory = CommandHistory,
            .Device = Device,
            .JobCommands = JobCommands,
            .EngineConfigControlState = EngineConfigControlState,
            .PreviewEngineConfigDocument = PreviewEngineConfigDocument,
            .ApplyEngineConfigHotSubset = ApplyEngineConfigHotSubset,
            .AttachmentActive = AttachmentActive,
            .InvalidateWorkspaceSnapshotCache = MakeWorkspaceSnapshotCacheInvalidator(),
            .EngineConfigCommandsAvailable = EngineConfigCommandsAvailable,
            .Selection = Selection,
            .MeshDenoiseKernelAvailable = MeshDenoiseKernelAvailable,
            .MeshCurvatureKernelAvailable = MeshCurvatureKernelAvailable,
            .MeshCurvatureDirectionsAvailable = MeshCurvatureDirectionsAvailable,
            .CurvatureSegmentationKernelAvailable = CurvatureSegmentationKernelAvailable,
            .MeshRemeshUniformKernelAvailable = MeshRemeshUniformKernelAvailable,
            .MeshRemeshAdaptiveKernelAvailable = MeshRemeshAdaptiveKernelAvailable,
            .MeshRemeshProjectToSurfaceAvailable = MeshRemeshProjectToSurfaceAvailable,
            .MeshRemeshErrorBoundedSizingAvailable = MeshRemeshErrorBoundedSizingAvailable,
            .MeshSubdivideLoopKernelAvailable = MeshSubdivideLoopKernelAvailable,
            .MeshSubdivideCatmullClarkKernelAvailable = MeshSubdivideCatmullClarkKernelAvailable,
            .MeshSubdivideSqrt3KernelAvailable = MeshSubdivideSqrt3KernelAvailable,
            .MeshSubdivideLoopFeatureEdgesAvailable = MeshSubdivideLoopFeatureEdgesAvailable,
            .MeshSimplifyKernelAvailable = MeshSimplifyKernelAvailable,
        };
    }

    EditorFeatureTestContext::operator Runtime::EditorProcessingCommands() const
    {
        return Runtime::BindEditorProcessingCommands(static_cast<Runtime::EditorProcessingContext>(*this));
    }

    EditorFeatureTestContext::operator Runtime::EditorVisualizationEditingContext() const
    {
        return Runtime::EditorVisualizationEditingContext{
            .Scene = Scene,
            .World = World,
            .Selection = Selection,
            .CommandHistory = CommandHistory,
            .TextureBake = TextureBake,
            .VisualizationRecipes = VisualizationRecipes,
            .VisualizationRecipeRevision = VisualizationRecipeRevision,
            .JobCommands = JobCommands,
            .ModelBuildStats = ModelBuildStats,
            .AttachmentActive = AttachmentActive,
            .InvalidateWorkspaceSnapshotCache = MakeWorkspaceSnapshotCacheInvalidator(),
            .VisualizationCommandsAvailable = VisualizationCommandsAvailable,
        };
    }

    EditorFeatureTestContext::operator Runtime::EditorRenderRecipeEditingContext() const
    {
        return Runtime::EditorRenderRecipeEditingContext{
            .RenderGraphStats = RenderGraphStats,
            .RenderRecipeContext = RenderRecipeContext,
            .RenderRecipeEditorState = RenderRecipeEditorState,
            .RenderRecipeRuntimeState = RenderRecipeRuntimeState,
            .PreviewRenderRecipeDocument = PreviewRenderRecipeDocument,
            .ApplyRenderRecipePreview = ApplyRenderRecipePreview,
            .EngineConfigControlState = EngineConfigControlState,
            .PreviewEngineConfigDocument = PreviewEngineConfigDocument,
            .ApplyEngineConfigHotSubset = ApplyEngineConfigHotSubset,
            .RenderArtifacts = RenderArtifacts,
            .AttachmentActive = AttachmentActive,
            .RenderRecipeCommandsAvailable = RenderRecipeCommandsAvailable,
            .EngineConfigCommandsAvailable = EngineConfigCommandsAvailable,
        };
    }

    EditorFeatureTestContext::operator Runtime::EditorWorkspaceSnapshotContext() const
    {
        return Runtime::EditorWorkspaceSnapshotContext{
            .Scene = static_cast<Runtime::EditorSceneEditingContext>(*this),
            .Geometry = static_cast<Runtime::EditorProcessingContext>(*this),
            .Visualization = static_cast<Runtime::EditorVisualizationEditingContext>(*this),
            .RenderRecipe = static_cast<Runtime::EditorRenderRecipeEditingContext>(*this),
            .SelectedModelCache = SelectedModelCache,
        };
    }
}

namespace Intrinsic::Tests::EditorGeometry
{
    ECS::EntityHandle MakeSelectable(ECS::Scene::Registry& registry,
                                    std::string name)
    {
        const ECS::EntityHandle entity = registry.Create();
        auto& raw = registry.Raw();
        raw.emplace<ECS::Components::MetaData>(entity, std::move(name));
        raw.emplace<ECS::Components::Transform::Component>(entity);
        raw.emplace<ECS::Components::Transform::WorldMatrix>(entity);
        raw.emplace<ECS::Components::Selection::SelectableTag>(entity);
        return entity;
    }

    void AddPointCloudSource(ECS::Scene::Registry& registry,
                             const ECS::EntityHandle entity,
                             const std::size_t pointCount)
    {
        auto& vertices = registry.Raw().emplace<GS::Vertices>(entity);
        vertices.Properties.Resize(pointCount);
        registry.Raw().emplace<Graphics::Components::RenderPoints>(entity);
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
}
