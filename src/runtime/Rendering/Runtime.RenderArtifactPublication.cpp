module;

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module Extrinsic.Runtime.RenderArtifactPublication;

namespace Extrinsic::Runtime
{
    namespace
    {
        [[nodiscard]] RenderArtifactDiagnostic MakeDiagnostic(
            const RenderArtifactDiagnosticCode code,
            const RenderArtifactDiagnosticSeverity severity,
            std::string message)
        {
            return RenderArtifactDiagnostic{
                .Code = code,
                .Severity = severity,
                .Message = std::move(message),
            };
        }

        void AddDiagnostic(std::vector<RenderArtifactDiagnostic>& diagnostics,
                           const RenderArtifactDiagnosticCode code,
                           const RenderArtifactDiagnosticSeverity severity,
                           std::string message)
        {
            diagnostics.push_back(MakeDiagnostic(code, severity, std::move(message)));
        }

        void AddError(std::vector<RenderArtifactDiagnostic>& diagnostics,
                      const RenderArtifactDiagnosticCode code,
                      std::string message)
        {
            AddDiagnostic(diagnostics,
                          code,
                          RenderArtifactDiagnosticSeverity::Error,
                          std::move(message));
        }

        void AddWarning(std::vector<RenderArtifactDiagnostic>& diagnostics,
                        const RenderArtifactDiagnosticCode code,
                        std::string message)
        {
            AddDiagnostic(diagnostics,
                          code,
                          RenderArtifactDiagnosticSeverity::Warning,
                          std::move(message));
        }

        [[nodiscard]] bool HasErrors(
            const std::vector<RenderArtifactDiagnostic>& diagnostics) noexcept
        {
            return std::any_of(diagnostics.begin(),
                               diagnostics.end(),
                               [](const RenderArtifactDiagnostic& diagnostic) {
                                   return diagnostic.Severity ==
                                          RenderArtifactDiagnosticSeverity::Error;
                               });
        }

        [[nodiscard]] RenderArtifactPublicationState StateFromGraphicsStatus(
            const Graphics::RenderArtifactStatus status) noexcept
        {
            switch (status)
            {
            case Graphics::RenderArtifactStatus::Declared:
            case Graphics::RenderArtifactStatus::Available:
                return RenderArtifactPublicationState::Unpublished;
            case Graphics::RenderArtifactStatus::Stale:
                return RenderArtifactPublicationState::Stale;
            case Graphics::RenderArtifactStatus::Missing:
            case Graphics::RenderArtifactStatus::Failed:
                return RenderArtifactPublicationState::Failed;
            case Graphics::RenderArtifactStatus::Published:
                return RenderArtifactPublicationState::Published;
            case Graphics::RenderArtifactStatus::Discarded:
                return RenderArtifactPublicationState::Canceled;
            }
            return RenderArtifactPublicationState::Unknown;
        }

        [[nodiscard]] Graphics::RenderArtifactStatus GraphicsStatusForState(
            const RenderArtifactPublicationState state) noexcept
        {
            switch (state)
            {
            case RenderArtifactPublicationState::Unpublished:
                return Graphics::RenderArtifactStatus::Available;
            case RenderArtifactPublicationState::Stale:
                return Graphics::RenderArtifactStatus::Stale;
            case RenderArtifactPublicationState::Canceled:
                return Graphics::RenderArtifactStatus::Discarded;
            case RenderArtifactPublicationState::Failed:
                return Graphics::RenderArtifactStatus::Failed;
            case RenderArtifactPublicationState::Superseded:
                return Graphics::RenderArtifactStatus::Stale;
            case RenderArtifactPublicationState::Published:
            case RenderArtifactPublicationState::Applied:
                return Graphics::RenderArtifactStatus::Published;
            case RenderArtifactPublicationState::Unknown:
                return Graphics::RenderArtifactStatus::Declared;
            }
            return Graphics::RenderArtifactStatus::Declared;
        }

        [[nodiscard]] bool SameLogicalArtifact(
            const RenderArtifactRecord& lhs,
            const RenderArtifactRecord& rhs) noexcept
        {
            return lhs.Kind == rhs.Kind &&
                   lhs.Metadata.RendererId == rhs.Metadata.RendererId &&
                   lhs.Metadata.SnapshotId == rhs.Metadata.SnapshotId &&
                   lhs.Metadata.ViewOutputRecipeId == rhs.Metadata.ViewOutputRecipeId &&
                   lhs.Metadata.Purpose == rhs.Metadata.Purpose;
        }

