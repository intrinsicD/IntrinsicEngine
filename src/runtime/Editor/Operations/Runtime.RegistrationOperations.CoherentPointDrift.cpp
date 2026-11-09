// Coherent Point Drift editor operation (RUNTIME-273) as a step run (ARCH-023). A run owns world-space copies of both
// point sets and a Geometry::CoherentPointDrift::Solver; one step is one EM iteration, and every run steps on a worker
// (StepRuns segments, as the CPD plugin does): its Step calls the solver there. A Vulkan E-step is evaluated on that
// worker (its own bounded page per iteration), and Vulkan subsample orders (RUNTIME-289) are computed by the run's
// first step on that worker before the solver samples (RUNTIME-355 F3), with no frame or main-thread pump. Panels and
// agents read a mutex-guarded snapshot (trace, result, the shown preview). Nothing is written to the scene until the
// run's one Accept, which revalidates the captured inputs and both entity transforms and publishes one undoable
// history entry.
module;
#include <chrono>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <coroutine>
#include <limits>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

module Extrinsic.Runtime.RegistrationOperations;

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Error;
import Extrinsic.Core.Tasks;
import Extrinsic.Core.Tasks.CounterEvent;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Component.Hierarchy;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.CoherentPointDriftGpuEStep;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.GpuPropertyBinding;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.PointSamplingGpu;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.StepRuns;
import Extrinsic.RHI.Device;
import Extrinsic.Runtime.WorldHandle;
import Geometry.HalfedgeMesh;
import Geometry.HalfedgeMesh.Vertices.Normals;
import Geometry.PointSampling;
import Geometry.Properties;
import Geometry.Registration.CoherentPointDrift;

#include "Editor/internal/Runtime.EditorGeometryHelpers.hpp"
#include "Editor/internal/Runtime.EditorTransformHelpers.hpp"
#include "Editor/internal/Runtime.EditorProcessingAccess.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.JobFailure.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.PointFields.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.MeshSources.hpp"

namespace Extrinsic::Runtime
{
    namespace CPD = Geometry::CoherentPointDrift;
    namespace ECSC = ECS::Components;
    namespace GPD = GeometryProcessingDetail;

    static_assert(int(CPD::EStep::NystromLandmarkSource::UnionRandom) == int(CoherentPointDriftNystromSource::UnionRandom) &&
                  int(CPD::EStep::NystromLandmarkSchedule::Resampled) == int(CoherentPointDriftNystromSchedule::Resampled) &&
                  int(CPD::EStep::NystromBoxPattern::Halton) == int(CoherentPointDriftNystromBoxPattern::Halton));

    struct EditorCoherentPointDriftRun
    {
        // Written once on the main thread before the run's first iteration (the device subsample orders in Params:
        // by the first step, see SubsampleOrdering).
        CoherentPointDriftConfig Config{};
        EditorProcessingContext Context{};
        entt::entity SourceEntity{entt::null}, TargetEntity{entt::null};
        GPD::PointInputCapture Source{}, Target{};
        ECSC::Transform::Component SourceBefore{}, TargetBefore{};
        bool SourceHadTransform{false}, TargetHadTransform{false};
        glm::dmat4 SourceModel{1.0}, TargetModel{1.0};
        std::vector<glm::vec3> SourceWorld{}, TargetWorld{};
        CPD::Params Params{};
        // Vulkan E-step (METHOD-056): the worker-to-main-thread broker, held by the run from its first iteration to
        // its end (its workspace lease keeps the uploaded target across steps, pauses and restarts), or why there is
        // none.
        std::shared_ptr<CoherentPointDriftGpuEStep> GpuEStep{};
        std::string GpuUnavailable{};
        // Bayesian subsamples (RUNTIME-289), per set: the backend that ordered it (empty: not subsampled), why the CPU
        // did, and whether a device order is still to be computed (Pending). ARCH-023 (RUNTIME-355 F3): the start (main
        // thread) prepares the device order (Device: its workspace and resident input); the run's first step computes it
        // on its worker before the solver samples, and from then on that step owns Pending, Backend, Diagnostic and
        // Params' order. Discard aborts a waiting page; the settled end releases Device once no step runs.
        struct SubsampleOrdering
        {
            std::string Backend{}, Diagnostic{};
            bool Pending{false};
            std::shared_ptr<PointSamplingGpuRun> Device{};
        };
        SubsampleOrdering TargetOrdering{}, SourceOrdering{};

        // Iteration state: the run's one worker step changes it while it runs; the main thread reads or resets it
        // (Commit, Restart, the settled end) only while none runs.
        CPD::Solver Solver{};
        bool Initialized{false};
        std::chrono::steady_clock::time_point Started{}, LastIteration{};
        StepRunMetric LastMetric{};
        StepOutcome LastOutcome{StepOutcome::Continue};
        std::atomic<bool> CancelRequested{false}; // the run ended: the solver and a pending E-step stop at once

        // Main thread.
        std::uint64_t Run{0u};
        bool Ended{false}; // committed, or its Discard ran
        std::function<void(EditorCoherentPointDriftResult)> Sink{};
        bool Delivered{false}; // the one terminal result: Commit, else the settled end (Finished)

        mutable std::mutex Mutex{};
        EditorCoherentPointDriftSnapshot Snapshot{};
    };

    namespace
    {
        using EditorFeatureDetail::ResolveStableEntity;
        using EditorFeatureDetail::SameTransformComponent;
        using EditorFeatureDetail::ExecuteEditorTransformMutation;
        using EditorFeatureDetail::ToEditorCommandStatus;

        CPD::Variant ToVariant(CoherentPointDriftMethod method) noexcept
        {
            switch (method)
            {
            case CoherentPointDriftMethod::Rigid: return CPD::Variant::Rigid;
            case CoherentPointDriftMethod::Affine: return CPD::Variant::Affine;
            case CoherentPointDriftMethod::Nonrigid: return CPD::Variant::Nonrigid;
            case CoherentPointDriftMethod::Bayesian: return CPD::Variant::Bayesian;
            }
            return CPD::Variant::Rigid;
        }

        GeometryPropertyRef ResolveDomain(const GeometryEntityAvailability& available, GeometryPropertyRef ref)
        {
            if (ref.Domain == GeometryElementDomain::Unknown)
                ref.Domain = GPD::PrimaryPointDomain(available);
            return ref;
        }

        EditorCoherentPointDriftResult Failure(const CoherentPointDriftConfig& config, EditorCommandStatus status,
                                               std::string message)
        {
            return {.Status = status, .Method = config.Method, .Output = config.Output, .Message = std::move(message)};
        }

        // The config enum mirrors the geometry policy numerically.
        static_assert(std::uint8_t(CoherentPointDriftEStep::Reference) == std::uint8_t(CPD::EStepPolicy::Reference) &&
                      std::uint8_t(CoherentPointDriftEStep::Dense) == std::uint8_t(CPD::EStepPolicy::Dense) &&
                      std::uint8_t(CoherentPointDriftEStep::Truncated) == std::uint8_t(CPD::EStepPolicy::Truncated) &&
                      std::uint8_t(CoherentPointDriftEStep::Auto) == std::uint8_t(CPD::EStepPolicy::Auto) &&
                      std::uint8_t(CoherentPointDriftEStep::FastGauss) == std::uint8_t(CPD::EStepPolicy::FastGauss) &&
                      std::uint8_t(CoherentPointDriftEStep::Nystrom) == std::uint8_t(CPD::EStepPolicy::Nystrom) &&
                      std::uint8_t(CoherentPointDriftEStep::Vulkan) == std::uint8_t(CPD::EStepPolicy::Vulkan));

