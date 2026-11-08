module;
#include "GeometryIntegration/Runtime.GeometryValueComparison.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
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
module Extrinsic.Runtime.PointSamplingOperations;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Error;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Component.Hierarchy;
import Extrinsic.ECS.Component.MetaData;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Core.Dag.Scheduler;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.PointSamplingGpu;
import Extrinsic.Runtime.GpuPropertyBinding;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.StepRunExecution;
import Extrinsic.Runtime.StepRuns;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.TransferQueue;
import Geometry.Graph;
import Geometry.HalfedgeMesh;
import Geometry.PointCloud;
import Geometry.PointSampling;
import Geometry.Properties;
#include "Editor/internal/Runtime.EditorGeometryHelpers.hpp"
#include "Editor/internal/Runtime.EditorProcessingAccess.hpp"
#include "Editor/internal/Runtime.EditorGeneratedEntity.hpp"
#include "Editor/internal/Runtime.EditorTransformHelpers.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.PointFields.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.JobFailure.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.GpuFront.hpp"
#include "Editor/Operations/Runtime.GpuTransactionLifecycle.hpp"
#include "Services/Runtime.WorkerGpuPages.hpp"

namespace Extrinsic::Runtime
{
    namespace
    {
        namespace ECSC = ECS::Components;
        namespace GPD = GeometryProcessingDetail;
        namespace PS = Geometry::PointSampling;
        using EditorFeatureDetail::ResolveStableEntity;
        using EditorFeatureDetail::ToEditorCommandStatus;
        constexpr std::uint64_t kSamplingIdentityHigh = 0x53414d504c494e47ull; // "SAMPLING"

        glm::dmat4 ModelMatrix(const ECSC::Transform::Component* transform)
        {
            return transform == nullptr ? glm::dmat4(1.0) : ECSC::Transform::GetMatrixDouble(*transform);
        }

        struct Captured
        {
            entt::entity Entity{entt::null};
            GPD::PointInputCapture Input{};
            std::vector<glm::vec3> World{};
            std::vector<double> Weights{};
            std::vector<float> Scores{};
            // World frame the samples were taken in (checked before a deferred publication).
            std::optional<ECSC::Transform::Component> Transform{};
        };

        EditorPointSamplingResult Failure(EditorCommandStatus status, std::string message)
        {
            return {.Status = status, .Message = std::move(message)};
        }

        // Shared by readiness (no value copy) and apply (world-space copies).
        std::optional<EditorPointSamplingResult> Capture(const EditorProcessingContext& context,
                                                         PointSamplingOperationConfig& config, bool copyValues,
                                                         Captured* out)
        {
            const auto validation = ValidatePointSamplingOperationConfigSection(
                SerializePointSamplingOperationConfig(config), {}, kPointSamplingConfigSectionName);
            if (!validation.Usable())
                return Failure(EditorCommandStatus::InvalidProcessingParameters, validation.Diagnostics.front().Message);
            if (context.Scene == nullptr) return Failure(EditorCommandStatus::MissingScene, "Scene is unavailable.");
            entt::registry& raw = context.Scene->Raw();
            const auto entity = ResolveStableEntity(raw, config.SourceStableEntityId);
            if (!entity) return Failure(EditorCommandStatus::StaleEntity, "Choose an existing entity.");
            if (const auto* hierarchy = raw.try_get<ECSC::Hierarchy::Component>(*entity);
                hierarchy != nullptr && raw.valid(hierarchy->Parent))
                return Failure(EditorCommandStatus::InvalidProcessingParameters,
                               "Point sampling needs an unparented entity (points are sampled in its world frame).");
            const auto available = BuildGeometryAvailability(raw, *entity);
            if (config.Positions.Domain == GeometryElementDomain::Unknown)
                config.Positions.Domain = GPD::PrimaryPointDomain(available);
            GPD::PointInputCapture input;
            std::string diagnostic;
            if (!GPD::CapturePointInput(available, config.Positions, copyValues, input, diagnostic))
                return Failure(EditorCommandStatus::InvalidProcessingParameters, diagnostic);
            if (input.LiveCount == 0u)
                return Failure(EditorCommandStatus::InvalidProcessingParameters, "The point domain has no live points.");
            const std::array outputs{
                GeometryPropertyRef{config.Positions.Domain, config.RankName, Geometry::PropertyValueKind::Float},
                GeometryPropertyRef{config.Positions.Domain, config.SelectedName, Geometry::PropertyValueKind::Bool}};
            if (!GPD::ValidatePointOutputs(available, config.Positions, outputs, "Point sampling", diagnostic))
                return Failure(EditorCommandStatus::InvalidProcessingParameters, diagnostic);
            const auto* props = ResolveGeometryPropertySet(available, config.Positions.Domain);
            std::optional<std::span<const float>> weights;
            if (!config.WeightsName.empty())
            {
                const auto property = props ? props->Get<float>(config.WeightsName) : decltype(props->Get<float>("")){};
                if (!property || property.Vector().size() != props->Size())
                    return Failure(EditorCommandStatus::InvalidProcessingParameters,
                                   "The weights must be a float property of the points' domain.");
                weights = std::span<const float>(property.Vector());
            }
            if (out == nullptr) return std::nullopt;
            out->Entity = *entity;
            out->Input = std::move(input);
            // Outputs are also watched from admission, so an edit during compute is never overwritten.
            for (const auto& name : {config.RankName, config.SelectedName})
                out->Input.Inputs.push_back(GPD::ObserveGeometryProperty(available, config.Positions.Domain, name));
            // A deferred run publishes only while the weights it read are unchanged, too.
            if (weights) out->Input.Inputs.push_back(GPD::ObserveGeometryProperty(available, config.Positions.Domain,
                                                                                  config.WeightsName));
            if (const auto* transform = raw.try_get<ECSC::Transform::Component>(*entity)) out->Transform = *transform;
            const glm::dmat4 model = ModelMatrix(raw.try_get<ECSC::Transform::Component>(*entity));
            for (const auto& p : out->Input.Points)
                out->World.push_back(glm::vec3(model * glm::dvec4(glm::dvec3(p), 1.0)));
            if (weights)
                for (const std::uint32_t slot : out->Input.Slots)
                {
                    out->Weights.push_back(double((*weights)[slot]));
                    out->Scores.push_back((*weights)[slot]);
                }
            return std::nullopt;
        }

