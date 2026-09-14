// Private editor bindings and helpers shared only by module implementation units.
// Include after the required runtime imports. C++ linkage keeps the shared
// types and helper functions independent of each including module's ownership.
#pragma once
#include "Editor/internal/Runtime.EditorGeometryHelpers.hpp"
#include "Editor/internal/Runtime.EditorFeatureCommands.Internal.hpp"
#include "Editor/internal/Runtime.EditorFeatureProperties.Internal.hpp"



extern "C++"
{
namespace Extrinsic::Runtime
{
    // Return-by-value declaration only; the parameterization family owns the
    // definition and the session that calls this imports it.
    struct EditorParameterizationUvViewCommandSurface;
    // Pointer/reference-only uses; Extrinsic.Runtime.EditorWorkspaceSnapshots
    // owns the complete definitions.
    struct EditorSelectedModelCache;
    struct EditorWorkspaceSnapshotContext;
}
namespace Extrinsic::Runtime::EditorFeatureDetail
{
    using namespace Extrinsic::Runtime;

    struct EditorFeatureBindings
    {
        std::optional<std::uint32_t> ModelEntityOverride{};
        ECS::Scene::Registry* Scene{nullptr};
        WorldHandle World{DefaultWorldHandle};
        SelectionController* Selection{nullptr};
        EditorCommandHistory* CommandHistory{nullptr};
        Assets::AssetService* AssetService{nullptr};
        const std::optional<PrimitiveSelectionResult>* LastRefinedPrimitive{nullptr};
        std::uint64_t LastRefinedPrimitiveGeneration{0u};
        CameraControllerRegistry* CameraControllers{nullptr};
        Core::Extent2D CameraViewport{};
        RHI::IDevice* Device{nullptr};
        TextureBakeService* TextureBake{nullptr};
        SpatialIndexCache* SpatialIndices{};
        EditorAssetImportCommandSurface AssetImportCommands{};
        EditorAssetImportQueueCommandSurface AssetImportQueueCommands{};
        EditorSceneFileCommandSurface SceneFileCommands{};
        EditorPrimitiveViewCommandSurface PrimitiveViewCommands{};
        EditorVisualizationRecipeCommandSurface VisualizationRecipes{};
        std::uint64_t VisualizationRecipeRevision{0u};
        EditorJobCommandSurface JobCommands{};
        RuntimeAssetImportQueueSnapshot AssetImportQueue{};
        std::string PendingAssetImportPath{};
        std::string PendingSceneFilePath{};
        Assets::AssetPayloadKind PendingAssetImportPayloadKind{Assets::AssetPayloadKind::Unknown};
        const EditorFileImportResult* LastAssetImportResult{nullptr};
        const EditorSceneFileResult* LastSceneFileResult{nullptr};
        const Graphics::RenderGraphFrameStats* RenderGraphStats{nullptr};
        const Graphics::RenderRecipeConfigContext* RenderRecipeContext{nullptr};
        EditorRenderRecipeEditorState* RenderRecipeEditorState{nullptr};
        const RuntimeRenderRecipeState* RenderRecipeRuntimeState{nullptr};
        const RuntimeEngineConfigControlState* EngineConfigControlState{nullptr};
        EditorWorkspaceSnapshotStats* ModelBuildStats{nullptr};
        EditorSelectedModelCache* SelectedModelCache{nullptr};
        std::function<bool()> AttachmentActive{};
        std::function<void()> InvalidateWorkspaceSnapshotCache{};
        std::function<Graphics::RenderRecipeConfigLoadResult(const std::string&,
                                                             const std::string&)>
            PreviewRenderRecipeDocument{};
        std::function<RuntimeRenderRecipeApplyResult(const Graphics::RenderRecipeConfigLoadResult&)>
            ApplyRenderRecipePreview{};
        std::function<Core::Config::EngineConfigLoadResult(const std::string&, const std::string&)>
            PreviewEngineConfigDocument{};
        std::function<RuntimeEngineConfigApplyResult(const Core::Config::EngineConfigLoadResult&)>
            ApplyEngineConfigHotSubset{};
        RenderArtifactRegistry* RenderArtifacts{nullptr};
        bool ImGuiAdapterAvailable{false};
        bool AssetImportCommandsAvailable{false};
        bool SceneFileCommandsAvailable{false};
        bool CameraRenderCommandsAvailable{false};
        bool VisualizationCommandsAvailable{false};
        bool RenderRecipeCommandsAvailable{false};
        bool EngineConfigCommandsAvailable{false};
        bool MeshDenoiseKernelAvailable{true};
        bool MeshCurvatureKernelAvailable{true};
        bool MeshCurvatureDirectionsAvailable{true};
        bool CurvatureSegmentationKernelAvailable{true};
        bool MeshRemeshUniformKernelAvailable{true};
        bool MeshRemeshAdaptiveKernelAvailable{true};
        bool MeshRemeshProjectToSurfaceAvailable{true};
        bool MeshRemeshErrorBoundedSizingAvailable{true};
        bool MeshSubdivideLoopKernelAvailable{true};
        bool MeshSubdivideCatmullClarkKernelAvailable{true};
        bool MeshSubdivideSqrt3KernelAvailable{true};
        bool MeshSubdivideLoopFeatureEdgesAvailable{true};
        bool MeshSimplifyKernelAvailable{true};
    };

    [[nodiscard]] EditorFeatureBindings
    ToEditorFeatureBindingsImpl(const EditorWorkspaceSnapshotContext& context);
    [[nodiscard]] EditorSceneEditingContext
    MakeEditorSceneEditingContext(const EditorFeatureBindings& bindings);
    [[nodiscard]] EditorProcessingContext
    MakeEditorProcessingContext(const EditorFeatureBindings& bindings);
    [[nodiscard]] EditorVisualizationEditingContext
    MakeEditorVisualizationEditingContext(const EditorFeatureBindings& bindings);
    [[nodiscard]] EditorRenderRecipeEditingContext
    MakeEditorRenderRecipeEditingContext(const EditorFeatureBindings& bindings);
    [[nodiscard]] EditorFeatureBindings MakeEditorFeatureBindings(
        WorldRegistry& worlds,
        ServiceRegistry& services);
    // The UV view surface is owned by the session, not by the bindings, so that
    // one guarded instance can be borrowed by the parameterization frame.
    [[nodiscard]] EditorParameterizationUvViewCommandSurface
    MakeEditorParameterizationUvViewCommandSurface(ServiceRegistry& services);
    [[nodiscard]] EditorFileImportResult ProjectEditorFileImportResult(
        const RuntimeAssetImportEvent& event);
    [[nodiscard]] EditorSceneFileResult ProjectEditorSceneFileResult(
        const RuntimeSceneFileEvent& event);

} // namespace Extrinsic::Runtime::EditorFeatureDetail

} // extern "C++"
