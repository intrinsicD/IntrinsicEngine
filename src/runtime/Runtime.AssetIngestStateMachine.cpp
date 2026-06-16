module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module Extrinsic.Runtime.AssetIngestStateMachine;

import Extrinsic.Core.Error;

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

        [[nodiscard]] RuntimeAssetImportQueueTimePoint Now() noexcept
        {
            return std::chrono::steady_clock::now();
        }

        void MarkUpdated(RuntimeAssetIngestRecord& record) noexcept
        {
            record.LastUpdatedAt = Now();
        }

        void MarkStarted(RuntimeAssetIngestRecord& record) noexcept
        {
            const RuntimeAssetImportQueueTimePoint now = Now();
            if (!record.StartedAt.has_value())
            {
                record.StartedAt = now;
            }
            record.LastUpdatedAt = now;
        }

        void MarkFinished(RuntimeAssetIngestRecord& record) noexcept
        {
            const RuntimeAssetImportQueueTimePoint now = Now();
            if (!record.StartedAt.has_value())
            {
                record.StartedAt = now;
            }
            record.FinishedAt = now;
            record.LastUpdatedAt = now;
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

        [[nodiscard]] std::string PathBasename(const std::string_view path)
        {
            if (path.empty())
            {
                return {};
            }
            const std::size_t slash = path.find_last_of("/\\");
            const std::size_t begin = slash == std::string_view::npos
                ? 0u
                : slash + 1u;
            if (begin >= path.size())
            {
                return {};
            }
            return std::string(path.substr(begin));
        }

        [[nodiscard]] RuntimeAssetImportQueueTerminalStatus TerminalStatusForPhase(
            const RuntimeAssetIngestPhase phase) noexcept
        {
            switch (phase)
            {
            case RuntimeAssetIngestPhase::Complete:
                return RuntimeAssetImportQueueTerminalStatus::Complete;
            case RuntimeAssetIngestPhase::Failed:
                return RuntimeAssetImportQueueTerminalStatus::Failed;
            case RuntimeAssetIngestPhase::Cancelled:
                return RuntimeAssetImportQueueTerminalStatus::Cancelled;
            case RuntimeAssetIngestPhase::Queued:
            case RuntimeAssetIngestPhase::RouteResolved:
            case RuntimeAssetIngestPhase::DecodeQueued:
            case RuntimeAssetIngestPhase::Decoding:
            case RuntimeAssetIngestPhase::AwaitingMainThreadApply:
            case RuntimeAssetIngestPhase::Applying:
            case RuntimeAssetIngestPhase::AwaitingGpuUpload:
                return RuntimeAssetImportQueueTerminalStatus::None;
            }
            return RuntimeAssetImportQueueTerminalStatus::Failed;
        }

        [[nodiscard]] RuntimeAssetImportQueueStage StageForPhase(
            const RuntimeAssetIngestPhase phase) noexcept
        {
            switch (phase)
            {
            case RuntimeAssetIngestPhase::Queued:
                return RuntimeAssetImportQueueStage::Queued;
            case RuntimeAssetIngestPhase::RouteResolved:
                return RuntimeAssetImportQueueStage::Routing;
            case RuntimeAssetIngestPhase::DecodeQueued:
                return RuntimeAssetImportQueueStage::DecodeQueued;
            case RuntimeAssetIngestPhase::Decoding:
                return RuntimeAssetImportQueueStage::Decoding;
            case RuntimeAssetIngestPhase::AwaitingMainThreadApply:
            case RuntimeAssetIngestPhase::Applying:
                return RuntimeAssetImportQueueStage::MainThreadApply;
            case RuntimeAssetIngestPhase::AwaitingGpuUpload:
                return RuntimeAssetImportQueueStage::GpuUpload;
            case RuntimeAssetIngestPhase::Complete:
                return RuntimeAssetImportQueueStage::Complete;
            case RuntimeAssetIngestPhase::Failed:
                return RuntimeAssetImportQueueStage::Failed;
            case RuntimeAssetIngestPhase::Cancelled:
                return RuntimeAssetImportQueueStage::Cancelled;
            }
            return RuntimeAssetImportQueueStage::Failed;
        }

        [[nodiscard]] bool IsIndeterminateProgressStage(
            const RuntimeAssetImportQueueStage stage) noexcept
        {
            switch (stage)
            {
            case RuntimeAssetImportQueueStage::Decoding:
            case RuntimeAssetImportQueueStage::MainThreadApply:
            case RuntimeAssetImportQueueStage::GpuUpload:
                return true;
            case RuntimeAssetImportQueueStage::Queued:
            case RuntimeAssetImportQueueStage::Routing:
            case RuntimeAssetImportQueueStage::DecodeQueued:
            case RuntimeAssetImportQueueStage::Complete:
            case RuntimeAssetImportQueueStage::Failed:
            case RuntimeAssetImportQueueStage::Cancelled:
                return false;
            }
            return false;
        }

        [[nodiscard]] float ProgressForStage(
            const RuntimeAssetImportQueueStage stage) noexcept
        {
            switch (stage)
            {
            case RuntimeAssetImportQueueStage::Queued:
                return 0.0f;
            case RuntimeAssetImportQueueStage::Routing:
                return 0.15f;
            case RuntimeAssetImportQueueStage::DecodeQueued:
                return 0.25f;
            case RuntimeAssetImportQueueStage::Decoding:
                return 0.45f;
            case RuntimeAssetImportQueueStage::MainThreadApply:
                return 0.80f;
            case RuntimeAssetImportQueueStage::GpuUpload:
                return 0.90f;
            case RuntimeAssetImportQueueStage::Complete:
            case RuntimeAssetImportQueueStage::Failed:
            case RuntimeAssetImportQueueStage::Cancelled:
                return 1.0f;
            }
            return 0.0f;
        }

        [[nodiscard]] std::string DiagnosticTextForRecord(
            const RuntimeAssetIngestRecord& record)
        {
            if (record.Diagnostic == RuntimeAssetIngestDiagnostic::None &&
                record.Error == Core::ErrorCode::Success)
            {
                return {};
            }

            std::string text =
                DebugNameForRuntimeAssetIngestDiagnostic(record.Diagnostic);
            if (record.Error != Core::ErrorCode::Success)
            {
                text += ": ";
                text += Core::Error::ToString(record.Error);
            }
            return text;
        }

        [[nodiscard]] RuntimeAssetImportQueueEntry QueueEntryForRecord(
            const RuntimeAssetIngestRecord& record)
        {
            const RuntimeAssetImportQueueStage stage = StageForPhase(record.Phase);
            RuntimeAssetImportQueueEntry entry{};
            entry.Operation = record.Handle;
            entry.Sequence = record.Sequence;
            entry.Source = record.Request.Source;
            entry.SourcePath = record.Request.Path;
            entry.PathBasename = PathBasename(record.Request.Path);
            entry.PayloadKind = record.Result.has_value()
                ? record.Result->PayloadKind
                : record.Request.PayloadKind;
            entry.Asset = record.Result.has_value()
                ? record.Result->Asset
                : record.Request.ExistingAsset;
            entry.Stage = stage;
            entry.TerminalStatus = TerminalStatusForPhase(record.Phase);
            entry.EnqueuedAt = record.EnqueuedAt;
            entry.StartedAt = record.StartedAt;
            entry.FinishedAt = record.FinishedAt;
            entry.LastUpdatedAt = record.LastUpdatedAt;
            entry.ProgressDeterminate = !IsIndeterminateProgressStage(stage);
            entry.NormalizedProgress = ProgressForStage(stage);
            entry.StageText = DebugNameForRuntimeAssetImportQueueStage(stage);
            entry.DiagnosticText = DiagnosticTextForRecord(record);
            return entry;
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
        case RuntimeAssetIngestPhase::AwaitingGpuUpload:
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
        case RuntimeAssetIngestPhase::AwaitingGpuUpload:
            return "AwaitingGpuUpload";
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

    const char* DebugNameForRuntimeAssetImportQueueStage(
        const RuntimeAssetImportQueueStage stage) noexcept
    {
        switch (stage)
        {
        case RuntimeAssetImportQueueStage::Queued:
            return "Queued";
        case RuntimeAssetImportQueueStage::Routing:
            return "Routing";
        case RuntimeAssetImportQueueStage::DecodeQueued:
            return "DecodeQueued";
        case RuntimeAssetImportQueueStage::Decoding:
            return "Decoding";
        case RuntimeAssetImportQueueStage::MainThreadApply:
            return "MainThreadApply";
        case RuntimeAssetImportQueueStage::GpuUpload:
            return "GpuUpload";
        case RuntimeAssetImportQueueStage::Complete:
            return "Complete";
        case RuntimeAssetImportQueueStage::Failed:
            return "Failed";
        case RuntimeAssetImportQueueStage::Cancelled:
            return "Cancelled";
        }
        return "Unknown";
    }

    const char* DebugNameForRuntimeAssetImportQueueTerminalStatus(
        const RuntimeAssetImportQueueTerminalStatus status) noexcept
    {
        switch (status)
        {
        case RuntimeAssetImportQueueTerminalStatus::None:
            return "None";
        case RuntimeAssetImportQueueTerminalStatus::Complete:
            return "Complete";
        case RuntimeAssetImportQueueTerminalStatus::Failed:
            return "Failed";
        case RuntimeAssetImportQueueTerminalStatus::Cancelled:
            return "Cancelled";
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
        record.EnqueuedAt = Now();
        record.LastUpdatedAt = record.EnqueuedAt;
        if (record.Request.Source == RuntimeAssetIngestSource::Reimport &&
            !record.Request.ExistingAsset.IsValid())
        {
            record.Phase = RuntimeAssetIngestPhase::Failed;
            record.Diagnostic = RuntimeAssetIngestDiagnostic::InvalidReimportTarget;
            record.Error = Core::ErrorCode::InvalidArgument;
            MarkFinished(record);
            diagnostic = record.Diagnostic;
            error = record.Error;
        }
        else if (record.Request.Path.empty())
        {
            record.Phase = RuntimeAssetIngestPhase::Failed;
            record.Diagnostic = RuntimeAssetIngestDiagnostic::MissingPath;
            record.Error = Core::ErrorCode::InvalidPath;
            MarkFinished(record);
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
            MarkFinished(record);
            return MakeTransition(record, mapped, diagnostic.Error, true);
        }

        record.Phase = RuntimeAssetIngestPhase::RouteResolved;
        record.Diagnostic = RuntimeAssetIngestDiagnostic::None;
        record.Error = Core::ErrorCode::Success;
        MarkUpdated(record);
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
        MarkUpdated(record);
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
        MarkFinished(record);
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

    RuntimeAssetIngestTransition RuntimeAssetIngestStateMachine::BeginGpuUpload(
        const RuntimeAssetIngestHandle handle)
    {
        return SetPhase(
            handle,
            RuntimeAssetIngestPhase::Applying,
            RuntimeAssetIngestPhase::AwaitingGpuUpload);
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
        if (record.Phase != RuntimeAssetIngestPhase::Applying &&
            record.Phase != RuntimeAssetIngestPhase::AwaitingGpuUpload)
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
        MarkFinished(record);
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
        if (record.Phase != RuntimeAssetIngestPhase::Applying &&
            record.Phase != RuntimeAssetIngestPhase::AwaitingGpuUpload)
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
        MarkFinished(record);
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
        MarkFinished(record);
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

    RuntimeAssetImportQueueSnapshot
    RuntimeAssetIngestStateMachine::SnapshotQueue() const
    {
        RuntimeAssetImportQueueSnapshot snapshot{};
        for (const RuntimeAssetIngestRecord& record : m_Records)
        {
            if (!record.VisibleInQueue)
            {
                continue;
            }

            if (IsTerminal(record.Phase))
            {
                ++snapshot.TerminalCount;
            }
            else
            {
                ++snapshot.ActiveCount;
            }
            snapshot.Entries.push_back(QueueEntryForRecord(record));
        }
        snapshot.CanClearCompleted = snapshot.TerminalCount > 0u;
        if (!snapshot.CanClearCompleted)
        {
            snapshot.ClearCompletedDisabledReason =
                "No completed, failed, or cancelled imports are visible.";
        }
        return snapshot;
    }

    std::size_t RuntimeAssetIngestStateMachine::ClearCompletedQueueEntries()
    {
        std::size_t cleared = 0u;
        for (RuntimeAssetIngestRecord& record : m_Records)
        {
            if (!record.VisibleInQueue || !IsTerminal(record.Phase))
            {
                continue;
            }
            record.VisibleInQueue = false;
            ++cleared;
        }
        return cleared;
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
        if (next == RuntimeAssetIngestPhase::Decoding ||
            next == RuntimeAssetIngestPhase::Applying)
        {
            MarkStarted(record);
        }
        else
        {
            MarkUpdated(record);
        }
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
        MarkFinished(record);
        return MakeTransition(record, diagnostic, error, true);
    }
}
