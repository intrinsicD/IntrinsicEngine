module;

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

module Extrinsic.Runtime.Private.EditorFeatures;

import Extrinsic.Asset.ImportRouter;
import Extrinsic.Asset.GeometryPayload;
import Extrinsic.Asset.Registry;
import Extrinsic.Core.Error;
import Extrinsic.Graphics.Component.GpuSceneSlot;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Graphics.UvView;
import Extrinsic.Platform.Window;
import Extrinsic.RHI.Bindless;
import Extrinsic.RHI.Device;
import Extrinsic.Runtime.AssetWorkflowModule;
import Extrinsic.Runtime.AssetWorkflowRecipePolicies;
import Extrinsic.Runtime.EditorUiHost;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.SceneDocumentModule;
import Extrinsic.Runtime.SceneInteractionModule;
import Extrinsic.Runtime.TextureBakeModule;

namespace Extrinsic::Runtime::EditorFeatureDetail {
EditorSceneEditingContext
MakeEditorSceneEditingContext(const EditorFeatureBindings &bindings) {
  return EditorSceneEditingContext{
      .Scene = bindings.Scene,
      .World = bindings.World,
      .Selection = bindings.Selection,
      .CommandHistory = bindings.CommandHistory,
      .AssetService = bindings.AssetService,
      .LastRefinedPrimitive = bindings.LastRefinedPrimitive,
      .LastRefinedPrimitiveGeneration = bindings.LastRefinedPrimitiveGeneration,
      .CameraControllers = bindings.CameraControllers,
      .CameraViewport = bindings.CameraViewport,
      .AssetImportCommands = bindings.AssetImportCommands,
      .AssetImportQueueCommands = bindings.AssetImportQueueCommands,
      .SceneFileCommands = bindings.SceneFileCommands,
      .PrimitiveViewCommands = bindings.PrimitiveViewCommands,
      .AssetImportQueue = bindings.AssetImportQueue,
      .PendingAssetImportPath = bindings.PendingAssetImportPath,
      .PendingSceneFilePath = bindings.PendingSceneFilePath,
      .PendingAssetImportPayloadKind = bindings.PendingAssetImportPayloadKind,
      .LastAssetImportResult = bindings.LastAssetImportResult,
      .LastSceneFileResult = bindings.LastSceneFileResult,
      .AttachmentActive = bindings.AttachmentActive,
      .InvalidateWorkspaceSnapshotCache =
          bindings.InvalidateWorkspaceSnapshotCache,
      .ImGuiAdapterAvailable = bindings.ImGuiAdapterAvailable,
      .AssetImportCommandsAvailable = bindings.AssetImportCommandsAvailable,
      .SceneFileCommandsAvailable = bindings.SceneFileCommandsAvailable,
      .CameraRenderCommandsAvailable = bindings.CameraRenderCommandsAvailable,
  };
}

EditorGeometryProcessingContext
MakeEditorGeometryProcessingContext(const EditorFeatureBindings &bindings) {
  return EditorGeometryProcessingContext{
      .Scene = bindings.Scene,
      .World = bindings.World,
      .Selection = bindings.Selection,
      .CommandHistory = bindings.CommandHistory,
      .Device = bindings.Device,
      .Clustering = bindings.Clustering,
      .PointCloudConsolidation = bindings.PointCloudConsolidation,
      .ParameterizationUvViewCommands = bindings.ParameterizationUvViewCommands,
      .JobCommands = bindings.JobCommands,
      .MethodResultSinks = bindings.MethodResultSinks,
      .LastKMeansResult = bindings.LastKMeansResult,
      .LastPointCloudConsolidationResult =
          bindings.LastPointCloudConsolidationResult,
      .LastMeshDenoiseResult = bindings.LastMeshDenoiseResult,
      .LastMeshCurvatureResult = bindings.LastMeshCurvatureResult,
      .LastMeshRemeshResult = bindings.LastMeshRemeshResult,
      .LastMeshSubdivideResult = bindings.LastMeshSubdivideResult,
      .LastMeshSimplifyResult = bindings.LastMeshSimplifyResult,
      .LastMeshVertexNormalsResult = bindings.LastMeshVertexNormalsResult,
      .LastGraphVertexNormalsResult = bindings.LastGraphVertexNormalsResult,
      .LastPointCloudVertexNormalsResult =
          bindings.LastPointCloudVertexNormalsResult,
      .LastPointCloudOutlierRemovalResult =
          bindings.LastPointCloudOutlierRemovalResult,
      .LastUvRegenerationResult = bindings.LastUvRegenerationResult,
      .LastParameterizationResult = bindings.LastParameterizationResult,
      .LastProgressivePoissonResult = bindings.LastProgressivePoissonResult,
      .LastRegistrationResult = bindings.LastRegistrationResult,
      .EngineConfigControlState = bindings.EngineConfigControlState,
      .PreviewEngineConfigDocument = bindings.PreviewEngineConfigDocument,
      .ApplyEngineConfigHotSubset = bindings.ApplyEngineConfigHotSubset,
      .AttachmentActive = bindings.AttachmentActive,
      .InvalidateWorkspaceSnapshotCache =
          bindings.InvalidateWorkspaceSnapshotCache,
      .EngineConfigCommandsAvailable = bindings.EngineConfigCommandsAvailable,
      .MeshDenoiseKernelAvailable = bindings.MeshDenoiseKernelAvailable,
      .MeshCurvatureKernelAvailable = bindings.MeshCurvatureKernelAvailable,
      .MeshCurvatureDirectionsAvailable =
          bindings.MeshCurvatureDirectionsAvailable,
      .MeshRemeshUniformKernelAvailable =
          bindings.MeshRemeshUniformKernelAvailable,
      .MeshRemeshAdaptiveKernelAvailable =
          bindings.MeshRemeshAdaptiveKernelAvailable,
      .MeshRemeshProjectToSurfaceAvailable =
          bindings.MeshRemeshProjectToSurfaceAvailable,
      .MeshRemeshErrorBoundedSizingAvailable =
          bindings.MeshRemeshErrorBoundedSizingAvailable,
      .MeshSubdivideLoopKernelAvailable =
          bindings.MeshSubdivideLoopKernelAvailable,
      .MeshSubdivideCatmullClarkKernelAvailable =
          bindings.MeshSubdivideCatmullClarkKernelAvailable,
      .MeshSubdivideSqrt3KernelAvailable =
          bindings.MeshSubdivideSqrt3KernelAvailable,
      .MeshSubdivideLoopFeatureEdgesAvailable =
          bindings.MeshSubdivideLoopFeatureEdgesAvailable,
      .MeshSimplifyKernelAvailable = bindings.MeshSimplifyKernelAvailable,
  };
}

EditorVisualizationEditingContext
MakeEditorVisualizationEditingContext(const EditorFeatureBindings &bindings) {
  return EditorVisualizationEditingContext{
      .Scene = bindings.Scene,
      .World = bindings.World,
      .Selection = bindings.Selection,
      .CommandHistory = bindings.CommandHistory,
      .TextureBake = bindings.TextureBake,
      .VisualizationRecipes = bindings.VisualizationRecipes,
      .VisualizationRecipeRevision = bindings.VisualizationRecipeRevision,
      .JobCommands = bindings.JobCommands,
      .ModelBuildStats = bindings.ModelBuildStats,
      .AttachmentActive = bindings.AttachmentActive,
      .InvalidateWorkspaceSnapshotCache =
          bindings.InvalidateWorkspaceSnapshotCache,
      .OperationalGpuAvailable =
          bindings.Device != nullptr && bindings.Device->IsOperational(),
      .VisualizationCommandsAvailable = bindings.VisualizationCommandsAvailable,
  };
}

EditorRenderRecipeEditingContext
MakeEditorRenderRecipeEditingContext(const EditorFeatureBindings &bindings) {
  return EditorRenderRecipeEditingContext{
      .RenderGraphStats = bindings.RenderGraphStats,
      .RenderRecipeContext = bindings.RenderRecipeContext,
      .RenderRecipeEditorState = bindings.RenderRecipeEditorState,
      .RenderRecipeRuntimeState = bindings.RenderRecipeRuntimeState,
      .PreviewRenderRecipeDocument = bindings.PreviewRenderRecipeDocument,
      .ApplyRenderRecipePreview = bindings.ApplyRenderRecipePreview,
      .EngineConfigControlState = bindings.EngineConfigControlState,
      .PreviewEngineConfigDocument = bindings.PreviewEngineConfigDocument,
      .ApplyEngineConfigHotSubset = bindings.ApplyEngineConfigHotSubset,
      .RenderArtifacts = bindings.RenderArtifacts,
      .AttachmentActive = bindings.AttachmentActive,
      .RenderRecipeCommandsAvailable = bindings.RenderRecipeCommandsAvailable,
      .EngineConfigCommandsAvailable = bindings.EngineConfigCommandsAvailable,
  };
}

EditorFeatureBindings
ToEditorFeatureBindingsImpl(const EditorSceneEditingContext &context) {
  if (context.AttachmentActive && !context.AttachmentActive()) {
    return EditorFeatureBindings{
        .World = context.World,
        .AssetImportCommands = context.AssetImportCommands,
        .AssetImportQueueCommands = context.AssetImportQueueCommands,
        .SceneFileCommands = context.SceneFileCommands,
        .PrimitiveViewCommands = context.PrimitiveViewCommands,
        .AttachmentActive = context.AttachmentActive,
    };
  }

  return EditorFeatureBindings{
      .Scene = context.Scene,
      .World = context.World,
      .Selection = context.Selection,
      .CommandHistory = context.CommandHistory,
      .AssetService = context.AssetService,
      .LastRefinedPrimitive = context.LastRefinedPrimitive,
      .LastRefinedPrimitiveGeneration = context.LastRefinedPrimitiveGeneration,
      .CameraControllers = context.CameraControllers,
      .CameraViewport = context.CameraViewport,
      .AssetImportCommands = context.AssetImportCommands,
      .AssetImportQueueCommands = context.AssetImportQueueCommands,
      .SceneFileCommands = context.SceneFileCommands,
      .PrimitiveViewCommands = context.PrimitiveViewCommands,
      .AssetImportQueue = context.AssetImportQueue,
      .PendingAssetImportPath = context.PendingAssetImportPath,
      .PendingSceneFilePath = context.PendingSceneFilePath,
      .PendingAssetImportPayloadKind = context.PendingAssetImportPayloadKind,
      .LastAssetImportResult = context.LastAssetImportResult,
      .LastSceneFileResult = context.LastSceneFileResult,
      .AttachmentActive = context.AttachmentActive,
      .InvalidateWorkspaceSnapshotCache =
          context.InvalidateWorkspaceSnapshotCache,
      .ImGuiAdapterAvailable = context.ImGuiAdapterAvailable,
      .AssetImportCommandsAvailable = context.AssetImportCommandsAvailable,
      .SceneFileCommandsAvailable = context.SceneFileCommandsAvailable,
      .CameraRenderCommandsAvailable = context.CameraRenderCommandsAvailable,
  };
}

EditorFeatureBindings
ToEditorFeatureBindingsImpl(const EditorGeometryProcessingContext &context) {
  if (context.AttachmentActive && !context.AttachmentActive()) {
    return EditorFeatureBindings{
        .World = context.World,
        .ParameterizationUvViewCommands =
            context.ParameterizationUvViewCommands,
        .JobCommands = context.JobCommands,
        .MethodResultSinks = context.MethodResultSinks,
        .AttachmentActive = context.AttachmentActive,
        .PreviewEngineConfigDocument = context.PreviewEngineConfigDocument,
        .ApplyEngineConfigHotSubset = context.ApplyEngineConfigHotSubset,
    };
  }

  return EditorFeatureBindings{
      .Scene = context.Scene,
      .World = context.World,
      .Selection = context.Selection,
      .CommandHistory = context.CommandHistory,
      .Device = context.Device,
      .Clustering = context.Clustering,
      .PointCloudConsolidation = context.PointCloudConsolidation,
      .ParameterizationUvViewCommands = context.ParameterizationUvViewCommands,
      .JobCommands = context.JobCommands,
      .MethodResultSinks = context.MethodResultSinks,
      .LastKMeansResult = context.LastKMeansResult,
      .LastPointCloudConsolidationResult =
          context.LastPointCloudConsolidationResult,
      .LastMeshDenoiseResult = context.LastMeshDenoiseResult,
      .LastMeshCurvatureResult = context.LastMeshCurvatureResult,
      .LastMeshRemeshResult = context.LastMeshRemeshResult,
      .LastMeshSubdivideResult = context.LastMeshSubdivideResult,
      .LastMeshSimplifyResult = context.LastMeshSimplifyResult,
      .LastMeshVertexNormalsResult = context.LastMeshVertexNormalsResult,
      .LastGraphVertexNormalsResult = context.LastGraphVertexNormalsResult,
      .LastPointCloudVertexNormalsResult =
          context.LastPointCloudVertexNormalsResult,
      .LastPointCloudOutlierRemovalResult =
          context.LastPointCloudOutlierRemovalResult,
      .LastUvRegenerationResult = context.LastUvRegenerationResult,
      .LastParameterizationResult = context.LastParameterizationResult,
      .LastProgressivePoissonResult = context.LastProgressivePoissonResult,
      .LastRegistrationResult = context.LastRegistrationResult,
      .EngineConfigControlState = context.EngineConfigControlState,
      .AttachmentActive = context.AttachmentActive,
      .InvalidateWorkspaceSnapshotCache =
          context.InvalidateWorkspaceSnapshotCache,
      .PreviewEngineConfigDocument = context.PreviewEngineConfigDocument,
      .ApplyEngineConfigHotSubset = context.ApplyEngineConfigHotSubset,
      .EngineConfigCommandsAvailable = context.EngineConfigCommandsAvailable,
      .MeshDenoiseKernelAvailable = context.MeshDenoiseKernelAvailable,
      .MeshCurvatureKernelAvailable = context.MeshCurvatureKernelAvailable,
      .MeshCurvatureDirectionsAvailable =
          context.MeshCurvatureDirectionsAvailable,
      .MeshRemeshUniformKernelAvailable =
          context.MeshRemeshUniformKernelAvailable,
      .MeshRemeshAdaptiveKernelAvailable =
          context.MeshRemeshAdaptiveKernelAvailable,
      .MeshRemeshProjectToSurfaceAvailable =
          context.MeshRemeshProjectToSurfaceAvailable,
      .MeshRemeshErrorBoundedSizingAvailable =
          context.MeshRemeshErrorBoundedSizingAvailable,
      .MeshSubdivideLoopKernelAvailable =
          context.MeshSubdivideLoopKernelAvailable,
      .MeshSubdivideCatmullClarkKernelAvailable =
          context.MeshSubdivideCatmullClarkKernelAvailable,
      .MeshSubdivideSqrt3KernelAvailable =
          context.MeshSubdivideSqrt3KernelAvailable,
      .MeshSubdivideLoopFeatureEdgesAvailable =
          context.MeshSubdivideLoopFeatureEdgesAvailable,
      .MeshSimplifyKernelAvailable = context.MeshSimplifyKernelAvailable,
  };
}

EditorFeatureBindings
ToEditorFeatureBindingsImpl(const EditorVisualizationEditingContext &context) {
  if (context.AttachmentActive && !context.AttachmentActive()) {
    return EditorFeatureBindings{
        .World = context.World,
        .VisualizationRecipes = context.VisualizationRecipes,
        .JobCommands = context.JobCommands,
        .AttachmentActive = context.AttachmentActive,
    };
  }

  return EditorFeatureBindings{
      .Scene = context.Scene,
      .World = context.World,
      .Selection = context.Selection,
      .CommandHistory = context.CommandHistory,
      .TextureBake = context.TextureBake,
      .VisualizationRecipes = context.VisualizationRecipes,
      .VisualizationRecipeRevision = context.VisualizationRecipeRevision,
      .JobCommands = context.JobCommands,
      .ModelBuildStats = context.ModelBuildStats,
      .AttachmentActive = context.AttachmentActive,
      .InvalidateWorkspaceSnapshotCache =
          context.InvalidateWorkspaceSnapshotCache,
      .VisualizationCommandsAvailable = context.VisualizationCommandsAvailable,
  };
}

EditorFeatureBindings
ToEditorFeatureBindingsImpl(const EditorRenderRecipeEditingContext &context) {
  if (context.AttachmentActive && !context.AttachmentActive()) {
    return EditorFeatureBindings{
        .AttachmentActive = context.AttachmentActive,
        .PreviewRenderRecipeDocument = context.PreviewRenderRecipeDocument,
        .ApplyRenderRecipePreview = context.ApplyRenderRecipePreview,
        .PreviewEngineConfigDocument = context.PreviewEngineConfigDocument,
        .ApplyEngineConfigHotSubset = context.ApplyEngineConfigHotSubset,
    };
  }

  return EditorFeatureBindings{
      .RenderGraphStats = context.RenderGraphStats,
      .RenderRecipeContext = context.RenderRecipeContext,
      .RenderRecipeEditorState = context.RenderRecipeEditorState,
      .RenderRecipeRuntimeState = context.RenderRecipeRuntimeState,
      .EngineConfigControlState = context.EngineConfigControlState,
      .AttachmentActive = context.AttachmentActive,
      .PreviewRenderRecipeDocument = context.PreviewRenderRecipeDocument,
      .ApplyRenderRecipePreview = context.ApplyRenderRecipePreview,
      .PreviewEngineConfigDocument = context.PreviewEngineConfigDocument,
      .ApplyEngineConfigHotSubset = context.ApplyEngineConfigHotSubset,
      .RenderArtifacts = context.RenderArtifacts,
      .RenderRecipeCommandsAvailable = context.RenderRecipeCommandsAvailable,
      .EngineConfigCommandsAvailable = context.EngineConfigCommandsAvailable,
  };
}

EditorFeatureBindings
ToEditorFeatureBindingsImpl(const EditorWorkspaceSnapshotContext &context) {
  const auto attachmentExpired = [](const auto &feature) {
    return feature.AttachmentActive && !feature.AttachmentActive();
  };
  if (attachmentExpired(context.Scene) || attachmentExpired(context.Geometry) ||
      attachmentExpired(context.Visualization) ||
      attachmentExpired(context.RenderRecipe))
    return {};

  EditorFeatureBindings bindings = ToEditorFeatureBindingsImpl(context.Scene);
  const EditorFeatureBindings geometry =
      ToEditorFeatureBindingsImpl(context.Geometry);
  const EditorFeatureBindings visualization =
      ToEditorFeatureBindingsImpl(context.Visualization);
  const EditorFeatureBindings renderRecipe =
      ToEditorFeatureBindingsImpl(context.RenderRecipe);

  bindings.Device = geometry.Device;
  bindings.Clustering = geometry.Clustering;
  bindings.PointCloudConsolidation = geometry.PointCloudConsolidation;
  bindings.ParameterizationUvViewCommands =
      geometry.ParameterizationUvViewCommands;
  bindings.JobCommands = geometry.JobCommands;
  bindings.MethodResultSinks = geometry.MethodResultSinks;
  bindings.LastKMeansResult = geometry.LastKMeansResult;
  bindings.LastPointCloudConsolidationResult =
      geometry.LastPointCloudConsolidationResult;
  bindings.LastMeshDenoiseResult = geometry.LastMeshDenoiseResult;
  bindings.LastMeshCurvatureResult = geometry.LastMeshCurvatureResult;
  bindings.LastMeshRemeshResult = geometry.LastMeshRemeshResult;
  bindings.LastMeshSubdivideResult = geometry.LastMeshSubdivideResult;
  bindings.LastMeshSimplifyResult = geometry.LastMeshSimplifyResult;
  bindings.LastMeshVertexNormalsResult = geometry.LastMeshVertexNormalsResult;
  bindings.LastGraphVertexNormalsResult = geometry.LastGraphVertexNormalsResult;
  bindings.LastPointCloudVertexNormalsResult =
      geometry.LastPointCloudVertexNormalsResult;
  bindings.LastPointCloudOutlierRemovalResult =
      geometry.LastPointCloudOutlierRemovalResult;
  bindings.LastUvRegenerationResult = geometry.LastUvRegenerationResult;
  bindings.LastParameterizationResult = geometry.LastParameterizationResult;
  bindings.LastProgressivePoissonResult = geometry.LastProgressivePoissonResult;
  bindings.LastRegistrationResult = geometry.LastRegistrationResult;
  bindings.MeshDenoiseKernelAvailable = geometry.MeshDenoiseKernelAvailable;
  bindings.MeshCurvatureKernelAvailable = geometry.MeshCurvatureKernelAvailable;
  bindings.MeshCurvatureDirectionsAvailable =
      geometry.MeshCurvatureDirectionsAvailable;
  bindings.MeshRemeshUniformKernelAvailable =
      geometry.MeshRemeshUniformKernelAvailable;
  bindings.MeshRemeshAdaptiveKernelAvailable =
      geometry.MeshRemeshAdaptiveKernelAvailable;
  bindings.MeshRemeshProjectToSurfaceAvailable =
      geometry.MeshRemeshProjectToSurfaceAvailable;
  bindings.MeshRemeshErrorBoundedSizingAvailable =
      geometry.MeshRemeshErrorBoundedSizingAvailable;
  bindings.MeshSubdivideLoopKernelAvailable =
      geometry.MeshSubdivideLoopKernelAvailable;
  bindings.MeshSubdivideCatmullClarkKernelAvailable =
      geometry.MeshSubdivideCatmullClarkKernelAvailable;
  bindings.MeshSubdivideSqrt3KernelAvailable =
      geometry.MeshSubdivideSqrt3KernelAvailable;
  bindings.MeshSubdivideLoopFeatureEdgesAvailable =
      geometry.MeshSubdivideLoopFeatureEdgesAvailable;
  bindings.MeshSimplifyKernelAvailable = geometry.MeshSimplifyKernelAvailable;

  bindings.TextureBake = visualization.TextureBake;
  bindings.VisualizationRecipes = visualization.VisualizationRecipes;
  bindings.VisualizationRecipeRevision =
      visualization.VisualizationRecipeRevision;
  if (!bindings.JobCommands.Available())
    bindings.JobCommands = visualization.JobCommands;
  bindings.ModelBuildStats = visualization.ModelBuildStats;
  bindings.VisualizationCommandsAvailable =
      visualization.VisualizationCommandsAvailable;

  bindings.RenderGraphStats = renderRecipe.RenderGraphStats;
  bindings.RenderRecipeContext = renderRecipe.RenderRecipeContext;
  bindings.RenderRecipeEditorState = renderRecipe.RenderRecipeEditorState;
  bindings.RenderRecipeRuntimeState = renderRecipe.RenderRecipeRuntimeState;
  bindings.EngineConfigControlState =
      geometry.EngineConfigControlState != nullptr
          ? geometry.EngineConfigControlState
          : renderRecipe.EngineConfigControlState;
  bindings.PreviewRenderRecipeDocument =
      renderRecipe.PreviewRenderRecipeDocument;
  bindings.ApplyRenderRecipePreview = renderRecipe.ApplyRenderRecipePreview;
  bindings.PreviewEngineConfigDocument =
      geometry.PreviewEngineConfigDocument
          ? geometry.PreviewEngineConfigDocument
          : renderRecipe.PreviewEngineConfigDocument;
  bindings.ApplyEngineConfigHotSubset =
      geometry.ApplyEngineConfigHotSubset
          ? geometry.ApplyEngineConfigHotSubset
          : renderRecipe.ApplyEngineConfigHotSubset;
  bindings.RenderArtifacts = renderRecipe.RenderArtifacts;
  bindings.RenderRecipeCommandsAvailable =
      renderRecipe.RenderRecipeCommandsAvailable;
  bindings.EngineConfigCommandsAvailable =
      geometry.EngineConfigCommandsAvailable ||
      renderRecipe.EngineConfigCommandsAvailable;
  if (!bindings.AttachmentActive)
    bindings.AttachmentActive =
        geometry.AttachmentActive        ? geometry.AttachmentActive
        : visualization.AttachmentActive ? visualization.AttachmentActive
                                         : renderRecipe.AttachmentActive;
  if (!bindings.InvalidateWorkspaceSnapshotCache) {
    bindings.InvalidateWorkspaceSnapshotCache =
        geometry.InvalidateWorkspaceSnapshotCache
            ? geometry.InvalidateWorkspaceSnapshotCache
            : visualization.InvalidateWorkspaceSnapshotCache;
  }
  bindings.SelectedModelCache = context.SelectedModelCache;
  return bindings;
}

namespace
{
    namespace A = Extrinsic::Assets;
        [[nodiscard]] bool IsModelTextureImportPayload(
            const A::AssetPayloadKind payloadKind) noexcept
        {
            return payloadKind == A::AssetPayloadKind::ModelScene ||
                   payloadKind == A::AssetPayloadKind::Texture2D;
        }
        [[nodiscard]] std::string ErrorName(const Core::ErrorCode error)
        {
            return std::string(Core::Error::ToString(error));
        }