        std::size_t MinimumPoints(CoherentPointDriftMethod method) noexcept
        {
            return method == CoherentPointDriftMethod::Affine ? 4u : 3u;
        }

        // Shared by readiness (no value copy) and Start (values copied, world space).
        // Why a Vulkan E-step cannot run here (empty: it can): the run's worker submits and waits for its pages, which
        // needs the job lane and an operational device with shader float64.
        std::string VulkanEStepUnavailable(const EditorProcessingContext& context)
        {
            if (!context.JobCommands.Available()) return "No job lane";
            if (context.SpatialIndices == nullptr || context.Device == nullptr || !context.Device->IsOperational())
                return "No operational Vulkan device";
            if (!context.Device->SupportsShaderFloat64()) return "The Vulkan device lacks shader float64";
            return {};
        }

        // Copies the subsample orderings into the run's result, so every terminal result (failed,
        // cancelled before a step, applied) carries them; called whenever an ordering changes.
        void ReportSubsampling(EditorCoherentPointDriftRun& run)
        {
            std::scoped_lock lock{run.Mutex};
            auto& result = run.Snapshot.Result;
            result.SubsampleRequestedBackend = std::string(run.Config.SubsampleBackend == PointSamplingBackend::Vulkan
                                                               ? kPointSamplingGpuBackendId : kPointSamplingCpuBackendId);
            result.SubsampleTargetBackend = run.TargetOrdering.Backend;
            result.SubsampleSourceBackend = run.SourceOrdering.Backend;
            result.SubsampleBackendDiagnostic.clear();
            for (const auto& [name, set] : {std::pair{"Target: ", &run.TargetOrdering}, std::pair{"Source: ", &run.SourceOrdering}})
                if (!set->Diagnostic.empty())
                    result.SubsampleBackendDiagnostic += (result.SubsampleBackendDiagnostic.empty() ? "" : " ") + (name + set->Diagnostic);
        }

