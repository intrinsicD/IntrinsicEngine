// ARCH-022 S3c-3: the main-thread stall of a GPU K-Means step run under a slider preview (R7, under 4 ms).
// Opt-in: skipped unless INTRINSIC_KMEANS_STALL_OUT names the JSON to write. INTRINSIC_KMEANS_STALL_ARM selects
// the arm: "run" (the slider stages and the accept), "null" (the same frames, no request) or "controls" (no request;
// one delay per measured interval at tagged frames). INTRINSIC_KMEANS_STALL_POINTS is the cloud size,
// INTRINSIC_KMEANS_STALL_TIMEOUT_S the wall-clock limit. Every frame's sample is kept in memory and written after
// the measured frames (no I/O in the loop). The evaluation is tasks/evidence/ARCH-022/kmeans_stall_report.py.
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include "RuntimeTestModule.hpp"

import Extrinsic.Backends.Vulkan;
import Extrinsic.Core.Config.Engine;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.TransferQueue;
import Extrinsic.Runtime.AsyncWorkModule;
import Extrinsic.Runtime.ClusteringModule;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.EngineConfigBoot;
import Extrinsic.Runtime.FramePacingDiagnostics;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.SceneDocumentModule;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.StepRuns;
import Geometry.Properties;

namespace
{
    namespace R = Extrinsic::Runtime;
    namespace GS = Extrinsic::ECS::Components::GeometrySources;
    using Clock = std::chrono::steady_clock;
    using Json = nlohmann::json;

    enum class Stage : int { Warmup, Continuous, Hold, Settle, Controls, Idle };
    constexpr const char* kStageNames[] = {"warmup", "continuous", "hold", "settle", "controls", "idle"};
    constexpr std::uint32_t kWarmupFrames = 60, kContinuousFrames = 600, kHoldChanges = 20, kHoldLimit = 240,
                            kSettleLimit = 300, kIdleFrames = 1000;

    void BusyWait(const std::chrono::microseconds duration)
    {
        const auto until = Clock::now() + duration;
        while (Clock::now() < until) {}
    }

    // The controls: one delay per measured interval, each at its own tagged frame of the controls stage.
    enum class Control : int { None, Command, Completion, Collect, Drain, Record, Split, GpuWork };
    struct ControlAt
    {
        std::uint32_t Frame;
        Control Kind;
    };
    constexpr std::array kControls{ControlAt{20, Control::Command}, ControlAt{40, Control::Completion},
                                   ControlAt{60, Control::Collect},  ControlAt{80, Control::Drain},
                                   ControlAt{100, Control::Record},  ControlAt{120, Control::Split},
                                   ControlAt{140, Control::GpuWork}};
    constexpr std::uint32_t kControlFrames = 180;
    struct StallCommand
    {
        std::chrono::microseconds Delay{};
    };

    struct Sample
    {
        // The harness's own record of frame f, taken at its Maintenance hook (the last run work of a frame is the
        // transfer drain before it); the pacing of frame f is read one frame later and stored beside it.
        int StageValue{};
        std::uint32_t Requests{};
        int ControlValue{};
        R::ClusteringModuleStats Module{};
        std::optional<R::RuntimeFramePacingDiagnostics> Pacing{};
    };

    class StallApp final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        std::string Arm{"run"};
        std::uint32_t Points{100000u};
        std::chrono::seconds Timeout{600};
        std::vector<Sample> Samples{};
        std::vector<R::KMeansRunCompleted> Completions{};
        bool TimedOut{}, Unavailable{}, Done{};
        std::uint64_t CloudHash{};
        std::uint32_t Changes{};
        R::CommandCorrelationId Final{};

        [[nodiscard]] Extrinsic::Core::Result OnRegister(R::EngineSetup& setup) override
        {
            if (auto base = RuntimeTestModule::OnRegister(setup); !base) return base;
            setup.RegisterCommandHandler<StallCommand>([](R::CommandContext&, const StallCommand& command)
            {
                BusyWait(command.Delay);
                return R::CommandOutcome::Ok();
            });
            Participant = setup.Jobs().RegisterGpuQueueParticipant({
                .DebugName = "ArchStall.Controls",
                .RecordFrameCommands = [this](Extrinsic::RHI::ICommandContext& cmd) { RecordControls(cmd); },
                .DrainCompletedTransfers = [this]
                { if (const auto delay = std::exchange(DrainDelay, {}); delay.count() > 0) BusyWait(delay); },
                .HasInFlightWork = [] { return false; },
                .ShutdownAfterDeviceIdle = [this] { ReleaseGpuBuffer(); },
            });
            return setup.RegisterFrameHook(R::FramePhase::Maintenance,
                                           [this](R::RuntimeFrameHookContext&) { SampleFrame(); });
        }