        [[nodiscard]] std::string BuildImportSuccessMessage(
            const EditorFileImportCommand& command,
            const EditorFileImportResult& result)
        {
            std::string message = "Imported ";
            message += A::DebugNameForAssetPayloadKind(result.PayloadKind);
            message += " asset";
            if (!command.Path.empty())
            {
                message += " from ";
                message += command.Path;
            }
            message += ".";
            return message;
        }

        [[nodiscard]] std::string BuildImportPendingMessage(
            const EditorFileImportCommand& command,
            const A::AssetPayloadKind payloadKind)
        {
            std::string message = "Queued ";
            message += A::DebugNameForAssetPayloadKind(payloadKind);
            message += " asset import";
            if (!command.Path.empty())
            {
                message += " from ";
                message += command.Path;
            }
            message += ".";
            return message;
        }

        [[nodiscard]] std::string BuildImportFailureMessage(
            const Core::ErrorCode error)
        {
            std::string message = "Asset import failed: ";
            message += ErrorName(error);
            message += ".";
            return message;
        }

        [[nodiscard]] EditorFileImportResult BuildFileImportResultFromRuntimeEvent(
            const RuntimeAssetImportEvent& event)
        {
            if (!event.Result.has_value())
            {
                return EditorFileImportResult{
                    .Status = EditorCommandStatus::AssetImportFailed,
                    .PayloadKind = event.RequestedPayloadKind,
                    .Error = event.Error,
                    .Message = BuildImportFailureMessage(event.Error),
                };
            }

            const RuntimeAssetImportResult& imported = *event.Result;
            EditorFileImportResult result{
                .Status = EditorCommandStatus::Applied,
                .Asset = imported.Asset,
                .PayloadKind = imported.PayloadKind,
                .Error = Core::ErrorCode::Success,
                .PrimitiveEntitiesCreated = imported.PrimitiveEntitiesCreated,
                .EmbeddedTextureAssetsCreated = imported.EmbeddedTextureAssetsCreated,
                .TextureUploadRequests = imported.TextureUploadRequests,
                .MaterializedModelScene = imported.MaterializedModelScene,
                .RequestedTextureUpload = imported.RequestedTextureUpload,
            };
            result.Message = BuildImportSuccessMessage(
                EditorFileImportCommand{
                    .Path = event.Path,
                    .PayloadKind = event.RequestedPayloadKind,
                },
                result);
            return result;
        }

