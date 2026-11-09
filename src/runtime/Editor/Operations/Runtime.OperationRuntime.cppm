// The operation runtime (ADR 0031, RUNTIME-347): one driver runs a declared operation through the
// stages 1 resolve, 2-3 readiness and capture, 4 acquire, 5 execute, 6 gate, 7 publish, 8 deliver.
// One shape remains on its base (OperationShape): point columns a definition publishes itself (outlier
// analysis, RUNTIME-354); the point scalar shape went with its last user (density weights, ARCH-022 Q90). A definition declares names,
// parameters, its outputs and a pure Compute; optional members override the runtime's defaults.
// RUNTIME-349: an exported module for operation modules (ADR 0031 Decision 7). The editor helpers
// the templates call are included in its purview as global-module declarations, reachable where an
// operation module instantiates the runtime; those a custom definition calls itself are exported by
// name in OperationSupport (RUNTIME-354), and no others.
module;
#include <atomic>
#include <algorithm>
#include <array>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>
#include <glm/vec3.hpp>
#include <entt/entity/registry.hpp>
export module Extrinsic.Runtime.OperationRuntime;
export import Extrinsic.Runtime.EditorProcessing;
export import Extrinsic.Runtime.PointScalarTransaction;
export import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Core.Error;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.GpuPropertyResidency;
import Extrinsic.Graphics.PointScalarAnalysis;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.StepRunExecution;
import Extrinsic.Runtime.StepRuns; // the worker-step hooks of Runtime.GpuTransactionLifecycle.hpp
import Extrinsic.RHI.Handles;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.GpuPropertyBinding;
import Geometry.Properties;
#include "Editor/internal/Runtime.EditorProcessingAccess.hpp"
#include "Editor/internal/Runtime.EditorGeometryHelpers.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.PointFields.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.JobFailure.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.OneShotCpuRun.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.RadiusRows.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.GpuScalar.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.GpuFront.hpp"
#include "Editor/Operations/Runtime.GpuTransactionLifecycle.hpp"
export extern "C++"
{
    namespace Extrinsic::Runtime::GeometryProcessingDetail
    {
    // The texts a definition may adjust (Def::Wording(defaults) returns them changed); by default they
    // are built from its names: "<Noun> estimation" labels the job, "<Method> requires at least two
    // live samples." refuses a small input.
        struct OperationWording
        {
            std::string StaleTarget, OutputLabel, TooFewSamples, LbvhLimits, IndexMismatch, ConfigUnavailable, JobLabel,
                JobName, GpuHistory;
        };

        inline std::string JoinOperationText(std::initializer_list<std::string_view> parts)
        {
            std::size_t size = 0;
            for (const auto part : parts)
                size += part.size();
            std::string text;
            text.reserve(size);
            for (const auto part : parts)
                text += part;
            return text;
        }

    // What every shape reads from a definition: its optional members, or the runtime's defaults.
        template <class Def> struct DefinitionTraits
        {
            using Backend = typename Def::Backend;
            static constexpr Backend Reference = []
            {
                if constexpr (requires { Def::ReferenceBackend; })
                    return Def::ReferenceBackend;
                else
                    return Backend::CpuOctree;
            }();
            static constexpr std::uint32_t MinimumLiveCount = []
            {
                if constexpr (requires { Def::MinimumLiveCount; })
                    return std::uint32_t(Def::MinimumLiveCount);
                else
                    return 2u;
            }();
            static const OperationWording& Words()
            {
                static const OperationWording words = []
                {
                    const auto job = JoinOperationText({Def::Noun, " estimation"});
                    OperationWording w{
                        .StaleTarget = JoinOperationText({Def::Noun, " target entity is stale or missing."}),
                        .OutputLabel = std::string(Def::Noun),
                        .TooFewSamples = {},
                        .LbvhLimits =
                            "LBVH requires the spatial cache, at most 2^24 samples and coordinates within 1e18.",
                        .IndexMismatch =
                            JoinOperationText({Def::Noun, " index snapshot does not match the selected samples."}),
                        .ConfigUnavailable = JoinOperationText({Def::Noun, " estimation config is unavailable."}),
                        .JobLabel = job,
                        .JobName = job,
                        .GpuHistory = {}};
                    if constexpr (requires { Def::Method; })
                        w.TooFewSamples = JoinOperationText({Def::Method, " requires at least two live samples."});
                    if constexpr (requires { Def::History; })
                        w.GpuHistory = std::string(Def::History);
                    if constexpr (requires { Def::Wording(w); })
                        w = Def::Wording(std::move(w));
                    return w;
                }();
                return words;
            }
        };

    // Stage 8's slot (RUNTIME-348): the session's store, written before any sink runs and only while
    // the attachment is active. A duplicate refusal never writes it (it answers for another run).
        inline std::uint64_t BeginOperationRun(const EditorProcessingContext& context)
        {
            return context.OperationResults ? context.OperationResults->BeginRun() : 0u;
        }
        template <class Op>
        typename Op::Result KeepOperationResult(
            const EditorProcessingContext& context, std::uint64_t invocation, typename Op::Result result)
        {
            RetainOperationResult<Op>(context.OperationResults, context.AttachmentActive, invocation, result);
            return result;
        }

        enum class CapturePurpose
        {
            Execute,
            Readiness
        };

    // What both shapes share (RUNTIME-354), over the shape's Capture, Execute, Current, Publication and
    // Apply: readiness, configured Apply, the store's retention, a stored refusal, stage 7's frame, and
    // stages 4-8 of a CPU run.
        template <class Shape, class Def> struct OperationShape : DefinitionTraits<Def>
        {
            using Traits = DefinitionTraits<Def>;
            using Op = typename Def::Operation;
            using Params = typename Def::Params;
            using Result = typename Def::Result;

        // Stages 1-2 only: ready, refused, or pending (the input kind's deferred verdict).
            static ActionReadiness Preview(const EditorProcessingCommands& commands, const Params& config)
            {
                const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
                std::string diagnostic;
                const auto work = Shape::Capture(context, config, diagnostic, CapturePurpose::Readiness);
                return {bool(work), std::move(diagnostic)};
            }

        // Configured Apply: the active config's run, or a stored refusal without one.
            static Result ApplyConfigured(const EditorProcessingCommands& commands,
                const std::optional<Params>& config,
                std::function<void(Result)> onComplete)
            {
                if (config)
                    return Shape::Apply(commands, *config, std::move(onComplete));
                const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
                return KeepOperationResult<Op>(context,
                    BeginOperationRun(context),
                    {.Status = EditorCommandStatus::InvalidProcessingParameters,
                        .Message = Traits::Words().ConfigUnavailable});
            }

        // The store's retention of a run's later result (a queued or GPU run), before its sink.
            static std::function<void(Result)> Retainer(
                const EditorProcessingContext& context, std::uint64_t invocation)
            {
                return [store = context.OperationResults, active = context.AttachmentActive, invocation](Result r)
                {
                    RetainOperationResult<Op>(store, active, invocation, r);
                };
            }

        // A refusal: the captured run's result (or the request's) with the reason, stored.
            template <class Work>
            static Result Report(const EditorProcessingContext& context,
                std::uint64_t invocation,
                const std::shared_ptr<Work>& w,
                const Params& config,
                EditorCommandStatus status,
                std::string message)
            {
                auto result = w ? w->Result : Def::InitialResult(config);
                result.Status = status;
                result.Message = std::move(message);
                return KeepOperationResult<Op>(context, invocation, std::move(result));
            }

            static std::string StalePublication()
            {
                return JoinOperationText({Def::Noun, " input or output changed before publication."});
            }

            // Stage 7, behind its own currentness check (an abandoned run never reaches it in the job service).
            template <class Work>
            static Result Publish(const EditorProcessingContext& context, const std::shared_ptr<Work>& w)
            {
                auto& r = w->Result;
                if (w->Abandoned || !Shape::Current(context, *w))
                {
                    r.Status = EditorCommandStatus::StaleEntity;
                    r.Message = StalePublication();
                    return r;
                }
                if (r.Status != EditorCommandStatus::Applied)
                    return r;
                return Shape::Publication(context, *w);
            }

            // Stages 4-8 on the CPU for a captured run: the shared index of a non-reference backend; then,
            // without a job lane, execution and publication inline (stored and returned, not given to the
            // sink); otherwise one queued run per output (a duplicate answers
            // Pending for the active run and is not stored): its Pending answer is stored, stage 6 refuses a
            // cancelled, abandoned or stale run, and the terminal result is stored before the sink receives it.
            // With the session's step runs the run is a one-shot step run with `execution`; without them only a batch
            // (computing at once, accepted at its end) runs, as one queued job.
            template <class Work>
            static Result RunOnCpu(const EditorProcessingContext& context,
                std::uint64_t invocation,
                const std::shared_ptr<Work>& w,
                const GeometryPropertyRef& output,
                std::function<void(Result)> onComplete,
                const StepRunExecution& execution)
            {
                if (w->Config.Backend != Traits::Reference)
                {
                    std::string diagnostic;
                    const auto state = AcquirePointIndex(*context.SpatialIndices,
                        context.World,
                        w->Entity,
                        w->Config.Positions,
                        w->Slots,
                        w->Points,
                        w->GpuIndex,
                        w->Index,
                        w->Result.IndexReused,
                        diagnostic);
                    if (state == PointIndexState::Unavailable)
                        return Report(context,
                            invocation,
                            w,
                            w->Config,
                            EditorCommandStatus::InvalidProcessingParameters,
                            std::move(diagnostic));
                    if (state == PointIndexState::Mismatched)
                        return Report(context,
                            invocation,
                            w,
                            w->Config,
                            EditorCommandStatus::StaleEntity,
                            Traits::Words().IndexMismatch);
                }
                const std::string& label = Traits::Words().JobLabel;
                const bool batch =
                    execution.Start == StepRunStart::Run && execution.AutoAccept != StepRunAutoAccept::Never;
                if (!batch && (!context.StepRuns || !context.JobCommands.Available()))
                    return Report(context,
                        invocation,
                        w,
                        w->Config,
                        EditorCommandStatus::InvalidProcessingParameters,
                        label + " needs the step-run service and a job lane for its run controls; none is composed.");
                if (!context.JobCommands.Available())
                {
                    Shape::Execute(*w);
                    return KeepOperationResult<Op>(context, invocation, Publish(context, w));
                }
                const EditorJobIdentity identity{.EntityId = w->Config.StableEntityId,
                    .Scope = ToEditorJobScope(output.Domain),
                    .OutputSemantic = GeometryPresentationSlotSemantic::ScalarField,
                    .OutputName = output.Name,
                    .RequestedDomain = EditorJobDomainOfBackend(ToString(w->Config.Backend))};
                if (auto busy = MeshSupport::ActiveOutputJobRefusal(context, identity, label))
                {
                    auto result = w->Result;
                    result.Status = EditorCommandStatus::Pending;
                    result.Message = std::move(busy->Message);
                    return result;
                }
                auto queued = w->Result;
                queued.Message = label + " queued.";
                const MeshSupport::QueuedJobDelivery<Result> delivery{context,
                    [retain = Retainer(context, invocation), sink = std::move(onComplete)](Result result)
                    {
                        retain(result);
                        if (sink)
                            sink(std::move(result));
                    },
                    std::move(queued),
                    label};
                // ARCH-023: stages 5-7 as a one-shot worker step run (OneShotCpuRun.hpp).
                if (context.StepRuns)
                    return KeepOperationResult<Op>(context,
                        invocation,
                        MeshSupport::StartOneShotCpuRun<Work, Result>(
                            context,
                            {.Label = label,
                                .Owner = std::string{Def::Noun},
                                .Identity = identity,
                                .Execution = execution},
                            w,
                            [](Work& work)
                            {
                                Shape::Execute(work);
                            },
                            [context](const Work& work)
                            {
                                return Shape::Current(context, work);
                            },
                            [context](const std::shared_ptr<Work>& work)
                            {
                                return Publish(context, work);
                            },
                            delivery));
                JobDesc desc{.DebugName = Traits::Words().JobName,
                    .Scope = context.World,
                    .Work = [w](const JobCancellation&) -> JobResultEnvelope
                    {
                        Shape::Execute(*w);
                        return JobResultEnvelope::Make(true);
                    },
                    // Stage 6: a cancelled, abandoned or stale run publishes nothing.
                    .ValidateBeforeApply =
                        [context, w]
                    {
                        return MeshSupport::ValidateQueuedJob(w->Abandoned, Shape::Current(context, *w));
                    },
                    .PublishCompletion =
                        [context, w, delivery](KernelEventBus&, const JobResultEnvelope&)
                    {
                        return delivery.Publish(Publish(context, w));
                    },
                    .FinalizeUnpublishedOnMainThread =
                        [w, delivery]
                    {
                        w->Abandoned = true;
                        delivery.FinalizeAfterWorker(w->Result);
                    }};
                if (!context.JobCommands.Submit(std::move(desc), identity).IsValid())
                {
                    w->Abandoned = true;
                    return KeepOperationResult<Op>(context, invocation, delivery.Rejected());
                }
                return KeepOperationResult<Op>(context, invocation, delivery.Pending());
            }
        };

        // A point columns run (RUNTIME-354): the capture, the watches and before-state of its N outputs,
        // the values its definition computes, the shared index.
        template <class P, class R, class C, std::size_t N> struct PointColumnsWork : PointInputCapture
        {
            P Config{};
            entt::entity Entity{};
            std::array<PointPropertyWatch, N> OutputWatches{};
            std::array<GeometryScalarPropertySnapshot, N> Before{};
            C Values{};
            std::shared_ptr<const SpatialIndexSnapshot> Index{};
            SpatialIndexHandle GpuIndex{};
            bool Abandoned{};
            R Result{};
        };

        // A definition that publishes its own columns (ADR 0031 Decision 5, custom): Work names its
        // PointColumnsWork; Outputs lists pointers to its output references (domains resolved in place, the
        // first names the job); Prepare adds its checks to the shared capture, before the before-state;
        // Compute is stage 5 on the run; Publish writes stage 7's history entry after the runtime's check;
        // Immediate selects a synchronous mode published by PublishImmediate (no index, no compute);
        // StartGpu and MakeTestGpu run its own transaction, given the store's retention.
        template <class Def>
        concept PointColumnsDefinition = requires(typename Def::Params& editable,
            const typename Def::Params& params,
            typename Def::Work& work,
            const EditorProcessingContext& context) {
            typename Def::Operation;
            requires std::same_as<typename Def::Operation::Result, typename Def::Result>;
            requires std::derived_from<typename Def::Work, PointInputCapture>;
            { Def::Noun } -> std::convertible_to<std::string_view>;
            Def::Outputs(editable);
            { Def::Validate(params) } -> std::same_as<Core::Config::EngineConfigSectionValidationResult>;
            { Def::InitialResult(params) } -> std::same_as<typename Def::Result>;
            { Def::Immediate(params) } -> std::same_as<bool>;
            Def::Compute(work);
            { Def::Publish(context, work) } -> std::same_as<typename Def::Result>;
        };

        template <PointColumnsDefinition Def> struct PointColumnsRuntime : OperationShape<PointColumnsRuntime<Def>, Def>
        {
            using Base = OperationShape<PointColumnsRuntime<Def>, Def>;
            using Params = typename Def::Params;
            using Result = typename Def::Result;
            using Backend = typename Def::Backend;
            using Work = typename Def::Work;
            using Op = typename Def::Operation;
            using Base::MinimumLiveCount;
            using Base::Reference;
            using Base::Words;
            static constexpr std::size_t N = std::tuple_size_v<decltype(Def::Outputs(std::declval<Params&>()))>;

            // Stages 1-3: section, scene, entity, the point input (copied for execution, prepared for
            // readiness), the outputs' domains and checks, their watches, the live count, the definition's
            // checks, and for an execution the outputs' before-state.
            static std::shared_ptr<Work> Capture(const EditorProcessingContext& context,
                Params c,
                std::string& diagnostic,
                CapturePurpose purpose = CapturePurpose::Execute)
            {
                auto fail = [&](std::string why) -> std::shared_ptr<Work>
                {
                    diagnostic = std::move(why);
                    return {};
                };
                const auto validation = Def::Validate(c);
                if (!validation.Usable())
                    return fail(validation.Diagnostics.front().Message);
                if (!context.Scene)
                    return fail("Scene is unavailable.");
                const auto entity = EditorFeatureDetail::ResolveStableEntity(context.Scene->Raw(), c.StableEntityId);
                if (!entity)
                    return fail(Words().StaleTarget);
                const auto a = BuildGeometryAvailability(context.Scene->Raw(), *entity);
                auto w = std::make_shared<Work>();
                const bool execute = purpose == CapturePurpose::Execute;
                if (!(execute ? CapturePointInput(a, c.Positions, true, *w, diagnostic)
                              : PreparePointInput(context, *entity, a, c.Positions, *w, diagnostic)))
                    return {};
                const auto outputs = Def::Outputs(c);
                std::array<GeometryPropertyRef, N> refs{};
                for (std::size_t i = 0; i < N; ++i)
                {
                    if (outputs[i]->Domain == GeometryElementDomain::Unknown)
                        outputs[i]->Domain = c.Positions.Domain;
                    refs[i] = *outputs[i];
                }
                if (!ValidatePointOutputs(a, c.Positions, refs, Words().OutputLabel, diagnostic))
                    return {};
                w->Config = c;
                w->Entity = *entity;
                w->Result = Def::InitialResult(c);
                w->Result.SlotCount = w->SlotCount;
                w->Result.LiveCount = w->LiveCount;
                for (std::size_t i = 0; i < N; ++i)
                    w->OutputWatches[i] = ObserveGeometryProperty(a, refs[i].Domain, refs[i].Name);
                if (w->Result.LiveCount < MinimumLiveCount)
                    return fail(Words().TooFewSamples);
                if (!Def::Prepare(context, a, *w, diagnostic))
                    return {};
                if (execute && !Def::Immediate(c))
                {
                    const auto* props = ResolveGeometryPropertySet(a, c.Positions.Domain);
                    for (std::size_t i = 0; i < N; ++i)
                        w->Before[i] = CaptureGeometryScalarProperty(*props, refs[i]);
                    w->Values.Resize(props->Size());
                }
                return w;
            }

            static void Execute(Work& w)
            {
                Def::Compute(w);
            }
            static bool Current(const EditorProcessingContext& context, const Work& w)
            {
                return GeometryPropertiesCurrent(context, w.Entity, w.Inputs) &&
                    GeometryPropertiesCurrent(context, w.Entity, w.OutputWatches);
            }

            static Result Publication(const EditorProcessingContext& context, Work& w)
            {
                return Def::Publish(context, w);
            }

            // Stages 1-8, as the scalar shape. The immediate mode: stage 4 needs nothing, the runtime's gate,
            // the definition's publication, stored and returned. A GPU start (the definition's transaction)
            // stores its answer unless another run owns the output.
            static Result Apply(
                const EditorProcessingCommands& commands, const Params& config, std::function<void(Result)> onComplete)
            {
                const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
                const std::uint64_t invocation = BeginOperationRun(context);
                std::string diagnostic;
                auto w = Capture(context, config, diagnostic);
                if (!w)
                    return Base::Report(context,
                        invocation,
                        w,
                        config,
                        EditorCommandStatus::InvalidProcessingParameters,
                        std::move(diagnostic));
                if (Def::Immediate(w->Config))
                {
                    if (!Current(context, *w))
                        return Base::Report(
                            context, invocation, w, config, EditorCommandStatus::StaleEntity, Base::StalePublication());
                    return KeepOperationResult<Op>(context, invocation, Def::PublishImmediate(context, w));
                }
                if (w->Config.Backend == Backend::VulkanLBVH)
                {
                    auto result = w->Result;
                    bool busy = false;
                    (void)Def::StartGpu(context,
                        w,
                        result,
                        std::move(onComplete),
                        kOneShotBatchExecution,
                        Base::Retainer(context, invocation),
                        busy);
                    if (!busy)
                        (void)KeepOperationResult<Op>(context, invocation, result);
                    return result;
                }
                return Base::RunOnCpu(
                    context, invocation, w, *Def::Outputs(w->Config)[0], std::move(onComplete), kOneShotBatchExecution);
            }

            // ARCH-023: an interactive or agent start with `config.Execution` (its auto_accept and start): the CPU
            // backends as RunOnCpu's one-shot step run, Vulkan as the definition's transaction (whose step run keeps it
            // until it ends, so no caller need hold it), the immediate mode as Apply's. Its answer carries the step
            // run (Result.StepRun; 0 when refused or immediate); `onComplete` gets the terminal result as Apply's does.
            static Result StartRun(
                const EditorProcessingCommands& commands, const Params& config, std::function<void(Result)> onComplete)
            {
                const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
                const auto& execution = config.Execution;
                if (Def::Immediate(config))
                    return Apply(commands, config, std::move(onComplete));
                std::string diagnostic;
                const std::uint64_t invocation = BeginOperationRun(context);
                auto w = Capture(context, config, diagnostic);
                if (!w)
                    return Base::Report(context,
                        invocation,
                        w,
                        config,
                        EditorCommandStatus::InvalidProcessingParameters,
                        std::move(diagnostic));
                if (w->Config.Backend == Backend::VulkanLBVH)
                {
                    auto result = w->Result;
                    bool busy = false;
                    const auto run = Def::StartGpu(context,
                        w,
                        result,
                        std::move(onComplete),
                        execution,
                        Base::Retainer(context, invocation),
                        busy);
                    if (run)
                        result.StepRun = run->Core.StepRun;
                    if (!busy)
                        (void)KeepOperationResult<Op>(context, invocation, result);
                    return result;
                }
                return Base::RunOnCpu(
                    context, invocation, w, *Def::Outputs(w->Config)[0], std::move(onComplete), execution);
            }

            // A GPU transaction the caller drives: `result` holds the request; a refusal keeps it with the
            // reason and is stored, as is a start no other run answers for. The caller's execution policy
            // passes through unchanged; the core normalizes only the one-shot cap and preview cadence.
            static auto StartTransaction(const EditorProcessingCommands& commands,
                const Params& config,
                Result& result,
                const StepRunExecution& execution)
            {
                const auto& ctx = EditorProcessingCommandsAccess::Resolve(commands);
                const std::uint64_t invocation = BeginOperationRun(ctx);
                std::string why;
                auto w = Capture(ctx, config, why);
                if (!w)
                {
                    result.Status = EditorCommandStatus::InvalidProcessingParameters;
                    result.Message = std::move(why);
                    (void)KeepOperationResult<Op>(ctx, invocation, result);
                    return decltype(Def::StartGpu(ctx, w, result, {}, execution, {}, std::declval<bool&>())){};
                }
                bool busy = false;
                auto run = Def::StartGpu(ctx, w, result, {}, execution, Base::Retainer(ctx, invocation), busy);
                if (!busy)
                    (void)KeepOperationResult<Op>(ctx, invocation, result);
                return run;
            }
            static void Refuse(const EditorProcessingCommands& commands, const Result& result)
            {
                const auto& ctx = EditorProcessingCommandsAccess::Resolve(commands);
                (void)KeepOperationResult<Op>(ctx, BeginOperationRun(ctx), result);
            }

            // A transaction over given values (`fill` writes them into the run), captured as the CPU
            // reference would be; its Pending answer is stored, and its terminal result.
            template <class Fill>
            static auto MakeTransactionForTest(const EditorProcessingCommands& commands,
                const Params& config,
                Fill fill,
                Graphics::GpuPropertyResidency& residency)
            {
                const auto& ctx = EditorProcessingCommandsAccess::Resolve(commands);
                auto cpu = config;
                cpu.Backend = Reference;
                std::string why;
                auto w = Capture(ctx, cpu, why);
                using Run = decltype(Def::MakeTestGpu(ctx, w, residency, {}));
                if (!w || !fill(*w))
                    return Run{};
                w->Config.Backend = config.Backend;
                w->Result.RequestedBackend = config.Backend;
                const std::uint64_t invocation = BeginOperationRun(ctx);
                auto run = Def::MakeTestGpu(ctx, w, residency, Base::Retainer(ctx, invocation));
                if (run)
                    (void)KeepOperationResult<Op>(ctx, invocation, w->Result);
                return run;
            }
        };
    }
}

