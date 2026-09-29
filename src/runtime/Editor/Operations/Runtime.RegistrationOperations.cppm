// Point-set registration of named point bindings through shared processing commands:
// iterative closest point (ICP) and Coherent Point Drift (CPD, RUNTIME-273).
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
    // ICP needs a solvable correspondence problem, so this catalog keeps only
    // vec3 bindings carrying at least three live finite samples. It is
    // deliberately narrower than the shared point-input catalog.
    [[nodiscard]] GeometryPropertyCatalogSnapshot GetEditorRegistrationInputCatalog(
        const EditorProcessingCommands&, std::uint32_t stableId);
    // Immediate outcomes return directly. Only a newly queued job delivers a
    // terminal callback, while attached. Duplicate Pending requests add no callback.
    [[nodiscard]] EditorRegistrationResult ApplyEditorRegistrationCommand(
        const EditorProcessingCommands&, const EditorRegistrationCommand&,
        std::function<void(EditorRegistrationResult)> onComplete = {});
    [[nodiscard]] RuntimeEngineConfigApplyResult ApplyEditorRegistrationConfig(
        const EditorProcessingCommands&, const RegistrationConfig&, std::string sourceId = {});
    [[nodiscard]] std::optional<RegistrationConfig> GetEditorRegistrationConfig(
        const EditorProcessingCommands&);
    [[nodiscard]] EditorRegistrationResult ApplyEditorConfiguredRegistrationCommand(
        const EditorProcessingCommands&, std::function<void(EditorRegistrationResult)> onComplete = {});

    // --- Coherent Point Drift (RUNTIME-273) -------------------------------------------
    // A run captures both point sets in world space, iterates on a worker (all at once or
    // step by step for inspection), streams a per-iteration trace and the moving source
    // positions, and publishes only on Apply, as one undoable history entry after
    // revalidating the inputs and entity transforms.
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
        std::uint64_t KernelEvaluations{0u};
        double Seconds{0.0};               // UI-067: since the run's first step began
        double IterationSeconds{0.0};      // this iteration's wall time
    };

    enum class EditorCoherentPointDriftPhase : std::uint8_t
    {
        Ready,     // captured, no iteration yet
        Running,   // a step job is in flight
        Paused,    // stepped, not finished; more steps or Apply
        Finished,  // converged, sigma floor or iteration cap; Apply publishes
        Failed,
        Cancelled,
        Applied,
    };
    [[nodiscard]] const char* ToString(EditorCoherentPointDriftPhase phase) noexcept;

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
        std::uint32_t KernelRank{0u}; // low-rank nonrigid eigenpairs
        double KernelApproximationError{0.0};
        std::string Message{};

        [[nodiscard]] bool Succeeded() const noexcept { return Status == EditorCommandStatus::Applied; }
    };

    struct EditorCoherentPointDriftSnapshot
    {
        EditorCoherentPointDriftPhase Phase{EditorCoherentPointDriftPhase::Ready};
        EditorCoherentPointDriftResult Result{};
        std::vector<EditorCoherentPointDriftTrace> Trace{};
        // Shared and immutable, so a per-frame snapshot copies no points.
        std::shared_ptr<const std::vector<glm::vec3>> SourcePreview{}; // current T(y), world space, capture order
        std::shared_ptr<const std::vector<glm::vec3>> Target{};        // fixed points, world space
        std::uint64_t Revision{0u};             // increments with every update (not with Stage)
        // UI-067: the phase a Running step is in ("preparing", "building_kernel",
        // "expectation_step", ...) and when it began, for a live progress line.
        std::string Stage{};
        std::chrono::steady_clock::time_point StageStarted{};
    };

    struct EditorCoherentPointDriftRun;
    using EditorCoherentPointDriftRunHandle = std::shared_ptr<EditorCoherentPointDriftRun>;

    [[nodiscard]] ActionReadiness PreviewEditorCoherentPointDriftCommand(
        const EditorProcessingCommands&, const CoherentPointDriftConfig&);
    // Validates the config and captures both point sets; nothing iterates yet. On failure
    // returns null and fills `failure`.
    [[nodiscard]] EditorCoherentPointDriftRunHandle StartEditorCoherentPointDrift(
        const EditorProcessingCommands&, const CoherentPointDriftConfig&, EditorCoherentPointDriftResult& failure);
    // Queues `iterations` EM iterations (0 = until the run ends) as one background job.
    // Pending on success; rejected while a step is running or after the run ended.
    [[nodiscard]] EditorCommandStatus StepEditorCoherentPointDrift(
        const EditorProcessingCommands&, const EditorCoherentPointDriftRunHandle&, std::uint32_t iterations);
    // Stops a running step after its current iteration; the run becomes Cancelled.
    void CancelEditorCoherentPointDrift(const EditorCoherentPointDriftRunHandle&);
    [[nodiscard]] EditorCoherentPointDriftSnapshot SnapshotEditorCoherentPointDrift(const EditorCoherentPointDriftRunHandle&);
    // Publishes the current estimate (Paused or Finished runs).
    [[nodiscard]] EditorCoherentPointDriftResult ApplyEditorCoherentPointDrift(
        const EditorProcessingCommands&, const EditorCoherentPointDriftRunHandle&);
    // Start, run to the end and publish; onComplete receives the terminal result of a
    // queued run (immediate outcomes return directly).
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