        std::optional<EditorCoherentPointDriftResult> Capture(const EditorProcessingContext& context,
                                                              CoherentPointDriftConfig& config, bool copyValues,
                                                              EditorCoherentPointDriftRun* run)
        {
            const auto validation = ValidateCoherentPointDriftConfigSection(
                SerializeCoherentPointDriftConfig(config), {}, kCoherentPointDriftConfigSectionName);
            if (!validation.Usable())
                return Failure(config, EditorCommandStatus::InvalidProcessingParameters, validation.Diagnostics.front().Message);
            if (context.Scene == nullptr) return Failure(config, EditorCommandStatus::MissingScene, "Scene is unavailable.");
            entt::registry& raw = context.Scene->Raw();
            const auto source = ResolveStableEntity(raw, config.SourceStableEntityId);
            const auto target = ResolveStableEntity(raw, config.TargetStableEntityId);
            if (!source || !target)
                return Failure(config, EditorCommandStatus::StaleEntity, "Choose existing source and target entities.");
            if (*source == *target)
                return Failure(config, EditorCommandStatus::InvalidProcessingParameters, "Source and target must be different entities.");
            // Point sets are mapped with their local Transform only, so parented entities (whose
            // world matrix includes the parents) are refused rather than registered in the wrong frame.
            const auto parented = [&](entt::entity entity) {
                const auto* hierarchy = raw.try_get<ECSC::Hierarchy::Component>(entity);
                return hierarchy != nullptr && raw.valid(hierarchy->Parent);
            };
            if (parented(*source) || parented(*target))
                return Failure(config, EditorCommandStatus::InvalidProcessingParameters,
                               "Coherent Point Drift needs unparented source and target entities.");
            const auto sourceAvailable = BuildGeometryAvailability(raw, *source);
            const auto targetAvailable = BuildGeometryAvailability(raw, *target);
            config.SourcePositions = ResolveDomain(sourceAvailable, config.SourcePositions);
            config.TargetPositions = ResolveDomain(targetAvailable, config.TargetPositions);
            GPD::PointInputCapture sourceCapture, targetCapture;
            std::string diagnostic;
            if (!GPD::CapturePointInput(sourceAvailable, config.SourcePositions, copyValues, sourceCapture, diagnostic))
                return Failure(config, EditorCommandStatus::InvalidProcessingParameters, "Source: " + diagnostic);
            if (!GPD::CapturePointInput(targetAvailable, config.TargetPositions, copyValues, targetCapture, diagnostic))
                return Failure(config, EditorCommandStatus::InvalidProcessingParameters, "Target: " + diagnostic);
            const std::size_t minimum = MinimumPoints(config.Method);
            if (sourceCapture.LiveCount < minimum || targetCapture.LiveCount < minimum)
                return Failure(config, EditorCommandStatus::InvalidProcessingParameters,
                               "Coherent Point Drift needs at least " + std::to_string(minimum) +
                                   " live points on each side.");
            // Bayesian runs may register a subsample; the kernel then covers only the samples.
            const bool deforming = config.Method == CoherentPointDriftMethod::Nonrigid ||
                                   config.Method == CoherentPointDriftMethod::Bayesian;
            const std::size_t kernelPoints = config.Method == CoherentPointDriftMethod::Bayesian && config.Subsample > 0u
                ? std::min<std::size_t>(config.Subsample, sourceCapture.LiveCount) : sourceCapture.LiveCount;
            // The full kernel costs O(m^3) per iteration; above a thousand points it takes seconds
            // per iteration, so unless the exact kernel is asked for a low rank is used instead.
            if (deforming && config.LowRank == 0u && config.AutoLowRank && kernelPoints > kCoherentPointDriftAutoLowRankPoints)
                config.LowRank = kCoherentPointDriftAutoLowRank;
            const std::size_t nonrigidLimit = config.LowRank > 0u ? CPD::kMaxLowRankSourcePoints : CPD::kMaxNonrigidSourcePoints;
            if (deforming && (kernelPoints > nonrigidLimit || sourceCapture.LiveCount > CPD::kMaxLowRankSourcePoints))
                return Failure(config, EditorCommandStatus::InvalidProcessingParameters,
                               config.LowRank > 0u
                                   ? "Low-rank nonrigid CPD handles at most " + std::to_string(nonrigidLimit) + " source points."
                                   : "Nonrigid and Bayesian CPD with the full kernel handle at most " + std::to_string(nonrigidLimit) +
                                         " source points; set a low rank (for example 100), turn on the automatic rank or subsample the source.");
            const auto* sourceTransform = raw.try_get<ECSC::Transform::Component>(*source);
            if (config.Output == CoherentPointDriftOutput::SourceTransform && sourceTransform == nullptr)
                return Failure(config, EditorCommandStatus::MissingTransform, "The source entity has no Transform to drive.");
            if (config.Output == CoherentPointDriftOutput::DisplacementProperty)
            {
                // Config validation saw an unresolved domain; check the resolved one again.
                if (IsTopologyProperty(config.SourcePositions.Domain, config.DisplacementName))
                    return Failure(config, EditorCommandStatus::InvalidProcessingParameters,
                                   "The displacement property cannot replace topology or deletion data.");
                const auto* props = ResolveGeometryPropertySet(sourceAvailable, config.SourcePositions.Domain);
                const GeometryPropertyRef output{config.SourcePositions.Domain, config.DisplacementName,
                                                 Geometry::PropertyValueKind::Vec3};
                if (props != nullptr && props->Exists(config.DisplacementName) &&
                    !ResolveGeometryProperty(sourceAvailable, output, props->Size(), false).Resolved())
                    return Failure(config, EditorCommandStatus::InvalidProcessingParameters,
                                   "The displacement property exists with another type or size.");
            }
            if (run == nullptr) return std::nullopt;

            run->Config = config;
            run->SourceEntity = *source;
            run->TargetEntity = *target;
            run->SourceHadTransform = sourceTransform != nullptr;
            if (sourceTransform) run->SourceBefore = *sourceTransform;
            const auto* targetTransform = raw.try_get<ECSC::Transform::Component>(*target);
            run->TargetHadTransform = targetTransform != nullptr;
            if (targetTransform) run->TargetBefore = *targetTransform;
            run->SourceModel = sourceTransform ? ECSC::Transform::GetMatrixDouble(*sourceTransform) : glm::dmat4(1.0);
            run->TargetModel = targetTransform ? ECSC::Transform::GetMatrixDouble(*targetTransform) : glm::dmat4(1.0);
            for (const auto& p : sourceCapture.Points) run->SourceWorld.push_back(glm::vec3(run->SourceModel * glm::dvec4(glm::dvec3(p), 1.0)));
            for (const auto& p : targetCapture.Points) run->TargetWorld.push_back(glm::vec3(run->TargetModel * glm::dvec4(glm::dvec3(p), 1.0)));
            run->Source = std::move(sourceCapture);
            run->Target = std::move(targetCapture);
            run->Params = CPD::Params{.Method = ToVariant(config.Method),
                .OutlierWeight = config.OutlierWeight,
                .MaxIterations = config.Execution.IterationCap,
                .Tolerance = config.Tolerance,
                .InitialSigma2 = config.InitialSigma2,
                .Sigma2Floor = config.Sigma2Floor,
                .NormalizeInputs = config.NormalizeInputs,
                .EstimateScale = config.EstimateScale,
                .AllowReflection = config.AllowReflection,
                .Beta = config.Beta,
                .Lambda = config.Lambda,
                .EStep = CPD::EStepPolicy(config.EStep),
                .EStepTolerance = config.EStepTolerance,
                .Threads = config.Threads,
                .NystromLandmarks = config.NystromLandmarks,
                .NystromErrorLimit = config.NystromErrorLimit,
                .NystromLocalSigma = config.NystromLocalSigma,
                .NystromSource = CPD::EStep::NystromLandmarkSource(config.NystromSource),
                .NystromSchedule = CPD::EStep::NystromLandmarkSchedule(config.NystromSchedule),
                .NystromPattern = CPD::EStep::NystromBoxPattern(config.NystromBoxPattern),
                .LowRank = config.LowRank,
                .Gamma = config.Gamma,
                .Kappa = config.Kappa > 0.0 ? config.Kappa : std::numeric_limits<double>::infinity(),
                .SubsampleSource = config.Subsample,
                .SubsampleTarget = config.SubsampleTarget,
                .SubsampleSampling = ToPointSamplingParams(config.SubsampleSampling),
                .LandmarkSampling = ToPointSamplingParams(config.LandmarkSampling)};
            // UI-067: cancellation polled inside kernel builds and E-step chunks, and the phase in
            // flight for the panel (the run owns its solver, so the raw pointer outlives both).
            run->Params.Cancelled = [flag = &run->CancelRequested] { return flag->load(std::memory_order_relaxed); };
            run->Params.StageObserver = [raw = run](const CPD::Stage stage) {
                std::scoped_lock lock{raw->Mutex};
                raw->Snapshot.Stage = std::string(CPD::ToString(stage));
                raw->Snapshot.StageStarted = std::chrono::steady_clock::now();
            };
            // Subsampled sets as the solver decides them; a device order is prepared at the start and computed by the
            // first step.
            const auto plan = [&](EditorCoherentPointDriftRun::SubsampleOrdering& set, const std::uint32_t count,
                                  const std::size_t points) {
                if (config.Method != CoherentPointDriftMethod::Bayesian || count == 0u || count >= points) return;
                set.Backend = std::string(kPointSamplingCpuBackendId);
                if (config.SubsampleBackend != PointSamplingBackend::Vulkan) return;
                set.Diagnostic = !context.JobCommands.Available() || context.SpatialIndices == nullptr
                    ? std::string("No job lane or spatial compute service; sampling ran on the CPU.")
                    : PointSamplingGpuUnsupportedReason(run->Params.SubsampleSampling, points, count, context.Device);
                set.Pending = set.Diagnostic.empty();
            };
            plan(run->TargetOrdering, config.SubsampleTarget, run->TargetWorld.size());
            plan(run->SourceOrdering, config.Subsample, run->SourceWorld.size());
            if (config.EStep == CoherentPointDriftEStep::Vulkan)
            {
                if (const auto reason = VulkanEStepUnavailable(context); !reason.empty())
                    run->GpuUnavailable = reason + "; the E-step ran on the CPU.";
                else
                {
                    // ARCH-023: the run's Init leases and prepares the device workspace for its full sets (a
                    // subsample is smaller), so every E-step is evaluated on the step's own worker.
                    run->GpuEStep =
                        std::make_shared<CoherentPointDriftGpuEStep>(*context.SpatialIndices, *context.Device);
                    if (!run->GpuEStep->Prepare(run->TargetWorld.size(), run->SourceWorld.size()))
                    {
                        run->GpuUnavailable = run->GpuEStep->Diagnostic();
                        run->GpuEStep.reset();
                    }
                }
            }
            return std::nullopt;
        }

        // Result metadata is independent of preview delivery, including an inline or preview-disabled run.
        void UpdateEstimate(EditorCoherentPointDriftRun& run, const CPD::Result& current)
        {
            double displacement = 0.0;
            for (std::size_t i = 0; i < current.TransformedSource.size(); ++i)
                displacement += glm::length(current.TransformedSource[i] - glm::dvec3(run.SourceWorld[i]));
            std::scoped_lock lock{run.Mutex};
            auto& snapshot = run.Snapshot;
            auto& result = snapshot.Result;
            result.Method = run.Config.Method;
            result.Output = run.Config.Output;
            result.SourcePointCount = run.SourceWorld.size();
            result.TargetPointCount = run.TargetWorld.size();
            result.MeanDisplacement =
                current.TransformedSource.empty() ? 0.0 : displacement / double(current.TransformedSource.size());
            result.Iterations = current.Iterations;
            result.Sigma2 = current.Sigma2;
            result.NegativeLogLikelihood = current.NegativeLogLikelihood;
            result.MatchedWeight = current.MatchedWeight;
            result.Transform = current.Transform;
            result.Termination = std::string(CPD::ToString(current.Stop));
            result.Backend = std::string(current.Backend);
            result.EStepErrorBound = current.EStepErrorBound;
            result.EStepSampledError = current.EStepSampledError;
            result.EStepFallbacks = current.EStepFallbacks;
            result.EStepDeviceIterations = current.EStepDeviceIterations;
            result.EStepDeviceCpuRows = current.EStepDeviceCpuRows;
            result.GpuDiagnostic = run.GpuEStep ? run.GpuEStep->Diagnostic() : run.GpuUnavailable;
            result.KernelRank = current.KernelRank;
            result.KernelApproximationError = current.KernelApproximationError;
            // The subsamples are fixed once the solver has chosen them.
            if (!snapshot.SourceSamples && !current.SourceSamples.empty())
                snapshot.SourceSamples = std::make_shared<const std::vector<std::uint32_t>>(current.SourceSamples);
            if (!snapshot.TargetSamples && !current.TargetSamples.empty())
                snapshot.TargetSamples = std::make_shared<const std::vector<std::uint32_t>>(current.TargetSamples);
            ++snapshot.Revision;
        }

