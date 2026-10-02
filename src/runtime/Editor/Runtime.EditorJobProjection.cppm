// Shared copied job records and command handles for runtime and editor snapshots.
module;

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

export module Extrinsic.Runtime.EditorJobProjection;

import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.JobService;

export namespace Extrinsic::Runtime
{
    enum class EditorJobScope : std::uint8_t
    {
        Unknown,
        MeshVertex,
        MeshEdge,
        MeshHalfedge,
        MeshFace,
        MeshSurface,
        GraphNode,
        GraphHalfedge,
        GraphEdge,
        PointCloudPoint,
    };
    [[nodiscard]] EditorJobScope ToEditorJobScope(
        GeometryElementDomain domain) noexcept;
    struct EditorJobIdentity
    {
        std::uint32_t EntityId{0u};
        EditorJobScope Scope{EditorJobScope::Unknown};
        GeometryPresentationSlotSemantic OutputSemantic{GeometryPresentationSlotSemantic::Albedo};
        std::string OutputName{};
    };
    [[nodiscard]] bool SameEditorJobOutput(
        const EditorJobIdentity& lhs,
        const EditorJobIdentity& rhs) noexcept;
    [[nodiscard]] bool IsActiveEditorJobState(JobState state) noexcept;
    [[nodiscard]] bool IsFailedEditorJobState(JobState state) noexcept;
    enum class EditorJobDomain : std::uint8_t
    {
        Cpu,
        GpuCompute,
        GpuGraphics,
        Auto,
    };
    struct EditorJobRecord
    {
        JobToken Token{};
        EditorJobIdentity Identity{};
        // `JobDesc::CorrelationId` of a service run (K-Means, consolidation);
        // 0 for jobs submitted through `EditorJobCommandSurface::Submit`.
        std::uint64_t CorrelationId{0u};
        std::string Name{};
        JobState State{JobState::Invalid};
        EditorJobDomain RequestedJobDomain{EditorJobDomain::Cpu};
        EditorJobDomain ResolvedJobDomain{EditorJobDomain::Cpu};
        std::vector<JobDependency> Dependencies{};
        float NormalizedProgress{0.0f};
        // False until the worker reports; "never reported" is not 0%.
        bool ProgressDeterminate{false};
        bool PreviousOutputRetained{false};
        std::uint64_t PayloadToken{0u};
        std::uint64_t ElapsedMilliseconds{0u};
        std::string Diagnostic{};
    };
    struct EditorJobQueueSnapshot
    {
        std::vector<EditorJobRecord> Entries{};
    };
    enum class EditorOperationState : std::uint8_t
    {
        None,
        Queued,
        Running,
        Succeeded,
        Failed,
        Cancelled,
    };
    // One run of one operation as a panel, the Jobs window and the agent lane
    // read it. Pure projection of a job record; `None` means "nothing to show".
    struct EditorOperationProgress
    {
        EditorOperationState State{EditorOperationState::None};
        bool Determinate{false};
        float Normalized{0.0f};
        double ElapsedSeconds{0.0};
        std::string Label{};
        std::string Diagnostic{};
        // Session scene epoch the answer belongs to (also set for `None`);
        // it changes when the scene is replaced (new, load, close) or the
        // workspace is reattached, so a panel can drop what it remembered.
        std::uint64_t Epoch{0u};
    };
    // A service run's command correlation id (`CommandCorrelationId::Value`),
    // restated here so the projection does not import the command bus.
    struct EditorRunCorrelation
    {
        std::uint64_t Value{0u};
        [[nodiscard]] bool IsValid() const noexcept { return Value != 0u; }
    };
    // The output a method writes, as the panel knows it from its own config:
    // the entity and the output property name, whatever scope or semantic the
    // runtime filed the job under. The newest run writing it answers.
    struct EditorOutputRef
    {
        std::uint32_t EntityId{0u};
        std::string OutputName{};
    };
    // Which run a panel is watching. A `JobToken` names exactly one job (the
    // caller captured it at submit); an `EditorJobIdentity` names a full output
    // identity and an `EditorOutputRef` just the entity and output name, so the
    // newest run of that output answers; an `EditorRunCorrelation` names a
    // service run (K-Means, consolidation) that stamped it on its job(s).
    using EditorOperationRunKey =
        std::variant<EditorJobIdentity, EditorRunCorrelation, JobToken, EditorOutputRef>;
    [[nodiscard]] EditorOperationState ToEditorOperationState(
        JobState state) noexcept;
    [[nodiscard]] EditorOperationProgress ProjectEditorOperationProgress(
        const EditorJobRecord& job);
    // Progress of one run among `records`. An identity key never matches a
    // record without an identity (a correlation-only service job). A service
    // run can chain jobs, so the newest active match wins, else the newest
    // terminal one; never the oldest job of anyone else. `None` for a key that
    // matches nothing.
    [[nodiscard]] EditorOperationProgress ResolveEditorOperationProgress(
        const std::vector<EditorJobRecord>& records,
        const EditorOperationRunKey& key);
    // The record `ResolveEditorOperationProgress` projects: the job a key names
    // among `records`, or nullopt.
    [[nodiscard]] std::optional<EditorJobRecord> FindEditorOperationRun(
        const std::vector<EditorJobRecord>& records,
        const EditorOperationRunKey& key);

