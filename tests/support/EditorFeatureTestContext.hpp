// Shared runtime editor test context and canonical geometry fixture builders.
#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>
#include <glm/glm.hpp>
#include <functional>
#include <optional>
#include <string>

import Extrinsic.Asset.ImportRouter;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Geometry2D;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Components.GeometrySources;
import Geometry.Properties;
import Extrinsic.Graphics.RenderRecipeConfig;
import Extrinsic.Graphics.RenderDiagnostics;
import Extrinsic.RHI.Device;
import Extrinsic.Runtime.AssetIngestStateMachine;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.EditorProcessing;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.EditorWorkspaceSnapshots;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.PrimitiveSelectionRefinement;
import Extrinsic.Runtime.RenderArtifactPublication;
import Extrinsic.Runtime.RenderRecipeActivation;
import Extrinsic.Runtime.RenderRecipeEditingOperations;
import Extrinsic.Runtime.SceneEditingOperations;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.TextureBakeModule;
import Extrinsic.Runtime.VisualizationEditingOperations;
import Extrinsic.Runtime.WorldHandle;

namespace Intrinsic::Tests
{
    namespace Assets = Extrinsic::Assets;
    namespace Core = Extrinsic::Core;
    namespace ECS = Extrinsic::ECS;
    namespace Graphics = Extrinsic::Graphics;
    namespace RHI = Extrinsic::RHI;
    namespace Runtime = Extrinsic::Runtime;

    // Explicit low-level test seam for feature operations. Production app
    // code receives bound command handles and copied snapshots instead of
    // assembling these live runtime dependencies.
    struct EditorFeatureTestContext
    {
        ECS::Scene::Registry* Scene{nullptr};
        Runtime::WorldHandle World{Runtime::DefaultWorldHandle};
        Runtime::SelectionController* Selection{nullptr};
        Runtime::EditorCommandHistory* CommandHistory{nullptr};
        const std::optional<Runtime::PrimitiveSelectionResult>* LastRefinedPrimitive{nullptr};
        std::uint64_t LastRefinedPrimitiveGeneration{0u};
        Runtime::CameraControllerRegistry* CameraControllers{nullptr};
        Core::Extent2D CameraViewport{};
        RHI::IDevice* Device{nullptr};
        Runtime::TextureBakeService* TextureBake{nullptr};
        Runtime::EditorAssetImportCommandSurface AssetImportCommands{};
        Runtime::EditorAssetImportQueueCommandSurface AssetImportQueueCommands{};
        Runtime::EditorSceneFileCommandSurface SceneFileCommands{};
        Runtime::EditorPrimitiveViewCommandSurface PrimitiveViewCommands{};
        Runtime::EditorVisualizationRecipeCommandSurface VisualizationRecipes{};
        std::uint64_t VisualizationRecipeRevision{0u};
        Runtime::EditorJobCommandSurface JobCommands{};
        Runtime::RuntimeAssetImportQueueSnapshot AssetImportQueue{};
        std::string PendingAssetImportPath{};
        std::string PendingSceneFilePath{};
        Assets::AssetPayloadKind PendingAssetImportPayloadKind{Assets::AssetPayloadKind::Unknown};
        const Runtime::EditorFileImportResult* LastAssetImportResult{nullptr};
        const Runtime::EditorSceneFileResult* LastSceneFileResult{nullptr};
        const Graphics::RenderGraphFrameStats* RenderGraphStats{nullptr};
        const Graphics::RenderRecipeConfigContext* RenderRecipeContext{nullptr};
        Runtime::EditorRenderRecipeEditorState* RenderRecipeEditorState{nullptr};
        const Runtime::RuntimeRenderRecipeState* RenderRecipeRuntimeState{nullptr};
        const Runtime::RuntimeEngineConfigControlState* EngineConfigControlState{nullptr};
        Runtime::EditorWorkspaceSnapshotStats* ModelBuildStats{nullptr};
        Runtime::EditorSelectedModelCache* SelectedModelCache{nullptr};
        std::function<bool()> AttachmentActive{};
        std::function<Graphics::RenderRecipeConfigLoadResult(const std::string&,
                                                             const std::string&)>
            PreviewRenderRecipeDocument{};
        std::function<Runtime::RuntimeRenderRecipeApplyResult(
            const Graphics::RenderRecipeConfigLoadResult&)>
            ApplyRenderRecipePreview{};
        std::function<Core::Config::EngineConfigLoadResult(const std::string&, const std::string&)>
            PreviewEngineConfigDocument{};
        std::function<Runtime::RuntimeEngineConfigApplyResult(
            const Core::Config::EngineConfigLoadResult&)>
            ApplyEngineConfigHotSubset{};
        Runtime::RenderArtifactRegistry* RenderArtifacts{nullptr};
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

