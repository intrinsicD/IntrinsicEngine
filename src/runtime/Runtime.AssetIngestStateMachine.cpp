module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

module Extrinsic.Runtime.AssetIngestStateMachine;

namespace Extrinsic::Runtime
{
    namespace
    {
        [[nodiscard]] bool SameRequestKey(
            const RuntimeAssetIngestRequest& lhs,
            const RuntimeAssetIngestRequest& rhs) noexcept
        {
            return lhs.Path == rhs.Path &&
                   lhs.PayloadKind == rhs.PayloadKind &&
                   lhs.ExistingAsset == rhs.ExistingAsset;
        }

        [[nodiscard]] RuntimeAssetIngestTransition MakeTransition(
            const RuntimeAssetIngestRecord& record,
            const RuntimeAssetIngestDiagnostic diagnostic,
            const Core::ErrorCode error,
            const bool mutated,
            const bool duplicate = false) noexcept
        {
            return RuntimeAssetIngestTransition{
                .Handle = record.Handle,
                .Sequence = record.Sequence,
                .Phase = record.Phase,
                .Diagnostic = diagnostic,
                .Error = error,
                .Duplicate = duplicate,
                .Mutated = mutated,
            };
        }

        [[nodiscard]] RuntimeAssetIngestTransition UnknownHandleTransition() noexcept
        {
            return RuntimeAssetIngestTransition{};
        }

        [[nodiscard]] RuntimeAssetIngestDiagnostic RouteDiagnosticFromStatus(
            const Assets::AssetRouteStatus status) noexcept
        {
            switch (status)
            {
            case Assets::AssetRouteStatus::Ready:
                return RuntimeAssetIngestDiagnostic::None;
            case Assets::AssetRouteStatus::MissingExtension:
                return RuntimeAssetIngestDiagnostic::MissingExtension;
            case Assets::AssetRouteStatus::UnsupportedExtension:
                return RuntimeAssetIngestDiagnostic::UnsupportedExtension;
            case Assets::AssetRouteStatus::AmbiguousPayloadKind:
                return RuntimeAssetIngestDiagnostic::AmbiguousPayloadKind;
            case Assets::AssetRouteStatus::PayloadKindNotSupported:
                return RuntimeAssetIngestDiagnostic::PayloadKindNotSupported;
            }
            return RuntimeAssetIngestDiagnostic::UnsupportedExtension;
        }
    }

    bool RuntimeAssetIngestTransition::Succeeded() const noexcept
    {
        return Diagnostic == RuntimeAssetIngestDiagnostic::None &&
               Error == Core::ErrorCode::Success;
    }

    bool IsTerminal(const RuntimeAssetIngestPhase phase) noexcept
    {
        switch (phase)
        {
        case RuntimeAssetIngestPhase::Complete:
        case RuntimeAssetIngestPhase::Failed:
        case RuntimeAssetIngestPhase::Cancelled:
            return true;
        case RuntimeAssetIngestPhase::Queued:
        case RuntimeAssetIngestPhase::RouteResolved:
        case RuntimeAssetIngestPhase::DecodeQueued:
        case RuntimeAssetIngestPhase::Decoding:
        case RuntimeAssetIngestPhase::AwaitingMainThreadApply:
        case RuntimeAssetIngestPhase::Applying:
            return false;
        }
        return true;
    }

    const char* DebugNameForRuntimeAssetIngestSource(
        const RuntimeAssetIngestSource source) noexcept
    {
        switch (source)
        {
        case RuntimeAssetIngestSource::ManualImport:
            return "ManualImport";
        case RuntimeAssetIngestSource::DroppedFile:
            return "DroppedFile";
        case RuntimeAssetIngestSource::Reimport:
            return "Reimport";
        }
        return "Unknown";
    }

    const char* DebugNameForRuntimeAssetIngestPhase(
        const RuntimeAssetIngestPhase phase) noexcept
    {
        switch (phase)
        {
        case RuntimeAssetIngestPhase::Queued:
            return "Queued";
        case RuntimeAssetIngestPhase::RouteResolved:
            return "RouteResolved";
        case RuntimeAssetIngestPhase::DecodeQueued:
            return "DecodeQueued";
        case RuntimeAssetIngestPhase::Decoding:
            return "Decoding";
        case RuntimeAssetIngestPhase::AwaitingMainThreadApply:
            return "AwaitingMainThreadApply";
        case RuntimeAssetIngestPhase::Applying:
            return "Applying";
        case RuntimeAssetIngestPhase::Complete:
            return "Complete";
        case RuntimeAssetIngestPhase::Failed:
            return "Failed";
        case RuntimeAssetIngestPhase::Cancelled:
            return "Cancelled";
        }
        return "Unknown";
    }

