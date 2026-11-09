// Point-set registration of named point bindings through shared processing commands: iterative closest point (ICP)
// and Coherent Point Drift (CPD, RUNTIME-273). Both run as step runs of the session's StepRunsService (one step: one
// ICP or EM iteration) with the method config's shared execution options, and publish once, on Accept.
module;
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <glm/glm.hpp>
export module Extrinsic.Runtime.RegistrationOperations;
export import Extrinsic.Runtime.EditorProcessing;
export import Extrinsic.Runtime.EditorCommon;
export import Extrinsic.Runtime.RegistrationConfig;
export import Extrinsic.Runtime.CoherentPointDriftConfig;
export import Extrinsic.Core.Error;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.EditorWorkspaceAttachment;
export namespace Extrinsic::Runtime
{
    using EditorRegistrationCommand = RegistrationConfig;
    [[nodiscard]] const char* DebugNameForEditorICPVariant(EditorICPVariant variant) noexcept;

    struct EditorRegistrationResult
    {
        EditorCommandStatus Status{EditorCommandStatus::NoChange};
        bool HasResult{false};
        RegistrationBackend RequestedBackend{RegistrationBackend::CpuKDTree};
        RegistrationBackend ActualBackend{RegistrationBackend::CpuKDTree};
        bool FellBackToCPU{};
        bool TargetIndexReused{};
        std::string BackendDiagnostic{};
        // The variant the command asked for.
        EditorICPVariant Variant{EditorICPVariant::PointToPoint};
        // The variant the solver actually ran. Runtime preflight rejects a
        // point-to-plane request when it cannot supply valid target normals,
        // preventing `Geometry.Registration`'s point-to-point fallback from
        // becoming silent; successful requested/effective variants agree.
        EditorICPVariant EffectiveVariant{EditorICPVariant::PointToPoint};
        std::size_t SourcePointCount{0u};
        std::size_t TargetPointCount{0u};
        // Target normals resolved, transformed to world space, and handed to
        // the solver. Zero for a point-to-point run.
        std::size_t TargetNormalCount{0u};
        std::size_t IterationsPerformed{0u};
        std::size_t TrajectoryLength{0u};
        std::size_t AppliedStep{0u};
        double FinalRMSE{0.0};
        bool Converged{false};
        std::size_t FinalInlierCount{0u};
        // The step run (run_control, run_observe); 0 for an immediate or refused outcome.
        std::uint64_t StepRun{0u};
        Core::ErrorCode Error{Core::ErrorCode::Success};
        std::string Message{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == EditorCommandStatus::Applied;
        }
    };

    // Incomplete borrowed containers keep sibling workspace features independent
    // of registration records; prepared frames copy their values.
    extern "C++"
    {
        struct EditorRegistrationResultSinks
        {
            std::function<void()> DismissResult{};
            std::function<void(EditorRegistrationResult)> Registration{};
        };
        struct EditorRegistrationResultsSnapshot
        {
            std::optional<EditorRegistrationResult> LastRegistrationResult{};
        };
    }
    struct EditorRegistrationPreparedFrame
    {
        EditorProcessingCommands Commands{};
        EditorRegistrationResultSinks ResultSinks{};
        EditorRegistrationResultsSnapshot Results{};
    };
    [[nodiscard]] EditorRegistrationPreparedFrame PrepareEditorRegistrationFrame(const EditorWorkspaceAttachment&);

    [[nodiscard]] ActionReadiness PreviewEditorRegistrationCommand(
        const EditorProcessingCommands&, const EditorRegistrationCommand&);
    // Why Vulkan LBVH correspondences would fall back to the CPU here (empty when they can run),
    // so the panel can say so before a run starts.
    [[nodiscard]] std::string RegistrationVulkanUnavailableReason(const EditorProcessingCommands&);
    // ICP needs a solvable correspondence problem, so this catalog keeps only
    // vec3 bindings carrying at least three live finite samples. It is
    // deliberately narrower than the shared point-input catalog.
    [[nodiscard]] GeometryPropertyCatalogSnapshot GetEditorRegistrationInputCatalog(
        const EditorProcessingCommands&, std::uint32_t stableId);
    // UI-067: the panel's view of an ICP run (main thread). The caller creates it and passes it to the command; the
    // run fills in its step run, the pre-aligned source in world space, and the pose of each preview it shows (16
    // floats per copy, at the run's preview_every/preview_interval_ms; none without a view). The trace is the step
    // run's metric log: rmse, inliers, seconds since the first step and the step's seconds. Controls are the step
    // run's (run_control).
    struct EditorRegistrationProgressSnapshot
    {
        std::uint64_t StepRun{0u};
        std::shared_ptr<const std::vector<glm::vec3>> SourceWorld{};
        glm::mat4 Pose{1.0f};
        std::uint64_t Revision{0u};
    };
    struct EditorRegistrationProgress;
    using EditorRegistrationProgressHandle = std::shared_ptr<EditorRegistrationProgress>;
    [[nodiscard]] EditorRegistrationProgressHandle MakeEditorRegistrationProgress();
    [[nodiscard]] EditorRegistrationProgressSnapshot SnapshotEditorRegistrationProgress(
        const EditorRegistrationProgressHandle&);

