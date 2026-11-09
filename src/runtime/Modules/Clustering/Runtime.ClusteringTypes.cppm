// Clustering parameters, property bindings and completion records and borrowed service access, independent of lifecycle setup.
module;
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <glm/vec3.hpp>
#include <entt/entity/fwd.hpp>
export module Extrinsic.Runtime.ClusteringTypes;
import Extrinsic.Core.Error;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.CommandBus;
export import Extrinsic.Runtime.GeometryProperty.Types;
export import Extrinsic.Runtime.StepRunExecution;
import Extrinsic.Runtime.WorldHandle;
export namespace Extrinsic::Runtime
{
    enum class ClusteringBackend : std::uint8_t
    {
        None,
        CpuReference,
        VulkanCompute,
    };

    [[nodiscard]] std::string_view ToString(
        ClusteringBackend backend) noexcept;

    enum class KMeansInitialization : std::uint8_t
    {
        Random,
        Hierarchical,
    };

    // The Lloyd iteration cap of the config and panel (a request admits any positive cap).
    inline constexpr std::uint32_t kMaxKMeansIterations = 4096u;

    struct KMeansParameters
    {
        std::uint32_t ClusterCount{8u};
        std::uint32_t Seed{42u};
        KMeansInitialization Initialization{
            KMeansInitialization::Hierarchical};
        // ARCH-023: the shared execution options. One step is one Lloyd iteration (assign, then update) on both
        // backends, each a step run with the same start, cap, Accept and restart; the CPU reference shows no live
        // preview (its labels appear at Accept), the Vulkan run previews at the cadence.
        StepRunExecution Execution{.IterationCap = 32u,
            .PreviewEvery = 5u,
            .PreviewIntervalMs = 0u,
            .AutoAccept = StepRunAutoAccept::Never,
            .Start = StepRunStart::Run};
    };

    struct KMeansPropertyRefs
    {
        GeometryPropertyRef InputPositions{
            .Domain = GeometryElementDomain::PointCloudPoint,
            .Name = "v:position",
            .ValueKind = Geometry::PropertyValueKind::Vec3,
        };
        GeometryPropertyRef OutputLabels{
            .Domain = GeometryElementDomain::PointCloudPoint,
            .Name = "p:kmeans_label",
            .ValueKind = Geometry::PropertyValueKind::UInt32,
        };
        GeometryPropertyRef OutputColors{
            .Domain = GeometryElementDomain::PointCloudPoint,
            .Name = "p:kmeans_color",
            .ValueKind = Geometry::PropertyValueKind::Vec4,
        };
        std::optional<GeometryPropertyRef> OutputScalarLabels{};
    };

    [[nodiscard]] KMeansPropertyRefs MakeKMeansPropertyRefs(
        GeometryElementDomain domain);

    [[nodiscard]] bool IsValidKMeansPropertyBindings(const KMeansPropertyRefs&) noexcept;

    enum class KMeansRunStatus : std::uint8_t
    {
        Queued,
        Applied,
        MissingScene,
        InvalidProcessingParameters,
        StaleEntity,
        UnsupportedGeometryDomain,
        GeometryProcessingFailed,
        Cancelled,
        StaleSource,
        StaleWorld,
        ModuleUnavailable,
    };

    [[nodiscard]] std::string_view ToString(
        KMeansRunStatus status) noexcept;

    enum class KMeansGpuAction : std::uint8_t { Observe, Stop, Accept, Discard };
    // A resident GPU run, also a step run (`StepRun`: its id for run_control and run_observe). ReadyToAccept: ended,
    // or paused at an iteration boundary (Accept then prepares the labels of that iteration's centroids).
    struct KMeansGpuObservation
    {
        bool Running{}, ReadyToAccept{}, Accepting{}, CanAccept{};
        std::string Message{};
        std::uint64_t StepRun{};
        std::uint32_t Iterations{}, Submissions{}, Previews{};
        std::uint64_t InputUploadBytes{}, InputCacheHits{}, CpuStageUploadBytes{}, CpuStageReadbackBytes{};
    };
    struct RunKMeans
    {
        std::uint32_t StableEntityId{0u};
        KMeansPropertyRefs Properties{};
        KMeansParameters Parameters{};
        ClusteringBackend Backend{ClusteringBackend::CpuReference};
        // A preview's newest parameters (a slider): a GPU request that replaces the GPU run on the same output instead
        // of being refused while it runs. That run is discarded (while it is being accepted it finishes first) and
        // this request starts once it settled, with fresh captures; a newer replacing request that arrives first
        // supersedes it, and it then completes Cancelled.
        bool ReplaceRunning{false};
        std::function<bool()> AttachmentActive{};
        // History label prefix active when the run was queued (an agent call's "Agent: "); the
        // commit on a later frame applies it again.
        std::string LabelPrefix{};
    };