        [[nodiscard]] bool SameSourceRevisions(
            const RenderArtifactRecord& lhs,
            const RenderArtifactRecord& rhs) noexcept
        {
            return lhs.Metadata.SourceRevisions == rhs.Metadata.SourceRevisions;
        }

        [[nodiscard]] bool IsCandidateKind(
            const RenderArtifactPublicationKind kind) noexcept
        {
            return kind == RenderArtifactPublicationKind::CandidateProjectResult;
        }

        [[nodiscard]] RenderArtifactOperationResult MakeResult(
            const RenderArtifactOperationStatus status,
            const std::string_view artifactId,
            const RenderArtifactPublicationState state,
            const std::uint64_t revision)
        {
            return RenderArtifactOperationResult{
                .Status = status,
                .ArtifactId = std::string{artifactId},
                .State = state,
                .Revision = revision,
            };
        }

        [[nodiscard]] RenderArtifactOperationResult MakeRejected(
            const std::string_view artifactId,
            const RenderArtifactPublicationState state,
            const std::uint64_t revision,
            const RenderArtifactDiagnosticCode code,
            std::string message)
        {
            RenderArtifactOperationResult result =
                MakeResult(RenderArtifactOperationStatus::Rejected,
                           artifactId,
                           state,
                           revision);
            AddError(result.Diagnostics, code, std::move(message));
            return result;
        }

        [[nodiscard]] std::vector<RenderArtifactDiagnostic> ValidateDeclaration(
            const RenderArtifactDeclaration& declaration)
        {
            std::vector<RenderArtifactDiagnostic> diagnostics{};
            if (declaration.Metadata.ArtifactId.empty())
                AddError(diagnostics,
                         RenderArtifactDiagnosticCode::EmptyArtifactId,
                         "render artifact declarations must carry an artifact id");
            if (declaration.Metadata.RendererId.empty())
                AddError(diagnostics,
                         RenderArtifactDiagnosticCode::MissingRendererId,
                         "render artifact declarations must name the renderer");
            if (declaration.Metadata.SnapshotId.empty())
                AddError(diagnostics,
                         RenderArtifactDiagnosticCode::MissingSnapshotId,
                         "render artifact declarations must name the snapshot");
            if (declaration.Metadata.ViewOutputRecipeId.empty())
                AddError(diagnostics,
                         RenderArtifactDiagnosticCode::MissingViewOutputRecipe,
                         "render artifact declarations must name the view/output recipe");
            if (declaration.Metadata.Purpose.empty())
                AddError(diagnostics,
                         RenderArtifactDiagnosticCode::MissingPurpose,
                         "render artifact declarations must name the output purpose");
            if (declaration.Metadata.SourceRevisions.empty())
                AddError(diagnostics,
                         RenderArtifactDiagnosticCode::MissingSourceRevision,
                         "render artifact declarations must carry source revisions");
            return diagnostics;
        }

        void ImportDeclarationDiagnostics(RenderArtifactRecord& record,
                                          const RenderArtifactDeclaration& declaration)
        {
            for (const std::string& diagnostic : declaration.Diagnostics)
            {
                AddDiagnostic(record.Diagnostics,
                              RenderArtifactDiagnosticCode::None,
                              RenderArtifactDiagnosticSeverity::Info,
                              diagnostic);
            }
            for (const std::string& diagnostic : declaration.Metadata.Diagnostics)
            {
                AddDiagnostic(record.Diagnostics,
                              RenderArtifactDiagnosticCode::None,
                              RenderArtifactDiagnosticSeverity::Info,
                              diagnostic);
            }
        }

        void SetRecordState(RenderArtifactRecord& record,
                            const RenderArtifactPublicationState state)
        {
            record.State = state;
            record.Metadata.Status = GraphicsStatusForState(state);
            if (state == RenderArtifactPublicationState::Published ||
                state == RenderArtifactPublicationState::Applied)
            {
                record.Metadata.Lifetime = Graphics::RenderArtifactLifetime::Published;
            }
        }

        [[nodiscard]] RenderArtifactDiagnosticCode BlockedCodeForState(
            const RenderArtifactPublicationState state) noexcept
        {
            switch (state)
            {
            case RenderArtifactPublicationState::Stale:
                return RenderArtifactDiagnosticCode::ArtifactStale;
            case RenderArtifactPublicationState::Canceled:
                return RenderArtifactDiagnosticCode::ArtifactCanceled;
            case RenderArtifactPublicationState::Failed:
                return RenderArtifactDiagnosticCode::ArtifactFailed;
            case RenderArtifactPublicationState::Superseded:
                return RenderArtifactDiagnosticCode::ArtifactSuperseded;
            case RenderArtifactPublicationState::Published:
                return RenderArtifactDiagnosticCode::AlreadyPublished;
            case RenderArtifactPublicationState::Applied:
                return RenderArtifactDiagnosticCode::AlreadyApplied;
            case RenderArtifactPublicationState::Unknown:
            case RenderArtifactPublicationState::Unpublished:
                break;
            }
            return RenderArtifactDiagnosticCode::PublishRequiresUnpublished;
        }