        // Called between iterations, on the worker or owner, never beside a solver update.
        void RefreshEstimate(EditorCoherentPointDriftRun& run, std::vector<float>* values)
        {
            const CPD::Result current = run.Solver.Current();
            UpdateEstimate(run, current);
            if (values != nullptr)
            {
                values->clear();
                values->reserve(3u * current.TransformedSource.size());
                for (const auto& p : current.TransformedSource)
                    values->insert(values->end(), {float(p.x), float(p.y), float(p.z)});
            }
        }

        // A preview copy shown: the moved source for the overlay.
        void ShowEstimate(EditorCoherentPointDriftRun& run, const std::vector<float>& values)
        {
            auto preview = std::make_shared<std::vector<glm::vec3>>(values.size() / 3u);
            for (std::size_t i = 0; i < preview->size(); ++i)
            {
                (*preview)[i] = {values[3 * i], values[3 * i + 1], values[3 * i + 2]};
            }
            std::scoped_lock lock{run.Mutex};
            run.Snapshot.SourcePreview = std::move(preview);
            ++run.Snapshot.Revision;
        }

        // RUNTIME-289/ARCH-023 (RUNTIME-355 F3), worker, the first step: the device subsample orders the start prepared,
        // each in bounded pages submitted and waited for on this worker (no frame), before the solver samples; once per
        // run (a restart reuses the orders in Params). A verified order is the solver's; otherwise the set stays on the
        // CPU with the reason. A run that ended meanwhile records nothing: its Discard gave the waiting page up.
        void OrderSubsamples(EditorCoherentPointDriftRun& run)
        {
            bool processed = false;
            for (const bool target : {true, false})
            {
                auto& set = target ? run.TargetOrdering : run.SourceOrdering;
                if (run.CancelRequested.load() || !std::exchange(set.Pending, false) || !set.Device)
                    continue;
                std::string why;
                const bool ordered = set.Device->OrderOnWorker(why);
                if (run.CancelRequested.load())
                    break; // preserve the backend of an earlier completed order when the next one is cancelled
                processed = true;
                if (ordered)
                {
                    set.Backend = std::string(kPointSamplingGpuBackendId);
                    (target ? run.Params.SubsampleTargetOrder : run.Params.SubsampleSourceOrder) =
                        set.Device->Current();
                }
                else
                    set.Diagnostic = std::move(why);
            }
            if (processed)
                ReportSubsampling(run);
        }

        // Worker: the first iteration prepares the solver (kernel builds included), its device subsample orders first.
        [[nodiscard]] CPD::Status InitializeRun(EditorCoherentPointDriftRun& run)
        {
            if (run.Initialized)
                return CPD::Status::Success;
            OrderSubsamples(run);
            if (run.CancelRequested.load())
                return CPD::Status::Cancelled;
            run.Initialized = true;
            run.Started = run.LastIteration = std::chrono::steady_clock::now();
            {
                std::scoped_lock lock{run.Mutex};
                run.Snapshot.RunStarted = run.Started;
            }
            return run.Solver.Initialize(run.TargetWorld, run.SourceWorld, run.Params);
        }

        // Worker: one trace row per completed EM iteration, with the result's running scalars.
        void ObserveIteration(EditorCoherentPointDriftRun& run, const CPD::IterationTrace& trace)
        {
            const auto at = std::chrono::steady_clock::now();
            const double iterationSeconds = std::chrono::duration<double>(at - run.LastIteration).count();
            run.LastIteration = at;
            run.LastMetric.Values = {trace.Sigma2, trace.Objective, trace.MatchedWeight, trace.NegativeLogLikelihood};
            std::scoped_lock lock{run.Mutex};
            run.Snapshot.Trace.push_back({.Iteration = trace.Iteration, .Sigma2 = trace.Sigma2,
                                          .NegativeLogLikelihood = trace.NegativeLogLikelihood,
                                          .Objective = trace.Objective, .MatchedWeight = trace.MatchedWeight,
                                          .EStep = std::string(CPD::ToString(trace.EStep)),
                                          .EStepErrorBound = trace.EStepErrorBound,
                                          .EStepSampledError = trace.EStepSampledError,
                                          .NystromLocal = trace.NystromLocal,
                                          .KernelEvaluations = trace.KernelEvaluations,
                                          .Seconds = std::chrono::duration<double>(at - run.Started).count(),
                                          .IterationSeconds = iterationSeconds});
            auto& result = run.Snapshot.Result;
            result.Iterations = trace.Iteration + 1u;
            result.Sigma2 = trace.Sigma2;
            result.NegativeLogLikelihood = trace.NegativeLogLikelihood;
            result.MatchedWeight = trace.MatchedWeight;
            ++run.Snapshot.Revision;
        }

        // Worker, once the iteration's solver call returned: the step's outcome. The solver detects convergence in an
        // E-step that ends without an iteration, so a step may iterate nothing: its metric is that E-step's. The
        // solver's cap is the run's (Params.MaxIterations is Execution.IterationCap): the run ends it at that step.
        void FinishIteration(EditorCoherentPointDriftRun& run, const bool iterated)
        {
            {
                std::scoped_lock lock{run.Mutex};
                run.Snapshot.Stage.clear();
            }
            if (!run.Solver.Finished())
            {
                run.LastOutcome = StepOutcome::Continue;
                return;
            }
            const CPD::Result result = run.Solver.Current(); // once, at the end
            UpdateEstimate(run, result);
            if (!iterated)
                run.LastMetric.Values = {
                    result.Sigma2, result.NegativeLogLikelihood, result.MatchedWeight, result.NegativeLogLikelihood};
            run.LastOutcome = !result.Succeeded()               ? StepOutcome::Failed
                : result.Stop == CPD::Termination::IterationCap ? StepOutcome::Continue
                                                                : StepOutcome::Converged;
        }

        // Worker, one EM iteration. With the Vulkan E-step (ARCH-023) the device E-step is evaluated on this worker,
        // its one page submitted and waited for here (no frame or main-thread pump); the M-step, the objective and rows
        // flagged for exact evaluation stay on the CPU (hybrid: every iteration uploads the moved source and reads back
        // the row and source statistics). A refused or failed device E-step runs on the CPU; an ended run abandons the
        // pending step without fallback or M-step (the solver polls CancelRequested, which Discard sets).
        void Iterate(EditorCoherentPointDriftRun& run)
        {
            run.LastOutcome = StepOutcome::Failed;
            if (run.CancelRequested.load() || InitializeRun(run) != CPD::Status::Success)
                return;
            bool iterated = false;
            const CPD::IterationObserver observer = [&](const CPD::IterationTrace& trace)
            {
                iterated = true;
                ObserveIteration(run, trace);
            };
            if (!run.GpuEStep)
                (void)run.Solver.Step(observer);
            else if (run.Solver.BeginStep(observer) == CPD::StepState::AwaitingExternal)
            {
                const auto* request = run.Solver.PendingExternalRequest();
                const bool success = run.GpuEStep->Evaluate(*request);
                if (run.CancelRequested.load())
                    return; // abandoned: no fallback, no M-step
                (void)run.Solver.CompleteExternalStep(success, observer);
            }
            FinishIteration(run, iterated);
        }