        // One history entry writing (or restoring) the rank and selection properties together.
        EditorCommandHistoryStatus PublishProperties(const EditorProcessingContext& context, const Captured& captured,
                                                     const PointSamplingOperationConfig& config,
                                                     const std::vector<std::uint32_t>& order, std::size_t count)
        {
            if (!GPD::EditorProcessingContextWorldCurrent(context) ||
                !GPD::GeometryPropertiesCurrent(context, captured.Entity, captured.Input.Inputs))
                return EditorCommandHistoryStatus::StaleEntity;
            entt::registry& raw = context.Scene->Raw();
            const GeometryElementDomain domain = config.Positions.Domain;
            const auto* props = GPD::MutableGeometryProperties(raw, captured.Entity, domain);
            if (props == nullptr) return EditorCommandHistoryStatus::StaleEntity;
            struct State
            {
                bool RankExists{}, SelectedExists{};
                std::vector<float> Rank{};
                std::vector<bool> Selected{};
            };
            const auto& read = std::as_const(*props);
            const auto rank = read.Get<float>(config.RankName);
            const auto selected = read.Get<bool>(config.SelectedName);
            auto before = std::make_shared<State>(State{bool(rank), bool(selected), rank ? rank.Vector() : std::vector<float>{},
                                                        selected ? selected.Vector() : std::vector<bool>{}});
            auto after = std::make_shared<State>(State{true, true, std::vector<float>(props->Size(), -1.0f),
                                                       std::vector<bool>(props->Size(), false)});
            for (std::size_t k = 0; k < order.size(); ++k)
            {
                const std::uint32_t slot = captured.Input.Slots[order[k]];
                after->Rank[slot] = float(k);
                if (k < count) after->Selected[slot] = true;
            }
            auto inputs = captured.Input.Inputs;
            std::erase_if(inputs, [&](const auto& input) {
                return input.Domain == domain && (input.Name == config.RankName || input.Name == config.SelectedName);
            });
            const std::array outputRefs{
                GeometryPropertyRef{domain, config.RankName, Geometry::PropertyValueKind::Float},
                GeometryPropertyRef{domain, config.SelectedName, Geometry::PropertyValueKind::Bool}};
            auto outputWatches = GPD::CapturePointPropertyHistory(context, captured.Entity, outputRefs);
            const auto mutate = [context, entity = captured.Entity, inputs = std::move(inputs), domain,
                                 outputWatches=std::move(outputWatches),
                                 rankName = config.RankName, selectedName = config.SelectedName](const State& expected,
                                                                                             const State& target) {
                if (!GPD::EditorProcessingContextWorldCurrent(context) ||
                    !GPD::GeometryPropertiesCurrent(context, entity, inputs) ||
                    !GPD::PointPropertyHistoryCurrent(context, entity, outputWatches))
                    return EditorCommandHistoryStatus::StaleEntity;
                auto& registry = context.Scene->Raw();
                auto* set = GPD::MutableGeometryProperties(registry, entity, domain);
                if (!set) return EditorCommandHistoryStatus::StaleEntity;
                const auto& read = std::as_const(*set);
                const auto rank = read.Get<float>(rankName);
                const auto selected = read.Get<bool>(selectedName);
                // Undo of a newer command restores these values with fresh revisions.
                // Check both expected columns before either write, including absent/wrong-type outputs.
                if (read.Exists(rankName) != expected.RankExists ||
                    read.Exists(selectedName) != expected.SelectedExists ||
                    (expected.RankExists && (!rank || !GeometryValueComparison::BitEqual(rank.Vector(), expected.Rank))) ||
                    (expected.SelectedExists && (!selected || !GeometryValueComparison::BitEqual(selected.Vector(), expected.Selected))))
                    return EditorCommandHistoryStatus::StaleEntity;
                if (target.RankExists) set->GetOrAdd<float>(rankName).Vector() = target.Rank;
                else if (auto property = set->Get<float>(rankName)) set->Remove(property);
                if (target.SelectedExists) set->GetOrAdd<bool>(selectedName).Vector() = target.Selected;
                else if (auto property = set->Get<bool>(selectedName)) set->Remove(property);
                GPD::StampPointPropertyHistory(context, entity, outputWatches);
                ECS::Components::DirtyTags::MarkGpuDirty(registry, entity);
                ECS::Components::DirtyTags::MarkVertexAttributesDirty(registry, entity);
                if (context.InvalidateWorkspaceSnapshotCache) context.InvalidateWorkspaceSnapshotCache();
                return EditorCommandHistoryStatus::Applied;
            };
            return context.CommandHistory
                ? context.CommandHistory->Execute({.Label = "Point sampling",
                                                   .Redo = [mutate, before, after] { return mutate(*before, *after); },
                                                   .Undo = [mutate, before, after] { return mutate(*after, *before); }}).Status
                : mutate(*before, *after);
        }