        [[nodiscard]] std::string BlockedMessageForState(
            const RenderArtifactPublicationState state)
        {
            return std::string{"render artifact state does not allow this operation: "} +
                   std::string{ToString(state)};
        }
    }

    bool RenderArtifactOperationResult::Succeeded() const noexcept
    {
        switch (Status)
        {
        case RenderArtifactOperationStatus::Registered:
        case RenderArtifactOperationStatus::Updated:
        case RenderArtifactOperationStatus::Marked:
        case RenderArtifactOperationStatus::Published:
        case RenderArtifactOperationStatus::Applied:
        case RenderArtifactOperationStatus::Undone:
            return true;
        case RenderArtifactOperationStatus::NotFound:
        case RenderArtifactOperationStatus::InvalidRequest:
        case RenderArtifactOperationStatus::Rejected:
            return false;
        }
        return false;
    }

    std::string_view ToString(const RenderArtifactPublicationKind value) noexcept
    {
        switch (value)
        {
        case RenderArtifactPublicationKind::TransientFrame:
            return "TransientFrame";
        case RenderArtifactPublicationKind::CachedFrame:
            return "CachedFrame";
        case RenderArtifactPublicationKind::SavedToFile:
            return "SavedToFile";
        case RenderArtifactPublicationKind::PreviewOnly:
            return "PreviewOnly";
        case RenderArtifactPublicationKind::DatasetBatchOutput:
            return "DatasetBatchOutput";
        case RenderArtifactPublicationKind::ReadbackMetric:
            return "ReadbackMetric";
        case RenderArtifactPublicationKind::CandidateProjectResult:
            return "CandidateProjectResult";
        }
        return "Unknown";
    }

    std::string_view ToString(const RenderArtifactPublicationState value) noexcept
    {
        switch (value)
        {
        case RenderArtifactPublicationState::Unknown:
            return "Unknown";
        case RenderArtifactPublicationState::Unpublished:
            return "Unpublished";
        case RenderArtifactPublicationState::Stale:
            return "Stale";
        case RenderArtifactPublicationState::Canceled:
            return "Canceled";
        case RenderArtifactPublicationState::Failed:
            return "Failed";
        case RenderArtifactPublicationState::Superseded:
            return "Superseded";
        case RenderArtifactPublicationState::Published:
            return "Published";
        case RenderArtifactPublicationState::Applied:
            return "Applied";
        }
        return "Unknown";
    }

    std::string_view ToString(const RenderArtifactDiagnosticCode value) noexcept
    {
        switch (value)
        {
        case RenderArtifactDiagnosticCode::None:
            return "None";
        case RenderArtifactDiagnosticCode::EmptyArtifactId:
            return "EmptyArtifactId";
        case RenderArtifactDiagnosticCode::MissingRendererId:
            return "MissingRendererId";
        case RenderArtifactDiagnosticCode::MissingSnapshotId:
            return "MissingSnapshotId";
        case RenderArtifactDiagnosticCode::MissingViewOutputRecipe:
            return "MissingViewOutputRecipe";
        case RenderArtifactDiagnosticCode::MissingPurpose:
            return "MissingPurpose";
        case RenderArtifactDiagnosticCode::MissingSourceRevision:
            return "MissingSourceRevision";
        case RenderArtifactDiagnosticCode::MissingProvenance:
            return "MissingProvenance";
        case RenderArtifactDiagnosticCode::MissingPublishTarget:
            return "MissingPublishTarget";
        case RenderArtifactDiagnosticCode::MissingApplyTarget:
            return "MissingApplyTarget";
        case RenderArtifactDiagnosticCode::MissingUndoLabel:
            return "MissingUndoLabel";
        case RenderArtifactDiagnosticCode::ArtifactNotFound:
            return "ArtifactNotFound";
        case RenderArtifactDiagnosticCode::ArtifactStale:
            return "ArtifactStale";
        case RenderArtifactDiagnosticCode::ArtifactCanceled:
            return "ArtifactCanceled";
        case RenderArtifactDiagnosticCode::ArtifactFailed:
            return "ArtifactFailed";
        case RenderArtifactDiagnosticCode::ArtifactSuperseded:
            return "ArtifactSuperseded";
        case RenderArtifactDiagnosticCode::ArtifactNotCandidate:
            return "ArtifactNotCandidate";
        case RenderArtifactDiagnosticCode::AlreadyPublished:
            return "AlreadyPublished";
        case RenderArtifactDiagnosticCode::AlreadyApplied:
            return "AlreadyApplied";
        case RenderArtifactDiagnosticCode::PublishRequiresUnpublished:
            return "PublishRequiresUnpublished";
        case RenderArtifactDiagnosticCode::ApplyRequiresPublished:
            return "ApplyRequiresPublished";
        case RenderArtifactDiagnosticCode::UndoRequiresPublished:
            return "UndoRequiresPublished";
        case RenderArtifactDiagnosticCode::UndoRequiresApplied:
            return "UndoRequiresApplied";
        case RenderArtifactDiagnosticCode::SupersededByNewerArtifact:
            return "SupersededByNewerArtifact";
        }
        return "Unknown";
    }

