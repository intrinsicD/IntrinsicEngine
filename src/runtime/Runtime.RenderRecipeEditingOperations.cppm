module;

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

export module Extrinsic.Runtime.RenderRecipeEditingOperations;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Graphics.RenderRecipeConfig;
import Extrinsic.Graphics.RenderingContract;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.RenderArtifactPublication;
import Extrinsic.Runtime.RenderRecipeActivation;

export namespace Extrinsic::Runtime
{
    using EditorRecipeSlotKind = Graphics::RecipeSlotKind;
    using EditorRenderRecipeConfigState = Graphics::RenderRecipeConfigState;
    using EditorRenderRecipeConfigDiagnosticCode = Graphics::RenderRecipeConfigDiagnosticCode;

    [[nodiscard]] std::string_view
    DebugNameForEditorRenderRecipeConfigState(EditorRenderRecipeConfigState state) noexcept;

    [[nodiscard]] std::string_view DebugNameForEditorRenderRecipeConfigDiagnosticCode(
        EditorRenderRecipeConfigDiagnosticCode code) noexcept;
    struct EditorRenderGraphPassModel
    {
        std::string Name{};
        bool HasTypedId{false};
        std::uint32_t TypedId{0u};
        std::string Status{};
    };

    struct EditorGpuProfileQueueModel
    {
        std::string Queue{};
        std::string Source{};
        std::optional<std::uint64_t> DurationNs{};
    };

    struct EditorGpuProfilePassModel
    {
        std::string Name{};
        bool HasTypedId{false};
        std::uint32_t TypedId{0u};
        std::string Queue{};
        std::string CommandStatus{};
        std::string Source{};
        std::optional<std::uint64_t> DurationNs{};
    };

    struct EditorGpuProfileModel
    {
        std::string Status{};
        std::string Source{};
        std::string Diagnostic{};
        bool Fresh{false};
        bool Stale{false};
        bool HasResolvedFrame{false};
        std::uint64_t ResolvedSubmittedFrameNumber{0u};
        std::uint32_t ResolvedFrameSlot{0u};
        std::uint64_t SampleAgeFrames{0u};
        std::vector<EditorGpuProfileQueueModel> QueueEnvelopes{};
        std::vector<EditorGpuProfilePassModel> Passes{};
    };

    enum class EditorGpuProfilingConfigStatus : std::uint8_t
    {
        None = 0,
        Applied,
        NoChange,
        MissingConfigControl,
        PreviewRejected,
        ApplyRejected,
    };

    struct EditorGpuProfilingConfigResult
    {
        EditorGpuProfilingConfigStatus Status{EditorGpuProfilingConfigStatus::None};
        Core::Config::EngineConfigLoadResult Preview{};
        RuntimeEngineConfigApplyResult Apply{};
        std::string Message{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == EditorGpuProfilingConfigStatus::Applied ||
                   Status == EditorGpuProfilingConfigStatus::NoChange;
        }
    };

    struct EditorRenderGraphModel
    {
        bool Enabled{false};
        bool GpuProfilingEnabled{false};
        bool GpuProfilingToggleAvailable{false};
        std::string GpuProfilingToggleDisabledReason{};
        std::string GpuProfilingControlStatusText{};
        std::vector<std::string> GpuProfilingControlDiagnostics{};
        bool CompileSucceeded{false};
        bool ExecuteSucceeded{false};
        bool DeviceOperational{false};
        std::uint32_t PassCount{0u};
        std::uint32_t CulledPassCount{0u};
        std::uint32_t ResourceCount{0u};
        std::uint32_t BarrierCount{0u};
        std::uint32_t QueueHandoffEdgeCount{0u};
        std::uint32_t CrossQueueTimelineEdgeCount{0u};
        std::uint32_t CrossQueueTimelineSignalCount{0u};
        std::uint32_t CrossQueueTimelineWaitCount{0u};
        std::uint32_t CrossQueueOwnershipTransferCount{0u};
        std::uint64_t TransientMemoryEstimateBytes{0u};
        std::uint64_t CompileTimeMicros{0u};
        std::uint64_t ExecuteTimeMicros{0u};
        std::uint32_t CommandPassesRecorded{0u};
        std::uint32_t CommandPassesSkipped{0u};
        std::uint32_t CommandPassesSkippedNonOperational{0u};
        std::uint32_t CommandPassesSkippedUnavailable{0u};
        std::uint32_t AsyncComputeUtilizedFrames{0u};
        std::string StatusText{};
        std::string Diagnostic{};
        std::string LifecycleDiagnostic{};
        std::string DebugDump{};
        EditorGpuProfileModel GpuProfile{};
        std::vector<EditorRenderGraphPassModel> CommandPasses{};
        std::vector<EditorDiagnostic> Diagnostics{};
    };

    enum class EditorRenderRecipeDraftState : std::uint8_t
    {
        InactiveDraft = 0,
        Debounced,
        Validated,
        Rejected,
        Previewed,
        Activated,
        Canceled,
    };