export extern "C++"
{
    namespace Extrinsic::Runtime
    {
    // An operation's config surface (RUNTIME-349): its section applied through the editor's config
    // control, and the active config. Operation modules reach the editor only through this module.
        inline RuntimeEngineConfigApplyResult ApplyOperationConfig(const EditorProcessingCommands& commands,
            const Core::Config::EngineConfigSectionValidationResult& validation,
            const std::string& sourceId,
            const std::function<void(Core::Config::EngineConfig&)>& update)
        {
            return ApplyEditorProcessingConfig(commands, validation, sourceId, update);
        }
        inline const Core::Config::EngineConfig* ActiveOperationConfig(const EditorProcessingCommands& commands)
        {
            const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
            return context.EngineConfigControlState ? &context.EngineConfigControlState->ActiveConfig : nullptr;
        }
        // The helpers a custom definition (Decision 5, its own publication and GPU steps) calls itself.
        namespace OperationSupport
        {
            using EditorFeatureDetail::ToEditorCommandStatus;
            using GeometryProcessingDetail::AcquirePointIndex;
            using GeometryProcessingDetail::AppendPointKnnRows;
            using GeometryProcessingDetail::GeometryPropertiesCurrent;
            using GeometryProcessingDetail::MutableGeometryProperties;
            using GeometryProcessingDetail::ObserveGeometryProperty;
            using GeometryProcessingDetail::PointIndexState;
            using GeometryProcessingDetail::PointPropertyWatch;
            using GeometryProcessingDetail::RebasePointPropertyHistory;
            using GeometryProcessingDetail::MeshSupport::QueuedJobRejectedMessage;
            using Runtime::GuardEditorProcessingResult;
            // The GPU lifecycle (Decision 6): the runtime-owned core and its steps.
            using GeometryProcessingDetail::AcquireGpuTransactionBack;
            using GeometryProcessingDetail::BeginGpuTransactionAccept;
            using GeometryProcessingDetail::DiscardGpuTransaction;
            using GeometryProcessingDetail::FailGpuTransaction;
            using GeometryProcessingDetail::FinishGpuTransaction;
            using GeometryProcessingDetail::GpuRingAcquisition;
            using GeometryProcessingDetail::GpuTransactionAcceptRefusal;
            using GeometryProcessingDetail::GpuTransactionCore;
            using GeometryProcessingDetail::GpuTransactionCurrent;
            using GeometryProcessingDetail::GpuTransactionDeferralsExhausted;
            using GeometryProcessingDetail::GpuTransactionOf;
            // ARCH-023: a worker-stepped transaction's Init answer and its owner-side checks.
            using GeometryProcessingDetail::GpuTransactionPrepare;
            using GeometryProcessingDetail::GpuTransactionStepCompleted;
            using GeometryProcessingDetail::GpuTransactionWorkerBusy;
            using GeometryProcessingDetail::GpuTransactionStartRefusal;
            using GeometryProcessingDetail::ReadyGpuTransaction;
            using GeometryProcessingDetail::SubmitGpuTransactionRun;
        }
    }
}
