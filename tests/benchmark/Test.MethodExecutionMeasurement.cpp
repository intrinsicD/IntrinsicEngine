// Opt-in ARCH-024 measurements through real runtime method services. The campaign driver freezes inputs and budgets.
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <future>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include "RuntimeTestModule.hpp"

import Extrinsic.Backends.Vulkan;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Graphics.GpuPropertyResidency;
import Extrinsic.Graphics.PointLBVH;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.TransferQueue;
import Extrinsic.RHI.QueueAffinity;
import Extrinsic.Runtime.AsyncWorkModule;
import Extrinsic.Runtime.ClusteringModule;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.EngineConfigBoot;
import Extrinsic.Runtime.FramePacingDiagnostics;
import Extrinsic.Runtime.GeometryProperty.Types;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.ReferenceScene;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.SceneDocumentModule;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.StepRuns;
import Geometry.KMeans;
import Geometry.PointLBVH;
import Geometry.Properties;

namespace
{
    namespace R = Extrinsic::Runtime;
    namespace GS = Extrinsic::ECS::Components::GeometrySources;
    namespace RI = Extrinsic::RHI;
    namespace GK = Geometry::KMeans;
    namespace V = Extrinsic::Backends::Vulkan;
    using Json = nlohmann::json;
    using Clock = std::chrono::steady_clock;

