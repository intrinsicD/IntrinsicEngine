// The operation runtime (ADR 0031, RUNTIME-347): one driver runs a declared operation through the
// stages 1 resolve, 2-3 readiness and capture, 4 acquire, 5 execute, 6 gate, 7 publish, 8 deliver.
// This is its pilot specialization: point samples in, one float column out, from kNN rows (kernel
// density, point spacing). A definition declares names, parameters, its output and a pure Compute.
// Include after the GeometryProcessingOperations PointFields, JobFailure, RadiusRows and GpuScalar
// headers; RUNTIME-349 turns it into the exported framework module.
#pragma once
extern "C++"
{
    namespace Extrinsic::Runtime::GeometryProcessingDetail
    {
    // The live samples a definition computes on; for CPU LBVH also their kNN rows, self included.
        struct PointSamplesInput
        {
            std::span<const glm::vec3> Points{};
            const std::vector<std::uint32_t>* Neighbors{nullptr};
        };

    // Messages are built from the names: "<Noun> estimation" labels the job, "<Method> requires at
    // least two live samples." refuses a small input. MinimumK floors the kNN row width. Output
    // names the parameters' output reference; capture resolves its domain in place.
        template <class Def>
        concept PointScalarKnnDefinition = requires(const typename Def::Params& params,
            typename Def::Params& editable,
            typename Def::Result& result,
            const Graphics::PointScalarGpuStats& stats,
            PointSamplesInput input) {
            typename Def::Backend;
            Def::Backend::CpuOctree;
            Def::Backend::CpuLBVH;
            Def::Backend::VulkanLBVH;
            { Def::Noun } -> std::convertible_to<std::string_view>;
            { Def::Method } -> std::convertible_to<std::string_view>;
            { Def::History } -> std::convertible_to<std::string_view>;
            { Def::Evaluation } -> std::convertible_to<std::string_view>;
            { Def::Failure } -> std::convertible_to<std::string_view>;
            { Def::MinimumK } -> std::convertible_to<std::size_t>;
            { Def::Output(editable) } -> std::same_as<GeometryPropertyRef&>;
            { Def::Validate(params) } -> std::same_as<Core::Config::EngineConfigSectionValidationResult>;
            { Def::InitialResult(params) } -> std::same_as<typename Def::Result>;
            { Def::GpuParams(params) } -> std::same_as<Graphics::PointScalarGpuParams>;
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

        template <PointScalarKnnDefinition Def> struct OperationRuntime
        {
            using Params = typename Def::Params;
            using Result = typename Def::Result;
            using Backend = typename Def::Backend;
            static constexpr std::uint32_t MinimumLiveCount = 2;

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
                    return fail(JoinOperationText({Def::Noun, " target entity is stale or missing."}));
                const auto a = BuildGeometryAvailability(context.Scene->Raw(), *entity);
                auto w = std::make_shared<Work>();
                if (!CapturePointScalarField(context,
                        *entity,
                        a,
                        c.Positions,
                        Def::Output(c),
                        Def::Noun,
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
                    return fail(JoinOperationText({Def::Method, " requires at least two live samples."}));
                if (c.Backend != Backend::CpuOctree)
                {
                    if (!context.SpatialIndices || !w->ValidLbvh || w->Result.LiveCount > (1u << 24))
                        return fail(
                            "LBVH requires the spatial cache, at most 2^24 samples and coordinates within 1e18.");
                    if (c.Backend == Backend::VulkanLBVH)
                    {
                        if (!AdmitPointScalarGpu(context, *w, Def::GpuParams(c), diagnostic))
                            return {};
                    }
                }
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
                if (c.Backend != Backend::CpuOctree)
                {
                    const auto width = std::uint32_t(
                        std::min<std::size_t>(w.Points.size(), std::max<std::size_t>(c.KNeighbors, Def::MinimumK) + 1));
                    if (c.Backend == Backend::CpuLBVH &&
                        !AppendPointKnnRows(*w.Index, w.Points, width, w.Neighbors.Indices, r.Message))
                        return;
                    input.Neighbors = &w.Neighbors.Indices;
                }
                const auto values = Def::Compute(c, input, r);
                if (!values)
                {
                    r.Message = Def::Failure;
                    return;
                }
                for (std::size_t i = 0; i < w.Slots.size(); ++i)
                    w.AfterValues[w.Slots[i]] = (*values)[i];
                r.WrittenCount = w.Slots.size();
                r.Status = EditorCommandStatus::Applied;
                r.CpuComputeMilliseconds =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
                r.Message = JoinOperationText(
                    {Def::Noun, " computed using ", r.ActualBackend, " neighborhoods and ", Def::Evaluation, "."});
            }

        // Stage 7, behind its own currentness check.
            static Result Publish(const EditorProcessingContext& context, const std::shared_ptr<Work>& w)
            {
                auto& r = w->Result;
                if (!PointScalarFieldCurrent(context, w->Entity, *w))
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
                RetainOperationResult(context.OperationResults, context.AttachmentActive, invocation, result);
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
                    Def::GpuParams(w->Config),
                    std::string(Def::History),
                    JoinOperationText({Def::Noun, " estimation"}),
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
                        RetainOperationResult(store, active, invocation, terminal);
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
                if (w->Config.Backend != Backend::CpuOctree)
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
                        return report(EditorCommandStatus::StaleEntity,
                            JoinOperationText({Def::Noun, " index snapshot does not match the selected samples."}));
                }
                if (!context.JobCommands.Available())
                {
                    Execute(*w);
                    const Result result = Keep(context, invocation, Publish(context, w));
                    if (onComplete)
                        onComplete(result);
                    return result;
                }
                const auto& output = Def::Output(w->Config);
                const EditorJobIdentity identity{.EntityId = config.StableEntityId,
                    .Scope = ToEditorJobScope(output.Domain),
                    .OutputSemantic = GeometryPresentationSlotSemantic::ScalarField,
                    .OutputName = output.Name,
                    .RequestedDomain = EditorJobDomainOfBackend(ToString(w->Config.Backend))};
                const std::string label = JoinOperationText({Def::Noun, " estimation"});
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
                        RetainOperationResult(store, active, invocation, result);
                        if (sink)
                            sink(std::move(result));
                    },
                    std::move(queued),
                    label};
                JobDesc desc{.DebugName = label,
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
                            .Message = JoinOperationText({Def::Noun, " estimation config is unavailable."})});
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
                cpu.Backend = Backend::CpuOctree;
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
