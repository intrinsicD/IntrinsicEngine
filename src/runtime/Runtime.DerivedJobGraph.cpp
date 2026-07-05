module;

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

module Extrinsic.Runtime.DerivedJobGraph;

import Extrinsic.Core.Error;
import Extrinsic.Core.Dag.Scheduler;
import Extrinsic.Runtime.ProgressiveRenderData;
import Extrinsic.Runtime.StreamingExecutor;

namespace Extrinsic::Runtime
{
    namespace
    {
        [[nodiscard]] bool IsTerminal(const DerivedJobStatus status) noexcept
        {
            return status == DerivedJobStatus::Complete ||
                   status == DerivedJobStatus::Failed ||
                   status == DerivedJobStatus::Cancelled ||
                   status == DerivedJobStatus::StaleDiscarded;
        }

        [[nodiscard]] bool IsUnsupportedJobDomain(const ProgressiveJobDomain domain) noexcept
        {
            return domain != ProgressiveJobDomain::Cpu;
        }

        [[nodiscard]] std::string UnsupportedDomainDiagnostic(const ProgressiveJobDomain domain)
        {
            std::string diagnostic{"progressive derived-job domain "};
            diagnostic += std::string{ToString(domain)};
            diagnostic += " is unavailable; CPU is the only operational domain in this registry";
            return diagnostic;
        }

        [[nodiscard]] DerivedJobStatus StatusForValidation(
            const DerivedJobApplyValidation validation) noexcept
        {
            switch (validation)
            {
            case DerivedJobApplyValidation::Current:
                return DerivedJobStatus::Complete;
            case DerivedJobApplyValidation::Cancelled:
                return DerivedJobStatus::Cancelled;
            case DerivedJobApplyValidation::MissingEntity:
            case DerivedJobApplyValidation::StaleEntityGeneration:
            case DerivedJobApplyValidation::StaleGeometryGeneration:
            case DerivedJobApplyValidation::StaleSourcePropertyGeneration:
            case DerivedJobApplyValidation::StaleBindingGeneration:
                return DerivedJobStatus::StaleDiscarded;
            }
            return DerivedJobStatus::StaleDiscarded;
        }

        [[nodiscard]] std::string FailureDiagnostic(const Core::ErrorCode code)
        {
            std::string diagnostic{"worker failed: "};
            diagnostic += std::string{Core::Error::ToString(code)};
            return diagnostic;
        }

        [[nodiscard]] std::uint64_t ElapsedNs(
            const std::chrono::steady_clock::time_point start) noexcept
        {
            const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - start);
            return elapsed.count() > 0
                ? static_cast<std::uint64_t>(elapsed.count())
                : 1u;
        }

        [[nodiscard]] std::uint64_t PositiveDelta(
            const std::uint64_t after,
            const std::uint64_t before) noexcept
        {
            return after > before ? after - before : 0u;
        }