    double Milliseconds(const Clock::time_point start, const Clock::time_point end = Clock::now())
    {
        return std::chrono::duration<double, std::milli>(end - start).count();
    }
    bool RecordFailure(Json& output, const std::string& why)
    {
        output["technical_passed"] = false;
        output["failure"] = why;
        return false;
    }
    bool Read(const std::string& path, Json& output)
    {
        std::ifstream file(path);
        if (!file)
            return RecordFailure(output, "Cannot read " + path);
        auto parsed = Json::parse(file, nullptr, false);
        if (parsed.is_discarded() || !parsed.is_object())
            return RecordFailure(output, "Invalid JSON object in " + path);
        output = std::move(parsed);
        return true;
    }
    bool ValidConfig(const Json& config)
    {
        if (!config.is_object())
            return false;
        for (const char* key : {"mode", "queue", "strategy"})
            if (!config.contains(key) || !config[key].is_string())
                return false;
        for (const char* key : {"points", "seed", "preview_every", "preview_buffers", "timeout_seconds"})
            if (!config.contains(key) || !config[key].is_number_unsigned())
                return false;
        if (config["mode"] != "reference" && config["mode"] != "kmeans" && config["mode"] != "queries" &&
            config["mode"] != "controls")
            return false;
        if (config["queue"] != "graphics" && config["queue"] != "separate")
            return false;
        if (config["strategy"] != "brute_force" && config["strategy"] != "lbvh")
            return false;
        if (config["points"].get<std::uint64_t>() < 7u || config["points"].get<std::uint64_t>() > 1000000u ||
            config["seed"].get<std::uint64_t>() > 0xffffffffu || config["preview_every"].get<std::uint64_t>() > 1u ||
            config["preview_buffers"].get<std::uint64_t>() < 2u ||
            config["preview_buffers"].get<std::uint64_t>() > 3u ||
            config["timeout_seconds"].get<std::uint64_t>() == 0u ||
            config["timeout_seconds"].get<std::uint64_t>() > 20u)
            return false;
        return (config["mode"] != "kmeans" && config["mode"] != "controls") ||
            (config.contains("reference") && config["reference"].is_string() &&
                !config["reference"].get<std::string>().empty());
    }
    std::vector<glm::vec3> Cloud(std::uint32_t count, std::uint32_t seed)
    {
        std::vector<glm::vec3> out(count);
        for (std::uint32_t i = 0; i < count; ++i)
        {
            glm::vec3 p{float(i % 7u) * 3.f, float((i / 7u) % 3u) * 2.f, 0.f};
            for (int axis = 0; axis < 3; ++axis)
            {
                seed = seed * 1664525u + 1013904223u;
                p[axis] += 2.f * float(seed >> 8u) / float(1u << 24u);
            }
            out[i] = p;
        }
        // Coincident peers exercise deterministic ID ties in the query workload.
        if (count > 3u)
            out[1] = out[0], out[3] = out[2];
        return out;
    }
    std::uint64_t CloudHash(std::span<const glm::vec3> points)
    {
        std::uint64_t hash = 1469598103934665603ull;
        for (const auto p : points)
            for (int axis = 0; axis < 3; ++axis)
                hash = (hash ^ std::bit_cast<std::uint32_t>(p[axis])) * 1099511628211ull;
        return hash;
    }
    Json Vectors(std::span<const glm::vec3> values)
    {
        Json result = Json::array();
        for (auto p : values)
            result.push_back({p.x, p.y, p.z});
        return result;
    }
    Json Reference(std::span<const glm::vec3> points, std::uint32_t seed, std::uint32_t iterations = 20u)
    {
        const auto result = GK::Cluster(points,
            {.ClusterCount = 7u, .MaxIterations = iterations, .Seed = seed, .Init = GK::Initialization::Random});
        if (!result)
            return {{"failure", "CPU reference refused the frozen cloud"}};
        return {{"cloud_hash", CloudHash(points)},
            {"labels", result->Labels},
            {"centroids", Vectors(result->Centroids)},
            {"iterations", result->Iterations},
            {"converged", result->Converged},
            {"inertia", result->Inertia}};
    }
    bool KMeansShape(const Json& value)
    {
        if (!value.is_object() || !value.contains("labels") || !value["labels"].is_array() || value["labels"].empty() ||
            !value["labels"][0].is_number_unsigned() || !value.contains("iterations") ||
            !value["iterations"].is_number_unsigned() || value["iterations"].get<std::uint64_t>() == 0u ||
            value["iterations"].get<std::uint64_t>() > 20u || !value.contains("converged") ||
            !value["converged"].is_boolean() || !value.contains("centroids") || !value["centroids"].is_array() ||
            value["centroids"].size() != 7u || !value.contains("inertia") || !value["inertia"].is_number() ||
            !std::isfinite(value["inertia"].get<double>()))
            return false;
        for (const auto& centroid : value["centroids"])
        {
            if (!centroid.is_array() || centroid.size() != 3u)
                return false;
            for (const auto& axis : centroid)
                if (!axis.is_number() || !std::isfinite(axis.get<double>()))
                    return false;
        }
        return true;
    }
    bool SameKMeans(const Json& expected, const Json& actual)
    {
        if (!KMeansShape(expected) || !KMeansShape(actual) || actual["labels"] != expected["labels"] ||
            actual["iterations"] != expected["iterations"] || actual["converged"] != expected["converged"])
            return false;
        for (std::size_t i = 0; i < 7u; ++i)
        {
            if (actual["centroids"][i].size() != 3u)
                return false;
            for (int axis = 0; axis < 3; ++axis)
            {
                if (!actual["centroids"][i][axis].is_number())
                    return false;
                const double x = actual["centroids"][i][axis].get<double>();
                if (!std::isfinite(x) || std::abs(x - expected["centroids"][i][axis].get<double>()) > 1e-5)
                    return false;
            }
        }
        const double inertia = actual["inertia"].get<double>();
        return std::isfinite(inertia) &&
            std::abs(inertia - expected["inertia"].get<double>()) <=
            1e-4 * std::max(1.0, expected["inertia"].get<double>());
    }
    bool CheckKMeansEvaluator(const Json& reference, std::string& failure)
    {
        if (!SameKMeans(reference, reference))
        {
            failure = "positive reference control rejected";
            return false;
        }
        auto wrong = reference;
        wrong["labels"][0] = (wrong["labels"][0].get<unsigned>() + 1u) % 7u;
        if (SameKMeans(reference, wrong))
        {
            failure = "wrong label passed quality gate";
            return false;
        }
        wrong = reference;
        wrong["centroids"][0][0] = 1000.0;
        if (SameKMeans(reference, wrong))
        {
            failure = "wrong centroid passed quality gate";
            return false;
        }
        wrong = reference;
        wrong["labels"] = Json::array();
        if (SameKMeans(reference, wrong))
        {
            failure = "empty result passed quality gate";
            return false;
        }
        wrong = reference;
        wrong["labels"] = std::vector<unsigned>(reference["labels"].size(), 0u);
        if (SameKMeans(reference, wrong))
        {
            failure = "collapsed result passed quality gate";
            return false;
        }
        return true;
    }
    Json Queue(const RI::TransferQueueDiagnostics& d)
    {
        return {{"requested", int(d.ComputeLane.Requested)},
            {"resolved", int(d.ComputeLane.Resolved)},
            {"demoted", d.ComputeLane.Demoted},
            {"shares_frame_compute", d.ComputeSharesFrameComputeQueue},
            {"shares_frame_lock", d.ComputeSharesFrameQueueLock}};
    }
    struct FrameSample
    {
        double Time{}, HookEndTime{};
        std::uint64_t ExpectedFrame{};
        std::optional<double> DelayBegin{}, DelayEnd{};
        unsigned Repeat{}, Steps{};
        std::string Phase{};
        R::RuntimeFramePacingDiagnostics Pacing{};
        R::SpatialIndexCacheStats Spatial{};
        Extrinsic::Graphics::GpuPropertyResidencyStats Residency{};
        RI::TransferQueueDiagnostics Transfer{};
        std::uint64_t Copies{};
        std::optional<std::uint32_t> Shown{};
    };
    bool InjectedDelayObserved(std::span<const FrameSample> frames)
    {
        const FrameSample* injected = nullptr;
        for (const auto& frame : frames)
            if (frame.Phase == "delay_control")
            {
                if (injected)
                    return false;
                injected = &frame;
            }
        if (!injected || !injected->DelayBegin || !injected->DelayEnd)
            return false;
        const auto& f = *injected;
        const double duration = *f.DelayEnd - *f.DelayBegin;
        return f.Pacing.Valid && f.Pacing.FrameIndex == f.ExpectedFrame && f.Time <= *f.DelayBegin &&
            *f.DelayEnd <= f.HookEndTime && duration >= 5.0 &&
            f.Pacing.VariableTickMicros >= static_cast<std::uint64_t>(duration * 1000.0) &&
            f.Pacing.TotalMicros >= f.Pacing.VariableTickMicros;
    }
    Json FrameJson(const FrameSample& f)
    {
        const auto& p = f.Pacing;
        return {{"time_ms", f.Time},
            {"repeat", f.Repeat},
            {"phase", f.Phase},
            {"steps", f.Steps},
            {"frame", p.FrameIndex},
            {"expected_frame", f.ExpectedFrame},
            {"owner_hook_end_ms", f.HookEndTime},
            {"delay_begin_ms", f.DelayBegin ? Json(*f.DelayBegin) : Json(nullptr)},
            {"delay_end_ms", f.DelayEnd ? Json(*f.DelayEnd) : Json(nullptr)},
            {"variable_tick_us", p.VariableTickMicros},
            {"total_us", p.TotalMicros},
            {"command_us", p.CommandDrainMicros},
            {"completion_us", p.JobCompletionDrainMicros},
            {"collect_us", p.TransferCollectMicros},
            {"gpu_drain_us", p.GpuTransferDrainMicros},
            {"record_us", p.GpuQueueRecordMicros},
            {"present_us", p.PresentMicros},
            {"preview_copies", f.Copies},
            {"shown_step", f.Shown ? Json(*f.Shown) : Json(nullptr)},
            {"resident_bytes", f.Residency.ResidentBytes},
            {"index_bytes", f.Spatial.IndexDeviceBytes},
            {"scratch_active_bytes", f.Spatial.ScratchActiveBytes},
            {"scratch_pending_bytes", f.Spatial.ScratchPendingBytes},
            {"scratch_idle_bytes", f.Spatial.ScratchIdleBytes},
            {"residency_upload_bytes", f.Residency.UploadBytes},
            {"residency_readback_bytes", f.Residency.ReadbackBytes},
            {"query_upload_bytes", f.Spatial.GpuQueryUploadBytes},
            {"readback_staged_bytes", f.Transfer.ReadbackBytesStaged},
            {"readback_ring_high_water_bytes", f.Transfer.ReadbackRingHighWaterBytes}};
    }

