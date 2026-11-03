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
import Extrinsic.Runtime.UiHostTypes;

namespace Intrinsic::Plugins::BilateralFilter
{
    namespace
    {
        // The window's state: the edited settings (from the section until applied), the runs still going (one per
        // output; the host refuses a second run on the same output), the last run started and messages.
        struct WindowState
        {
            std::optional<Config> Draft{};
            std::vector<std::shared_ptr<Run>> Running{};
            std::shared_ptr<Run> Last{};
            std::string Message{};
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
            if (!state.Draft) state.Draft = R::ActivePluginSection<kSection>();
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
            ImGui::InputScalar("Iterations", ImGuiDataType_U32, &draft.Iterations);
            ImGui::InputFloat("Spatial sigma (0: auto)", &draft.SpatialSigma);
            ImGui::InputFloat("Normal sigma", &draft.NormalSigma);
            if (draft.Backend == FilterBackend::VulkanLBVH)
                ImGui::InputScalar("GPU query batch", ImGuiDataType_U32, &draft.GpuQueryBatchSize);
            if (ImGui::Button("Apply settings"))
            {
                std::string error;
                state.Message = R::ApplyPluginSection<kSection>(draft, error) ? "Settings applied." : error;
            }
            ImGui::SameLine();
            if (ImGui::Button("Revert")) state.Draft = R::ActivePluginSection<kSection>();
            if (!state.Message.empty()) ImGui::TextWrapped("%s", state.Message.c_str());
            ImGui::Separator();
            // Run uses the applied settings, as the MCP tool does.
            const auto active = R::ActivePluginSection<kSection>();
            const std::string reason = active ? Preview(*active) : "The section is invalid.";
            std::erase_if(state.Running, [](const auto& run) { return run->Final.has_value(); });
            ImGui::BeginDisabled(!reason.empty());
            if (ImGui::Button("Run"))
            {
                state.Last = Start(*active);
                if (!state.Last->Final) state.Running.push_back(state.Last);
            }
            ImGui::EndDisabled();
            if (!reason.empty()) ImGui::TextWrapped("Not ready: %s", reason.c_str());
            for (std::size_t i = 0; i < state.Running.size(); ++i)
            {
                ImGui::PushID(int(i));
                ImGui::Text("Running into '%s'", state.Running[i]->Pending.Output.Name.c_str());
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) (void)R::CancelPluginRun(*state.Running[i]);
                ImGui::PopID();
            }
            if (state.Last && state.Last->Final) ShowResult(*state.Last->Final);
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