        // Publishes an order computed by either backend (the source was captured in `captured`).
        EditorPointSamplingResult Publish(const EditorProcessingContext& context, const Captured& captured,
                                          const PointSamplingOperationConfig& config, std::size_t count,
                                          const PS::Result& order, EditorPointSamplingResult result)
        {
            if (!order.Succeeded())
            {
                result.Status = EditorCommandStatus::InvalidProcessingParameters;
                result.Message = "Sampling failed: " + std::string(PS::ToString(order.State)) + ".";
                return result;
            }
            result.DistancePairs = order.DistancePairs;
            result.SampleCount = std::uint32_t(std::min(count, order.Order.size()));
            if (config.Output == PointSamplingOutput::PointCloud)
            {
                Geometry::PointCloud::Cloud cloud;
                cloud.Reserve(result.SampleCount);
                for (std::size_t k = 0; k < result.SampleCount; ++k)
                    static_cast<void>(cloud.AddPoint(captured.World[order.Order[k]]));
                const auto* meta = context.Scene->Raw().try_get<ECSC::MetaData>(captured.Entity);
                const std::string name = (meta ? meta->EntityName : std::string("Points")) + " samples";
                const auto published = GPD::PublishEditorGeneratedEntity(
                    context, {.Source = captured.Entity, .Name = name, .Cloud = std::move(cloud),
                              .IdentityHigh = kSamplingIdentityHigh, .Label = "Point sampling"});
                result.Status = published.Status;
                result.OutputEntityId = published.OutputEntityId;
                result.Message = published.Succeeded()
                    ? "Created " + name + " with " + std::to_string(result.SampleCount) + " points."
                    : published.Message;
                return result;
            }
            result.Status = ToEditorCommandStatus(PublishProperties(context, captured, config, order.Order, count));
            result.Message = result.Succeeded()
                ? "Ranked " + std::to_string(order.Order.size()) + " points; selected " +
                      std::to_string(result.SampleCount) + "."
                : "The source changed before publication.";
            return result;
        }

        // Everything the samples depend on: positions, deletions, weights and the world frame.
        bool CapturedCurrent(const EditorProcessingContext& context, const Captured& captured)
        {
            if (!GPD::EditorProcessingContextWorldCurrent(context) ||
                !GPD::GeometryPropertiesCurrent(context, captured.Entity, captured.Input.Inputs))
                return false;
            auto& raw = context.Scene->Raw();
            if (const auto* hierarchy = raw.try_get<ECSC::Hierarchy::Component>(captured.Entity);
                hierarchy != nullptr && raw.valid(hierarchy->Parent))
                return false;
            const auto* transform = raw.try_get<ECSC::Transform::Component>(captured.Entity);
            return transform == nullptr
                ? !captured.Transform
                : captured.Transform && EditorFeatureDetail::SameTransformComponent(*transform, *captured.Transform);
        }

        // One run's request, shared by both backends' step runs.
        struct Request
        {
            EditorProcessingContext Context{};
            std::shared_ptr<Captured> Source{};
            PointSamplingOperationConfig Config{};
            std::size_t Count{};
            PS::Params Params{}; // its weights and scores view Source's copies
            EditorJobIdentity Identity{};
            EditorPointSamplingResult Base{};
            std::function<void(EditorPointSamplingResult)> Sink{};
        };
        // One selected sample is one step of exact farthest-point sampling; every other method orders whole in one.
        bool SampleSteps(const PS::Params& params)
        {
            return params.Method == PS::Method::FarthestPoint;
        }
        // The run's cap: the count, or one whole-order step.
        StepRunExecution RunExecution(const Request& request, StepRunExecution execution)
        {
            execution.IterationCap = SampleSteps(request.Params) ? std::uint32_t(request.Count) : 1u;
            execution.PreviewEvery = 0u; // no live preview: the order is published on Accept
            execution.PreviewIntervalMs = 0u;
            return execution;
        }