    struct KMeansRunCompleted
    {
        CommandCorrelationId Correlation{};
        WorldHandle World{};
        KMeansRunStatus Status{KMeansRunStatus::Queued};
        std::uint32_t StableEntityId{0u};
        KMeansPropertyRefs Properties{};
        KMeansParameters Parameters{};
        std::uint32_t LabelCount{0u};
        std::uint32_t ClusterCount{0u};
        std::uint32_t Iterations{0u};
        bool Converged{false};
        float Inertia{0.0f};
        std::uint32_t MaxDistanceIndex{0u};
        ClusteringBackend RequestedBackend{ClusteringBackend::CpuReference};
        ClusteringBackend ActualBackend{ClusteringBackend::None};
        bool FellBackToCpu{false};
        std::string BackendDiagnostic{};
        std::string ImplementationId{};
        std::uint64_t GpuInputUploadBytes{}, GpuInputCacheHits{}, CpuStageUploadBytes{}, CpuStageReadbackBytes{};
        std::uint32_t GpuSubmissions{}, GpuPreviews{};
        // Workspace buffers/pipelines created at admission; a retained idle workspace reports zero.
        std::uint32_t GpuWorkspaceBuffersCreated{}, GpuWorkspacePipelinesCreated{};
        std::uint64_t StepRun{}; // the run's step run (run_control, run_observe); 0: it never started
        std::vector<glm::vec3> Centroids{};
        Core::ErrorCode Error{Core::ErrorCode::Success};
        std::string Message{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == KMeansRunStatus::Applied;
        }
    };

    // Metadata-only admission: nullopt admits; a value describes rejection.
    // Execution still checks finite values and captures exact publication state.
    // Dispatch fills world/correlation; an admission rejection has neither.
    [[nodiscard]] std::optional<KMeansRunCompleted> ValidateKMeansRequest(
        const entt::registry*, const Runtime::RunKMeans&);

    struct ClusterLabelsChanged
    {
        CommandCorrelationId Correlation{};
        WorldHandle World{};
        GeometryPropertyRef Labels{};
        GeometryPropertyRef Colors{};
        std::uint32_t StableEntityId{0u};
        std::uint32_t LabelCount{0u};
    };

    struct ClusteringModuleStats
    {
        std::uint64_t CommandsHandled{0};
        std::uint64_t JobsSubmitted{0};
        std::uint64_t JobSubmissionFailures{0};
        std::uint64_t GpuRequestsAccepted{0};
        std::uint64_t GpuFallbacks{0};
        std::uint64_t GpuCompletions{0};
        std::uint64_t CompletionEvents{0};
        std::uint64_t LabelsCommitted{0};
        std::uint64_t CommitsDropped{0};
        std::uint64_t ClusterLabelsChangedEvents{0};
        std::uint64_t VisualizationRefreshReactions{0};
        std::uint64_t CaptureReuses{0}; // requests that reused the previous request's unchanged point capture
        std::uint64_t CachedCapturePoints{0}; // points of the capture kept for the next request (0: none)
        // ARCH-022 R7, GPU runs, cumulative on the main thread. The microseconds are nested in the engine's
        // frame-pacing intervals (they explain them, never add to them): a GPU request's capture and start (in the
        // command drain, or in the advance for a held request), the GPU participant's drain (advance: pages,
        // previews, completions, a held request's start), preview publications (inside the advance) and the Accept's
        // publication (inside the job completion drain). The counts gate a stall measurement on progress.
        std::uint64_t GpuCaptureInitMicros{0};
        std::uint64_t GpuAdvanceMicros{0};
        std::uint64_t GpuPreviewPublishMicros{0};
        std::uint64_t GpuAcceptPublishMicros{0};
        std::uint64_t GpuReplacementsHeld{0};
        std::uint64_t GpuReplacementsSuperseded{0};
        std::uint64_t GpuReplacementsStarted{0};
        std::uint64_t GpuIterations{0};
        std::uint64_t GpuPreviews{0};
        // ARCH-022 Q83: requests enqueued through the service (their frame is the submit's), the parts of a GPU
        // request's capture and start (nested in GpuCaptureInitMicros; the rest is validation, the publication's
        // admission and the step run), and the GPU pages submitted.
        std::uint64_t RequestsSubmitted{0};
        std::uint64_t LastRequestSteadyMicros{0}; // steady_clock at the latest request's enqueue
        std::uint64_t GpuSnapshotMicros{0};
        std::uint64_t GpuInputResolveMicros{0};
        std::uint64_t GpuSeedingMicros{0}; // main-thread seeding of a GPU start (Random; Hierarchical without a worker)
        std::uint64_t GpuSeedingJobs{0};   // Hierarchical seedings of GPU requests on a worker (ARCH-022 Q90)
        std::uint64_t GpuWorkspacePrepareMicros{0};
        std::uint64_t GpuSubmissions{0};
    };

