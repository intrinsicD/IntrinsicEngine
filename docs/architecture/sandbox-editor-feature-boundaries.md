# Sandbox editor feature boundaries

RUNTIME-202 retired the all-feature Sandbox runtime facade and its shared
Sandbox config/default-policy surface. Runtime now publishes focused workspace,
job, scene, geometry, visualization, render-recipe, and feature-config modules.
The Sandbox app owns window/menu/ImGui state, config-registration and default
aggregation, plus the copied `SandboxEditorContext` and `SandboxEditorFrame`
composition records.

Prepared-frame command/query handles carry the workspace attachment epoch.
Retaining one beyond detach is observable as unbound, and every operation
fails closed before reaching the copied service pointers; callback surfaces
retain their operation-specific expired-attachment diagnostics. Mutating
feature contexts receive only an epoch-guarded cache-invalidation callback,
not the workspace cache object, so scene/geometry/visualization operations
preserve selected-model cache behavior without widening their public owner
boundaries.

The shared `Extrinsic.Runtime.Private.EditorFeatures` and
`Extrinsic.Runtime.Private.FeatureConfigCodecs` BMIs are implementation detail.
Production app sources may not import `Extrinsic.Runtime.Private.*`; source
ratchets enforce that boundary. The standalone registration-alignment wrapper
is deleted: the typed registration operation privately invokes
`Geometry::Registration::AlignICP` and retains the observer trajectory only for
that operation.

Physical implementation ownership follows the same feature split. Scene,
geometry, visualization, and render-recipe operation bodies compile in their
respective operation units; they are not forwarded through the private editor
BMI. `Runtime.EditorWorkspaceSnapshots.Models.cpp` owns presentation-free
workspace model assembly, while the bounded
`internal/Runtime.EditorWorkspaceSession.cpp` owns only attachment epochs, job
identity/result retention, and prepared-frame lifecycle. The private detail BMI
contains the attachment binding and session declaration, and
`Runtime.EditorFeatureContextAdapters.cpp` projects composed runtime services
into feature contexts. Neither private surface is an all-feature operation
facade.

## Public ownership map

| Area | Current owner |
|---|---|
| Workspace attachment and copied snapshots | `Extrinsic.Runtime.EditorWorkspaceSnapshots` |
| Job identity/progress projection | `Extrinsic.Runtime.EditorJobProjection` |
| Selection, import, scene-file, transform, camera, primitive-view operations | `Extrinsic.Runtime.SceneEditingOperations` |
| Geometry methods, texture/UV, clustering, registration, parameterization | `Extrinsic.Runtime.GeometryProcessingOperations` |
| Property, presentation, binding, spatial-debug, visualization operations | `Extrinsic.Runtime.VisualizationEditingOperations` |
| Frame-graph, recipe, profiling, artifact operations | `Extrinsic.Runtime.RenderRecipeEditingOperations` |
| Clustering config schema/codec | `Extrinsic.Runtime.ClusteringConfig` |
| Progressive-Poisson config schema/codec | `Extrinsic.Runtime.ProgressivePoissonConfig` |
| Parameterization config schema/codec | `Extrinsic.Runtime.ParameterizationConfig` |
| Sandbox context/frame, windows, defaults, registration aggregation | `Extrinsic.Sandbox.Editor.Shell`, `Extrinsic.Sandbox.ConfigSections`, and `Extrinsic.Sandbox` |

## Workflow verification matrix

