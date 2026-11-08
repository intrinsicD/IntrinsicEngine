// ARCH-023: the editor's resident GPU producers advance on their step run's worker with graphics held and no frame
// (HeldStepRunProbe over the shared HeldFramesProgress gate): farthest-point sampling (its batch of samples), property
// smoothing explicit (its batched passes) and implicit (time steps, each its CG pages), the point normals one-shot
// (the shared transaction core's kernel against a retained index view), the Vulkan E-step of Coherent Point
// Drift and point construction's hybrid kNN graph (its one step's query-session pages). Asserted outcomes only, bounded deadlines; no timing is measured or claimed.
#include "RuntimeTestModule.hpp"
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <tuple>
#include <vector>
#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <gtest/gtest.h>
import Extrinsic.Backends.Vulkan;
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.RHI.Device;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.EngineConfigBoot;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.StepRuns;
import Extrinsic.Runtime.PointSamplingOperations;
import Extrinsic.Runtime.MeshFieldOperations;
import Extrinsic.Runtime.NormalOperations;
import Extrinsic.Runtime.RegistrationOperations;
import Extrinsic.Runtime.PointConstructionOperations;
import Extrinsic.Runtime.EditorProcessing;
import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Component.Transform;
import Geometry.Properties;
import Geometry.Smoothing.Types;
import Geometry.PointCloud.Normals;
#include "HeldFramesProgress.hpp"
#include "HeldStepRunProbe.hpp"

namespace
{
    namespace Runtime = Extrinsic::Runtime;
    namespace GS = Extrinsic::ECS::Components::GeometrySources;
    namespace T = Extrinsic::ECS::Components::Transform;
    using D = Runtime::GeometryElementDomain;

    enum class Producer : std::uint8_t
    {
        Sampling,
        SmoothingExplicit,
        SmoothingImplicit,
        PointNormals,
        Cpd,
        Construction
    };