    enum class EditorRenderRecipeCommandKind : std::uint8_t
    {
        UpdateDraft = 0,
        ValidateDraft,
        PreviewDraft,
        ActivatePreview,
        CancelDraft,
        PublishArtifact,
        ApplyArtifact,
    };

    enum class EditorRenderRecipeCommandStatus : std::uint8_t
    {
        NoChange = 0,
        DraftUpdated,
        Debounced,
        Validated,
        ValidationFailed,
        Previewed,
        PreviewFailed,
        Activated,
        Canceled,
        Published,
        Applied,
        ActivationFailed,
        MissingRecipeContext,
        MissingEditorState,
        MissingArtifactRegistry,
        ArtifactCommandFailed,
    };

    [[nodiscard]] const char*
    DebugNameForEditorRenderRecipeDraftState(EditorRenderRecipeDraftState state) noexcept;

    [[nodiscard]] const char*
    DebugNameForEditorRenderRecipeCommandKind(EditorRenderRecipeCommandKind kind) noexcept;

    [[nodiscard]] const char*
    DebugNameForEditorRenderRecipeCommandStatus(EditorRenderRecipeCommandStatus status) noexcept;

    struct EditorRenderRecipeSlotModel
    {
        std::string StableName{};
        Graphics::RecipeSlotKind Kind{Graphics::RecipeSlotKind::Extension};
        std::string SchemaId{};
        std::string Defaults{};
        std::vector<std::string> RequiredCapabilities{};
        std::vector<std::string> AllowedBindingRoles{};
        std::vector<std::string> UsedBindingRoles{};
        bool DeclaredByRenderer{false};
        bool Editable{false};
        std::string DisabledReason{};
    };

    struct EditorRenderRecipeBindingOverrideModel
    {
        std::string SemanticName{};
        std::string Slot{};
        std::string SourceDomain{};
        std::string SourceIdentity{};
        std::string SourceRevision{};
        std::string ValueType{};
        std::string ValueFormat{};
        bool Required{false};
        bool Editable{false};
        std::string DisabledReason{};
    };

    struct EditorRenderRecipeOutputModel
    {
        std::string Name{};
        std::string Kind{};
        std::string Format{};
        bool Required{false};
    };

    struct EditorRenderArtifactRow
    {
        std::string ArtifactId{};
        std::string Purpose{};
        RenderArtifactPublicationKind Kind{RenderArtifactPublicationKind::TransientFrame};
        RenderArtifactUiStatus Status{RenderArtifactPublicationState::Unknown};
        std::string PayloadUri{};
        std::string ProducerLabel{};
        bool CanPublish{false};
        bool CanApply{false};
        std::string DisabledReason{};
    };

    struct EditorRenderRecipeEditorModel
    {
        bool Available{false};
        std::string RendererId{};
        std::string ActiveRecipeId{};
        std::string ActiveViewOutputRecipeId{};
        std::string DraftRecipeId{};
        std::string DraftSourceId{};
        EditorRenderRecipeDraftState DraftState{EditorRenderRecipeDraftState::InactiveDraft};
        Graphics::RenderRecipeConfigState ValidationState{
            Graphics::RenderRecipeConfigState::Invalid};
        std::uint64_t DraftRevision{0u};
        std::uint64_t ActiveRevision{0u};
        std::uint32_t ParsedSlotCount{0u};
        std::uint32_t ParsedBindingOverrideCount{0u};
        std::string ViewKind{};
        std::string OutputTarget{};
        std::string InteractionMode{};
        std::uint32_t ViewportWidth{0u};
        std::uint32_t ViewportHeight{0u};
        float RenderScale{1.0f};
        bool CaptureRequested{false};
        bool ReadbackRequested{false};
        bool CanValidate{false};
        bool CanPreview{false};
        bool CanActivate{false};
        bool CanCancel{false};
        std::vector<EditorRenderRecipeSlotModel> Slots{};
        std::vector<EditorRenderRecipeBindingOverrideModel> BindingOverrides{};
        std::vector<EditorRenderRecipeOutputModel> Outputs{};
        std::vector<EditorRenderArtifactRow> Artifacts{};
        std::vector<EditorDiagnostic> Diagnostics{};
        std::vector<Graphics::RenderRecipeConfigDiagnostic> RecipeDiagnostics{};
    };

    struct EditorRenderRecipeEditorState
    {
        std::string DraftDocument{};
        std::string DraftSourceId{"sandbox-render-recipe-draft.json"};
        Graphics::RenderRecipeConfigLoadResult LastPreview{};
        bool HasLastPreview{false};
        Graphics::RenderRecipeDescriptor ActiveRecipe{};
        Graphics::ViewOutputRecipeDescriptor ActiveViewOutput{};
        Graphics::BindingSet ActiveBindings{};
        bool HasActiveOverride{false};
        EditorRenderRecipeDraftState DraftState{EditorRenderRecipeDraftState::InactiveDraft};
        std::uint64_t DraftRevision{0u};
        std::uint64_t ActiveRevision{0u};
    };

