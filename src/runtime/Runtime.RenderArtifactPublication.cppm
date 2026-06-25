module;

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

export module Extrinsic.Runtime.RenderArtifactPublication;

import Extrinsic.Graphics.RenderingContract;

export namespace Extrinsic::Runtime
{
    enum class RenderArtifactPublicationKind : std::uint8_t
    {
        TransientFrame = 0,
        CachedFrame,
        SavedToFile,
        PreviewOnly,
        DatasetBatchOutput,
        ReadbackMetric,
        CandidateProjectResult,
    };

    enum class RenderArtifactPublicationState : std::uint8_t
    {
        Unknown = 0,
        Unpublished,
        Stale,
        Canceled,
        Failed,
        Superseded,
        Published,
        Applied,
    };

    using RenderArtifactUiStatus = RenderArtifactPublicationState;

    enum class RenderArtifactDiagnosticSeverity : std::uint8_t
    {
        Info = 0,
        Warning,
        Error,
    };

    enum class RenderArtifactDiagnosticCode : std::uint8_t
    {
        None = 0,
        EmptyArtifactId,
        MissingRendererId,
        MissingSnapshotId,
        MissingViewOutputRecipe,
        MissingPurpose,
        MissingSourceRevision,
        MissingProvenance,
        MissingPublishTarget,
        MissingApplyTarget,
        MissingUndoLabel,
        ArtifactNotFound,
        ArtifactStale,
        ArtifactCanceled,
        ArtifactFailed,
        ArtifactSuperseded,
        ArtifactNotCandidate,
        AlreadyPublished,
        AlreadyApplied,
        PublishRequiresUnpublished,
        ApplyRequiresPublished,
        UndoRequiresPublished,
        UndoRequiresApplied,
        SupersededByNewerArtifact,
    };

    enum class RenderArtifactOperationStatus : std::uint8_t
    {
        Registered = 0,
        Updated,
        Marked,
        Published,
        Applied,
        Undone,
        NotFound,
        InvalidRequest,
        Rejected,
    };

    enum class RenderArtifactAuditAction : std::uint8_t
    {
        Registered = 0,
        Updated,
        MarkedStale,
        MarkedCanceled,
        MarkedFailed,
        Superseded,
        Published,
        Applied,
        Unpublished,
        ApplyReverted,
    };

    struct RenderArtifactDiagnostic
    {
        RenderArtifactDiagnosticCode Code{RenderArtifactDiagnosticCode::None};
        RenderArtifactDiagnosticSeverity Severity{RenderArtifactDiagnosticSeverity::Info};
        std::string Message{};
    };

    struct RenderArtifactDeclaration
    {
        Graphics::RenderArtifactMetadata Metadata{};
        RenderArtifactPublicationKind Kind{RenderArtifactPublicationKind::TransientFrame};
        std::string PayloadUri{};
        std::string ProducerLabel{};
        std::vector<std::string> Diagnostics{};
    };

    struct RenderArtifactUndoMetadata
    {
        std::string ArtifactId{};
        std::string Label{};
        std::string Provenance{};
        RenderArtifactPublicationState RestoresState{RenderArtifactPublicationState::Unknown};
        bool Dirtying{false};
    };

    struct RenderArtifactPublishCommand
    {
        std::string ArtifactId{};
        std::string Provenance{};
        std::string TargetUri{};
        std::string Label{"Publish Render Artifact"};
        std::string UndoLabel{"Unpublish Render Artifact"};
    };

    struct RenderArtifactApplyCommand
    {
        std::string ArtifactId{};
        std::string Provenance{};
        std::string ProjectTarget{};
        std::string Label{"Apply Render Artifact"};
        std::string UndoLabel{"Revert Render Artifact Apply"};
    };

    struct RenderArtifactUndoCommand
    {
        std::string ArtifactId{};
        std::string Provenance{};
        std::string Label{};
    };

    struct RenderArtifactRecord
    {
        Graphics::RenderArtifactMetadata Metadata{};
        RenderArtifactPublicationKind Kind{RenderArtifactPublicationKind::TransientFrame};
        RenderArtifactPublicationState State{RenderArtifactPublicationState::Unpublished};
        std::string PayloadUri{};
        std::string ProducerLabel{};
        std::string PublishTargetUri{};
        std::string ApplyTarget{};
        std::string PublishProvenance{};
        std::string ApplyProvenance{};
        std::vector<RenderArtifactDiagnostic> Diagnostics{};
        std::uint64_t RegistryRevision{0u};
        std::uint64_t PublishedRevision{0u};
        std::uint64_t AppliedRevision{0u};
    };

