// Coherent Point Drift editor operation (RUNTIME-273). A run owns world-space copies of
// both point sets and a Geometry::CoherentPointDrift::Solver that only step jobs touch;
// after every iteration the worker publishes a snapshot (trace, moving source positions)
// under a mutex, so panels and agents read progress without waiting on the solver.
// Nothing is written to the scene until Apply, which revalidates the captured inputs and
// both entity transforms and publishes one undoable history entry.
module;
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
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
import Extrinsic.Core.Dag.Scheduler;
import Extrinsic.Core.Error;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.WorldHandle;
import Geometry.Properties;
import Geometry.Registration.CoherentPointDrift;

#include "Editor/internal/Runtime.EditorGeometryHelpers.hpp"
#include "Editor/internal/Runtime.EditorTransformHelpers.hpp"
#include "Editor/internal/Runtime.EditorProcessingAccess.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.PointFields.hpp"

namespace Extrinsic::Runtime
{
    namespace CPD = Geometry::CoherentPointDrift;
    namespace ECSC = ECS::Components;
    namespace GPD = GeometryProcessingDetail;

    struct EditorCoherentPointDriftRun
    {
        CoherentPointDriftConfig Config{};
        entt::entity SourceEntity{entt::null}, TargetEntity{entt::null};
        GPD::PointInputCapture Source{}, Target{};
        ECSC::Transform::Component SourceBefore{}, TargetBefore{};
        bool SourceHadTransform{false}, TargetHadTransform{false};
        glm::dmat4 SourceModel{1.0};
        std::vector<glm::vec3> SourceWorld{}, TargetWorld{};
        CPD::Params Params{};

        // Worker-owned; only one step job runs at a time (Busy).
        CPD::Solver Solver{};
        bool Initialized{false};
        std::atomic<bool> Busy{false};
        std::atomic<bool> CancelRequested{false};

        mutable std::mutex Mutex{};
        EditorCoherentPointDriftSnapshot Snapshot{};
    };

    namespace
    {
        using EditorFeatureDetail::ResolveStableEntity;
        using EditorFeatureDetail::SameTransformComponent;
        using EditorFeatureDetail::ExecuteEditorTransformMutation;
        using EditorFeatureDetail::ToEditorCommandStatus;
        using Phase = EditorCoherentPointDriftPhase;

        glm::dmat4 ModelMatrix(const ECSC::Transform::Component& transform)
        {
            glm::dmat4 model = glm::dmat4(glm::mat4_cast(glm::dquat(transform.Rotation)));
            model[0] *= double(transform.Scale.x);
            model[1] *= double(transform.Scale.y);
            model[2] *= double(transform.Scale.z);
            model[3] = glm::dvec4(glm::dvec3(transform.Position), 1.0);
            return model;
        }