    const char* DebugNameForRuntimeAssetIngestDiagnostic(
        const RuntimeAssetIngestDiagnostic diagnostic) noexcept
    {
        switch (diagnostic)
        {
        case RuntimeAssetIngestDiagnostic::None:
            return "None";
        case RuntimeAssetIngestDiagnostic::MissingPath:
            return "MissingPath";
        case RuntimeAssetIngestDiagnostic::MissingFile:
            return "MissingFile";
        case RuntimeAssetIngestDiagnostic::MissingExtension:
            return "MissingExtension";
        case RuntimeAssetIngestDiagnostic::UnsupportedExtension:
            return "UnsupportedExtension";
        case RuntimeAssetIngestDiagnostic::AmbiguousPayloadKind:
            return "AmbiguousPayloadKind";
        case RuntimeAssetIngestDiagnostic::PayloadKindNotSupported:
            return "PayloadKindNotSupported";
        case RuntimeAssetIngestDiagnostic::InvalidReimportTarget:
            return "InvalidReimportTarget";
        case RuntimeAssetIngestDiagnostic::DuplicateActiveRequest:
            return "DuplicateActiveRequest";
        case RuntimeAssetIngestDiagnostic::DecodeFailed:
            return "DecodeFailed";
        case RuntimeAssetIngestDiagnostic::CallbackFailed:
            return "CallbackFailed";
        case RuntimeAssetIngestDiagnostic::MaterializationFailed:
            return "MaterializationFailed";
        case RuntimeAssetIngestDiagnostic::Cancelled:
            return "Cancelled";
        case RuntimeAssetIngestDiagnostic::StaleCompletion:
            return "StaleCompletion";
        case RuntimeAssetIngestDiagnostic::InvalidTransition:
            return "InvalidTransition";
        case RuntimeAssetIngestDiagnostic::UnknownHandle:
            return "UnknownHandle";
        }
        return "Unknown";
    }

    RuntimeAssetIngestDiagnostic RuntimeAssetIngestDiagnosticFromRouteStatus(
        const Assets::AssetRouteStatus status) noexcept
    {
        return RouteDiagnosticFromStatus(status);
    }

    RuntimeAssetIngestTransition RuntimeAssetIngestStateMachine::Submit(
        RuntimeAssetIngestRequest request)
    {
        for (const RuntimeAssetIngestRecord& record : m_Records)
        {
            if (!IsTerminal(record.Phase) &&
                SameRequestKey(record.Request, request))
            {
                return MakeTransition(
                    record,
                    RuntimeAssetIngestDiagnostic::DuplicateActiveRequest,
                    Core::ErrorCode::ResourceBusy,
                    false,
                    true);
            }
        }

        const std::uint32_t index = static_cast<std::uint32_t>(m_Records.size());
        RuntimeAssetIngestRecord record{};
        record.Handle = RuntimeAssetIngestHandle{index, 1u};
        record.Sequence = m_NextSequence++;
        record.Request = std::move(request);

        RuntimeAssetIngestDiagnostic diagnostic = RuntimeAssetIngestDiagnostic::None;
        Core::ErrorCode error = Core::ErrorCode::Success;
        if (record.Request.Source == RuntimeAssetIngestSource::Reimport &&
            !record.Request.ExistingAsset.IsValid())
        {
            record.Phase = RuntimeAssetIngestPhase::Failed;
            record.Diagnostic = RuntimeAssetIngestDiagnostic::InvalidReimportTarget;
            record.Error = Core::ErrorCode::InvalidArgument;
            diagnostic = record.Diagnostic;
            error = record.Error;
        }
        else if (record.Request.Path.empty())
        {
            record.Phase = RuntimeAssetIngestPhase::Failed;
            record.Diagnostic = RuntimeAssetIngestDiagnostic::MissingPath;
            record.Error = Core::ErrorCode::InvalidPath;
            diagnostic = record.Diagnostic;
            error = record.Error;
        }

        m_Records.push_back(record);
        return MakeTransition(m_Records.back(), diagnostic, error, true);
    }

