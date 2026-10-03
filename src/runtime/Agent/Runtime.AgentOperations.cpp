module;

#include <chrono>
#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

module Extrinsic.Runtime.AgentOperations;

import Extrinsic.Core.Telemetry;
import Extrinsic.Runtime.DiagnosticsStream;

namespace Extrinsic::Runtime
{
    namespace
    {
        struct InvocationRecord
        {
            EditorDiagnosticsStream& Stream;
            EditorOperationRecord Record;
            std::chrono::steady_clock::time_point Started{std::chrono::steady_clock::now()};
            std::uint64_t AllocationStart{Core::Telemetry::Alloc::SnapshotCumulativeBytes()};
            std::uint64_t Sequence{};
            bool Finished{false};

            void Finish(const DiagnosticOperationStatus status, const std::string_view text = {})
            {
                if (Finished) return;
                Finished = true;
                Record.Status = status;
                Record.WallTimeUs = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - Started).count());
                const auto bytes = Core::Telemetry::Alloc::SnapshotCumulativeBytes();
                Record.AllocationDeltaBytes = bytes >= AllocationStart ? bytes - AllocationStart : 0;
                // Property pages contain values only; parsing their potentially large JSON
                // again cannot discover backend metadata and would block the main thread.
                const auto result = Record.Name == "property_values" ? nlohmann::json{} :
                    nlohmann::json::parse(text, nullptr, false);
                if (result.is_object())
                {
                    auto field = [&result](const char* name) -> std::string {
                        const auto it = result.find(name);
                        if (it == result.end() || it->is_null()) return {};
                        return it->is_string() ? it->get<std::string>() : it->is_number_integer() ? it->dump() : std::string{};
                    };
                    Record.RequestedBackend = field("requested_backend");
                    Record.ActualBackend = field("actual_backend");
                    if (Record.ActualBackend.empty()) Record.ActualBackend = field("backend");
                    Record.BackendFallbackReason = field("backend_fallback_reason");
                    if (Record.BackendFallbackReason.empty()) Record.BackendFallbackReason = field("fallback_reason");
                    const auto fallback = result.find("fell_back_to_cpu");
                    if (Record.BackendFallbackReason.empty() && fallback != result.end() && fallback->is_boolean() && fallback->get<bool>())
                        Record.BackendFallbackReason = field("backend_diagnostic");
                }
                Stream.UpdateOperation(Sequence, std::move(Record));
            }

            ~InvocationRecord() { Finish(DiagnosticOperationStatus::Abandoned); }
        };
    }

    bool AgentOperationRegistry::Register(AgentOperationSpec spec)
    {
        if (spec.Name.empty() || !spec.Invoke || Find(spec.Name) != nullptr) return false;
        m_Entries.push_back(std::move(spec));
        return true;
    }

    const AgentOperationSpec* AgentOperationRegistry::Find(const std::string_view name) const noexcept
    {
        for (const auto& entry : m_Entries)
            if (entry.Name == name) return &entry;
        return nullptr;
    }

    AgentOperationOutcome InvokeAgentOperation(const AgentOperationRegistry& registry, const std::string_view name,
                                               const AgentOperationContext& context, const std::string_view argumentsJson,
                                               const bool readOnlySession)
    {
        // Observation must not fill the bounded history with the observer's own polling.
        const bool observe = context.Diagnostics && name != "diagnostics_read" && name != "device_status" && name != "log";
        std::shared_ptr<InvocationRecord> record;
        if (observe)
        {
            record = std::make_shared<InvocationRecord>(*context.Diagnostics,
                EditorOperationRecord{.Name = std::string(name), .Source = context.Source});
            record->Sequence = context.Diagnostics->AppendOperation(record->Record);
        }
        auto invoke = [&]() -> AgentOperationOutcome {
            const auto* spec = registry.Find(name);
            if (spec == nullptr) return {.IsError = true, .Text = "Unknown operation '" + std::string(name) + "'."};
            if (readOnlySession && !spec->ReadOnly)
                return {.IsError = true,
                        .Text = "'" + spec->Name + "' changes the scene or files, but the Sandbox agent lane is read-only (--agent-readonly)."};
            if (spec->NeedsPresentedFrame && !context.ViewportPresentable)
                return {.IsError = true,
                        .Text = "'" + spec->Name + "' needs a presented frame, but the Sandbox window is minimized; restore it and retry.",
                        .ErrorCode = "viewport_not_presentable"};
            const ScopedEditorCommandLabelPrefix prefix{context.History, "Agent: "};
            return spec->Invoke(context, argumentsJson.empty() ? std::string_view{"{}"} : argumentsJson);
        };
        auto outcome = invoke();
        if (!record) return outcome;
        if (!outcome.Continuation)
            record->Finish(outcome.IsError ? DiagnosticOperationStatus::Failed : DiagnosticOperationStatus::Succeeded, outcome.Text);
        else
            outcome.Continuation = [record, continuation = std::move(outcome.Continuation)](
                const AgentOperationContext& current, AgentOperationOutcome& completed) {
                if (!continuation(current, completed)) return false;
                record->Finish(completed.IsError ? DiagnosticOperationStatus::Failed : DiagnosticOperationStatus::Succeeded, completed.Text);
                return true;
            };
        return outcome;
    }

    std::optional<std::string> ResolveAgentPath(const AgentOperationContext& context, const std::string_view path)
    {
        namespace fs = std::filesystem;
        if (context.AllowedRoots.empty() || path.empty()) return std::nullopt;
        std::error_code error;
        fs::path candidate{std::string(path)};
        if (candidate.is_relative()) candidate = fs::path(context.AllowedRoots.front()) / candidate;
        const fs::path resolved = fs::weakly_canonical(candidate, error);
        if (error) return std::nullopt;
        // weakly_canonical resolves every symlink that has a target; one still in the path is dangling and
        // a write through it would create its target, possibly outside the roots.
        fs::path partial;
        for (const auto& component : resolved)
        {
            partial /= component;
            if (fs::is_symlink(fs::symlink_status(partial, error))) return std::nullopt;
        }
        for (const auto& root : context.AllowedRoots)
        {
            const fs::path base = fs::weakly_canonical(fs::path(root), error);
            if (error) continue;
            // Component-wise containment: "/data/a" must not admit "/data/ab" or "/data/a/../b".
            const fs::path relative = resolved.lexically_relative(base);
            const bool inside = !relative.empty() && relative != "." && *relative.begin() != ".."; // not the root itself
            if (inside) return resolved.string();
        }
        return std::nullopt;
    }
}