    std::string_view ToString(const RenderArtifactOperationStatus value) noexcept
    {
        switch (value)
        {
        case RenderArtifactOperationStatus::Registered:
            return "Registered";
        case RenderArtifactOperationStatus::Updated:
            return "Updated";
        case RenderArtifactOperationStatus::Marked:
            return "Marked";
        case RenderArtifactOperationStatus::Published:
            return "Published";
        case RenderArtifactOperationStatus::Applied:
            return "Applied";
        case RenderArtifactOperationStatus::Undone:
            return "Undone";
        case RenderArtifactOperationStatus::NotFound:
            return "NotFound";
        case RenderArtifactOperationStatus::InvalidRequest:
            return "InvalidRequest";
        case RenderArtifactOperationStatus::Rejected:
            return "Rejected";
        }
        return "Unknown";
    }

    std::string_view ToString(const RenderArtifactAuditAction value) noexcept
    {
        switch (value)
        {
        case RenderArtifactAuditAction::Registered:
            return "Registered";
        case RenderArtifactAuditAction::Updated:
            return "Updated";
        case RenderArtifactAuditAction::MarkedStale:
            return "MarkedStale";
        case RenderArtifactAuditAction::MarkedCanceled:
            return "MarkedCanceled";
        case RenderArtifactAuditAction::MarkedFailed:
            return "MarkedFailed";
        case RenderArtifactAuditAction::Superseded:
            return "Superseded";
        case RenderArtifactAuditAction::Published:
            return "Published";
        case RenderArtifactAuditAction::Applied:
            return "Applied";
        case RenderArtifactAuditAction::Unpublished:
            return "Unpublished";
        case RenderArtifactAuditAction::ApplyReverted:
            return "ApplyReverted";
        }
        return "Unknown";
    }

    RenderArtifactUiStatus ToUiStatus(const RenderArtifactRecord& record) noexcept
    {
        return record.State;
    }

    bool IsTerminalArtifactState(const RenderArtifactPublicationState state) noexcept
    {
        switch (state)
        {
        case RenderArtifactPublicationState::Canceled:
        case RenderArtifactPublicationState::Failed:
        case RenderArtifactPublicationState::Superseded:
            return true;
        case RenderArtifactPublicationState::Unknown:
        case RenderArtifactPublicationState::Unpublished:
        case RenderArtifactPublicationState::Stale:
        case RenderArtifactPublicationState::Published:
        case RenderArtifactPublicationState::Applied:
            return false;
        }
        return false;
    }

    bool HasDiagnostic(const RenderArtifactOperationResult& result,
                       const RenderArtifactDiagnosticCode code) noexcept
    {
        return std::any_of(result.Diagnostics.begin(),
                           result.Diagnostics.end(),
                           [code](const RenderArtifactDiagnostic& diagnostic) {
                               return diagnostic.Code == code;
                           });
    }

