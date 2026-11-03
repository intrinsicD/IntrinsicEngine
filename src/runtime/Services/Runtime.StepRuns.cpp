module;

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

module Extrinsic.Runtime.StepRuns;

import Extrinsic.Core.TripleBuffer;
import Extrinsic.Runtime.ServiceRegistry;

namespace Extrinsic::Runtime
{
    using Json = nlohmann::json;
    using Clock = std::chrono::steady_clock;

    std::string_view ToString(const StepRunPhase phase) noexcept
    {
        switch (phase)
        {
        case StepRunPhase::Ready: return "ready";
        case StepRunPhase::Running: return "running";
        case StepRunPhase::Paused: return "paused";
        case StepRunPhase::Accepting: return "accepting";
        case StepRunPhase::Committed: return "committed";
        case StepRunPhase::Discarded: return "discarded";
        case StepRunPhase::Failed: return "failed";
        }
        return "unknown";
    }

    std::string_view ToString(const StepRunEnd end) noexcept
    {
        switch (end)
        {
        case StepRunEnd::None: return "none";
        case StepRunEnd::Converged: return "converged";
        case StepRunEnd::IterationCap: return "iteration_cap";
        case StepRunEnd::Stopped: return "stopped";
        }
        return "unknown";
    }

    std::optional<StepRunAction> ParseStepRunAction(const std::string_view name) noexcept
    {
        constexpr std::array<std::pair<std::string_view, StepRunAction>, 6> kActions{{
            {"run", StepRunAction::Run}, {"pause", StepRunAction::Pause}, {"step", StepRunAction::Step},
            {"stop", StepRunAction::Stop}, {"accept", StepRunAction::Accept}, {"discard", StepRunAction::Discard}}};
        for (const auto& [text, action] : kActions)
            if (text == name) return action;
        return std::nullopt;
    }

    StepRunJobs PluginStepRunJobs()
    {
        return {.Submit = [](PluginJob job, std::string* refusal) { return SubmitPluginJob({}, std::move(job), refusal); },
                .Cancel = [](const PluginJobHandle job) { return CancelPluginJob(job); }};
    }

    struct StepRun
    {
        std::uint64_t Id{0u};
        StepRunOptions Options{};
        StepRunCallbacks Callbacks{};
        StepRunJobs Jobs{};
        PluginJobHandle Holder{};

        // Main thread.
        StepRunPhase Phase{StepRunPhase::Ready};
        StepRunEnd End{StepRunEnd::None};
        std::string Diagnostic{};
        bool Computing{false};                    // run or step n requested and not yet done
        std::optional<std::uint32_t> Remaining{}; // steps left of a step n (none: until paused or ended)
        bool SegmentInFlight{false};
        bool AcceptRequested{false};
        bool Retired{false}; // terminal and no segment left: the owner's callables and the previews are gone
        std::function<bool(std::string&)> PendingRestart{};
        std::uint64_t Generation{0u};
        std::optional<std::uint32_t> ShownStep{};
        bool Unshown{false}; // the copy the reader holds is to be offered again (Show answered Later)

        // Written by the segment on a worker, read on the main thread after its completion (the job service orders
        // both), or observed while it runs (atomics).
        std::shared_ptr<Core::AppendLog<StepRunMetric>> Log{};
        std::atomic<std::uint32_t> Steps{0u};
        std::atomic<bool> Yield{false}; // stop after the step in flight
        std::atomic<std::uint64_t> Copies{0u};
        StepOutcome SegmentOutcome{StepOutcome::Continue};
        StepRunEnd SegmentEnd{StepRunEnd::None};
        std::uint32_t SegmentSteps{0u};
        // The preview: written by whoever runs the owner's code (a segment, or the pump while none runs).
        std::optional<Core::TripleBuffer<StepPreview>> Previews{std::in_place};
        std::uint32_t CopiedStep{0u};
        std::optional<Clock::time_point> LastCopy{};

        [[nodiscard]] Clock::time_point Now() const { return Options.Clock ? Options.Clock() : Clock::now(); }

        // A copy needs a Preview callable, a newer step, the reader to have taken the previous copy and the interval passed.
        void MaybeCopy()
        {
            const std::uint32_t steps = Steps.load(std::memory_order_relaxed);
            if (!Callbacks.Preview || steps == CopiedStep || Previews->Unread()) return;
            const auto now = Now();
            if (LastCopy && now - *LastCopy < Options.PreviewInterval) return;
            StepPreview& back = Previews->Back();
            back.Generation = Generation;
            back.Step = steps;
            Callbacks.Preview(back.Values);
            Previews->Publish();
            LastCopy = now;
            CopiedStep = steps;
            Copies.fetch_add(1u, std::memory_order_relaxed);
        }