    RuntimeAssetIngestTransition RuntimeAssetIngestStateMachine::MarkMissingFile(
        const RuntimeAssetIngestHandle handle)
    {
        return FailWithDiagnostic(
            handle,
            RuntimeAssetIngestDiagnostic::MissingFile,
            Core::ErrorCode::FileNotFound);
    }

    RuntimeAssetIngestTransition RuntimeAssetIngestStateMachine::MarkInvalidReimportTarget(
        const RuntimeAssetIngestHandle handle,
        const Core::ErrorCode error)
    {
        return FailWithDiagnostic(
            handle,
            RuntimeAssetIngestDiagnostic::InvalidReimportTarget,
            error == Core::ErrorCode::Success ? Core::ErrorCode::InvalidArgument : error);
    }

    RuntimeAssetIngestTransition RuntimeAssetIngestStateMachine::ResolveRoute(
        const RuntimeAssetIngestHandle handle,
        const Assets::AssetRouteDiagnostic& diagnostic)
    {
        if (!handle.IsValid() || handle.Index >= m_Records.size())
            return UnknownHandleTransition();

        RuntimeAssetIngestRecord& record = m_Records[handle.Index];
        if (record.Handle != handle)
            return UnknownHandleTransition();
        if (record.Phase != RuntimeAssetIngestPhase::Queued)
        {
            return MakeTransition(
                record,
                RuntimeAssetIngestDiagnostic::InvalidTransition,
                Core::ErrorCode::InvalidState,
                false);
        }

        record.RouteStatus = diagnostic.Status;
        const RuntimeAssetIngestDiagnostic mapped =
            RouteDiagnosticFromStatus(diagnostic.Status);
        if (mapped != RuntimeAssetIngestDiagnostic::None)
        {
            record.Phase = RuntimeAssetIngestPhase::Failed;
            record.Diagnostic = mapped;
            record.Error = diagnostic.Error;
            return MakeTransition(record, mapped, diagnostic.Error, true);
        }

        record.Phase = RuntimeAssetIngestPhase::RouteResolved;
        record.Diagnostic = RuntimeAssetIngestDiagnostic::None;
        record.Error = Core::ErrorCode::Success;
        return MakeTransition(
            record,
            RuntimeAssetIngestDiagnostic::None,
            Core::ErrorCode::Success,
            true);
    }

    RuntimeAssetIngestTransition RuntimeAssetIngestStateMachine::QueueDecode(
        const RuntimeAssetIngestHandle handle)
    {
        return SetPhase(
            handle,
            RuntimeAssetIngestPhase::RouteResolved,
            RuntimeAssetIngestPhase::DecodeQueued);
    }

    RuntimeAssetIngestTransition RuntimeAssetIngestStateMachine::MarkDecoding(
        const RuntimeAssetIngestHandle handle)
    {
        return SetPhase(
            handle,
            RuntimeAssetIngestPhase::DecodeQueued,
            RuntimeAssetIngestPhase::Decoding);
    }

    RuntimeAssetIngestTransition RuntimeAssetIngestStateMachine::CompleteDecode(
        const RuntimeAssetIngestHandle handle,
        const std::uint32_t completionGeneration)
    {
        if (!handle.IsValid() || handle.Index >= m_Records.size())
            return UnknownHandleTransition();

        RuntimeAssetIngestRecord& record = m_Records[handle.Index];
        if (record.Handle != handle)
            return UnknownHandleTransition();
        if (record.Handle.Generation != completionGeneration ||
            IsTerminal(record.Phase))
        {
            return MakeTransition(
                record,
                RuntimeAssetIngestDiagnostic::StaleCompletion,
                Core::ErrorCode::InvalidState,
                false);
        }
        if (record.Phase != RuntimeAssetIngestPhase::Decoding &&
            record.Phase != RuntimeAssetIngestPhase::DecodeQueued)
        {
            return MakeTransition(
                record,
                RuntimeAssetIngestDiagnostic::InvalidTransition,
                Core::ErrorCode::InvalidState,
                false);
        }

        record.Phase = RuntimeAssetIngestPhase::AwaitingMainThreadApply;
        record.Diagnostic = RuntimeAssetIngestDiagnostic::None;
        record.Error = Core::ErrorCode::Success;
        return MakeTransition(
            record,
            RuntimeAssetIngestDiagnostic::None,
            Core::ErrorCode::Success,
            true);
    }