    // Starts a step run with the command's execution options (start ready or at once; automatic Accept never, at
    // convergence, or at its end). A CPU run steps on a worker; a Vulkan run records its correspondence batches on the
    // main thread. The source transform changes only on Accept, as one undoable entry after a full revalidation.
    // Without a job lane the run computes and accepts at once (start=run, auto_accept=end only). Immediate outcomes
    // return directly; a started run delivers one terminal result to `onComplete` (accepted, discarded, stale or
    // failed), while attached. Duplicate Pending requests add no callback.
    [[nodiscard]] EditorRegistrationResult ApplyEditorRegistrationCommand(
        const EditorProcessingCommands&, const EditorRegistrationCommand&,
        std::function<void(EditorRegistrationResult)> onComplete = {},
        EditorRegistrationProgressHandle progress = {});
    [[nodiscard]] RuntimeEngineConfigApplyResult ApplyEditorRegistrationConfig(
        const EditorProcessingCommands&, const RegistrationConfig&, std::string sourceId = {});
    [[nodiscard]] std::optional<RegistrationConfig> GetEditorRegistrationConfig(
        const EditorProcessingCommands&);
    [[nodiscard]] EditorRegistrationResult ApplyEditorConfiguredRegistrationCommand(
        const EditorProcessingCommands&, std::function<void(EditorRegistrationResult)> onComplete = {},
        EditorRegistrationProgressHandle progress = {});

    // --- Coherent Point Drift (RUNTIME-273) -------------------------------------------
    // A run captures both point sets in world space and is a step run of the session's StepRunsService: one step is
    // one EM iteration (a worker job; with the Vulkan E-step a coroutine that parks while the device E-step is
    // recorded on the main thread), controlled by run_control or the panel's step-run row. It streams a per-iteration
    // trace and, at the run's preview cadence, the moving source positions, and publishes only on Accept, as one
    // undoable history entry after revalidating the inputs and entity transforms.
    struct EditorCoherentPointDriftTrace
    {
        std::uint32_t Iteration{0u};
        double Sigma2{0.0};                // world units^2
        double NegativeLogLikelihood{0.0};
        double Objective{0.0};             // NLL plus the nonrigid coherence term
        double MatchedWeight{0.0};         // sum of inlier responsibilities
        std::string EStep{"reference"};    // E-step policy that ran this iteration
        double EStepErrorBound{0.0};       // its max relative responsibility error (0: exact)
        double EStepSampledError{0.0};     // Nystrom: sampled relative error (estimate, not a bound)
        bool NystromLocal{false};        // planned transition to bounded neighbor sums
        std::uint64_t KernelEvaluations{0u};
        double Seconds{0.0};               // UI-067: since the run's first step began
        double IterationSeconds{0.0};      // this iteration's wall time
    };

    struct EditorCoherentPointDriftResult
    {
        EditorCommandStatus Status{EditorCommandStatus::NoChange};
        CoherentPointDriftMethod Method{CoherentPointDriftMethod::Rigid};
        CoherentPointDriftOutput Output{CoherentPointDriftOutput::SourceTransform};
        std::string Backend{"cpu_reference"};
        std::string Termination{"none"};
        std::size_t SourcePointCount{0u};
        std::size_t TargetPointCount{0u};
        std::uint32_t Iterations{0u};
        double Sigma2{0.0};
        double NegativeLogLikelihood{0.0};
        double MatchedWeight{0.0};
        // World-space source->target map for rigid and affine runs.
        glm::dmat4 Transform{1.0};
        double MeanDisplacement{0.0}; // mean |T(y) - y| in world units
        double EStepErrorBound{0.0};  // max over the run (0: exact)
        double EStepSampledError{0.0}; // Nystrom: max sampled error estimate over the run
        // Vulkan: iterations meant for the device that ran on the CPU, and why the last one did.
        std::uint32_t EStepFallbacks{0u};
        std::uint32_t EStepDeviceIterations{0u};
        std::uint64_t EStepDeviceCpuRows{0u}; // rows of device iterations evaluated on the CPU
        std::string GpuDiagnostic{};
        // Bayesian subsamples (RUNTIME-289): the backend asked for, the one that orders each set (for
        // a run that ends before solving, the one that was to order it; empty: not subsampled), and
        // why a set asked on Vulkan ran on the CPU.
        std::string SubsampleRequestedBackend{"cpu_reference"};
        std::string SubsampleTargetBackend{}, SubsampleSourceBackend{};
        std::string SubsampleBackendDiagnostic{};
        std::uint32_t KernelRank{0u}; // low-rank nonrigid eigenpairs
        double KernelApproximationError{0.0};
        std::uint64_t StepRun{0u}; // the step run (run_control, run_observe); 0 for an immediate outcome
        std::string Message{};

