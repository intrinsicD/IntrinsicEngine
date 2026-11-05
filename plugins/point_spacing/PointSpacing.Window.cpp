module;

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <imgui.h>

module Intrinsic.Plugin.PointSpacing;

import Extrinsic.Runtime.PluginContract;
import Extrinsic.Runtime.PluginSections;
import Extrinsic.Runtime.PointCloudEditing;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.UiHostTypes;

namespace Intrinsic::Plugins::PointSpacing
{
    namespace
    {
        // The window's state: the edited settings (from the section until applied) and messages. The runs are the
        // plugin's (Latest, Active), so a tool's run shows here too.
        struct WindowState
        {
            std::optional<Config> Draft{};
            std::string LastApplied{}; // the section the draft was taken from (serialized)
            std::string Message{}, ShowMessage{};
        };
        WindowState* g_Window{nullptr};

        void ShowResult(const Result& r)
        {
            ImGui::Separator();
            ImGui::Text("Last run: %s", r.Status.c_str());
            ImGui::Text("Requested: %s; ran: %s", ToString(r.RequestedBackend), r.ActualBackend.c_str());
            ImGui::Text("Live / total: %zu / %zu", r.LiveCount, r.SlotCount);
            if (r.Status == "applied")
            {
                ImGui::Text("Radius min / mean / max: %.5g / %.5g / %.5g", double(r.MinRadius), double(r.MeanRadius),
                            double(r.MaxRadius));
                ImGui::Text("Nearest spacing min / mean / max: %.5g / %.5g / %.5g", double(r.MinSpacing),
                            double(r.AverageSpacing), double(r.MaxSpacing));
                ImGui::Text("Centroid: %.5g / %.5g / %.5g; bounds diagonal: %.5g", double(r.Centroid.x),
                            double(r.Centroid.y), double(r.Centroid.z), double(r.BoundingBoxDiagonal));
            }
            if (r.GpuQueryBatches > 0)
                ImGui::Text("GPU: %zu query batches, %llu bytes uploaded, %llu cache hits, %llu bytes read back",
                            r.GpuQueryBatches, static_cast<unsigned long long>(r.GpuInputUploadBytes),
                            static_cast<unsigned long long>(r.GpuInputCacheHits),
                            static_cast<unsigned long long>(r.CpuStageReadbackBytes));
            if (!r.Message.empty()) ImGui::TextWrapped("%s", r.Message.c_str());
        }

        // A run not yet ended: a Vulkan run's preview with Accept and Discard, a CPU run with Cancel.
        void DrawActive(Run& run)
        {
            Refresh(run);
            if (run.Final) return;
            ImGui::TextWrapped("Into '%s': %s", run.Pending.Radii.Name.c_str(), run.Pending.Message.c_str());
            if (!run.Analysis)
            {
                if (ImGui::Button("Cancel")) (void)Cancel(run);
                return;
            }
            const auto state = run.Analysis.Poll();
            ImGui::BeginDisabled(!state.CanAccept);
            if (ImGui::Button("Accept")) Accept(run);
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Discard")) (void)Cancel(run);
            if (state.Phase == R::ScalarAnalysisPhase::ReadyToAccept && !state.CanAccept && !state.AcceptRefusal.empty())
                ImGui::TextWrapped("%s", state.AcceptRefusal.c_str());
        }