    // Only the lifecycle owner may bind the service. C++ linkage lets its
    // definition stay in the lifecycle module without importing that module here.
    extern "C++"
    {
        class ClusteringModule;
        struct EditorProcessingContext;
        class EditorWorkspaceAttachment;
        class EditorWorkspaceSlot;
    }

    class ClusteringService
    {
    public:
        ClusteringService() = default;
        ClusteringService(const ClusteringService&) = delete;
        ClusteringService& operator=(const ClusteringService&) = delete;

        [[nodiscard]] bool Available() const noexcept;
        [[nodiscard]] CommandCorrelationId RunKMeans(RunKMeans command);

        [[nodiscard]] KernelEventSubscription SubscribeRunCompleted(
            std::function<void(const KMeansRunCompleted&)> listener);
        [[nodiscard]] KernelEventSubscription SubscribeClusterLabelsChanged(
            std::function<void(const ClusterLabelsChanged&)> listener);
        void Unsubscribe(KernelEventSubscription subscription);

        [[nodiscard]] ClusteringModuleStats Stats() const noexcept;
        [[nodiscard]] KMeansGpuObservation GpuRun(CommandCorrelationId, KMeansGpuAction = KMeansGpuAction::Observe);
        // The step run of the request's run, CPU or Vulkan (run_control, run_observe, the panel's controls); 0 before
        // the request started a run, after it was refused, or once newer runs replaced it in the module's short list.
        [[nodiscard]] std::uint64_t StepRun(CommandCorrelationId) const;
        // Read-only admission reason shared with GPU submission; empty preserves CPU fallback paths.
        [[nodiscard]] std::string GpuReadinessDiagnostic(const Runtime::RunKMeans&, const EditorProcessingContext&) const;
        // The app's editor workspace (the Sandbox shell's) while it lends one: a GPU run and its Accept are rows of
        // its Jobs window. Release before the workspace detaches; the module's own workspace is used otherwise.
        void UseEditorWorkspace(EditorWorkspaceAttachment& workspace) noexcept;
        void ReleaseEditorWorkspace(const EditorWorkspaceAttachment& workspace) noexcept;

    private:
        friend class ClusteringModule;

        void Bind(CommandBus* commands,
                  KernelEventBus* events,
                  const ClusteringModuleStats* stats) noexcept;

        std::function<KMeansGpuObservation(CommandCorrelationId, KMeansGpuAction)> m_GpuRun{};
        std::function<std::uint64_t(CommandCorrelationId)> m_StepRun{};
        std::function<std::string(const Runtime::RunKMeans&, const EditorProcessingContext&)> m_GpuReadinessDiagnostic{};
        // The module's slot of the workspace its GPU runs queue their editor jobs in (the lent one, else its own).
        EditorWorkspaceSlot* m_Workspace{};
        CommandBus* m_Commands{};
        KernelEventBus* m_Events{};
        const ClusteringModuleStats* m_Stats{};
        std::uint64_t m_RequestsSubmitted{0};
        std::uint64_t m_LastRequestSteadyMicros{0};
    };

}