    struct RenderArtifactAuditRecord
    {
        RenderArtifactAuditAction Action{RenderArtifactAuditAction::Registered};
        std::string ArtifactId{};
        RenderArtifactPublicationState Before{RenderArtifactPublicationState::Unknown};
        RenderArtifactPublicationState After{RenderArtifactPublicationState::Unknown};
        std::string Provenance{};
        std::string Target{};
        std::string Label{};
        std::uint64_t Revision{0u};
    };

    struct RenderArtifactOperationResult
    {
        RenderArtifactOperationStatus Status{RenderArtifactOperationStatus::InvalidRequest};
        std::string ArtifactId{};
        RenderArtifactPublicationState State{RenderArtifactPublicationState::Unknown};
        std::uint64_t Revision{0u};
        bool ProjectMutationAuthorized{false};
        bool ProjectMutationPerformed{false};
        std::vector<RenderArtifactDiagnostic> Diagnostics{};
        std::optional<RenderArtifactUndoMetadata> Undo{};

        [[nodiscard]] bool Succeeded() const noexcept;
    };

    [[nodiscard]] std::string_view ToString(RenderArtifactPublicationKind value) noexcept;
    [[nodiscard]] std::string_view ToString(RenderArtifactPublicationState value) noexcept;
    [[nodiscard]] std::string_view ToString(RenderArtifactDiagnosticCode value) noexcept;
    [[nodiscard]] std::string_view ToString(RenderArtifactOperationStatus value) noexcept;
    [[nodiscard]] std::string_view ToString(RenderArtifactAuditAction value) noexcept;
    [[nodiscard]] RenderArtifactUiStatus ToUiStatus(const RenderArtifactRecord& record) noexcept;
    [[nodiscard]] bool IsTerminalArtifactState(RenderArtifactPublicationState state) noexcept;
    [[nodiscard]] bool HasDiagnostic(
        const RenderArtifactOperationResult& result,
        RenderArtifactDiagnosticCode code) noexcept;

    class RenderArtifactRegistry
    {
    public:
        [[nodiscard]] RenderArtifactOperationResult RegisterArtifact(
            RenderArtifactDeclaration declaration);
        [[nodiscard]] RenderArtifactOperationResult MarkStale(
            std::string_view artifactId,
            std::string_view diagnostic = {});
        [[nodiscard]] RenderArtifactOperationResult MarkCanceled(
            std::string_view artifactId,
            std::string_view diagnostic = {});
        [[nodiscard]] RenderArtifactOperationResult MarkFailed(
            std::string_view artifactId,
            std::string_view diagnostic = {});
        [[nodiscard]] RenderArtifactOperationResult PublishArtifact(
            RenderArtifactPublishCommand command);
        [[nodiscard]] RenderArtifactOperationResult ApplyArtifact(
            RenderArtifactApplyCommand command);
        [[nodiscard]] RenderArtifactOperationResult UnpublishArtifact(
            RenderArtifactUndoCommand command);
        [[nodiscard]] RenderArtifactOperationResult RevertAppliedArtifact(
            RenderArtifactUndoCommand command);

        [[nodiscard]] std::optional<RenderArtifactRecord> Find(
            std::string_view artifactId) const;
        [[nodiscard]] std::vector<RenderArtifactRecord> Snapshot() const;
        [[nodiscard]] std::vector<RenderArtifactAuditRecord> AuditLog() const;
        [[nodiscard]] std::vector<RenderArtifactRecord> ListByStatus(
            RenderArtifactPublicationState state) const;
        [[nodiscard]] std::size_t Size() const noexcept { return m_Records.size(); }
        [[nodiscard]] std::uint64_t Revision() const noexcept { return m_Revision; }

    private:
        [[nodiscard]] RenderArtifactRecord* FindMutable(std::string_view artifactId);
        [[nodiscard]] const RenderArtifactRecord* FindRecord(
            std::string_view artifactId) const;
        [[nodiscard]] std::uint64_t NextRevision() noexcept;
        void AppendAudit(RenderArtifactAuditAction action,
                         const RenderArtifactRecord& record,
                         RenderArtifactPublicationState before,
                         std::string provenance,
                         std::string target,
                         std::string label);

        std::vector<RenderArtifactRecord> m_Records{};
        std::vector<RenderArtifactAuditRecord> m_Audit{};
        std::uint64_t m_Revision{0u};
    };
}
