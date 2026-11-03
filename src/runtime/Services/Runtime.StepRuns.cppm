// ARCH-022 S3: step runs, the job form of iterative work with preview and accept. A run steps on a worker, one
// `Step` at a time, checks its controls between steps (run, pause, step n, stop, accept, discard), records one metric
// per step, hands its newest state to the main thread at a limited rate, and commits once on Accept. It is carried by
// jobs of its owner (a plugin through its contract, or a runtime module): a holder job that holds the output until
// the run ends (admission, a Jobs row, cancel and an unload drain all see it; its readiness gate is the run's
// main-thread pump) and segment jobs joined to it (the worker loop).
//
//   ready --run/step n--> running --pause, steps done--> paused --run/step n--> running
//   running --converged, iteration cap, stop--> paused (ended: Accept or Discard only)
//   paused (also ended) --accept--> accepting --> committed | failed (Commit refused, e.g. stale)
//   converged with accept-on-convergence --> accepting at once
//   any --discard, cancel (Jobs window, unload), invalid captures--> discarded;  a failing step --> failed
// Terminal phases refuse every control. The owner's `Discard` runs once in its context as soon as the run is terminal;
// every callable of the owner is dropped once no segment is in flight.
module;

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module Extrinsic.Runtime.StepRuns;

import Extrinsic.Core.AppendLog;
import Extrinsic.Runtime.AgentOperations;
import Extrinsic.Runtime.ModuleLifecycle;
export import Extrinsic.Runtime.PluginContract;

export namespace Extrinsic::Runtime
{
    enum class StepRunPhase : std::uint8_t { Ready, Running, Paused, Accepting, Committed, Discarded, Failed };
    enum class StepRunEnd : std::uint8_t { None, Converged, IterationCap, Stopped };
    enum class StepOutcome : std::uint8_t { Continue, Converged, Failed };
    enum class StepRunAction : std::uint8_t { Run, Pause, Step, Stop, Accept, Discard };
    // What Show did with a copy: shown; not now (offered again until shown or replaced); cannot be shown (dropped).
    enum class StepRunShow : std::uint8_t { Shown, Later, Never };
    [[nodiscard]] std::string_view ToString(StepRunPhase phase) noexcept;
    [[nodiscard]] std::string_view ToString(StepRunEnd end) noexcept;
    [[nodiscard]] std::optional<StepRunAction> ParseStepRunAction(std::string_view name) noexcept;
    [[nodiscard]] constexpr bool IsTerminal(const StepRunPhase phase) noexcept
    {
        return phase == StepRunPhase::Committed || phase == StepRunPhase::Discarded || phase == StepRunPhase::Failed;
    }

    // One step's record: its number (1-based within a generation) and up to four values, named by the run.
    struct StepRunMetric
    {
        std::uint32_t Step{0u};
        std::array<double, 4> Values{};
    };

    // A copy of the run's state for display; Generation counts restarts.
    struct StepPreview
    {
        std::uint64_t Generation{0u};
        std::uint32_t Step{0u};
        std::vector<float> Values{};
    };

    using StepRunClock = std::function<std::chrono::steady_clock::time_point()>;

    // The owner's code. Step runs on a worker; Preview on a worker or on the main thread, never beside Step; the rest
    // on the main thread in the owner's context. Commit runs only while no step runs, but Valid, Show and Discard may
    // run while a step does: they must not read what Step changes (Show gets its own copy; Valid checks the captures).
    struct StepRunCallbacks
    {
        std::function<StepOutcome(StepRunMetric& metric)> Step{}; // one iteration; fills the metric's values
        std::function<void(std::vector<float>& values)> Preview{}; // copies the newest state; none: no preview
        std::function<StepRunShow(const StepPreview& preview)> Show{}; // a newer copy to display
        std::function<bool()> Valid{};                             // the captures are current (world and inputs)
        std::function<bool(std::string& diagnostic)> Commit{};     // publishes once; false when refused
        // Releases what the run shows, once, as soon as it ends without a commit (even while a step still runs: only
        // main-thread code may touch what it shows). Commit releases it itself.
        std::function<void()> Discard{};
    };