        [[nodiscard]] std::string BuildSceneFileSuccessMessage(
            const EditorSceneFileCommand& command,
            const EditorSceneFileResult& result)
        {
            std::string message{};
            switch (result.Operation)
            {
            case EditorSceneFileOperation::New:
                message = "Created new scene";
                break;
            case EditorSceneFileOperation::Save:
                message = "Saved scene";
                break;
            case EditorSceneFileOperation::Load:
                message = "Opened scene";
                break;
            case EditorSceneFileOperation::Close:
                message = "Closed scene";
                break;
            }
            if (!command.Path.empty())
            {
                if (result.Operation == EditorSceneFileOperation::Save)
                    message += " to ";
                else if (result.Operation == EditorSceneFileOperation::Load)
                    message += " from ";
                else
                    message += " ";
                message += command.Path;
            }
            message += " (entities=";
            message += std::to_string(result.Stats.Entities);
            message += ", mesh=";
            message += std::to_string(result.Stats.MeshEntities);
            message += ", graph=";
            message += std::to_string(result.Stats.GraphEntities);
            message += ", pointCloud=";
            message += std::to_string(result.Stats.PointCloudEntities);
            message += ").";
            return message;
        }

        [[nodiscard]] std::string BuildSceneFileFailureMessage(
            const EditorSceneFileOperation operation,
            const Core::ErrorCode error)
        {
            std::string message{};
            switch (operation)
            {
            case EditorSceneFileOperation::New:
                message = "Scene new failed: ";
                break;
            case EditorSceneFileOperation::Save:
                message = "Scene save failed: ";
                break;
            case EditorSceneFileOperation::Load:
                message = "Scene open failed: ";
                break;
            case EditorSceneFileOperation::Close:
                message = "Scene close failed: ";
                break;
            }
            message += ErrorName(error);
            message += ".";
            return message;
        }