        CPD::Variant ToVariant(CoherentPointDriftMethod method) noexcept
        {
            switch (method)
            {
            case CoherentPointDriftMethod::Rigid: return CPD::Variant::Rigid;
            case CoherentPointDriftMethod::Affine: return CPD::Variant::Affine;
            case CoherentPointDriftMethod::Nonrigid: return CPD::Variant::Nonrigid;
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
                      std::uint8_t(CoherentPointDriftEStep::FastGauss) == std::uint8_t(CPD::EStepPolicy::FastGauss));

        std::size_t MinimumPoints(CoherentPointDriftMethod method) noexcept
        {
            return method == CoherentPointDriftMethod::Affine ? 4u : 3u;
        }

        // Shared by readiness (no value copy) and Start (values copied, world space).
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
            const std::size_t nonrigidLimit = config.LowRank > 0u ? CPD::kMaxLowRankSourcePoints : CPD::kMaxNonrigidSourcePoints;
            if (config.Method == CoherentPointDriftMethod::Nonrigid && sourceCapture.LiveCount > nonrigidLimit)
                return Failure(config, EditorCommandStatus::InvalidProcessingParameters,
                               config.LowRank > 0u
                                   ? "Low-rank nonrigid CPD handles at most " + std::to_string(nonrigidLimit) + " source points."
                                   : "Nonrigid CPD with the full kernel handles at most " + std::to_string(nonrigidLimit) +
                                         " source points; set a low rank (for example 100) or subsample the source.");
            const auto* sourceTransform = raw.try_get<ECSC::Transform::Component>(*source);
            if (config.Output == CoherentPointDriftOutput::SourceTransform && sourceTransform == nullptr)
                return Failure(config, EditorCommandStatus::MissingTransform, "The source entity has no Transform to drive.");
            if (config.Output == CoherentPointDriftOutput::DisplacementProperty)
            {
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
            run->SourceModel = sourceTransform ? ModelMatrix(*sourceTransform) : glm::dmat4(1.0);
            const glm::dmat4 targetModel = targetTransform ? ModelMatrix(*targetTransform) : glm::dmat4(1.0);
            for (const auto& p : sourceCapture.Points) run->SourceWorld.push_back(glm::vec3(run->SourceModel * glm::dvec4(glm::dvec3(p), 1.0)));
            for (const auto& p : targetCapture.Points) run->TargetWorld.push_back(glm::vec3(targetModel * glm::dvec4(glm::dvec3(p), 1.0)));
            run->Source = std::move(sourceCapture);
            run->Target = std::move(targetCapture);
            run->Params = CPD::Params{.Method = ToVariant(config.Method), .OutlierWeight = config.OutlierWeight,
                                      .MaxIterations = config.MaxIterations, .Tolerance = config.Tolerance,
                                      .InitialSigma2 = config.InitialSigma2, .Sigma2Floor = config.Sigma2Floor,
                                      .NormalizeInputs = config.NormalizeInputs, .EstimateScale = config.EstimateScale,
                                      .AllowReflection = config.AllowReflection, .Beta = config.Beta, .Lambda = config.Lambda,
                                      .EStep = CPD::EStepPolicy(config.EStep), .EStepTolerance = config.EStepTolerance,
                                      .Threads = config.Threads, .LowRank = config.LowRank};
            return std::nullopt;
        }

        // Worker side: copy the solver's current estimate into the shared snapshot.
        void PublishProgress(EditorCoherentPointDriftRun& run, std::optional<Phase> phase)
        {
            const CPD::Result current = run.Solver.Current();
            std::vector<glm::vec3> preview(current.TransformedSource.size());
            double displacement = 0.0;
            for (std::size_t i = 0; i < preview.size(); ++i)
            {
                preview[i] = glm::vec3(current.TransformedSource[i]);
                displacement += glm::length(current.TransformedSource[i] - glm::dvec3(run.SourceWorld[i]));
            }
            std::scoped_lock lock{run.Mutex};
            auto& snapshot = run.Snapshot;
            auto& result = snapshot.Result;
            result.Iterations = current.Iterations;
            result.Sigma2 = current.Sigma2;
            result.NegativeLogLikelihood = current.NegativeLogLikelihood;
            result.MatchedWeight = current.MatchedWeight;
            result.Transform = current.Transform;
            result.Termination = std::string(CPD::ToString(current.Stop));
            result.Backend = std::string(current.Backend);
            result.EStepErrorBound = current.EStepErrorBound;
            result.KernelRank = current.KernelRank;
            result.KernelApproximationError = current.KernelApproximationError;
            result.MeanDisplacement = preview.empty() ? 0.0 : displacement / double(preview.size());
            if (!current.Succeeded())
            {
                result.Status = EditorCommandStatus::GeometryProcessingFailed;
                result.Message = "Coherent Point Drift failed: " + std::string(CPD::ToString(current.State)) + ".";
            }
            else
                snapshot.SourcePreview = std::move(preview);
            if (phase) snapshot.Phase = *phase;
            ++snapshot.Revision;
        }

        // Runs up to `iterations` EM iterations (0: until the solver ends).
        void RunSteps(EditorCoherentPointDriftRun& run, std::uint32_t iterations, const JobCancellation& cancellation)
        {
            if (!run.Initialized)
            {
                run.Initialized = true;
                const auto status = run.Solver.Initialize(run.TargetWorld, run.SourceWorld, run.Params);
                if (status != CPD::Status::Success)
                {
                    std::scoped_lock lock{run.Mutex};
                    run.Snapshot.Phase = Phase::Failed;
                    run.Snapshot.Result.Status = EditorCommandStatus::GeometryProcessingFailed;
                    run.Snapshot.Result.Message = "Coherent Point Drift rejected the input: " + std::string(CPD::ToString(status)) + ".";
                    ++run.Snapshot.Revision;
                    return;
                }
            }
            std::uint32_t done = 0;
            const CPD::IterationObserver observer = [&run](const CPD::IterationTrace& trace) {
                std::scoped_lock lock{run.Mutex};
                run.Snapshot.Trace.push_back({.Iteration = trace.Iteration, .Sigma2 = trace.Sigma2,
                                              .NegativeLogLikelihood = trace.NegativeLogLikelihood,
                                              .Objective = trace.Objective, .MatchedWeight = trace.MatchedWeight,
                                              .EStep = std::string(CPD::ToString(trace.EStep)),
                                              .EStepErrorBound = trace.EStepErrorBound});
            };
            while (!run.Solver.Finished())
            {
                if (cancellation.IsCancelled() || run.CancelRequested.load())
                {
                    PublishProgress(run, Phase::Cancelled);
                    return;
                }
                run.Solver.Step(observer);
                ++done;
                const bool ended = run.Solver.Finished();
                PublishProgress(run, ended ? std::optional{run.Solver.Current().Succeeded() ? Phase::Finished : Phase::Failed}
                                           : std::nullopt);
                if (ended) return;
                if (iterations != 0u && done >= iterations)
                {
                    PublishProgress(run, Phase::Paused);
                    return;
                }
            }
            // Finished before this call (e.g. a converged solver asked for more steps).
            PublishProgress(run, run.Solver.Current().Succeeded() ? Phase::Finished : Phase::Failed);
        }

        bool InputsCurrent(const EditorProcessingContext& context, const EditorCoherentPointDriftRun& run,
                           std::string& why)
        {
            if (context.AttachmentActive && !context.AttachmentActive()) { why = "The editor session changed."; return false; }
            if (context.Scene == nullptr) { why = "Scene is unavailable."; return false; }
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

            struct State { bool Exists{}; std::vector<glm::vec3> Values{}; };
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
            auto revisions = std::make_shared<std::vector<GPD::PointPropertyWatch>>(
                std::vector{GPD::ObserveGeometryProperty(available, domain, name)});
            auto inputs = run.Source.Inputs;
            std::erase_if(inputs, [&](const auto& input) { return input.Domain == domain && input.Name == name; });
            const auto mutate = [context, entity = run.SourceEntity, inputs = std::move(inputs), revisions, domain, name,
                                 positions](const State& target) {
                if (!GPD::GeometryPropertiesCurrent(context, entity, inputs) ||
                    !GPD::GeometryPropertiesCurrent(context, entity, *revisions))
                    return EditorCommandHistoryStatus::StaleEntity;
                auto& registry = context.Scene->Raw();
                auto* set = GPD::MutableGeometryProperties(registry, entity, domain);
                if (target.Exists) set->GetOrAdd<glm::vec3>(name).Vector() = target.Values;
                else if (auto property = set->Get<glm::vec3>(name)) set->Remove(property);
                *revisions = {GPD::ObserveGeometryProperty(BuildGeometryAvailability(registry, entity), domain, name)};
                ECS::Components::DirtyTags::MarkGpuDirty(registry, entity);
                if (positions) ECS::Components::DirtyTags::MarkVertexPositionsDirty(registry, entity);
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

        EditorCoherentPointDriftResult Publish(const EditorProcessingContext& context, EditorCoherentPointDriftRun& run)
        {
            EditorCoherentPointDriftSnapshot snapshot;
            {
                std::scoped_lock lock{run.Mutex};
                snapshot = run.Snapshot;
            }
            auto result = snapshot.Result;
            const auto reject = [&](EditorCommandStatus status, std::string message) {
                result.Status = status;
                result.Message = std::move(message);
                return result;
            };
            if (snapshot.Phase != Phase::Paused && snapshot.Phase != Phase::Finished)
                return reject(EditorCommandStatus::InvalidProcessingParameters,
                              std::string("Nothing to apply: the run is ") + ToString(snapshot.Phase) + ".");
            std::string why;
            if (!InputsCurrent(context, run, why)) return reject(EditorCommandStatus::StaleEntity, why + " Start a new run.");
            if (run.Config.Output == CoherentPointDriftOutput::SourceTransform &&
                !(glm::determinant(glm::dmat3(snapshot.Result.Transform)) > 0.0))
                return reject(EditorCommandStatus::InvalidProcessingParameters,
                              "A mirrored result cannot be stored in the source transform; write positions instead.");
            const EditorCommandHistoryStatus status = run.Config.Output == CoherentPointDriftOutput::SourceTransform
                ? PublishTransform(context, run, snapshot.Result.Transform)
                : PublishProperty(context, run, snapshot.SourcePreview, why);
            result.Status = ToEditorCommandStatus(status);
            if (!result.Succeeded())
                return reject(result.Status, why.empty() ? "Coherent Point Drift publication was rejected by history checks." : why);
            result.Message = "Coherent Point Drift (" + std::string(CPD::ToString(ToVariant(run.Config.Method))) + ", " +
                             result.Backend + ") applied after " + std::to_string(result.Iterations) + " iterations (" +
                             result.Termination + ").";
            std::scoped_lock lock{run.Mutex};
            run.Snapshot.Phase = Phase::Applied;
            run.Snapshot.Result = result;
            ++run.Snapshot.Revision;
            return result;
        }

        EditorJobIdentity Identity(const EditorCoherentPointDriftRun& run)
        {
            return EditorJobIdentity{.EntityId = run.Config.SourceStableEntityId,
                                     .Scope = ToEditorJobScope(run.Config.SourcePositions.Domain),
                                     .OutputSemantic = GeometryPresentationSlotSemantic::Displacement,
                                     .OutputName = "coherent_point_drift"};
        }

        // Queues one step job; `publish` applies the result when the run ends (one-shot command).
        EditorCommandStatus Submit(const EditorProcessingContext& context, const EditorCoherentPointDriftRunHandle& run,
                                   std::uint32_t iterations,
                                   std::function<void(EditorCoherentPointDriftResult)> publish)
        {
            bool expected = false;
            if (!run->Busy.compare_exchange_strong(expected, true)) return EditorCommandStatus::Pending;
            {
                std::scoped_lock lock{run->Mutex};
                const Phase phase = run->Snapshot.Phase;
                if (phase != Phase::Ready && phase != Phase::Paused)
                {
                    run->Busy = false;
                    return EditorCommandStatus::InvalidProcessingParameters;
                }
                run->Snapshot.Phase = Phase::Running;
                run->Snapshot.Result.Status = EditorCommandStatus::Pending;
                ++run->Snapshot.Revision;
            }
            if (!context.JobCommands.Available())
            {
                RunSteps(*run, iterations, JobCancellation{});
                run->Busy = false;
                if (publish) publish(Publish(context, *run));
                return EditorCommandStatus::Pending;
            }
            auto delivered = std::make_shared<bool>(false);
            const std::size_t work = std::max(run->SourceWorld.size(), run->TargetWorld.size());
            JobDesc desc{
                .DebugName = "Sandbox.CoherentPointDrift",
                .Scope = context.World,
                .Priority = Core::Dag::TaskPriority::Normal,
                .Kind = RuntimeTaskKinds::GeometryProcess,
                .EstimatedCost = std::uint32_t(std::min<std::size_t>(1u << 20, 1u + work / 1024u)),
                .Work = [run, iterations](const JobCancellation& cancellation) -> JobResultEnvelope {
                    RunSteps(*run, iterations, cancellation);
                    return JobResultEnvelope::Make(true);
                },
                .ValidateBeforeApply = [] { return JobApplyValidation::Current; },
                .PublishCompletion = [context, run, publish, delivered](KernelEventBus&, const JobResultEnvelope&) {
                    run->Busy = false;
                    if (publish && !*delivered)
                    {
                        *delivered = true;
                        publish(Publish(context, *run));
                    }
                    return true;
                },
                .FinalizeUnpublishedOnMainThread = [context, run, publish, delivered] {
                    run->Busy = false;
                    {
                        std::scoped_lock lock{run->Mutex};
                        if (run->Snapshot.Phase == Phase::Running) run->Snapshot.Phase = Phase::Cancelled;
                        ++run->Snapshot.Revision;
                    }
                    if (publish && !*delivered)
                    {
                        *delivered = true;
                        auto result = SnapshotEditorCoherentPointDrift(run).Result;
                        result.Status = EditorCommandStatus::StaleEntity;
                        result.Message = "Coherent Point Drift was cancelled or its world closed; nothing was applied.";
                        publish(std::move(result));
                    }
                },
            };
            const JobToken token = context.JobCommands.Submit(std::move(desc), Identity(*run));
            if (!token.IsValid())
            {
                run->Busy = false;
                std::scoped_lock lock{run->Mutex};
                run->Snapshot.Phase = Phase::Failed;
                run->Snapshot.Result.Status = EditorCommandStatus::GeometryProcessingFailed;
                run->Snapshot.Result.Message = "The job lane rejected the Coherent Point Drift job.";
                ++run->Snapshot.Revision;
                return EditorCommandStatus::GeometryProcessingFailed;
            }
            return EditorCommandStatus::Pending;
        }
    }

    const char* ToString(const EditorCoherentPointDriftPhase phase) noexcept
    {
        switch (phase)
        {
        case Phase::Ready: return "ready";
        case Phase::Running: return "running";
        case Phase::Paused: return "paused";
        case Phase::Finished: return "finished";
        case Phase::Failed: return "failed";
        case Phase::Cancelled: return "cancelled";
        case Phase::Applied: return "applied";
        }
        return "unknown";
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
        auto run = std::make_shared<EditorCoherentPointDriftRun>();
        auto resolved = config;
        if (auto error = Capture(EditorProcessingCommandsAccess::Resolve(commands), resolved, true, run.get()))
        {
            failure = std::move(*error);
            return nullptr;
        }
        auto& snapshot = run->Snapshot;
        snapshot.Phase = Phase::Ready;
        snapshot.Result = {.Status = EditorCommandStatus::Pending, .Method = resolved.Method, .Output = resolved.Output,
                           .Backend = std::string(CPD::BackendId(run->Params.EStep)),
                           .SourcePointCount = run->SourceWorld.size(), .TargetPointCount = run->TargetWorld.size(),
                           .Message = "Coherent Point Drift is ready."};
        snapshot.SourcePreview = run->SourceWorld;
        snapshot.Target = run->TargetWorld;
        snapshot.Revision = 1u;
        return run;
    }

    EditorCommandStatus StepEditorCoherentPointDrift(const EditorProcessingCommands& commands,
                                                     const EditorCoherentPointDriftRunHandle& run, std::uint32_t iterations)
    {
        if (!run) return EditorCommandStatus::InvalidProcessingParameters;
        return Submit(EditorProcessingCommandsAccess::Resolve(commands), run, iterations, {});
    }

    void CancelEditorCoherentPointDrift(const EditorCoherentPointDriftRunHandle& run)
    {
        if (!run) return;
        run->CancelRequested = true;
        if (!run->Busy.load())
        {
            std::scoped_lock lock{run->Mutex};
            if (run->Snapshot.Phase == Phase::Ready || run->Snapshot.Phase == Phase::Paused)
            {
                run->Snapshot.Phase = Phase::Cancelled;
                ++run->Snapshot.Revision;
            }
        }
    }

    EditorCoherentPointDriftSnapshot SnapshotEditorCoherentPointDrift(const EditorCoherentPointDriftRunHandle& run)
    {
        if (!run) return {};
        std::scoped_lock lock{run->Mutex};
        return run->Snapshot;
    }

    EditorCoherentPointDriftResult ApplyEditorCoherentPointDrift(const EditorProcessingCommands& commands,
                                                                 const EditorCoherentPointDriftRunHandle& run)
    {
        if (!run) return {.Status = EditorCommandStatus::InvalidProcessingParameters, .Message = "No Coherent Point Drift run."};
        if (run->Busy.load())
            return Failure(run->Config, EditorCommandStatus::Pending, "Wait for the running step to finish, or cancel it.");
        return Publish(EditorProcessingCommandsAccess::Resolve(commands), *run);
    }

    EditorCoherentPointDriftResult ApplyEditorCoherentPointDriftCommand(
        const EditorProcessingCommands& commands, const CoherentPointDriftConfig& config,
        std::function<void(EditorCoherentPointDriftResult)> onComplete)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        EditorCoherentPointDriftResult failure;
        auto run = StartEditorCoherentPointDrift(commands, config, failure);
        if (!run) return failure;
        if (!context.JobCommands.Available())
        {
            EditorCoherentPointDriftResult immediate;
            (void)Submit(context, run, 0u, [&immediate](EditorCoherentPointDriftResult result) { immediate = std::move(result); });
            return immediate;
        }
        auto sink = GuardEditorProcessingResult(context, std::move(onComplete));
        const auto status = Submit(context, run, 0u, [sink](EditorCoherentPointDriftResult result) { if (sink) sink(std::move(result)); });
        auto pending = SnapshotEditorCoherentPointDrift(run).Result;
        pending.Status = status;
        pending.Message = status == EditorCommandStatus::Pending ? "Coherent Point Drift queued (" + pending.Backend + ")."
                                                                 : pending.Message;
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
