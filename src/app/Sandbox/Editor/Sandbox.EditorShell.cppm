// Defines the Sandbox editor frame context and window shell so app-owned ImGui
// panels consume runtime-prepared views and commands.
module;

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

export module Extrinsic.Sandbox.Editor.Shell;

import Extrinsic.Runtime.EditorUiHost;
import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.EditorWorkspaceSnapshots;
import Extrinsic.Runtime.GeometryProcessingOperations;
import Extrinsic.Runtime.RenderRecipeEditingOperations;
import Extrinsic.Runtime.SceneEditingOperations;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.VisualizationEditingOperations;
import Extrinsic.Runtime.WorldRegistry;

export namespace Extrinsic::Sandbox::Editor
{
    void DrawDisabledReasonTooltip(std::string_view disabledReason);

    struct SandboxEditorFrame final : Runtime::EditorWorkspaceSnapshot
    {
        SandboxEditorFrame() = default;
        explicit SandboxEditorFrame(const Runtime::EditorWorkspaceSnapshot& frame)
            : Runtime::EditorWorkspaceSnapshot(frame)
        {
        }
    };

    // View data is copied, but command handles borrow live runtime services and
    // remain valid only inside the prepared-frame visitor that supplies them.
    struct SandboxEditorContext final
    {
        SandboxEditorContext() = default;
        SandboxEditorContext(
            const Runtime::EditorWorkspaceSnapshotPreparedFrame& workspace,
            const Runtime::EditorSceneEditingPreparedFrame& scene,
            const Runtime::EditorGeometryProcessingPreparedFrame& geometry,
            const Runtime::EditorVisualizationEditingPreparedFrame& visualization,
            const Runtime::EditorRenderRecipeEditingPreparedFrame& renderRecipe,
            SandboxEditorFrame& frame)
            : SceneCommands(scene.Commands),
              GeometryCommands(geometry.Commands),
              VisualizationCommands(visualization.Commands),
              RenderRecipeCommands(renderRecipe.Commands),
              SnapshotQueries(workspace.SnapshotQueries),
              AssetImportQueueCommands(scene.AssetImportQueueCommands),
              DocumentCommands(scene.DocumentCommands),
              MethodResultSinks(geometry.ResultSinks),
              GeometryResults(geometry.Results),
              RenderRecipeDraft(renderRecipe.Draft),
              SceneAvailable(scene.SceneAvailable),
              GeometryConfigCommandsAvailable(
                  geometry.ConfigCommandsAvailable),
              ClusteringAvailable(geometry.ClusteringAvailable),
              PointCloudConsolidationAvailable(
                  geometry.PointCloudConsolidationAvailable),
              RenderRecipeCommandsAvailable(
                  renderRecipe.CommandsAvailable),
              RenderArtifactCommandsAvailable(
                  renderRecipe.ArtifactCommandsAvailable),
              Selection(&frame.Selection),
              Document(&frame.Document),
              ModelBuildStats(&frame.ModelBuildStats)
        {
        }

        Runtime::EditorSceneEditingCommands SceneCommands{};
        Runtime::EditorGeometryProcessingCommands GeometryCommands{};
        Runtime::EditorVisualizationEditingCommands VisualizationCommands{};
        Runtime::EditorRenderRecipeEditingCommands RenderRecipeCommands{};
        Runtime::EditorWorkspaceSnapshotQueries SnapshotQueries{};
        Runtime::EditorAssetImportQueueCommandSurface
            AssetImportQueueCommands{};
        Runtime::EditorDocumentCommandSurface DocumentCommands{};
        Runtime::EditorMethodResultSinks MethodResultSinks{};
        Runtime::EditorGeometryProcessingResultsSnapshot GeometryResults{};
        Runtime::EditorRenderRecipeDraftSnapshot RenderRecipeDraft{};
        bool SceneAvailable{false};
        bool GeometryConfigCommandsAvailable{false};
        bool ClusteringAvailable{false};
        bool PointCloudConsolidationAvailable{false};
        bool RenderRecipeCommandsAvailable{false};
        bool RenderArtifactCommandsAvailable{false};
        const Runtime::EditorSelectionModel* Selection{nullptr};
        const Runtime::EditorDocumentModel* Document{nullptr};
        Runtime::EditorWorkspaceSnapshotStats* ModelBuildStats{nullptr};
    };

    struct EditorWindowDescriptor
    {
        std::string Id{};
        std::vector<std::string> MenuPath{};
        std::string Title{};
        bool OpenByDefault{false};
        std::function<void(
            bool&,
            const SandboxEditorContext&)> Draw{};
        std::function<void(bool)> OpenStateChanged{};
    };

    class EditorShell final
    {
    public:
        EditorShell();
        ~EditorShell();

        EditorShell(const EditorShell&) = delete;
        EditorShell& operator=(const EditorShell&) = delete;
        EditorShell(EditorShell&&) = delete;
        EditorShell& operator=(EditorShell&&) = delete;

        void Attach(Runtime::WorldRegistry& worlds, Runtime::ServiceRegistry& services);
        void Detach();

        [[nodiscard]] Runtime::EditorWindowHandle RegisterEditorWindow(
            EditorWindowDescriptor descriptor);
        [[nodiscard]] bool UnregisterEditorWindow(
            Runtime::EditorWindowHandle handle);
        [[nodiscard]] Runtime::EditorUiVisibilityCommandResult
        ApplyEditorUiVisibilityCommand(
            Runtime::EditorUiVisibilityCommand command) noexcept;
        [[nodiscard]] bool IsEditorVisible() const noexcept;
        [[nodiscard]] std::vector<Runtime::EditorWindowMenuEntry>
        BuildEditorWindowMenuModel() const;
        [[nodiscard]] bool SetEditorWindowOpen(
            std::string_view id,
            bool open);
        [[nodiscard]] bool IsAttached() const noexcept;
        [[nodiscard]] const SandboxEditorFrame&
        GetLastFrame() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