    RenderArtifactOperationResult RenderArtifactRegistry::RegisterArtifact(
        RenderArtifactDeclaration declaration)
    {
        std::vector<RenderArtifactDiagnostic> diagnostics =
            ValidateDeclaration(declaration);
        if (HasErrors(diagnostics))
        {
            RenderArtifactOperationResult result =
                MakeResult(RenderArtifactOperationStatus::InvalidRequest,
                           declaration.Metadata.ArtifactId,
                           RenderArtifactPublicationState::Unknown,
                           m_Revision);
            result.Diagnostics = std::move(diagnostics);
            return result;
        }

        RenderArtifactRecord next{
            .Kind = declaration.Kind,
            .State = StateFromGraphicsStatus(declaration.Metadata.Status),
            .PayloadUri = std::move(declaration.PayloadUri),
            .ProducerLabel = std::move(declaration.ProducerLabel),
        };
        ImportDeclarationDiagnostics(next, declaration);
        next.Metadata = std::move(declaration.Metadata);

        const bool hadExisting = FindRecord(next.Metadata.ArtifactId) != nullptr;
        const std::uint64_t revision = NextRevision();
        next.RegistryRevision = revision;

        for (RenderArtifactRecord& record : m_Records)
        {
            if (record.Metadata.ArtifactId == next.Metadata.ArtifactId)
                continue;
            if (!SameLogicalArtifact(record, next) || SameSourceRevisions(record, next))
                continue;
            if (IsTerminalArtifactState(record.State))
                continue;

            const RenderArtifactPublicationState before = record.State;
            SetRecordState(record, RenderArtifactPublicationState::Superseded);
            record.RegistryRevision = revision;
            AddWarning(record.Diagnostics,
                       RenderArtifactDiagnosticCode::SupersededByNewerArtifact,
                       "a newer artifact was registered for the same renderer/snapshot/view/purpose");
            AppendAudit(RenderArtifactAuditAction::Superseded,
                        record,
                        before,
                        next.Metadata.ArtifactId,
                        {},
                        "Superseded by newer render artifact");
        }

        if (RenderArtifactRecord* existing = FindMutable(next.Metadata.ArtifactId);
            existing != nullptr)
        {
            const RenderArtifactPublicationState before = existing->State;
            *existing = std::move(next);
            AppendAudit(RenderArtifactAuditAction::Updated,
                        *existing,
                        before,
                        {},
                        existing->PayloadUri,
                        "Update render artifact");
            return MakeResult(RenderArtifactOperationStatus::Updated,
                              existing->Metadata.ArtifactId,
                              existing->State,
                              revision);
        }

        m_Records.push_back(std::move(next));
        RenderArtifactRecord& inserted = m_Records.back();
        AppendAudit(hadExisting ? RenderArtifactAuditAction::Updated
                                : RenderArtifactAuditAction::Registered,
                    inserted,
                    RenderArtifactPublicationState::Unknown,
                    {},
                    inserted.PayloadUri,
                    "Register render artifact");
        return MakeResult(RenderArtifactOperationStatus::Registered,
                          inserted.Metadata.ArtifactId,
                          inserted.State,
                          revision);
    }

    RenderArtifactOperationResult RenderArtifactRegistry::MarkStale(
        const std::string_view artifactId,
        const std::string_view diagnostic)
    {
        RenderArtifactRecord* record = FindMutable(artifactId);
        if (record == nullptr)
        {
            RenderArtifactOperationResult result =
                MakeResult(RenderArtifactOperationStatus::NotFound,
                           artifactId,
                           RenderArtifactPublicationState::Unknown,
                           m_Revision);
            AddError(result.Diagnostics,
                     RenderArtifactDiagnosticCode::ArtifactNotFound,
                     "render artifact not found");
            return result;
        }

        const RenderArtifactPublicationState before = record->State;
        SetRecordState(*record, RenderArtifactPublicationState::Stale);
        record->RegistryRevision = NextRevision();
        AddWarning(record->Diagnostics,
                   RenderArtifactDiagnosticCode::ArtifactStale,
                   diagnostic.empty() ? "render artifact is stale"
                                      : std::string{diagnostic});
        AppendAudit(RenderArtifactAuditAction::MarkedStale,
                    *record,
                    before,
                    {},
                    {},
                    "Mark render artifact stale");
        return MakeResult(RenderArtifactOperationStatus::Marked,
                          record->Metadata.ArtifactId,
                          record->State,
                          m_Revision);
    }