    class Measurement final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        Json Config, Output{{"runs", Json::array()}, {"events", Json::array()}};
        std::vector<glm::vec3> Points;
        std::vector<FrameSample> Frames;
        std::string Failure;
        bool Complete{};
        explicit Measurement(Json config)
            : Config(std::move(config)), Points(Cloud(Config.at("points"), Config.at("seed")))
        {
        }

        void Resolve() override
        {
            auto& engine = Kernel();
            Cache = engine.Services().Find<R::SpatialIndexCache>();
            Clustering = engine.Services().Find<R::ClusteringService>();
            Steps = engine.Services().Find<R::StepRunsService>();
            History = engine.Services().Find<R::EditorCommandHistory>();
            if (!Check(Cache && Clustering && Steps && History, "required runtime services absent"))
                return;
            auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
            // Same real triangle in every arm; the measured point cloud intentionally stays unrendered.
            Population = R::BootstrapReferenceScene(Extrinsic::Core::Config::ReferenceSceneSelector::Triangle, scene);
            if (auto* camera = engine.Services().Find<R::CameraControllerRegistry>())
                (void)camera->SetWorldSeed(engine.ActiveWorld(), Population.Camera);
            Entity = scene.Create();
            auto& properties = scene.Raw().emplace<GS::Vertices>(Entity).Properties;
            properties.Resize(Points.size());
            auto positions = properties.GetOrAdd<glm::vec3>("v:position");
            for (std::size_t i = 0; i < Points.size(); ++i)
                positions[i] = Points[i];
            Subscription = Clustering->SubscribeRunCompleted(
                [this](const auto& done)
                {
                    Completion = done;
                });
            Frames.reserve(10000u);
            Origin = Clock::now();
        }
        void Event(const std::string& name)
        {
            Output["events"].push_back({{"name", name}, {"time_ms", Milliseconds(Origin)}, {"repeat", Repeat}});
        }
        void Frame(double, double) override
        {
            if (!Failure.empty())
                return;
            FlushFrame();
            if (!Failure.empty())
                return;
            const auto previous = Kernel().GetLastFramePacingDiagnostics();
            const bool alreadyInjected = DelayInjected;
            FrameSample sample{.Time = Milliseconds(Origin),
                .ExpectedFrame = previous.Valid ? previous.FrameIndex + 1u : 0u,
                .Repeat = Repeat,
                .Steps = Last ? Last->Steps : 0u,
                .Phase = Completion ? "result_capture"
                    : Last          ? std::string(R::ToString(Last->Phase))
                                    : "idle",
                .Copies = Last ? Last->PreviewCopies : 0u,
                .Shown = Last ? Last->ShownStep : std::nullopt};
            Advance();
            sample.HookEndTime = Milliseconds(Origin);
            if (!alreadyInjected && DelayInjected)
            {
                sample.Phase = "delay_control";
                sample.DelayBegin = DelayBegin;
                sample.DelayEnd = DelayEnd;
            }
            if (Kernel().GetDevice().IsOperational() && Cache)
            {
                sample.Spatial = Cache->Stats();
                auto* residency = Cache->PropertyResidency();
                if (Check(residency != nullptr, "property residency observation absent"))
                    sample.Residency = residency->Stats();
                sample.Transfer = Kernel().GetDevice().GetTransferQueue().GetDiagnostics();
                PendingFrame = std::move(sample);
            }
        }
        void Shutdown() override
        {
            Abort.store(true);
            if (QueryFuture.valid())
                QueryFuture.wait();
            Session.reset();
            Workspace = {};
            if (Clustering)
                Clustering->Unsubscribe(Subscription);
        }
        void Finish()
        {
            if (QueryFuture.valid())
            {
                Abort.store(true);
                auto query = QueryFuture.get(); // Query observes Abort; join before serializing partial results.
                Output["runs"] = std::move(query["runs"]);
                (void)Check(false, query.value("failure", std::string{"query stopped before completion"}));
            }
            FlushFrame();
            // Preserve observed trajectories before a control/quality assertion can reject the cell.
            Json frames = Json::array();
            for (const auto& f : Frames)
                frames.push_back(FrameJson(f));
            Output["frames"] = std::move(frames);
            for (auto& [metadata, labels] : Captured)
            {
                metadata["labels"] = std::move(labels);
                Output["runs"].push_back(std::move(metadata));
            }
            Output["config"] = Config;
            Output["cloud_hash"] = CloudHash(Points);
            Output["points_preview"] = Vectors(std::span(Points).first(std::min<std::size_t>(Points.size(), 256u)));
            Output["complete"] = Complete;
            Output["failure"] = Failure;
            Output["schema"] = "intrinsic.method_execution.raw.v1";
            if (!Failure.empty())
                return;
            if (Config.at("mode") != "queries" && Complete)
            {
                if (!Check(InjectedDelayObserved(Frames),
                        "injected delay absent from its exact frame/variable-tick interval"))
                    return;
                if (!Check(std::ranges::any_of(Frames,
                               [](const auto& f)
                               {
                                   return f.Phase == "idle" && f.Steps == 0u;
                               }),
                        "null-work observation absent"))
                    return;
                Output["controls_passed"] = true;
                Json reference;
                if (!Read(Config.at("reference"), reference))
                {
                    (void)Check(false, reference["failure"].get<std::string>());
                    return;
                }
                if (!Check(reference.contains("cloud_hash") && reference["cloud_hash"].is_number_unsigned() &&
                            reference["cloud_hash"] == CloudHash(Points),
                        "reference cloud digest mismatch"))
                    return;
                std::string evaluationFailure;
                const bool evaluatorValid = CheckKMeansEvaluator(reference, evaluationFailure);
                if (!Check(evaluatorValid, evaluationFailure))
                    return;
                for (auto& run : Output["runs"])
                {
                    const auto expected = Config.at("mode") == "controls"
                        ? Reference(Points, Config.at("seed"), run.at("iterations"))
                        : reference;
                    if (!Check(SameKMeans(expected, run), "K-Means CPU quality comparison failed"))
                        return;
                    run["quality_passed"] = true;
                    double linf = 0;
                    for (unsigned i = 0; i < 7u; ++i)
                        for (unsigned axis = 0; axis < 3u; ++axis)
                            linf = std::max(linf,
                                std::abs(run["centroids"][i][axis].get<double>() -
                                    expected["centroids"][i][axis].get<double>()));
                    run["quality_error_linf"] = linf;
                    const auto& labels = run.at("labels");
                    std::uint64_t hash = 1469598103934665603ull;
                    Json sample = Json::array();
                    for (std::size_t i = 0; i < labels.size(); ++i)
                    {
                        hash = (hash ^ labels[i].get<std::uint32_t>()) * 1099511628211ull;
                        if (i < 256u)
                            sample.push_back(labels[i]);
                    }
                    run["labels_hash"] = hash;
                    run["labels_preview"] = std::move(sample);
                    run.erase("labels");
                }
            }
        }