        bool InputsCurrent(const EditorProcessingContext& context, const EditorCoherentPointDriftRun& run,
                           std::string& why)
        {
            if (context.AttachmentActive && !context.AttachmentActive()) { why = "The editor session changed."; return false; }
            if (context.Scene == nullptr) { why = "Scene is unavailable."; return false; }
            if (!GPD::EditorProcessingContextWorldCurrent(context))
            {
                why = "The active world changed.";
                return false;
            }
            entt::registry& raw = context.Scene->Raw();
            const auto source = ResolveStableEntity(raw, run.Config.SourceStableEntityId);
            const auto target = ResolveStableEntity(raw, run.Config.TargetStableEntityId);
            if (!source || !target || *source != run.SourceEntity || *target != run.TargetEntity)
            { why = "Source or target entity no longer exists."; return false; }
            if (!GPD::GeometryPropertiesCurrent(context, *source, run.Source.Inputs) ||
                !GPD::GeometryPropertiesCurrent(context, *target, run.Target.Inputs))
            { why = "Source or target points changed since the run started."; return false; }
            const auto transformCurrent = [&](entt::entity entity, bool had, const ECSC::Transform::Component& before) {
                const auto* now = raw.try_get<ECSC::Transform::Component>(entity);
                return had ? now != nullptr && SameTransformComponent(*now, before) : now == nullptr;
            };
            if (!transformCurrent(*source, run.SourceHadTransform, run.SourceBefore) ||
                !transformCurrent(*target, run.TargetHadTransform, run.TargetBefore))
            { why = "An entity transform changed since the run started."; return false; }
            return true;
        }

        EditorCommandHistoryStatus PublishTransform(const EditorProcessingContext& context, EditorCoherentPointDriftRun& run,
                                                    const glm::dmat4& map)
        {
            // New local transform: position' = map(position), rotation' = R * rotation, scale' = s * scale.
            const glm::dmat3 linear{map};
            if (!(glm::determinant(linear) > 0.0)) return EditorCommandHistoryStatus::StaleEntity; // mirrored: no TRS form
            const double scale = std::cbrt(glm::determinant(linear));
            const glm::dmat3 rotation = scale > 0.0 ? linear / scale : glm::dmat3(1.0);
            ECSC::Transform::Component after = run.SourceBefore;
            after.Position = glm::vec3(map * glm::dvec4(glm::dvec3(run.SourceBefore.Position), 1.0));
            after.Rotation = glm::normalize(glm::quat(glm::quat_cast(rotation)) * run.SourceBefore.Rotation);
            after.Scale = run.SourceBefore.Scale * float(scale);
            if (context.CommandHistory != nullptr)
                return ExecuteEditorTransformMutation(*context.CommandHistory, context.Scene, context.World,
                                                      run.Config.SourceStableEntityId, run.SourceBefore, after,
                                                      "Align point sets (Coherent Point Drift)").Status;
            entt::registry& raw = context.Scene->Raw();
            raw.get<ECSC::Transform::Component>(run.SourceEntity) = after;
            raw.emplace_or_replace<ECSC::Transform::IsDirtyTag>(run.SourceEntity);
            return EditorCommandHistoryStatus::Applied;
        }

        // Positions (overwrite the source binding) or a displacement property, in source-local space.
        EditorCommandHistoryStatus PublishProperty(const EditorProcessingContext& context, EditorCoherentPointDriftRun& run,
                                                   const std::vector<glm::vec3>& moved, std::string& why)
        {
            entt::registry& raw = context.Scene->Raw();
            const auto& c = run.Config;
            const bool positions = c.Output == CoherentPointDriftOutput::Positions;
            const std::string name = positions ? c.SourcePositions.Name : c.DisplacementName;
            const GeometryElementDomain domain = c.SourcePositions.Domain;
            const auto* props = GPD::MutableGeometryProperties(raw, run.SourceEntity, domain);
            if (props == nullptr) { why = "The source domain has no properties."; return EditorCommandHistoryStatus::StaleEntity; }
            const glm::dmat4 inverse = glm::inverse(run.SourceModel);
            if (!std::isfinite(inverse[0][0]) || glm::determinant(glm::dmat3(run.SourceModel)) == 0.0)
            { why = "The source transform is not invertible."; return EditorCommandHistoryStatus::StaleEntity; }

            // Normals: stored vertex normals of a deformed mesh, recomputed from the new positions.
            struct State { bool Exists{}; std::vector<glm::vec3> Values{}; std::vector<glm::vec3> Normals{}; };
            const auto existing = std::as_const(*props).Get<glm::vec3>(name);
            auto before = std::make_shared<State>(State{bool(existing), existing ? existing.Vector() : std::vector<glm::vec3>{}});
            auto after = std::make_shared<State>(State{true, existing ? existing.Vector() : std::vector<glm::vec3>(props->Size(), glm::vec3(0.0f))});
            for (std::size_t i = 0; i < run.Source.Slots.size(); ++i)
            {
                const glm::dvec3 local = glm::dvec3(inverse * glm::dvec4(glm::dvec3(moved[i]), 1.0));
                after->Values[run.Source.Slots[i]] =
                    positions ? glm::vec3(local) : glm::vec3(local - glm::dvec3(run.Source.Points[i]));
            }
            const auto available = BuildGeometryAvailability(raw, run.SourceEntity);
            static constexpr std::string_view kNormal = Geometry::HalfedgeMesh::VertexNormals::kDefaultOutputProperty;
            const auto storedNormals = std::as_const(*props).Get<glm::vec3>(kNormal);
            const bool recomputeNormals = positions && domain == GeometryElementDomain::MeshVertex &&
                                          name == ECSC::GeometrySources::PropertyNames::kPosition && storedNormals &&
                                          storedNormals.Size() == props->Size();
            if (recomputeNormals)
            {
                // The mesh is rebuilt from the stored topology (vertices keep their slots), then
                // re-positioned; a topology it cannot rebuild keeps its stored normals.
                auto built = GeometryProcessingDetail::MeshSupport::BuildHalfedgeMeshForVertexNormalRecompute(
                    available.SourceView, name);
                if (built.Succeeded() && built.Mesh.Positions().size() == after->Values.size())
                {
                    std::ranges::copy(after->Values, built.Mesh.Positions().begin());
                    const auto normals = Geometry::HalfedgeMesh::VertexNormals::Recompute(built.Mesh);
                    if (normals.Status == Geometry::HalfedgeMesh::VertexNormals::RecomputeStatus::Success &&
                        normals.Normals.Vector().size() == props->Size())
                    {
                        before->Normals = storedNormals.Vector();
                        after->Normals = normals.Normals.Vector();
                    }
                }
            }
            auto revisions = std::make_shared<std::vector<GPD::PointPropertyWatch>>(
                std::vector{GPD::ObserveGeometryProperty(available, domain, name)});
            if (!after->Normals.empty())
                revisions->push_back(GPD::ObserveGeometryProperty(available, domain, std::string(kNormal)));
            auto inputs = run.Source.Inputs;
            std::erase_if(inputs, [&](const auto& input) { return input.Domain == domain && input.Name == name; });
            const auto mutate = [context, entity = run.SourceEntity, inputs = std::move(inputs), revisions, domain, name,
                                 positions](const State& target) {
                if (!GPD::GeometryPropertiesCurrent(context, entity, inputs) ||
                    !GPD::GeometryPropertiesCurrent(context, entity, *revisions))
                    return EditorCommandHistoryStatus::StaleEntity;
                auto& registry = context.Scene->Raw();
                auto* set = GPD::MutableGeometryProperties(registry, entity, domain);
                const auto historyBefore = *revisions;
                if (target.Exists) set->GetOrAdd<glm::vec3>(name).Vector() = target.Values;
                else if (auto property = set->Get<glm::vec3>(name)) set->Remove(property);
                if (!target.Normals.empty()) set->GetOrAdd<glm::vec3>(std::string(kNormal)).Vector() = target.Normals;
                const auto now = BuildGeometryAvailability(registry, entity);
                for (auto& watch : *revisions) watch = GPD::ObserveGeometryProperty(now, domain, watch.Name);
                GPD::RebasePointPropertyHistory(context, entity, historyBefore);
                ECS::Components::DirtyTags::MarkGpuDirty(registry, entity);
                if (positions) ECS::Components::DirtyTags::MarkVertexPositionsDirty(registry, entity);
                if (!target.Normals.empty()) ECS::Components::DirtyTags::MarkVertexNormalsDirty(registry, entity);
                if (context.InvalidateWorkspaceSnapshotCache) context.InvalidateWorkspaceSnapshotCache();
                return EditorCommandHistoryStatus::Applied;
            };
            const std::string label = positions ? "Deform point set (Coherent Point Drift)"
                                                : "Coherent Point Drift displacement";
            return context.CommandHistory
                ? context.CommandHistory->Execute({.Label = label, .Redo = [mutate, after] { return mutate(*after); },
                                                   .Undo = [mutate, before] { return mutate(*before); }}).Status
                : mutate(*after);
        }

