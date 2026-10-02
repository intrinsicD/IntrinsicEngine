// RUNTIME-311 (RUNTIME-279 follow-up): a Vulkan property-smoothing transaction cancelled through
// the editor job surface (the agent's `jobs_cancel`) while one of its jobs is parked in
// AwaitingApply: first the chunked Run, then the Accept stage while its front readback is
// pending. Each cancel ends the transaction once as stale, the previous CPU output, its revision
// and the canonical slot are unchanged, and no ring is left. On the device it also pins that a
// duplicate start answers Pending without a handle or callback and that the Accept stage carries
// the run (`EditorJobIdentity::Run`).
#include "RuntimeTestModule.hpp"
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <entt/entity/entity.hpp>
#include <glm/glm.hpp>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.RHI.Device;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.Module;
import Extrinsic.Core.Error;
import Extrinsic.Runtime.EngineConfigBoot;
import Extrinsic.Runtime.AsyncWorkModule;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.WorldRegistry;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.MeshFieldOperations;
import Extrinsic.Runtime.GeometryProcessingOperations;
import Extrinsic.Runtime.EditorProcessing;
import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.EditorWorkspaceAttachment;
import Extrinsic.Runtime.EditorWorkspaceSnapshots;
import Extrinsic.Runtime.AgentOperations;
import Extrinsic.Runtime.GpuPropertyBinding;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Graphics.GpuPropertyResidency;
import Extrinsic.Core.Config.Engine;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Component.StableId;
import Geometry.Properties;
import Geometry.Smoothing;

namespace
{
    namespace Runtime = Extrinsic::Runtime;
    namespace GS = Extrinsic::ECS::Components::GeometrySources;
    namespace S = Geometry::Smoothing;
    using Phase = Runtime::EditorGpuTransactionPhase;
    using Domain = Runtime::GeometryElementDomain;
    using K = Geometry::PropertyValueKind;
    using Json = nlohmann::json;

    struct Shutdown
    {
        Runtime::Engine& Engine;
        ~Shutdown() { Engine.Shutdown(); }
    };

    class CancelApp final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        // Accept is issued from Maintenance, after this frame's transfers were collected: its
        // readback lands at the earliest at the next collection, after the next completion
        // drain parked the Accept job, so the next UiBuild frame sees it in AwaitingApply.
        [[nodiscard]] Extrinsic::Core::Result OnRegister(Runtime::EngineSetup& setup) override
        {
            if (auto registered = RuntimeTestModule::OnRegister(setup); !registered) return registered;
            return setup.RegisterFrameHook(Runtime::FramePhase::Maintenance,
                                           [this](Runtime::RuntimeFrameHookContext&) { MaintenanceFrame(); });
        }

        void MaintenanceFrame()
        {
            if (Step != 4 || !Run || !Commands.IsBound()) return;
            const auto snapshot = Runtime::SnapshotEditorPropertySmoothing(Commands, Run);
            if (snapshot.Phase == Phase::Running) return;
            if (snapshot.Phase != Phase::ReadyToAccept) return Fail("explicit run ended: " + snapshot.Result.Message);
            Delivered.clear();
            const auto accepted = Runtime::AcceptEditorPropertySmoothing(Commands, Run,
                [this](Runtime::EditorPropertySmoothingResult r) { Delivered.push_back(std::move(r)); });
            if (accepted.Status != Runtime::EditorCommandStatus::Pending) return Fail("Accept refused: " + accepted.Message);
            const auto job = Job("Vulkan property smoothing accept");
            if (!job) return Fail("the Accept job is not an editor job");
            AcceptToken = job->Token;
            EXPECT_EQ(job->Identity.Run, RunToken) << "the Accept stage joins the run";
            ++Step;
        }