    private:
        R::SpatialIndexCache* Cache{};
        R::ClusteringService* Clustering{};
        R::StepRunsService* Steps{};
        R::EditorCommandHistory* History{};
        Extrinsic::ECS::EntityHandle Entity{};
        R::ReferenceScenePopulation Population{};
        R::KernelEventSubscription Subscription{};
        R::CommandCorrelationId Correlation{};
        std::optional<R::KMeansRunCompleted> Completion;
        std::vector<std::pair<Json, std::vector<std::uint32_t>>> Captured;
        R::SpatialIndexWorkspace Workspace{};
        std::shared_ptr<R::SpatialQuerySession> Session;
        std::future<Json> QueryFuture;
        std::atomic<bool> Abort{false};
        Clock::time_point Origin{}, Requested{}, Ready{}, Accepted{}, Computed{}, ControlRequested{};
        double DelayBegin{}, DelayEnd{};
        unsigned Repeat{}, IdleFrames{}, ControlStage{}, PausedSteps{};
        bool Submitted{}, Running{}, Accepting{}, DelayInjected{};
        std::optional<R::StepRunObservation> Last;
        std::optional<FrameSample> PendingFrame;
        bool Check(bool okay, const std::string& why)
        {
            // Owner-thread callbacks propagate failure by returning. Never unwind across the engine's no-exception boundary.
            if (okay)
                return true;
            if (Failure.empty())
                Failure = why.empty() ? "measurement check failed" : why;
            (void)RecordFailure(Output, Failure);
            Abort.store(true);
            Kernel().RequestExit();
            return false;
        }
        void FlushFrame()
        {
            if (!PendingFrame)
                return;
            const auto pacing = Kernel().GetLastFramePacingDiagnostics();
            if (!pacing.Valid)
                return;
            if (!Check(pacing.FrameIndex == PendingFrame->ExpectedFrame,
                    "frame sample paired with a different pacing interval"))
                return;
            if (!Check(Frames.size() < Frames.capacity(), "frame sample bound exceeded"))
                return;
            PendingFrame->Pacing = pacing;
            Frames.push_back(std::move(*PendingFrame));
            PendingFrame.reset();
        }
        std::size_t UndoBefore{};

