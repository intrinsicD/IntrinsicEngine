module;

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <imgui.h>

module Intrinsic.Plugin.DescriptorAnalysis;

import Extrinsic.Runtime.PluginContract;
import Extrinsic.Runtime.PluginSections;
import Extrinsic.Runtime.PointCloudEditing;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.StepRunControls;
import Extrinsic.Runtime.UiHostTypes;

namespace Intrinsic::Plugins::DescriptorAnalysis
{
    namespace
    {
        // The window's state: the edited settings (from the section until applied), the host's run row (it controls
        // the run started last, here or over MCP; the Jobs window lists every run), the output prefix, the displayed
        // bin and whether the viewport follows it (after a successful Show), and messages.
        struct WindowState
        {
            std::optional<Config> Draft{};
            std::string LastApplied{}; // the section the draft was taken from (serialized)
            R::StepRunControlsState Row{};
            std::array<char, 256> Prefix{"fpfh"};
            int DisplayBin{0};
            bool FollowDisplayBin{false};
            std::string Message{}, ShowMessage{};
        };
        WindowState* g_Window{nullptr};

        bool NameField(const char* label, R::GeometryPropertyRef& ref)
        {
            char name[128]{};
            ref.Name.copy(name, sizeof(name) - 1);
            if (!ImGui::InputText(label, name, sizeof(name))) return false;
            ref.Name = name;
            return true;
        }

        void ShowResult(const Result& r)
        {
            ImGui::Separator();
            ImGui::Text("Last run: %s", r.Status.c_str());
            ImGui::Text("Requested: %s; ran: %s", ToString(r.RequestedBackend), r.ActualBackend.c_str());
            ImGui::Text("Live / total: %zu / %zu; rows written: %zu", r.LiveCount, r.SlotCount, r.WrittenCount);
            ImGui::Text("Spacing: %.5g; feature radius: %.5g", double(r.MeanSpacing), double(r.FeatureRadius));
            ImGui::Text("GPU batches: %zu; largest indexed support: %zu", r.GpuQueryBatches, r.MaximumNeighbors);
            if (!r.Message.empty()) ImGui::TextWrapped("%s", r.Message.c_str());
        }

        // The displayed bin's property in the viewport; true when the editor showed it.
        bool ShowBin(WindowState& state, const Config& config)
        {
            auto* editing = R::PluginService<R::PointCloudEditingService>();
            std::string diagnostic = "The point-cloud editing service is unavailable.";
            const bool shown = editing != nullptr &&
                editing->ShowProperty(config.StableEntityId, config.Outputs[std::size_t(state.DisplayBin)], diagnostic);
            state.ShowMessage = shown ? std::string{} : diagnostic;
            return shown;
        }

