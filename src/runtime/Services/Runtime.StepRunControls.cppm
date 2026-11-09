// ARCH-023: the host's one step-run control row and its execution-options editor (ImGui, the host's context), shared
// by the Sandbox's method panels and plugin windows. A pressed control goes through StepRunsService::Control, the call
// MCP run_control makes, so the UI and an agent issue identical actions; the options editor edits the shared
// StepRunExecution within its field rows (StepRunExecutionFields), the ranges its config codec and MCP arguments use.
module;

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

export module Extrinsic.Runtime.StepRunControls;

export import Extrinsic.Runtime.StepRuns;

export namespace Extrinsic::Runtime
{
    // The row's own state between frames: the count of Step and the last refusal.
    struct StepRunControlsState
    {
        std::uint32_t StepCount{1u};
        std::string Message{};
    };

    // Draws the run's status line and Run, Pause, Step [n], Stop, Restart (only when the run can restart), Accept and
    // Discard on one line (labels "Run##<suffix>" and so on; plain labels for an empty suffix), then the last refusal.
    // Returns the action issued this frame (accepted or refused; nullopt: none). Nothing for an unknown run.
    std::optional<StepRunAction> DrawStepRunControls(
        StepRunsService& runs, std::uint64_t run, std::string_view idSuffix, StepRunControlsState& state);

    // Edits `options` within StepRunExecutionFields(maxIterationCap): iteration cap and, when `stepRun` (the chosen
    // path runs as a step run), live preview cadence and time limit (only with `livePreview`: the path shows its
    // state while computing), device ring capacity only with `devicePreview`, automatic Accept and start mode; a whole-solve path shows the cap alone (no inert
    // control). A method whose cap is its own parameter (smoothing's iterations, sampling's count) passes
    // maxIterationCap 0: no cap row. A one-step run (cap 1) also hides the cap. Labels "<field>##<suffix>". True when a value changed.
    bool DrawStepRunExecutionOptions(std::string_view idSuffix,
        StepRunExecution& options,
        std::uint32_t maxIterationCap,
        bool stepRun,
        bool livePreview = true,
        std::uint32_t minIterationCap = 1u,
        bool devicePreview = false);
}