        void Resolve() override
        {
            auto& engine = Kernel();
            Service = engine.Services().Find<R::ClusteringService>();
            if (Service == nullptr || !Service->Available())
            {
                Unavailable = true;
                return;
            }
            Samples.reserve(kWarmupFrames + kContinuousFrames + kHoldChanges * kHoldLimit + kSettleLimit + kIdleFrames + 8);
            auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
            Entity = scene.Create();
            auto& vertices = scene.Raw().emplace<GS::Vertices>(Entity);
            vertices.Properties.Resize(Points);
            auto positions = vertices.Properties.GetOrAdd<glm::vec3>("v:position", glm::vec3{0.0f});
            std::uint32_t random = 17u;
            CloudHash = 1469598103934665603ull;
            for (std::uint32_t i = 0; i < Points; ++i)
            {
                glm::vec3 point{float(i % 8u) * 10.0f, float((i / 8u) % 4u) * 10.0f, 0.0f};
                for (int axis = 0; axis < 3; ++axis)
                {
                    random = random * 1664525u + 1013904223u;
                    point[axis] += float(random >> 8) / float(1u << 24);
                }
                positions[i] = point;
                for (int axis = 0; axis < 3; ++axis)
                    CloudHash = (CloudHash ^ std::uint64_t(std::bit_cast<std::uint32_t>(point[axis]))) * 1099511628211ull;
            }
            Subscription = Service->SubscribeRunCompleted([this](const auto& completed) { Completions.push_back(completed); });
            Started = Clock::now();
        }

        // UiBuild: this frame's request (drained by the next frame's command drain) or control.
        void Frame(double, double) override
        {
            auto& engine = Kernel();
            if (Clock::now() - Started > Timeout) TimedOut = true;
            if (Unavailable || TimedOut || Done)
            {
                engine.RequestExit();
                return;
            }
            if (!engine.GetDevice().IsOperational()) return; // before the first clean frame: not counted
            Pending = {};
            const std::uint32_t inStage = ++FramesInStage;
            if (Arm == "controls")
                return ControlsFrame(inStage);
            if (Arm == "null")
                return NullFrame(inStage);
            RunFrame(inStage);
        }

    private:
        static constexpr std::chrono::microseconds Ms5() { return std::chrono::microseconds{5000}; }
        static constexpr std::chrono::microseconds Ms2_5() { return std::chrono::microseconds{2500}; }

        R::ClusteringService* Service{};
        R::KernelEventSubscription Subscription{};
        Extrinsic::ECS::EntityHandle Entity{Extrinsic::ECS::InvalidEntityHandle};
        R::GpuQueueParticipantHandle Participant{};
        Clock::time_point Started{};
        Stage Current{Stage::Warmup};
        std::uint32_t FramesInStage{}, Pending{}, HoldFrames{};
        bool HoldStarted{}, Accepted{};
        Control PendingControl{Control::None};
        R::CommandCorrelationId Latest{};
        std::chrono::microseconds DrainDelay{}, RecordDelay{};
        bool GpuWorkDue{};
        Extrinsic::RHI::BufferHandle GpuBuffer{}, ReadbackBuffer{};

        void Next(const Stage stage)
        {
            Current = stage;
            FramesInStage = 0u;
        }

        void Request(const std::uint32_t seed)
        {
            R::RunKMeans request{.StableEntityId = R::SelectionController::ToStableEntityId(Entity),
                .Properties = R::MakeKMeansPropertyRefs(R::GeometryElementDomain::PointCloudPoint),
                .Parameters = {.ClusterCount = 8u,
                    .Seed = seed,
                    .Initialization = R::KMeansInitialization::Random,
                    .Execution = {.IterationCap = 50u,
                        .PreviewEvery = 5u,
                        .PreviewIntervalMs = 0u,
                        .AutoAccept = R::StepRunAutoAccept::Never,
                        .Start = R::StepRunStart::Run}},
                .Backend = R::ClusteringBackend::VulkanCompute,
                .ReplaceRunning = true};
            Latest = Service->RunKMeans(std::move(request));
            ++Pending;
            ++Changes;
        }