        void Draw(bool& open)
        {
            if (g_Window == nullptr) g_Window = new WindowState(); // freed by Shutdown
            auto& state = *g_Window;
            ImGui::SetNextWindowSize(ImVec2(460, 600), ImGuiCond_FirstUseEver);
            if (!ImGui::Begin("FPFH Descriptor Analysis###view.descriptor_analysis", &open))
            {
                ImGui::End();
                return;
            }
            const auto active = R::ActivePluginSection<kSection>();
            if (R::FollowPluginSection<kSection>(active, state.Draft, state.LastApplied)) state.Message.clear();
            if (!state.Draft)
            {
                ImGui::TextWrapped("The descriptor analysis section is missing or invalid.");
                ImGui::End();
                return;
            }
            auto& draft = *state.Draft;
            const auto entity = draft.StableEntityId;
            ImGui::InputScalar("Entity", ImGuiDataType_U32, &draft.StableEntityId);
            ImGui::SameLine();
            if (auto* selection = R::PluginService<R::SelectionController>(); selection != nullptr &&
                !selection->SelectedStableIds().empty() && ImGui::Button("Use selection"))
                draft.StableEntityId = selection->SelectedStableIds().front();
            if (draft.StableEntityId != entity) state.FollowDisplayBin = false;
            if (auto* editing = R::PluginService<R::PointCloudEditingService>())
            {
                if (R::PointPropertyCombo("Positions", draft.Positions, editing->InputCatalog(draft.StableEntityId, 2u)))
                    for (auto& output : draft.Outputs) output.Domain = draft.Positions.Domain;
                R::PointPropertyCombo("Normals", draft.Normals,
                                    editing->InputCatalog(draft.StableEntityId, 2u, draft.Positions.Domain));
            }
            draft.Normals.Domain = draft.Positions.Domain;
            if (ImGui::TreeNode("Histogram output properties"))
            {
                ImGui::InputText("Output prefix", state.Prefix.data(), state.Prefix.size());
                if (ImGui::Button("Name all 33 bins")) draft.Outputs = MakeOutputs(draft.Positions.Domain, state.Prefix.data());
                constexpr std::array blocks{"alpha", "phi", "theta"};
                for (std::size_t i = 0; i < kBins; ++i)
                {
                    const std::string label = std::string(blocks[i / 11]) + " bin " + std::to_string(i % 11);
                    (void)NameField(label.c_str(), draft.Outputs[i]);
                }
                ImGui::TreePop();
            }
            if (ImGui::BeginCombo("Backend", kBackendNames[std::size_t(draft.Backend)].data()))
            {
                for (std::size_t i = 0; i < std::size(kBackendNames); ++i)
                    if (ImGui::Selectable(kBackendNames[i].data(), std::size_t(draft.Backend) == i))
                        draft.Backend = DescriptorBackend(i);
                ImGui::EndCombo();
            }
            ImGui::InputFloat("Feature radius (0 = automatic)", &draft.FeatureRadius);
            ImGui::InputScalar("Maximum neighbors (0 = all)", ImGuiDataType_U32, &draft.MaxNeighbors);
            ImGui::TextWrapped("FPFH uses three eleven-bin histograms and nonzero normals. Automatic radius is five "
                               "times mean nearest-neighbor spacing. A neighbor cap keeps the lowest source IDs within "
                               "the radius.");
            if (draft.Backend == DescriptorBackend::VulkanLBVH)
            {
                ImGui::InputScalar("GPU query batch size", ImGuiDataType_U32, &draft.GpuQueryBatchSize);
                ImGui::InputScalar("Radius result capacity", ImGuiDataType_U32, &draft.GpuRadiusCapacity);
                ImGui::TextWrapped(
                    "Uncapped radius support must fit the selected capacity (up to 1024). A neighbor cap "
                    "within that capacity can use the exact lowest-ID prefix even in denser "
                    "neighborhoods. A hybrid: the radius rows run on the GPU and are read back; scale, "
                    "SPFH and FPFH run on CPU.");
            }
            // One step, no live preview: Accept and start alone (no inert control).
            (void)R::DrawStepRunExecutionOptions("Descriptor", draft.Execution, 1u, true, false);
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
            // Start uses the applied settings, as descriptor_analysis_start does.
            const std::string reason = active ? Preview(*active) : "The section is invalid.";
            ImGui::BeginDisabled(!reason.empty());
            if (ImGui::Button("Start"))
                (void)Start(*active);
            ImGui::EndDisabled();
            if (!reason.empty()) ImGui::TextWrapped("Not ready: %s", reason.c_str());
            const auto last = LastRun();
            if (auto* runs = R::PluginService<R::StepRunsService>(); runs != nullptr && last && last->Pending.StepRun)
                (void)R::DrawStepRunControls(*runs, last->Pending.StepRun, "Descriptor", state.Row);
            ImGui::TextWrapped("Writes 33 named float histogram properties in one undoable operation. Each nonempty "
                               "eleven-bin block sums to 100.");
            // The viewport follows the slider only after a Show succeeded.
            const bool binChanged = ImGui::SliderInt("Display histogram bin", &state.DisplayBin, 0, int(kBins) - 1, "%d",
                                                     ImGuiSliderFlags_AlwaysClamp);
            ImGui::Text("Property: %s", draft.Outputs[std::size_t(state.DisplayBin)].Name.c_str());
            if (ImGui::Button("Show histogram bin") || (binChanged && state.FollowDisplayBin))
                state.FollowDisplayBin = ShowBin(state, draft);
            if (!state.ShowMessage.empty()) ImGui::TextWrapped("%s", state.ShowMessage.c_str());
            if (last && last->Final)
                ShowResult(*last->Final);
            ImGui::End();
        }
    }

    void AddWindow(R::PluginRegistrations& staged)
    {
        staged.Windows.push_back({
            .Id = "view.descriptor_analysis",
            .MenuPath = {"View", "Processing", "Features & Descriptors"},
            .Title = "FPFH Descriptor Analysis",
            .Draw = [](bool& open) { Draw(open); },
        });
    }

    void ShutdownWindow() noexcept
    {
        delete g_Window;
        g_Window = nullptr;
    }
}
