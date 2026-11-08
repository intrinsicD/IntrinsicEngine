module;

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <imgui.h>

module Intrinsic.Plugin.BilateralFilter;

import Extrinsic.Runtime.PluginContract;
import Extrinsic.Runtime.PluginSections;
import Extrinsic.Runtime.PointCloudEditing;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.StepRunControls;
import Extrinsic.Runtime.UiHostTypes;

namespace Intrinsic::Plugins::BilateralFilter
{
    namespace
    {
        // The window's state: the edited settings (from the section until applied), messages and the host's run row
        // (it controls the run started last, here or over MCP; the Jobs window lists every run).
        struct WindowState
        {
            std::optional<Config> Draft{};
            std::string LastApplied{}; // the section the draft was taken from (serialized)
            std::string Message{};
            R::StepRunControlsState Row{};
        };
        WindowState* g_Window{nullptr};

        void ShowResult(const Result& r)
        {
            ImGui::Text("Last run: %s", r.Status.c_str());
            if (!r.Message.empty()) ImGui::TextWrapped("%s", r.Message.c_str());
            if (r.Status != "applied") return;
            ImGui::Text("Backend %s, %u passes, %zu of %zu points filtered", r.ActualBackend.c_str(),
                        r.CompletedIterations, r.PointsFiltered, r.LiveCount);
            ImGui::Text("Last pass: mean displacement %.6g, max %.6g; spatial sigma %.6g", r.AverageDisplacement,
                        r.MaxDisplacement, r.SpatialSigmaUsed);
        }

        void Draw(bool& open)
        {
            if (g_Window == nullptr) g_Window = new WindowState(); // freed by Shutdown
            auto& state = *g_Window;
            if (!ImGui::Begin("Bilateral Point Filter###view.bilateral_filter", &open))
            {
                ImGui::End();
                return;
            }
            const auto active = R::ActivePluginSection<kSection>();
            if (R::FollowPluginSection<kSection>(active, state.Draft, state.LastApplied)) state.Message.clear();
            if (!state.Draft)
            {
                ImGui::TextWrapped("The bilateral filter section is missing or invalid.");
                ImGui::End();
                return;
            }
            auto& draft = *state.Draft;
            ImGui::InputScalar("Entity", ImGuiDataType_U32, &draft.StableEntityId);
            ImGui::SameLine();
            if (auto* selection = R::PluginService<R::SelectionController>(); selection != nullptr &&
                !selection->SelectedStableIds().empty() && ImGui::Button("Use selection"))
                draft.StableEntityId = selection->SelectedStableIds().front();
            if (auto* editing = R::PluginService<R::PointCloudEditingService>())
            {
                R::PointPropertyCombo("Positions", draft.Positions, editing->InputCatalog(draft.StableEntityId, 2u));
                R::PointPropertyCombo("Normals", draft.Normals,
                              editing->InputCatalog(draft.StableEntityId, 2u, draft.Positions.Domain));
            }
            char output[128]{};
            draft.Output.Name.copy(output, sizeof(output) - 1);
            if (ImGui::InputText("Output", output, sizeof(output))) draft.Output.Name = output;
            draft.Output.Domain = draft.Positions.Domain;
            draft.Normals.Domain = draft.Positions.Domain;
            if (ImGui::BeginCombo("Backend", kBackendNames[std::size_t(draft.Backend)].data()))
            {
                for (std::size_t i = 0; i < std::size(kBackendNames); ++i)
                    if (ImGui::Selectable(kBackendNames[i].data(), std::size_t(draft.Backend) == i))
                        draft.Backend = FilterBackend(i);
                ImGui::EndCombo();
            }
            ImGui::InputScalar("k neighbors", ImGuiDataType_U32, &draft.KNeighbors);
            // One step is one filter pass; live preview only for an in-place output (no inert control).
            (void)R::DrawStepRunExecutionOptions("Bilateral",
                draft.Execution,
                kMaxIterations,
                true,
                draft.Output.Name == draft.Positions.Name,
                kMinIterations);
            ImGui::InputFloat("Spatial sigma (0: auto)", &draft.SpatialSigma);
            ImGui::InputFloat("Normal sigma", &draft.NormalSigma);
            if (IsVulkan(draft.Backend))
                ImGui::InputScalar("GPU query batch", ImGuiDataType_U32, &draft.GpuQueryBatchSize);
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
            // Start uses the applied settings, as bilateral_filter_start does.
            const std::string reason = active ? Preview(*active) : "The section is invalid.";
            ImGui::BeginDisabled(!reason.empty());
            if (ImGui::Button("Start"))
                (void)Start(*active);
            ImGui::EndDisabled();
            if (!reason.empty()) ImGui::TextWrapped("Not ready: %s", reason.c_str());
            const auto last = LastRun();
            if (auto* runs = R::PluginService<R::StepRunsService>(); runs != nullptr && last && last->Pending.StepRun)
                (void)R::DrawStepRunControls(*runs, last->Pending.StepRun, "Bilateral", state.Row);
            if (last && last->Final)
                ShowResult(*last->Final);
            ImGui::End();
        }
    }

    void AddWindow(R::PluginRegistrations& staged)
    {
        staged.Windows.push_back({
            .Id = "view.bilateral_filter",
            .MenuPath = {"View", "Processing", "Filtering & Smoothing"},
            .Title = "Bilateral Point Filter",
            .Draw = [](bool& open) { Draw(open); },
        });
    }

    void ShutdownWindow() noexcept
    {
        delete g_Window;
        g_Window = nullptr;
    }
}