        [[nodiscard]] std::string BuildSceneFilePendingMessage(
            const EditorSceneFileCommand& command,
            const EditorSceneFileOperation operation)
        {
            std::string message{};
            switch (operation)
            {
            case EditorSceneFileOperation::New:
                message = "Queued scene new";
                break;
            case EditorSceneFileOperation::Save:
                message = "Queued scene save";
                break;
            case EditorSceneFileOperation::Load:
                message = "Queued scene open";
                break;
            case EditorSceneFileOperation::Close:
                message = "Queued scene close";
                break;
            }
            if (!command.Path.empty())
            {
                if (operation == EditorSceneFileOperation::Save)
                    message += " to ";
                else if (operation == EditorSceneFileOperation::Load)
                    message += " from ";
                else
                    message += " ";
                message += command.Path;
            }
            message += ".";
            return message;
        }

        [[nodiscard]] EditorSceneFileOperation ToSandboxSceneFileOperation(
            const RuntimeSceneFileOperation operation) noexcept
        {
            switch (operation)
            {
            case RuntimeSceneFileOperation::Save:
                return EditorSceneFileOperation::Save;
            case RuntimeSceneFileOperation::Load:
                return EditorSceneFileOperation::Load;
            case RuntimeSceneFileOperation::None:
                break;
            }
            return EditorSceneFileOperation::Load;
        }