| Workflow | Owning path | Coverage |
|---|---|---|
| Workspace hierarchy, inspector, selection, cache invalidation | workspace snapshot + scene operations | `Test.SandboxEditorModels.cpp`, `Test.RuntimeReferenceScene.cpp` |
| File import, reimport, queue state, scene save/load/new/close | scene operations + `AssetWorkflowModule` / `SceneDocumentModule` | `Test.SandboxEditorSceneCommands.cpp`, `Test.AssetImportFormatCoverage.cpp` |
| Clustering submit, pending/ready/failure, stale completion | `ClusteringService` + geometry operations + job projection | `Test.SandboxEditorClusteringMethods.cpp`, `Test.SandboxEditorSessionLifecycle.cpp` |
| Denoise, curvature, remesh, subdivide, simplify, normals, outliers, registration | geometry operations | `Test.SandboxEditorMeshMethods.cpp`, `Test.SandboxEditorClusteringMethods.cpp` |
| Parameterization config, execution, undo, diagnostics, CPU/GPU view fallback | geometry operations + parameterization config | `Test.ParameterizationOperations.cpp`, `Test.SandboxParameterizationPanel.cpp` |
| Property catalog, bindings, texture bake, presentation, visualization recipes | visualization operations | `Test.SandboxEditorVisualization.cpp`, `Test.TextureBakeModule.cpp` |
| Frame graph, profiling, render-recipe preview/apply, artifact state | render-recipe operations + config control | `Test.RuntimeRenderRecipeActivation.cpp`, `Test.RuntimeConfigControl.cpp` |
| App-owned context/frame, window lifecycle, runtime-only imports | Sandbox shell | `Test.SandboxEditorPresentation.cpp`, `Test.SandboxDomainPanels.cpp` |
| Config file, UI, and agent/CLI parity | feature config modules + app registration + config control | `Test.SandboxConfigSections.cpp`, `Test.RuntimeConfigControl.cpp`, `Test.ParameterizationOperations.cpp` |

## Exhaustive removed-type ledger

This table is the no-compatibility-bucket inventory of every type exported by
the removed `Runtime.SandboxEditorFacades` and
`Runtime.SandboxConfigSections` module interfaces.