        void Advance()
        {
            auto& engine = Kernel();
            if (!Check(Milliseconds(Origin) < Config.at("timeout_seconds").get<double>() * 1000.0,
                    "process work deadline reached"))
                return;
            if (!engine.GetDevice().IsOperational())
                return;
            const auto queue = engine.GetDevice().GetTransferQueue().GetDiagnostics();
            const bool graphics = Config.at("queue") == "graphics";
            if (!Check(queue.ComputeLane.Resolved ==
                            (graphics ? RI::QueueAffinity::Graphics : RI::QueueAffinity::AsyncCompute) &&
                        !queue.ComputeLane.Demoted,
                    "requested queue placement unavailable"))
                return;
            Output["queue"] = Queue(queue);
            const auto& render = engine.GetRenderer().GetLastRenderGraphStats();
            const bool surface = std::ranges::any_of(render.CommandRecords.Passes,
                [](const auto& p)
                {
                    return p.Name == "SurfacePass" &&
                        p.Status == Extrinsic::Graphics::RenderCommandPassStatus::Recorded;
                });
            auto* extraction = engine.Services().Find<R::RenderExtractionCache>();
            if (!Check(extraction != nullptr, "render extraction absent"))
                return;
            if (!surface || extraction->GetLastStats().SubmittedVisualizationCount == 0u)
            {
                if (!Check(++IdleFrames < 120u, "reference triangle never reached recorded SurfacePass"))
                    return;
                return;
            }
            Output["rendered_reference_surface"] = true;
            if (Config.at("mode") == "queries")
            {
                Queries();
                return;
            }
            // Idle samples and a known tagged delay establish the pacing observation before work.
            if (!DelayInjected)
            {
                Event("known_owner_delay_5ms");
                DelayBegin = Milliseconds(Origin);
                const auto until = Clock::now() + std::chrono::milliseconds(5);
                while (Clock::now() < until)
                {
                }
                DelayEnd = Milliseconds(Origin);
                DelayInjected = true;
                IdleFrames = 0u;
                return;
            }
            if (!Submitted && IdleFrames++ < 3u)
                return;
            if (!Submitted)
            {
                Start();
                return;
            }
            if (Completion)
            {
                Capture();
                return;
            }
            const auto id = Clustering->StepRun(Correlation);
            Last = id ? Steps->Observe(id) : std::nullopt;
            if (!Last)
                return;
            if (!Check(Last->Phase != R::StepRunPhase::Failed && Last->Phase != R::StepRunPhase::Discarded,
                    "method ended without publication: " + Last->Diagnostic))
                return;
            const bool settled = !Last->SegmentInFlight && !Last->WorkerInFlight;
            if (!Running && Last->Phase == R::StepRunPhase::Ready && settled)
            {
                Ready = Clock::now();
                Event("ready");
                if (!Control(R::StepRunAction::Run))
                    return;
                Running = true;
                return;
            }
            if (Config.at("mode") == "controls" && Running && !Accepting)
            {
                Controls(settled);
                return;
            }
            if (!Accepting && Last->Phase == R::StepRunPhase::Paused && settled && Last->End != R::StepRunEnd::None &&
                Clustering->GpuRun(Correlation).ReadyToAccept)
            {
                Computed = Clock::now();
                Accept();
            }
        }
        bool Control(R::StepRunAction action)
        {
            if (!Check(Last.has_value(), "control without observation"))
                return false;
            std::string refusal;
            const bool accepted = Steps->Control(Last->Id, action, 1u, refusal);
            if (!Check(accepted, "run control refused: " + refusal))
                return false;
            Event(std::string("control_") +
                (action == R::StepRunAction::Run            ? "run"
                        : action == R::StepRunAction::Pause ? "pause"
                        : action == R::StepRunAction::Step  ? "step"
                        : action == R::StepRunAction::Stop  ? "stop"
                                                            : "accept"));
            return true;
        }
        void Start()
        {
            R::KMeansParameters parameters{.ClusterCount = 7u,
                .Seed = Config.at("seed"),
                .Initialization = R::KMeansInitialization::Random,
                .Execution = {.IterationCap = 20u,
                    .PreviewEvery = Config.at("preview_every"),
                    .PreviewIntervalMs = 0u,
                    .AutoAccept = R::StepRunAutoAccept::Never,
                    .Start = R::StepRunStart::Ready,
                    .PreviewBuffers = Config.at("preview_buffers")}};
            Requested = Clock::now();
            Event("request");
            UndoBefore = History->Snapshot().UndoCount;
            Correlation = Clustering->RunKMeans({.StableEntityId = R::SelectionController::ToStableEntityId(Entity),
                .Properties = R::MakeKMeansPropertyRefs(R::GeometryElementDomain::PointCloudPoint),
                .Parameters = parameters,
                .Backend = R::ClusteringBackend::VulkanCompute});
            Submitted = true;
            Running = Accepting = false;
            Last.reset();
        }
        void Accept()
        {
            Accepted = Clock::now();
            Event("accept_request");
            if (!Control(R::StepRunAction::Accept))
                return;
            Accepting = true;
        }
        void Controls(bool settled)
        {
            switch (ControlStage)
            {
            case 0:
                if (!Check(Last->End == R::StepRunEnd::None, "pause trigger missed active work"))
                    return;
                if (Last->WorkerInFlight && Last->Steps >= 1u)
                {
                    ControlRequested = Clock::now();
                    if (!Control(R::StepRunAction::Pause))
                        return;
                    ++ControlStage;
                }
                break;
            case 1:
                if (settled && Last->Phase == R::StepRunPhase::Paused)
                {
                    Output["pause_ms"] = Milliseconds(ControlRequested);
                    Event("pause_settled");
                    PausedSteps = Last->Steps;
                    ControlRequested = Clock::now();
                    if (!Control(R::StepRunAction::Step))
                        return;
                    ++ControlStage;
                }
                break;
            case 2:
                if (settled && Last->Phase == R::StepRunPhase::Paused && Last->Steps > PausedSteps)
                {
                    if (!Check(Last->Steps == PausedSteps + 1u, "Step1 did not settle after one step"))
                        return;
                    Output["step_ms"] = Milliseconds(ControlRequested);
                    Event("step_settled");
                    if (!Control(R::StepRunAction::Run))
                        return;
                    ++ControlStage;
                }
                break;
            case 3:
                if (!Check(Last->End == R::StepRunEnd::None, "stop trigger missed active work"))
                    return;
                if (Last->WorkerInFlight)
                {
                    ControlRequested = Clock::now();
                    if (!Control(R::StepRunAction::Stop))
                        return;
                    ++ControlStage;
                }
                break;
            case 4:
                if (settled && Last->Phase == R::StepRunPhase::Paused && Clustering->GpuRun(Correlation).ReadyToAccept)
                {
                    if (!Check(Last->End == R::StepRunEnd::Stopped, "stop did not settle with Stopped reason"))
                        return;
                    Output["stop_ms"] = Milliseconds(ControlRequested);
                    Computed = Clock::now();
                    Event("stop_settled");
                    Accept();
                }
                break;
            }
        }
        void Capture()
        {
            const auto published = Clock::now();
            if (!Check(Accepting && Completion->Succeeded() &&
                        Completion->ActualBackend == R::ClusteringBackend::VulkanCompute && !Completion->FellBackToCpu,
                    "GPU completion/publication failed: " + Completion->Message))
                return;
            if (!Check(
                    History->Snapshot().UndoCount == UndoBefore + 1u, "Accept did not add exactly one history entry"))
                return;
            const auto& properties =
                Kernel().Worlds().Get(Kernel().ActiveWorld())->Raw().get<GS::Vertices>(Entity).Properties;
            const auto labels = properties.Get<std::uint32_t>("p:kmeans_label");
            if (!Check(bool(labels) && labels.Vector().size() == Points.size(), "accepted labels are missing"))
                return;
            Json metrics = Json::array();
            if (Last && Last->Metrics)
                for (const auto& m : Last->Metrics->Records())
                    metrics.push_back({{"step", m.Step}, {"values", m.Values}});
            Json metadata{{"repeat", Repeat},
                {"warmup", Repeat == 0u && Config.at("mode") != "controls"},
                {"request_ready_ms", Milliseconds(Requested, Ready)},
                {"ready_computed_ms", Milliseconds(Ready, Computed)},
                {"accept_ms", Milliseconds(Accepted, published)},
                {"ready_accepted_ms", Milliseconds(Ready, published)},
                {"request_accepted_ms", Milliseconds(Requested, published)},
                {"centroids", Vectors(Completion->Centroids)},
                {"iterations", Completion->Iterations},
                {"converged", Completion->Converged},
                {"inertia", Completion->Inertia},
                {"metrics", metrics},
                {"metric_names", Last ? Json(Last->MetricNames) : Json::array()},
                {"input_upload_bytes", Completion->GpuInputUploadBytes},
                {"input_cache_hits", Completion->GpuInputCacheHits},
                {"cpu_stage_upload_bytes", Completion->CpuStageUploadBytes},
                {"cpu_stage_readback_bytes", Completion->CpuStageReadbackBytes},
                {"submissions", Completion->GpuSubmissions},
                {"previews", Completion->GpuPreviews}};
            Captured.emplace_back(std::move(metadata), labels.Vector());
            Event("published");
            if (!Check(History->Undo().Succeeded(), "output reset Undo failed"))
                return;
            History->ClearHistory();
            // Capturing output/undo are instrumentation; the raw event marks their frame instead of hiding its cost.
            Event("capture_and_output_reset_finished");
            Completion.reset();
            Last.reset();
            Submitted = false;
            IdleFrames = 0;
            if (++Repeat == (Config.at("mode") == "controls" ? 1u : 4u))
            {
                Complete = true;
                Kernel().RequestExit();
            }
        }
        void Queries();
    };

    void Measurement::Queries()
    {
        if (!Submitted)
        {
            std::vector<glm::vec3> queries(256u);
            std::vector<std::uint32_t> excluded(256u);
            for (std::size_t i = 0; i < queries.size(); ++i)
            {
                queries[i] = Points[i % Points.size()];
                excluded[i] = std::uint32_t(i % Points.size());
            }
            const auto start = Clock::now();
            Workspace = Cache->CreateWorkspace(Points);
            if (!Check(Workspace.Ready(), Workspace.Diagnostic))
                return;
            std::string why;
            Session = Cache->PrepareQuerySession(Workspace.Handle,
                {.Strategy = Config.at("strategy") == "brute_force"
                        ? Extrinsic::Graphics::PointQueryStrategy::BruteForce
                        : Extrinsic::Graphics::PointQueryStrategy::Lbvh,
                    .Capacity = 16u,
                    .PageQueries = 16u,
                    .Queries = queries,
                    .Excluded = excluded},
                why);
            if (!Check(bool(Session), why))
                return;
            Output["preparation_call_ms"] = Milliseconds(start);
            Event("query_prepared");
            QueryFuture = std::async(std::launch::async,
                [this, queries = std::move(queries), excluded = std::move(excluded)]
                {
                    // The worker owns this result until the future is joined; failures retain completed and partial sweeps.
                    Json result{{"runs", Json::array()}};
                    std::vector<std::vector<Geometry::PointLBVH::Neighbor>> expected;
                    for (std::size_t i = 0; i < queries.size(); ++i)
                    {
                        if (Abort.load())
                        {
                            (void)RecordFailure(result, "query reference preparation aborted");
                            return result;
                        }
                        expected.push_back(
                            Geometry::PointLBVH::KNearestReference(Points, queries[i], 16u, excluded[i]));
                    }
                    const auto valid = [&](const R::SpatialNearestBatch& page, unsigned first)
                    {
                        if (page.Capacity != 16u || page.Counts.size() != 16u || page.Neighbors.size() != 256u)
                            return false;
                        for (unsigned row = 0; row < 16u; ++row)
                        {
                            if (page.Counts[row] != 16u || expected[first + row].size() != 16u)
                                return false;
                            for (unsigned k = 0; k < 16u; ++k)
                            {
                                const auto& a = page.Neighbors[row * 16u + k];
                                const auto& b = expected[first + row][k];
                                if (a.Index != b.Index || !std::isfinite(a.SquaredDistance) ||
                                    std::abs(a.SquaredDistance - b.SquaredDistance) > 1e-5f)
                                    return false;
                            }
                        }
                        return true;
                    };
                    for (unsigned repeat = 0; repeat < 4u; ++repeat)
                    {
                        double elapsed = 0, distanceError = 0;
                        result["runs"].push_back({{"repeat", repeat},
                            {"warmup", repeat == 0u},
                            {"completed_queries", 0u},
                            {"runtime_ms", 0.},
                            {"quality_passed", false},
                            {"page_samples", Json::array()}});
                        auto& sweep = result["runs"].back();
                        for (unsigned first = 0; first < 256u; first += 16u)
                        {
                            R::SpatialNearestBatch page;
                            const auto began = Clock::now();
                            const auto status = Session->Query(first, 16u, -1.f, nullptr, Abort, page);
                            const auto pageMs = Milliseconds(began);
                            elapsed += pageMs;
                            sweep["runtime_ms"] = elapsed;
                            sweep["completed_queries"] = status == R::SpatialPageStatus::Ready ? first + 16u : first;
                            sweep["page_samples"].push_back({{"first", first},
                                {"time_ms", Milliseconds(Origin)},
                                {"runtime_ms", pageMs},
                                {"completed_queries", sweep["completed_queries"]},
                                {"upload_bytes", Session->UploadBytes()},
                                {"readback_bytes", Session->ReadbackBytes()}});
                            if (status != R::SpatialPageStatus::Ready)
                            {
                                (void)RecordFailure(result, "GPU query page failed: " + page.Diagnostic);
                                return result;
                            }
                            if (!valid(page, first))
                            {
                                (void)RecordFailure(result, "query differs from CPU reference");
                                return result;
                            }
                            for (unsigned row = 0; row < 16u; ++row)
                                for (unsigned k = 0; k < 16u; ++k)
                                    distanceError = std::max(distanceError,
                                        double(std::abs(page.Neighbors[row * 16u + k].SquaredDistance -
                                            expected[first + row][k].SquaredDistance)));
                            if (repeat == 0u && first == 0u)
                            {
                                auto corrupt = page;
                                corrupt.Neighbors[0].Index = ~0u;
                                if (valid(corrupt, first))
                                {
                                    (void)RecordFailure(result, "wrong query ID passed evaluator");
                                    return result;
                                }
                                corrupt = page;
                                corrupt.Neighbors[0].SquaredDistance += 1.f;
                                if (valid(corrupt, first))
                                {
                                    (void)RecordFailure(result, "wrong query distance passed evaluator");
                                    return result;
                                }
                                corrupt = {};
                                if (valid(corrupt, first))
                                {
                                    (void)RecordFailure(result, "empty query passed evaluator");
                                    return result;
                                }
                            }
                        }
                        sweep["quality_passed"] = true;
                        sweep["quality_error_linf"] = distanceError;
                        sweep["pages"] = Session->Pages();
                        sweep["upload_bytes"] = Session->UploadBytes();
                        sweep["readback_bytes"] = Session->ReadbackBytes();
                    }
                    return result;
                });
            Submitted = true;
            return;
        }
        if (QueryFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        {
            auto query = QueryFuture.get();
            Output["runs"] = std::move(query["runs"]);
            if (!Check(!query.contains("failure"), query.value("failure", std::string{})))
                return;
            Event("query_completed");
            Complete = true;
            Kernel().RequestExit();
        }
    }
}