        void AccumulateStatus(
            DerivedJobQueueDiagnostics& diagnostics,
            const DerivedJobStatus status) noexcept
        {
            ++diagnostics.TotalJobs;
            switch (status)
            {
            case DerivedJobStatus::Blocked:
                ++diagnostics.BlockedJobs;
                break;
            case DerivedJobStatus::Queued:
                ++diagnostics.QueuedJobs;
                break;
            case DerivedJobStatus::Running:
                ++diagnostics.RunningJobs;
                break;
            case DerivedJobStatus::Applying:
                ++diagnostics.ApplyingJobs;
                break;
            case DerivedJobStatus::Complete:
                ++diagnostics.CompleteJobs;
                break;
            case DerivedJobStatus::Failed:
                ++diagnostics.FailedJobs;
                break;
            case DerivedJobStatus::Cancelled:
                ++diagnostics.CancelledJobs;
                break;
            case DerivedJobStatus::StaleDiscarded:
                ++diagnostics.StaleDiscardedJobs;
                break;
            }
        }
    }

    struct DerivedJobRegistry::Impl
    {
        struct Record
        {
            std::uint32_t Generation{1u};
            DerivedJobKey Key{};
            std::string Name{};
            ProgressiveJobDomain RequestedDomain{ProgressiveJobDomain::Cpu};
            ProgressiveJobDomain ResolvedDomain{ProgressiveJobDomain::Cpu};
            DerivedJobStatus Status{DerivedJobStatus::Queued};
            std::vector<DerivedJobDependency> Dependencies{};
            StreamingTaskHandle Streaming{};
            std::chrono::steady_clock::time_point SubmittedAt{};
            DerivedJobOutput Output{};
            bool HasWorkerOutput{false};
            bool HasPreviousOutput{false};
            bool PreviousOutputRetained{false};
            bool IsReadbackJob{false};
            std::uint64_t ReadbackByteSize{0u};
            std::string Diagnostic{};
            std::move_only_function<DerivedJobWorkerResult()> Execute{};
            std::move_only_function<bool()> IsReadbackReady{};
            std::move_only_function<DerivedJobApplyValidation()> ValidateOnMainThread{};
            std::move_only_function<Core::Result(DerivedJobApplyContext&)> ApplyOnMainThread{};
        };

        explicit Impl(StreamingExecutor& executor) noexcept
            : Executor(&executor)
        {
        }

        mutable std::mutex Mutex{};
        StreamingExecutor* Executor{nullptr};
        DerivedJobRegistry* Owner{nullptr};
        std::vector<Record> Records{};
        std::uint64_t ApplyMainThreadCalls{0u};
        std::uint64_t LastApplyMainThreadTimeNs{0u};
        std::uint64_t LastApplyMainThreadCompletedJobs{0u};
        std::uint64_t LastApplyMainThreadFailedJobs{0u};
        std::uint64_t LastApplyMainThreadCancelledJobs{0u};
        std::uint64_t LastApplyMainThreadStaleDiscardedJobs{0u};
        std::uint64_t TotalApplyMainThreadCompletedJobs{0u};
        std::uint64_t TotalApplyMainThreadFailedJobs{0u};
        std::uint64_t TotalApplyMainThreadCancelledJobs{0u};
        std::uint64_t TotalApplyMainThreadStaleDiscardedJobs{0u};

        [[nodiscard]] std::optional<std::uint32_t> Resolve(
            const DerivedJobHandle handle) const noexcept
        {
            if (!handle.IsValid() || handle.Index >= Records.size())
            {
                return std::nullopt;
            }
            if (Records[handle.Index].Generation != handle.Generation)
            {
                return std::nullopt;
            }
            return handle.Index;
        }

        [[nodiscard]] bool DependencyCompletedLocked(const DerivedJobDependency& dependency) const
        {
            const auto resolved = Resolve(dependency.Job);
            return resolved.has_value() &&
                   Records[*resolved].Status == DerivedJobStatus::Complete;
        }

        [[nodiscard]] DerivedJobStatus SnapshotStatusLocked(const Record& record) const
        {
            if (IsTerminal(record.Status) || Executor == nullptr || !record.Streaming.IsValid())
            {
                return record.Status;
            }

            switch (Executor->GetState(record.Streaming))
            {
            case StreamingTaskState::Pending:
            case StreamingTaskState::Ready:
                if (std::any_of(record.Dependencies.begin(),
                                record.Dependencies.end(),
                                [this](const DerivedJobDependency& dep)
                                {
                                    return !DependencyCompletedLocked(dep);
                                }))
                {
                    return DerivedJobStatus::Blocked;
                }
                return DerivedJobStatus::Queued;
            case StreamingTaskState::Running:
                return DerivedJobStatus::Running;
            case StreamingTaskState::WaitingForMainThreadApply:
            case StreamingTaskState::WaitingForGpuUpload:
            case StreamingTaskState::WaitingForReadback:
                return DerivedJobStatus::Applying;
            case StreamingTaskState::Complete:
                return record.Status == DerivedJobStatus::Queued
                    ? DerivedJobStatus::Complete
                    : record.Status;
            case StreamingTaskState::Failed:
                return DerivedJobStatus::Failed;
            case StreamingTaskState::Cancelled:
                return DerivedJobStatus::Cancelled;
            }
            return record.Status;
        }

        [[nodiscard]] StreamingTaskState ExecutionStateLocked(const Record& record) const
        {
            if (Executor == nullptr || !record.Streaming.IsValid())
            {
                switch (record.Status)
                {
                case DerivedJobStatus::Complete:
                    return StreamingTaskState::Complete;
                case DerivedJobStatus::Failed:
                    return StreamingTaskState::Failed;
                case DerivedJobStatus::Cancelled:
                case DerivedJobStatus::StaleDiscarded:
                    return StreamingTaskState::Cancelled;
                case DerivedJobStatus::Blocked:
                case DerivedJobStatus::Queued:
                case DerivedJobStatus::Running:
                case DerivedJobStatus::Applying:
                    return StreamingTaskState::Pending;
                }
            }
            return Executor->GetState(record.Streaming);
        }

        [[nodiscard]] DerivedJobReadbackDiagnostics BuildReadbackDiagnosticsLocked() const
        {
            DerivedJobReadbackDiagnostics diagnostics{};
            for (const Record& record : Records)
            {
                if (!record.IsReadbackJob)
                {
                    continue;
                }

                ++diagnostics.Issued;
                const DerivedJobStatus status = SnapshotStatusLocked(record);
                const StreamingTaskState execution = ExecutionStateLocked(record);
                if (execution == StreamingTaskState::WaitingForReadback)
                {
                    ++diagnostics.Waiting;
                }
                if (status == DerivedJobStatus::Complete)
                {
                    ++diagnostics.Completed;
                }
                else if (status == DerivedJobStatus::Failed)
                {
                    ++diagnostics.Failed;
                }
                else if (status == DerivedJobStatus::Cancelled ||
                         status == DerivedJobStatus::StaleDiscarded)
                {
                    ++diagnostics.StaleOrCancelled;
                }
            }
            return diagnostics;
        }

        [[nodiscard]] DerivedJobQueueDiagnostics CountStatusesLocked() const
        {
            DerivedJobQueueDiagnostics diagnostics{};
            for (const Record& record : Records)
            {
                AccumulateStatus(diagnostics, SnapshotStatusLocked(record));
            }
            return diagnostics;
        }

        [[nodiscard]] DerivedJobQueueDiagnostics BuildDiagnosticsLocked() const
        {
            DerivedJobQueueDiagnostics diagnostics = CountStatusesLocked();
            diagnostics.ApplyMainThreadCalls = ApplyMainThreadCalls;
            diagnostics.LastApplyMainThreadTimeNs = LastApplyMainThreadTimeNs;
            diagnostics.LastApplyMainThreadCompletedJobs =
                LastApplyMainThreadCompletedJobs;
            diagnostics.LastApplyMainThreadFailedJobs =
                LastApplyMainThreadFailedJobs;
            diagnostics.LastApplyMainThreadCancelledJobs =
                LastApplyMainThreadCancelledJobs;
            diagnostics.LastApplyMainThreadStaleDiscardedJobs =
                LastApplyMainThreadStaleDiscardedJobs;
            diagnostics.TotalApplyMainThreadCompletedJobs =
                TotalApplyMainThreadCompletedJobs;
            diagnostics.TotalApplyMainThreadFailedJobs =
                TotalApplyMainThreadFailedJobs;
            diagnostics.TotalApplyMainThreadCancelledJobs =
                TotalApplyMainThreadCancelledJobs;
            diagnostics.TotalApplyMainThreadStaleDiscardedJobs =
                TotalApplyMainThreadStaleDiscardedJobs;
            return diagnostics;
        }

        [[nodiscard]] DerivedJobSnapshot BuildSnapshotLocked(
            const DerivedJobHandle handle,
            const Record& record) const
        {
            const auto now = std::chrono::steady_clock::now();
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - record.SubmittedAt);

            const DerivedJobStatus status = SnapshotStatusLocked(record);
            const bool pendingRetainsPrevious =
                record.HasPreviousOutput &&
                (status == DerivedJobStatus::Blocked ||
                 status == DerivedJobStatus::Queued ||
                 status == DerivedJobStatus::Running ||
                 status == DerivedJobStatus::Applying ||
                 status == DerivedJobStatus::Failed ||
                 status == DerivedJobStatus::Cancelled ||
                 status == DerivedJobStatus::StaleDiscarded);

            return DerivedJobSnapshot{
                .Handle = handle,
                .Key = record.Key,
                .Name = record.Name,
                .RequestedJobDomain = record.RequestedDomain,
                .ResolvedJobDomain = record.ResolvedDomain,
                .Status = status,
                .ExecutionState = ExecutionStateLocked(record),
                .IsReadbackJob = record.IsReadbackJob,
                .Dependencies = record.Dependencies,
                .NormalizedProgress = record.Output.NormalizedProgress,
                .ProgressDeterminate = record.Output.ProgressDeterminate,
                .PreviousOutputRetained = record.PreviousOutputRetained || pendingRetainsPrevious,
                .PayloadToken = record.Output.PayloadToken,
                .ElapsedMilliseconds = static_cast<std::uint64_t>(std::max<std::int64_t>(
                    0,
                    elapsed.count())),
                .Diagnostic = record.Diagnostic,
            };
        }
    };

    std::string_view ToString(const DerivedJobStatus value) noexcept
    {
        switch (value)
        {
        case DerivedJobStatus::Blocked: return "Blocked";
        case DerivedJobStatus::Queued: return "Queued";
        case DerivedJobStatus::Running: return "Running";
        case DerivedJobStatus::Applying: return "Applying";
        case DerivedJobStatus::Complete: return "Complete";
        case DerivedJobStatus::Failed: return "Failed";
        case DerivedJobStatus::Cancelled: return "Cancelled";
        case DerivedJobStatus::StaleDiscarded: return "StaleDiscarded";
        }
        return "Unknown";
    }

    std::string_view ToString(const DerivedJobApplyValidation value) noexcept
    {
        switch (value)
        {
        case DerivedJobApplyValidation::Current: return "Current";
        case DerivedJobApplyValidation::MissingEntity: return "MissingEntity";
        case DerivedJobApplyValidation::StaleEntityGeneration: return "StaleEntityGeneration";
        case DerivedJobApplyValidation::StaleGeometryGeneration: return "StaleGeometryGeneration";
        case DerivedJobApplyValidation::StaleSourcePropertyGeneration: return "StaleSourcePropertyGeneration";
        case DerivedJobApplyValidation::StaleBindingGeneration: return "StaleBindingGeneration";
        case DerivedJobApplyValidation::Cancelled: return "Cancelled";
        }
        return "Unknown";
    }

    DerivedJobRegistry::DerivedJobRegistry(StreamingExecutor& executor)
        : m_Impl(std::make_shared<Impl>(executor))
    {
        std::scoped_lock lock(m_Impl->Mutex);
        m_Impl->Owner = this;
    }

    DerivedJobRegistry::~DerivedJobRegistry()
    {
        if (m_Impl != nullptr)
        {
            std::scoped_lock lock(m_Impl->Mutex);
            m_Impl->Owner = nullptr;
        }
    }

    DerivedJobHandle DerivedJobRegistry::Submit(DerivedJobDesc desc)
    {
        if (m_Impl == nullptr)
        {
            return {};
        }

        std::weak_ptr<Impl> weakImpl = m_Impl;
        DerivedJobHandle handle{};
        StreamingTaskDesc streaming{};

        {
            std::scoped_lock lock(m_Impl->Mutex);
            const std::uint32_t index = static_cast<std::uint32_t>(m_Impl->Records.size());
            Impl::Record record{};
            record.Key = std::move(desc.Key);
            record.Name = std::move(desc.Name);
            record.RequestedDomain = desc.RequestedJobDomain;
            record.ResolvedDomain = desc.RequestedJobDomain;
            record.Dependencies = std::move(desc.DependsOn);
            record.SubmittedAt = std::chrono::steady_clock::now();
            record.HasPreviousOutput = desc.HasPreviousOutput;
            record.PreviousOutputRetained = desc.HasPreviousOutput;
            record.IsReadbackJob = desc.IsReadbackJob;
            record.ReadbackByteSize = desc.ReadbackByteSize;
            record.Execute = std::move(desc.Execute);
            record.IsReadbackReady = std::move(desc.IsReadbackReady);
            record.ValidateOnMainThread = std::move(desc.ValidateOnMainThread);
            record.ApplyOnMainThread = std::move(desc.ApplyOnMainThread);
            handle = DerivedJobHandle{index, record.Generation};

            if (IsUnsupportedJobDomain(record.RequestedDomain))
            {
                record.Status = DerivedJobStatus::Failed;
                record.PreviousOutputRetained = record.HasPreviousOutput;
                record.Diagnostic = UnsupportedDomainDiagnostic(record.RequestedDomain);
                m_Impl->Records.push_back(std::move(record));
                return handle;
            }

            if (!record.Execute)
            {
                record.Status = DerivedJobStatus::Failed;
                record.PreviousOutputRetained = record.HasPreviousOutput;
                record.Diagnostic = "progressive derived job has no worker callback";
                m_Impl->Records.push_back(std::move(record));
                return handle;
            }

            if (record.IsReadbackJob && !record.IsReadbackReady)
            {
                record.Status = DerivedJobStatus::Failed;
                record.PreviousOutputRetained = record.HasPreviousOutput;
                record.Diagnostic = "progressive readback job has no readiness callback";
                m_Impl->Records.push_back(std::move(record));
                return handle;
            }
            if (record.IsReadbackJob && !record.ApplyOnMainThread)
            {
                record.Status = DerivedJobStatus::Failed;
                record.PreviousOutputRetained = record.HasPreviousOutput;
                record.Diagnostic = "progressive readback job has no apply callback";
                m_Impl->Records.push_back(std::move(record));
                return handle;
            }

            streaming.Name = record.Name;
            streaming.Kind = desc.Kind;
            streaming.Priority = desc.Priority;
            streaming.EstimatedCost = std::max<std::uint32_t>(1u, desc.EstimatedCost);
            streaming.CancellationGeneration = desc.CancellationGeneration;
            for (const DerivedJobDependency& dependency : record.Dependencies)
            {
                const auto resolved = m_Impl->Resolve(dependency.Job);
                if (resolved.has_value())
                {
                    const StreamingTaskHandle streamingDependency =
                        m_Impl->Records[*resolved].Streaming;
                    if (streamingDependency.IsValid())
                    {
                        streaming.DependsOn.push_back(streamingDependency);
                    }
                }
            }

            m_Impl->Records.push_back(std::move(record));
        }

        streaming.Execute = [weakImpl, handle]() mutable -> StreamingResult
        {
            auto impl = weakImpl.lock();
            if (impl == nullptr)
            {
                return std::unexpected(Core::ErrorCode::InvalidState);
            }

            std::move_only_function<DerivedJobWorkerResult()> execute{};
            {
                std::scoped_lock lock(impl->Mutex);
                const auto resolved = impl->Resolve(handle);
                if (!resolved.has_value())
                {
                    return std::unexpected(Core::ErrorCode::InvalidState);
                }

                auto& record = impl->Records[*resolved];
                for (const DerivedJobDependency& dependency : record.Dependencies)
                {
                    const auto parent = impl->Resolve(dependency.Job);
                    if (!parent.has_value() ||
                        impl->Records[*parent].Status != DerivedJobStatus::Complete)
                    {
                        record.Status = DerivedJobStatus::Failed;
                        record.PreviousOutputRetained = record.HasPreviousOutput;
                        record.Diagnostic = "dependency did not complete";
                        return std::unexpected(Core::ErrorCode::InvalidState);
                    }
                }

                execute = std::move(record.Execute);
            }

            if (!execute)
            {
                return std::unexpected(Core::ErrorCode::InvalidState);
            }

            DerivedJobWorkerResult worker = execute();
            {
                std::scoped_lock lock(impl->Mutex);
                const auto resolved = impl->Resolve(handle);
                if (!resolved.has_value())
                {
                    return std::unexpected(Core::ErrorCode::InvalidState);
                }

                auto& record = impl->Records[*resolved];
                if (!worker.has_value())
                {
                    record.Status = DerivedJobStatus::Failed;
                    record.PreviousOutputRetained = record.HasPreviousOutput;
                    record.Diagnostic = FailureDiagnostic(worker.error());
                    return std::unexpected(worker.error());
                }

                record.Output = std::move(*worker);
                record.HasWorkerOutput = true;
                record.Diagnostic = record.Output.Diagnostic;
                if (record.IsReadbackJob)
                {
                    return StreamingResult{StreamingReadbackRequest{
                        .PayloadToken = record.Output.PayloadToken,
                        .ByteSize = record.ReadbackByteSize,
                    }};
                }
                return StreamingResult{StreamingCpuPayloadReady{
                    .PayloadToken = record.Output.PayloadToken,
                }};
            }
        };

        streaming.ApplyOnMainThread = [weakImpl, handle](StreamingResult&& result) mutable
        {
            auto impl = weakImpl.lock();
            if (impl == nullptr)
            {
                return;
            }

            DerivedJobOutput output{};
            DerivedJobKey key{};
            DerivedJobRegistry* owner = nullptr;
            std::move_only_function<DerivedJobApplyValidation()> validate{};
            std::move_only_function<Core::Result(DerivedJobApplyContext&)> apply{};
            {
                std::scoped_lock lock(impl->Mutex);
                const auto resolved = impl->Resolve(handle);
                if (!resolved.has_value())
                {
                    return;
                }

                auto& record = impl->Records[*resolved];
                if (!result.has_value())
                {
                    record.Status = DerivedJobStatus::Failed;
                    record.PreviousOutputRetained = record.HasPreviousOutput;
                    record.Diagnostic = FailureDiagnostic(result.error());
                    return;
                }

                if (!record.HasWorkerOutput)
                {
                    record.Status = DerivedJobStatus::Failed;
                    record.PreviousOutputRetained = record.HasPreviousOutput;
                    record.Diagnostic = "worker completed without derived payload";
                    return;
                }

                record.Status = DerivedJobStatus::Applying;
                output = std::move(record.Output);
                key = record.Key;
                owner = impl->Owner;
                validate = std::move(record.ValidateOnMainThread);
                apply = std::move(record.ApplyOnMainThread);
            }

            const DerivedJobApplyValidation validation =
                validate ? validate() : DerivedJobApplyValidation::Current;
            if (validation != DerivedJobApplyValidation::Current)
            {
                std::scoped_lock lock(impl->Mutex);
                const auto resolved = impl->Resolve(handle);
                if (resolved.has_value())
                {
                    auto& record = impl->Records[*resolved];
                    record.Status = StatusForValidation(validation);
                    record.PreviousOutputRetained = record.HasPreviousOutput;
                    record.Diagnostic = std::string{"apply discarded: "} +
                                        std::string{ToString(validation)};
                }
                return;
            }

            Core::Result applyResult = Core::Ok();
            if (apply)
            {
                DerivedJobApplyContext context{
                    .Registry = owner,
                    .Handle = handle,
                    .Key = std::move(key),
                    .Output = std::move(output),
                };
                applyResult = apply(context);
            }

            std::scoped_lock lock(impl->Mutex);
            const auto resolved = impl->Resolve(handle);
            if (!resolved.has_value())
            {
                return;
            }

            auto& record = impl->Records[*resolved];
            if (!applyResult.has_value())
            {
                record.Status = DerivedJobStatus::Failed;
                record.PreviousOutputRetained = record.HasPreviousOutput;
                record.Diagnostic = FailureDiagnostic(applyResult.error());
                return;
            }

            record.Status = DerivedJobStatus::Complete;
            record.PreviousOutputRetained = false;
        };

        StreamingTaskHandle streamingHandle{};
        if (m_Impl->Executor != nullptr)
        {
            streamingHandle = m_Impl->Executor->Submit(std::move(streaming));
        }

        {
            std::scoped_lock lock(m_Impl->Mutex);
            const auto resolved = m_Impl->Resolve(handle);
            if (resolved.has_value())
            {
                auto& record = m_Impl->Records[*resolved];
                record.Streaming = streamingHandle;
                if (!streamingHandle.IsValid() && !IsTerminal(record.Status))
                {
                    record.Status = DerivedJobStatus::Failed;
                    record.PreviousOutputRetained = record.HasPreviousOutput;
                    record.Diagnostic = "streaming executor rejected derived job";
                }
            }
        }

        return handle;
    }

    DerivedJobHandle DerivedJobRegistry::SubmitFollowUp(
        const DerivedJobHandle parent,
        DerivedJobDesc desc,
        std::string reason)
    {
        const bool alreadyDependsOnParent = std::any_of(
            desc.DependsOn.begin(),
            desc.DependsOn.end(),
            [parent](const DerivedJobDependency& dependency)
            {
                return dependency.Job == parent;
            });
        if (!alreadyDependsOnParent)
        {
            desc.DependsOn.push_back(DerivedJobDependency{
                .Job = parent,
                .Reason = std::move(reason),
            });
        }
        return Submit(std::move(desc));
    }

    void DerivedJobRegistry::Cancel(const DerivedJobHandle handle)
    {
        if (m_Impl == nullptr)
        {
            return;
        }

        StreamingTaskHandle streaming{};
        {
            std::scoped_lock lock(m_Impl->Mutex);
            const auto resolved = m_Impl->Resolve(handle);
            if (!resolved.has_value())
            {
                return;
            }

            auto& record = m_Impl->Records[*resolved];
            if (IsTerminal(record.Status))
            {
                return;
            }

            record.Status = DerivedJobStatus::Cancelled;
            record.PreviousOutputRetained = record.HasPreviousOutput;
            record.Diagnostic = "cancelled";
            streaming = record.Streaming;
        }

        if (m_Impl->Executor != nullptr && streaming.IsValid())
        {
            m_Impl->Executor->Cancel(streaming);
        }
    }

    std::uint32_t DerivedJobRegistry::CancelForEntity(const std::uint32_t entityId)
    {
        if (m_Impl == nullptr)
        {
            return 0u;
        }

        std::vector<DerivedJobHandle> handles{};
        {
            std::scoped_lock lock(m_Impl->Mutex);
            for (std::uint32_t index = 0; index < m_Impl->Records.size(); ++index)
            {
                const auto& record = m_Impl->Records[index];
                if (record.Key.EntityId == entityId && !IsTerminal(record.Status))
                {
                    handles.push_back(DerivedJobHandle{index, record.Generation});
                }
            }
        }

        for (const DerivedJobHandle handle : handles)
        {
            Cancel(handle);
        }
        return static_cast<std::uint32_t>(handles.size());
    }

    void DerivedJobRegistry::Pump(const std::uint32_t maxLaunches)
    {
        if (m_Impl != nullptr && m_Impl->Executor != nullptr)
        {
            m_Impl->Executor->PumpBackground(maxLaunches);
        }
    }

    void DerivedJobRegistry::DrainCompletions()
    {
        if (m_Impl != nullptr && m_Impl->Executor != nullptr)
        {
            m_Impl->Executor->DrainCompletions();
        }
    }

    void DerivedJobRegistry::DrainReadbacks()
    {
        if (m_Impl == nullptr || m_Impl->Executor == nullptr)
        {
            return;
        }

        std::vector<StreamingTaskHandle> ready{};
        {
            std::scoped_lock lock(m_Impl->Mutex);
            for (Impl::Record& record : m_Impl->Records)
            {
                if (!record.IsReadbackJob ||
                    IsTerminal(record.Status) ||
                    !record.Streaming.IsValid())
                {
                    continue;
                }

                if (m_Impl->Executor->GetState(record.Streaming) != StreamingTaskState::WaitingForReadback)
                {
                    continue;
                }

                if (!record.IsReadbackReady)
                {
                    record.Status = DerivedJobStatus::Failed;
                    record.PreviousOutputRetained = record.HasPreviousOutput;
                    record.Diagnostic = "readback job lost readiness callback";
                    continue;
                }

                if (record.IsReadbackReady())
                {
                    ready.push_back(record.Streaming);
                }
            }
        }

        for (const StreamingTaskHandle handle : ready)
        {
            (void)m_Impl->Executor->ResumeReadback(handle);
        }
    }

    void DerivedJobRegistry::ApplyMainThreadResults()
    {
        if (m_Impl == nullptr || m_Impl->Executor == nullptr)
        {
            return;
        }

        DerivedJobQueueDiagnostics before{};
        {
            std::scoped_lock lock(m_Impl->Mutex);
            before = m_Impl->CountStatusesLocked();
        }

        const auto applyBegin = std::chrono::steady_clock::now();
        m_Impl->Executor->ApplyMainThreadResults();
        const std::uint64_t elapsedNs = ElapsedNs(applyBegin);

        std::scoped_lock lock(m_Impl->Mutex);
        const DerivedJobQueueDiagnostics after = m_Impl->CountStatusesLocked();
        const std::uint64_t completed =
            PositiveDelta(after.CompleteJobs, before.CompleteJobs);
        const std::uint64_t failed =
            PositiveDelta(after.FailedJobs, before.FailedJobs);
        const std::uint64_t cancelled =
            PositiveDelta(after.CancelledJobs, before.CancelledJobs);
        const std::uint64_t staleDiscarded =
            PositiveDelta(after.StaleDiscardedJobs, before.StaleDiscardedJobs);

        ++m_Impl->ApplyMainThreadCalls;
        m_Impl->LastApplyMainThreadTimeNs = elapsedNs;
        m_Impl->LastApplyMainThreadCompletedJobs = completed;
        m_Impl->LastApplyMainThreadFailedJobs = failed;
        m_Impl->LastApplyMainThreadCancelledJobs = cancelled;
        m_Impl->LastApplyMainThreadStaleDiscardedJobs = staleDiscarded;
        m_Impl->TotalApplyMainThreadCompletedJobs += completed;
        m_Impl->TotalApplyMainThreadFailedJobs += failed;
        m_Impl->TotalApplyMainThreadCancelledJobs += cancelled;
        m_Impl->TotalApplyMainThreadStaleDiscardedJobs += staleDiscarded;
    }

    DerivedJobStatus DerivedJobRegistry::GetStatus(const DerivedJobHandle handle) const
    {
        if (m_Impl == nullptr)
        {
            return DerivedJobStatus::Cancelled;
        }

        std::scoped_lock lock(m_Impl->Mutex);
        const auto resolved = m_Impl->Resolve(handle);
        if (!resolved.has_value())
        {
            return DerivedJobStatus::Cancelled;
        }
        return m_Impl->SnapshotStatusLocked(m_Impl->Records[*resolved]);
    }

    std::optional<DerivedJobSnapshot> DerivedJobRegistry::Snapshot(
        const DerivedJobHandle handle) const
    {
        if (m_Impl == nullptr)
        {
            return std::nullopt;
        }

        std::scoped_lock lock(m_Impl->Mutex);
        const auto resolved = m_Impl->Resolve(handle);
        if (!resolved.has_value())
        {
            return std::nullopt;
        }
        return m_Impl->BuildSnapshotLocked(handle, m_Impl->Records[*resolved]);
    }

    DerivedJobQueueSnapshot DerivedJobRegistry::SnapshotAll() const
    {
        DerivedJobQueueSnapshot snapshot{};
        if (m_Impl == nullptr)
        {
            return snapshot;
        }

        std::scoped_lock lock(m_Impl->Mutex);
        snapshot.Readbacks = m_Impl->BuildReadbackDiagnosticsLocked();
        snapshot.Diagnostics = m_Impl->BuildDiagnosticsLocked();
        snapshot.Entries.reserve(m_Impl->Records.size());
        for (std::uint32_t index = 0; index < m_Impl->Records.size(); ++index)
        {
            const auto& record = m_Impl->Records[index];
            snapshot.Entries.push_back(m_Impl->BuildSnapshotLocked(
                DerivedJobHandle{index, record.Generation},
                record));
        }
        return snapshot;
    }

    DerivedJobQueueSnapshot DerivedJobRegistry::SnapshotEntity(
        const std::uint32_t entityId) const
    {
        DerivedJobQueueSnapshot snapshot{};
        if (m_Impl == nullptr)
        {
            return snapshot;
        }

        std::scoped_lock lock(m_Impl->Mutex);
        snapshot.Readbacks = m_Impl->BuildReadbackDiagnosticsLocked();
        snapshot.Diagnostics = m_Impl->BuildDiagnosticsLocked();
        for (std::uint32_t index = 0; index < m_Impl->Records.size(); ++index)
        {
            const auto& record = m_Impl->Records[index];
            if (record.Key.EntityId == entityId)
            {
                snapshot.Entries.push_back(m_Impl->BuildSnapshotLocked(
                    DerivedJobHandle{index, record.Generation},
                    record));
            }
        }
        return snapshot;
    }
}
