// The operation runtime (ADR 0031, RUNTIME-347): one driver runs a declared operation through the
// stages 1 resolve, 2-3 readiness and capture, 4 acquire, 5 execute, 6 gate, 7 publish, 8 deliver.
// This is its point scalar specialization: point samples in, one float column out (kernel density and
// point spacing from kNN rows; density weights from radius rows, RUNTIME-353). A definition declares
// names, parameters, its output and a pure Compute; optional members override the runtime's defaults.
// RUNTIME-349: an exported module for operation modules (ADR 0031 Decision 7). The editor helpers
// the templates call are included in its purview as global-module declarations: reachable where an
// operation module instantiates the runtime, not exported.
module;
#include <algorithm>
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
import Geometry.Properties;
#include "Editor/internal/Runtime.EditorProcessingAccess.hpp"
#include "Editor/internal/Runtime.EditorGeometryHelpers.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.PointFields.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.JobFailure.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.RadiusRows.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.GpuScalar.hpp"
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

    // The texts a definition may declare whole (Def::Wording()); by default they are built from its
    // names: "<Noun> estimation" labels the job, "<Method> requires at least two live samples."
    // refuses a small input.
        struct OperationWording
        {
            std::string StaleTarget, OutputLabel, TooFewSamples, LbvhLimits, IndexMismatch, ConfigUnavailable, JobLabel,
                JobName, GpuHistory;
        };

    // Output names the parameters' output reference; capture resolves its domain in place. Optional:
    // Method, Evaluation and Failure (default texts); MinimumK (kNN rows, floored at it); Wording();
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

        template <PointScalarDefinition Def> struct OperationRuntime
        {
            using Params = typename Def::Params;
            using Result = typename Def::Result;
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
                    if constexpr (requires { Def::Wording(); })
                        return Def::Wording();
                    else
                    {
                        const auto job = JoinOperationText({Def::Noun, " estimation"});
                        return OperationWording{
                            .StaleTarget = JoinOperationText({Def::Noun, " target entity is stale or missing."}),
                            .OutputLabel = std::string(Def::Noun),
                            .TooFewSamples = JoinOperationText({Def::Method, " requires at least two live samples."}),
                            .LbvhLimits =
                                "LBVH requires the spatial cache, at most 2^24 samples and coordinates within 1e18.",
                            .IndexMismatch =
                                JoinOperationText({Def::Noun, " index snapshot does not match the selected samples."}),
                            .ConfigUnavailable = JoinOperationText({Def::Noun, " estimation config is unavailable."}),
                            .JobLabel = job,
                            .JobName = job,
                            .GpuHistory = std::string(Def::History)};
                    }
                }();
                return words;
            }
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
            enum class Purpose
            {
                Execute,
                Readiness
            };

        // Stages 1-3 (and the GPU admission); null with the reason in `diagnostic` when refused.
            static std::shared_ptr<Work> Capture(const EditorProcessingContext& context,
                Params c,
                std::string& diagnostic,
                Purpose purpose = Purpose::Execute)
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
                        purpose == Purpose::Execute,
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
                    if (!Def::Prepare(c, w->Points, w->Result, purpose == Purpose::Execute, admit, diagnostic))
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

            // Stage 7, behind its own currentness check (an abandoned run never reaches it in the job service).
            static Result Publish(const EditorProcessingContext& context, const std::shared_ptr<Work>& w)
            {
                auto& r = w->Result;
                if (w->Abandoned || !PointScalarFieldCurrent(context, w->Entity, *w))
                {
                    r.Status = EditorCommandStatus::StaleEntity;
                    r.Message = JoinOperationText({Def::Noun, " input or output changed before publication."});
                    return r;
                }
                if (r.Status != EditorCommandStatus::Applied)
                    return r;
                const auto status = PublishPointScalarField(context, w->Entity, *w, std::string(Def::History));
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

        // Stage 8's slot (RUNTIME-348): the session's store, written before any sink runs and only while
        // the attachment is active. A duplicate refusal never writes it (it answers for another run).
            static std::uint64_t BeginRun(const EditorProcessingContext& context)
            {
                return context.OperationResults ? context.OperationResults->BeginRun() : 0u;
            }
            static Result Keep(const EditorProcessingContext& context, std::uint64_t invocation, Result result)
            {
                RetainOperationResult<typename Def::Operation>(
                    context.OperationResults, context.AttachmentActive, invocation, result);
                return result;
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
                    [w, store = ctx.OperationResults, active = ctx.AttachmentActive, invocation](
                        EditorPointScalarTransactionSnapshot s)
                    {
                        auto terminal = w->Result;
                        FoldTransaction(terminal, s);
                        RetainOperationResult<typename Def::Operation>(store, active, invocation, terminal);
                    });
                FoldTransaction(w->Result, state);
                result = w->Result;
                // A start refused because another run owns the output answers for that run.
                if (!state.OutputBusy)
                    (void)Keep(ctx, invocation, result);
                return run;
            }

        // Stages 1-2 only: ready, refused, or pending (the input kind's deferred verdict).
            static ActionReadiness Preview(const EditorProcessingCommands& commands, const Params& config)
            {
                const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
                std::string diagnostic;
                const auto work = Capture(context, config, diagnostic, Purpose::Readiness);
                return {bool(work), std::move(diagnostic)};
            }

            static GeometryPropertyCatalogSnapshot InputCatalog(
                const EditorProcessingCommands& commands, std::uint32_t id)
            {
                return BuildPointInputCatalog(EditorProcessingCommandsAccess::Resolve(commands), id, MinimumLiveCount);
            }

        // Stages 1-8. Immediate failures are stored and returned; without a job lane 5-8 run inline and
        // the result is stored, delivered to the sink once and returned; a queued run stores its Pending
        // and later its terminal result, which the sink receives once.
            static Result Apply(
                const EditorProcessingCommands& commands, const Params& config, std::function<void(Result)> onComplete)
            {
                const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
                const std::uint64_t invocation = BeginRun(context);
                std::string diagnostic;
                auto w = Capture(context, config, diagnostic);
                const auto answer = [&config, &w](EditorCommandStatus status, std::string message)
                {
                    auto result = w ? w->Result : Def::InitialResult(config);
                    result.Status = status;
                    result.Message = std::move(message);
                    return result;
                };
                const auto report = [&](EditorCommandStatus status, std::string message)
                {
                    return Keep(context, invocation, answer(status, std::move(message)));
                };
                if (!w)
                    return report(EditorCommandStatus::InvalidProcessingParameters, std::move(diagnostic));
                if (w->Config.Backend == Backend::VulkanLBVH)
                {
                    Result result;
                    (void)StartGpu(context, w, invocation, result, std::move(onComplete), true);
                    return result;
                }
            // Stage 4: the shared index, acquired after capture.
                if (w->Config.Backend != Reference)
                {
                    const auto indexState = AcquirePointIndex(*context.SpatialIndices,
                        context.World,
                        w->Entity,
                        w->Config.Positions,
                        w->Slots,
                        w->Points,
                        w->GpuIndex,
                        w->Index,
                        w->Result.IndexReused,
                        diagnostic);
                    if (indexState == PointIndexState::Unavailable)
                        return report(EditorCommandStatus::InvalidProcessingParameters, diagnostic);
                    if (indexState == PointIndexState::Mismatched)
                        return report(EditorCommandStatus::StaleEntity, Words().IndexMismatch);
                }
                if (!context.JobCommands.Available())
                {
                    Execute(*w);
                    const Result result = Keep(context, invocation, Publish(context, w));
                    if (InlineDeliversToSink && onComplete)
                        onComplete(result);
                    return result;
                }
                const auto& output = Def::Output(w->Config);
                const EditorJobIdentity identity{.EntityId = config.StableEntityId,
                    .Scope = ToEditorJobScope(output.Domain),
                    .OutputSemantic = GeometryPresentationSlotSemantic::ScalarField,
                    .OutputName = output.Name,
                    .RequestedDomain = EditorJobDomainOfBackend(ToString(w->Config.Backend))};
                const std::string& label = Words().JobLabel;
                if (auto busy = MeshSupport::ActiveOutputJobRefusal(context, identity, label))
                    return answer(EditorCommandStatus::Pending, std::move(busy->Message));
                auto queued = w->Result;
                queued.Message = label + " queued.";
                const MeshSupport::QueuedJobDelivery<Result> delivery{context,
                    [store = context.OperationResults,
                        active = context.AttachmentActive,
                        invocation,
                        sink = std::move(onComplete)](Result result)
                    {
                        RetainOperationResult<typename Def::Operation>(store, active, invocation, result);
                        if (sink)
                            sink(std::move(result));
                    },
                    std::move(queued),
                    label};
                JobDesc desc{.DebugName = Words().JobName,
                    .Scope = context.World,
                    .Work = [w](const JobCancellation&) -> JobResultEnvelope
                    {
                        Execute(*w);
                        return JobResultEnvelope::Make(true);
                    },
                    // Stage 6: a cancelled, abandoned or stale run publishes nothing.
                    .ValidateBeforeApply =
                        [context, w]
                    {
                        return MeshSupport::ValidateQueuedJob(
                            w->Abandoned, PointScalarFieldCurrent(context, w->Entity, *w));
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
                        if constexpr (FinalizeFromPending)
                            delivery.Finalize();
                        else
                            delivery.FinalizeAfterWorker(w->Result);
                    }};
                if (!context.JobCommands.Submit(std::move(desc), identity).IsValid())
                {
                    w->Abandoned = true;
                    return Keep(context, invocation, delivery.Rejected());
                }
                return Keep(context, invocation, delivery.Pending());
            }

            static Result ApplyConfigured(const EditorProcessingCommands& commands,
                const std::optional<Params>& config,
                std::function<void(Result)> onComplete)
            {
                if (!config)
                {
                    const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
                    return Keep(context,
                        BeginRun(context),
                        {.Status = EditorCommandStatus::InvalidProcessingParameters,
                            .Message = Words().ConfigUnavailable});
                }
                return Apply(commands, *config, std::move(onComplete));
            }

        // A GPU transaction the caller drives (preview, Accept, Discard); refused unless VulkanLBVH.
            static EditorPointScalarTransactionHandle StartTransaction(const EditorProcessingCommands& commands,
                const Params& config,
                Result& result,
                std::function<void(Result)> sink)
            {
                const auto& ctx = EditorProcessingCommandsAccess::Resolve(commands);
                const std::uint64_t invocation = BeginRun(ctx);
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
                    result = Keep(ctx, invocation, std::move(result));
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
                return StartGpu(ctx, w, BeginRun(ctx), result, {}, false, &residency, diagnostics);
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
    }
}