        // ARCH-023: the CPU counterpart as a worker step run: a step is one sample of the exact farthest-point
        // sieve (Geometry FarthestPointSieve::Extend), or the whole order of another method; Commit publishes the
        // samples of the last complete step once through the undoable property (or entity) publication.
        struct CpuSamplingRun
        {
            Request Run{};
            // Written by Step on the worker, read by Commit, Restart and a failed run's Discard once no step runs.
            std::vector<double> X{}, Y{}, Z{};
            std::optional<PS::FarthestPointSieve> Sieve{};
            PS::Result Order{};
            std::string Failure{};
            double Milliseconds{};
            std::atomic<std::uint32_t> Steps{};
            std::uint64_t Id{};
            bool Delivered{};
            void Deliver(EditorPointSamplingResult result)
            {
                if (std::exchange(Delivered, true))
                    return;
                if (auto sink = std::move(Run.Sink))
                    sink(std::move(result));
            }
            // The samples so far as the reference's result.
            [[nodiscard]] PS::Result Current() const
            {
                if (!Sieve)
                    return Order;
                PS::Result result;
                result.Order.assign(Sieve->Order().begin(), Sieve->Order().end());
                result.Clearance.assign(Sieve->Clearance().begin(), Sieve->Clearance().end());
                result.DistancePairs = Sieve->DistancePairs();
                return result;
            }
        };
        StepOutcome StepCpuSampling(CpuSamplingRun& run, StepRunMetric& metric)
        {
            const auto started = std::chrono::steady_clock::now();
            const auto& r = run.Run;
            const auto elapsed = [&]
            {
                run.Milliseconds +=
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
            };
            if (!SampleSteps(r.Params))
            {
                run.Order = PS::Order(std::span<const glm::vec3>(r.Source->World), r.Params, r.Count);
                elapsed();
                run.Steps.store(1u);
                if (!run.Order.Succeeded())
                {
                    run.Failure = "Sampling failed: " + std::string(PS::ToString(run.Order.State)) + ".";
                    return StepOutcome::Failed;
                }
                metric.Values[0] = double(run.Order.Order.size());
                return StepOutcome::Converged;
            }
            if (!run.Sieve)
            {
                // The reference converts the same floats to doubles (Order on glm::vec3).
                for (const auto& p : r.Source->World)
                {
                    run.X.push_back(p.x);
                    run.Y.push_back(p.y);
                    run.Z.push_back(p.z);
                }
                run.Sieve.emplace();
                if (const auto status = run.Sieve->Build(
                        {run.X, run.Y, run.Z}, r.Params.FirstIndex, r.Params.Weights, r.Params.LeafSize);
                    status != PS::Status::Success)
                {
                    run.Failure = "Sampling failed: " + std::string(PS::ToString(status)) + ".";
                    return StepOutcome::Failed;
                }
            }
            const auto next = run.Steps.load() + 1u;
            (void)run.Sieve->Extend(next);
            elapsed();
            run.Steps.store(next);
            metric.Values[0] = run.Sieve->Clearance().empty() ? 0.0 : run.Sieve->Clearance().back();
            return StepOutcome::Continue;
        }
        EditorPointSamplingResult StartCpuSampling(Request request, const StepRunExecution& execution)
        {
            auto run = std::make_shared<CpuSamplingRun>();
            const std::weak_ptr<CpuSamplingRun> weak = run;
            auto base = request.Base;
            run->Run = std::move(request);
            auto runExecution = RunExecution(run->Run, execution);
            StepRunCallbacks callbacks{.Step =
                                           [run](StepRunMetric& metric)
                {
                    return StepCpuSampling(*run, metric);
                },
                .Valid =
                    [weak]
                {
                    const auto r = weak.lock();
                    return r && CapturedCurrent(r->Run.Context, *r->Run.Source);
                },
                .Commit =
                    [weak](std::string& diagnostic)
                {
                    const auto r = weak.lock();
                    if (!r)
                    {
                        diagnostic = "The sampling run ended.";
                        return StepRunCommit::Refused;
                    }
                    const auto& q = r->Run;
                    auto result = q.Base;
                    result.Milliseconds = r->Milliseconds;
                    result.Steps = r->Steps.load();
                    const std::size_t count = SampleSteps(q.Params) ? result.Steps : q.Count;
                    auto published = Publish(q.Context, *q.Source, q.Config, count, r->Current(), std::move(result));
                    const bool committed = published.Succeeded();
                    if (!committed)
                        diagnostic = published.Message;
                    r->Deliver(std::move(published));
                    return committed ? StepRunCommit::Committed : StepRunCommit::Refused;
                },
                .Discard =
                    [weak]
                {
                    const auto r = weak.lock();
                    if (!r || r->Delivered)
                        return;
                    // A failed run's step has completed; a discard may come while a step runs (its state is not read).
                    const auto observed = r->Run.Context.StepRuns->Observe(r->Id);
                    const bool failed = observed && observed->Phase == StepRunPhase::Failed;
                    auto ended = r->Run.Base;
                    ended.Steps = r->Steps.load();
                    ended.Status =
                        failed ? EditorCommandStatus::InvalidProcessingParameters : EditorCommandStatus::StaleEntity;
                    ended.Message = failed
                        ? (r->Failure.empty() ? observed->Diagnostic : r->Failure)
                        : "Point sampling was discarded or its source became stale; nothing was applied.";
                    r->Deliver(std::move(ended));
                },
                .Restart =
                    [weak](std::string&)
                {
                    const auto r = weak.lock();
                    if (!r)
                        return false;
                    r->Sieve.reset();
                    r->Order = {};
                    r->Failure.clear();
                    r->Milliseconds = 0.0;
                    r->Steps.store(0u);
                    return true;
                }};
            std::string refusal;
            const auto& context = run->Run.Context; // the request moved into the run
            const auto id = context.StepRuns->Start(
                {.Label = "Point sampling",
                    .Owner = "Runtime.PointSampling",
                    .Execution = runExecution,
                    .MetricNames = {SampleSteps(run->Run.Params) ? "clearance" : "samples", "", "", ""}},
                std::move(callbacks),
                EditorStepRunJobs(context.JobCommands, run->Run.Identity, std::make_shared<JobToken>()),
                refusal);
            if (!id)
            {
                base.Status = EditorCommandStatus::GeometryProcessingFailed;
                base.Message = GPD::MeshSupport::QueuedJobRejectedMessage("Point sampling") + " " + refusal;
                run->Deliver(base); // an immediate answer reaches the sink too
                return base;
            }
            run->Id = id;
            run->Run.Base.StepRun = id;
            base.StepRun = id;
            base.Status = EditorCommandStatus::Pending;
            base.Message = runExecution.Start == StepRunStart::Ready ? "Point sampling is ready; run or step it."
                                                                     : "Point sampling queued.";
            return base;
        }