    RenderArtifactOperationResult RenderArtifactRegistry::MarkCanceled(
        const std::string_view artifactId,
        const std::string_view diagnostic)
    {
        RenderArtifactRecord* record = FindMutable(artifactId);
        if (record == nullptr)
        {
            RenderArtifactOperationResult result =
                MakeResult(RenderArtifactOperationStatus::NotFound,
                           artifactId,
                           RenderArtifactPublicationState::Unknown,
                           m_Revision);
            AddError(result.Diagnostics,
                     RenderArtifactDiagnosticCode::ArtifactNotFound,
                     "render artifact not found");
            return result;
        }

        const RenderArtifactPublicationState before = record->State;
        SetRecordState(*record, RenderArtifactPublicationState::Canceled);
        record->RegistryRevision = NextRevision();
        AddWarning(record->Diagnostics,
                   RenderArtifactDiagnosticCode::ArtifactCanceled,
                   diagnostic.empty() ? "render artifact publication was canceled"
                                      : std::string{diagnostic});
        AppendAudit(RenderArtifactAuditAction::MarkedCanceled,
                    *record,
                    before,
                    {},
                    {},
                    "Cancel render artifact");
        return MakeResult(RenderArtifactOperationStatus::Marked,
                          record->Metadata.ArtifactId,
                          record->State,
                          m_Revision);
    }

    RenderArtifactOperationResult RenderArtifactRegistry::MarkFailed(
        const std::string_view artifactId,
        const std::string_view diagnostic)
    {
        RenderArtifactRecord* record = FindMutable(artifactId);
        if (record == nullptr)
        {
            RenderArtifactOperationResult result =
                MakeResult(RenderArtifactOperationStatus::NotFound,
                           artifactId,
                           RenderArtifactPublicationState::Unknown,
                           m_Revision);
            AddError(result.Diagnostics,
                     RenderArtifactDiagnosticCode::ArtifactNotFound,
                     "render artifact not found");
            return result;
        }

        const RenderArtifactPublicationState before = record->State;
        SetRecordState(*record, RenderArtifactPublicationState::Failed);
        record->RegistryRevision = NextRevision();
        AddError(record->Diagnostics,
                 RenderArtifactDiagnosticCode::ArtifactFailed,
                 diagnostic.empty() ? "render artifact publication failed"
                                    : std::string{diagnostic});
        AppendAudit(RenderArtifactAuditAction::MarkedFailed,
                    *record,
                    before,
                    {},
                    {},
                    "Fail render artifact");
        return MakeResult(RenderArtifactOperationStatus::Marked,
                          record->Metadata.ArtifactId,
                          record->State,
                          m_Revision);
    }

    RenderArtifactOperationResult RenderArtifactRegistry::PublishArtifact(
        RenderArtifactPublishCommand command)
    {
        if (command.ArtifactId.empty())
            return MakeRejected({},
                                RenderArtifactPublicationState::Unknown,
                                m_Revision,
                                RenderArtifactDiagnosticCode::EmptyArtifactId,
                                "publish commands must name an artifact");
        if (command.Provenance.empty())
            return MakeRejected(command.ArtifactId,
                                RenderArtifactPublicationState::Unknown,
                                m_Revision,
                                RenderArtifactDiagnosticCode::MissingProvenance,
                                "publish commands must carry provenance");
        if (command.TargetUri.empty())
            return MakeRejected(command.ArtifactId,
                                RenderArtifactPublicationState::Unknown,
                                m_Revision,
                                RenderArtifactDiagnosticCode::MissingPublishTarget,
                                "publish commands must name a target uri");

        RenderArtifactRecord* record = FindMutable(command.ArtifactId);
        if (record == nullptr)
        {
            RenderArtifactOperationResult result =
                MakeResult(RenderArtifactOperationStatus::NotFound,
                           command.ArtifactId,
                           RenderArtifactPublicationState::Unknown,
                           m_Revision);
            AddError(result.Diagnostics,
                     RenderArtifactDiagnosticCode::ArtifactNotFound,
                     "render artifact not found");
            return result;
        }

        if (record->State != RenderArtifactPublicationState::Unpublished)
        {
            return MakeRejected(record->Metadata.ArtifactId,
                                record->State,
                                m_Revision,
                                BlockedCodeForState(record->State),
                                BlockedMessageForState(record->State));
        }

        const RenderArtifactPublicationState before = record->State;
        SetRecordState(*record, RenderArtifactPublicationState::Published);
        record->PublishTargetUri = std::move(command.TargetUri);
        record->PublishProvenance = std::move(command.Provenance);
        record->PublishedRevision = NextRevision();
        AppendAudit(RenderArtifactAuditAction::Published,
                    *record,
                    before,
                    record->PublishProvenance,
                    record->PublishTargetUri,
                    command.Label);

        RenderArtifactOperationResult result =
            MakeResult(RenderArtifactOperationStatus::Published,
                       record->Metadata.ArtifactId,
                       record->State,
                       m_Revision);
        result.Undo = RenderArtifactUndoMetadata{
            .ArtifactId = record->Metadata.ArtifactId,
            .Label = std::move(command.UndoLabel),
            .Provenance = record->PublishProvenance,
            .RestoresState = RenderArtifactPublicationState::Unpublished,
            .Dirtying = false,
        };
        return result;
    }