        void Resolve() override
        {
            Started = std::chrono::steady_clock::now();
            auto* scene = Kernel().Worlds().Get(Kernel().ActiveWorld());
            auto& raw = scene->Raw();
            Entity = scene->Create();
            raw.emplace<Extrinsic::ECS::Components::StableId>(Entity, Extrinsic::ECS::Components::StableId{311u, 1u});
            auto& vertices = raw.emplace<GS::Vertices>(Entity).Properties;
            constexpr std::size_t kSide = 16;
            vertices.Resize(kSide * kSide);
            auto positions = vertices.GetOrAdd<glm::vec3>("v:position", glm::vec3{0.f});
            auto signal = vertices.GetOrAdd<float>("signal", 0.f);
            auto previous = vertices.GetOrAdd<float>("smooth", 0.f);
            for (std::size_t i = 0; i < kSide * kSide; ++i)
            {
                positions[i] = {float(i % kSide) / float(kSide), float(i / kSide) / float(kSide), 0.f};
                signal[i] = std::sin(7.f * positions[i].x) + std::cos(5.f * positions[i].y);
                previous[i] = 42.f + float(i); // the previous output every cancel must leave alone
            }
            Id = Runtime::SelectionController::ToStableEntityId(Entity);
            Config.Input = {Domain::PointCloudPoint, "signal", K::Float};
            Config.Output = {Domain::PointCloudPoint, "smooth", K::Float};
            Config.Positions = {Domain::PointCloudPoint, "v:position", K::Vec3};
            Config.Weight = S::PropertyWeight::Gaussian;
            Config.Neighbors = 6;
            Config.SpatialSigma = 0.2;
            Config.Backend = Runtime::PropertySmoothingBackend::Vulkan;
            // A chunked implicit solve keeps the Run job parked across many frames.
            Implicit = Config;
            Implicit.Filter.Method = S::PropertyFilter::Implicit;
            Implicit.Filter.Solver = S::PropertySolver::ConjugateGradient;
            Implicit.Filter.TimeStep = 0.5;
            Implicit.Filter.Iterations = 600;
            Implicit.Filter.SolverTolerance = 1e-10;
            Implicit.Filter.MaxSolverIterations = 500;
            Explicit = Config;
            Explicit.Filter.Method = S::PropertyFilter::Averaging;
            Explicit.Filter.Iterations = 3;
            Runtime::RegisterEditorAgentOperations(Registry);
            Attachment.Attach(Kernel().Worlds(), Kernel().Services());
        }

        void Fail(const std::string& message)
        {
            ADD_FAILURE() << message << " (step " << Step << ")";
            Kernel().RequestExit();
        }

        Geometry::PropertySet& Props()
        {
            return Kernel().Worlds().Get(Kernel().ActiveWorld())->Raw().get<GS::Vertices>(Entity).Properties;
        }
        std::vector<float> Output() { return std::as_const(Props()).Get<float>("smooth").Vector(); }
        Extrinsic::Graphics::GpuPropertyResidency* Residency()
        {
            auto* cache = Kernel().Services().Find<Runtime::SpatialIndexCache>();
            return cache ? cache->PropertyResidency() : nullptr;
        }
        Extrinsic::Graphics::GpuPropertyKey Key() { return Runtime::MakeGpuPropertyKey(Kernel().ActiveWorld(), Entity, Config.Output); }

        // The editor job of this output named `name`, if the surface lists one.
        std::optional<Runtime::EditorJobRecord> Job(const std::string_view name)
        {
            for (const auto& job : Runtime::GetEditorJobs(Commands))
                if (job.Name == name && job.Identity.OutputName == "smooth") return job;
            return std::nullopt;
        }
        // Cancels `token` through the agent's editor job surface.
        bool CancelThroughAgent(const Runtime::JobToken token)
        {
            const Runtime::AgentOperationContext context{.Attachment = &Attachment, .Jobs = &Kernel().Jobs()};
            const std::string text = std::to_string(token.Index) + ":" + std::to_string(token.Generation);
            const auto out = Runtime::InvokeAgentOperation(Registry, "jobs_cancel", context, Json{{"token", text}}.dump(), false);
            const auto parsed = Json::parse(out.Text, nullptr, false);
            EXPECT_FALSE(out.IsError) << out.Text;
            return !out.IsError && parsed.is_object() && parsed.value("status", "") == "requested";
        }
        // The previous output, its revision and the canonical slot are what they were; no ring.
        void ExpectPreviousOutputKept(const char* what)
        {
            EXPECT_EQ(Output(), Before) << what;
            EXPECT_EQ(Props().FindPropertyRevision("smooth"), BeforeRevision) << what;
            auto* residency = Residency();
            ASSERT_NE(residency, nullptr);
            EXPECT_FALSE(residency->HasRing(Key())) << what << ": the cancelled run left its ring";
            if (const auto front = residency->Front(Key()))
                EXPECT_EQ(front->Revision, *BeforeRevision) << what << ": the canonical slot moved";
        }