        [[nodiscard]] EditorSceneFileResult
        BuildSceneFileResultFromRuntimeEvent(const RuntimeSceneFileEvent& event)
        {
            const EditorSceneFileOperation operation =
                ToSandboxSceneFileOperation(event.Operation);
            if (!event.Succeeded())
            {
                return EditorSceneFileResult{
                    .Status = operation == EditorSceneFileOperation::Save
                        ? EditorCommandStatus::SceneSaveFailed
                        : EditorCommandStatus::SceneLoadFailed,
                    .Operation = operation,
                    .Task = event.Task,
                    .Error = event.Error,
                    .Message = BuildSceneFileFailureMessage(operation, event.Error),
                };
            }

            EditorSceneFileResult result{
                .Status = EditorCommandStatus::Applied,
                .Operation = operation,
                .Task = event.Task,
                .Error = Core::ErrorCode::Success,
            };
            if (operation == EditorSceneFileOperation::Load &&
                event.LoadResult.has_value())
            {
                result.Stats = event.LoadResult->Stats;
            }
            else if (operation == EditorSceneFileOperation::Save &&
                     event.SaveResult.has_value())
            {
                result.Stats = event.SaveResult->Stats;
            }
            result.Message = BuildSceneFileSuccessMessage(
                EditorSceneFileCommand{.Path = event.Path},
                result);
            return result;
        }
        [[nodiscard]] Graphics::UvViewBackgroundMode ToGraphicsUvViewBackground(
            const ParameterizationUvBackgroundMode mode) noexcept
        {
            using ConfigMode = ParameterizationUvBackgroundMode;
            switch (mode)
            {
            case ConfigMode::Grid:
                return Graphics::UvViewBackgroundMode::Grid;
            case ConfigMode::Checker:
                return Graphics::UvViewBackgroundMode::Checker;
            case ConfigMode::TexelDensity:
                return Graphics::UvViewBackgroundMode::TexelDensity;
            case ConfigMode::Texture:
                return Graphics::UvViewBackgroundMode::Texture;
            }
            return Graphics::UvViewBackgroundMode::Grid;
        }

        [[nodiscard]] ParameterizationUvBackgroundMode
        ToConfigUvViewBackground(
            const Graphics::UvViewBackgroundMode mode) noexcept
        {
            using ConfigMode = ParameterizationUvBackgroundMode;
            switch (mode)
            {
            case Graphics::UvViewBackgroundMode::Grid:
                return ConfigMode::Grid;
            case Graphics::UvViewBackgroundMode::Checker:
                return ConfigMode::Checker;
            case Graphics::UvViewBackgroundMode::TexelDensity:
                return ConfigMode::TexelDensity;
            case Graphics::UvViewBackgroundMode::Texture:
                return ConfigMode::Texture;
            }
            return ConfigMode::Grid;
        }

        [[nodiscard]] EditorParameterizationUvViewStatus
        ToSandboxUvViewStatus(const Graphics::UvViewStatus status) noexcept
        {
            using SandboxStatus =
                EditorParameterizationUvViewStatus;
            switch (status)
            {
            case Graphics::UvViewStatus::Disabled:
                return SandboxStatus::Disabled;
            case Graphics::UvViewStatus::CpuFallbackNonOperational:
                return SandboxStatus::CpuFallbackNonOperational;
            case Graphics::UvViewStatus::WaitingForGeometry:
                return SandboxStatus::WaitingForGeometry;
            case Graphics::UvViewStatus::InvalidRequest:
                return SandboxStatus::InvalidRequest;
            case Graphics::UvViewStatus::ResourceCreationFailed:
                return SandboxStatus::ResourceCreationFailed;
            case Graphics::UvViewStatus::Ready:
                return SandboxStatus::Ready;
            }
            return SandboxStatus::InvalidRequest;
        }

        void MixSandboxUvViewToken(
            std::uint64_t& token,
            const std::uint64_t value) noexcept
        {
            token ^= value + 0x9E3779B97F4A7C15ull +
                     (token << 6u) + (token >> 2u);
        }