TEST(MethodExecutionMeasurement, FailureStatusPreservesObservations)
{
    const Json frames = Json::array({{{"frame", 7u}, {"total_us", 5100u}}});
    const Json runs = Json::array({{{"repeat", 0u}, {"quality_passed", false}}});
    Json output{{"frames", frames}, {"runs", runs}};
    EXPECT_FALSE(RecordFailure(output, "quality rejected"));
    EXPECT_FALSE(output["technical_passed"].get<bool>());
    EXPECT_EQ(output["failure"], "quality rejected");
    EXPECT_EQ(output["frames"], frames);
    EXPECT_EQ(output["runs"], runs);
}

TEST(MethodExecutionMeasurement, InvalidJsonShapesFailWithoutUnwinding)
{
    Json config{{"mode", "kmeans"},
        {"queue", "graphics"},
        {"strategy", "lbvh"},
        {"points", 100000u},
        {"seed", 11u},
        {"preview_every", 0u},
        {"preview_buffers", 3u},
        {"timeout_seconds", 20u},
        {"reference", "fixture.json"}};
    EXPECT_TRUE(ValidConfig(config));
    auto wrong = config;
    wrong["points"] = "100000";
    EXPECT_FALSE(ValidConfig(wrong));
    wrong = config;
    wrong.erase("reference");
    EXPECT_FALSE(ValidConfig(wrong));
    wrong = config;
    wrong["points"] = 0u;
    EXPECT_FALSE(ValidConfig(wrong));
    EXPECT_FALSE(ValidConfig(Json::parse("{broken", nullptr, false)));

    std::array<glm::vec3, 7> centroids{};
    centroids.fill(glm::vec3{0.f});
    Json reference{{"labels", std::vector<std::uint32_t>{0u, 1u, 2u, 3u, 4u, 5u, 6u}},
        {"centroids", Vectors(centroids)},
        {"iterations", 1u},
        {"converged", false},
        {"inertia", 0.}};
    std::string failure;
    EXPECT_TRUE(CheckKMeansEvaluator(reference, failure)) << failure;
    wrong = reference;
    wrong["iterations"] = "one";
    EXPECT_FALSE(SameKMeans(reference, wrong));
    wrong = reference;
    wrong["centroids"][0][0] = "poison";
    EXPECT_FALSE(SameKMeans(wrong, reference));
}