        // Worker: steps until paused, the budget is spent, the run ends or is cancelled.
        void RunSegment(const std::uint32_t budget, const std::function<bool()>& cancelled)
        {
            SegmentOutcome = StepOutcome::Continue;
            SegmentEnd = StepRunEnd::None;
            SegmentSteps = 0u;
            while (!cancelled() && !Yield.load(std::memory_order_acquire) && (budget == 0u || SegmentSteps < budget))
            {
                StepRunMetric metric{.Step = Steps.load(std::memory_order_relaxed) + 1u};
                SegmentOutcome = Callbacks.Step(metric);
                ++SegmentSteps;
                if (SegmentOutcome == StepOutcome::Failed) return;
                (void)Log->Append(metric);
                Steps.store(metric.Step, std::memory_order_relaxed);
                MaybeCopy();
                if (SegmentOutcome == StepOutcome::Converged) SegmentEnd = StepRunEnd::Converged;
                else if (metric.Step >= Options.IterationCap) SegmentEnd = StepRunEnd::IterationCap;
                if (SegmentEnd != StepRunEnd::None) return;
            }
        }

        // Terminal at once (any caller); what needs the owner's context follows in the pump or a completion.
        void MarkTerminal(const StepRunPhase phase, std::string diagnostic)
        {
            if (!IsTerminal(Phase))
            {
                Phase = phase;
                if (!diagnostic.empty()) Diagnostic = std::move(diagnostic);
            }
            Computing = false;
            AcceptRequested = false;
            Yield.store(true, std::memory_order_release);
        }

        // The owner's context, a terminal run: what it shows goes at once (only main-thread code touches it, so it need
        // not wait for a segment, and the output is free only after it); the callables go once no segment runs.
        void Settle()
        {
            if (auto discard = std::exchange(Callbacks.Discard, {}); discard && Phase != StepRunPhase::Committed)
                discard();
            Callbacks.Show = {};
            if (SegmentInFlight || Retired) return;
            Retired = true;
            Callbacks = {};
            PendingRestart = {};
            Options.Clock = {}; // the owner's too
            Previews.reset();   // nothing reads them any more
        }

        // In the owner's context.
        void Terminate(const StepRunPhase phase, std::string diagnostic = {})
        {
            MarkTerminal(phase, std::move(diagnostic));
            Settle();
        }

        // Latest wins: once a restart is requested, nothing of the older computation shows. A copy Show refused is
        // offered again on the next pump until it is shown or a newer one replaces it.
        void Show()
        {
            const bool newer = Previews->Update();
            if (PendingRestart || !Callbacks.Show)
            {
                Unshown = false;
                return;
            }
            if (!newer && !Unshown) return;
            const StepRunShow shown = Callbacks.Show(Previews->Front());
            Unshown = shown == StepRunShow::Later;
            if (shown == StepRunShow::Shown) ShownStep = Previews->Front().Step;
        }

        bool Valid() const { return !Callbacks.Valid || Callbacks.Valid(); }

        void SubmitSegment(const std::shared_ptr<StepRun>& self)
        {
            const std::uint32_t budget = Remaining.value_or(0u);
            PluginJob job{.Label = Options.Label, .Run = Holder};
            job.Work = [self, budget](const std::function<bool()>& cancelled) { self->RunSegment(budget, cancelled); };
            job.Completion = [self](const bool applied) { return self->SegmentDone(applied); };
            std::string refusal;
            SegmentInFlight = true;
            if (!Jobs.Submit(std::move(job), &refusal).IsValid())
            {
                SegmentInFlight = false;
                Terminate(StepRunPhase::Failed, "the next step was refused: " + refusal);
            }
        }

        // Main thread, the segment's completion (also a late one after the run ended).
        bool SegmentDone(const bool applied)
        {
            SegmentInFlight = false;
            if (IsTerminal(Phase))
            {
                Settle();
                return applied;
            }
            if (!applied)
            {
                Terminate(StepRunPhase::Discarded, "cancelled");
                return false;
            }
            if (SegmentOutcome == StepOutcome::Failed)
            {
                Terminate(StepRunPhase::Failed, "a step failed");
                return true;
            }
            if (Remaining) *Remaining -= std::min(*Remaining, SegmentSteps);
            if (SegmentEnd != StepRunEnd::None && End == StepRunEnd::None) End = SegmentEnd;
            if (End != StepRunEnd::None || (Remaining && *Remaining == 0u)) Computing = false;
            if (!Computing) Phase = StepRunPhase::Paused;
            // An older computation's convergence never accepts the restart that replaces it.
            if (End == StepRunEnd::Converged && Options.AcceptOnConvergence && !PendingRestart) AcceptRequested = true;
            return true;
        }

