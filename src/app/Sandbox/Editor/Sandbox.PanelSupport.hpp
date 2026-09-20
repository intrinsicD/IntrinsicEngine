// Shared app-private panel context, controls and action/view models.
// Include after module imports; standard and GLM headers belong in the caller's
// global module fragment. C++ linkage lets all panel implementations share it.
#pragma once


extern "C++"
{
namespace Extrinsic::Runtime
{
    struct EditorPointCloudServicePreparedFrame;
}

namespace Extrinsic::Sandbox::Editor
{
    inline constexpr std::array<const char*, 6> kColormapNames{{
        "Viridis", "Inferno", "Plasma", "Jet", "Coolwarm", "Heat"}};

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

    void DrawBoundRenderStateRows(const Runtime::EditorBoundRenderStateModel& bound);

    void DrawDiagnostics(const std::vector<Runtime::EditorDiagnostic>& diagnostics);

    void DrawDomainWindowHeader(
        const Runtime::EditorDomainWindowModel& model);

    [[nodiscard]] bool DomainWindowReady(
        const Runtime::EditorDomainWindowModel& model) noexcept;

    void DrawVec3(const char* label, const glm::vec3 value);

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
    [[nodiscard]] bool DrawProcessingActionButton(
        const char* label, const Runtime::ActionReadiness& readiness);

    extern "C++"
    {
        struct SandboxEditorFrame final : Runtime::EditorWorkspaceSnapshot
        {
            SandboxEditorFrame() = default;
            explicit SandboxEditorFrame(const Runtime::EditorWorkspaceSnapshot& frame);
        };

        // Most view data is copied; the point-cloud service frame and live command
        // handles are borrowed only for the prepared-frame draw visit.
        struct SandboxEditorContext final
        {
            SandboxEditorContext() = default;
            SandboxEditorContext(
                const Runtime::EditorWorkspaceSnapshotPreparedFrame& workspace,
                const Runtime::EditorSceneEditingPreparedFrame& scene,
                const Runtime::EditorProcessingCommands& processing,
                const Runtime::EditorPointFieldPreparedFrame& pointFields,
                const Runtime::EditorPointAnalysisPreparedFrame& pointAnalysis,
                const Runtime::EditorPointSetPreparedFrame& pointSet,
                const Runtime::EditorPointConstructionPreparedFrame& pointConstruction,
                Runtime::EditorPointCloudServicePreparedFrame& pointCloudService,
                const Runtime::EditorNormalPreparedFrame& normals,
                const Runtime::EditorRegistrationPreparedFrame& registration,
                const Runtime::EditorMeshFieldPreparedFrame& meshFields,
                const Runtime::EditorMeshTopologyPreparedFrame& meshTopology,
                const Runtime::EditorParameterizationPreparedFrame& parameterization,
                const Runtime::EditorVisualizationEditingPreparedFrame& visualization,
                const Runtime::EditorRenderRecipeEditingPreparedFrame& renderRecipe,
                SandboxEditorFrame& frame);

            Runtime::EditorSceneEditingCommands SceneCommands{};
            // Shared processing handle for surfaces every family reuses, such as
            // the common point-input catalog, discovery and primitive selection.
            Runtime::EditorProcessingCommands Processing{};
            Runtime::EditorPointFieldPreparedFrame PointFields{};
            Runtime::EditorPointAnalysisPreparedFrame PointAnalysis{};
            Runtime::EditorPointSetPreparedFrame PointSet{};
            Runtime::EditorPointConstructionPreparedFrame PointConstruction{};
            // Borrows shell-owned frame storage for the current draw visit.
            const Runtime::EditorPointCloudServicePreparedFrame* PointCloudService{nullptr};
            Runtime::EditorNormalPreparedFrame Normals{};
            Runtime::EditorRegistrationPreparedFrame Registration{};
            Runtime::EditorMeshFieldPreparedFrame MeshFields{};
            Runtime::EditorMeshTopologyPreparedFrame MeshTopology{};
            Runtime::EditorParameterizationPreparedFrame Parameterization{};
            Runtime::EditorVisualizationEditingCommands VisualizationCommands{};
            Runtime::EditorRenderRecipeEditingCommands RenderRecipeCommands{};
            Runtime::EditorWorkspaceSnapshotQueries SnapshotQueries{};
            Runtime::EditorAssetImportQueueCommandSurface
                AssetImportQueueCommands{};
            Runtime::EditorDocumentCommandSurface DocumentCommands{};
            Runtime::EditorRenderRecipeDraftSnapshot RenderRecipeDraft{};
            bool SceneAvailable{false};
            bool ProcessingConfigCommandsAvailable{false};
            bool RenderRecipeCommandsAvailable{false};
            bool RenderArtifactCommandsAvailable{false};
            const Runtime::EditorSelectionModel* Selection{nullptr};
            const Runtime::EditorDocumentModel* Document{nullptr};
            Runtime::EditorWorkspaceSnapshotStats* ModelBuildStats{nullptr};
        };

    }

    // Each drawing surface retains its own persistent rename draft and diagnostic.
    struct TextureBakeMutationUiState
    {
        std::string RenameTarget{};
        std::array<char, 128> RenameBuffer{};
        std::string MutationDiagnostic{};
    };

    void DrawTextureBakeControls(
        const Runtime::EditorTextureBakeControlsModel& model,
        const SandboxEditorContext* context,
        TextureBakeUiState* state,
        TextureBakeMutationUiState& mutation);

    void DrawUniformVisualizationColorEdit(
        const Runtime::EditorVisualizationConfigModel& visualization,
        const SandboxEditorContext& context,
        std::uint32_t selectedStableId,
        Runtime::EditorVisualizationTarget target,
        bool canEditVisualization);

    // Both scalar blocks require the caller's scalar-source visibility check.
    void DrawScalarFieldColorControls(
        const Runtime::EditorVisualizationConfigModel& visualization,
        const SandboxEditorContext& context,
        std::uint32_t selectedStableId,
        Runtime::EditorVisualizationTarget target,
        bool canEditVisualization);

    void DrawScalarFieldBinAndIsolineControls(
        const Runtime::EditorVisualizationConfigModel& visualization,
        const SandboxEditorContext& context,
        std::uint32_t selectedStableId,
        Runtime::EditorVisualizationTarget target,
        bool canEditVisualization);

    // Storage the shared UV-regeneration block reads and writes. Every pointer
    // must be bound; callers pass either their panel-lifetime members or the
    // same frame-local fallbacks the rest of their bake panel uses, so an
    // adopted atlas extent lands where the bake request reads it.
    struct SandboxUvRegenerationControls
    {
        std::optional<Runtime::EditorUvRegenerationCommandResult>*
            LastResult{nullptr};
        std::optional<Runtime::EditorUvRegenerationCommandResult>*
            LastExtentAdoption{nullptr};
        std::int32_t* BakeWidth{nullptr};
        std::int32_t* BakeHeight{nullptr};
        std::int32_t* BakePadding{nullptr};
        std::int32_t* UvResolution{nullptr};
        std::int32_t* UvPadding{nullptr};
        float* UvTexelsPerUnit{nullptr};
        bool* UvForceRegenerate{nullptr};
        bool* UvPreserveAuthored{nullptr};
    };

    // The one "Regenerate UVs" control: atlas parameters, submission with its
    // terminal callback, atlas-extent adoption, status and dismissal. Every
    // texture-bake panel drives this helper, so the command, the session sink
    // that carries a queued job's terminal result, and the dismissal that
    // clears it cannot drift apart between panels.
    void DrawSandboxUvRegenerationControls(
        const Runtime::EditorTextureBakeControlsModel& model,
        const SandboxEditorContext* context,
        const SandboxUvRegenerationControls& controls);

    template <typename Config, typename Result>
    struct ProcessingDraftState
    {
        std::optional<std::vector<std::uint32_t>> LastSelectedEntity{};
        std::optional<Result> LastResult{};
        Config Draft{};
        std::string LastApplied{}, ConfigDiagnostic{}, VisualizationDiagnostic{};

        bool Synchronize(const Config& active, const std::string& serialized)
        {
            if (serialized == LastApplied) return false;
            Draft = active;
            LastApplied = serialized;
            ConfigDiagnostic.clear();
            return true;
        }
    };

    // The method catalog performs canonical preflight; it is queried only while
    // the combo is open. Unlike the fixed-domain selector, this may change domain.
    bool DrawProcessingPointInput(const char* label,
        const std::function<Runtime::GeometryPropertyCatalogSnapshot()>& getCatalog,
        Runtime::GeometryPropertyRef& property,
        std::optional<Runtime::GeometryElementDomain> domain = std::nullopt);

    bool DrawProcessingPropertyName(const char* label, std::string& name);
    struct ProcessingEntityInput
    {
        std::uint32_t Entity{};
        std::optional<std::vector<std::uint32_t>> PreviousSelection{};
    };
    [[nodiscard]] Runtime::EditorWorkspaceSnapshot BuildProcessingInputWorkspace(
        const SandboxEditorContext& context);
    bool SynchronizeProcessingEntity(const Runtime::EditorSelectionModel& selection,
        std::optional<std::vector<std::uint32_t>>& previousSelection, std::uint32_t& entity,
        std::size_t slot = 0u);
    bool DrawProcessingEntity(const char* label, const SandboxEditorContext& context,
        std::uint32_t& entity, std::optional<std::vector<std::uint32_t>>& previousSelection,
        std::optional<Runtime::EditorDomainWindowKind> domain = std::nullopt,
        std::size_t slot = 0u);
    void DrawProcessingCpuBackend();

    bool DrawProcessingPropertyInput(const char* label,
        const Runtime::EditorPropertyCatalogModel& catalog, Runtime::GeometryPropertyRef& property,
        bool (*accepts)(const Runtime::GeometryPropertyRef&) = nullptr,
        std::uint32_t maxComponents = 4u);
    [[nodiscard]] Runtime::EditorCommandStatus ShowProcessingProperty(
        const SandboxEditorContext& context, std::uint32_t entity,
        const Runtime::GeometryPropertyRef& property);
    bool DrawProcessingPropertyShowButton(const SandboxEditorContext& context,
        std::uint32_t entity, const Runtime::GeometryPropertyRef& property,
        std::string& diagnostic);

    // Dismissal clears both the panel result and the session slot that
    // rebuilds it. Draw this control after all readers of the panel result.
    template <typename ResultT, typename Slot, typename Dismiss>
    void DrawDismissLastResultButton(
        const char* const label,
        std::optional<ResultT>& panelResult,
        const Slot slot,
        const Dismiss& dismiss)
    {
        if (!DrawDismissLastResultButton(label))
            return;
        panelResult.reset();
        if (dismiss) dismiss(slot);
    }

    struct SandboxParameterizationStrategyOption
    {
        Runtime::EditorParameterizationStrategy Strategy{
            Runtime::EditorParameterizationStrategy::Lscm};
        std::string_view Label{};
        std::string_view StableToken{};
    };

    [[nodiscard]] std::array<SandboxParameterizationStrategyOption, 4u>
    SandboxParameterizationStrategyOptions() noexcept;

    struct SandboxParameterizationPanelActionResult
    {
        Runtime::RuntimeEngineConfigApplyResult Config{};
        std::optional<Runtime::EditorParameterizationResult> Execution{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Config.Succeeded() && Execution.has_value() &&
                   Execution->Succeeded();
        }
    };

    [[nodiscard]] SandboxParameterizationPanelActionResult
    ApplySandboxParameterizationPanelAction(
        const SandboxEditorContext& context,
        std::uint32_t stableEntityId,
        const Runtime::ParameterizationConfig& config);

    struct SandboxParameterizationUvPane
    {
        glm::vec2 Min{0.0f};
        glm::vec2 Max{0.0f};
        float Padding{20.0f};
        float Zoom{1.0f};
        glm::vec2 Pan{0.0f};
        bool IncludeUnitSquare{false};
    };

    struct SandboxParameterizationUvProjection
    {
        bool Valid{false};
        bool FitsPane{false};
        glm::vec2 PaneCenter{0.0f};
        glm::vec2 UvCenter{0.0f};
        float Scale{1.0f};
        float Zoom{1.0f};
        glm::vec2 Pan{0.0f};
        std::vector<glm::vec2> Vertices{};
        std::vector<std::array<std::uint32_t, 3u>> Triangles{};
        std::string Message{};
    };

    [[nodiscard]] SandboxParameterizationUvProjection
    BuildSandboxParameterizationUvProjection(
        const Runtime::EditorParameterizationViewModel& model,
        const SandboxParameterizationUvPane& pane);

    [[nodiscard]] glm::vec2 ProjectSandboxParameterizationUvPoint(
        const SandboxParameterizationUvProjection& projection,
        glm::vec2 uv) noexcept;

    struct SandboxParameterizationResultSummary
    {
        bool Succeeded{false};
        bool HasDiagnostics{false};
        std::string StrategyToken{};
        std::string CommandStatus{};
        std::string SolverStatus{};
        std::string Message{};
        std::size_t EvaluatedFaceCount{0u};
        std::size_t SkippedFaceCount{0u};
        std::size_t FlippedElementCount{0u};
        std::size_t BoundaryEdgeCount{0u};
        double MeanConformalDistortion{0.0};
        double MeanAreaDistortion{0.0};
        double MeanStretch{0.0};
    };

    [[nodiscard]] SandboxParameterizationResultSummary
    BuildSandboxParameterizationResultSummary(
        const Runtime::EditorParameterizationResult& result);

    [[nodiscard]] bool IsFiniteVec2(glm::vec2 value) noexcept;
}

} // extern "C++"