        // The run's output identity: every job of the run carries it.
        EditorJobIdentity Identity(const EditorCoherentPointDriftRun& run)
        {
            return EditorJobIdentity{.EntityId = run.Config.SourceStableEntityId,
                .Scope = ToEditorJobScope(run.Config.SourcePositions.Domain),
                .OutputSemantic = GeometryPresentationSlotSemantic::Displacement,
                .OutputName = "coherent_point_drift",
                .RequestedDomain = run.Config.EStep == CoherentPointDriftEStep::Vulkan ? EditorJobDomain::GpuCompute
                                                                                       : EditorJobDomain::Cpu};
        }

        // RUNTIME-289/ARCH-023 (RUNTIME-355 F3), main thread at the start: prepares one full set's device order on its
        // resident positions (captured live rows and model, so the rows match the world copies the solver samples):
        // the workspace's pipeline, buffers, row map and transform, and the order's page gate. The run's first step
        // orders it on its worker (OrderSubsamples). Otherwise the set stays on the CPU with the reason.
        void PrepareSubsampleOrdering(
            const EditorProcessingContext& context, EditorCoherentPointDriftRun& run, const bool target)
        {
            auto& set = target ? run.TargetOrdering : run.SourceOrdering;
            const auto fallback = [&set](std::string_view why)
            {
                set.Pending = false;
                set.Diagnostic = std::string(why) + "; sampling ran on the CPU.";
            };
            auto* residency = context.SpatialIndices ? context.SpatialIndices->PropertyResidency() : nullptr;
            if (residency == nullptr || context.Scene == nullptr || context.Device == nullptr)
                return fallback("GPU property residency unavailable");
            const auto& capture = target ? run.Target : run.Source;
            const auto positions = ResolveGpuPropertyInput(*residency,
                *context.Scene,
                context.World,
                target ? run.TargetEntity : run.SourceEntity,
                target ? run.Config.TargetPositions : run.Config.SourcePositions);
            if (!positions) return fallback("Resident sampling input acquisition refused");
            const std::uint32_t count = target ? run.Params.SubsampleTarget : run.Params.SubsampleSource;
            Graphics::FarthestPointGpuInput input{.Positions = *positions,
                .Rows = capture.Slots,
                .FirstIndex = run.Params.SubsampleSampling.FirstIndex,
                .Count = count};
            if (capture.Slots.size() == positions->Layout.Count) input.Rows = {};
            const glm::dmat4& model = target ? run.TargetModel : run.SourceModel;
            for (std::size_t column = 0; column < 4; ++column)
                for (std::size_t row = 0; row < 4; ++row) input.Model[4 * column + row] = model[column][row];
            auto gpu = std::make_shared<PointSamplingGpuRun>(*context.SpatialIndices,
                input,
                target ? run.TargetWorld : run.SourceWorld,
                run.Params.SubsampleSampling);
            if (!gpu->PrepareWorker(*context.Device, *residency))
                return fallback("The Vulkan sampler refused its resident input");
            set.Device = std::move(gpu);
        }

        // Main thread, once no step runs (the settled end): the device orders' resident input slots, stamped with the
        // compute fence, and their workspaces go; a page a discarded run gave up keeps them reserved until it completed.
        void ReleaseOrderings(EditorCoherentPointDriftRun& run)
        {
            for (auto* set : {&run.TargetOrdering, &run.SourceOrdering})
                if (auto device = std::exchange(set->Device, {}))
                    device->Release();
        }

        // Accept (no iteration runs): the current estimate, after revalidating the captured inputs and both entity
        // transforms, as one undoable entry: the source transform (rigid) or its positions or displacement property.
        EditorCoherentPointDriftResult Publish(const EditorProcessingContext& context, EditorCoherentPointDriftRun& run)
        {
            std::vector<float> values;
            RefreshEstimate(run, &values);
            EditorCoherentPointDriftResult result;
            {
                std::scoped_lock lock{run.Mutex};
                result = run.Snapshot.Result;
            }
            result.StepRun = run.Run;
            const auto reject = [&](EditorCommandStatus status, std::string message)
            {
                result.Status = status;
                result.Message = std::move(message);
                return result;
            };
            std::string why;
            if (!InputsCurrent(context, run, why))
                return reject(EditorCommandStatus::StaleEntity, why + " Start a new run.");
            if (run.Config.Output == CoherentPointDriftOutput::SourceTransform &&
                !(glm::determinant(glm::dmat3(result.Transform)) > 0.0))
                return reject(EditorCommandStatus::InvalidProcessingParameters,
                    "A mirrored result cannot be stored in the source transform; write positions instead.");
            std::vector<glm::vec3> moved(values.size() / 3u);
            for (std::size_t i = 0; i < moved.size(); ++i)
                moved[i] = {values[3 * i], values[3 * i + 1], values[3 * i + 2]};
            const EditorCommandHistoryStatus status = run.Config.Output == CoherentPointDriftOutput::SourceTransform
                ? PublishTransform(context, run, result.Transform)
                : PublishProperty(context, run, moved, why);
            result.Status = ToEditorCommandStatus(status);
            if (!result.Succeeded())
                return reject(result.Status,
                    why.empty() ? "Coherent Point Drift publication was rejected by history checks." : why);
            result.Message = "Coherent Point Drift (" + std::string(CPD::ToString(ToVariant(run.Config.Method))) +
                ", " + result.Backend + ") applied after " + std::to_string(result.Iterations) + " iterations (" +
                result.Termination + ").";
            return result;
        }

