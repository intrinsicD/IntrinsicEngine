// Shared editor-job fixture; link SandboxEditorJobHarness.cpp for runtime contract tests.
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <gtest/gtest.h>

import Extrinsic.Core.StrongHandle;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.EditorJobProjection;

namespace Extrinsic::Tests
{
    class EditorJobHarness
    {
    public:
        explicit EditorJobHarness(const unsigned workerCount = 2u)
            : m_Scheduler(workerCount) {}

        [[nodiscard]] Runtime::JobService& Jobs() noexcept { return m_Jobs; }
        [[nodiscard]] Runtime::KernelEventBus& Events() noexcept { return m_Events; }

        void Attach(auto& context) { AttachCommands(context.JobCommands); }

        // The scene epoch `Progress` and `SceneEpoch` answer (the session bumps it on scene replacement).
        void SetSceneEpoch(std::uint64_t epoch) noexcept { m_SceneEpoch = epoch; }

        [[nodiscard]] Runtime::EditorJobQueueSnapshot Snapshot() const;
        [[nodiscard]] bool DrainUntilTerminal(
            std::chrono::milliseconds timeout = std::chrono::seconds{5});

    private:
        void AttachCommands(Runtime::EditorJobCommandSurface&);
        [[nodiscard]] static bool IsTerminal(Runtime::JobState state) noexcept;

        class SchedulerScope final
        {
        public:
            explicit SchedulerScope(unsigned workerCount);
            ~SchedulerScope();
            SchedulerScope(const SchedulerScope&) = delete;
            SchedulerScope& operator=(const SchedulerScope&) = delete;
        };

        Runtime::JobService m_Jobs{};
        Runtime::KernelEventBus m_Events{};
        Runtime::EditorJobIdentityIndex m_Identities{};
        // Reported outcome per run, kept as the workspace session keeps it.
        Runtime::EditorJobOutcomeIndex m_Outcomes{};
        std::vector<Runtime::JobToken> m_CancelledRuns{};
        std::uint64_t m_SceneEpoch{0u};
        // Destroy the scheduler first, while worker-reachable state is alive.
        // Callers likewise declare the harness after its scene/context.
        SchedulerScope m_Scheduler;
    };

    // Drives one queued editor command from submit to its terminal job state.
    // `submit(sink)` applies the command with `sink` as its result callback and
    // must answer Pending ("...queued") without publishing. `inFlight(pending)`
    // runs while the job is still active: assert nothing changed yet, or edit
    // the input the job snapshotted to make it stale. `terminal(result)` checks
    // the delivered result. Every queued job owes exactly one terminal result;
    // a stale discard (BUG-138) reports StaleEntity with "did not apply" instead
    // of leaving the caller on its submit-time Pending text.
    template <typename Submit, typename InFlight, typename Terminal>
    void ExpectQueuedJobTerminates(EditorJobHarness& jobs, const std::string_view jobName,
        const Runtime::JobState expected, Submit submit, InFlight inFlight, Terminal terminal)
    {
        using Result = decltype(submit([](auto) {}));
        const auto text = [](const Result& result) -> const std::string& {
            if constexpr (requires { result.Message; }) return result.Message;
            else return result.Diagnostic;
        };
        std::optional<Result> delivered{};
        unsigned deliveries = 0u;
        const Result pending = submit([&](Result result) {
            ++deliveries;
            delivered = std::move(result);
        });
        ASSERT_EQ(pending.Status, Runtime::EditorCommandStatus::Pending) << text(pending);
        EXPECT_NE(text(pending).find("queued"), std::string::npos) << text(pending);
        const Runtime::EditorJobQueueSnapshot queued = jobs.Snapshot();
        ASSERT_EQ(queued.Entries.size(), 1u);
        EXPECT_EQ(queued.Entries[0].Name, jobName);
        // `JobService` dispatches at submit, so the pre-drain state races
        // between Queued/Running/AwaitingGate; only the drain publishes.
        EXPECT_TRUE(Runtime::IsActiveEditorJobState(queued.Entries[0].State));
        EXPECT_FALSE(delivered.has_value()) << "the terminal result must not precede the job";
        // Only a fatal failure raised by this call's in-flight step stops it; an earlier
        // block's failure in the same test must not mask this block's checks.
        const ::testing::TestResult& testResult =
            *::testing::UnitTest::GetInstance()->current_test_info()->result();
        const int firstPart = testResult.total_part_count();
        inFlight(pending);
        for (int part = firstPart; part < testResult.total_part_count(); ++part)
        {
            if (testResult.GetTestPartResult(part).fatally_failed())
                return;
        }

        ASSERT_TRUE(jobs.DrainUntilTerminal());
        const Runtime::EditorJobQueueSnapshot done = jobs.Snapshot();
        ASSERT_EQ(done.Entries.size(), 1u);
        EXPECT_EQ(done.Entries[0].State, expected);
        ASSERT_TRUE(delivered.has_value()) << "a queued job owes exactly one terminal result";
        EXPECT_EQ(deliveries, 1u);
        if (expected == Runtime::JobState::Published)
        {
            EXPECT_TRUE(delivered->Succeeded()) << text(*delivered);
        }
        else if (expected == Runtime::JobState::StaleDiscarded)
        {
            EXPECT_EQ(delivered->Status, Runtime::EditorCommandStatus::StaleEntity) << text(*delivered);
            EXPECT_FALSE(delivered->Succeeded());
            EXPECT_NE(text(*delivered).find("did not apply"), std::string::npos) << text(*delivered);
        }
        terminal(*delivered);
    }
}