    struct StepRunOptions
    {
        std::string Label{};  // its Jobs row
        std::string Owner{};  // a plugin's or module's name
        PluginJobOutput Output{};
        std::uint32_t IterationCap{100u};
        bool AcceptOnConvergence{false};
        std::chrono::nanoseconds PreviewInterval{std::chrono::milliseconds{33}}; // about 30 copies a second
        std::array<std::string, 4> MetricNames{};
        StepRunClock Clock{}; // monotonic; steady_clock by default (tests inject one)
    };

    // How the owner submits and cancels its jobs (a plugin: PluginStepRunJobs()). Called on the main thread in the
    // owner's context: from Start and from the run's pump and completions.
    struct StepRunJobs
    {
        std::function<PluginJobHandle(PluginJob job, std::string* refusal)> Submit{};
        std::function<bool(PluginJobHandle job)> Cancel{}; // cancels the job's run (all its jobs)
    };
    [[nodiscard]] StepRunJobs PluginStepRunJobs();

    struct StepRunObservation
    {
        std::uint64_t Id{0u};
        std::string Label{}, Owner{}, Diagnostic{};
        std::array<std::string, 4> MetricNames{};
        StepRunPhase Phase{StepRunPhase::Ready};
        StepRunEnd End{StepRunEnd::None};
        std::uint64_t Generation{0u};
        std::uint32_t Steps{0u};
        bool SegmentInFlight{false};
        std::uint64_t PreviewCopies{0u}; // every copy made, over all generations
        std::optional<std::uint32_t> ShownStep{}; // the step of the copy shown last (this generation)
        std::shared_ptr<const Core::AppendLog<StepRunMetric>> Metrics{}; // this generation's, complete
    };

    struct StepRun; // host data: controls, observation and the owner's callables until the run retires

    // Main thread. Runs by id; a retired run (terminal, its owner's callables and previews dropped) stays observable,
    // the newest 32 of them (older ones go at the next call).
    class StepRunsService
    {
    public:
        StepRunsService();
        ~StepRunsService();
        StepRunsService(const StepRunsService&) = delete;
        StepRunsService& operator=(const StepRunsService&) = delete;

        // In the owner's context: submits the holder job. 0 with `refusal` when the callables are incomplete or the
        // owner refused the job (another run holds the output, ...). The run starts ready.
        [[nodiscard]] std::uint64_t Start(StepRunOptions options, StepRunCallbacks callbacks, StepRunJobs jobs,
                                          std::string& refusal);
        // A control (count: the steps of Step); false with the reason when refused.
        bool Control(std::uint64_t run, StepRunAction action, std::uint32_t count, std::string& refusal);
        // Latest wins: `init` replaces a pending one (never called then) and is applied once no step runs; the run
        // then computes again from step 0 in a new generation. False when the run is terminal.
        bool Restart(std::uint64_t run, std::function<bool(std::string& diagnostic)> init, std::string& refusal);
        [[nodiscard]] std::optional<StepRunObservation> Observe(std::uint64_t run);
        [[nodiscard]] std::vector<std::uint64_t> Runs();

        // MCP: run_control and run_observe.
        void RegisterOperations(AgentOperationRegistry& registry);

    private:
        std::map<std::uint64_t, std::shared_ptr<StepRun>> m_Runs{};
        std::uint64_t m_NextId{1u};
        void Trim();
    };

    class StepRunsModule final : public IRuntimeModule
    {
    public:
        [[nodiscard]] std::string_view Name() const noexcept override { return "Runtime.Services.StepRuns"; }
        [[nodiscard]] RuntimeModuleResult OnRegister(EngineSetup& setup) override;
        void OnShutdown(RuntimeModuleShutdownContext&) override {}
        [[nodiscard]] StepRunsService& Service() noexcept { return m_Service; }

    private:
        StepRunsService m_Service{};
    };
}
