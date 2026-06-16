module;

#include <cstddef>
#include <cstdint>
#include <chrono>
#include <optional>
#include <string>
#include <vector>

export module Extrinsic.Runtime.AssetIngestStateMachine;

import Extrinsic.Asset.ImportRouter;
import Extrinsic.Asset.Registry;
import Extrinsic.Core.Error;
import Extrinsic.Core.StrongHandle;

export namespace Extrinsic::Runtime
{
    struct RuntimeAssetIngestTag;
    using RuntimeAssetIngestHandle = Core::StrongHandle<RuntimeAssetIngestTag>;

    enum class RuntimeAssetIngestSource : std::uint8_t
    {
        ManualImport,
        DroppedFile,
        Reimport,
    };

    enum class RuntimeAssetIngestPhase : std::uint8_t
    {
        Queued,
        RouteResolved,
        DecodeQueued,
        Decoding,
        AwaitingMainThreadApply,
        Applying,
        AwaitingGpuUpload,
        Complete,
        Failed,
        Cancelled,
    };

    enum class RuntimeAssetIngestDiagnostic : std::uint8_t
    {
        None,
        MissingPath,
        MissingFile,
        MissingExtension,
        UnsupportedExtension,
        AmbiguousPayloadKind,
        PayloadKindNotSupported,
        InvalidReimportTarget,
        DuplicateActiveRequest,
        DecodeFailed,
        CallbackFailed,
        MaterializationFailed,
        Cancelled,
        StaleCompletion,
        InvalidTransition,
        UnknownHandle,
    };

    enum class RuntimeAssetImportQueueStage : std::uint8_t
    {
        Queued,
        Routing,
        DecodeQueued,
        Decoding,
        MainThreadApply,
        GpuUpload,
        Complete,
        Failed,
        Cancelled,
    };

    enum class RuntimeAssetImportQueueTerminalStatus : std::uint8_t
    {
        None,
        Complete,
        Failed,
        Cancelled,
    };

    using RuntimeAssetImportQueueTimePoint =
        std::chrono::steady_clock::time_point;

    struct RuntimeAssetIngestRequest
    {
        RuntimeAssetIngestSource Source{RuntimeAssetIngestSource::ManualImport};
        std::string Path{};
        Assets::AssetPayloadKind PayloadKind{Assets::AssetPayloadKind::Unknown};
        Assets::AssetId ExistingAsset{};
        std::uint64_t CancellationGeneration{0u};
    };

    struct RuntimeAssetIngestResult
    {
        Assets::AssetPayloadKind PayloadKind{Assets::AssetPayloadKind::Unknown};
        Assets::AssetId Asset{};
        std::uint32_t PrimitiveEntitiesCreated{0u};
        std::uint32_t EmbeddedTextureAssetsCreated{0u};
        std::uint32_t GeneratedTextureAssetsCreated{0u};
        std::uint32_t TextureUploadRequests{0u};
        bool MaterializedModelScene{false};
        bool RequestedTextureUpload{false};
    };

    struct RuntimeAssetIngestRecord
    {
        RuntimeAssetIngestHandle Handle{};
        std::uint64_t Sequence{0u};
        RuntimeAssetIngestRequest Request{};
        RuntimeAssetIngestPhase Phase{RuntimeAssetIngestPhase::Queued};
        RuntimeAssetIngestDiagnostic Diagnostic{RuntimeAssetIngestDiagnostic::None};
        Core::ErrorCode Error{Core::ErrorCode::Success};
        Assets::AssetRouteStatus RouteStatus{Assets::AssetRouteStatus::Ready};
        std::optional<RuntimeAssetIngestResult> Result{};
        RuntimeAssetImportQueueTimePoint EnqueuedAt{};
        std::optional<RuntimeAssetImportQueueTimePoint> StartedAt{};
        std::optional<RuntimeAssetImportQueueTimePoint> FinishedAt{};
        RuntimeAssetImportQueueTimePoint LastUpdatedAt{};
        bool VisibleInQueue{true};
    };

    struct RuntimeAssetImportQueueEntry
    {
        RuntimeAssetIngestHandle Operation{};
        std::uint64_t Sequence{0u};
        RuntimeAssetIngestSource Source{RuntimeAssetIngestSource::ManualImport};
        std::string SourcePath{};
        std::string PathBasename{};
        Assets::AssetPayloadKind PayloadKind{Assets::AssetPayloadKind::Unknown};
        Assets::AssetId Asset{};
        RuntimeAssetImportQueueStage Stage{RuntimeAssetImportQueueStage::Queued};
        RuntimeAssetImportQueueTerminalStatus TerminalStatus{
            RuntimeAssetImportQueueTerminalStatus::None};
        RuntimeAssetImportQueueTimePoint EnqueuedAt{};
        std::optional<RuntimeAssetImportQueueTimePoint> StartedAt{};
        std::optional<RuntimeAssetImportQueueTimePoint> FinishedAt{};
        RuntimeAssetImportQueueTimePoint LastUpdatedAt{};
        bool ProgressDeterminate{true};
        float NormalizedProgress{0.0f};
        std::string StageText{};
        std::string DiagnosticText{};
        bool CanCancel{false};
        std::string CancelDisabledReason{"Cancellation is not available for this import stage."};
    };