        [[nodiscard]] std::function<void()> MakeWorkspaceSnapshotCacheInvalidator() const;

        [[nodiscard]] operator Runtime::EditorSceneEditingContext() const;

        [[nodiscard]] operator Runtime::EditorProcessingContext() const;
        [[nodiscard]] operator Runtime::EditorProcessingCommands() const;
        [[nodiscard]] operator Runtime::EditorVisualizationEditingContext() const;

        [[nodiscard]] operator Runtime::EditorRenderRecipeEditingContext() const;

        [[nodiscard]] operator Runtime::EditorWorkspaceSnapshotContext() const;
    };

    [[nodiscard]] EditorFeatureTestContext MakeContext(
        Extrinsic::ECS::Scene::Registry& registry,
        Runtime::SelectionController& selection,
        bool imguiAvailable = true,
        const std::optional<Runtime::PrimitiveSelectionResult>* lastPrimitive = nullptr,
        Extrinsic::RHI::IDevice* device = nullptr);

    [[nodiscard]] Runtime::GeometryPresentationRecipe MakeGeometryPresentationRecipe();
    [[nodiscard]] Runtime::GeometryPresentationRuntimeState MakeGeometryPresentationRuntimeState();
    void AttachGeometryPresentation(Extrinsic::ECS::Scene::Registry& registry,
                                    Extrinsic::ECS::EntityHandle entity);
} // namespace Intrinsic::Tests

namespace Intrinsic::Tests::EditorGeometry
{
    namespace ECS = Extrinsic::ECS;
    namespace GS = ECS::Components::GeometrySources;
    namespace PN = GS::PropertyNames;
    inline constexpr std::uint32_t kInvalidIndex =
        std::numeric_limits<std::uint32_t>::max();

    [[nodiscard]] ECS::EntityHandle MakeSelectable(
        ECS::Scene::Registry& registry,
        std::string name);

    void AddPointCloudSource(ECS::Scene::Registry& registry,
                             ECS::EntityHandle entity,
                             std::size_t pointCount);

    void SetPositions(GS::Vertices& vertices,
                      const std::vector<glm::vec3>& positions);

    void SetTexcoords(GS::Vertices& vertices,
                      const std::vector<glm::vec2>& texcoords);

    void SetEdges(GS::Edges& edges,
                  const std::vector<std::uint32_t>& v0,
                  const std::vector<std::uint32_t>& v1);

    void SetHalfedges(GS::Halfedges& halfedges,
                      const std::vector<std::uint32_t>& toVertex,
                      const std::vector<std::uint32_t>& next,
                      const std::vector<std::uint32_t>& face);

    void SetFaces(GS::Faces& faces,
                  const std::vector<std::uint32_t>& faceHalfedge);

    void AddGraphSource(ECS::Scene::Registry& registry, ECS::EntityHandle entity);

    void AddIcosahedronMeshSource(ECS::Scene::Registry& registry, ECS::EntityHandle entity);

    void AddTriangleMeshSource(ECS::Scene::Registry& registry,
                               const ECS::EntityHandle entity);
}
