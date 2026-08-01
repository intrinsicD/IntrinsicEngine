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

    // Sandbox owns only copied view state and feature-named command
    // capabilities. Live ECS/assets/jobs/renderer bindings stay behind the
    // runtime command handles and are valid only during the prepared-frame
    // visitor that supplies this context.
    struct SandboxEditorContext final
    {
        SandboxEditorContext() = default;
        SandboxEditorContext(
            const Runtime::EditorWorkspacePreparedFrame& prepared,
            SandboxEditorFrame& frame)
            : SceneCommands(prepared.SceneCommands),
              GeometryCommands(prepared.GeometryCommands),
              VisualizationCommands(prepared.VisualizationCommands),
              RenderRecipeCommands(prepared.RenderRecipeCommands),
              SnapshotQueries(prepared.SnapshotQueries),
              AssetImportQueueCommands(prepared.AssetImportQueueCommands),
              DocumentCommands(prepared.DocumentCommands),
              MethodResultSinks(prepared.MethodResultSinks),
              GeometryResults(prepared.GeometryResults),
              RenderRecipeDraft(prepared.RenderRecipeDraft),
              SceneAvailable(prepared.SceneAvailable),
              GeometryConfigCommandsAvailable(
                  prepared.GeometryConfigCommandsAvailable),
              ClusteringAvailable(prepared.ClusteringAvailable),
              RenderRecipeCommandsAvailable(
                  prepared.RenderRecipeCommandsAvailable),
              RenderArtifactCommandsAvailable(
                  prepared.RenderArtifactCommandsAvailable),
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
