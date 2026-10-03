// Runtime-owned diagnostics shared by editor panels and agent readers; backend types
// and the core logging API remain below the app boundary.
module;
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

export module Extrinsic.Runtime.DiagnosticsStream;
import Extrinsic.Core.Config.Render;
import Extrinsic.RHI.Device;

export namespace Extrinsic::Runtime
{
    enum class DiagnosticLevel : std::uint8_t { Info, Warning, Error, Debug };
    enum class DiagnosticOperationSource : std::uint8_t { Editor, AgentCli };
    enum class DiagnosticOperationStatus : std::uint8_t { Pending, Succeeded, Failed, Abandoned };

    struct EditorDiagnosticLogEntry
    {
        std::uint64_t Sequence{};
        std::uint64_t TimestampNs{};
        DiagnosticLevel Level{};
        std::string Category{};
        std::string Message{};
    };

    struct EditorDiagnosticsFilter
    {
        std::uint8_t Levels{0x0f};
        std::vector<std::string> Categories{};
        std::size_t Limit{1000};
    };

    struct EditorDeviceStatus
    {
        std::string RequestedBackend{"unavailable"};
        std::string ActualBackend{"unavailable"};
        std::string FallbackReason{"Runtime device is not attached."};
        bool IsOperational{false};
        bool ValidationEnabled{false};
        std::uint64_t ValidationErrorCount{};
    };

    struct EditorOperationRecord
    {
        std::uint64_t Sequence{};
        std::string Name{};
        DiagnosticOperationSource Source{DiagnosticOperationSource::AgentCli};
        DiagnosticOperationStatus Status{DiagnosticOperationStatus::Pending};
        std::uint64_t WallTimeUs{};
        // Process-wide tracked allocation bytes during the invocation, not exclusive
        // ownership by this operation; overlapping deferred calls can overlap counts.
        std::uint64_t AllocationDeltaBytes{};
        std::string RequestedBackend{};
        std::string ActualBackend{};
        std::string BackendFallbackReason{};
    };

    // Main-thread service. Engine owns it beyond all agent continuations and detaches
    // the borrowed device before teardown. Bootstrap config is frozen at attachment.
    class EditorDiagnosticsStream
    {
    public:
        void AttachDevice(const Core::Config::RenderConfig& config, const RHI::IDevice& device);
        void DetachDevice() noexcept;
        [[nodiscard]] EditorDeviceStatus ReadDeviceStatus() const;
        [[nodiscard]] std::uint64_t AppendOperation(EditorOperationRecord record);
        void UpdateOperation(std::uint64_t sequence, EditorOperationRecord record);
        [[nodiscard]] const std::vector<EditorOperationRecord>& OperationRecords() const noexcept { return m_Operations; }
    private:
        const RHI::IDevice* m_Device{};
        Core::Config::RenderConfig m_BootstrapConfig{};
        std::vector<EditorOperationRecord> m_Operations{};
        std::uint64_t m_OperationSequence{};
    };

    struct EditorDiagnosticsSnapshot
    {
        std::vector<EditorDiagnosticLogEntry> Entries{};
        std::uint64_t NextCursor{};
        std::uint64_t Dropped{};
        std::uint64_t ClearedThrough{};
        bool CursorReset{false};
        EditorDeviceStatus DeviceStatus{};
        // The latest 256 invocations, including pending records updated in place.
        std::vector<EditorOperationRecord> OperationRecords{};
    };

    [[nodiscard]] EditorDiagnosticsSnapshot ReadEditorDiagnostics(
        const EditorDiagnosticsStream* stream, std::uint64_t cursor, const EditorDiagnosticsFilter& filter = {});
    void ClearEditorDiagnosticsLog();
}
