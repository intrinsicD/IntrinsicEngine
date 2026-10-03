module;
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <nlohmann/json.hpp>

module Extrinsic.Runtime.AgentOperations;
import Extrinsic.Runtime.DiagnosticsStream;

namespace Extrinsic::Runtime
{
    namespace
    {
        using Json = nlohmann::json;

        Json DeviceJson(const EditorDeviceStatus& device)
        {
            return {{"requested_backend", device.RequestedBackend}, {"actual_backend", device.ActualBackend},
                    {"fallback_reason", device.FallbackReason}, {"is_operational", device.IsOperational},
                    {"validation_enabled", device.ValidationEnabled}, {"validation_error_count", device.ValidationErrorCount}};
        }

        AgentOperationOutcome ReadDiagnostics(const AgentOperationContext& context, const std::string_view arguments)
        {
            const auto args = Json::parse(arguments, nullptr, false);
            auto fail = [] { return AgentOperationOutcome{.IsError = true,
                .Text = "Expected since_cursor >= 0, levels [0=info,1=warning,2=error,3=debug], categories [strings], and limit 1..1000.",
                .ErrorCode = "invalid_arguments"}; };
            if (!args.is_object()) return fail();
            for (auto it = args.begin(); it != args.end(); ++it)
                if (it.key() != "since_cursor" && it.key() != "levels" && it.key() != "categories" && it.key() != "limit") return fail();
            std::uint64_t cursor = 0;
            EditorDiagnosticsFilter filter{};
            if (args.contains("since_cursor"))
            {
                if (!args["since_cursor"].is_number_unsigned()) return fail();
                cursor = args["since_cursor"].get<std::uint64_t>();
            }
            if (args.contains("limit"))
            {
                if (!args["limit"].is_number_unsigned()) return fail();
                const auto limit = args["limit"].get<std::uint64_t>();
                if (limit == 0 || limit > 1000) return fail();
                filter.Limit = static_cast<std::size_t>(limit);
            }
            if (args.contains("levels"))
            {
                if (!args["levels"].is_array()) return fail();
                filter.Levels = 0;
                for (const auto& level : args["levels"])
                {
                    if (!level.is_number_unsigned() || level.get<std::uint64_t>() > 3) return fail();
                    filter.Levels |= static_cast<std::uint8_t>(1u << level.get<unsigned>());
                }
            }
            if (args.contains("categories"))
            {
                if (!args["categories"].is_array() || args["categories"].size() > 1000) return fail();
                for (const auto& category : args["categories"])
                {
                    if (!category.is_string()) return fail();
                    filter.Categories.push_back(category.get<std::string>());
                }
            }
            const auto snapshot = ReadEditorDiagnostics(context.Diagnostics, cursor, filter);
            Json entries = Json::array();
            for (const auto& entry : snapshot.Entries)
                entries.push_back({{"sequence", entry.Sequence}, {"timestamp_ns", entry.TimestampNs},
                    {"level", static_cast<unsigned>(entry.Level)}, {"category", entry.Category}, {"message", entry.Message}});
            Json operations = Json::array();
            for (const auto& record : snapshot.OperationRecords)
                operations.push_back({{"sequence", record.Sequence}, {"name", record.Name},
                    {"source", static_cast<unsigned>(record.Source)}, {"status", static_cast<unsigned>(record.Status)},
                    {"wall_time_us", record.WallTimeUs}, {"allocation_delta_bytes", record.AllocationDeltaBytes},
                    {"requested_backend", record.RequestedBackend}, {"actual_backend", record.ActualBackend},
                    {"backend_fallback_reason", record.BackendFallbackReason}});
            return {.Text = Json{{"entries", std::move(entries)}, {"next_cursor", snapshot.NextCursor},
                {"dropped", snapshot.Dropped}, {"cleared_through", snapshot.ClearedThrough}, {"cursor_reset", snapshot.CursorReset},
                {"device_status", DeviceJson(snapshot.DeviceStatus)}, {"operation_records", std::move(operations)}}.dump()};
        }
    }

    extern "C++" void RegisterDiagnosticsAgentOperations(AgentOperationRegistry& registry)
    {
        (void)registry.Register({.Name = "diagnostics_read", .Title = "Read diagnostics",
            .Description = "New log entries after an exclusive cursor with exact category and integer level filters. "
                "Returns next_cursor, dropped unread log count (including clear), device status and latest 256 operation records. "
                "A cursor ahead of the current process restarts from zero with cursor_reset=true; discard prior cached entries. "
                "Operation source: 0 Editor, 1 AgentCli; status: 0 pending, 1 succeeded, 2 failed, 3 abandoned. "
                "Allocation bytes are process-wide tracked allocations during each call, not exclusive ownership. "
                "Diagnostics readers do not record themselves. Empty categories means all; empty levels means none.",
            .InputSchemaJson = R"({"type":"object","properties":{"since_cursor":{"type":"integer","minimum":0,"default":0},"levels":{"type":"array","items":{"type":"integer","enum":[0,1,2,3],"x-enum-names":["Info","Warning","Error","Debug"]}},"categories":{"type":"array","maxItems":1000,"items":{"type":"string"}},"limit":{"type":"integer","minimum":1,"maximum":1000,"default":1000}},"additionalProperties":false})",
            .Invoke = ReadDiagnostics});
        (void)registry.Register({.Name = "device_status", .Title = "Device status",
            .Description = "Runtime startup requested/actual backend, fallback reason, live operational status and validation counters. Read-only.",
            .Invoke = [](const AgentOperationContext& context, const std::string_view arguments) {
                const auto args = Json::parse(arguments, nullptr, false);
                if (!args.is_object() || !args.empty())
                    return AgentOperationOutcome{.IsError = true, .Text = "device_status expects an empty object.", .ErrorCode = "invalid_arguments"};
                return AgentOperationOutcome{.Text = DeviceJson(context.Diagnostics ? context.Diagnostics->ReadDeviceStatus() : EditorDeviceStatus{}).dump()};
            }});
    }
}