        // Main thread, the holder's readiness gate, once per drain: true starts Accept (the holder's completion).
        bool Pump(const std::shared_ptr<StepRun>& self)
        {
            if (IsTerminal(Phase))
            {
                Settle();
                (void)Jobs.Cancel(Holder); // once it took, the gate is not asked again
                return false;
            }
            if (!Valid())
            {
                Terminate(StepRunPhase::Discarded, "stale: the captured inputs or the world changed");
                return false;
            }
            Show();
            if (SegmentInFlight) return false;
            if (PendingRestart)
            {
                auto init = std::exchange(PendingRestart, {});
                std::string diagnostic;
                if (!init(diagnostic))
                {
                    Terminate(StepRunPhase::Failed, "restart failed: " + diagnostic);
                    return false;
                }
                ++Generation;
                Log = std::make_shared<Core::AppendLog<StepRunMetric>>(Options.IterationCap);
                Steps.store(0u, std::memory_order_relaxed);
                CopiedStep = 0u;
                ShownStep.reset();
                End = StepRunEnd::None;
                Remaining.reset();
                Computing = true;
                Diagnostic.clear();
            }
            Yield.store(false, std::memory_order_release);
            if (AcceptRequested)
            {
                Phase = StepRunPhase::Accepting;
                return true;
            }
            if (Computing && End == StepRunEnd::None)
            {
                Phase = StepRunPhase::Running;
                SubmitSegment(self);
                return false;
            }
            MaybeCopy(); // idle: the paused or ended state still shows, at the same limits
            return false;
        }

        // Main thread, the holder's completion: Accept when applied, else the run was cancelled.
        bool HolderDone(const bool applied)
        {
            bool committed = false;
            if (!applied)
                Terminate(StepRunPhase::Discarded, "cancelled");
            else
            {
                std::string diagnostic;
                committed = Phase == StepRunPhase::Accepting && Valid() && Callbacks.Commit(diagnostic);
                if (committed)
                    Terminate(StepRunPhase::Committed);
                else
                    Terminate(StepRunPhase::Failed, diagnostic.empty() ? "stale: the captured inputs changed" : diagnostic);
            }
            Jobs = {}; // only the holder's pump used them
            return committed;
        }
    };

    StepRunsService::StepRunsService() = default;
    StepRunsService::~StepRunsService() = default;

    std::uint64_t StepRunsService::Start(StepRunOptions options, StepRunCallbacks callbacks, StepRunJobs jobs,
                                         std::string& refusal)
    {
        if (!callbacks.Step || !callbacks.Commit || !jobs.Submit || !jobs.Cancel || options.IterationCap == 0u)
        {
            refusal = "a step run needs Step, Commit, the owner's Submit and Cancel and an iteration cap";
            return 0u;
        }
        auto run = std::make_shared<StepRun>();
        run->Id = m_NextId;
        run->Options = std::move(options);
        run->Callbacks = std::move(callbacks);
        run->Jobs = std::move(jobs);
        run->Log = std::make_shared<Core::AppendLog<StepRunMetric>>(run->Options.IterationCap);
        PluginJob holder{.Label = run->Options.Label, .Output = run->Options.Output};
        holder.Work = [](const std::function<bool()>&) {};
        holder.IsReadyToApply = [run] { return run->Pump(run); };
        holder.Completion = [run](const bool applied) { return run->HolderDone(applied); };
        run->Holder = run->Jobs.Submit(std::move(holder), &refusal);
        if (!run->Holder.IsValid())
        {
            run->Callbacks = {};
            if (refusal.empty()) refusal = "the owner refused the run's job";
            return 0u;
        }
        m_Runs.emplace(m_NextId, run);
        Trim();
        return m_NextId++;
    }