| Removed export | Current symbol | Owner | Disposition |
|---|---|---|---|
| `SandboxEditorAssetPayloadKind` | `EditorAssetPayloadKind` | `Runtime.SceneEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorCameraControllerKind` | `EditorCameraControllerKind` | `Runtime.SceneEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorRecipeSlotKind` | `EditorRecipeSlotKind` | `Runtime.RenderRecipeEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorRenderRecipeConfigState` | `EditorRenderRecipeConfigState` | `Runtime.RenderRecipeEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorRenderRecipeConfigDiagnosticCode` | `EditorRenderRecipeConfigDiagnosticCode` | `Runtime.RenderRecipeEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorDiagnosticCode` | `EditorDiagnosticCode` | `Runtime.EditorWorkspaceSnapshots.cppm` | feature-owned runtime contract |
| `SandboxEditorCommandStatus` | `EditorCommandStatus` | `Runtime.EditorWorkspaceSnapshots.cppm` | feature-owned runtime contract |
| `SandboxEditorDomainWindowKind` | `EditorDomainWindowKind` | `Runtime.EditorWorkspaceSnapshots.cppm` | feature-owned runtime contract |
| `SandboxEditorVisualizationPropertyDomain` | `EditorVisualizationPropertyDomain` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorVisualizationPropertyPreset` | `EditorVisualizationPropertyPreset` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorVisualizationTarget` | `EditorVisualizationTarget` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorGeometryProcessingDomain` | `EditorGeometryProcessingDomain` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorGeometryProcessingAlgorithm` | `EditorGeometryProcessingAlgorithm` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorGeometryProcessingCapabilities` | `EditorGeometryProcessingCapabilities` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorGeometryProcessingEntry` | `EditorGeometryProcessingEntry` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorGeometryProcessingMenuItem` | `EditorGeometryProcessingMenuItem` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorProgressivePoissonChannel` | `EditorProgressivePoissonChannel` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorProgressivePoissonBackend` | `EditorProgressivePoissonBackend` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorProgressivePoissonConfig` | `EditorProgressivePoissonConfig` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorProgressivePoissonCommand` | `EditorProgressivePoissonCommand` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorProgressivePoissonResult` | `EditorProgressivePoissonResult` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorProgressivePoissonConfigStatus` | `EditorProgressivePoissonConfigStatus` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorProgressivePoissonConfigCommand` | `EditorProgressivePoissonConfigCommand` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorProgressivePoissonConfigResult` | `EditorProgressivePoissonConfigResult` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorMeshDenoiseStage` | `EditorMeshDenoiseStage` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorMeshCurvatureOutput` | `EditorMeshCurvatureOutput` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorMeshRemeshMode` | `EditorMeshRemeshMode` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorMeshRemeshSizingLaw` | `EditorMeshRemeshSizingLaw` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorMeshSubdivideOperator` | `EditorMeshSubdivideOperator` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorMeshSimplifyMetric` | `EditorMeshSimplifyMetric` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorMeshDenoiseCommand` | `EditorMeshDenoiseCommand` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorMeshDenoiseResult` | `EditorMeshDenoiseResult` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorMeshCurvatureCommand` | `EditorMeshCurvatureCommand` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorMeshCurvatureResult` | `EditorMeshCurvatureResult` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorMeshRemeshCommand` | `EditorMeshRemeshCommand` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorMeshRemeshResult` | `EditorMeshRemeshResult` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorMeshSubdivideCommand` | `EditorMeshSubdivideCommand` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorMeshSubdivideResult` | `EditorMeshSubdivideResult` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorMeshSimplifyCommand` | `EditorMeshSimplifyCommand` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorMeshSimplifyResult` | `EditorMeshSimplifyResult` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorICPVariant` | `EditorICPVariant` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorRegistrationCommand` | `EditorRegistrationCommand` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorRegistrationResult` | `EditorRegistrationResult` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorMeshVertexNormalsCommand` | `EditorMeshVertexNormalsCommand` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorMeshVertexNormalsResult` | `EditorMeshVertexNormalsResult` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorGraphVertexNormalsCommand` | `EditorGraphVertexNormalsCommand` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorGraphVertexNormalsResult` | `EditorGraphVertexNormalsResult` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorPointCloudVertexNormalsCommand` | `EditorPointCloudVertexNormalsCommand` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorPointCloudVertexNormalsResult` | `EditorPointCloudVertexNormalsResult` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorPointCloudOutlierMethod` | `EditorPointCloudOutlierMethod` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorPointCloudOutlierRemovalCommand` | `EditorPointCloudOutlierRemovalCommand` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorPointCloudOutlierRemovalResult` | `EditorPointCloudOutlierRemovalResult` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorDiagnostic` | `EditorDiagnostic` | `Runtime.EditorWorkspaceSnapshots.cppm` | feature-owned runtime contract |
| `SandboxEditorEntityRow` | `EditorEntityRow` | `Runtime.SceneEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorTransformModel` | `EditorTransformModel` | `Runtime.SceneEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorRenderHintModel` | `EditorRenderHintModel` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorGeometryDomainModel` | `EditorGeometryDomainModel` | `Runtime.EditorWorkspaceSnapshots.cppm` | feature-owned runtime contract |
| `SandboxEditorGeometryPresentationPropertyOptionModel` | `EditorGeometryPresentationPropertyOptionModel` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorPropertyCatalogDomain` | `EditorPropertyCatalogDomain` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorPropertyValuePreview` | `EditorPropertyValuePreview` | `Runtime.EditorWorkspaceSnapshots.cppm` | feature-owned runtime contract |
| `SandboxEditorPropertyCatalogRow` | `EditorPropertyCatalogRow` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorPropertyBindingTargetModel` | `EditorPropertyBindingTargetModel` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorVertexChannelBindingOptionModel` | `EditorVertexChannelBindingOptionModel` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorVertexChannelBindingTargetModel` | `EditorVertexChannelBindingTargetModel` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorPropertyCatalogModel` | `EditorPropertyCatalogModel` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorGeometryPresentationSlotModel` | `EditorGeometryPresentationSlotModel` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorJobScope` | `EditorJobScope` | `Runtime.EditorJobProjection.cppm` | feature-owned runtime contract |
| `SandboxEditorJobIdentity` | `EditorJobIdentity` | `Runtime.EditorJobProjection.cppm` | feature-owned runtime contract |
| `SandboxEditorJobDependency` | `EditorJobDependency` | `Runtime.EditorJobProjection.cppm` | feature-owned runtime contract |
| `SandboxEditorJobDomain` | `EditorJobDomain` | `Runtime.EditorJobProjection.cppm` | feature-owned runtime contract |
| `SandboxEditorJobRecord` | `EditorJobRecord` | `Runtime.EditorJobProjection.cppm` | feature-owned runtime contract |
| `SandboxEditorJobQueueSnapshot` | `EditorJobQueueSnapshot` | `Runtime.EditorJobProjection.cppm` | feature-owned runtime contract |
| `SandboxEditorJobDependencyModel` | `EditorJobDependencyModel` | `Runtime.EditorJobProjection.cppm` | feature-owned runtime contract |
| `SandboxEditorJobModel` | `EditorJobModel` | `Runtime.EditorJobProjection.cppm` | feature-owned runtime contract |
| `SandboxEditorGeometryCompositionSummary` | `EditorGeometryCompositionSummary` | `Runtime.EditorWorkspaceSnapshots.cppm` | feature-owned runtime contract |
| `SandboxEditorGeometryPresentationModel` | `EditorGeometryPresentationModel` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorBoundRenderStateRowKind` | `EditorBoundRenderStateRowKind` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorBoundRenderStateRow` | `EditorBoundRenderStateRow` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorBoundRenderStateModel` | `EditorBoundRenderStateModel` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorTextureBakeSourceCategory` | `EditorTextureBakeSourceCategory` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorTextureBakeSourceRow` | `EditorTextureBakeSourceRow` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorUvDiagnosticsModel` | `EditorUvDiagnosticsModel` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorTextureBakeTarget` | `EditorTextureBakeTarget` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorTextureBakeTargetSnapshot` | `EditorTextureBakeTargetSnapshot` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorTextureBakeTargetUpdateRequest` | `EditorTextureBakeTargetUpdateRequest` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorTextureBakeControlsModel` | `EditorTextureBakeControlsModel` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorInspectorModel` | `EditorInspectorModel` | `Runtime.SceneEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorPrimitiveDetailModel` | `EditorPrimitiveDetailModel` | `Runtime.SceneEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorSelectionModel` | `EditorSelectionModel` | `Runtime.SceneEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorSceneFileOperation` | `EditorSceneFileOperation` | `Runtime.SceneEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorSceneFileCommand` | `EditorSceneFileCommand` | `Runtime.SceneEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorSceneFileResult` | `EditorSceneFileResult` | `Runtime.SceneEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorSceneFileCommandSurface` | `EditorSceneFileCommandSurface` | `Runtime.SceneEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorSceneFileModel` | `EditorSceneFileModel` | `Runtime.SceneEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorDocumentModel` | `EditorDocumentModel` | `Runtime.SceneEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorFileImportCommand` | `EditorFileImportCommand` | `Runtime.SceneEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorFileImportResult` | `EditorFileImportResult` | `Runtime.SceneEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorAssetImportCommandSurface` | `EditorAssetImportCommandSurface` | `Runtime.SceneEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorFileImportPayloadOption` | `EditorFileImportPayloadOption` | `Runtime.SceneEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorFileImportModel` | `EditorFileImportModel` | `Runtime.SceneEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorAssetImportQueueCommandSurface` | `EditorAssetImportQueueCommandSurface` | `Runtime.SceneEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorAssetImportQueueRow` | `EditorAssetImportQueueRow` | `Runtime.SceneEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorAssetImportQueueModel` | `EditorAssetImportQueueModel` | `Runtime.SceneEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorRenderGraphPassModel` | `EditorRenderGraphPassModel` | `Runtime.RenderRecipeEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorGpuProfileQueueModel` | `EditorGpuProfileQueueModel` | `Runtime.RenderRecipeEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorGpuProfilePassModel` | `EditorGpuProfilePassModel` | `Runtime.RenderRecipeEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorGpuProfileModel` | `EditorGpuProfileModel` | `Runtime.RenderRecipeEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorGpuProfilingConfigStatus` | `EditorGpuProfilingConfigStatus` | `Runtime.RenderRecipeEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorGpuProfilingConfigResult` | `EditorGpuProfilingConfigResult` | `Runtime.RenderRecipeEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorRenderGraphModel` | `EditorRenderGraphModel` | `Runtime.RenderRecipeEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorRenderRecipeDraftState` | `EditorRenderRecipeDraftState` | `Runtime.RenderRecipeEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorRenderRecipeCommandKind` | `EditorRenderRecipeCommandKind` | `Runtime.RenderRecipeEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorRenderRecipeCommandStatus` | `EditorRenderRecipeCommandStatus` | `Runtime.RenderRecipeEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorRenderRecipeSlotModel` | `EditorRenderRecipeSlotModel` | `Runtime.RenderRecipeEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorRenderRecipeBindingOverrideModel` | `EditorRenderRecipeBindingOverrideModel` | `Runtime.RenderRecipeEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorRenderRecipeOutputModel` | `EditorRenderRecipeOutputModel` | `Runtime.RenderRecipeEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorRenderArtifactRow` | `EditorRenderArtifactRow` | `Runtime.RenderRecipeEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorRenderRecipeEditorModel` | `EditorRenderRecipeEditorModel` | `Runtime.RenderRecipeEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorRenderRecipeEditorState` | `EditorRenderRecipeEditorState` | `Runtime.RenderRecipeEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorRenderRecipeCommand` | `EditorRenderRecipeCommand` | `Runtime.RenderRecipeEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorRenderRecipeCommandResult` | `EditorRenderRecipeCommandResult` | `Runtime.RenderRecipeEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorPrimitiveViewSettings` | `EditorPrimitiveViewSettings` | `Runtime.SceneEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorPrimitiveViewCommandSurface` | `EditorPrimitiveViewCommandSurface` | `Runtime.SceneEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorVisualizationRecipeCommandSurface` | `EditorVisualizationRecipeCommandSurface` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorJobCommandSurface` | `EditorJobCommandSurface` | `Runtime.EditorJobProjection.cppm` | feature-owned runtime contract |
| `SandboxEditorUvRegenerationCommandResult` | `EditorUvRegenerationCommandResult` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorParameterizationResult` | `EditorParameterizationResult` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorMethodResultSinks` | `EditorMethodResultSinks` | `Runtime.EditorWorkspaceSnapshots.cppm` | feature-owned runtime contract |
| `SandboxEditorCameraRenderModel` | `EditorCameraRenderModel` | `Runtime.SceneEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorVisualizationConfigModel` | `EditorVisualizationConfigModel` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorVisualizationPropertyInfo` | `EditorVisualizationPropertyInfo` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorVisualizationRecipeModel` | `EditorVisualizationRecipeModel` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorVisualizationModel` | `EditorVisualizationModel` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorGeometryProcessingModel` | `EditorGeometryProcessingModel` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorDomainWindowModel` | `EditorDomainWindowModel` | `Runtime.EditorWorkspaceSnapshots.cppm` | feature-owned runtime contract |
| `SandboxEditorSelectedModelCacheSection` | `EditorSelectedModelCacheSection` | `Runtime.EditorWorkspaceSnapshots.cppm` | feature-owned runtime contract |
| `SandboxEditorSelectedAnalysisCacheConsumer` | `EditorSelectedAnalysisCacheConsumer` | `Runtime.EditorWorkspaceSnapshots.cppm` | feature-owned runtime contract |
| `SandboxEditorSelectedModelCacheKey` | `EditorSelectedModelCacheKey` | `Runtime.EditorWorkspaceSnapshots.cppm` | feature-owned runtime contract |
| `SandboxEditorSelectedAnalysisModel` | `EditorSelectedAnalysisModel` | `Runtime.EditorWorkspaceSnapshots.cppm` | feature-owned runtime contract |
| `SandboxEditorSelectedAnalysisCacheEntry` | `EditorSelectedAnalysisCacheEntry` | `Runtime.EditorWorkspaceSnapshots.cppm` | feature-owned runtime contract |
| `SandboxEditorVisualizationModelCacheEntry` | `EditorVisualizationModelCacheEntry` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorSelectedModelCacheStats` | `EditorSelectedModelCacheStats` | `Runtime.EditorWorkspaceSnapshots.cppm` | feature-owned runtime contract |
| `SandboxEditorSelectedModelCache` | `EditorSelectedModelCache` | `Runtime.EditorWorkspaceSnapshots.cppm` | feature-owned runtime contract |
| `SandboxEditorModelBuildRequest` | `EditorWorkspaceSnapshotRequest` | `Runtime.EditorWorkspaceSnapshots.cppm` | workspace snapshot request |
| `SandboxEditorModelBuildStats` | `EditorWorkspaceSnapshotStats` | `Runtime.EditorWorkspaceSnapshots.cppm` | workspace snapshot diagnostics |
| `SandboxEditorPanelFrame` | `SandboxEditorFrame` | `app/Sandbox.EditorShell.cppm` | app-owned composition copied from `EditorWorkspaceSnapshot` |
| `SandboxEditorParameterizationUvViewStatus` | `EditorParameterizationUvViewStatus` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorParameterizationUvViewRequest` | `EditorParameterizationUvViewRequest` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorParameterizationUvViewState` | `EditorParameterizationUvViewState` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorParameterizationUvViewCommandSurface` | `EditorParameterizationUvViewCommandSurface` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorContext` | `SandboxEditorContext` | `app/Sandbox.EditorShell.cppm` | app-owned composition of copied snapshots and feature-named command/query handles |
| `SandboxEditorTransformEditCommand` | `EditorTransformEditCommand` | `Runtime.SceneEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorCameraControllerCommand` | `EditorCameraControllerCommand` | `Runtime.SceneEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorPrimitiveViewCommand` | `EditorPrimitiveViewCommand` | `Runtime.SceneEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorRenderHintCommand` | `EditorRenderHintCommand` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorVisualizationConfigCommand` | `EditorVisualizationConfigCommand` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorVisualizationPropertyCommand` | `EditorVisualizationPropertyCommand` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorVisualizationRecipeCommand` | `EditorVisualizationRecipeCommand` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorVertexChannelBindingCommand` | `EditorVertexChannelBindingCommand` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorGeometryPresentationSlotDefaultCommand` | `EditorGeometryPresentationSlotDefaultCommand` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorGeometryPresentationSlotPropertyCommand` | `EditorGeometryPresentationSlotPropertyCommand` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorTextureBakeCommand` | `EditorTextureBakeCommand` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorTextureBakeCommandResult` | `EditorTextureBakeCommandResult` | `Runtime.VisualizationEditingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorUvRegenerationCommand` | `EditorUvRegenerationCommand` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorParameterizationStrategy` | `EditorParameterizationStrategy` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorParameterizationCommand` | `EditorParameterizationCommand` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorConfiguredParameterizationCommand` | `EditorConfiguredParameterizationCommand` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorParameterizationConfigStatus` | `EditorParameterizationConfigStatus` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorParameterizationConfigCommand` | `EditorParameterizationConfigCommand` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorParameterizationConfigResult` | `EditorParameterizationConfigResult` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorParameterizationViewModel` | `EditorParameterizationViewModel` | `Runtime.GeometryProcessingOperations.cppm` | feature-owned runtime contract |
| `SandboxEditorPreparedFrameView` | `EditorWorkspacePreparedFrame` | `Runtime.EditorWorkspaceSnapshots.cppm` | callback-scoped prepared snapshot |
| `SandboxEditorPreparedFrameVisitor` | `EditorWorkspacePreparedFrameVisitor` | `Runtime.EditorWorkspaceSnapshots.cppm` | workspace visitor |
| `SandboxEditorSession` | `EditorWorkspaceSession` | `Runtime.EditorWorkspaceSnapshots.cppm` | workspace attachment lifecycle |
| `EngineConfigSectionRegistry` | `Core::Config::EngineConfigSectionRegistry` | `runtime feature config modules` | shared Core alias; app aggregates registrations |
| `EngineConfigSectionRegistration` | `Core::Config::EngineConfigSectionRegistration` | `runtime feature config modules` | shared Core alias; feature factory returns it |
| `EngineConfigSectionChangedCallback` | `Core::Config::EngineConfigSectionChangedCallback` | `runtime feature config modules` | shared Core alias; app supplies callbacks |
| `ClusteringConfig` | `ClusteringConfig` | `Runtime.ClusteringConfig.cppm` | feature-owned config contract; app aggregates registration |
| `ProgressivePoissonPlaygroundChannel` | `ProgressivePoissonPlaygroundChannel` | `Runtime.ProgressivePoissonConfig.cppm` | feature-owned config contract; app aggregates registration |
| `ProgressivePoissonPlaygroundBackend` | `ProgressivePoissonPlaygroundBackend` | `Runtime.ProgressivePoissonConfig.cppm` | feature-owned config contract; app aggregates registration |
| `ProgressivePoissonPlaygroundConfig` | `ProgressivePoissonPlaygroundConfig` | `Runtime.ProgressivePoissonConfig.cppm` | feature-owned config contract; app aggregates registration |
| `ParameterizationStrategyKind` | `ParameterizationStrategyKind` | `Runtime.ParameterizationConfig.cppm` | feature-owned config contract; app aggregates registration |
| `ParameterizationBoundaryPolicy` | `ParameterizationBoundaryPolicy` | `Runtime.ParameterizationConfig.cppm` | feature-owned config contract; app aggregates registration |
| `ParameterizationBffBoundaryMode` | `ParameterizationBffBoundaryMode` | `Runtime.ParameterizationConfig.cppm` | feature-owned config contract; app aggregates registration |
| `ParameterizationUvRenderMode` | `ParameterizationUvRenderMode` | `Runtime.ParameterizationConfig.cppm` | feature-owned config contract; app aggregates registration |
| `ParameterizationUvBackgroundMode` | `ParameterizationUvBackgroundMode` | `Runtime.ParameterizationConfig.cppm` | feature-owned config contract; app aggregates registration |
| `ParameterizationViewConfig` | `ParameterizationViewConfig` | `Runtime.ParameterizationConfig.cppm` | feature-owned config contract; app aggregates registration |
| `ParameterizationUvConfig` | `ParameterizationUvConfig` | `Runtime.ParameterizationConfig.cppm` | feature-owned config contract; app aggregates registration |
| `ParameterizationLscmConfig` | `ParameterizationLscmConfig` | `Runtime.ParameterizationConfig.cppm` | feature-owned config contract; app aggregates registration |
| `ParameterizationHarmonicConfig` | `ParameterizationHarmonicConfig` | `Runtime.ParameterizationConfig.cppm` | feature-owned config contract; app aggregates registration |
| `ParameterizationBffConfig` | `ParameterizationBffConfig` | `Runtime.ParameterizationConfig.cppm` | feature-owned config contract; app aggregates registration |
| `ParameterizationConfig` | `ParameterizationConfig` | `Runtime.ParameterizationConfig.cppm` | feature-owned config contract; app aggregates registration |
