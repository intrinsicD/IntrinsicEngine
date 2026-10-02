// Shared editor-job fixture; link SandboxEditorJobHarness.cpp for runtime contract tests.
#pragma once

#include <chrono>
#include <cstdint>
#include <unordered_map>
#include <vector>

import Extrinsic.Core.StrongHandle;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
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
        std::unordered_map<Runtime::JobToken,
                           Runtime::EditorJobIdentity,
                           Core::StrongHandleHash<Runtime::JobTokenTag>>
            m_Identities{};
        std::vector<Runtime::JobToken> m_CancelledRuns{};
        std::uint64_t m_SceneEpoch{0u};
        // Destroy the scheduler first, while worker-reachable state is alive.
        // Callers likewise declare the harness after its scene/context.
        SchedulerScope m_Scheduler;
    };
}
