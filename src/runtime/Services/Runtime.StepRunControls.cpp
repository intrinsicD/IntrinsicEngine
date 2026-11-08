module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

#include <imgui.h>

module Extrinsic.Runtime.StepRunControls;

import Extrinsic.Runtime.ConfigFieldSpec;

namespace Extrinsic::Runtime
{
    namespace
    {
        std::string Label(const std::string_view text, const std::string_view suffix)
        {
            std::string label{text};
            if (!suffix.empty())
                label.append("##").append(suffix);
            return label;
        }
    }

    std::optional<StepRunAction> DrawStepRunControls(
        StepRunsService& runs, const std::uint64_t run, const std::string_view idSuffix, StepRunControlsState& state)
    {
        const auto observed = runs.Observe(run);
        if (!observed)
            return std::nullopt;
        ImGui::Text("Run %llu: %s, end %s, step %u, generation %llu",
            static_cast<unsigned long long>(observed->Id),
            ToString(observed->Phase).data(),
            ToString(observed->End).data(),
            observed->Steps,
            static_cast<unsigned long long>(observed->Generation));
        if (!observed->Diagnostic.empty())
            ImGui::TextWrapped("%s", observed->Diagnostic.c_str());
        struct Button
        {
            std::string_view Text;
            StepRunAction Action;
        };
        constexpr Button kButtons[]{{"Run", StepRunAction::Run},
            {"Pause", StepRunAction::Pause},
            {"Step", StepRunAction::Step},
            {"Stop", StepRunAction::Stop},
            {"Restart", StepRunAction::Restart},
            {"Accept", StepRunAction::Accept},
            {"Discard", StepRunAction::Discard}};
        std::optional<StepRunAction> issued{};
        bool first = true;
        for (const auto& button : kButtons)
        {
            // Restart is offered only where the owner gives an initializer (no inert control).
            if (button.Action == StepRunAction::Restart && !observed->CanRestart)
                continue;
            if (!first)
                ImGui::SameLine();
            first = false;
            if (ImGui::Button(Label(button.Text, idSuffix).c_str()))
            {
                issued = button.Action;
                std::string refusal;
                state.Message = runs.Control(run, button.Action, state.StepCount, refusal) ? std::string{} : refusal;
            }
            if (button.Action == StepRunAction::Step)
            {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(ImGui::GetFontSize() * 4.0f);
                (void)ImGui::InputScalar(Label("##steps", idSuffix).c_str(), ImGuiDataType_U32, &state.StepCount);
                state.StepCount = std::max(state.StepCount, 1u);
            }
        }
        if (!state.Message.empty())
            ImGui::TextWrapped("%s", state.Message.c_str());
        return issued;
    }

    bool DrawStepRunExecutionOptions(const std::string_view idSuffix,
        StepRunExecution& options,
        const std::uint32_t maxIterationCap,
        const bool stepRun,
        const bool livePreview,
        const std::uint32_t minIterationCap,
        const bool devicePreview)
    {
        const auto fields = StepRunExecutionFields(maxIterationCap, minIterationCap);
        bool changed = false;
        const auto number = [&](const ConfigFieldSpec& field, const std::string_view text, std::uint32_t& value)
        {
            if (ImGui::InputScalar(Label(text, idSuffix).c_str(), ImGuiDataType_U32, &value))
            {
                value = static_cast<std::uint32_t>(ClampToConfigFieldRange(field, double(value)));
                changed = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%.*s", int(field.Description.size()), field.Description.data());
        };
        const auto choice = [&](const ConfigFieldSpec& field, const std::string_view text, auto& value)
        {
            int index = static_cast<int>(value);
            const std::string current{field.EnumValues[static_cast<std::size_t>(index)]};
            if (ImGui::BeginCombo(Label(text, idSuffix).c_str(), current.c_str()))
            {
                for (std::size_t i = 0; i < field.EnumValues.size(); ++i)
                {
                    const std::string token{field.EnumValues[i]};
                    if (ImGui::Selectable(token.c_str(), int(i) == index))
                    {
                        value = static_cast<std::remove_cvref_t<decltype(value)>>(i);
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%.*s", int(field.Description.size()), field.Description.data());
        };
        if (maxIterationCap > 1u)
            number(fields[0], "Max iterations", options.IterationCap);
        if (!stepRun)
            return changed;
        if (livePreview && maxIterationCap != 1u)
        {
            number(fields[1], "Preview every k iterations (0: off)", options.PreviewEvery);
            number(fields[2], "Preview interval (ms)", options.PreviewIntervalMs);
            if (devicePreview)
                number(fields[5], "Device preview buffers", options.PreviewBuffers);
        }
        choice(fields[3], "Accept automatically", options.AutoAccept);
        choice(fields[4], "Start", options.Start);
        return changed;
    }
}