        void Frame(double, double) override
        {
            if (std::chrono::steady_clock::now() - Started > std::chrono::seconds(120)) { TimedOut = true; Fail("cancel smoke timed out"); return; }
            if (!Kernel().GetDevice().IsOperational() || !Residency()) return;
            if (!Runtime::PrepareEditorWorkspaceSnapshotFrame(Attachment)) return;
            Commands = Runtime::PrepareEditorProcessingCommands(Attachment);
            if (!Commands.IsBound()) return;
            const auto snapshot = Runtime::SnapshotEditorPropertySmoothing(Commands, Run);
            switch (Step)
            {
            case 0: // The chunked Run.
            {
                if (++OperationalFrames < 8) return;
                Before = Output();
                BeforeRevision = Props().FindPropertyRevision("smooth");
                Runtime::EditorPropertySmoothingResult failure;
                Run = Runtime::StartEditorPropertySmoothing(Commands, Id, Implicit, failure);
                if (!Run) return Fail("implicit start refused: " + failure.Message);
                const auto job = Job("Vulkan property smoothing");
                if (!job) return Fail("the Run job is not an editor job");
                RunToken = job->Token;
                // A duplicate start on the same output is Pending, without a handle or a callback.
                Runtime::EditorPropertySmoothingResult duplicate;
                EXPECT_FALSE(Runtime::StartEditorPropertySmoothing(Commands, Id, Implicit, duplicate));
                EXPECT_EQ(duplicate.Status, Runtime::EditorCommandStatus::Pending) << duplicate.Message;
                EXPECT_NE(duplicate.Message.find("already has an active"), std::string::npos) << duplicate.Message;
                unsigned duplicateCallbacks = 0;
                const auto applied = Runtime::ApplyEditorPropertySmoothingCommand(Commands, Id, Implicit,
                    [&duplicateCallbacks](Runtime::EditorPropertySmoothingResult) { ++duplicateCallbacks; });
                EXPECT_EQ(applied.Status, Runtime::EditorCommandStatus::Pending) << applied.Message;
                EXPECT_EQ(duplicateCallbacks, 0u);
                ++Step;
                return;
            }
            case 1: // Cancel the Run while it is parked in AwaitingApply.
            {
                if (snapshot.Phase != Phase::Running) return Fail("the run ended before its cancel: " + snapshot.Result.Message);
                if (Kernel().Jobs().GetState(RunToken) != Runtime::JobState::AwaitingApply) return;
                if (!CancelThroughAgent(RunToken)) return Fail("jobs_cancel refused the parked Run");
                ++Step;
                return;
            }
            case 2:
            {
                if (snapshot.Phase == Phase::Running) return;
                EXPECT_EQ(snapshot.Phase, Phase::Discarded);
                EXPECT_EQ(snapshot.Result.Status, Runtime::EditorCommandStatus::StaleEntity) << snapshot.Result.Message;
                EXPECT_EQ(Kernel().Jobs().GetState(RunToken), Runtime::JobState::Cancelled);
                ExpectPreviousOutputKept("cancelled Run");
                Run.reset();
                Settle = 0;
                ++Step;
                return;
            }
            case 3: // A short Run, then Accept.
            {
                if (++Settle < 4) return;
                Before = Output();
                BeforeRevision = Props().FindPropertyRevision("smooth");
                Runtime::EditorPropertySmoothingResult failure;
                Run = Runtime::StartEditorPropertySmoothing(Commands, Id, Explicit, failure);
                if (!Run) return Fail("explicit start refused: " + failure.Message);
                const auto job = Job("Vulkan property smoothing");
                if (!job) return Fail("the second Run job is not an editor job");
                RunToken = job->Token;
                ++Step;
                return;
            }
            case 4: return; // Accept: MaintenanceFrame
            case 5: // Cancel the Accept while its readback keeps it parked in AwaitingApply.
            {
                const auto state = Kernel().Jobs().GetState(AcceptToken);
                if (state == Runtime::JobState::AwaitingApply)
                {
                    if (!CancelThroughAgent(AcceptToken)) return Fail("jobs_cancel refused the parked Accept");
                    ++Step;
                    return;
                }
                if (!Runtime::IsActiveEditorJobState(state))
                {
                    // The readback landed before a frame saw the job parked: try again from the
                    // published output.
                    if (++AcceptAttempts >= 8) return Fail("the Accept never parked in AwaitingApply");
                    Run.reset();
                    Settle = 0;
                    Step = 3;
                }
                return;
            }
            case 6:
            {
                if (snapshot.Phase == Phase::Accepting) return;
                EXPECT_EQ(snapshot.Phase, Phase::Discarded);
                ASSERT_EQ(Delivered.size(), 1u);
                EXPECT_EQ(Delivered.front().Status, Runtime::EditorCommandStatus::StaleEntity) << Delivered.front().Message;
                EXPECT_EQ(Kernel().Jobs().GetState(AcceptToken), Runtime::JobState::Cancelled);
                ExpectPreviousOutputKept("cancelled Accept");
                Settle = 0;
                ++Step;
                return;
            }
            case 7: // Nothing arrives later.
            {
                if (++Settle < 8) return;
                EXPECT_EQ(Delivered.size(), 1u);
                ExpectPreviousOutputKept("after the cancelled Accept settled");
                ::testing::Test::RecordProperty("accept_retries_before_parked", std::to_string(AcceptAttempts));
                Done = true;
                Kernel().RequestExit();
                return;
            }
            default: return;
            }
        }