    struct EditorRenderRecipeDraftSnapshot
    {
        std::string DraftDocument{};
        EditorRenderRecipeDraftState DraftState{EditorRenderRecipeDraftState::InactiveDraft};
        std::uint64_t DraftRevision{0u};
    };
    struct EditorRenderRecipeCommand
    {
        EditorRenderRecipeCommandKind Kind{EditorRenderRecipeCommandKind::UpdateDraft};
        std::string Document{};
        std::string SourceId{"sandbox-render-recipe-draft.json"};
        bool Debounced{false};
        std::string ArtifactId{};
        std::string Provenance{"sandbox-editor"};
        std::string TargetUri{};
        std::string ProjectTarget{};
        std::string Label{};
        std::string UndoLabel{};
    };

    struct EditorRenderRecipeCommandResult
    {
        EditorRenderRecipeCommandStatus Status{EditorRenderRecipeCommandStatus::NoChange};
        EditorRenderRecipeDraftState DraftState{EditorRenderRecipeDraftState::InactiveDraft};
        Graphics::RenderRecipeConfigState ValidationState{
            Graphics::RenderRecipeConfigState::Invalid};
        RenderArtifactOperationStatus ArtifactStatus{RenderArtifactOperationStatus::InvalidRequest};
        RenderArtifactPublicationState ArtifactState{RenderArtifactPublicationState::Unknown};
        std::string ArtifactId{};
        std::uint64_t Revision{0u};
        bool ProjectMutationAuthorized{false};
        std::vector<Graphics::RenderRecipeConfigDiagnostic> RecipeDiagnostics{};
        std::vector<RenderArtifactDiagnostic> ArtifactDiagnostics{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == EditorRenderRecipeCommandStatus::NoChange ||
                   Status == EditorRenderRecipeCommandStatus::DraftUpdated ||
                   Status == EditorRenderRecipeCommandStatus::Debounced ||
                   Status == EditorRenderRecipeCommandStatus::Validated ||
                   Status == EditorRenderRecipeCommandStatus::Previewed ||
                   Status == EditorRenderRecipeCommandStatus::Activated ||
                   Status == EditorRenderRecipeCommandStatus::Canceled ||
                   Status == EditorRenderRecipeCommandStatus::Published ||
                   Status == EditorRenderRecipeCommandStatus::Applied;
        }
    };

    struct EditorRenderRecipeEditingContext
    {
        const Graphics::RenderGraphFrameStats* RenderGraphStats{nullptr};
        const Graphics::RenderRecipeConfigContext* RenderRecipeContext{nullptr};
        EditorRenderRecipeEditorState* RenderRecipeEditorState{nullptr};
        const RuntimeRenderRecipeState* RenderRecipeRuntimeState{nullptr};
        std::function<Graphics::RenderRecipeConfigLoadResult(const std::string&,
                                                             const std::string&)>
            PreviewRenderRecipeDocument{};
        std::function<RuntimeRenderRecipeApplyResult(const Graphics::RenderRecipeConfigLoadResult&)>
            ApplyRenderRecipePreview{};
        const RuntimeEngineConfigControlState* EngineConfigControlState{nullptr};
        std::function<Core::Config::EngineConfigLoadResult(const std::string&, const std::string&)>
            PreviewEngineConfigDocument{};
        std::function<RuntimeEngineConfigApplyResult(const Core::Config::EngineConfigLoadResult&)>
            ApplyEngineConfigHotSubset{};
        RenderArtifactRegistry* RenderArtifacts{nullptr};
        std::function<bool()> AttachmentActive{};
        bool RenderRecipeCommandsAvailable{false};
        bool EngineConfigCommandsAvailable{false};
    };

    class EditorRenderRecipeEditingCommands final
    {
    public:
        EditorRenderRecipeEditingCommands() = default;

        [[nodiscard]] bool IsBound() const noexcept;
        [[nodiscard]] operator const EditorRenderRecipeEditingContext&() const noexcept;

    private:
        struct State;
        std::shared_ptr<const State> m_State{};

        explicit EditorRenderRecipeEditingCommands(std::shared_ptr<const State> state);
        friend EditorRenderRecipeEditingCommands
        BindEditorRenderRecipeEditingCommands(EditorRenderRecipeEditingContext context);
    };

    [[nodiscard]] EditorRenderRecipeEditingCommands
    BindEditorRenderRecipeEditingCommands(EditorRenderRecipeEditingContext context);

    [[nodiscard]] EditorGpuProfilingConfigResult ApplyEditorGpuProfilingConfigCommand(
        const EditorRenderRecipeEditingContext& context, bool enabled,
        std::string sourceId = "sandbox.frame_graph.gpu_profiling");

    [[nodiscard]] EditorRenderRecipeEditorModel
    BuildEditorRenderRecipeEditorModel(const EditorRenderRecipeEditingContext& context);
    EditorRenderRecipeCommandResult
    ApplyEditorRenderRecipeCommand(const EditorRenderRecipeEditingContext& context,
                                   const EditorRenderRecipeCommand& command);
} // namespace Extrinsic::Runtime