        void NullFrame(const std::uint32_t inStage)
        {
            if (Current == Stage::Warmup && inStage > kWarmupFrames) Next(Stage::Idle);
            else if (Current == Stage::Idle && inStage > kIdleFrames) Done = true;
        }

        void RunFrame(const std::uint32_t inStage)
        {
            switch (Current)
            {
            case Stage::Warmup: // engine frames only: the first request's cold start is measured
                if (inStage > kWarmupFrames) Next(Stage::Continuous);
                return;
            case Stage::Continuous:
                if (inStage > kContinuousFrames)
                {
                    Next(Stage::Hold);
                    HoldStarted = false;
                    return;
                }
                Request(1000u + Changes);
                return;
            case Stage::Hold:
            {
                // Each change waits until its own run (observed only once it is the active one) published a
                // preview or is ready; bounded.
                if (HoldStarted)
                {
                    const auto observed = Service->GpuRun(Latest);
                    if (observed.Previews == 0u && !observed.ReadyToAccept)
                    {
                        if (++HoldFrames > kHoldLimit) TimedOut = true; // the report marks the cell UNEVALUATED
                        return;
                    }
                    HoldStarted = false;
                }
                if (Changes >= kContinuousFrames + kHoldChanges)
                {
                    Next(Stage::Settle);
                    Request(1000u + Changes); // the final run: it ends, is accepted and published
                    Final = Latest;
                    return;
                }
                Request(1000u + Changes);
                HoldStarted = true;
                HoldFrames = 0u;
                return;
            }
            case Stage::Settle:
            {
                const auto observed = Service->GpuRun(Final); // never paused: ready means ended
                if (!Accepted && observed.ReadyToAccept && observed.CanAccept)
                {
                    (void)Service->GpuRun(Final, R::KMeansGpuAction::Accept);
                    Accepted = true;
                }
                const bool published = std::ranges::any_of(Completions, [&](const auto& c) { return c.Correlation == Final; });
                if (published) Done = true;
                else if (inStage > kSettleLimit) TimedOut = true;
                return;
            }
            default: return;
            }
        }

        void ControlsFrame(const std::uint32_t inStage)
        {
            if (Current == Stage::Warmup)
            {
                if (inStage > kWarmupFrames) Next(Stage::Controls);
                return;
            }
            if (inStage > kControlFrames)
            {
                Done = true;
                return;
            }
            for (const auto& control : kControls)
                if (control.Frame == inStage) Fire(control.Kind);
        }

        // A control fired at UiBuild of frame f delays its interval in frame f+1 (the command drain and the
        // completion drain run before UiBuild) or later in frame f (collection, participant drain, recording).
        void Fire(const Control kind)
        {
            PendingControl = kind;
            auto& engine = Kernel();
            const auto delay = kind == Control::Split ? Ms2_5() : Ms5();
            switch (kind)
            {
            case Control::Command: engine.Commands().Enqueue(StallCommand{delay}); break;
            case Control::Completion:
            {
                R::JobDesc job{};
                job.DebugName = "ArchStall.Completion";
                job.Work = [](const R::JobCancellation&) { return R::JobResultEnvelope::Make(true); };
                job.PublishCompletion = [delay](R::KernelEventBus&, const R::JobResultEnvelope&)
                {
                    BusyWait(delay);
                    return true;
                };
                (void)engine.Jobs().Submit(std::move(job));
                break;
            }
            case Control::Collect:
            {
                auto& device = engine.GetDevice();
                if (!ReadbackBuffer.IsValid())
                    ReadbackBuffer = device.CreateBuffer({.SizeBytes = 256u,
                                                          .Usage = Extrinsic::RHI::BufferUsage::TransferSrc |
                                                                   Extrinsic::RHI::BufferUsage::TransferDst,
                                                          .DebugName = "ArchStall.Readback"});
                (void)device.GetTransferQueue().DownloadBuffer(
                    ReadbackBuffer, 256u, 0u,
                    Extrinsic::RHI::ReadbackSink::Invoke([delay](std::span<const std::byte>) { BusyWait(delay); }));
                break;
            }
            case Control::Drain: DrainDelay = delay; break;
            case Control::Record: RecordDelay = delay; break;
            case Control::Split: // two intervals of one frame (both later in this frame), each under 4 ms, over it together
                RecordDelay = delay;
                DrainDelay = delay;
                break;
            case Control::GpuWork: GpuWorkDue = true; break;
            case Control::None: break;
            }
        }