    void StepRunsService::Trim()
    {
        constexpr std::ptrdiff_t kKeptRetired = 32;
        auto retired = std::ranges::count_if(m_Runs, [](const auto& entry) { return entry.second->Retired; });
        for (auto it = m_Runs.begin(); it != m_Runs.end() && retired > kKeptRetired;)
            if (it->second->Retired)
            {
                it = m_Runs.erase(it);
                --retired;
            }
            else
                ++it;
    }

    bool StepRunsService::Control(const std::uint64_t id, const StepRunAction action, const std::uint32_t count,
                                  std::string& refusal)
    {
        Trim();
        const auto found = m_Runs.find(id);
        if (found == m_Runs.end())
        {
            refusal = "no run " + std::to_string(id);
            return false;
        }
        StepRun& run = *found->second;
        const auto refuse = [&](std::string why)
        {
            refusal = std::move(why);
            return false;
        };
        if (IsTerminal(run.Phase)) return refuse("the run is " + std::string{ToString(run.Phase)});
        if (run.Phase == StepRunPhase::Accepting) return refuse("the run is being accepted");
        switch (action)
        {
        case StepRunAction::Run:
        case StepRunAction::Step:
            if (run.End != StepRunEnd::None) return refuse("the run ended (" + std::string{ToString(run.End)} + "); Accept or Discard");
            if (action == StepRunAction::Step && count == 0u) return refuse("step needs a count of at least 1");
            if (action == StepRunAction::Step && run.SegmentInFlight) return refuse("the run is computing; pause first");
            run.Computing = true;
            run.Remaining = action == StepRunAction::Step ? std::optional{count} : std::nullopt;
            run.Phase = StepRunPhase::Running;
            return true;
        case StepRunAction::Pause:
            if (!run.Computing) return refuse("the run is not computing");
            run.Computing = false;
            run.Yield.store(true, std::memory_order_release);
            if (!run.SegmentInFlight) run.Phase = StepRunPhase::Paused;
            return true;
        case StepRunAction::Stop:
            if (run.End == StepRunEnd::None) run.End = StepRunEnd::Stopped;
            run.Computing = false;
            run.Yield.store(true, std::memory_order_release);
            if (!run.SegmentInFlight) run.Phase = StepRunPhase::Paused;
            return true;
        case StepRunAction::Accept:
            if (run.Computing || run.SegmentInFlight || run.PendingRestart) return refuse("pause or stop the run first");
            if (run.Steps.load(std::memory_order_relaxed) == 0u) return refuse("the run has no step yet");
            if (run.AcceptRequested) return refuse("Accept is already requested");
            run.AcceptRequested = true;
            return true;
        case StepRunAction::Discard: // the pump retires it and cancels its jobs, in the owner's context
            run.MarkTerminal(StepRunPhase::Discarded, "discarded");
            return true;
        }
        return refuse("unknown action");
    }

    bool StepRunsService::Restart(const std::uint64_t id, std::function<bool(std::string&)> init, std::string& refusal)
    {
        Trim();
        const auto found = m_Runs.find(id);
        if (found == m_Runs.end() || !init)
        {
            refusal = found == m_Runs.end() ? "no run " + std::to_string(id) : "a restart needs its init";
            return false;
        }
        StepRun& run = *found->second;
        if (IsTerminal(run.Phase) || run.Phase == StepRunPhase::Accepting)
        {
            refusal = "the run is " + std::string{ToString(run.Phase)};
            return false;
        }
        run.PendingRestart = std::move(init); // an older pending restart is dropped, never called
        run.AcceptRequested = false;
        run.Yield.store(true, std::memory_order_release);
        return true;
    }

    std::optional<StepRunObservation> StepRunsService::Observe(const std::uint64_t id)
    {
        Trim();
        const auto found = m_Runs.find(id);
        if (found == m_Runs.end()) return std::nullopt;
        const StepRun& run = *found->second;
        return StepRunObservation{.Id = run.Id,
                                  .Label = run.Options.Label,
                                  .Owner = run.Options.Owner,
                                  .Diagnostic = run.Diagnostic,
                                  .MetricNames = run.Options.MetricNames,
                                  .Phase = run.Phase,
                                  .End = run.End,
                                  .Generation = run.Generation,
                                  .Steps = run.Steps.load(std::memory_order_relaxed),
                                  .SegmentInFlight = run.SegmentInFlight,
                                  .PreviewCopies = run.Copies.load(std::memory_order_relaxed),
                                  .ShownStep = run.ShownStep,
                                  .Metrics = run.Log};
    }

    std::vector<std::uint64_t> StepRunsService::Runs()
    {
        Trim();
        std::vector<std::uint64_t> ids;
        for (const auto& [id, run] : m_Runs) ids.push_back(id);
        return ids;
    }

