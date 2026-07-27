#pragma once

// Test-side stand-in for the piece of `SandboxEditorSession` that owns editor
// job identity, added by RUNTIME-194 Slice B5d.
//
// `JobService` deliberately stores no domain identity, so the editor records
// which entity and output each token belongs to and joins that against
// `JobService::SnapshotAll()` to build the queue view its dedup guard and
// panels read. Contract tests that drive editor facades without a session need
// the same join, and several of them need it, so it lives here rather than
// being re-derived per file.
//
// This mirrors the session; it does not replace it. Session-level behaviour
// (attachment epochs, world scoping) stays covered by the session's own tests.

#include <chrono>
#include <cstdint>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

import Extrinsic.Core.StrongHandle;
import Extrinsic.Core.Tasks;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.SandboxEditorFacades;

namespace Extrinsic::Tests
{
    class SandboxEditorJobHarness
    {
    public:
        [[nodiscard]] Runtime::JobService& Jobs() noexcept { return m_Jobs; }
        [[nodiscard]] Runtime::KernelEventBus& Events() noexcept
        {
            return m_Events;
        }

        // Installs the `JobService` submit path on `context`, recording the
        // editor identity the service does not keep.
        void Attach(Runtime::SandboxEditorContext& context)
        {
            context.DerivedJobCommands.SubmitJob =
                [this](Runtime::JobDesc desc,
                       Runtime::SandboxEditorJobIdentity identity)
                    -> Runtime::JobToken
            {
                const Runtime::JobToken token = m_Jobs.Submit(std::move(desc));
                if (token.IsValid())
                    m_Identities.insert_or_assign(token, std::move(identity));
                return token;
            };
            context.DerivedJobCommands.CancelJob =
                [this](const Runtime::JobToken token)
            {
                (void)m_Jobs.Cancel(token);
            };
        }

        // The queue view the editor's dedup guard and panels read.
        [[nodiscard]] Runtime::SandboxEditorJobQueueSnapshot Snapshot() const
        {
            Runtime::SandboxEditorJobQueueSnapshot snapshot{};
            for (const Runtime::JobSnapshot& job : m_Jobs.SnapshotAll())
            {
                const auto identity = m_Identities.find(job.Token);
                if (identity == m_Identities.end())
                    continue;

                snapshot.Entries.push_back(Runtime::SandboxEditorJobRecord{
                    .Token = job.Token,
                    .Identity = identity->second,
                    .Name = job.DebugName,
                    .State = job.State,
                    .NormalizedProgress = job.Progress.Normalized,
                    .ProgressDeterminate = job.Progress.Determinate,
                    .ElapsedMilliseconds = job.ElapsedMilliseconds,
                });
            }
            return snapshot;
        }

        // Per RUNTIME-194 Slice B0, drain until every retained job is terminal
        // rather than asserting on a fixed drain count.
        [[nodiscard]] bool DrainUntilTerminal(
            const std::chrono::milliseconds timeout = std::chrono::seconds{5})
        {
            const auto deadline = std::chrono::steady_clock::now() + timeout;
            for (;;)
            {
                if (Core::Tasks::Scheduler::IsInitialized())
                    Core::Tasks::Scheduler::WaitForAll();
                (void)m_Jobs.DrainCompletions(m_Events);

                bool allTerminal = true;
                for (const Runtime::JobSnapshot& job : m_Jobs.SnapshotAll())
                {
                    if (!IsTerminal(job.State))
                    {
                        allTerminal = false;
                        break;
                    }
                }
                if (allTerminal)
                    return true;
                if (std::chrono::steady_clock::now() >= deadline)
                    return false;
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
            }
        }

    private:
        [[nodiscard]] static bool IsTerminal(
            const Runtime::JobState state) noexcept
        {
            switch (state)
            {
            case Runtime::JobState::Published:
            case Runtime::JobState::Dropped:
            case Runtime::JobState::Cancelled:
            case Runtime::JobState::Rejected:
            case Runtime::JobState::StaleDiscarded:
                return true;
            default:
                return false;
            }
        }

        // `JobService` dispatches at submit onto the shared CPU pool and has
        // no inline fallback, so the harness owns a scheduler. It is the *last*
        // member so it is destroyed *first*: the pool must be quiesced while
        // everything a worker can still reach is alive (RUNTIME-194 Slice B5c).
        // For the same reason, declare the harness after the scene/context it
        // submits work against.
        class SchedulerScope final
        {
        public:
            SchedulerScope()
            {
                if (Core::Tasks::Scheduler::IsInitialized())
                    Core::Tasks::Scheduler::Shutdown();
                Core::Tasks::Scheduler::Initialize(2);
            }

            ~SchedulerScope()
            {
                Core::Tasks::Scheduler::WaitForAll();
                Core::Tasks::Scheduler::Shutdown();
            }

            SchedulerScope(const SchedulerScope&) = delete;
            SchedulerScope& operator=(const SchedulerScope&) = delete;
        };

        Runtime::JobService m_Jobs{};
        Runtime::KernelEventBus m_Events{};
        std::unordered_map<Runtime::JobToken,
                           Runtime::SandboxEditorJobIdentity,
                           Core::StrongHandleHash<Runtime::JobTokenTag>>
            m_Identities{};
        SchedulerScope m_Scheduler{};
    };
}