        // ARCH-023: exact farthest-point sampling on Vulkan as a worker-stepped GPU transaction without rings: one
        // selected sample is one step. Init (main thread) creates the run's pipeline and buffers; its steps then run on
        // the step run's worker, Run batching the allowed samples into one chunk within the pair budget, each chunk
        // waited for there (no frame paces it). Only the chunk that adds the last sample reads the order back (an
        // Accept after a stop reads the samples so far in an owner page). The device workspace is kept until Accept,
        // Discard or the run's end; Accept checks the leading samples against the CPU reference.
        struct EditorPointSamplingGpuTransaction : std::enable_shared_from_this<EditorPointSamplingGpuTransaction>
        {
            GPD::GpuTransactionCore Core{};
            Request Run{};
            std::shared_ptr<PointSamplingGpuRun> Device{};
            std::shared_ptr<SpatialGpuResult> Chunk{};
            std::atomic<std::uint32_t>
                Done{}; // samples completed (a worker step's; the owner reads them once none runs)
            bool Read{}; // the order of the Done samples is on the CPU
            std::string Failure{};
            EditorPointSamplingResult Result{};
            std::chrono::steady_clock::time_point Started{};
            // The residency slots the chunks read (stamped before they go).
            std::array<RHI::BufferHandle, 2> Inputs{};
            // The worker's own: the workspace (Release drops the run's reference at once) and the last chunk's bytes,
            // which the owner observes once no step runs.
            std::shared_ptr<Graphics::FarthestPointSamplingWorkspace> Workspace{};
            std::vector<std::byte> Final{};
        };
        using GpuSampling = std::shared_ptr<EditorPointSamplingGpuTransaction>;
        bool FailGpuSampling(const GpuSampling& w, std::string why)
        {
            w->Failure = std::move(why);
            return true;
        }
        // Accept after a stop: the samples so far, read back in an owner page (no sample is added).
        void QueueSamplingReadback(const GpuSampling& w)
        {
            w->Read = false;
            w->Chunk = w->Core.Gpu = w->Device->QueueNext(*w->Run.Context.SpatialIndices, 0u, true);
            GPD::GpuTransactionRecording(w->Core, true);
        }
        // The step run's batched Step, on a worker: up to `allowance` samples in one chunk within the pair budget,
        // waited for here; the chunk adding the last sample takes the order and clearances.
        StepOutcome SampleOnWorker(EditorPointSamplingGpuTransaction& w,
            StepRunMetric& metric,
            const std::uint32_t allowance,
            std::uint32_t& completed)
        {
            auto& workspace = *w.Workspace;
            const std::uint32_t added = workspace.NextChunkSamples(allowance);
            if (added == 0u || w.Core.Pages->Aborted())
            {
                if (added == 0u)
                    w.Core.WorkerFailure = "The Vulkan sampler refused its next chunk.";
                return StepOutcome::Failed;
            }
            const bool last = workspace.NextChunkFinishes(allowance);
            w.Final.assign(
                last ? Graphics::FarthestPointSamplingWorkspace::ReadbackBytes(std::uint32_t(w.Run.Count)) : 0u,
                std::byte{0});
            switch (w.Core.Pages->Run(
                [&workspace, allowance](RHI::ICommandContext& commands)
                {
                    return workspace.RecordNext(commands, allowance);
                },
                w.Final))
            {
            case WorkerGpuPageStatus::Complete: break;
            case WorkerGpuPageStatus::Aborted:  return StepOutcome::Failed; // given up: the owner ended the run
            case WorkerGpuPageStatus::Refused:
                w.Core.WorkerFailure = "GPU sampling submission refused.";
                return StepOutcome::Failed;
            case WorkerGpuPageStatus::Failed:
            case WorkerGpuPageStatus::TimedOut:
                w.Core.WorkerFailure = "GPU sampling did not complete: the device was lost.";
                return StepOutcome::Failed;
            }
            completed = added;
            const std::uint32_t done = w.Done.load() + added;
            w.Done.store(done);
            double clearance = 0.0;
            if (last && done > 0u) // the clearances lead the readback, the newest last
                std::memcpy(&clearance, w.Final.data() + std::size_t(done - 1u) * sizeof(double), sizeof(double));
            metric.Values[0] = clearance;
            return StepOutcome::Continue;
        }
        // The owner's side of the Run job's readiness (after the Init): once no step runs, the last chunk's order is
        // observed, and at the step run's Commit the samples so far are read back if they are not yet.
        bool PollGpuSampling(const GpuSampling& w)
        {
            auto& t = w->Core;
            if (!w->Failure.empty())
                return true;
            if (GPD::GpuTransactionWorkerBusy(t))
                return false;
            if (!w->Final.empty())
            {
                (void)w->Device->Observe(std::span<const std::byte>(w->Final));
                w->Read = true;
                w->Final.clear();
            }
            if (w->Chunk)
            {
                if (w->Chunk->State == SpatialQueryState::Failed)
                {
                    GPD::GpuTransactionRecording(t, false);
                    return FailGpuSampling(
                        w, w->Chunk->Diagnostic.empty() ? "GPU sampling submission refused." : w->Chunk->Diagnostic);
                }
                if (w->Chunk->State != SpatialQueryState::Ready)
                    return false;
                const auto chunk = std::exchange(w->Chunk, {});
                (void)w->Device->Observe(*chunk);
                w->Read = !chunk->Data.empty();
                GPD::GpuTransactionRecording(t, false);
            }
            if (!t.CommitWanted)
                return false;
            if (w->Done.load() == 0u || w->Read)
                return true;
            QueueSamplingReadback(w);
            return w->Chunk == nullptr && FailGpuSampling(w, "GPU sampling submission refused.");
        }
        void CompleteGpuSamplingRun(const GpuSampling& w)
        {
            if (!w->Failure.empty())
            {
                GPD::FailGpuTransaction(w->Core, w->Failure);
                return;
            }
            if (w->Done.load() == 0u)
            {
                GPD::FinishGpuTransaction(w->Core,
                    EditorGpuTransactionPhase::Discarded,
                    EditorCommandStatus::StaleEntity,
                    "Vulkan point sampling selected no sample; nothing was applied.");
                return;
            }
            w->Device->Release();
            GPD::ReadyGpuTransaction(w->Core);
        }
        // Accept: the device order (its leading samples checked against the CPU) or, when they differ, the CPU's.
        void CompleteGpuSamplingAccept(const GpuSampling& w)
        {
            const auto& q = w->Run;
            auto final = w->Result;
            final.Steps = w->Done.load();
            const std::size_t count = final.Steps;
            std::string why;
            PS::Result order;
            if (w->Device->Current().Order.size() != count)
                why = "The Vulkan sampler returned an incomplete order; sampling ran on the CPU.";
            else if (!w->Device->VerifyPrefix(why))
            {
            }
            if (why.empty())
            {
                order = w->Device->Current();
                final.Backend = std::string(kPointSamplingGpuBackendId);
            }
            else
            {
                final.BackendDiagnostic = std::move(why);
                order = PS::Order(std::span<const glm::vec3>(q.Source->World), q.Params, count);
            }
            final.Milliseconds =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - w->Started).count();
            final.CpuStageReadbackBytes +=
                Graphics::FarthestPointSamplingWorkspace::ReadbackBytes(std::uint32_t(q.Count));
            w->Result = Publish(q.Context, *q.Source, q.Config, count, order, std::move(final));
            GPD::FinishGpuTransaction(w->Core,
                w->Result.Succeeded() ? EditorGpuTransactionPhase::Applied : EditorGpuTransactionPhase::Failed,
                w->Result.Status,
                w->Result.Message);
        }
        EditorPointSamplingResult StartGpuSampling(
            Request request, const StepRunExecution& execution, const Graphics::FarthestPointGpuInput& input)
        {
            const auto& context = request.Context;
            auto w = std::make_shared<EditorPointSamplingGpuTransaction>();
            auto* raw = w.get();
            const auto self = [raw]
            {
                return raw->shared_from_this();
            };
            auto& t = w->Core;
            t.Context = context;
            t.Residency = context.SpatialIndices->PropertyResidency();
            t.Identity = request.Identity;
            t.Label = "Vulkan point sampling";
            t.JobLabel = "Point sampling";
            t.AcceptJobName = "Vulkan point sampling accept";
            t.Pages = std::make_shared<WorkerGpuPages>(*context.Device); // its samples are chosen on a worker
            t.Kind = GPD::GpuTransactionKind::Iterative;
            t.Execution = RunExecution(request, execution);
            w->Result = request.Base;
            w->Started = std::chrono::steady_clock::now();
            w->Device = std::make_shared<PointSamplingGpuRun>(
                *context.SpatialIndices, input, request.Source->World, request.Params);
            w->Inputs = {input.Positions.Buffer, input.Weights.Buffer};
            w->Run = std::move(request);
            t.Hooks = {.Current =
                           [raw]
                {
                    return CapturedCurrent(raw->Run.Context, *raw->Run.Source);
                },
                .Poll =
                    [self]
                {
                    return PollGpuSampling(self());
                },
                .CompleteRun =
                    [self]
                {
                    CompleteGpuSamplingRun(self());
                },
                .CompleteAccept =
                    [self]
                {
                    CompleteGpuSamplingAccept(self());
                },
                // run_control restart, once no step runs: the run from its first sample again with the workspace kept
                // (its capacity too); the Init prepares it again before the next step.
                .Restart =
                    [self](std::string& diagnostic)
                {
                    const auto w = self();
                    if (!w->Device->Restart(*w->Run.Context.SpatialIndices))
                    {
                        diagnostic = "The Vulkan sampler refused its input.";
                        return false;
                    }
                    w->Done.store(0u);
                    w->Read = false;
                    w->Final.clear();
                    w->Started = std::chrono::steady_clock::now();
                    return true;
                },
                // The Init: the run's pipeline and buffers on this thread, then the worker's own workspace reference.
                .Prepare =
                    [raw](std::string& diagnostic)
                {
                    if (!raw->Device->Prepare())
                    {
                        diagnostic = "The Vulkan sampler refused its resident input; nothing was changed.";
                        return GPD::GpuTransactionPrepare::Failed;
                    }
                    raw->Workspace = raw->Device->Workspace();
                    return GPD::GpuTransactionPrepare::Ready;
                },
                .Step =
                    [raw](StepRunMetric& metric)
                {
                    std::uint32_t completed = 0u;
                    return SampleOnWorker(*raw, metric, 1u, completed);
                },
                .StepBatch =
                    [raw](StepRunMetric& metric, const std::uint32_t allowance, std::uint32_t& completed)
                {
                    return SampleOnWorker(*raw, metric, allowance, completed);
                },
                .Uses =
                    [raw](std::vector<RHI::BufferHandle>& buffers)
                {
                    buffers.insert(buffers.end(), raw->Inputs.begin(), raw->Inputs.end());
                },
                .Release =
                    [raw]
                {
                    raw->Device->Release();
                    raw->Workspace.reset(); // the worker's reference: no step runs any more (the core waits for that)
                },
                .Deliver =
                    [raw](const EditorCommandStatus status, std::string message)
                {
                    auto& result = raw->Result;
                    if (status != EditorCommandStatus::Applied || result.Status != EditorCommandStatus::Applied)
                    {
                        result.Status = status;
                        result.Message = std::move(message);
                    }
                    result.StepRun = raw->Core.StepRun;
                    result.Steps = raw->Done.load();
                    if (auto sink = std::move(raw->Run.Sink))
                        sink(result);
                }};
            auto answer = w->Result;
            // An immediate answer reaches the sink too (once); nothing was started.
            const auto refuse = [&](const EditorCommandStatus status, std::string message)
            {
                answer.Status = status;
                answer.Message = std::move(message);
                if (auto sink = std::move(w->Run.Sink))
                    sink(answer);
                return answer;
            };
            if (w->Device->NextChunkSamples(1u) == 0u)
                return refuse(EditorCommandStatus::GeometryProcessingFailed,
                    "The Vulkan sampler refused its resident input; nothing was changed.");
            if (auto refused = GPD::GpuTransactionStartRefusal(t))
                return refuse(refused->Status, std::move(refused->Message));
            if (!GPD::SubmitGpuTransactionRun(GPD::GpuTransactionOf(w), "Vulkan point sampling").IsValid())
                return refuse(EditorCommandStatus::GeometryProcessingFailed,
                    GPD::MeshSupport::QueuedJobRejectedMessage(t.JobLabel));
            w->Result.StepRun = t.StepRun;
            answer = w->Result;
            answer.Status = EditorCommandStatus::Pending;
            answer.Message = t.Execution.Start == StepRunStart::Ready
                ? "Vulkan point sampling is ready; run or step it."
                : "Vulkan point sampling queued.";
            return answer;
        }