    namespace
    {
        AgentOperationOutcome Fail(std::string code, std::string message)
        {
            return {.IsError = true, .Text = std::move(message), .ErrorCode = std::move(code)};
        }

        Json ToJson(const StepRunObservation& run, const bool allMetrics)
        {
            const auto records = run.Metrics ? run.Metrics->Records() : std::span<const StepRunMetric>{};
            const auto metric = [&](const StepRunMetric& m)
            {
                Json values = Json::object();
                for (std::size_t i = 0; i < m.Values.size(); ++i)
                    if (!run.MetricNames[i].empty()) values[run.MetricNames[i]] = m.Values[i];
                return Json{{"step", m.Step}, {"values", values}};
            };
            Json out{{"run", run.Id}, {"label", run.Label}, {"owner", run.Owner},
                     {"phase", ToString(run.Phase)}, {"end", ToString(run.End)}, {"diagnostic", run.Diagnostic},
                     {"generation", run.Generation}, {"steps", run.Steps}, {"computing", run.SegmentInFlight},
                     {"preview_copies", run.PreviewCopies},
                     {"shown_step", run.ShownStep ? Json(*run.ShownStep) : Json(nullptr)},
                     {"last_metric", records.empty() ? Json(nullptr) : metric(records.back())}};
            if (allMetrics)
            {
                Json all = Json::array();
                for (const auto& m : records) all.push_back(metric(m));
                out["metrics"] = std::move(all);
            }
            return out;
        }
    }

    void StepRunsService::RegisterOperations(AgentOperationRegistry& registry)
    {
        constexpr const char* kRun = R"("run":{"type":"integer","minimum":1,"description":"The run's id (run_observe without it lists the runs)."})";
        (void)registry.Register({
            .Name = "run_control",
            .Title = "Control a step run",
            .Description = "run (until paused or ended), pause (after the step in flight), step (count steps), stop "
                           "(pause and end), accept (commit once, from a paused or ended run), discard (end without "
                           "committing). Refused once the run is committed, discarded or failed. Takes effect from "
                           "the next frame; run_observe shows it.",
            .InputSchemaJson = std::string{R"({"type":"object","properties":{)"} + kRun +
                               R"(,"action":{"type":"string","enum":["run","pause","step","stop","accept","discard"]},)"
                               R"("count":{"type":"integer","minimum":1,"maximum":4294967295}},"required":["run","action"],"additionalProperties":false})",
            .ReadOnly = false,
            .Invoke = [this](const AgentOperationContext&, const std::string_view arguments)
            {
                // The dispatcher validated the arguments against the schema (run and action required, count 1..2^32-1).
                const Json args = Json::parse(arguments, nullptr, false);
                const auto action = ParseStepRunAction(args.value("action", ""));
                if (!action) return Fail("invalid_arguments", "unknown action");
                std::string refusal;
                const auto id = args.value("run", std::uint64_t{0});
                if (!Control(id, *action, static_cast<std::uint32_t>(args.value("count", std::uint64_t{1})), refusal))
                    return Fail("refused", refusal);
                return AgentOperationOutcome{.Text = ToJson(*Observe(id), false).dump()};
            },
        });
        (void)registry.Register({
            .Name = "run_observe",
            .Title = "Observe step runs",
            .Description = "A run's phase, end reason, step count, generation (restarts), preview copies, the step "
                           "shown last and its last metric; with metrics, every metric of this generation. Without "
                           "run, the ids of the runs.",
            .InputSchemaJson = std::string{R"({"type":"object","properties":{)"} + kRun +
                               R"(,"metrics":{"type":"boolean"}},"additionalProperties":false})",
            .Invoke = [this](const AgentOperationContext&, const std::string_view arguments)
            {
                const Json args = Json::parse(arguments, nullptr, false); // an object (the dispatcher validated it)
                if (!args.contains("run")) return AgentOperationOutcome{.Text = Json{{"runs", Runs()}}.dump()};
                const auto run = Observe(args.value("run", std::uint64_t{0}));
                if (!run) return Fail("not_found", "no such run");
                return AgentOperationOutcome{.Text = ToJson(*run, args.value("metrics", false)).dump()};
            },
        });
    }

    RuntimeModuleResult StepRunsModule::OnRegister(EngineSetup& setup)
    {
        return setup.Services().Provide<StepRunsService>(m_Service, Name());
    }
}