        void RecordControls(Extrinsic::RHI::ICommandContext& cmd)
        {
            if (const auto delay = std::exchange(RecordDelay, {}); delay.count() > 0) BusyWait(delay);
            if (!std::exchange(GpuWorkDue, false)) return;
            // GPU work long enough to block a later frame's fence: repeated fills of a 256 MiB buffer.
            constexpr std::uint64_t kBytes = 256ull << 20;
            if (!GpuBuffer.IsValid())
                GpuBuffer = Kernel().GetDevice().CreateBuffer(
                    {.SizeBytes = kBytes, .Usage = Extrinsic::RHI::BufferUsage::Storage |
                                                   Extrinsic::RHI::BufferUsage::TransferDst,
                     .DebugName = "ArchStall.GpuWork"});
            if (!GpuBuffer.IsValid()) return;
            for (int i = 0; i < GpuFills; ++i) cmd.FillBuffer(GpuBuffer, 0u, kBytes, std::uint32_t(i));
        }

        void ReleaseGpuBuffer()
        {
            auto& device = Kernel().GetDevice();
            if (GpuBuffer.IsValid()) device.DestroyBuffer(std::exchange(GpuBuffer, {}));
            if (ReadbackBuffer.IsValid()) device.DestroyBuffer(std::exchange(ReadbackBuffer, {}));
        }

        void SampleFrame()
        {
            if (Unavailable || !Kernel().GetDevice().IsOperational()) return;
            const auto pacing = Kernel().GetLastFramePacingDiagnostics(); // the previous frame, completed
            if (!Samples.empty() && !Samples.back().Pacing && pacing.Valid)
                Samples.back().Pacing = pacing;
            if (Samples.size() == Samples.capacity()) return; // bounded; the report checks the frame count
            Samples.push_back({.StageValue = int(Current), .Requests = Pending, .ControlValue = int(PendingControl),
                               .Module = Service != nullptr ? Service->Stats() : R::ClusteringModuleStats{}});
            PendingControl = Control::None;
        }

    public:
        static constexpr int GpuFills = 40; // sized in the technical test: a later frame then waits about 11 ms
    };

    Json PacingJson(const R::RuntimeFramePacingDiagnostics& p)
    {
        return {{"frame_index", p.FrameIndex}, {"total_us", p.TotalMicros}, {"command_drain_us", p.CommandDrainMicros},
                {"completion_drain_us", p.JobCompletionDrainMicros}, {"transfer_collect_us", p.TransferCollectMicros},
                {"gpu_transfer_drain_us", p.GpuTransferDrainMicros}, {"gpu_queue_record_us", p.GpuQueueRecordMicros},
                {"begin_frame_us", p.RenderBeginFrameMicros},
                // the rest of the frame, to place a wait the intervals above do not hold
                {"platform_begin_us", p.PlatformBeginMicros}, {"fixed_step_us", p.FixedStepMicros},
                {"variable_tick_us", p.VariableTickMicros}, {"pre_render_setup_us", p.PreRenderSetupMicros},
                {"render_extraction_us", p.RenderExtractionMicros}, {"render_prepare_us", p.RenderPrepareMicros},
                {"render_execute_us", p.RenderExecuteMicros}, {"render_end_frame_us", p.RenderEndFrameMicros},
                {"present_us", p.PresentMicros}, {"maintenance_us", p.MaintenanceMicros},
                {"release_render_world_us", p.ReleaseRenderWorldMicros}};
    }

    Json ModuleJson(const R::ClusteringModuleStats& s)
    {
        return {{"capture_init_us", s.GpuCaptureInitMicros}, {"advance_us", s.GpuAdvanceMicros},
                {"preview_publish_us", s.GpuPreviewPublishMicros}, {"accept_publish_us", s.GpuAcceptPublishMicros},
                {"held", s.GpuReplacementsHeld}, {"superseded", s.GpuReplacementsSuperseded},
                {"started", s.GpuReplacementsStarted}, {"iterations", s.GpuIterations}, {"previews", s.GpuPreviews},
                {"gpu_accepted", s.GpuRequestsAccepted}, {"gpu_fallbacks", s.GpuFallbacks}};
    }

    std::string Env(const char* name, const char* fallback = "")
    {
        const char* value = std::getenv(name);
        return value != nullptr ? value : fallback;
    }
}