    RenderArtifactOperationResult RenderArtifactRegistry::ApplyArtifact(
        RenderArtifactApplyCommand command)
    {
        if (command.ArtifactId.empty())
            return MakeRejected({},
                                RenderArtifactPublicationState::Unknown,
                                m_Revision,
                                RenderArtifactDiagnosticCode::EmptyArtifactId,
                                "apply commands must name an artifact");
        if (command.Provenance.empty())
            return MakeRejected(command.ArtifactId,
                                RenderArtifactPublicationState::Unknown,
                                m_Revision,
                                RenderArtifactDiagnosticCode::MissingProvenance,
                                "apply commands must carry provenance");
        if (command.ProjectTarget.empty())
            return MakeRejected(command.ArtifactId,
                                RenderArtifactPublicationState::Unknown,
                                m_Revision,
                                RenderArtifactDiagnosticCode::MissingApplyTarget,
                                "apply commands must name the project target");
        if (command.UndoLabel.empty())
            return MakeRejected(command.ArtifactId,
                                RenderArtifactPublicationState::Unknown,
                                m_Revision,
                                RenderArtifactDiagnosticCode::MissingUndoLabel,
                                "apply commands must carry an undo label");

        RenderArtifactRecord* record = FindMutable(command.ArtifactId);
        if (record == nullptr)
        {
            RenderArtifactOperationResult result =
                MakeResult(RenderArtifactOperationStatus::NotFound,
                           command.ArtifactId,
                           RenderArtifactPublicationState::Unknown,
                           m_Revision);
            AddError(result.Diagnostics,
                     RenderArtifactDiagnosticCode::ArtifactNotFound,
                     "render artifact not found");
            return result;
        }
        if (record->State != RenderArtifactPublicationState::Published)
        {
            return MakeRejected(record->Metadata.ArtifactId,
                                record->State,
                                m_Revision,
                                RenderArtifactDiagnosticCode::ApplyRequiresPublished,
                                "apply commands require an explicitly published artifact");
        }
        if (!IsCandidateKind(record->Kind))
        {
            return MakeRejected(record->Metadata.ArtifactId,
                                record->State,
                                m_Revision,
                                RenderArtifactDiagnosticCode::ArtifactNotCandidate,
                                "only candidate project-result artifacts can be applied");
        }

        const RenderArtifactPublicationState before = record->State;
        SetRecordState(*record, RenderArtifactPublicationState::Applied);
        record->ApplyTarget = std::move(command.ProjectTarget);
        record->ApplyProvenance = std::move(command.Provenance);
        record->AppliedRevision = NextRevision();
        AppendAudit(RenderArtifactAuditAction::Applied,
                    *record,
                    before,
                    record->ApplyProvenance,
                    record->ApplyTarget,
                    command.Label);

        RenderArtifactOperationResult result =
            MakeResult(RenderArtifactOperationStatus::Applied,
                       record->Metadata.ArtifactId,
                       record->State,
                       m_Revision);
        result.ProjectMutationAuthorized = true;
        result.ProjectMutationPerformed = false;
        result.Undo = RenderArtifactUndoMetadata{
            .ArtifactId = record->Metadata.ArtifactId,
            .Label = std::move(command.UndoLabel),
            .Provenance = record->ApplyProvenance,
            .RestoresState = RenderArtifactPublicationState::Published,
            .Dirtying = true,
        };
        return result;
    }

    RenderArtifactOperationResult RenderArtifactRegistry::UnpublishArtifact(
        RenderArtifactUndoCommand command)
    {
        RenderArtifactRecord* record = FindMutable(command.ArtifactId);
        if (record == nullptr)
        {
            RenderArtifactOperationResult result =
                MakeResult(RenderArtifactOperationStatus::NotFound,
                           command.ArtifactId,
                           RenderArtifactPublicationState::Unknown,
                           m_Revision);
            AddError(result.Diagnostics,
                     RenderArtifactDiagnosticCode::ArtifactNotFound,
                     "render artifact not found");
            return result;
        }
        if (record->State != RenderArtifactPublicationState::Published)
        {
            return MakeRejected(record->Metadata.ArtifactId,
                                record->State,
                                m_Revision,
                                RenderArtifactDiagnosticCode::UndoRequiresPublished,
                                "unpublish undo requires a published artifact");
        }

        const RenderArtifactPublicationState before = record->State;
        SetRecordState(*record, RenderArtifactPublicationState::Unpublished);
        record->PublishTargetUri.clear();
        record->PublishedRevision = NextRevision();
        AppendAudit(RenderArtifactAuditAction::Unpublished,
                    *record,
                    before,
                    std::move(command.Provenance),
                    {},
                    std::move(command.Label));
        return MakeResult(RenderArtifactOperationStatus::Undone,
                          record->Metadata.ArtifactId,
                          record->State,
                          m_Revision);
    }