        [[nodiscard]] EditorParameterizationUvViewState
        SubmitRuntimeParameterizationUvView(Graphics::IRenderer& renderer, RHI::IDevice& device,
                                            ServiceRegistry& services,
                                            const RenderExtractionCache* renderExtraction,
                                            EditorParameterizationUvViewRequest request)
        {
            using ConfigBackground = ParameterizationUvBackgroundMode;
            using ConfigMode = ParameterizationUvRenderMode;
            using SandboxStatus =
                EditorParameterizationUvViewStatus;

            EditorParameterizationUvViewState state{
                .Status = request.Enabled
                    ? SandboxStatus::WaitingForGpuFrame
                    : SandboxStatus::CpuLayout,
                .RequestedMode = request.View.RenderMode,
                .ActiveMode = ConfigMode::CpuLayout,
                .RequestedBackground = request.View.BackgroundMode,
                .ActiveBackground =
                    request.View.BackgroundMode == ConfigBackground::Grid ||
                            request.View.BackgroundMode == ConfigBackground::Checker
                        ? request.View.BackgroundMode
                        : ConfigBackground::Checker,
                .RequestToken = request.RequestToken,
                .Width = request.Width,
                .Height = request.Height,
                .Message = request.Enabled
                    ? "GPU UV view is waiting for the next rendered frame."
                    : "CPU UV layout is active.",
            };

            if (!request.Enabled)
            {
                Graphics::UvViewRequest disabledRequest{};
                disabledRequest.RequestToken = request.RequestToken;
                renderer.SubmitUvViewRequest(std::move(disabledRequest));
                return state;
            }

            std::optional<Graphics::GpuGeometryHandle> geometry{};
            if (renderExtraction != nullptr)
            {
                const auto availability =
                    renderExtraction->FindGpuRenderableAvailability(
                        request.StableEntityId);
                if (availability.has_value() &&
                    availability->Surface.HasGeometry)
                {
                    geometry = availability->Surface.Geometry;
                }
            }
            if (geometry.has_value())
            {
                MixSandboxUvViewToken(state.RequestToken, geometry->Index);
                MixSandboxUvViewToken(state.RequestToken, geometry->Generation);
            }
            else
            {
                MixSandboxUvViewToken(state.RequestToken, 0xFFFFFFFFFFFFFFFFull);
            }

            RHI::BindlessIndex backgroundTexture =
                RHI::kInvalidBindlessIndex;
            std::uint64_t backgroundTextureGeneration = 0u;
            const Graphics::GpuAssetCache* const gpuAssetCache =
                services.Find<Graphics::GpuAssetCache>();
            if (renderExtraction != nullptr &&
                gpuAssetCache != nullptr &&
                request.View.BackgroundMode == ConfigBackground::Texture)
            {
                const auto bindings =
                    renderExtraction->GetMaterialTextureAssetBindings(
                        request.StableEntityId);
                if (bindings.has_value() && bindings->Albedo.IsValid())
                {
                    const auto view =
                        gpuAssetCache->GetView(bindings->Albedo);
                    if (view.has_value() &&
                        view->Kind == Graphics::GpuAssetKind::Texture &&
                        view->BindlessIdx != RHI::kInvalidBindlessIndex)
                    {
                        backgroundTexture = view->BindlessIdx;
                        backgroundTextureGeneration = view->Generation;
                    }
                }
            }
            MixSandboxUvViewToken(state.RequestToken, backgroundTexture);
            MixSandboxUvViewToken(
                state.RequestToken,
                backgroundTextureGeneration);

            Graphics::UvViewRequest graphicsRequest{
                .Enabled = true,
                .RequestToken = state.RequestToken,
                .Geometry = geometry.value_or(Graphics::GpuGeometryHandle{}),
                .Width = request.Width,
                .Height = request.Height,
                .Bounds = Graphics::UvViewBounds{
                    .MinU = request.UvBoundsMin.x,
                    .MinV = request.UvBoundsMin.y,
                    .MaxU = request.UvBoundsMax.x,
                    .MaxV = request.UvBoundsMax.y,
                },
                .Background =
                    ToGraphicsUvViewBackground(request.View.BackgroundMode),
                .BackgroundTexture = backgroundTexture,
                .ShowDistortionHeatmap =
                    request.View.ShowDistortionHeatmap,
                .LineIndices = std::move(request.LineIndices),
                .TriangleConformalDistortion =
                    std::move(request.TriangleConformalDistortion),
            };
            renderer.SubmitUvViewRequest(std::move(graphicsRequest));

            if (!device.IsOperational())
            {
                state.Status = SandboxStatus::CpuFallbackNonOperational;
                state.Message = "GPU UV view is unavailable because the render device is "
                                "not operational; CPU layout is active.";
                return state;
            }
            if (!geometry.has_value())
            {
                state.Status = SandboxStatus::WaitingForGeometry;
                state.Message = "Selected mesh GPU surface residency is not ready; CPU "
                                "layout is active.";
                return state;
            }

            const Graphics::UvViewOutput output = renderer.GetUvViewOutput();
            if (output.RequestToken != state.RequestToken)
                return state;

            state.Status = ToSandboxUvViewStatus(output.Status);
            state.ActiveMode = output.ActiveMode ==
                    Graphics::UvViewActiveMode::GpuShaded
                ? ConfigMode::GpuShaded
                : ConfigMode::CpuLayout;
            state.RequestedBackground =
                ToConfigUvViewBackground(output.RequestedBackground);
            state.ActiveBackground =
                ToConfigUvViewBackground(output.ActiveBackground);
            state.HeatmapActive = output.HeatmapActive;
            state.TargetGeneration = output.TargetGeneration;
            state.RecordedPassCount = output.RecordedPassCount;
            state.Message = output.Diagnostic;
            if (state.ActiveMode == ConfigMode::CpuLayout &&
                state.ActiveBackground != ConfigBackground::Grid &&
                state.ActiveBackground != ConfigBackground::Checker)
            {
                state.ActiveBackground = ConfigBackground::Checker;
            }

            const bool extentMatches = output.Width == request.Width &&
                                       output.Height == request.Height;
            if (output.Status == Graphics::UvViewStatus::Ready &&
                (!extentMatches || !output.IsGpuReady() ||
                 output.RecordedPassCount == 0u))
            {
                state.Status = SandboxStatus::WaitingForGpuFrame;
                state.ActiveMode = ConfigMode::CpuLayout;
                if (state.ActiveBackground != ConfigBackground::Grid &&
                    state.ActiveBackground != ConfigBackground::Checker)
                {
                    state.ActiveBackground = ConfigBackground::Checker;
                }
                state.Message = "GPU UV view target is not yet ready for this pane extent; "
                                "CPU layout is active.";
                return state;
            }
            if (output.Status == Graphics::UvViewStatus::Ready)
            {
                state.GpuReady = true;
                state.BindlessIndex = output.BindlessIndex;
                state.Width = output.Width;
                state.Height = output.Height;
            }
            return state;
        }