        void Deliver(EditorCoherentPointDriftRun& run, EditorCoherentPointDriftResult result)
        {
            {
                std::scoped_lock lock{run.Mutex};
                run.Snapshot.Result = result;
                ++run.Snapshot.Revision;
            }
            if (!std::exchange(run.Delivered, true) && run.Sink)
                run.Sink(std::move(result));
        }

        // The run's Discard: it ended without a commit (discarded, cancelled, stale or failed). Nothing is published.
        // At once, also while an iteration runs: it stops at its solver's next poll, and a pending device E-step or
        // subsample ordering page is given up without a CPU fallback (the device may still run it: the settled end
        // releases what it uses, reserved until it completed). Reads no iteration state.
        void DiscardRun(EditorCoherentPointDriftRun& run)
        {
            run.Ended = true;
            run.CancelRequested = true;
            if (run.GpuEStep)
                run.GpuEStep->Close("Coherent Point Drift ended.");
            for (auto* set : {&run.TargetOrdering, &run.SourceOrdering})
                if (set->Device)
                    set->Device->Abort();
        }

        // The run's settled end (StepRunCallbacks::Finished: no iteration runs and the holder released the source, so
        // a new run there is admitted from here on), from its final observation. Without a commit the caller gets its
        // one terminal result here; after a Commit its answer was delivered already.
        void EndRun(EditorCoherentPointDriftRun& run, const StepRunObservation& observed)
        {
            if (run.Delivered)
                return;
            EditorCoherentPointDriftResult result;
            {
                std::scoped_lock lock{run.Mutex};
                result = run.Snapshot.Result;
            }
            result.StepRun = run.Run;
            const bool failed = observed.Phase == StepRunPhase::Failed;
            const bool stale = observed.Diagnostic.starts_with("stale");
            // Finished guarantees no iteration runs, so the solver says why a failed one failed.
            const std::string solver =
                failed ? ": " + std::string(CPD::ToString(run.Solver.Current().State)) : std::string{};
            result.Status = failed ? EditorCommandStatus::GeometryProcessingFailed : EditorCommandStatus::StaleEntity;
            result.Message = failed
                ? "Coherent Point Drift failed (" + observed.Diagnostic + solver + "); nothing was applied."
                : stale ? "Coherent Point Drift inputs changed since the run started; nothing was applied."
                        : "Coherent Point Drift was cancelled; nothing was applied.";
            Deliver(run, std::move(result));
        }

        // Restart (no iteration runs): a new generation from the captured inputs. The broker, its workspace and the
        // uploaded target stay; device subsample orders already in Params are reused.
        void RestartRun(EditorCoherentPointDriftRun& run)
        {
            run.Solver = CPD::Solver{};
            run.Initialized = false;
            {
                std::scoped_lock lock{run.Mutex};
                auto& snapshot = run.Snapshot;
                snapshot.Trace.clear();
                snapshot.Result = {.Status = EditorCommandStatus::Pending,
                    .Method = run.Config.Method,
                    .Output = run.Config.Output,
                    .Backend = std::string(CPD::BackendId(run.Params.EStep)),
                    .SourcePointCount = run.SourceWorld.size(),
                    .TargetPointCount = run.TargetWorld.size(),
                    .StepRun = run.Run,
                    .Message = "Coherent Point Drift restarted."};
                snapshot.SourcePreview = std::make_shared<const std::vector<glm::vec3>>(run.SourceWorld);
                snapshot.SourceSamples.reset();
                snapshot.TargetSamples.reset();
                snapshot.Stage.clear();
                snapshot.RunStarted = {};
                ++snapshot.Revision;
            }
            ReportSubsampling(run); // the orderings' report survives the new generation
        }

        // The run's callbacks: every step is a StepRuns segment step on a worker (one EM iteration, the first one with
        // the device subsample orders); the rest runs on the main thread.
        [[nodiscard]] StepRunCallbacks MakeCallbacks(const EditorCoherentPointDriftRunHandle& run)
        {
            return StepRunCallbacks{
                .Step =
                    [run](StepRunMetric& metric)
                {
                    Iterate(*run); // the solver and a pending device page stop at once once Discard ran
                    metric.Values = run->LastMetric.Values;
                    return run->LastOutcome;
                },
                .Preview =
                    [run](StepPreview& preview)
                {
                    RefreshEstimate(*run, &preview.Values);
                    return true;
                },
                .Show =
                    [run](const StepPreview& preview)
                {
                    ShowEstimate(*run, preview.Values);
                    return StepRunShow::Shown;
                },
                .Valid =
                    [run]
                {
                    std::string why;
                    return InputsCurrent(run->Context, *run, why);
                },
                .Commit =
                    [run](std::string& diagnostic)
                {
                    auto result = Publish(run->Context, *run);
                    const bool applied = result.Succeeded();
                    if (!applied)
                        diagnostic = result.Message;
                    run->Ended = applied || run->Ended;
                    // No step runs (Commit is called at a rest): the device workspace returns to the pool now.
                    if (run->Ended && run->GpuEStep)
                    {
                        run->GpuEStep->Close();
                        run->GpuEStep->ReleaseDeviceResources();
                    }
                    Deliver(*run, std::move(result));
                    return applied ? StepRunCommit::Committed : StepRunCommit::Refused;
                },
                .Discard =
                    [run]
                {
                    DiscardRun(*run);
                },
                .Restart =
                    [run](std::string&)
                {
                    RestartRun(*run);
                    return true;
                },
                .Finished =
                    [run](const StepRunObservation& observed)
                {
                    ReleaseOrderings(*run); // no step runs any more
                    EndRun(*run, observed);
                },
            };
        }

        // Without a job lane (the immediate outcome of the one-shot command): every iteration at once, then Accept.
        EditorCoherentPointDriftResult RunInline(
            const EditorProcessingContext& context, EditorCoherentPointDriftRun& run)
        {
            if (const auto status = InitializeRun(run); status != CPD::Status::Success)
                return Failure(run.Config,
                    EditorCommandStatus::GeometryProcessingFailed,
                    "Coherent Point Drift rejected the input: " + std::string(CPD::ToString(status)) + ".");
            while (run.Solver.Step(
                [&run](const CPD::IterationTrace& trace)
                {
                    ObserveIteration(run, trace);
                }))
            {
            }
            if (const auto current = run.Solver.Current(); !current.Succeeded())
                return Failure(run.Config,
                    EditorCommandStatus::GeometryProcessingFailed,
                    "Coherent Point Drift failed: " + std::string(CPD::ToString(current.State)) + ".");
            auto result = Publish(context, run);
            std::scoped_lock lock{run.Mutex};
            run.Snapshot.Result = result;
            return result;
        }

