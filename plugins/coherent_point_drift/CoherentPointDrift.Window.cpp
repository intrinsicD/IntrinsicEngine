module;

#include <cfloat>
#include <cstdint>
#include <string>
#include <vector>

#include <imgui.h>

module Intrinsic.Plugin.CoherentPointDrift;

import Extrinsic.Runtime.PluginContract;
import Extrinsic.Runtime.StepRunControls;
import Extrinsic.Runtime.UiHostTypes;

namespace Intrinsic::Plugins::CoherentPointDrift
{
    namespace
    {
        // The request being edited, the last start refusal and the control row's state; the controls act on the run
        // started last.
        struct WindowState
        {
            Request Draft{};
            std::string Message{};
            R::StepRunControlsState Row{};
        };
        WindowState* g_Window{nullptr};

        // The host's shared step-run row (the actions run_control issues), then the objective's curve.
        void Controls(WindowState& state, R::StepRunsService& runs)
        {
            (void)R::DrawStepRunControls(runs, LastRun(), {}, state.Row);
            const auto run = runs.Observe(LastRun());
            if (!run || !run->Metrics)
                return;
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
            if (!ImGui::Begin("Coherent Point Drift (steps)###view.coherent_point_drift_steps", &open))
            {
                ImGui::End();
                return;
            }
            auto& draft = state.Draft;
            ImGui::InputScalar("Source", ImGuiDataType_U32, &draft.Source);
            ImGui::InputScalar("Target", ImGuiDataType_U32, &draft.Target);
            (void)R::DrawStepRunExecutionOptions({}, draft.Execution, kMaxIterations, true, true, 1u, true);
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
            .Id = "view.coherent_point_drift_steps",
            .MenuPath = {"View", "Processing", "Coherent Point Drift (steps)"},
            .Title = "Coherent Point Drift (steps)",
            .Draw = [](bool& open) { Draw(open); },
        });
    }

    void ShutdownWindow() noexcept
    {
        delete g_Window;
        g_Window = nullptr;
    }
}