    class NoFrameApp final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        explicit NoFrameApp(const Producer producer) : Which(producer) {}
        void Resolve() override
        {
            Context.Scene = Kernel().Worlds().Get(Kernel().ActiveWorld());
            Context.World = Kernel().ActiveWorld();
            Context.SpatialIndices = Kernel().Services().Find<Runtime::SpatialIndexCache>();
            Context.Device = &Kernel().GetDevice();
            Context.CommandHistory = &History;
            Context.JobCommands.Submit = [this](Runtime::JobDesc desc, Runtime::EditorJobIdentity)
            {
                return Kernel().Jobs().Submit(std::move(desc));
            };
            Context.JobCommands.Cancel = [this](const Runtime::JobToken token)
            {
                return Kernel().Jobs().Cancel(token) ? Runtime::EditorJobCancelStatus::Requested
                                                     : Runtime::EditorJobCancelStatus::NotActive;
            };
            Context.StepRuns = Steps = Kernel().Services().Find<Runtime::StepRunsService>();
            Source = Cloud(1500u, 7u);
            Target = Cloud(1500u, 7u, glm::vec3{0.05f, -0.02f, 0.03f});
            Entity = Make(Source);
            Other = Make(Target);
        }
        void Frame(double, double) override
        {
            if (++Frames > 4'000u || std::chrono::steady_clock::now() - Started > std::chrono::seconds(90))
            {
                TimedOut = true;
                Kernel().RequestExit();
                return;
            }
            if (!Kernel().GetDevice().IsOperational())
                return;
            if (!Probe.Run && !Start())
            {
                Kernel().RequestExit();
                return;
            }
            if (!Probe.Frame(Kernel().GetDevice(), *Steps))
                return;
            std::string refusal;
            (void)Steps->Control(Probe.Run, Runtime::StepRunAction::Discard, 1u, refusal);
            Kernel().RequestExit();
        }
        void Shutdown() override
        {
            Smoothing.reset();
            Normals.reset();
            Cpd.reset();
            Context = {};
        }

        Producer Which;
        Intrinsic::Tests::HeldStepRunProbe Probe{};
        std::string Refusal{};
        bool TimedOut{};

    private:
        static std::vector<glm::vec3> Cloud(std::size_t count, unsigned seed, glm::vec3 offset = {})
        {
            std::mt19937 random(seed);
            std::uniform_real_distribution<float> uniform(-1.0f, 1.0f);
            std::vector<glm::vec3> points(count);
            for (auto& p : points)
                p = glm::vec3{uniform(random), 0.6f * uniform(random), 0.05f * uniform(random)} + offset;
            return points;
        }
        entt::entity Make(const std::vector<glm::vec3>& points)
        {
            const auto entity = Context.Scene->Create();
            (void)Context.Scene->Raw().emplace<T::Component>(entity);
            auto& properties = Context.Scene->Raw().emplace<GS::Vertices>(entity).Properties;
            properties.Resize(points.size());
            properties.GetOrAdd<glm::vec3>("v:position").Vector() = points;
            auto temperature = properties.GetOrAdd<double>("temperature", 0.0);
            for (std::size_t i = 0; i < points.size(); ++i)
                temperature[i] = double(points[i].x);
            return entity;
        }
        // Starts the producer's run ready (nothing computed before the probe's Step or Run); false with Refusal.
        bool Start()
        {
            const auto commands = Runtime::BindEditorProcessingCommands(Context);
            const auto id = Runtime::SelectionController::ToStableEntityId(Entity);
            const auto ready = [](Runtime::StepRunExecution execution)
            {
                execution.Start = Runtime::StepRunStart::Ready;
                execution.AutoAccept = Runtime::StepRunAutoAccept::Never;
                execution.PreviewEvery = 0u;
                return execution;
            };
            switch (Which)
            {
            case Producer::Sampling:
            {
                Runtime::PointSamplingOperationConfig config{.SourceStableEntityId = id, .Count = 64u,
                    .Backend = Runtime::PointSamplingBackend::Vulkan};
                config.Execution = ready(config.Execution);
                const auto started = Runtime::StartEditorPointSampling(commands, config);
                Probe.Run = started.StepRun;
                Refusal = started.Message;
                break;
            }
            case Producer::SmoothingExplicit:
            case Producer::SmoothingImplicit:
            {
                Runtime::PropertySmoothingConfig config;
                config.Input = {D::PointCloudPoint, "temperature", Geometry::PropertyValueKind::Double};
                config.Output = {D::PointCloudPoint, "smooth", Geometry::PropertyValueKind::Double};
                config.Positions = {D::PointCloudPoint, "v:position", Geometry::PropertyValueKind::Vec3};
                config.Backend = Runtime::PropertySmoothingBackend::Vulkan;
                config.Filter.Iterations = 6u;
                if (Which == Producer::SmoothingImplicit)
                {
                    config.Filter.Method = Geometry::Smoothing::PropertyFilter::Implicit;
                    config.Filter.Solver = Geometry::Smoothing::PropertySolver::ConjugateGradient;
                }
                config.Execution = ready(config.Execution);
                Runtime::EditorPropertySmoothingResult started;
                Smoothing = Runtime::StartEditorPropertySmoothing(commands, id, config, started);
                Probe.Run = started.StepRun;
                Refusal = started.Message;
                break;
            }
            case Producer::PointNormals:
            {
                Probe.Order = Intrinsic::Tests::HeldStepRunProbe::Mode::HoldFirst;
                Probe.Want = 1u; // a one-shot's one step
                Runtime::NormalEstimationConfig config{.StableEntityId = id,
                    .Method = Runtime::NormalEstimationMethod::PointSetPCA,
                    .Backend = Runtime::NormalEstimationBackend::VulkanLBVH};
                config.KNeighbors = 8u;
                config.Orientation = Geometry::PointCloud::Normals::OrientationMode::None;
                Runtime::EditorNormalEstimationResult started;
                config.Execution = ready(config.Execution);
                config.Execution.AutoAccept = Runtime::StepRunAutoAccept::Never;
                Normals = Runtime::StartEditorNormalEstimationTransaction(commands, config, started);
                Probe.Run = Normals ? Runtime::SnapshotEditorNormalEstimation(commands, Normals).Result.StepRun : 0u;
                Refusal = started.Message;
                break;
            }
            case Producer::Construction:
            {
                // ARCH-023: the hybrid kNN graph, its rows paged on the worker through a prepared query session.
                Probe.Order = Intrinsic::Tests::HeldStepRunProbe::Mode::HoldFirst;
                Probe.Want = 1u; // a one-shot's one step (six pages of 256 rows)
                Runtime::PointConstructionConfig config{.StableEntityId = id,
                    .Method = Runtime::PointConstructionMethod::KnnGraph,
                    .Backend = Runtime::PointConstructionBackend::VulkanLBVH,
                    .KNeighbors = 8u,
                    .GpuQueryBatchSize = 256u};
                config.Execution = ready(config.Execution);
                const auto started = Runtime::StartEditorPointConstructionRun(commands, config);
                Probe.Run = started.StepRun;
                Refusal = started.Message;
                break;
            }
            case Producer::Cpd:
            {
                Runtime::CoherentPointDriftConfig config{.SourceStableEntityId = id,
                    .TargetStableEntityId = Runtime::SelectionController::ToStableEntityId(Other),
                    .EStep = Runtime::CoherentPointDriftEStep::Vulkan};
                config.Execution = ready(config.Execution);
                config.Execution.IterationCap = 12u;
                config.Tolerance = 1.0e-12; // the cap ends the run: no early convergence within the held steps
                Runtime::EditorCoherentPointDriftResult failure;
                Cpd = Runtime::StartEditorCoherentPointDrift(commands, config, failure);
                Probe.Run = Cpd ? Runtime::SnapshotEditorCoherentPointDrift(Cpd).StepRun : 0u;
                Refusal = failure.Message;
                break;
            }
            }
            return Probe.Run != 0u;
        }

        Runtime::EditorProcessingContext Context{};
        Runtime::EditorCommandHistory History{};
        Runtime::StepRunsService* Steps{};
        Runtime::EditorPropertySmoothingTransactionHandle Smoothing{};
        Runtime::EditorNormalTransactionHandle Normals{};
        Runtime::EditorCoherentPointDriftRunHandle Cpd{};
        std::vector<glm::vec3> Source{}, Target{};
        entt::entity Entity{entt::null}, Other{entt::null};
        unsigned Frames{};
        std::chrono::steady_clock::time_point Started{std::chrono::steady_clock::now()};
    };
}