        // A run that captured its inputs: its step run and its prepared device subsample orders. Null with `failure`
        // when the session refuses it (no step runs, another run holds the output, a job refused).
        EditorCoherentPointDriftRunHandle StartRun(const EditorProcessingContext& context,
            const CoherentPointDriftConfig& config,
            std::function<void(EditorCoherentPointDriftResult)> sink,
            EditorCoherentPointDriftResult& failure)
        {
            if (!context.JobCommands.Available() || context.StepRuns == nullptr)
            {
                failure = Failure(config,
                    EditorCommandStatus::GeometryProcessingFailed,
                    "Coherent Point Drift runs need the job lane and the session's step runs.");
                return nullptr;
            }
            auto run = std::make_shared<EditorCoherentPointDriftRun>();
            run->Context = context;
            auto resolved = config;
            if (auto error = Capture(context, resolved, true, run.get()))
            {
                failure = std::move(*error);
                return nullptr;
            }
            if (const auto active =
                    GPD::MeshSupport::ActiveOutputJobRefusal(context, Identity(*run), "Coherent Point Drift"))
            {
                failure = Failure(resolved, EditorCommandStatus::Pending, active->Message);
                return nullptr;
            }
            auto& snapshot = run->Snapshot;
            snapshot.Result = {.Status = EditorCommandStatus::Pending,
                .Method = resolved.Method,
                .Output = resolved.Output,
                .Backend = std::string(CPD::BackendId(run->Params.EStep)),
                .SourcePointCount = run->SourceWorld.size(),
                .TargetPointCount = run->TargetWorld.size(),
                .Message = "Coherent Point Drift is ready."};
            snapshot.SourcePreview = std::make_shared<const std::vector<glm::vec3>>(run->SourceWorld);
            snapshot.Target = std::make_shared<const std::vector<glm::vec3>>(run->TargetWorld);
            snapshot.Revision = 1u;

            // Every run steps on a worker (StepRuns segments), its Vulkan E-step and subsample orders computed there
            // (ARCH-023).
            std::string refusal;
            run->Run = context.StepRuns->Start(
                {.Label = "Coherent Point Drift",
                    .Owner = "Runtime.RegistrationOperations",
                    .Output = {.StableEntityId = resolved.SourceStableEntityId, .Property = resolved.SourcePositions},
                    .Execution = resolved.Execution,
                    .MetricNames = {"sigma2", "objective", "matched", "nll"}},
                MakeCallbacks(run),
                EditorStepRunJobs(context.JobCommands, Identity(*run), std::make_shared<JobToken>()),
                refusal);
            if (run->Run == 0u)
            {
                run->Delivered = true;
                failure = Failure(resolved,
                    EditorCommandStatus::GeometryProcessingFailed,
                    "The job lane rejected the Coherent Point Drift run: " + refusal);
                return nullptr;
            }
            snapshot.StepRun = run->Run;
            snapshot.Result.StepRun = run->Run;
            run->Sink = std::move(sink);
            // RUNTIME-355 F3: the device subsample orders' owner side, before any step (a segment starts from the
            // holder's pump in a later drain); the first step computes them on its worker.
            for (const bool target : {true, false})
                if ((target ? run->TargetOrdering : run->SourceOrdering).Pending)
                    PrepareSubsampleOrdering(context, *run, target);
            ReportSubsampling(*run);
            return run;
        }
    }

    std::string CoherentPointDriftVulkanUnavailableReason(const EditorProcessingCommands& commands)
    {
        return VulkanEStepUnavailable(EditorProcessingCommandsAccess::Resolve(commands));
    }

    ActionReadiness PreviewEditorCoherentPointDriftCommand(const EditorProcessingCommands& commands,
                                                           const CoherentPointDriftConfig& config)
    {
        auto resolved = config;
        const auto failure = Capture(EditorProcessingCommandsAccess::Resolve(commands), resolved, false, nullptr);
        return {!failure.has_value(), failure ? failure->Message : std::string{}};
    }

    EditorCoherentPointDriftRunHandle StartEditorCoherentPointDrift(const EditorProcessingCommands& commands,
                                                                    const CoherentPointDriftConfig& config,
                                                                    EditorCoherentPointDriftResult& failure)
    {
        return StartRun(EditorProcessingCommandsAccess::Resolve(commands), config, {}, failure);
    }

    EditorCoherentPointDriftSnapshot SnapshotEditorCoherentPointDrift(const EditorCoherentPointDriftRunHandle& run)
    {
        if (!run) return {};
        std::scoped_lock lock{run->Mutex};
        return run->Snapshot;
    }

    EditorCoherentPointDriftResult ApplyEditorCoherentPointDriftCommand(
        const EditorProcessingCommands& commands, const CoherentPointDriftConfig& config,
        std::function<void(EditorCoherentPointDriftResult)> onComplete)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        if (!context.JobCommands.Available())
        {
            EditorCoherentPointDriftRun run;
            run.Context = context;
            auto resolved = config;
            if (auto error = Capture(context, resolved, true, &run))
                return *error;
            ReportSubsampling(run);
            auto result = RunInline(context, run);
            std::scoped_lock lock{run.Mutex};
            result.SubsampleRequestedBackend = run.Snapshot.Result.SubsampleRequestedBackend;
            result.SubsampleTargetBackend = run.Snapshot.Result.SubsampleTargetBackend;
            result.SubsampleSourceBackend = run.Snapshot.Result.SubsampleSourceBackend;
            result.SubsampleBackendDiagnostic = run.Snapshot.Result.SubsampleBackendDiagnostic;
            return result;
        }
        // The one-shot command runs at once and accepts at its end, whatever the section's start and auto_accept.
        auto batch = config;
        batch.Execution.Start = StepRunStart::Run;
        batch.Execution.AutoAccept = StepRunAutoAccept::End;
        EditorCoherentPointDriftResult failure;
        const auto run = StartRun(context, batch, GuardEditorProcessingResult(context, std::move(onComplete)), failure);
        if (!run)
            return failure;
        auto pending = SnapshotEditorCoherentPointDrift(run).Result;
        pending.Status = EditorCommandStatus::Pending;
        pending.Message = "Coherent Point Drift queued (" + pending.Backend + ").";
        return pending;
    }

    EditorCoherentPointDriftResult ApplyEditorConfiguredCoherentPointDrift(
        const EditorProcessingCommands& commands, std::function<void(EditorCoherentPointDriftResult)> onComplete)
    {
        const auto config = GetEditorCoherentPointDriftConfig(commands);
        if (!config)
            return {.Status = EditorCommandStatus::InvalidProcessingParameters,
                    .Message = "The Coherent Point Drift config is unavailable."};
        return ApplyEditorCoherentPointDriftCommand(commands, *config, std::move(onComplete));
    }

    RuntimeEngineConfigApplyResult ApplyEditorCoherentPointDriftConfig(const EditorProcessingCommands& commands,
                                                                       const CoherentPointDriftConfig& config,
                                                                       std::string sourceId)
    {
        return ApplyEditorProcessingConfig(commands,
            ValidateCoherentPointDriftConfigSection(SerializeCoherentPointDriftConfig(config), {},
                                                    kCoherentPointDriftConfigSectionName),
            sourceId.empty() ? std::string{kCoherentPointDriftConfigSectionName} : sourceId,
            [&](Core::Config::EngineConfig& candidate) { SetCoherentPointDriftConfig(candidate, config); });
    }

    std::optional<CoherentPointDriftConfig> GetEditorCoherentPointDriftConfig(const EditorProcessingCommands& commands)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        if (!context.EngineConfigControlState) return std::nullopt;
        return GetCoherentPointDriftConfig(context.EngineConfigControlState->ActiveConfig);
    }
}
