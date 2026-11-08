module;

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <imgui.h>

module Intrinsic.Plugin.DensityWeights;

import Extrinsic.Runtime.PluginContract;
import Extrinsic.Runtime.PluginSections;
import Extrinsic.Runtime.PointCloudEditing;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.UiHostTypes;

namespace Intrinsic::Plugins::DensityWeights
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
                ImGui::Text("Weight min / max: %.5g / %.5g", double(r.MinWeight), double(r.MaxWeight));
                ImGui::Text("Contributions: %zu; largest candidate row: %zu", r.Diagnostics.NeighborContributionCount,
                            r.MaximumNeighbors);
                ImGui::Text("Support radius %.5g; query radius %.5g", r.SupportRadius, double(r.QueryRadius));
            }
            if (r.GpuQueryBatches > 0)
                ImGui::Text("GPU: %zu query batches, %llu bytes uploaded, %llu cache hits, %llu bytes read back",
                            r.GpuQueryBatches, static_cast<unsigned long long>(r.GpuInputUploadBytes),
                            static_cast<unsigned long long>(r.GpuInputCacheHits),
                            static_cast<unsigned long long>(r.CpuStageReadbackBytes));
            if (!r.Message.empty()) ImGui::TextWrapped("%s", r.Message.c_str());
        }

        // A run not yet ended: the host's run-control row (CPU and Vulkan alike).
        void DrawActive(Run& run)
        {
            Refresh(run);
            if (run.Final) return;
            ImGui::TextWrapped("Into '%s': %s", run.Pending.Weights.Name.c_str(), run.Pending.Message.c_str());
            R::DrawScalarAnalysisControls(run.Analysis, run.Pending.StepRun, {}, run.Row);
        }

        void Draw(bool& open)
        {
            if (g_Window == nullptr) g_Window = new WindowState(); // freed by Shutdown
            auto& state = *g_Window;
            ImGui::SetNextWindowSize(ImVec2(460, 600), ImGuiCond_FirstUseEver);
            if (!ImGui::Begin("Compact Density Weights###view.density_weights", &open))
            {
                ImGui::End();
                return;
            }
            const auto active = R::ActivePluginSection<kSection>();
            if (R::FollowPluginSection<kSection>(active, state.Draft, state.LastApplied)) state.Message.clear();
            if (!state.Draft)
            {
                ImGui::TextWrapped("The density weights section is missing or invalid.");
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
                R::PointPropertyCombo("Positions", draft.Positions, editing->InputCatalog(draft.StableEntityId, 1u));
            (void)R::PointScalarOutputInput("Weight property", draft.Weights);
            draft.Weights.Domain = draft.Positions.Domain;
            if (ImGui::BeginCombo("Backend", kBackendNames[std::size_t(draft.Backend)].data()))
            {
                for (std::size_t i = 0; i < std::size(kBackendNames); ++i)
                    if (ImGui::Selectable(kBackendNames[i].data(), std::size_t(draft.Backend) == i))
                        draft.Backend = DensityWeightBackend(i);
                ImGui::EndCombo();
            }
            ImGui::InputDouble("Support radius (0 = automatic)", &draft.SupportRadius);
            int kernel = int(draft.Kernel), mode = int(draft.Mode);
            if (ImGui::Combo("Kernel", &kernel, "Gaussian (sigma = h/4)\0LOP theta\0Wendland C2\0"))
                draft.Kernel = Kernels::KernelType(kernel);
            if (ImGui::Combo("Weight", &mode, "Direct\0Reciprocal\0")) draft.Mode = Kernels::DensityWeightMode(mode);
            ImGui::TextWrapped("Sums compact kernel contributions inside the support radius, with a leading 1. An "
                               "isolated sample has weight 1. Units follow the selected position property.");
            if (draft.Backend == DensityWeightBackend::VulkanLBVH)
            {
                ImGui::InputScalar("GPU query batch size", ImGuiDataType_U32, &draft.GpuQueryBatchSize);
                ImGui::InputScalar("GPU radius capacity", ImGuiDataType_U32, &draft.GpuRadiusCapacity);
                ImGui::TextWrapped("Vulkan collects complete conservative radius candidates. Overflow fails without "
                                   "changing the output and suggests a smaller radius. Subnormal coordinate components "
                                   "are unsupported.");
            }
            // One step, no live preview: Accept and start alone (no inert control), as the section's rows.
            (void)R::DrawStepRunExecutionOptions("DensityWeights", draft.Execution, 1u, true, false);
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
            if (ImGui::Button("Compute compact weights"))
                (void)Start(*active);
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
            ImGui::TextWrapped(
                "Every backend computes one step a run (Run or Step); Vulkan previews compact weights using double "
                "kernel sums. Accept publishes with Undo; Discard retains the property; Restart computes again from "
                "the same input.");
            if (editing != nullptr && ImGui::Button("Show weights"))
            {
                std::string diagnostic;
                state.ShowMessage =
                    editing->ShowProperty(draft.StableEntityId, draft.Weights, diagnostic) ? "" : diagnostic;
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
            .Id = "view.density_weights",
            .MenuPath = {"View", "Processing", "Sampling & Density"},
            .Title = "Compact Density Weights",
            .Draw = [](bool& open) { Draw(open); },
        });
    }

    void ShutdownWindow() noexcept
    {
        delete g_Window;
        g_Window = nullptr;
    }
}