TEST(MethodExecutionMeasurement, DelayObservationRejectsUnrelatedSlowFrames)
{
    FrameSample injected{.Time = 10.,
        .HookEndTime = 20.,
        .ExpectedFrame = 8u,
        .DelayBegin = 11.,
        .DelayEnd = 16.,
        .Phase = "delay_control"};
    injected.Pacing.Valid = true;
    injected.Pacing.FrameIndex = 8u;
    injected.Pacing.VariableTickMicros = 6000u;
    injected.Pacing.TotalMicros = 8000u;
    std::array frames{injected};
    EXPECT_TRUE(InjectedDelayObserved(frames));
    frames[0].Phase = "idle";
    EXPECT_FALSE(InjectedDelayObserved(frames)); // An unrelated earlier slow frame cannot satisfy the probe.
    frames[0] = injected;
    frames[0].DelayEnd = 11.001;
    EXPECT_FALSE(InjectedDelayObserved(frames)); // Removing the delay while retaining the tag must fail.
    auto unrelated = injected;
    unrelated.Phase = "idle";
    unrelated.ExpectedFrame = unrelated.Pacing.FrameIndex = 7u;
    const std::array earlierSlowAndMissingDelay{unrelated, frames[0]};
    EXPECT_FALSE(InjectedDelayObserved(earlierSlowAndMissingDelay));
    frames[0] = injected;
    frames[0].Pacing.FrameIndex = 7u;
    EXPECT_FALSE(InjectedDelayObserved(frames));
    frames[0] = injected;
    frames[0].Pacing.VariableTickMicros = 1000u;
    EXPECT_FALSE(InjectedDelayObserved(frames)); // Total time alone is not its containing interval.
    frames[0] = injected;
    frames[0].DelayBegin = 9.;
    EXPECT_FALSE(InjectedDelayObserved(frames));
}