        void Shutdown() override
        {
            Run.reset();
            Commands = {};
            Attachment.Detach();
        }

        Runtime::EditorWorkspaceAttachment Attachment{};
        Runtime::AgentOperationRegistry Registry{};
        Runtime::EditorProcessingCommands Commands{};
        Runtime::PropertySmoothingConfig Config{}, Implicit{}, Explicit{};
        Runtime::EditorPropertySmoothingTransactionHandle Run{};
        Runtime::JobToken RunToken{}, AcceptToken{};
        std::vector<Runtime::EditorPropertySmoothingResult> Delivered{};
        std::vector<float> Before{};
        std::optional<Geometry::PropertyRevision> BeforeRevision{};
        entt::entity Entity{};
        std::uint32_t Id{};
        std::chrono::steady_clock::time_point Started{};
        std::size_t OperationalFrames{}, Settle{}, AcceptAttempts{};
        int Step{};
        bool Done{}, TimedOut{};
    };
}

TEST(RUNTIME311GpuTransactionCancel, AwaitingApplyCancelThroughTheEditorJobSurfaceKeepsThePreviousOutput)
{
    Extrinsic::Core::Config::EngineConfig config = Runtime::CreateReferenceEngineConfig();
    config.Window.Width = 64;
    config.Window.Height = 64;
    config.Render.EnableValidation = true;
    config.Render.EnableVSync = false;
    config.ReferenceScene.Enabled = false;
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize()) GTEST_SKIP() << "GLFW unavailable";
    auto app = std::make_unique<CancelApp>();
    auto* run = app.get();
    Intrinsic::Tests::RuntimeTestKernel engine(config, std::move(app));
    engine.EmplaceModule<Runtime::AsyncWorkModule>();
    engine.EmplaceModule<Runtime::SpatialIndexCache>();
    engine.Initialize();
    Shutdown shutdown{engine};
    if (!engine.GetDevice().SupportsShaderFloat64()) GTEST_SKIP() << "Shader float64 unavailable";
    engine.Run();
    ASSERT_TRUE(engine.GetDevice().IsOperational());
    ASSERT_FALSE(run->TimedOut);
    ASSERT_TRUE(run->Done) << "the scenario stopped at step " << run->Step;
}