    RuntimeAssetIngestTransition RuntimeAssetIngestStateMachine::FailDecode(
        const RuntimeAssetIngestHandle handle,
        const std::uint32_t completionGeneration,
        const Core::ErrorCode error)
    {
        if (!handle.IsValid() || handle.Index >= m_Records.size())
            return UnknownHandleTransition();

        RuntimeAssetIngestRecord& record = m_Records[handle.Index];
        if (record.Handle != handle)
            return UnknownHandleTransition();
        if (record.Handle.Generation != completionGeneration ||
            IsTerminal(record.Phase))
        {
            return MakeTransition(
                record,
                RuntimeAssetIngestDiagnostic::StaleCompletion,
                Core::ErrorCode::InvalidState,
                false);
        }
        if (record.Phase != RuntimeAssetIngestPhase::Decoding &&
            record.Phase != RuntimeAssetIngestPhase::DecodeQueued)
        {
            return MakeTransition(
                record,
                RuntimeAssetIngestDiagnostic::InvalidTransition,
                Core::ErrorCode::InvalidState,
                false);
        }

        record.Phase = RuntimeAssetIngestPhase::Failed;
        record.Diagnostic = RuntimeAssetIngestDiagnostic::DecodeFailed;
        record.Error = error;
        return MakeTransition(
            record,
            RuntimeAssetIngestDiagnostic::DecodeFailed,
            error,
            true);
    }

    RuntimeAssetIngestTransition RuntimeAssetIngestStateMachine::FailCallback(
        const RuntimeAssetIngestHandle handle,
        const Core::ErrorCode error)
    {
        return FailWithDiagnostic(
            handle,
            RuntimeAssetIngestDiagnostic::CallbackFailed,
            error);
    }

    RuntimeAssetIngestTransition RuntimeAssetIngestStateMachine::BeginApply(
        const RuntimeAssetIngestHandle handle)
    {
        return SetPhase(
            handle,
            RuntimeAssetIngestPhase::AwaitingMainThreadApply,
            RuntimeAssetIngestPhase::Applying);
    }

    RuntimeAssetIngestTransition RuntimeAssetIngestStateMachine::CompleteApply(
        const RuntimeAssetIngestHandle handle,
        const std::uint32_t completionGeneration,
        RuntimeAssetIngestResult result)
    {
        if (!handle.IsValid() || handle.Index >= m_Records.size())
            return UnknownHandleTransition();

        RuntimeAssetIngestRecord& record = m_Records[handle.Index];
        if (record.Handle != handle)
            return UnknownHandleTransition();
        if (record.Handle.Generation != completionGeneration ||
            IsTerminal(record.Phase))
        {
            return MakeTransition(
                record,
                RuntimeAssetIngestDiagnostic::StaleCompletion,
                Core::ErrorCode::InvalidState,
                false);
        }
        if (record.Phase != RuntimeAssetIngestPhase::Applying)
        {
            return MakeTransition(
                record,
                RuntimeAssetIngestDiagnostic::InvalidTransition,
                Core::ErrorCode::InvalidState,
                false);
        }

        record.Result = std::move(result);
        record.Phase = RuntimeAssetIngestPhase::Complete;
        record.Diagnostic = RuntimeAssetIngestDiagnostic::None;
        record.Error = Core::ErrorCode::Success;
        return MakeTransition(
            record,
            RuntimeAssetIngestDiagnostic::None,
            Core::ErrorCode::Success,
            true);
    }

    RuntimeAssetIngestTransition RuntimeAssetIngestStateMachine::FailApply(
        const RuntimeAssetIngestHandle handle,
        const std::uint32_t completionGeneration,
        const Core::ErrorCode error)
    {
        if (!handle.IsValid() || handle.Index >= m_Records.size())
            return UnknownHandleTransition();

        RuntimeAssetIngestRecord& record = m_Records[handle.Index];
        if (record.Handle != handle)
            return UnknownHandleTransition();
        if (record.Handle.Generation != completionGeneration ||
            IsTerminal(record.Phase))
        {
            return MakeTransition(
                record,
                RuntimeAssetIngestDiagnostic::StaleCompletion,
                Core::ErrorCode::InvalidState,
                false);
        }
        if (record.Phase != RuntimeAssetIngestPhase::Applying)
        {
            return MakeTransition(
                record,
                RuntimeAssetIngestDiagnostic::InvalidTransition,
                Core::ErrorCode::InvalidState,
                false);
        }

        record.Phase = RuntimeAssetIngestPhase::Failed;
        record.Diagnostic = RuntimeAssetIngestDiagnostic::MaterializationFailed;
        record.Error = error;
        return MakeTransition(
            record,
            RuntimeAssetIngestDiagnostic::MaterializationFailed,
            error,
            true);
    }

