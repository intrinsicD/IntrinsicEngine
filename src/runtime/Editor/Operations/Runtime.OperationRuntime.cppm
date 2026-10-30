// The operation runtime (ADR 0031, RUNTIME-347): one driver runs a declared operation through the
// stages 1 resolve, 2-3 readiness and capture, 4 acquire, 5 execute, 6 gate, 7 publish, 8 deliver.
// Two shapes share one base (OperationShape): point samples in and one float column out (kernel
// density and point spacing from kNN rows, density weights from radius rows), and point columns a
// definition publishes itself (outlier analysis, RUNTIME-354). A definition declares names,
// parameters, its outputs and a pure Compute; optional members override the runtime's defaults.
// RUNTIME-349: an exported module for operation modules (ADR 0031 Decision 7). The editor helpers
// the templates call are included in its purview as global-module declarations, reachable where an
// operation module instantiates the runtime; those a custom definition calls itself are exported by
// name in OperationSupport (RUNTIME-354), and no others.
module;
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
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.GpuPropertyBinding;
import Geometry.Properties;
#include "Editor/internal/Runtime.EditorProcessingAccess.hpp"
#include "Editor/internal/Runtime.EditorGeometryHelpers.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.PointFields.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.JobFailure.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.RadiusRows.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.GpuScalar.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.GpuFront.hpp"
#include "Editor/Operations/Runtime.GpuTransactionLifecycle.hpp"
export extern "C++"
{
    namespace Extrinsic::Runtime::GeometryProcessingDetail
    {
    // The live samples a definition computes on. CPU LBVH adds their kNN rows (self included) for a
    // definition with MinimumK, otherwise the shared index for QueryPointRadiusRows.
        struct PointSamplesInput
        {
            std::span<const glm::vec3> Points{};
            const std::vector<std::uint32_t>* Neighbors{nullptr};
            const SpatialIndexSnapshot* Index{nullptr};
        };

    // CPU LBVH radius rows: every sample's complete support at `radius` (others only, ascending).
        struct PointRadiusSupport
        {
            std::vector<std::uint32_t> Offsets{}, Indices{};
            std::size_t MaximumNeighbors{};
        };
        inline bool QueryPointRadiusRows(
            const PointSamplesInput& input, float radius, PointRadiusSupport& support, std::string& message)
        {
            PointRadiusRows rows;
            std::vector<std::uint32_t> row;
            const auto capacity = std::uint32_t(std::max<std::size_t>(1, input.Points.size() - 1));
            for (std::uint32_t i = 0; i < input.Points.size(); ++i)
            {
                const auto found = input.Index->Index.Radius(input.Points[i], radius, capacity, i, true);
                if (found.Neighbors.size() != found.TotalCount)
                {
                    message = "Incomplete CPU radius candidates.";
                    return false;
                }
                row.clear();
                for (const auto& n : found.Neighbors)
                    row.push_back(n.Index);
                if (!AppendPointRadiusRow(rows, row, message))
                    return false;
            }
            support = {std::move(rows.Offsets), std::move(rows.Indices), rows.MaximumNeighbors};
            return true;
        }

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
            static constexpr bool InlineDeliversToSink = []
            {
                if constexpr (requires { Def::InlineDeliversToSink; })
                    return bool(Def::InlineDeliversToSink);
                else
                    return true;
            }();
            static constexpr bool FinalizeFromPending = []
            {
                if constexpr (requires { Def::FinalizeFromPending; })
                    return bool(Def::FinalizeFromPending);
                else
                    return false;
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
            // without a job lane, execution and publication inline (stored, returned, and given to the sink
            // unless the definition says otherwise); otherwise one queued run per output (a duplicate answers
            // Pending for the active run and is not stored): its Pending answer is stored, stage 6 refuses a
            // cancelled, abandoned or stale run, and the terminal result is stored before the sink receives it.
            template <class Work>
            static Result RunOnCpu(const EditorProcessingContext& context,
                std::uint64_t invocation,
                const std::shared_ptr<Work>& w,
                const GeometryPropertyRef& output,
                std::function<void(Result)> onComplete)
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
                if (!context.JobCommands.Available())
                {
                    Shape::Execute(*w);
                    const Result result = KeepOperationResult<Op>(context, invocation, Publish(context, w));
                    if (Traits::InlineDeliversToSink && onComplete)
                        onComplete(result);
                    return result;
                }
                const EditorJobIdentity identity{.EntityId = w->Config.StableEntityId,
                    .Scope = ToEditorJobScope(output.Domain),
                    .OutputSemantic = GeometryPresentationSlotSemantic::ScalarField,
                    .OutputName = output.Name,
                    .RequestedDomain = EditorJobDomainOfBackend(ToString(w->Config.Backend))};
                const std::string& label = Traits::Words().JobLabel;
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
                        if constexpr (Traits::FinalizeFromPending)
                            delivery.Finalize();
                        else
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

        // Output names the parameters' output reference; capture resolves its domain in place. Optional:
        // Method, Evaluation and Failure (default texts); MinimumK (kNN rows, floored at it); Wording(defaults);
        // ReferenceBackend (CpuOctree) and MinimumLiveCount (2); Prepare (a capture step before the Vulkan
        // admission); GpuParams(params, result); InlineDeliversToSink (true); FinalizeFromPending (false: a
        // failed worker is delivered as it is); RefuseInPlace.
        template <class Def>
        concept PointScalarDefinition = requires(const typename Def::Params& params,
            typename Def::Params& editable,
            typename Def::Result& result,
            const Graphics::PointScalarGpuStats& stats,
            PointSamplesInput input) {
            typename Def::Backend;
            typename Def::Operation; // the exported tag its results are stored under
            requires std::same_as<typename Def::Operation::Result, typename Def::Result>;
            Def::Backend::CpuLBVH;
            Def::Backend::VulkanLBVH;
            { Def::Noun } -> std::convertible_to<std::string_view>;
            { Def::History } -> std::convertible_to<std::string_view>;
            { Def::Output(editable) } -> std::same_as<GeometryPropertyRef&>;
            { Def::Validate(params) } -> std::same_as<Core::Config::EngineConfigSectionValidationResult>;
            { Def::InitialResult(params) } -> std::same_as<typename Def::Result>;
            requires requires { Def::GpuParams(params); } || requires { Def::GpuParams(params, result); };
            Def::FoldGpuStats(result, stats);
            { Def::Compute(params, input, result) } -> std::same_as<std::optional<std::vector<float>>>;
        };

        template <PointScalarDefinition Def> struct OperationRuntime : OperationShape<OperationRuntime<Def>, Def>
        {
            using Base = OperationShape<OperationRuntime<Def>, Def>;
            using Params = typename Def::Params;
            using Result = typename Def::Result;
            using Backend = typename Def::Backend;
            using Op = typename Def::Operation;
            using Base::MinimumLiveCount;
            using Base::Reference;
            using Base::Words;
            static Graphics::PointScalarGpuParams GpuParams(const Params& c, const Result& r)
            {
                if constexpr (requires { Def::GpuParams(c, r); })
                    return Def::GpuParams(c, r);
                else
                    return Def::GpuParams(c);
            }

            struct Work : PointScalarCapture
            {
                Params Config{};
                entt::entity Entity{};
                std::shared_ptr<const SpatialIndexSnapshot> Index{};
                SpatialIndexHandle GpuIndex{};
                PointKnnRows Neighbors{};
                bool Abandoned{};
                typename Def::Result Result{};
            };

        // Stages 1-3 (and the GPU admission); null with the reason in `diagnostic` when refused.
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
                if (!CapturePointScalarField(context,
                        *entity,
                        a,
                        c.Positions,
                        Def::Output(c),
                        Words().OutputLabel,
                        purpose == CapturePurpose::Execute,
                        *w,
                        diagnostic))
                    return {};
                w->Config = c;
                w->Entity = *entity;
                w->Result = Def::InitialResult(c);
                w->Result.SlotCount = w->SlotCount;
                w->Result.LiveCount = w->LiveCount;
                if (w->Result.LiveCount < MinimumLiveCount)
                    return fail(Words().TooFewSamples);
                if (c.Backend != Reference &&
                    (!context.SpatialIndices || !w->ValidLbvh || w->Result.LiveCount > (1u << 24)))
                    return fail(Words().LbvhLimits);
                const auto admit = [&]
                {
                    return AdmitPointScalarGpu(context, *w, GpuParams(c, w->Result), diagnostic);
                };
                if constexpr (requires { &Def::Prepare; })
                {
                    if (!Def::Prepare(c, w->Points, w->Result, purpose == CapturePurpose::Execute, admit, diagnostic))
                        return {};
                }
                if (c.Backend == Backend::VulkanLBVH && !admit())
                    return {};
                return w;
            }

        // Stage 5, on the worker or inline.
            static void Execute(Work& w)
            {
                const auto started = std::chrono::steady_clock::now();
                const auto& c = w.Config;
                auto& r = w.Result;
                r.Status = EditorCommandStatus::GeometryProcessingFailed;
                r.ActualBackend = ToString(c.Backend);
                PointSamplesInput input{.Points = w.Points};
                if (c.Backend != Reference)
                {
                    if constexpr (requires { Def::MinimumK; })
                    {
                        const auto width = std::uint32_t(std::min<std::size_t>(
                            w.Points.size(), std::max<std::size_t>(c.KNeighbors, Def::MinimumK) + 1));
                        if (c.Backend == Backend::CpuLBVH &&
                            !AppendPointKnnRows(*w.Index, w.Points, width, w.Neighbors.Indices, r.Message))
                            return;
                        input.Neighbors = &w.Neighbors.Indices;
                    }
                    else
                        input.Index = w.Index.get();
                }
                // A definition without Failure and Evaluation writes its own messages in Compute.
                const auto values = Def::Compute(c, input, r);
                if (!values)
                {
                    if constexpr (requires { Def::Failure; })
                        r.Message = Def::Failure;
                    return;
                }
                for (std::size_t i = 0; i < w.Slots.size(); ++i)
                    w.AfterValues[w.Slots[i]] = (*values)[i];
                r.WrittenCount = w.Slots.size();
                r.Status = EditorCommandStatus::Applied;
                r.CpuComputeMilliseconds =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
                if constexpr (requires { Def::Evaluation; })
                    r.Message = JoinOperationText(
                        {Def::Noun, " computed using ", r.ActualBackend, " neighborhoods and ", Def::Evaluation, "."});
            }

            static bool Current(const EditorProcessingContext& context, const Work& w)
            {
                return PointScalarFieldCurrent(context, w.Entity, w);
            }

            // Stage 7's publication: the column through the property-write transaction.
            static Result Publication(const EditorProcessingContext& context, Work& w)
            {
                auto& r = w.Result;
                const auto status = PublishPointScalarField(context, w.Entity, w, std::string(Def::History));
                r.Status = status == EditorCommandHistoryStatus::InvalidCommand
                    ? EditorCommandStatus::InvalidProcessingParameters
                    : EditorFeatureDetail::ToEditorCommandStatus(status);
                if (!r.Succeeded())
                    r.Message = status == EditorCommandHistoryStatus::InvalidCommand
                        ? "Output values are not exactly representable in the selected scalar storage."
                        : JoinOperationText({Def::Noun, " publication rejected by history checks."});
                return r;
            }

        // Folds a GPU transaction's diagnostics into the result.
            static void FoldTransaction(Result& r, const EditorPointScalarTransactionSnapshot& s)
            {
                r.Status = s.Status;
                r.Message = s.Message;
                r.LiveCount = s.LiveCount;
                r.ActualBackend = s.GpuQueryBatches ? "vulkan_lbvh" : "";
                r.GpuInputUploadBytes = s.GpuInputUploadBytes;
                r.GpuInputCacheHits = s.GpuInputCacheHits;
                r.CpuStageReadbackBytes = s.CpuStageReadbackBytes;
                r.GpuQueryBatches = s.GpuQueryBatches;
                r.IndexReused = s.IndexReused;
                Def::FoldGpuStats(r, s.Statistics);
                if (s.Phase == EditorGpuTransactionPhase::Applied)
                    r.WrittenCount = r.LiveCount;
            }

        // The GPU shape: today's scalar transaction (stages 4-8 run inside it). The store keeps its
        // started answer and, through the transaction's retention hook, its terminal result.
            static EditorPointScalarTransactionHandle StartGpu(const EditorProcessingContext& ctx,
                const std::shared_ptr<Work>& w,
                std::uint64_t invocation,
                Result& result,
                std::function<void(Result)> sink,
                bool automatic,
                Graphics::GpuPropertyResidency* test = nullptr,
                const EditorPointScalarTransactionSnapshot& diagnostics = {})
            {
                EditorPointScalarTransactionSnapshot state;
                auto run = StartPointScalarGpu(
                    ctx,
                    w,
                    w->Entity,
                    w->Config.StableEntityId,
                    w->Config.Positions,
                    GpuParams(w->Config, w->Result),
                    Words().GpuHistory,
                    Words().JobLabel,
                    state,
                    [w, sink = std::move(sink)](EditorPointScalarTransactionSnapshot s)
                    {
                        FoldTransaction(w->Result, s);
                        if (sink)
                            sink(w->Result);
                    },
                    automatic,
                    test,
                    diagnostics,
                    [w, retain = Base::Retainer(ctx, invocation)](EditorPointScalarTransactionSnapshot s)
                    {
                        auto terminal = w->Result;
                        FoldTransaction(terminal, s);
                        retain(terminal);
                    });
                FoldTransaction(w->Result, state);
                result = w->Result;
                // A start refused because another run owns the output answers for that run.
                if (!state.OutputBusy)
                    (void)KeepOperationResult<Op>(ctx, invocation, result);
                return run;
            }

            static GeometryPropertyCatalogSnapshot InputCatalog(
                const EditorProcessingCommands& commands, std::uint32_t id)
            {
                return BuildPointInputCatalog(EditorProcessingCommandsAccess::Resolve(commands), id, MinimumLiveCount);
            }

            // Stages 1-8: an immediate failure is stored and returned; then the GPU transaction, or the
            // CPU run (stages 4-8, Base::RunOnCpu).
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
                if (w->Config.Backend == Backend::VulkanLBVH)
                {
                    Result result;
                    (void)StartGpu(context, w, invocation, result, std::move(onComplete), true);
                    return result;
                }
                return Base::RunOnCpu(context, invocation, w, Def::Output(w->Config), std::move(onComplete));
            }

        // A GPU transaction the caller drives (preview, Accept, Discard); refused unless VulkanLBVH.
            static EditorPointScalarTransactionHandle StartTransaction(const EditorProcessingCommands& commands,
                const Params& config,
                Result& result,
                std::function<void(Result)> sink)
            {
                const auto& ctx = EditorProcessingCommandsAccess::Resolve(commands);
                const std::uint64_t invocation = BeginOperationRun(ctx);
                std::string why;
                auto w = Capture(ctx, config, why);
                if (!w || config.Backend != Backend::VulkanLBVH)
                {
                    if constexpr (requires { Def::RefuseInPlace(result, config); })
                        Def::RefuseInPlace(result, config); // the caller's other fields stay
                    else
                        result = Def::InitialResult(config);
                    result.Status = EditorCommandStatus::InvalidProcessingParameters;
                    result.Message = w ? "GPU transaction requires vulkan_lbvh." : why;
                    result = KeepOperationResult<Op>(ctx, invocation, std::move(result));
                    return {};
                }
                return StartGpu(ctx, w, invocation, result, std::move(sink), false);
            }

        // A transaction over given values on a test residency, captured as the CPU reference would be.
            static EditorPointScalarTransactionHandle MakeTransactionForTest(const EditorProcessingCommands& commands,
                const Params& config,
                std::vector<float> values,
                Graphics::GpuPropertyResidency& residency,
                const EditorPointScalarTransactionSnapshot& diagnostics)
            {
                const auto& ctx = EditorProcessingCommandsAccess::Resolve(commands);
                auto cpu = config;
                cpu.Backend = Reference;
                std::string why;
                auto w = Capture(ctx, cpu, why);
                if (!w || values.size() != w->SlotCount)
                    return {};
                w->Config.Backend = config.Backend;
                w->Result.RequestedBackend = config.Backend;
                w->AfterValues = std::move(values);
                Result result;
                return StartGpu(ctx, w, BeginOperationRun(ctx), result, {}, false, &residency, diagnostics);
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
                    (void)Def::StartGpu(
                        context, w, result, std::move(onComplete), true, Base::Retainer(context, invocation), busy);
                    if (!busy)
                        (void)KeepOperationResult<Op>(context, invocation, result);
                    return result;
                }
                return Base::RunOnCpu(context, invocation, w, *Def::Outputs(w->Config)[0], std::move(onComplete));
            }

            // A GPU transaction the caller drives: `result` holds the request; a refusal keeps it with the
            // reason and is stored, as is a start no other run answers for.
            static auto StartTransaction(const EditorProcessingCommands& commands, const Params& config, Result& result)
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
                    return decltype(Def::StartGpu(ctx, w, result, {}, false, {}, std::declval<bool&>())){};
                }
                bool busy = false;
                auto run = Def::StartGpu(ctx, w, result, {}, false, Base::Retainer(ctx, invocation), busy);
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
            using GeometryProcessingDetail::GpuTransactionStartRefusal;
            using GeometryProcessingDetail::ReadyGpuTransaction;
            using GeometryProcessingDetail::SubmitGpuTransactionRun;
        }
    }
}