        // Both entry points: the batch command (`execution` at once, accepted at its end) and the interactive
        // start. Immediate answers reach `onComplete` too.
        EditorPointSamplingResult Apply(const EditorProcessingContext& context,
            PointSamplingOperationConfig config,
            std::function<void(EditorPointSamplingResult)> onComplete,
            const StepRunExecution& execution,
            const bool interactive)
        {
            const auto finish = [&onComplete](EditorPointSamplingResult result) {
                if (onComplete) onComplete(result);
                return result;
            };
            auto captured = std::make_shared<Captured>();
            if (auto failure = Capture(context, config, true, captured.get())) return finish(*failure);
            EditorPointSamplingResult result;
            result.Method = std::string(DisplayName(config.Sampling.Method));
            result.InputCount = std::uint32_t(captured->World.size());
            const std::size_t count = config.Count == 0u ? captured->World.size()
                                                         : std::min<std::size_t>(config.Count, captured->World.size());
            const auto params = ToPointSamplingParams(config.Sampling, captured->Weights, captured->Scores);
            const bool runs = context.StepRuns && context.JobCommands.Available();
            if (config.Backend == PointSamplingBackend::Vulkan)
            {
                result.RequestedBackend = std::string(kPointSamplingGpuBackendId);
                result.BackendDiagnostic = !context.JobCommands.Available() || context.SpatialIndices == nullptr
                    ? "No job lane or spatial compute service; sampling ran on the CPU."
                    : !context.StepRuns
                    ? "No step-run service for the Vulkan run's controls; sampling ran on the CPU."
                    : PointSamplingGpuUnsupportedReason(params, captured->World.size(), count, context.Device);
            }
            // The run's jobs are named by the rank output it writes on the source.
            const EditorJobIdentity identity{.EntityId = config.SourceStableEntityId,
                                             .Scope = ToEditorJobScope(config.Positions.Domain),
                                             .OutputSemantic = GeometryPresentationSlotSemantic::ScalarField,
                                             .OutputName = config.RankName,
                                             .RequestedDomain = EditorJobDomainOfBackend(result.RequestedBackend)};
            // Property output: any run (CPU or Vulkan) would overwrite the rank and selection an active
            // run publishes, so a duplicate is refused like every queued editor job (Pending,
            // the active run keeps its callback). A point-cloud output creates a new entity per run.
            if (config.Output != PointSamplingOutput::PointCloud && context.JobCommands.Available())
                if (auto busy = GPD::MeshSupport::ActiveOutputJobRefusal(context, identity, "Point sampling"))
                {
                    result.Status = EditorCommandStatus::Pending;
                    result.Message = std::move(busy->Message);
                    return result;
                }
            Request request{.Context = context,
                .Source = captured,
                .Config = config,
                .Count = count,
                .Params = params,
                .Identity = identity,
                .Base = result};
            if (config.Backend == PointSamplingBackend::Cpu || !result.BackendDiagnostic.empty())
            {
                if (runs)
                {
                    request.Sink = std::move(onComplete);
                    return StartCpuSampling(std::move(request), execution);
                }
                if (interactive)
                    return finish(Failure(EditorCommandStatus::InvalidProcessingParameters,
                        "Interactive sampling needs the session's step runs and job lane."));
                // Without run controls the CPU orders inline and publishes at once.
                const auto start = std::chrono::steady_clock::now();
                const PS::Result order = PS::Order(std::span<const glm::vec3>(captured->World), params, count);
                result.Milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                result.Steps = SampleSteps(params) ? std::uint32_t(order.Order.size()) : 1u;
                return finish(Publish(context, *captured, config, count, order, std::move(result)));
            }

            // Vulkan: the resident input, then the owner-stepped transaction.
            auto* residency = context.SpatialIndices->PropertyResidency();
            if (!residency) return finish(Failure(EditorCommandStatus::GeometryProcessingFailed, "GPU property residency unavailable."));
            const auto before = residency->Stats();
            const auto positions = ResolveGpuPropertyInput(*residency, *context.Scene, context.World, captured->Entity, config.Positions);
            std::optional<Graphics::GpuPropertyView> weights;
            if (!config.WeightsName.empty())
                weights = ResolveGpuPropertyInput(*residency, *context.Scene, context.World, captured->Entity,
                    {.Domain = config.Positions.Domain, .Name = config.WeightsName, .ValueKind = Geometry::PropertyValueKind::Float});
            result.GpuInputUploadBytes = residency->Stats().UploadBytes - before.UploadBytes;
            result.GpuInputCacheHits = residency->Stats().Hits - before.Hits;
            if (!positions || (!config.WeightsName.empty() && !weights))
            {
                result.Status = EditorCommandStatus::GeometryProcessingFailed;
                result.Message = "Resident sampling input acquisition refused; nothing was changed.";
                return finish(result);
            }
            Graphics::FarthestPointGpuInput input{.Positions = *positions, .Weights = weights.value_or(Graphics::GpuPropertyView{}),
                .Rows = captured->Input.Slots, .FirstIndex = params.FirstIndex, .Count = std::uint32_t(count)};
            // Every row live: the shader's identity path needs no row map upload.
            if (captured->Input.Slots.size() == positions->Layout.Count) input.Rows = {};
            const auto model = ModelMatrix(captured->Transform ? &*captured->Transform : nullptr);
            for (std::size_t column = 0; column < 4; ++column)
                for (std::size_t row = 0; row < 4; ++row) input.Model[4 * column + row] = model[column][row];
            request.Base = result;
            request.Sink = std::move(onComplete);
            return StartGpuSampling(std::move(request), execution, input);
        }
    }

    ActionReadiness PreviewEditorPointSamplingCommand(const EditorProcessingCommands& commands,
                                                      const PointSamplingOperationConfig& config)
    {
        auto resolved = config;
        const auto failure = Capture(EditorProcessingCommandsAccess::Resolve(commands), resolved, false, nullptr);
        return {!failure.has_value(), failure ? failure->Message : std::string{}};
    }

    EditorPointSamplingResult ApplyEditorPointSamplingCommand(const EditorProcessingCommands& commands,
                                                              const PointSamplingOperationConfig& config,
                                                              std::function<void(EditorPointSamplingResult)> onComplete)
    {
        // Batch and agent callers: at once, accepted at the run's end.
        auto execution = config.Execution;
        execution.Start = StepRunStart::Run;
        execution.AutoAccept = StepRunAutoAccept::End;
        return Apply(
            EditorProcessingCommandsAccess::Resolve(commands), config, std::move(onComplete), execution, false);
    }

    EditorPointSamplingResult StartEditorPointSampling(const EditorProcessingCommands& commands,
        const PointSamplingOperationConfig& config,
        std::function<void(EditorPointSamplingResult)> onComplete)
    {
        return Apply(
            EditorProcessingCommandsAccess::Resolve(commands), config, std::move(onComplete), config.Execution, true);
    }

    RuntimeEngineConfigApplyResult ApplyEditorPointSamplingConfig(const EditorProcessingCommands& commands,
                                                                 const PointSamplingOperationConfig& config,
                                                                 std::string sourceId)
    {
        return ApplyEditorProcessingConfig(commands,
            ValidatePointSamplingOperationConfigSection(SerializePointSamplingOperationConfig(config), {},
                                                        kPointSamplingConfigSectionName),
            sourceId.empty() ? std::string{kPointSamplingConfigSectionName} : sourceId,
            [&](Core::Config::EngineConfig& candidate) { SetPointSamplingOperationConfig(candidate, config); });
    }

    std::optional<PointSamplingOperationConfig> GetEditorPointSamplingConfig(const EditorProcessingCommands& commands)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        if (!context.EngineConfigControlState) return std::nullopt;
        return GetPointSamplingOperationConfig(context.EngineConfigControlState->ActiveConfig);
    }
}