TEST(MethodExecutionMeasurement, FrozenCell)
{
    const char* input = std::getenv("INTRINSIC_METHOD_EXECUTION_CONFIG");
    const char* output = std::getenv("INTRINSIC_METHOD_EXECUTION_OUT");
    if (!input || !output)
        GTEST_SKIP() << "Opt-in: use tools/benchmark/method_execution_campaign.py";
    Json result;
    const auto execute = [&]() -> bool
    {
        Json config;
        if (!Read(input, config))
        {
            result = std::move(config);
            return false;
        }
        if (!ValidConfig(config))
            return RecordFailure(result, "Invalid frozen cell configuration");
        if (config.at("mode") == "reference")
        {
            result = Reference(Cloud(config.at("points"), config.at("seed")), config.at("seed"));
            if (result.contains("failure"))
                return false;
            std::string evaluationFailure;
            if (!CheckKMeansEvaluator(result, evaluationFailure))
                return RecordFailure(result, evaluationFailure);
        }
        else
        {
            if (!Extrinsic::Platform::Backends::Glfw::CanInitialize())
                return RecordFailure(result, "GLFW unavailable");
            auto boot = R::CreateReferenceEngineConfig();
            boot.Window.Title = "Intrinsic method execution measurement";
            boot.Window.Width = 640;
            boot.Window.Height = 480;
            boot.Window.Resizable = false;
            boot.Render.EnableVSync = false;
            boot.Render.EnableValidation = true;
            boot.Render.MethodComputeOnGraphicsQueue = config.at("queue") == "graphics";
            auto app = std::make_unique<Measurement>(config);
            auto* observed = app.get();
            Intrinsic::Tests::RuntimeTestKernel engine(boot, std::move(app));
            engine.EmplaceModule<R::SpatialIndexCache>();
            engine.EmplaceModule<R::AsyncWorkModule>();
            engine.EmplaceModule<R::SceneDocumentModule>();
            engine.EmplaceModule<R::ClusteringModule>();
            engine.EmplaceModule<R::StepRunsModule>();
            const auto validation = V::GetVulkanOperationalDiagnosticsSnapshot().VulkanValidationErrorCount;
            engine.Initialize();
            if (observed->Failure.empty())
                engine.Run();
            observed->Finish();
            result = observed->Output;
            engine.Shutdown();
            if (!result.value("complete", false) || !result.value("failure", std::string{}).empty())
                return RecordFailure(result, "method cell did not complete: " + result.value("failure", std::string{}));
            if (V::GetVulkanOperationalDiagnosticsSnapshot().VulkanValidationErrorCount != validation)
                return RecordFailure(result, "Vulkan validation error during cell");
        }
        return true;
    };
    const bool passed = execute();
    result["technical_passed"] = passed;
    std::ofstream file(output);
    file << result.dump() << '\n';
    ASSERT_TRUE(file.good());
    ASSERT_TRUE(result.value("technical_passed", false)) << result.value("failure", "unknown failure");
}