    // Answer of `EditorJobCommandSurface::Cancel`.
    enum class EditorJobCancelStatus : std::uint8_t
    {
        // `JobService::Cancel` accepted it; the job ends `Cancelled` on a later
        // drain and its unpublished finalizer delivers the terminal result once.
        Requested,
        // An editor job that already ended or whose cancel was already requested.
        NotActive,
        // Unknown or reaped token, or a job the editor did not submit through
        // `Submit` (asset decode, scene files, K-Means and consolidation runs).
        NotEditorJob,
        // Detached attachment (stale epoch) or no job service.
        Unavailable,
    };
    [[nodiscard]] std::string_view ToString(EditorJobCancelStatus status) noexcept;

    struct EditorJobCommandSurface
    {
        std::function<JobToken(JobDesc, EditorJobIdentity)> Submit{};
        std::function<std::optional<EditorJobRecord>(
            const EditorJobIdentity&)>
            FindActive{};
        std::function<std::vector<EditorJobRecord>(std::uint32_t)>
            SnapshotEntity{};
        // Every job submitted through `Submit` that the service still retains,
        // ordered by token; exactly the jobs `Cancel` accepts.
        std::function<std::vector<EditorJobRecord>()> SnapshotAll{};
        // Cancels a job submitted through `Submit`; never any other job.
        std::function<EditorJobCancelStatus(JobToken)> Cancel{};
        // `State::None` for an unknown, stale-epoch or pruned key.
        std::function<EditorOperationProgress(const EditorOperationRunKey&)>
            Progress{};
        // Main-thread progress report for a job whose work does not run in
        // `JobService::Work` (device-polled transactions); workers use
        // `JobCancellation::ReportProgress`. No-op for tokens this session
        // did not submit.
        std::function<void(JobToken, JobProgress)> ReportProgress{};

        [[nodiscard]] bool Available() const noexcept
        {
            return static_cast<bool>(Submit);
        }
    };

    struct EditorRunCancelCount
    {
        std::uint32_t Requested{0u}; // cancels `Cancel` accepted
        std::uint32_t Refused{0u};   // matching jobs that had already ended or were being cancelled
        bool Unavailable{false};     // no surface (stale attachment epoch, no job service)
    };
    // Cancels every active job of `surface` that writes one of `outputs`, so a
    // run's later stages (submitted after the run started, under the same
    // output identity) are cancelled with it. Only jobs `SnapshotAll` lists,
    // i.e. editor jobs, are reached.
    [[nodiscard]] EditorRunCancelCount CancelEditorOutputRuns(
        const EditorJobCommandSurface& surface,
        const std::vector<EditorJobIdentity>& outputs);
}