    struct RuntimeAssetImportQueueSnapshot
    {
        std::vector<RuntimeAssetImportQueueEntry> Entries{};
        std::size_t ActiveCount{0u};
        std::size_t TerminalCount{0u};
        bool CanClearCompleted{false};
        std::string ClearCompletedDisabledReason{};
    };

    struct RuntimeAssetIngestTransition
    {
        RuntimeAssetIngestHandle Handle{};
        std::uint64_t Sequence{0u};
        RuntimeAssetIngestPhase Phase{RuntimeAssetIngestPhase::Failed};
        RuntimeAssetIngestDiagnostic Diagnostic{RuntimeAssetIngestDiagnostic::UnknownHandle};
        Core::ErrorCode Error{Core::ErrorCode::ResourceNotFound};
        bool Duplicate{false};
        bool Mutated{false};

        [[nodiscard]] bool Succeeded() const noexcept;
    };

    [[nodiscard]] bool IsTerminal(RuntimeAssetIngestPhase phase) noexcept;

    [[nodiscard]] const char* DebugNameForRuntimeAssetIngestSource(
        RuntimeAssetIngestSource source) noexcept;
    [[nodiscard]] const char* DebugNameForRuntimeAssetIngestPhase(
        RuntimeAssetIngestPhase phase) noexcept;
    [[nodiscard]] const char* DebugNameForRuntimeAssetIngestDiagnostic(
        RuntimeAssetIngestDiagnostic diagnostic) noexcept;
    [[nodiscard]] const char* DebugNameForRuntimeAssetImportQueueStage(
        RuntimeAssetImportQueueStage stage) noexcept;
    [[nodiscard]] const char* DebugNameForRuntimeAssetImportQueueTerminalStatus(
        RuntimeAssetImportQueueTerminalStatus status) noexcept;

    [[nodiscard]] RuntimeAssetIngestDiagnostic
        RuntimeAssetIngestDiagnosticFromRouteStatus(
            Assets::AssetRouteStatus status) noexcept;

    class RuntimeAssetIngestStateMachine
    {
    public:
        [[nodiscard]] RuntimeAssetIngestTransition Submit(
            RuntimeAssetIngestRequest request);

        [[nodiscard]] RuntimeAssetIngestTransition MarkMissingFile(
            RuntimeAssetIngestHandle handle);
        [[nodiscard]] RuntimeAssetIngestTransition MarkInvalidReimportTarget(
            RuntimeAssetIngestHandle handle,
            Core::ErrorCode error);
        [[nodiscard]] RuntimeAssetIngestTransition ResolveRoute(
            RuntimeAssetIngestHandle handle,
            const Assets::AssetRouteDiagnostic& diagnostic);
        [[nodiscard]] RuntimeAssetIngestTransition QueueDecode(
            RuntimeAssetIngestHandle handle);
        [[nodiscard]] RuntimeAssetIngestTransition MarkDecoding(
            RuntimeAssetIngestHandle handle);
        [[nodiscard]] RuntimeAssetIngestTransition CompleteDecode(
            RuntimeAssetIngestHandle handle,
            std::uint32_t completionGeneration);
        [[nodiscard]] RuntimeAssetIngestTransition FailDecode(
            RuntimeAssetIngestHandle handle,
            std::uint32_t completionGeneration,
            Core::ErrorCode error);
        [[nodiscard]] RuntimeAssetIngestTransition FailCallback(
            RuntimeAssetIngestHandle handle,
            Core::ErrorCode error);
        [[nodiscard]] RuntimeAssetIngestTransition BeginApply(
            RuntimeAssetIngestHandle handle);
        [[nodiscard]] RuntimeAssetIngestTransition BeginGpuUpload(
            RuntimeAssetIngestHandle handle);
        [[nodiscard]] RuntimeAssetIngestTransition CompleteApply(
            RuntimeAssetIngestHandle handle,
            std::uint32_t completionGeneration,
            RuntimeAssetIngestResult result);
        [[nodiscard]] RuntimeAssetIngestTransition FailApply(
            RuntimeAssetIngestHandle handle,
            std::uint32_t completionGeneration,
            Core::ErrorCode error);
        [[nodiscard]] RuntimeAssetIngestTransition Cancel(
            RuntimeAssetIngestHandle handle);

        [[nodiscard]] std::optional<RuntimeAssetIngestRecord> Snapshot(
            RuntimeAssetIngestHandle handle) const;
        [[nodiscard]] std::vector<RuntimeAssetIngestRecord> SnapshotAll() const;
        [[nodiscard]] RuntimeAssetImportQueueSnapshot SnapshotQueue() const;
        [[nodiscard]] std::size_t ClearCompletedQueueEntries();
        [[nodiscard]] std::size_t ActiveCount() const noexcept;
        [[nodiscard]] std::size_t TotalCount() const noexcept;

    private:
        [[nodiscard]] RuntimeAssetIngestTransition SetPhase(
            RuntimeAssetIngestHandle handle,
            RuntimeAssetIngestPhase expected,
            RuntimeAssetIngestPhase next);
        [[nodiscard]] RuntimeAssetIngestTransition FailWithDiagnostic(
            RuntimeAssetIngestHandle handle,
            RuntimeAssetIngestDiagnostic diagnostic,
            Core::ErrorCode error);

        std::vector<RuntimeAssetIngestRecord> m_Records{};
        std::uint64_t m_NextSequence{1u};
    };
}
