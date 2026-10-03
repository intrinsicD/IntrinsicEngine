module;
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include <imgui.h>

module Extrinsic.Sandbox.Editor.Shell;
import Extrinsic.Runtime.DiagnosticsStream;
#include "Sandbox.DiagnosticsPanel.hpp"

namespace Extrinsic::Sandbox::Editor
{
    extern "C++" void DrawDiagnosticsPanel(Runtime::EditorDiagnosticsStream* stream, DiagnosticsPanelState& state)
    {
        constexpr std::array<const char*, 4> levels{"Info", "Warning", "Error", "Debug"};
        for (std::size_t i = 0; i < levels.size(); ++i)
        {
            if (i) ImGui::SameLine();
            ImGui::Checkbox(levels[i], &state.Levels[i]);
        }
        ImGui::SetNextItemWidth(240.0f);
        ImGui::InputTextWithHint("Category", "Exact tag; empty shows all", state.Category.data(), state.Category.size());
        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &state.AutoScroll);
        ImGui::SameLine();
        const bool clear = ImGui::Button("Clear");
        if (clear)
        {
            Runtime::ClearEditorDiagnosticsLog();
            state.Entries.clear();
            state.Dropped = 0;
        }

        Runtime::EditorDiagnosticsFilter filter{};
        filter.Levels = 0;
        for (unsigned i = 0; i < state.Levels.size(); ++i)
            if (state.Levels[i]) filter.Levels |= static_cast<std::uint8_t>(1u << i);
        const std::string category{state.Category.data()};
        if (!category.empty()) filter.Categories.push_back(category);
        if (filter.Levels != state.AppliedLevels || category != state.AppliedCategory)
        {
            state.Entries.clear();
            state.Cursor = state.ClearCursor;
            state.Dropped = 0;
            state.AppliedLevels = filter.Levels;
            state.AppliedCategory = category;
        }
        auto snapshot = Runtime::ReadEditorDiagnostics(stream, state.Cursor, filter);
        state.Cursor = snapshot.NextCursor;
        if (snapshot.CursorReset)
        {
            state.Entries.clear();
            state.Dropped = 0;
            state.ClearCursor = 0;
        }
        if (clear) state.ClearCursor = snapshot.ClearedThrough;
        else state.Dropped += snapshot.Dropped;
        state.Device = snapshot.DeviceStatus;
        std::erase_if(state.Entries, [&](const auto& entry) { return entry.Sequence <= snapshot.ClearedThrough; });
        const bool newEntries = !snapshot.Entries.empty();
        for (auto& entry : snapshot.Entries) state.Entries.push_back(std::move(entry));
        if (state.Entries.size() > 2048)
            state.Entries.erase(state.Entries.begin(), state.Entries.end() - 2048);

        ImGui::Separator();
        ImGui::Text("Device: requested %s | actual %s | %s", state.Device.RequestedBackend.c_str(),
                    state.Device.ActualBackend.c_str(), state.Device.IsOperational ? "operational" : "not operational");
        ImGui::Text("Validation: %s | errors: %llu", state.Device.ValidationEnabled ? "enabled" : "disabled",
                    static_cast<unsigned long long>(state.Device.ValidationErrorCount));
        if (!state.Device.FallbackReason.empty()) ImGui::TextWrapped("%s", state.Device.FallbackReason.c_str());
        if (state.Dropped) ImGui::TextDisabled("%llu log entries no longer retained", static_cast<unsigned long long>(state.Dropped));
        if (ImGui::BeginChild("DiagnosticsLog", ImVec2(0, 250), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar))
        {
            for (const auto& entry : state.Entries)
            {
                const auto index = static_cast<std::size_t>(entry.Level);
                ImGui::TextDisabled("%llu [%s]", static_cast<unsigned long long>(entry.Sequence), levels[index]);
                ImGui::SameLine();
                ImGui::TextUnformatted(entry.Message.c_str());
            }
            if (state.AutoScroll && newEntries) ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();

        ImGui::SeparatorText("Recent operations");
        if (ImGui::BeginTable("DiagnosticsOperations", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                              ImVec2(0, 180)))
        {
            for (const char* column : {"Name", "Source", "Status", "Time (us)", "Allocated (bytes)", "Requested", "Actual / fallback"})
                ImGui::TableSetupColumn(column);
            ImGui::TableHeadersRow();
            constexpr std::array<const char*, 4> statuses{"Pending", "Succeeded", "Failed", "Abandoned"};
            for (auto it = snapshot.OperationRecords.rbegin(); it != snapshot.OperationRecords.rend(); ++it)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::TextUnformatted(it->Name.c_str());
                ImGui::TableNextColumn(); ImGui::TextUnformatted(it->Source == Runtime::DiagnosticOperationSource::Editor ? "Editor" : "Agent/CLI");
                ImGui::TableNextColumn(); ImGui::TextUnformatted(statuses[static_cast<std::size_t>(it->Status)]);
                ImGui::TableNextColumn(); ImGui::Text("%llu", static_cast<unsigned long long>(it->WallTimeUs));
                ImGui::TableNextColumn(); ImGui::Text("%llu", static_cast<unsigned long long>(it->AllocationDeltaBytes));
                ImGui::TableNextColumn(); ImGui::TextUnformatted(it->RequestedBackend.c_str());
                ImGui::TableNextColumn(); ImGui::TextUnformatted(it->ActualBackend.c_str());
                if (!it->BackendFallbackReason.empty()) ImGui::TextWrapped("%s", it->BackendFallbackReason.c_str());
            }
            ImGui::EndTable();
        }
    }
}
