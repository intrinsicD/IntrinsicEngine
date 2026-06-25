module;

#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

export module Extrinsic.Runtime.DerivedJobGraph;

import Extrinsic.Core.Error;
import Extrinsic.Core.Dag.Scheduler;
import Extrinsic.Core.StrongHandle;
import Extrinsic.Runtime.ProgressiveRenderData;
import Extrinsic.Runtime.StreamingExecutor;

export namespace Extrinsic::Runtime
{
    struct DerivedJobTag;
    using DerivedJobHandle = Core::StrongHandle<DerivedJobTag>;

    enum class DerivedJobStatus : std::uint8_t
    {
        Blocked,
        Queued,
        Running,
        Applying,
        Complete,
        Failed,
        Cancelled,
        StaleDiscarded,
    };

    enum class DerivedJobApplyValidation : std::uint8_t
    {
        Current,
        MissingEntity,
        StaleEntityGeneration,
        StaleGeometryGeneration,
        StaleSourcePropertyGeneration,
        StaleBindingGeneration,
        Cancelled,
    };

    struct DerivedJobKey
    {
        std::uint32_t EntityId{0u};
        ProgressiveGeometryDomain Domain{ProgressiveGeometryDomain::Unknown};
        ProgressiveSlotSemantic OutputSemantic{ProgressiveSlotSemantic::Albedo};
        std::uint64_t EntityGeneration{0u};
        std::uint64_t GeometryGeneration{0u};
        std::uint64_t SourcePropertyGeneration{0u};
        std::uint64_t BindingGeneration{0u};
        std::string OutputName{};
    };

    struct DerivedJobOutput
    {
        std::uint64_t PayloadToken{0u};
        float NormalizedProgress{1.0f};
        bool ProgressDeterminate{true};
        std::string Diagnostic{};
    };

    using DerivedJobWorkerResult = std::expected<DerivedJobOutput, Core::ErrorCode>;

    struct DerivedJobDependency
    {
        DerivedJobHandle Job{};
        std::string Reason{};
    };

    class DerivedJobRegistry;

    struct DerivedJobApplyContext
    {
        DerivedJobRegistry* Registry{nullptr};
        DerivedJobHandle Handle{};
        DerivedJobKey Key{};
        DerivedJobOutput Output{};
    };

    struct DerivedJobDesc
    {
        DerivedJobKey Key{};
        std::string Name{};
        ProgressiveJobDomain RequestedJobDomain{ProgressiveJobDomain::Cpu};
        Core::Dag::TaskKind Kind{Core::Dag::TaskKind::GeometryProcess};
        Core::Dag::TaskPriority Priority{Core::Dag::TaskPriority::Normal};
        std::uint32_t EstimatedCost{1u};
        std::uint64_t CancellationGeneration{0u};
        bool HasPreviousOutput{false};
        bool IsReadbackJob{false};
        std::uint64_t ReadbackByteSize{0u};
        std::vector<DerivedJobDependency> DependsOn{};
        std::move_only_function<DerivedJobWorkerResult()> Execute{};
        std::move_only_function<bool()> IsReadbackReady{};
        std::move_only_function<DerivedJobApplyValidation()> ValidateOnMainThread{};
        std::move_only_function<Core::Result(DerivedJobApplyContext&)> ApplyOnMainThread{};
    };

    struct DerivedJobReadbackDiagnostics
    {
        std::uint64_t Issued{0u};
        std::uint64_t Waiting{0u};
        std::uint64_t Completed{0u};
        std::uint64_t Failed{0u};
        std::uint64_t StaleOrCancelled{0u};
    };

    struct DerivedJobSnapshot
    {
        DerivedJobHandle Handle{};
        DerivedJobKey Key{};
        std::string Name{};
        ProgressiveJobDomain RequestedJobDomain{ProgressiveJobDomain::Cpu};
        ProgressiveJobDomain ResolvedJobDomain{ProgressiveJobDomain::Cpu};
        DerivedJobStatus Status{DerivedJobStatus::Queued};
        StreamingTaskState ExecutionState{StreamingTaskState::Pending};
        bool IsReadbackJob{false};
        std::vector<DerivedJobDependency> Dependencies{};
        float NormalizedProgress{0.0f};
        bool ProgressDeterminate{true};
        bool PreviousOutputRetained{false};
        std::uint64_t PayloadToken{0u};
        std::uint64_t ElapsedMilliseconds{0u};
        std::string Diagnostic{};
    };

    struct DerivedJobQueueSnapshot
    {
        std::vector<DerivedJobSnapshot> Entries{};
        DerivedJobReadbackDiagnostics Readbacks{};
    };

    [[nodiscard]] std::string_view ToString(DerivedJobStatus value) noexcept;
    [[nodiscard]] std::string_view ToString(DerivedJobApplyValidation value) noexcept;

    class DerivedJobRegistry
    {
    public:
        explicit DerivedJobRegistry(StreamingExecutor& executor);
        ~DerivedJobRegistry();

        DerivedJobRegistry(const DerivedJobRegistry&) = delete;
        DerivedJobRegistry& operator=(const DerivedJobRegistry&) = delete;

        [[nodiscard]] DerivedJobHandle Submit(DerivedJobDesc desc);
        [[nodiscard]] DerivedJobHandle SubmitFollowUp(
            DerivedJobHandle parent,
            DerivedJobDesc desc,
            std::string reason);

        void Cancel(DerivedJobHandle handle);
        std::uint32_t CancelForEntity(std::uint32_t entityId);
        void Pump(std::uint32_t maxLaunches);
        void DrainCompletions();
        void DrainReadbacks();
        void ApplyMainThreadResults();

        [[nodiscard]] DerivedJobStatus GetStatus(DerivedJobHandle handle) const;
        [[nodiscard]] std::optional<DerivedJobSnapshot> Snapshot(DerivedJobHandle handle) const;
        [[nodiscard]] DerivedJobQueueSnapshot SnapshotAll() const;
        [[nodiscard]] DerivedJobQueueSnapshot SnapshotEntity(std::uint32_t entityId) const;

    private:
        struct Impl;
        std::shared_ptr<Impl> m_Impl{};
    };
}