        void Draw(bool& open)
        {
            if (g_Window == nullptr) g_Window = new WindowState(); // freed by Shutdown
            auto& state = *g_Window;
            ImGui::SetNextWindowSize(ImVec2(460, 600), ImGuiCond_FirstUseEver);
            if (!ImGui::Begin("Point Spacing and Radii###view.point_spacing", &open))
            {
                ImGui::End();
                return;
            }
            const auto active = R::ActivePluginSection<kSection>();
            if (R::FollowPluginSection<kSection>(active, state.Draft, state.LastApplied)) state.Message.clear();
            if (!state.Draft)
            {
                ImGui::TextWrapped("The point spacing section is missing or invalid.");
                ImGui::End();
                return;
            }
            auto& draft = *state.Draft;
            auto* editing = R::PluginService<R::PointCloudEditingService>();
            ImGui::InputScalar("Entity", ImGuiDataType_U32, &draft.StableEntityId);
            ImGui::SameLine();
            if (auto* selection = R::PluginService<R::SelectionController>(); selection != nullptr &&
                !selection->SelectedStableIds().empty() && ImGui::Button("Use selection"))
                draft.StableEntityId = selection->SelectedStableIds().front();
            if (editing != nullptr)
                R::PointPropertyCombo("Positions", draft.Positions, editing->InputCatalog(draft.StableEntityId, 2u));
            (void)R::PointScalarOutputInput("Radii property", draft.Radii);
            draft.Radii.Domain = draft.Positions.Domain;
            if (ImGui::BeginCombo("Backend", kBackendNames[std::size_t(draft.Backend)].data()))
            {
                for (std::size_t i = 0; i < std::size(kBackendNames); ++i)
                    if (ImGui::Selectable(kBackendNames[i].data(), std::size_t(draft.Backend) == i))
                        draft.Backend = SpacingBackend(i);
                ImGui::EndCombo();
            }
            ImGui::InputScalar("Neighbors k", ImGuiDataType_U32, &draft.KNeighbors);
            ImGui::InputFloat("Radius scale", &draft.ScaleFactor);
            ImGui::TextWrapped("Radius = scale times mean retained neighbor distance. Nearest-other spacing is reported "
                               "separately. Values use the selected property coordinates; coverage is not guaranteed.");
            if (draft.Backend == SpacingBackend::VulkanLBVH)
                ImGui::InputScalar("GPU query batch size", ImGuiDataType_U32, &draft.GpuQueryBatchSize);
            if (ImGui::Button("Apply settings"))
            {
                std::string error;
                const bool applied = R::ApplyPluginSection<kSection>(draft, error);
                state.Message = applied ? "Settings applied." : error;
                if (applied) state.LastApplied = R::SerializeDerivedSection<kSection>(draft); // its own change
            }
            ImGui::SameLine();
            if (ImGui::Button("Revert")) state.Draft = active;
            if (!state.Message.empty()) ImGui::TextWrapped("%s", state.Message.c_str());
            ImGui::Separator();
            // Run uses the applied settings, as the MCP tool does.
            const std::string reason = active ? Preview(*active) : "The section is invalid.";
            ImGui::BeginDisabled(!reason.empty());
            if (ImGui::Button("Estimate radii")) (void)Start(*active, false);
            ImGui::EndDisabled();
            if (!reason.empty()) ImGui::TextWrapped("Not ready: %s", reason.c_str());
            // A copy: Accept, Discard and Cancel change the plugin's list of active runs.
            const std::vector<std::shared_ptr<Run>> runs(Active().begin(), Active().end());
            for (std::size_t i = 0; i < runs.size(); ++i)
            {
                ImGui::PushID(int(i));
                DrawActive(*runs[i]);
                ImGui::PopID();
            }
            ImGui::TextWrapped("Vulkan previews spacing and radii on the device. Accept publishes with Undo; Discard "
                               "retains CPU rows. Show radii maps values to colors; point rendering currently expects "
                               "pixel sizes.");
            if (editing != nullptr && ImGui::Button("Show radii"))
            {
                std::string diagnostic;
                state.ShowMessage = editing->ShowProperty(draft.StableEntityId, draft.Radii, diagnostic) ? "" : diagnostic;
            }
            if (!state.ShowMessage.empty()) ImGui::TextWrapped("%s", state.ShowMessage.c_str());
            if (const auto latest = Latest(); latest != nullptr && latest->Final)
            {
                ShowResult(*latest->Final);
                if (ImGui::Button("Dismiss")) Dismiss(latest);
            }
            ImGui::End();
        }
    }

    void AddWindow(R::PluginRegistrations& staged)
    {
        staged.Windows.push_back({
            .Id = "view.point_spacing",
            .MenuPath = {"View", "Processing", "Sampling & Density"},
            .Title = "Point Spacing and Radii",
            .Draw = [](bool& open) { Draw(open); },
        });
    }

    void ShutdownWindow() noexcept
    {
        delete g_Window;
        g_Window = nullptr;
    }
}
