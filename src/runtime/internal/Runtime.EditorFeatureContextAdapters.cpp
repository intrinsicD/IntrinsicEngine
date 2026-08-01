module;

module Extrinsic.Runtime.Private.EditorFeatures;

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
      .ParameterizationUvViewCommands = bindings.ParameterizationUvViewCommands,
      .JobCommands = bindings.JobCommands,
      .MethodResultSinks = bindings.MethodResultSinks,
      .LastKMeansResult = bindings.LastKMeansResult,
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
      .ParameterizationUvViewCommands = context.ParameterizationUvViewCommands,
      .JobCommands = context.JobCommands,
      .MethodResultSinks = context.MethodResultSinks,
      .LastKMeansResult = context.LastKMeansResult,
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
  bindings.ParameterizationUvViewCommands =
      geometry.ParameterizationUvViewCommands;
  bindings.JobCommands = geometry.JobCommands;
  bindings.MethodResultSinks = geometry.MethodResultSinks;
  bindings.LastKMeansResult = geometry.LastKMeansResult;
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
} // namespace Extrinsic::Runtime::EditorFeatureDetail