TEST(ClusteringStallMeasurement, SliderPreviewMainThreadStall)
{
    const std::string out = Env("INTRINSIC_KMEANS_STALL_OUT");
    if (out.empty()) GTEST_SKIP() << "opt-in: set INTRINSIC_KMEANS_STALL_OUT";
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize()) GTEST_SKIP() << "GLFW could not initialize.";
    auto config = R::CreateReferenceEngineConfig();
    config.Window.Title = "Intrinsic K-Means stall measurement";
    config.Window.Width = 1280;
    config.Window.Height = 720;
    config.Window.Resizable = false;
    config.Render.EnableValidation = false;
    config.Render.EnableVSync = false;
    config.ReferenceScene.Enabled = false;
    auto app = std::make_unique<StallApp>();
    app->Arm = Env("INTRINSIC_KMEANS_STALL_ARM", "run");
    app->Points = std::uint32_t(std::stoul(Env("INTRINSIC_KMEANS_STALL_POINTS", "100000")));
    app->Timeout = std::chrono::seconds{std::stol(Env("INTRINSIC_KMEANS_STALL_TIMEOUT_S", "600"))};
    auto* appPtr = app.get();
    Intrinsic::Tests::RuntimeTestKernel engine(config, std::move(app));
    engine.EmplaceModule<R::SpatialIndexCache>();
    engine.EmplaceModule<R::AsyncWorkModule>();
    engine.EmplaceModule<R::SceneDocumentModule>(); // the command history the publication records into
    engine.EmplaceModule<R::ClusteringModule>();
    engine.EmplaceModule<R::StepRunsModule>();
    engine.Initialize();
    const auto ready = Extrinsic::Backends::Vulkan::GetVulkanDeviceOperationalInputs(&engine.GetDevice());
    const bool vulkan = ready.LogicalDeviceReady && ready.SwapchainReady && ready.CommandSyncReady;
    if (vulkan) engine.Run();
    // The last frame's pacing is read once the loop ended.
    if (!appPtr->Samples.empty() && !appPtr->Samples.back().Pacing && engine.GetLastFramePacingDiagnostics().Valid)
        appPtr->Samples.back().Pacing = engine.GetLastFramePacingDiagnostics();
    Json samples = Json::array();
    for (std::size_t i = 0; i < appPtr->Samples.size(); ++i)
    {
        const auto& s = appPtr->Samples[i];
        samples.push_back({{"stage", kStageNames[s.StageValue]}, {"requests", s.Requests},
                           {"control", s.ControlValue}, {"module", ModuleJson(s.Module)},
                           {"pacing", s.Pacing ? PacingJson(*s.Pacing) : Json(nullptr)}});
    }
    Json completions = Json::array();
    for (const auto& c : appPtr->Completions)
        completions.push_back({{"correlation", c.Correlation.Value}, {"status", std::string{R::ToString(c.Status)}},
                               {"backend", std::string{R::ToString(c.ActualBackend)}}, {"iterations", c.Iterations},
                               {"previews", c.GpuPreviews}, {"buffers_created", c.GpuWorkspaceBuffersCreated},
                               {"pipelines_created", c.GpuWorkspacePipelinesCreated},
                               {"input_upload_bytes", c.GpuInputUploadBytes}, {"message", c.Message}});
    const Json result{{"schema", "arch022.kmeans_stall.v1"}, {"arm", appPtr->Arm}, {"points", appPtr->Points},
                      {"cloud_hash", appPtr->CloudHash}, {"commit", Env("INTRINSIC_KMEANS_STALL_COMMIT")},
                      {"build", Env("INTRINSIC_KMEANS_STALL_BUILD")}, {"vulkan", vulkan},
                      {"unavailable", appPtr->Unavailable}, {"timed_out", appPtr->TimedOut},
                      {"done", appPtr->Done}, {"changes", appPtr->Changes},
                      {"final_correlation", appPtr->Final.Value}, {"gpu_fills", StallApp::GpuFills},
                      {"samples", samples}, {"completions", completions}};
    engine.Shutdown();
    std::ofstream file(out);
    file << result.dump() << '\n';
    ASSERT_TRUE(file.good()) << out;
}
