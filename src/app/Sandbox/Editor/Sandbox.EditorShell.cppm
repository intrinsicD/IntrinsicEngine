// Defines the Sandbox editor frame context and window shell so app-owned ImGui
// panels share drawing helpers and consume runtime-prepared views and commands.
module;

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

export module Extrinsic.Sandbox.Editor.Shell;

import Extrinsic.Runtime.EditorUiHost;
import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.TextureBakeModule;
import Extrinsic.Runtime.EditorWorkspaceSnapshots;
import Extrinsic.Runtime.GeometryProcessingOperations;
import Extrinsic.Runtime.RenderRecipeEditingOperations;
import Extrinsic.Runtime.SceneEditingOperations;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.VisualizationEditingOperations;
import Extrinsic.Runtime.WorldRegistry;

export namespace Extrinsic::Sandbox::Editor
{
    inline constexpr std::array<Runtime::GeometryPresentationSlotSemantic, 5>
        kTextureBakeTargetSemantics{{
            Runtime::GeometryPresentationSlotSemantic::Albedo,
            Runtime::GeometryPresentationSlotSemantic::Normal,
            Runtime::GeometryPresentationSlotSemantic::Roughness,
            Runtime::GeometryPresentationSlotSemantic::Metallic,
            Runtime::GeometryPresentationSlotSemantic::ScalarField,
        }};

    inline constexpr std::array<Runtime::PropertyTextureBakeEncoding, 8>
        kTextureBakeEncoders{{
            Runtime::PropertyTextureBakeEncoding::Auto,
            Runtime::PropertyTextureBakeEncoding::RgbaColor,
            Runtime::PropertyTextureBakeEncoding::Normal,
            Runtime::PropertyTextureBakeEncoding::ScalarColormap,
            Runtime::PropertyTextureBakeEncoding::LinearScalar,
            Runtime::PropertyTextureBakeEncoding::LabelPalette,
            Runtime::PropertyTextureBakeEncoding::Vector2,
            Runtime::PropertyTextureBakeEncoding::Vector3,
        }};

    inline constexpr std::array<Runtime::PropertyTextureBakeStorage, 3>
        kTextureBakeStorageModes{{
            Runtime::PropertyTextureBakeStorage::Auto,
            Runtime::PropertyTextureBakeStorage::RawFloat,
            Runtime::PropertyTextureBakeStorage::EncodedRgba,
        }};

    inline constexpr std::array<const char*, 3>
        kTextureBakeStorageNames{{
            "auto (raw except normals/labels)",
            "raw float texture",
            "encoded RGBA texture",
        }};

    inline constexpr std::array<const char*, 6> kColormapNames{{
        "Viridis", "Inferno", "Plasma", "Jet", "Coolwarm", "Heat"}};

    inline constexpr std::array<const char*, 2> kNormalSpaceNames{{
        "object space", "world space"}};

    // Borrows panel storage only for the current draw call.
    struct TextureBakeUiState
    {
        std::optional<Runtime::EditorUvRegenerationCommandResult>*
            LastUvRegenerationResult{nullptr};
        std::optional<Runtime::EditorUvRegenerationCommandResult>*
            LastUvExtentAdoption{nullptr};
        std::int32_t* SourceIndex{nullptr};
        std::int32_t* TargetSemanticIndex{nullptr};
        std::int32_t* EncoderIndex{nullptr};
        std::int32_t* StorageIndex{nullptr};
        std::int32_t* ColormapIndex{nullptr};
        std::int32_t* NormalSpaceIndex{nullptr};
        std::uint32_t* AdditionalConsumerMask{nullptr};
        std::int32_t* Width{nullptr};
        std::int32_t* Height{nullptr};
        std::int32_t* Padding{nullptr};
        std::int32_t* UvResolution{nullptr};
        std::int32_t* UvPadding{nullptr};
        float* UvTexelsPerUnit{nullptr};
        bool* UvForceRegenerate{nullptr};
        bool* UvPreserveAuthored{nullptr};
    };

    inline constexpr auto kUniformColorSource =
        static_cast<decltype(Runtime::EditorVisualizationConfigModel{}.Source)>(1);

    inline constexpr auto kScalarFieldSource =
        static_cast<decltype(Runtime::EditorVisualizationConfigModel{}.Source)>(2);

    void DrawDiagnostics(const std::vector<Runtime::EditorDiagnostic>& diagnostics);

    void DrawDomainWindowHeader(
        const Runtime::EditorDomainWindowModel& model);

    [[nodiscard]] bool DomainWindowReady(
        const Runtime::EditorDomainWindowModel& model) noexcept;

    void DrawVec3(const char* label, const glm::vec3 value);

    [[nodiscard]] const char* DebugNameForTextureBakeEncoder(
        const Runtime::PropertyTextureBakeEncoding encoder) noexcept;

    [[nodiscard]] std::span<const Runtime::EditorTextureBakeTarget>
    TextureBakeTargetsFor(
        const Runtime::EditorTextureBakeControlsModel& model,
        const std::string_view outputName);

    [[nodiscard]] Runtime::EditorVisualizationConfigCommand
    MakeVisualizationConfigCommandFromModel(
        const std::uint32_t stableEntityId,
        const Runtime::EditorVisualizationConfigModel& model,
        const Runtime::EditorVisualizationTarget target);

    [[nodiscard]] Runtime::EditorVisualizationConfigCommand
    MakeUniformVisualizationConfigCommandFromModel(
        const std::uint32_t stableEntityId,
        const Runtime::EditorVisualizationConfigModel& model,
        const Runtime::EditorVisualizationTarget target,
        const glm::vec4 color);

    [[nodiscard]] bool DrawDismissLastResultButton(const char* label);

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

    // Dismissal clears both the panel result and the session slot that
    // rebuilds it. Draw this control after all readers of the panel result.
    template <typename ResultT>
    void DrawDismissLastResultButton(
        const char* const label,
        std::optional<ResultT>& panelResult,
        const Runtime::EditorGeometryProcessingResultSlot slot,
        const SandboxEditorContext& context)
    {
        if (!DrawDismissLastResultButton(label))
            return;
        panelResult.reset();
        if (context.MethodResultSinks.DismissResult)
            context.MethodResultSinks.DismissResult(slot);
    }

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