        [[nodiscard]] bool Succeeded() const noexcept { return Status == EditorCommandStatus::Applied; }
    };

    // A run's view (its phase is its step run's: StepRunsService::Observe(StepRun)). Result is Pending until the run
    // ends, then the terminal result.
    struct EditorCoherentPointDriftSnapshot
    {
        std::uint64_t StepRun{0u};
        EditorCoherentPointDriftResult Result{};
        std::vector<EditorCoherentPointDriftTrace> Trace{};
        // Shared and immutable, so a per-frame snapshot copies no points.
        std::shared_ptr<const std::vector<glm::vec3>> SourcePreview{}; // the shown T(y), world space, capture order
        std::shared_ptr<const std::vector<glm::vec3>> Target{};        // fixed points, world space
        // BCPD subsamples (UI-067): indices into SourcePreview and Target (null: none).
        std::shared_ptr<const std::vector<std::uint32_t>> SourceSamples{}, TargetSamples{};
        std::uint64_t Revision{0u};             // increments with every update (not with Stage)
        // UI-067: the stage a running iteration is in ("preparing", "building_kernel",
        // "expectation_step", ...) and when it began, for a live progress line.
        std::string Stage{};
        std::chrono::steady_clock::time_point StageStarted{};
        // When the run's first step began (default-constructed before then).
        std::chrono::steady_clock::time_point RunStarted{};
    };

    struct EditorCoherentPointDriftRun;
    using EditorCoherentPointDriftRunHandle = std::shared_ptr<EditorCoherentPointDriftRun>;

    [[nodiscard]] ActionReadiness PreviewEditorCoherentPointDriftCommand(
        const EditorProcessingCommands&, const CoherentPointDriftConfig&);
    // Why the Vulkan E-step would run on the CPU here (empty when it can run on the device), so
    // the panel can say so before a run starts.
    [[nodiscard]] std::string CoherentPointDriftVulkanUnavailableReason(const EditorProcessingCommands&);
    // Validates the config, captures both point sets and starts the step run with the config's execution options
    // (start ready or at once; automatic Accept never, at convergence or at its end). Needs the job lane and the
    // session's step runs; on failure returns null and fills `failure`.
    [[nodiscard]] EditorCoherentPointDriftRunHandle StartEditorCoherentPointDrift(
        const EditorProcessingCommands&, const CoherentPointDriftConfig&, EditorCoherentPointDriftResult& failure);
    [[nodiscard]] EditorCoherentPointDriftSnapshot SnapshotEditorCoherentPointDrift(const EditorCoherentPointDriftRunHandle&);
    // The one-shot command: a step run that computes at once and accepts at its end, whatever the config's start and
    // auto_accept; onComplete receives its terminal result. Without a job lane it iterates and publishes at once
    // (the immediate outcome returns directly).
    [[nodiscard]] EditorCoherentPointDriftResult ApplyEditorCoherentPointDriftCommand(
        const EditorProcessingCommands&, const CoherentPointDriftConfig&,
        std::function<void(EditorCoherentPointDriftResult)> onComplete = {});
    [[nodiscard]] EditorCoherentPointDriftResult ApplyEditorConfiguredCoherentPointDrift(
        const EditorProcessingCommands&, std::function<void(EditorCoherentPointDriftResult)> onComplete = {});
    [[nodiscard]] RuntimeEngineConfigApplyResult ApplyEditorCoherentPointDriftConfig(
        const EditorProcessingCommands&, const CoherentPointDriftConfig&, std::string sourceId = {});
    [[nodiscard]] std::optional<CoherentPointDriftConfig> GetEditorCoherentPointDriftConfig(
        const EditorProcessingCommands&);
}
