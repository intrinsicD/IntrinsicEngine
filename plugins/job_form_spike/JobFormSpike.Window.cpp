module;

#include <cfloat>
#include <cstdint>
#include <string>
#include <vector>

#include <imgui.h>

module Intrinsic.Plugin.JobFormSpike;

import Extrinsic.Runtime.PluginContract;
import Extrinsic.Runtime.StepRuns;
import Extrinsic.Runtime.UiHostTypes;

namespace Intrinsic::Plugins::JobFormSpike
{
    namespace
    {
        // The request being edited and the last refusal; the controls act on the run started last.
        struct WindowState
        {
            Request Draft{};
            std::string Message{};
        };
        WindowState* g_Window{nullptr};

        void Controls(WindowState& state, R::StepRunsService& runs)
        {
            const auto run = runs.Observe(LastRun());
            if (!run) return;
            ImGui::Text("Run %llu: %s, end %s, step %u", static_cast<unsigned long long>(run->Id),
                        R::ToString(run->Phase).data(), R::ToString(run->End).data(), run->Steps);
            if (!run->Diagnostic.empty()) ImGui::TextWrapped("%s", run->Diagnostic.c_str());
            constexpr struct
            {
                const char* Label;
                R::StepRunAction Action;
            } kButtons[]{{"Run", R::StepRunAction::Run},   {"Pause", R::StepRunAction::Pause},
                         {"Step", R::StepRunAction::Step}, {"Stop", R::StepRunAction::Stop},
                         {"Accept", R::StepRunAction::Accept}, {"Discard", R::StepRunAction::Discard}};
            for (const auto& button : kButtons)
            {
                if (&button != kButtons) ImGui::SameLine();
                std::string refusal;
                if (ImGui::Button(button.Label) && !runs.Control(run->Id, button.Action, 1u, refusal))
                    state.Message = refusal;
            }
            if (!run->Metrics) return;
            std::vector<float> objective;
            for (const auto& metric : run->Metrics->Records()) objective.push_back(float(metric.Values[1]));
            if (!objective.empty())
                ImGui::PlotLines("Objective", objective.data(), int(objective.size()), 0, nullptr, FLT_MAX, FLT_MAX,
                                 ImVec2(0, 120));
        }

        void Draw(bool& open)
        {
            if (g_Window == nullptr) g_Window = new WindowState(); // freed by Shutdown
            auto& state = *g_Window;
            ImGui::SetNextWindowSize(ImVec2(420, 360), ImGuiCond_FirstUseEver);
            if (!ImGui::Begin("Job Form Spike (CPD)###view.job_form_spike", &open))
            {
                ImGui::End();
                return;
            }
            auto& draft = state.Draft;
            ImGui::InputScalar("Source", ImGuiDataType_U32, &draft.Source);
            ImGui::InputScalar("Target", ImGuiDataType_U32, &draft.Target);
            ImGui::InputScalar("Max iterations", ImGuiDataType_U32, &draft.MaxIterations);
            ImGui::Checkbox("Accept on convergence", &draft.AcceptOnConvergence);
            ImGui::Checkbox("Live preview", &draft.Preview);
            if (ImGui::Button("Start"))
            {
                std::string refusal;
                (void)StartRun(draft, refusal);
                state.Message = refusal;
            }
            if (auto* runs = R::PluginService<R::StepRunsService>(); runs != nullptr && LastRun() != 0u)
                Controls(state, *runs);
            if (!state.Message.empty()) ImGui::TextWrapped("%s", state.Message.c_str());
            ImGui::End();
        }
    }

    void AddWindow(R::PluginRegistrations& staged)
    {
        staged.Windows.push_back({
            .Id = "view.job_form_spike",
            .MenuPath = {"View", "Processing", "Job Form Spike (CPD)"},
            .Title = "Job Form Spike (CPD)",
            .Draw = [](bool& open) { Draw(open); },
        });
    }

    void ShutdownWindow() noexcept
    {
        delete g_Window;
        g_Window = nullptr;
    }
}