        [[nodiscard]] EditorFeatureBindings BuildContextFromRuntime(WorldRegistry& worlds,
                                                                   ServiceRegistry& services)
        {
            RenderExtractionCache* renderExtraction    = services.Find<RenderExtractionCache>();
            EngineConfigControl* configControl         = services.Find<EngineConfigControl>();
            Platform::IWindow* const window            = services.Find<Platform::IWindow>();
            RHI::IDevice* const device                 = services.Find<RHI::IDevice>();
            Graphics::IRenderer* const renderer        = services.Find<Graphics::IRenderer>();
            const auto activeWorld                     = worlds.ActiveWorld();
            ECS::Scene::Registry* const activeScene    = worlds.Get(activeWorld);
            EditorCommandHistory* const commandHistory = services.Find<EditorCommandHistory>();
            SceneDocumentModule* const sceneDocuments  = services.Find<SceneDocumentModule>();
            SceneInteractionModule* const interaction  = services.Find<SceneInteractionModule>();
            SelectionController* const selection       = services.Find<SelectionController>();
            Assets::AssetService* const assetService   = services.Find<Assets::AssetService>();
            AssetWorkflowModule* const assetWorkflow = services.Find<AssetWorkflowModule>();
            TextureBakeService* const textureBake          = services.Find<TextureBakeService>();
            EditorFeatureBindings context{
                .Scene          = activeScene,
                .World          = activeWorld,
                .Selection      = selection,
                .CommandHistory = commandHistory,
                .AssetService   = assetService,
                .LastRefinedPrimitive =
                    interaction != nullptr ? &interaction->LastRefinedPrimitive() : nullptr,
                .LastRefinedPrimitiveGeneration =
                    interaction != nullptr ? interaction->LastRefinedPrimitiveGeneration() : 0u,
                .CameraControllers = services.Find<CameraControllerRegistry>(),
                .CameraViewport    = window != nullptr
                                         ? Core::Extent2D{window->GetFramebufferExtent().Width,
                                                       window->GetFramebufferExtent().Height}
                                         : Core::Extent2D{},
                .Device            = device,
                .TextureBake       = textureBake,
                .AssetImportCommands =
                    EditorAssetImportCommandSurface{
                        .Import =
                            [assetWorkflow](const EditorFileImportCommand& command)
                        {
                            if (assetWorkflow == nullptr)
                            {
                                return EditorFileImportResult{
                                    .Status =
                                        EditorCommandStatus::
                                            AssetImportFailed,
                                    .PayloadKind = command.PayloadKind,
                                    .Error = Core::ErrorCode::InvalidState,
                                    .Message = BuildImportFailureMessage(
                                        Core::ErrorCode::InvalidState),
                                };
                            }
                            auto route = Assets::ResolveAssetImportRoute(
                                command.Path,
                                Assets::AssetRouteOperation::Import,
                                Assets::AssetImportHint{
                                    .PayloadKind = command.PayloadKind,
                                });
                            if (route.has_value() &&
                                (IsModelTextureImportPayload(route->PayloadKind) ||
                                 Assets::IsGeometryPayloadKind(route->PayloadKind)))
                            {
                                AssetImportRecipe recipe{
                                    .Path = command.Path,
                                    .PayloadKind = route->PayloadKind,
                                };
                                auto queued =
                                    assetWorkflow->QueueAssetImport(
                                        std::move(recipe));
                                if (!queued.has_value())
                                {
                                    return EditorFileImportResult{
                                        .Status = EditorCommandStatus::AssetImportFailed,
                                        .PayloadKind = route->PayloadKind,
                                        .Error = queued.error(),
                                        .Message = BuildImportFailureMessage(queued.error()),
                                    };
                                }

                                return EditorFileImportResult{
                                    .Status = EditorCommandStatus::Pending,
                                    .Operation = queued->Operation,
                                    .PayloadKind = queued->PayloadKind,
                                    .Error = Core::ErrorCode::Success,
                                    .Message = BuildImportPendingMessage(
                                        command,
                                        queued->PayloadKind),
                                };
                            }

                            auto imported =
                                assetWorkflow->ImportAssetFromPath(
                                RuntimeAssetImportRequest{
                                    .Path = command.Path,
                                    .PayloadKind = command.PayloadKind,
                                });
                            if (!imported.has_value())
                            {
                                return EditorFileImportResult{
                                    .Status = EditorCommandStatus::AssetImportFailed,
                                    .PayloadKind = command.PayloadKind,
                                    .Error = imported.error(),
                                    .Message = BuildImportFailureMessage(imported.error()),
                                };
                            }

                            EditorFileImportResult result{
                                .Status = EditorCommandStatus::Applied,
                                .Asset = imported->Asset,
                                .PayloadKind = imported->PayloadKind,
                                .PrimitiveEntitiesCreated =
                                    imported->PrimitiveEntitiesCreated,
                                .EmbeddedTextureAssetsCreated =
                                    imported->EmbeddedTextureAssetsCreated,
                                .TextureUploadRequests =
                                    imported->TextureUploadRequests,
                                .MaterializedModelScene =
                                    imported->MaterializedModelScene,
                                .RequestedTextureUpload =
                                    imported->RequestedTextureUpload,
                            };
                            result.Message =
                                BuildImportSuccessMessage(command, result);
                            return result;
                        },
                    },
                .AssetImportQueueCommands =
                    EditorAssetImportQueueCommandSurface{
                        .ClearCompleted =
                            [assetWorkflow]()
                        {
                            return assetWorkflow != nullptr
                                ? assetWorkflow->
                                      ClearCompletedAssetImports()
                                : std::size_t{0u};
                        },
                        .Cancel =
                            [assetWorkflow](const RuntimeAssetIngestHandle operation)
                        {
                            return assetWorkflow != nullptr
                                ? assetWorkflow->
                                      CancelAssetImport(operation)
                                : Core::Err(
                                      Core::ErrorCode::InvalidState);
                        },
                    },
                .SceneFileCommands =
                    EditorSceneFileCommandSurface{
                        .New =
                            [sceneDocuments]()
                        {
                            if (sceneDocuments == nullptr)
                            {
                                return EditorSceneFileResult{
                                    .Status = EditorCommandStatus::SceneNewFailed,
                                    .Operation = EditorSceneFileOperation::New,
                                    .Error = Core::ErrorCode::InvalidState,
                                    .Message = BuildSceneFileFailureMessage(
                                        EditorSceneFileOperation::New,
                                        Core::ErrorCode::InvalidState),
                                };
                            }
                            Core::Result created =
                                sceneDocuments->NewSceneDocument();
                            if (!created.has_value())
                            {
                                return EditorSceneFileResult{
                                    .Status = EditorCommandStatus::SceneNewFailed,
                                    .Operation = EditorSceneFileOperation::New,
                                    .Error = created.error(),
                                    .Message = BuildSceneFileFailureMessage(
                                        EditorSceneFileOperation::New,
                                        created.error()),
                                };
                            }
                            EditorSceneFileResult result{
                                .Status = EditorCommandStatus::Applied,
                                .Operation = EditorSceneFileOperation::New,
                            };
                            result.Message = BuildSceneFileSuccessMessage({}, result);
                            return result;
                        },
                        .Save =
                            [sceneDocuments](const EditorSceneFileCommand& command)
                        {
                            if (sceneDocuments == nullptr)
                            {
                                return EditorSceneFileResult{
                                    .Status = EditorCommandStatus::SceneSaveFailed,
                                    .Operation = EditorSceneFileOperation::Save,
                                    .Error = Core::ErrorCode::InvalidState,
                                    .Message = BuildSceneFileFailureMessage(
                                        EditorSceneFileOperation::Save,
                                        Core::ErrorCode::InvalidState),
                                };
                            }
                            auto queued =
                                sceneDocuments->QueueSceneSaveToPath(
                                    command.Path);
                            if (!queued.has_value())
                            {
                                return EditorSceneFileResult{
                                    .Status = EditorCommandStatus::SceneSaveFailed,
                                    .Operation = EditorSceneFileOperation::Save,
                                    .Error = queued.error(),
                                    .Message = BuildSceneFileFailureMessage(
                                        EditorSceneFileOperation::Save,
                                        queued.error()),
                                };
                            }
                            EditorSceneFileResult result{
                                .Status = EditorCommandStatus::Pending,
                                .Operation = EditorSceneFileOperation::Save,
                                .Task = queued->Task,
                                .Error = Core::ErrorCode::Success,
                            };
                            result.Message = BuildSceneFilePendingMessage(
                                command,
                                result.Operation);
                            return result;
                        },
                        .Load =
                            [sceneDocuments](const EditorSceneFileCommand& command)
                        {
                            if (sceneDocuments == nullptr)
                            {
                                return EditorSceneFileResult{
                                    .Status = EditorCommandStatus::SceneLoadFailed,
                                    .Operation = EditorSceneFileOperation::Load,
                                    .Error = Core::ErrorCode::InvalidState,
                                    .Message = BuildSceneFileFailureMessage(
                                        EditorSceneFileOperation::Load,
                                        Core::ErrorCode::InvalidState),
                                };
                            }
                            auto queued =
                                sceneDocuments->QueueSceneLoadFromPath(
                                    command.Path);
                            if (!queued.has_value())
                            {
                                return EditorSceneFileResult{
                                    .Status = EditorCommandStatus::SceneLoadFailed,
                                    .Operation = EditorSceneFileOperation::Load,
                                    .Error = queued.error(),
                                    .Message = BuildSceneFileFailureMessage(
                                        EditorSceneFileOperation::Load,
                                        queued.error()),
                                };
                            }
                            EditorSceneFileResult result{
                                .Status = EditorCommandStatus::Pending,
                                .Operation = EditorSceneFileOperation::Load,
                                .Task = queued->Task,
                                .Error = Core::ErrorCode::Success,
                            };
                            result.Message = BuildSceneFilePendingMessage(
                                command,
                                result.Operation);
                            return result;
                        },
                        .Close =
                            [sceneDocuments]()
                        {
                            if (sceneDocuments == nullptr)
                            {
                                return EditorSceneFileResult{
                                    .Status = EditorCommandStatus::SceneCloseFailed,
                                    .Operation = EditorSceneFileOperation::Close,
                                    .Error = Core::ErrorCode::InvalidState,
                                    .Message = BuildSceneFileFailureMessage(
                                        EditorSceneFileOperation::Close,
                                        Core::ErrorCode::InvalidState),
                                };
                            }
                            Core::Result closed =
                                sceneDocuments->CloseSceneDocument();
                            if (!closed.has_value())
                            {
                                return EditorSceneFileResult{
                                    .Status = EditorCommandStatus::SceneCloseFailed,
                                    .Operation = EditorSceneFileOperation::Close,
                                    .Error = closed.error(),
                                    .Message = BuildSceneFileFailureMessage(
                                        EditorSceneFileOperation::Close,
                                        closed.error()),
                                };
                            }
                            EditorSceneFileResult result{
                                .Status = EditorCommandStatus::Applied,
                                .Operation = EditorSceneFileOperation::Close,
                            };
                            result.Message = BuildSceneFileSuccessMessage({}, result);
                            return result;
                        },
                    },
                .ParameterizationUvViewCommands =
                    EditorParameterizationUvViewCommandSurface{
                        .Submit =
                            [renderer, device, &services,
                             renderExtraction](EditorParameterizationUvViewRequest request)
                        {
                            if (renderer == nullptr || device == nullptr)
                            {
                                return EditorParameterizationUvViewState{};
                            }
                            return SubmitRuntimeParameterizationUvView(
                                *renderer, *device, services, renderExtraction, std::move(request));
                        },
                    },
                .VisualizationRecipes =
                    EditorVisualizationRecipeCommandSurface{
                        .GetRecipe =
                            [renderExtraction](const std::uint32_t stableEntityId)
                        {
                            return renderExtraction != nullptr
                                       ? renderExtraction->GetVisualizationRecipe(
                                             stableEntityId)
                                       : std::optional<VisualizationRecipe>{};
                        },
                        .SetRecipe =
                            [renderExtraction](
                                const std::uint32_t stableEntityId,
                                VisualizationRecipe recipe)
                        {
                            if (renderExtraction != nullptr)
                                renderExtraction->SetVisualizationRecipe(
                                    stableEntityId, std::move(recipe));
                        },
                        .ClearRecipe =
                            [renderExtraction](const std::uint32_t stableEntityId)
                        {
                            if (renderExtraction != nullptr)
                                renderExtraction->ClearVisualizationRecipe(stableEntityId);
                        },
                    },
                .VisualizationRecipeRevision =
                    renderExtraction != nullptr
                        ? renderExtraction->GetVisualizationRecipeRevision()
                        : 0u,
                .AssetImportQueue   = assetWorkflow != nullptr
                                          ? assetWorkflow->GetAssetImportQueueSnapshot()
                                          : RuntimeAssetImportQueueSnapshot{},
                .RenderGraphStats =
                    renderer != nullptr ? &renderer->GetLastRenderGraphStats() : nullptr,
                .ImGuiAdapterAvailable =
                    [&services]
                {
                    const EditorUiHost* host = services.Find<EditorUiHost>();
                    return host != nullptr && host->IsOperational();
                }(),
                .AssetImportCommandsAvailable   = assetWorkflow != nullptr,
                .SceneFileCommandsAvailable     = true,
                .CameraRenderCommandsAvailable  = true,
                .VisualizationCommandsAvailable = true,
            };
            if (configControl != nullptr)
            {
                context.RenderRecipeRuntimeState =
                    &configControl->GetRenderRecipeState();
                context.PreviewRenderRecipeDocument =
                    [configControl](const std::string& document,
                                    const std::string& sourceId)
                    {
                        return configControl
                            ->PreviewRenderRecipeConfigDocument(
                                document,
                                sourceId);
                    };
                context.ApplyRenderRecipePreview =
                    [configControl](
                        const Graphics::RenderRecipeConfigLoadResult&
                            loadResult)
                    {
                        return configControl
                            ->ApplyRenderRecipeConfigPreview(
                                loadResult,
                                RuntimeRenderRecipeActivationSource::Editor);
                    };
                context.RenderRecipeCommandsAvailable = true;
            }
            return context;
        }

} // namespace

EditorFileImportResult ProjectEditorFileImportResult(
    const RuntimeAssetImportEvent& event)
{
    return BuildFileImportResultFromRuntimeEvent(event);
}

EditorSceneFileResult ProjectEditorSceneFileResult(
    const RuntimeSceneFileEvent& event)
{
    return BuildSceneFileResultFromRuntimeEvent(event);
}

EditorFeatureBindings MakeEditorFeatureBindings(
    WorldRegistry& worlds,
    ServiceRegistry& services)
{
    return BuildContextFromRuntime(worlds, services);
}

} // namespace Extrinsic::Runtime::EditorFeatureDetail
