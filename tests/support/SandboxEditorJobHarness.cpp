#include <algorithm>
#include <cstdint>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

// Standard declarations precede the import-bearing fixture for Clang 20.
#include "SandboxEditorJobHarness.hpp"

import Extrinsic.Core.Tasks;

namespace Extrinsic::Tests
{
    // JobService stores no editor identity. Join its snapshots with submitted
    // output identities for the same dedup and entity queries used by the editor.
    void EditorJobHarness::AttachCommands(Runtime::EditorJobCommandSurface& commands)
    {
        commands.Submit =
            [this](Runtime::JobDesc desc,
                   Runtime::EditorJobIdentity identity)
                -> Runtime::JobToken
        {
            const Runtime::JobToken token = m_Jobs.Submit(std::move(desc));
            if (token.IsValid())
                m_Identities.insert_or_assign(token, std::move(identity));
            return token;
        };
        commands.FindActive =
            [this](const Runtime::EditorJobIdentity& requested)
                -> std::optional<Runtime::EditorJobRecord>
        {
            for (const Runtime::EditorJobRecord& job :
                 Snapshot().Entries)
            {
                if (Runtime::IsActiveEditorJobState(job.State) &&
                    Runtime::SameEditorJobOutput(
                        job.Identity,
                        requested))
                {
                    return job;
                }
            }
            return std::nullopt;
        };
        commands.Progress =
            [this](const Runtime::EditorOperationRunKey& key)
        {
            Runtime::EditorOperationProgress progress =
                Runtime::ResolveEditorOperationProgress(Snapshot().Entries, key);
            progress.Epoch = m_SceneEpoch;
            return progress;
        };
        commands.SceneEpoch = [this] { return m_SceneEpoch; };
        commands.ReportProgress =
            [this](const Runtime::JobToken token, const Runtime::JobProgress progress)
        {
            if (m_Identities.contains(token))
                m_Jobs.ReportProgress(token, progress);
        };
        // Same contract as the workspace session: submitted-with-identity jobs only.
        commands.SnapshotAll = [this]
        {
            std::vector<Runtime::EditorJobRecord> rows{};
            for (Runtime::EditorJobRecord& job : Snapshot().Entries)
                if (m_Identities.contains(job.Token))
                    rows.push_back(std::move(job));
            return rows;
        };
        commands.Stats = [this] { return m_Jobs.Stats(); };
        commands.Cancel = [this](const Runtime::JobToken token)
        {
            if (!m_Identities.contains(token))
                return Runtime::EditorJobCancelStatus::NotEditorJob;
            if (!m_Jobs.Cancel(token))
                return Runtime::EditorJobCancelStatus::NotActive;
            const Runtime::EditorJobIdentity& identity = m_Identities.at(token);
            if (!identity.Auxiliary)
                m_CancelledRuns.push_back(identity.Run.IsValid() ? identity.Run : token);
            return Runtime::EditorJobCancelStatus::Requested;
        };
        commands.RunCancelRequested = [this](const Runtime::JobToken run)
        {
            return std::find(m_CancelledRuns.begin(), m_CancelledRuns.end(), run) != m_CancelledRuns.end();
        };
        commands.SnapshotEntity =
            [this](const std::uint32_t stableEntityId)
        {
            std::vector<Runtime::EditorJobRecord> rows{};
            for (const Runtime::EditorJobRecord& job :
                 Snapshot().Entries)
            {
                if (job.Identity.EntityId == stableEntityId)
                    rows.push_back(job);
            }
            return rows;
        };
    }

    Runtime::EditorJobQueueSnapshot EditorJobHarness::Snapshot() const
    {
        Runtime::EditorJobQueueSnapshot snapshot{};
        for (const Runtime::JobSnapshot& job : m_Jobs.SnapshotAll())
        {
            const auto identity = m_Identities.find(job.Token);
            if (identity == m_Identities.end() && job.CorrelationId == 0u)
                continue;

            snapshot.Entries.push_back(Runtime::EditorJobRecord{
                .Token = job.Token,
                .Identity = identity != m_Identities.end() ? identity->second
                                                           : Runtime::EditorJobIdentity{},
                .CorrelationId = job.CorrelationId,
                .Name = job.DebugName,
                .State = job.State,
                .NormalizedProgress = job.Progress.Normalized,
                .ProgressDeterminate = job.Progress.Determinate,
                .ElapsedMilliseconds = job.ElapsedMilliseconds,
            });
        }
        return snapshot;
    }

    bool EditorJobHarness::DrainUntilTerminal(const std::chrono::milliseconds timeout)
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

    bool EditorJobHarness::IsTerminal(const Runtime::JobState state) noexcept
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

    EditorJobHarness::SchedulerScope::SchedulerScope(const unsigned workerCount)
    {
        if (Core::Tasks::Scheduler::IsInitialized())
            Core::Tasks::Scheduler::Shutdown();
        Core::Tasks::Scheduler::Initialize(workerCount);
    }

    EditorJobHarness::SchedulerScope::~SchedulerScope()
    {
        Core::Tasks::Scheduler::WaitForAll();
        Core::Tasks::Scheduler::Shutdown();
    }
}