    RuntimeAssetIngestTransition RuntimeAssetIngestStateMachine::Cancel(
        const RuntimeAssetIngestHandle handle)
    {
        if (!handle.IsValid() || handle.Index >= m_Records.size())
            return UnknownHandleTransition();

        RuntimeAssetIngestRecord& record = m_Records[handle.Index];
        if (record.Handle != handle)
            return UnknownHandleTransition();
        if (IsTerminal(record.Phase))
        {
            return MakeTransition(
                record,
                RuntimeAssetIngestDiagnostic::InvalidTransition,
                Core::ErrorCode::InvalidState,
                false);
        }

        record.Phase = RuntimeAssetIngestPhase::Cancelled;
        record.Diagnostic = RuntimeAssetIngestDiagnostic::Cancelled;
        record.Error = Core::ErrorCode::InvalidState;
        return MakeTransition(
            record,
            RuntimeAssetIngestDiagnostic::Cancelled,
            Core::ErrorCode::InvalidState,
            true);
    }

    std::optional<RuntimeAssetIngestRecord>
    RuntimeAssetIngestStateMachine::Snapshot(
        const RuntimeAssetIngestHandle handle) const
    {
        if (!handle.IsValid() || handle.Index >= m_Records.size())
            return std::nullopt;

        const RuntimeAssetIngestRecord& record = m_Records[handle.Index];
        if (record.Handle != handle)
            return std::nullopt;
        return record;
    }

    std::vector<RuntimeAssetIngestRecord>
    RuntimeAssetIngestStateMachine::SnapshotAll() const
    {
        return m_Records;
    }

    std::size_t RuntimeAssetIngestStateMachine::ActiveCount() const noexcept
    {
        return static_cast<std::size_t>(
            std::count_if(
                m_Records.begin(),
                m_Records.end(),
                [](const RuntimeAssetIngestRecord& record)
                {
                    return !IsTerminal(record.Phase);
                }));
    }

    std::size_t RuntimeAssetIngestStateMachine::TotalCount() const noexcept
    {
        return m_Records.size();
    }

    RuntimeAssetIngestTransition RuntimeAssetIngestStateMachine::SetPhase(
        const RuntimeAssetIngestHandle handle,
        const RuntimeAssetIngestPhase expected,
        const RuntimeAssetIngestPhase next)
    {
        if (!handle.IsValid() || handle.Index >= m_Records.size())
            return UnknownHandleTransition();

        RuntimeAssetIngestRecord& record = m_Records[handle.Index];
        if (record.Handle != handle)
            return UnknownHandleTransition();
        if (record.Phase != expected || IsTerminal(record.Phase))
        {
            return MakeTransition(
                record,
                RuntimeAssetIngestDiagnostic::InvalidTransition,
                Core::ErrorCode::InvalidState,
                false);
        }

        record.Phase = next;
        record.Diagnostic = RuntimeAssetIngestDiagnostic::None;
        record.Error = Core::ErrorCode::Success;
        return MakeTransition(
            record,
            RuntimeAssetIngestDiagnostic::None,
            Core::ErrorCode::Success,
            true);
    }

    RuntimeAssetIngestTransition
    RuntimeAssetIngestStateMachine::FailWithDiagnostic(
        const RuntimeAssetIngestHandle handle,
        const RuntimeAssetIngestDiagnostic diagnostic,
        const Core::ErrorCode error)
    {
        if (!handle.IsValid() || handle.Index >= m_Records.size())
            return UnknownHandleTransition();

        RuntimeAssetIngestRecord& record = m_Records[handle.Index];
        if (record.Handle != handle)
            return UnknownHandleTransition();
        if (IsTerminal(record.Phase))
        {
            return MakeTransition(
                record,
                RuntimeAssetIngestDiagnostic::InvalidTransition,
                Core::ErrorCode::InvalidState,
                false);
        }

        record.Phase = RuntimeAssetIngestPhase::Failed;
        record.Diagnostic = diagnostic;
        record.Error = error;
        return MakeTransition(record, diagnostic, error, true);
    }
}