// Predictions per producer: started ready (a step run id), the probe gated graphics, the worker completed the wanted
// logical steps with no frame (sampling and explicit smoothing: >= 3 samples or passes in their batch after Step(1);
// implicit smoothing and CPD: >= 3 time steps or EM iterations, each its own pages; point normals and construction:
// its one step); no new validation error. Shared hardware without a separate method queue is reported, not claimed. Mutations: an
// owner-polled page (QueueGpuCompute from Poll, a pump, an ordinary sink) leaves 0 steps; recording on the worker
// through a device readiness query is refused while graphics is held only where the backend blocks it (not asserted).
TEST(ARCH023WorkerProducers, EditorProducersAdvanceWithGraphicsHeldAndNoFrames)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize())
        GTEST_SKIP() << "GLFW unavailable";
    for (const auto producer : {Producer::Sampling,
             Producer::SmoothingExplicit,
             Producer::SmoothingImplicit,
             Producer::PointNormals,
             Producer::Cpd,
             Producer::Construction})
    {
        SCOPED_TRACE(static_cast<int>(producer));
        auto config = Runtime::CreateReferenceEngineConfig();
        config.Window.Width = 64;
        config.Window.Height = 64;
        config.Render.EnableValidation = true;
        config.Render.EnableVSync = false;
        config.ReferenceScene.Enabled = false;
        auto app = std::make_unique<NoFrameApp>(producer);
        const auto* observed = app.get();
        Intrinsic::Tests::RuntimeTestKernel engine(config, std::move(app));
        engine.EmplaceModule<Runtime::SpatialIndexCache>();
        engine.EmplaceModule<Runtime::StepRunsModule>();
        engine.Initialize();
        const auto readiness = Extrinsic::Backends::Vulkan::GetVulkanDeviceOperationalInputs(&engine.GetDevice());
        if (!readiness.LogicalDeviceReady || !readiness.SwapchainReady || !readiness.CommandSyncReady ||
            !engine.GetDevice().SupportsShaderFloat64())
        {
            engine.Shutdown();
            GTEST_SKIP() << "Vulkan bootstrap or shader float64 unavailable";
        }
        const auto before = Extrinsic::Backends::Vulkan::GetVulkanOperationalDiagnosticsSnapshot();
        engine.Run();
        const bool timedOut = observed->TimedOut;
        const auto probe = std::make_tuple(observed->Probe.Run, observed->Probe.Done, observed->Probe.SeparateQueue,
            observed->Probe.Gated, observed->Probe.Progressed, observed->Probe.StepsWithoutFrames,
            observed->Probe.StepsBefore, observed->Probe.Want, observed->Refusal);
        engine.Shutdown();
        const auto& [run, done, separate, gated, progressed, without, stepsBefore, want, refusal] = probe;
        ASSERT_NE(run, 0u) << "the producer did not start: " << refusal;
        ASSERT_FALSE(timedOut);
        ASSERT_TRUE(done);
        EXPECT_EQ(Extrinsic::Backends::Vulkan::GetVulkanOperationalDiagnosticsSnapshot().VulkanValidationErrorCount,
            before.VulkanValidationErrorCount);
        if (!separate)
            GTEST_SKIP() << "No separate method compute queue: no-frame progress cannot be separated from graphics.";
        EXPECT_TRUE(gated);
        EXPECT_TRUE(progressed);
        EXPECT_GE(without, want);
        if (producer != Producer::PointNormals && producer != Producer::Construction)
            EXPECT_EQ(stepsBefore, 1u) << "Step(1) is exactly one logical step";
    }
}
