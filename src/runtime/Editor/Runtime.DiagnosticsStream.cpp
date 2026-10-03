module;
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

module Extrinsic.Runtime.DiagnosticsStream;
import Extrinsic.Core.Logging;
import Extrinsic.Runtime.DeviceBootstrap;
#if defined(EXTRINSIC_RUNTIME_HAS_PROMOTED_VULKAN)
import Extrinsic.Backends.Vulkan;
#endif

namespace Extrinsic::Runtime
{
    void EditorDiagnosticsStream::AttachDevice(const Core::Config::RenderConfig& config, const RHI::IDevice& device)
    {
        m_BootstrapConfig = config;
        m_Device = &device;
    }

    void EditorDiagnosticsStream::DetachDevice() noexcept { m_Device = nullptr; }

    EditorDeviceStatus EditorDiagnosticsStream::ReadDeviceStatus() const
    {
        if (!m_Device) return {};
#if defined(EXTRINSIC_RUNTIME_HAS_PROMOTED_VULKAN)
        constexpr bool compiled = true;
#else
        constexpr bool compiled = false;
#endif
        const auto selection = SelectRuntimeDeviceBackend(m_BootstrapConfig, compiled);
        EditorDeviceStatus result{
            .RequestedBackend = "vulkan",
            .ActualBackend = selection.UsePromotedVulkanDevice ? "vulkan" : "null",
            .FallbackReason = {},
            .IsOperational = m_Device->IsOperational(),
        };
        if (!selection.UsePromotedVulkanDevice)
            result.FallbackReason = !m_BootstrapConfig.EnablePromotedVulkanDevice ?
                "Promoted Vulkan is disabled in the startup configuration." :
                "Promoted Vulkan is not compiled into this build.";
#if defined(EXTRINSIC_RUNTIME_HAS_PROMOTED_VULKAN)
        if (selection.UsePromotedVulkanDevice)
        {
            const auto bootstrap = Backends::Vulkan::GetVulkanBootstrapDiagnosticsSnapshot();
            result.ValidationEnabled = bootstrap.ValidationEnabled;
            result.ValidationErrorCount = Backends::Vulkan::GetVulkanOperationalDiagnosticsSnapshot().VulkanValidationErrorCount;
            if (!result.IsOperational)
                result.FallbackReason = m_Device->IsDeviceLost() ? "Vulkan device lost." :
                    "Vulkan device is selected but its operational gate is not satisfied.";
        }
#endif
        return result;
    }

    std::uint64_t EditorDiagnosticsStream::AppendOperation(EditorOperationRecord record)
    {
        record.Sequence = ++m_OperationSequence;
        if (m_Operations.size() == 256) m_Operations.erase(m_Operations.begin());
        m_Operations.push_back(std::move(record));
        return m_OperationSequence;
    }

    void EditorDiagnosticsStream::UpdateOperation(const std::uint64_t sequence, EditorOperationRecord record)
    {
        for (auto& existing : m_Operations)
            if (existing.Sequence == sequence)
            {
                record.Sequence = sequence;
                existing = std::move(record);
                return;
            }
    }

    EditorDiagnosticsSnapshot ReadEditorDiagnostics(const EditorDiagnosticsStream* stream, const std::uint64_t cursor,
                                                     const EditorDiagnosticsFilter& filter)
    {
        const auto logs = Core::Log::TakeSnapshotSince(cursor, std::clamp(filter.Limit, std::size_t{1}, std::size_t{1000}),
                                                      filter.Levels, filter.Categories);
        EditorDiagnosticsSnapshot result{.NextCursor = logs.NextCursor, .Dropped = logs.Dropped,
                                         .ClearedThrough = logs.ClearedThrough, .CursorReset = logs.CursorReset};
        result.Entries.reserve(logs.Entries.size());
        for (const auto& entry : logs.Entries)
            result.Entries.push_back({entry.Sequence, entry.TimestampNs, static_cast<DiagnosticLevel>(entry.Lvl),
                                      entry.Category, entry.Message});
        if (stream)
        {
            result.DeviceStatus = stream->ReadDeviceStatus();
            result.OperationRecords = stream->OperationRecords();
        }
        return result;
    }

    void ClearEditorDiagnosticsLog() { Core::Log::ClearEntries(); }
}