    RenderArtifactOperationResult RenderArtifactRegistry::RevertAppliedArtifact(
        RenderArtifactUndoCommand command)
    {
        RenderArtifactRecord* record = FindMutable(command.ArtifactId);
        if (record == nullptr)
        {
            RenderArtifactOperationResult result =
                MakeResult(RenderArtifactOperationStatus::NotFound,
                           command.ArtifactId,
                           RenderArtifactPublicationState::Unknown,
                           m_Revision);
            AddError(result.Diagnostics,
                     RenderArtifactDiagnosticCode::ArtifactNotFound,
                     "render artifact not found");
            return result;
        }
        if (record->State != RenderArtifactPublicationState::Applied)
        {
            return MakeRejected(record->Metadata.ArtifactId,
                                record->State,
                                m_Revision,
                                RenderArtifactDiagnosticCode::UndoRequiresApplied,
                                "apply undo requires an applied artifact");
        }

        const RenderArtifactPublicationState before = record->State;
        SetRecordState(*record, RenderArtifactPublicationState::Published);
        record->AppliedRevision = NextRevision();
        AppendAudit(RenderArtifactAuditAction::ApplyReverted,
                    *record,
                    before,
                    std::move(command.Provenance),
                    record->ApplyTarget,
                    std::move(command.Label));

        RenderArtifactOperationResult result =
            MakeResult(RenderArtifactOperationStatus::Undone,
                       record->Metadata.ArtifactId,
                       record->State,
                       m_Revision);
        result.ProjectMutationAuthorized = true;
        result.ProjectMutationPerformed = false;
        return result;
    }

    std::optional<RenderArtifactRecord> RenderArtifactRegistry::Find(
        const std::string_view artifactId) const
    {
        const RenderArtifactRecord* record = FindRecord(artifactId);
        if (record == nullptr)
            return std::nullopt;
        return *record;
    }

    std::vector<RenderArtifactRecord> RenderArtifactRegistry::Snapshot() const
    {
        return m_Records;
    }

    std::vector<RenderArtifactAuditRecord> RenderArtifactRegistry::AuditLog() const
    {
        return m_Audit;
    }

    std::vector<RenderArtifactRecord> RenderArtifactRegistry::ListByStatus(
        const RenderArtifactPublicationState state) const
    {
        std::vector<RenderArtifactRecord> records{};
        for (const RenderArtifactRecord& record : m_Records)
        {
            if (record.State == state)
                records.push_back(record);
        }
        return records;
    }

    RenderArtifactRecord* RenderArtifactRegistry::FindMutable(
        const std::string_view artifactId)
    {
        const auto found = std::find_if(m_Records.begin(),
                                        m_Records.end(),
                                        [artifactId](const RenderArtifactRecord& record) {
                                            return record.Metadata.ArtifactId == artifactId;
                                        });
        if (found == m_Records.end())
            return nullptr;
        return &*found;
    }

    const RenderArtifactRecord* RenderArtifactRegistry::FindRecord(
        const std::string_view artifactId) const
    {
        const auto found = std::find_if(m_Records.begin(),
                                        m_Records.end(),
                                        [artifactId](const RenderArtifactRecord& record) {
                                            return record.Metadata.ArtifactId == artifactId;
                                        });
        if (found == m_Records.end())
            return nullptr;
        return &*found;
    }

    std::uint64_t RenderArtifactRegistry::NextRevision() noexcept
    {
        ++m_Revision;
        return m_Revision;
    }

    void RenderArtifactRegistry::AppendAudit(
        const RenderArtifactAuditAction action,
        const RenderArtifactRecord& record,
        const RenderArtifactPublicationState before,
        std::string provenance,
        std::string target,
        std::string label)
    {
        m_Audit.push_back(RenderArtifactAuditRecord{
            .Action = action,
            .ArtifactId = record.Metadata.ArtifactId,
            .Before = before,
            .After = record.State,
            .Provenance = std::move(provenance),
            .Target = std::move(target),
            .Label = std::move(label),
            .Revision = m_Revision,
        });
    }
}
