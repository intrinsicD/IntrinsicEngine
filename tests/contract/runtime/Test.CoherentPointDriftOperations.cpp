// RUNTIME-273: the Coherent Point Drift editor operation. Config validation, every variant
// on point-cloud and graph-node domains with its publication path and undo/redo, and the
// step-run lifecycle (ARCH-023): ready start, step n, run, pause, stop, restart, accept,
// discard, stale captures, shutdown and the Vulkan E-step's resident target.
#include <cmath>
#include <utility>
#include <span>
#include <ranges>
#include <optional>
#include <cstring>
#include <algorithm>
#include <random>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <entt/entity/registry.hpp>
#include <gtest/gtest.h>
#include "SandboxEditorJobHarness.hpp"
#include "MockRHI.hpp"
#include <atomic>
#include <chrono>
#include <limits>
#include <thread>
#include <functional>
#include <memory>
#include <mutex>
import Extrinsic.Core.Tasks.CounterEvent;
import Extrinsic.Runtime.CoherentPointDriftGpuEStep;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.JobService;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.TransferQueue;
import Extrinsic.RHI.Handles;
import Extrinsic.Graphics.CoherentPointDriftEStep;
import Geometry.Registration.CoherentPointDrift;
import Extrinsic.Runtime.RegistrationOperations;
import Extrinsic.Runtime.StepRuns;
import Extrinsic.Runtime.WorldRegistry;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Component.Hierarchy;
import Extrinsic.ECS.Component.DirtyTags;
import Geometry.Graph;
import Geometry.HalfedgeMesh;
namespace R = Extrinsic::Runtime;
namespace GS = Extrinsic::ECS::Components::GeometrySources;
namespace T = Extrinsic::ECS::Components::Transform;
using D = R::GeometryElementDomain;
using M = R::CoherentPointDriftMethod;
using O = R::CoherentPointDriftOutput;
namespace
{
    std::vector<glm::vec3> Cloud(std::size_t count, unsigned seed)
    {
        std::mt19937 random(seed);
        std::uniform_real_distribution<float> uniform(-1.0f, 1.0f);
        std::vector<glm::vec3> points(count);
        for (auto& p : points) p = {uniform(random), uniform(random), 0.7f * uniform(random)};
        return points;
    }

    // A point cloud or a graph (nodes only) carrying `points` in "v:position".
    entt::entity Make(Extrinsic::ECS::Scene::Registry& scene, D domain, const std::vector<glm::vec3>& points)
    {
        const auto entity = scene.Create();
        scene.Raw().emplace<T::Component>(entity);
        if (domain == D::GraphNode)
        {
            Geometry::Graph::Graph graph;
            for (const auto& p : points) (void)graph.AddVertex(p);
            GS::PopulateFromGraph(scene.Raw(), entity, graph);
        }
        else
        {
            auto& vertices = scene.Raw().emplace<GS::Vertices>(entity).Properties;
            vertices.Resize(points.size());
            vertices.GetOrAdd<glm::vec3>("v:position").Vector() = points;
        }
        return entity;
    }

    struct Scene
    {
        R::WorldRegistry Worlds;
        R::WorldHandle World{Worlds.CreateWorld("cpd")};
        Extrinsic::ECS::Scene::Registry& Registry{*Worlds.Get(World)};
        R::EditorCommandHistory History;
        R::EditorProcessingContext Context{.Scene = &Registry, .World = World, .CommandHistory = &History};
        [[nodiscard]] R::EditorProcessingCommands Commands() { return R::BindEditorProcessingCommands(Context); }
    };

    std::uint32_t Id(entt::entity entity) { return R::SelectionController::ToStableEntityId(entity); }

    // The CPD section's execution options with another iteration cap (ready, wait for Accept).
    R::StepRunExecution Cap(const std::uint32_t iterations)
    {
        auto execution = R::kDefaultCoherentPointDriftExecution;
        execution.IterationCap = iterations;
        return execution;
    }

    // Drains the job service until `done` holds (a paused run's holder never ends, so DrainUntilTerminal cannot).
    template <class Done>
    bool DrainUntil(Extrinsic::Tests::EditorJobHarness& jobs,
        Done done,
        const std::chrono::milliseconds timeout = std::chrono::seconds{10})
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            (void)jobs.Jobs().DrainCompletions(jobs.Events());
            if (done())
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        return false;
    }

    bool Control(Extrinsic::Tests::EditorJobHarness& jobs,
        const std::uint64_t run,
        const R::StepRunAction action,
        const std::uint32_t count = 1u)
    {
        std::string refusal;
        const bool accepted = jobs.StepRuns().Control(run, action, count, refusal);
        EXPECT_TRUE(accepted) << refusal;
        return accepted;
    }

    // A rigid case that converges in a few dozen EM iterations: the source is the target moved by (0.3, 0.1, 0).
    struct ShiftedPair
    {
        Scene S;
        Extrinsic::Tests::EditorJobHarness Jobs;
        entt::entity Source{}, Target{};
        std::vector<glm::vec3> Points{};
        // `related` false: the target is an unrelated cloud, so sigma^2 never reaches its floor (no early end).
        explicit ShiftedPair(const std::size_t count = 100u, const unsigned seed = 11u, const bool related = true)
            : Points(Cloud(count, seed))
        {
            std::vector<glm::vec3> moved = related ? std::vector<glm::vec3>{} : Cloud(count, seed + 1000u);
            if (related)
                for (const auto& p : Points)
                    moved.push_back(p + glm::vec3(0.3f, 0.1f, 0.0f));
            Source = Make(S.Registry, D::PointCloudPoint, Points);
            Target = Make(S.Registry, D::PointCloudPoint, moved);
            Jobs.Attach(S.Context);
        }
        [[nodiscard]] R::CoherentPointDriftConfig Config() const
        {
            return {.SourceStableEntityId = Id(Source), .TargetStableEntityId = Id(Target)};
        }
        [[nodiscard]] R::EditorCoherentPointDriftRunHandle Start(const R::CoherentPointDriftConfig& config)
        {
            R::EditorCoherentPointDriftResult failure;
            auto run = R::StartEditorCoherentPointDrift(S.Commands(), config, failure);
            EXPECT_TRUE(run) << failure.Message;
            return run;
        }
        [[nodiscard]] std::optional<R::StepRunObservation> Observe(const R::EditorCoherentPointDriftRunHandle& run)
        {
            return Jobs.StepRuns().Observe(R::SnapshotEditorCoherentPointDrift(run).StepRun);
        }
        // Until the run rests (paused or ended) with nothing in flight.
        bool Rest(const R::EditorCoherentPointDriftRunHandle& run)
        {
            return DrainUntil(Jobs,
                [&]
                {
                    const auto o = Observe(run);
                    return o && (o->Phase == R::StepRunPhase::Paused || R::IsTerminal(o->Phase)) && !o->SegmentInFlight;
                });
        }
        bool Terminal(const R::EditorCoherentPointDriftRunHandle& run)
        {
            return DrainUntil(Jobs,
                [&]
                {
                    const auto o = Observe(run);
                    return o && R::IsTerminal(o->Phase);
                });
        }
        [[nodiscard]] glm::vec3 SourcePosition()
        {
            return S.Registry.Raw().get<T::Component>(Source).Position;
        }
    };
}

TEST(CoherentPointDriftOperations, ConfigRoundTripsAndRejectsUnstorableOutputs)
{
    const auto registration = R::MakeCoherentPointDriftConfigSectionRegistration();
    EXPECT_FALSE(registration.SchemaJson.empty());
    R::CoherentPointDriftConfig config{.SourceStableEntityId = 3, .TargetStableEntityId = 4, .Method = M::Nonrigid,
                                       .OutlierWeight = 0.2, .Beta = 1.5, .Output = O::DisplacementProperty,
                                       .DisplacementName = "warp", .EStep = R::CoherentPointDriftEStep::Truncated,
                                       .EStepTolerance = 1e-4, .Threads = 3, .NystromLandmarks = 128,
                                       .NystromErrorLimit = 1e-4, .NystromLocalSigma = 0.35, .LowRank = 40, .AutoLowRank = false};
    const auto decoded = R::DecodeCoherentPointDriftConfig(R::SerializeCoherentPointDriftConfig(config));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->Method, M::Nonrigid);
    EXPECT_EQ(decoded->DisplacementName, "warp");
    EXPECT_EQ(decoded->Beta, 1.5);
    EXPECT_EQ(decoded->EStep, R::CoherentPointDriftEStep::Truncated);
    EXPECT_EQ(decoded->EStepTolerance, 1e-4);
    EXPECT_EQ(decoded->Threads, 3u);
    EXPECT_EQ(decoded->LowRank, 40u);
    EXPECT_FALSE(decoded->AutoLowRank);
    EXPECT_EQ(decoded->NystromLandmarks, 128u);
    EXPECT_EQ(decoded->NystromErrorLimit, 1e-4);
    EXPECT_EQ(decoded->NystromLocalSigma, 0.35);
    const auto invalid = [&](R::CoherentPointDriftConfig c) {
        return !registration.Validate(R::SerializeCoherentPointDriftConfig(c), {}, "test").Usable();
    };
    EXPECT_TRUE(invalid({.Method = M::Affine, .Output = O::SourceTransform}));
    EXPECT_TRUE(invalid({.Method = M::Nonrigid, .Output = O::SourceTransform}));
    EXPECT_TRUE(invalid({.Output = O::DisplacementProperty, .DisplacementName = "v:position"}));
    EXPECT_TRUE(invalid({.SourceStableEntityId = 5, .TargetStableEntityId = 5}));
    EXPECT_TRUE(invalid({.OutlierWeight = 1.0}));
    EXPECT_TRUE(invalid({.EStepTolerance = 0.0}));
    EXPECT_TRUE(invalid({.EStepTolerance = 1.0}));
    EXPECT_TRUE(invalid({.LowRank = 5000}));
    EXPECT_TRUE(invalid({.NystromLandmarks = 1}));
    EXPECT_TRUE(invalid({.NystromLandmarks = 5000}));
    EXPECT_TRUE(invalid({.NystromErrorLimit = 0.0}));
    EXPECT_TRUE(invalid({.NystromLocalSigma = -0.1}));
    EXPECT_TRUE(invalid({.NystromLocalSigma = std::numeric_limits<double>::infinity()}));
    EXPECT_FALSE(registration.Validate(R"({"no_such_field":1})", {}, "test").Usable());
}

TEST(CoherentPointDriftOperations, RigidPublishesTheSourceTransformOnEveryPointDomain)
{
    for (const D sourceDomain : {D::PointCloudPoint, D::GraphNode})
        for (const D targetDomain : {D::PointCloudPoint, D::GraphNode})
        {
            SCOPED_TRACE(std::to_string(int(sourceDomain)) + " -> " + std::to_string(int(targetDomain)));
            Scene s;
            const auto points = Cloud(120, 7);
            const glm::mat3 rotation = glm::mat3(glm::rotate(glm::mat4(1.0f), 0.4f, glm::vec3(0.2f, 1.0f, 0.3f)));
            std::vector<glm::vec3> moved;
            for (const auto& p : points) moved.push_back(rotation * p + glm::vec3(0.5f, -0.25f, 0.1f));
            const auto source = Make(s.Registry, sourceDomain, points);
            const auto target = Make(s.Registry, targetDomain, moved);
            const auto result = R::ApplyEditorCoherentPointDriftCommand(s.Commands(), {
                .SourceStableEntityId = Id(source), .TargetStableEntityId = Id(target), .OutlierWeight = 0.0});
            ASSERT_TRUE(result.Succeeded()) << result.Message;
            EXPECT_EQ(result.Backend, "cpu_auto") << "the editor default is the automatic E-step";
            EXPECT_EQ(result.EStepErrorBound <= 1e-6, true);
            EXPECT_EQ(result.SourcePointCount, 120u);
            auto& transform = s.Registry.Raw().get<T::Component>(source);
            EXPECT_NEAR(transform.Position.x, 0.5f, 2e-3f);
            EXPECT_NEAR(transform.Position.y, -0.25f, 2e-3f);
            EXPECT_LT(glm::length(glm::mat3_cast(transform.Rotation) * points[5] - rotation * points[5]), 2e-3f);
            EXPECT_EQ(transform.Scale, glm::vec3(1.0f)) << "scale estimation is off by default";
            // The captured points stay untouched; the transform carries the alignment.
            EXPECT_EQ(R::ResolveGeometryPropertySet(R::BuildGeometryAvailability(s.Registry.Raw(), source), sourceDomain)
                          ->Get<glm::vec3>("v:position")[5], points[5]);
            ASSERT_TRUE(s.History.Undo().Succeeded());
            EXPECT_EQ(transform.Position, glm::vec3(0.0f));
            ASSERT_TRUE(s.History.Redo().Succeeded());
            EXPECT_NEAR(transform.Position.x, 0.5f, 2e-3f);
        }
}

TEST(CoherentPointDriftOperations, AffineWritesPositionsAndNonrigidWritesADisplacementProperty)
{
    const auto registration = R::MakeCoherentPointDriftConfigSectionRegistration();
    const auto invalidConfig = [&](R::CoherentPointDriftConfig c) {
        return !registration.Validate(R::SerializeCoherentPointDriftConfig(c), {}, "test").Usable();
    };
    Scene s;
    const auto points = Cloud(150, 9);
    std::vector<glm::vec3> affine, bent;
    const glm::mat3 map{1.2f, 0.1f, 0.0f, 0.0f, 0.9f, 0.1f, 0.05f, 0.0f, 1.1f};
    for (const auto& p : points)
    {
        affine.push_back(map * p + glm::vec3(0.2f, 0.0f, -0.1f));
        bent.push_back(p + glm::vec3(0.08f * std::sin(2.0f * p.y), 0.0f, 0.08f * std::cos(1.5f * p.x)));
    }
    const auto source = Make(s.Registry, D::PointCloudPoint, points);
    // The source has a non-identity transform: published positions are in its local space.
    s.Registry.Raw().get<T::Component>(source).Position = {1.0f, 0.0f, 0.0f};
    std::vector<glm::vec3> affineShifted = affine;
    for (auto& p : affineShifted) p += glm::vec3(1.0f, 0.0f, 0.0f);
    const auto affineTarget = Make(s.Registry, D::GraphNode, affineShifted);
    const auto result = R::ApplyEditorCoherentPointDriftCommand(s.Commands(), {
        .SourceStableEntityId = Id(source), .TargetStableEntityId = Id(affineTarget), .Method = M::Affine,
        .OutlierWeight = 0.0, .Output = O::Positions});
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    auto* props = R::ResolveGeometryPropertySet(R::BuildGeometryAvailability(s.Registry.Raw(), source), D::PointCloudPoint);
    EXPECT_LT(glm::length(props->Get<glm::vec3>("v:position")[11] - affine[11]), 5e-3f);
    ASSERT_TRUE(s.History.Undo().Succeeded());
    EXPECT_EQ(props->Get<glm::vec3>("v:position")[11], points[11]);

    s.Registry.Raw().get<T::Component>(source).Position = {};
    const auto bentTarget = Make(s.Registry, D::GraphNode, bent);
    const auto nonrigid = R::ApplyEditorCoherentPointDriftCommand(s.Commands(), {
        .SourceStableEntityId = Id(source), .TargetStableEntityId = Id(bentTarget), .Method = M::Nonrigid,
        .OutlierWeight = 0.0, .Output = O::DisplacementProperty, .DisplacementName = "warp"});
    ASSERT_TRUE(nonrigid.Succeeded()) << nonrigid.Message;
    EXPECT_GT(nonrigid.MeanDisplacement, 0.01);
    const auto warp = props->Get<glm::vec3>("warp");
    ASSERT_TRUE(warp);
    double error = 0.0;
    for (std::size_t i = 0; i < points.size(); ++i) error += glm::length(points[i] + warp[i] - bent[i]);
    EXPECT_LT(error / double(points.size()), 0.02);
    EXPECT_EQ(props->Get<glm::vec3>("v:position")[3], points[3]) << "displacement output leaves positions alone";
    ASSERT_TRUE(s.History.Undo().Succeeded());
    EXPECT_FALSE(props->Exists("warp"));

    // Low-rank kernel with the exact reference E-step reaches the same field.
    const auto lowRank = R::ApplyEditorCoherentPointDriftCommand(s.Commands(), {
        .SourceStableEntityId = Id(source), .TargetStableEntityId = Id(bentTarget), .Method = M::Nonrigid,
        .OutlierWeight = 0.0, .Output = O::DisplacementProperty, .DisplacementName = "warp",
        .EStep = R::CoherentPointDriftEStep::Reference, .LowRank = 40});
    ASSERT_TRUE(lowRank.Succeeded()) << lowRank.Message;
    EXPECT_EQ(lowRank.Backend, "cpu_reference");
    EXPECT_GT(lowRank.KernelRank, 0u);
    EXPECT_LE(lowRank.KernelRank, 40u);
    const auto lowRankWarp = props->Get<glm::vec3>("warp");
    ASSERT_TRUE(lowRankWarp);
    double lowRankError = 0.0;
    for (std::size_t i = 0; i < points.size(); ++i) lowRankError += glm::length(points[i] + lowRankWarp[i] - bent[i]);
    EXPECT_LT(lowRankError / double(points.size()), 0.02);

    // Bayesian (similarity plus deformation) through the same command, written as a displacement.
    const auto bayesian = R::ApplyEditorCoherentPointDriftCommand(s.Commands(), {
        .SourceStableEntityId = Id(source), .TargetStableEntityId = Id(bentTarget), .Method = M::Bayesian,
        .OutlierWeight = 0.0, .Beta = 1.0, .Lambda = 2.0, .Output = O::DisplacementProperty, .DisplacementName = "warp"});
    ASSERT_TRUE(bayesian.Succeeded()) << bayesian.Message;
    const auto bayesianWarp = props->Get<glm::vec3>("warp");
    ASSERT_TRUE(bayesianWarp);
    double bayesianError = 0.0;
    for (std::size_t i = 0; i < points.size(); ++i) bayesianError += glm::length(points[i] + bayesianWarp[i] - bent[i]);
    EXPECT_LT(bayesianError / double(points.size()), 0.02);
    EXPECT_TRUE(invalidConfig({.Subsample = 2}));
}

// ARCH-023: a ready run computes nothing until a control asks; Step n takes exactly n EM iterations and pauses; the
// preview moves while nothing is published; Run continues to the end; Accept publishes the estimate once (one undo
// entry) and an ended run takes no more steps.
TEST(CoherentPointDriftOperations, StepModeTracesEachIterationAndAcceptsTheCurrentEstimate)
{
    ShiftedPair pair;
    const auto run = pair.Start(pair.Config());
    ASSERT_TRUE(run);
    const auto id = R::SnapshotEditorCoherentPointDrift(run).StepRun;
    ASSERT_NE(id, 0u);
    // Init stays ready: drains compute nothing.
    for (int i = 0; i < 20; ++i)
        (void)pair.Jobs.Jobs().DrainCompletions(pair.Jobs.Events());
    EXPECT_EQ(pair.Observe(run)->Phase, R::StepRunPhase::Ready);
    EXPECT_EQ(pair.Observe(run)->Steps, 0u);
    EXPECT_TRUE(R::SnapshotEditorCoherentPointDrift(run).Trace.empty());
    EXPECT_EQ(R::SnapshotEditorCoherentPointDrift(run).Target->size(), 100u);
    std::string refusal;
    EXPECT_FALSE(pair.Jobs.StepRuns().Control(id, R::StepRunAction::Accept, 1u, refusal))
        << "nothing to accept before a step";

    ASSERT_TRUE(Control(pair.Jobs, id, R::StepRunAction::Step, 2u));
    ASSERT_TRUE(pair.Rest(run));
    auto snapshot = R::SnapshotEditorCoherentPointDrift(run);
    EXPECT_EQ(pair.Observe(run)->Phase, R::StepRunPhase::Paused);
    EXPECT_EQ(pair.Observe(run)->Steps, 2u) << "Step 2 is two EM iterations";
    EXPECT_EQ(snapshot.Trace.size(), 2u);
    EXPECT_EQ(snapshot.Result.Iterations, 2u);
    EXPECT_LT(snapshot.Trace[1].Sigma2, snapshot.Trace[0].Sigma2);
    EXPECT_NE((*snapshot.SourcePreview)[0], pair.Points[0]) << "the preview moves before anything is published";
    EXPECT_EQ(pair.SourcePosition(), glm::vec3(0.0f));
    // A paused run stays at its boundary.
    for (int i = 0; i < 20; ++i)
        (void)pair.Jobs.Jobs().DrainCompletions(pair.Jobs.Events());
    EXPECT_EQ(pair.Observe(run)->Steps, 2u);

    ASSERT_TRUE(Control(pair.Jobs, id, R::StepRunAction::Run));
    ASSERT_TRUE(pair.Rest(run));
    snapshot = R::SnapshotEditorCoherentPointDrift(run);
    EXPECT_NE(pair.Observe(run)->End, R::StepRunEnd::None);
    EXPECT_NE(snapshot.Result.Termination, "none");
    EXPECT_FALSE(pair.Jobs.StepRuns().Control(id, R::StepRunAction::Step, 1u, refusal))
        << "an ended run takes no more steps";
    ASSERT_TRUE(Control(pair.Jobs, id, R::StepRunAction::Accept));
    ASSERT_TRUE(pair.Terminal(run));
    EXPECT_EQ(pair.Observe(run)->Phase, R::StepRunPhase::Committed);
    EXPECT_TRUE(R::SnapshotEditorCoherentPointDrift(run).Result.Succeeded());
    EXPECT_NEAR(pair.SourcePosition().x, 0.3f, 1e-3f);
    ASSERT_TRUE(pair.S.History.Undo().Succeeded());
    EXPECT_EQ(pair.SourcePosition(), glm::vec3(0.0f));
    EXPECT_FALSE(pair.S.History.CanUndo()) << "Accept is one undo entry";
}

// Run equals the same steps taken one at a time (CPU E-step, deterministic): the same trace and published transform.
TEST(CoherentPointDriftOperations, ARunEqualsItsStepsOneAtATime)
{
    const auto accepted = [](const bool stepwise)
    {
        ShiftedPair pair;
        auto config = pair.Config();
        config.EStep = R::CoherentPointDriftEStep::Reference;
        config.Execution.IterationCap = 12u;
        const auto run = pair.Start(config);
        const auto id = R::SnapshotEditorCoherentPointDrift(run).StepRun;
        if (stepwise)
            while (pair.Observe(run)->End == R::StepRunEnd::None)
            {
                EXPECT_TRUE(Control(pair.Jobs, id, R::StepRunAction::Step, 1u));
                EXPECT_TRUE(pair.Rest(run));
            }
        else
        {
            EXPECT_TRUE(Control(pair.Jobs, id, R::StepRunAction::Run));
            EXPECT_TRUE(pair.Rest(run));
        }
        std::vector<double> sigma2;
        for (const auto& row : R::SnapshotEditorCoherentPointDrift(run).Trace)
            sigma2.push_back(row.Sigma2);
        EXPECT_TRUE(Control(pair.Jobs, id, R::StepRunAction::Accept));
        EXPECT_TRUE(pair.Terminal(run));
        return std::pair{sigma2, pair.SourcePosition()};
    };
    const auto [stepwiseTrace, stepwisePosition] = accepted(true);
    const auto [runTrace, runPosition] = accepted(false);
    ASSERT_FALSE(runTrace.empty());
    EXPECT_EQ(stepwiseTrace, runTrace);
    EXPECT_EQ(stepwisePosition, runPosition);
}

// Stop ends a run with its partial estimate, which Accept publishes; Restart begins a new generation from step 0 whose
// steps match a fresh run's, so nothing of the older computation is published.
TEST(CoherentPointDriftOperations, StopKeepsAPartialResultAndRestartBeginsANewGeneration)
{
    const auto twoSteps = [](const bool restart)
    {
        ShiftedPair pair;
        auto config = pair.Config();
        config.EStep = R::CoherentPointDriftEStep::Reference;
        const auto run = pair.Start(config);
        const auto id = R::SnapshotEditorCoherentPointDrift(run).StepRun;
        if (restart)
        {
            EXPECT_TRUE(Control(pair.Jobs, id, R::StepRunAction::Step, 3u));
            EXPECT_TRUE(pair.Rest(run));
            EXPECT_TRUE(Control(pair.Jobs, id, R::StepRunAction::Restart));
            EXPECT_TRUE(DrainUntil(pair.Jobs,
                [&]
                {
                    return pair.Observe(run)->Generation == 1u;
                }));
            EXPECT_EQ(pair.Observe(run)->Steps, 0u);
            EXPECT_EQ(pair.Observe(run)->Phase, R::StepRunPhase::Ready) << "the start mode of the run (ready)";
            EXPECT_TRUE(R::SnapshotEditorCoherentPointDrift(run).Trace.empty());
        }
        EXPECT_TRUE(Control(pair.Jobs, id, R::StepRunAction::Step, 2u));
        EXPECT_TRUE(pair.Rest(run));
        EXPECT_TRUE(Control(pair.Jobs, id, R::StepRunAction::Stop));
        EXPECT_TRUE(pair.Rest(run));
        EXPECT_EQ(pair.Observe(run)->End, R::StepRunEnd::Stopped);
        const auto stopped = R::SnapshotEditorCoherentPointDrift(run).Result;
        EXPECT_EQ(stopped.Iterations, 2u);
        EXPECT_EQ(pair.SourcePosition(), glm::vec3(0.0f)) << "Stop never accepts";
        EXPECT_TRUE(Control(pair.Jobs, id, R::StepRunAction::Accept));
        EXPECT_TRUE(pair.Terminal(run));
        EXPECT_EQ(R::SnapshotEditorCoherentPointDrift(run).Result.Iterations, 2u);
        EXPECT_TRUE(pair.S.History.CanUndo());
        return pair.SourcePosition();
    };
    const auto fresh = twoSteps(false);
    EXPECT_NE(fresh, glm::vec3(0.0f));
    EXPECT_GT(fresh.x, 0.0f);
    EXPECT_LT(fresh.x, 0.3f) << "two iterations are a partial alignment";
    EXPECT_EQ(twoSteps(true), fresh);
}

// Changed captures end a run (paused or computing) without publication; Discard ends it at once; a shutdown with a
// paused run and with a computing one drains both, each delivering one terminal result and publishing nothing.
TEST(CoherentPointDriftOperations, StaleDiscardedAndShutdownRunsPublishNothing)
{
    {
        ShiftedPair pair;
        auto config = pair.Config();
        config.Execution.Start = R::StepRunStart::Run;
        const auto run = pair.Start(config);
        ASSERT_TRUE(pair.Rest(run));
        pair.S.Registry.Raw().get<T::Component>(pair.Target).Position = {0.0f, 5.0f, 0.0f};
        ASSERT_TRUE(pair.Terminal(run));
        EXPECT_EQ(pair.Observe(run)->Phase, R::StepRunPhase::Discarded);
        ASSERT_TRUE(pair.Jobs.DrainUntilTerminal()); // the result arrives at the settled end
        EXPECT_EQ(R::SnapshotEditorCoherentPointDrift(run).Result.Status, R::EditorCommandStatus::StaleEntity);
        EXPECT_FALSE(pair.S.History.CanUndo());
    }
    {
        ShiftedPair pair;
        const auto run = pair.Start(pair.Config());
        pair.S.Registry.Raw().get<GS::Vertices>(pair.Source).Properties.GetOrAdd<glm::vec3>("v:position").Vector()[0] +=
            glm::vec3(1.0f);
        ASSERT_TRUE(pair.Terminal(run));
        EXPECT_EQ(pair.Observe(run)->Phase, R::StepRunPhase::Discarded);
        EXPECT_NE(pair.Observe(run)->Diagnostic.find("stale"), std::string::npos) << pair.Observe(run)->Diagnostic;
        EXPECT_FALSE(pair.S.History.CanUndo());
    }
    {
        ShiftedPair pair;
        const auto run = pair.Start(pair.Config());
        ASSERT_TRUE(Control(pair.Jobs, R::SnapshotEditorCoherentPointDrift(run).StepRun, R::StepRunAction::Discard));
        EXPECT_EQ(pair.Observe(run)->Phase, R::StepRunPhase::Discarded) << "Discard answers at once";
        std::string refusal;
        EXPECT_FALSE(pair.Jobs.StepRuns().Control(
            R::SnapshotEditorCoherentPointDrift(run).StepRun, R::StepRunAction::Step, 1u, refusal));
        ASSERT_TRUE(pair.Jobs.DrainUntilTerminal());
        EXPECT_EQ(R::SnapshotEditorCoherentPointDrift(run).Result.Status, R::EditorCommandStatus::StaleEntity);
        EXPECT_FALSE(pair.S.History.CanUndo()) << "nothing was published";
    }
    for (const bool computing : {false, true})
    {
        SCOPED_TRACE(computing ? "computing" : "paused");
        ShiftedPair pair(400u, 23u, false);
        unsigned delivered = 0u;
        auto config = pair.Config();
        config.Execution.IterationCap = 10000u;
        config.Tolerance = 0.0;
        // The one-shot command (run at once, accept at its end) with a sink, paused or left computing.
        (void)R::ApplyEditorCoherentPointDriftCommand(pair.S.Commands(),
            config,
            [&](const R::EditorCoherentPointDriftResult& result)
            {
                ++delivered;
                EXPECT_FALSE(result.Succeeded());
            });
        const auto id = pair.Jobs.StepRuns().Runs().back();
        ASSERT_TRUE(DrainUntil(pair.Jobs,
            [&]
            {
                return pair.Jobs.StepRuns().Observe(id)->Steps >= 2u;
            }));
        if (!computing)
        {
            ASSERT_TRUE(Control(pair.Jobs, id, R::StepRunAction::Pause));
            ASSERT_TRUE(DrainUntil(pair.Jobs,
                [&]
                {
                    return pair.Jobs.StepRuns().Observe(id)->Phase == R::StepRunPhase::Paused;
                }));
        }
        pair.Jobs.Jobs().CancelAndDrain();
        (void)pair.Jobs.Jobs().DrainCompletions(pair.Jobs.Events());
        EXPECT_TRUE(R::IsTerminal(pair.Jobs.StepRuns().Observe(id)->Phase));
        EXPECT_EQ(delivered, 1u) << "one terminal result";
        EXPECT_FALSE(pair.S.History.CanUndo());
        EXPECT_EQ(pair.SourcePosition(), glm::vec3(0.0f));
    }
}

TEST(CoherentPointDriftOperations, ReadinessExplainsWhyARunCannotStart)
{
    Scene s;
    const auto source = Make(s.Registry, D::PointCloudPoint, Cloud(2, 1));
    const auto target = Make(s.Registry, D::PointCloudPoint, Cloud(50, 2));
    const auto few = R::PreviewEditorCoherentPointDriftCommand(s.Commands(), {.SourceStableEntityId = Id(source),
        .TargetStableEntityId = Id(target)});
    EXPECT_FALSE(few.Enabled);
    EXPECT_NE(few.DisabledReason.find("at least 3"), std::string::npos) << few.DisabledReason;
    const auto missing = R::PreviewEditorCoherentPointDriftCommand(s.Commands(), {.SourceStableEntityId = 9999,
        .TargetStableEntityId = Id(target)});
    EXPECT_FALSE(missing.Enabled);
    const auto large = Make(s.Registry, D::PointCloudPoint, Cloud(9000, 3));
    // The exact full kernel is refused above 8192 points; the default automatic rank runs.
    const auto nonrigid = R::PreviewEditorCoherentPointDriftCommand(s.Commands(), {.SourceStableEntityId = Id(large),
        .TargetStableEntityId = Id(target), .Method = M::Nonrigid, .Output = O::Positions, .AutoLowRank = false});
    EXPECT_FALSE(nonrigid.Enabled);
    EXPECT_NE(nonrigid.DisabledReason.find("8192"), std::string::npos) << nonrigid.DisabledReason;
    EXPECT_TRUE(R::PreviewEditorCoherentPointDriftCommand(s.Commands(), {.SourceStableEntityId = Id(large),
        .TargetStableEntityId = Id(target), .Method = M::Nonrigid, .Output = O::Positions}).Enabled);
    EXPECT_TRUE(R::PreviewEditorCoherentPointDriftCommand(s.Commands(), {.SourceStableEntityId = Id(target),
        .TargetStableEntityId = Id(large)}).Enabled);
}

// Above 1024 registered source points a deforming run with low_rank 0 uses the automatic rank
// (the full kernel costs O(m^3) per iteration).
TEST(CoherentPointDriftOperations, LargeDeformingRunsUseTheAutomaticLowRank)
{
    Scene s;
    const auto points = Cloud(1100, 41);
    std::vector<glm::vec3> moved;
    for (const auto& p : points) moved.push_back(p + glm::vec3(0.05f * p.y, 0.0f, 0.02f));
    const auto source = Make(s.Registry, D::PointCloudPoint, points);
    const auto target = Make(s.Registry, D::PointCloudPoint, moved);
    R::CoherentPointDriftConfig config{.SourceStableEntityId = Id(source),
        .TargetStableEntityId = Id(target),
        .Method = M::Nonrigid,
        .Execution = Cap(2u),
        .Output = O::DisplacementProperty};
    const auto automatic = R::ApplyEditorCoherentPointDriftCommand(s.Commands(), config);
    ASSERT_TRUE(automatic.Succeeded()) << automatic.Message;
    EXPECT_GT(automatic.KernelRank, 0u);
    EXPECT_LE(automatic.KernelRank, R::kCoherentPointDriftAutoLowRank);

    // Below the threshold the default stays exact.
    const auto small = Make(s.Registry, D::PointCloudPoint, Cloud(200, 43));
    config = {.SourceStableEntityId = Id(small),
        .TargetStableEntityId = Id(target),
        .Method = M::Nonrigid,
        .Execution = Cap(2u),
        .Output = O::DisplacementProperty};
    const auto smallRun = R::ApplyEditorCoherentPointDriftCommand(s.Commands(), config);
    ASSERT_TRUE(smallRun.Succeeded()) << smallRun.Message;
    EXPECT_EQ(smallRun.KernelRank, 0u);
}

// auto_low_rank off keeps the exact full kernel. A separate case: above the threshold the full
// kernel costs seconds per iteration in Debug.
TEST(CoherentPointDriftOperations, LargeDeformingRunsKeepTheFullKernelWhenExactIsAsked)
{
    Scene s;
    const auto points = Cloud(1100, 41);
    std::vector<glm::vec3> moved;
    for (const auto& p : points) moved.push_back(p + glm::vec3(0.05f * p.y, 0.0f, 0.02f));
    const auto source = Make(s.Registry, D::PointCloudPoint, points);
    const auto target = Make(s.Registry, D::PointCloudPoint, moved);
    const auto exact = R::ApplyEditorCoherentPointDriftCommand(s.Commands(),
        {.SourceStableEntityId = Id(source),
            .TargetStableEntityId = Id(target),
            .Method = M::Nonrigid,
            .Execution = Cap(1u),
            .Output = O::DisplacementProperty,
            .AutoLowRank = false});
    ASSERT_TRUE(exact.Succeeded()) << exact.Message;
    EXPECT_EQ(exact.KernelRank, 0u) << "the full kernel was asked for";
}

TEST(CoherentPointDriftOperations, QueuedRunsCompleteOnTheJobServiceAndDeliverOneResult)
{
    Scene s;
    const auto points = Cloud(200, 17);
    std::vector<glm::vec3> moved;
    for (const auto& p : points) moved.push_back(p + glm::vec3(-0.2f, 0.3f, 0.1f));
    const auto source = Make(s.Registry, D::PointCloudPoint, points);
    const auto target = Make(s.Registry, D::PointCloudPoint, moved);
    Extrinsic::Tests::EditorJobHarness jobs;
    jobs.Attach(s.Context);
    int delivered = 0;
    R::EditorCoherentPointDriftResult last;
    const auto pending = R::ApplyEditorCoherentPointDriftCommand(s.Commands(),
        {.SourceStableEntityId = Id(source), .TargetStableEntityId = Id(target)},
        [&](R::EditorCoherentPointDriftResult result) { ++delivered; last = std::move(result); });
    EXPECT_EQ(pending.Status, R::EditorCommandStatus::Pending);
    ASSERT_TRUE(jobs.DrainUntilTerminal());
    EXPECT_EQ(delivered, 1);
    ASSERT_TRUE(last.Succeeded()) << last.Message;
    EXPECT_NEAR(s.Registry.Raw().get<T::Component>(source).Position.y, 0.3f, 1e-3f);
}

TEST(CoherentPointDriftOperations, DeformedMeshesGetRecomputedNormalsAndParentedEntitiesAreRefused)
{
    Scene s;
    // An 8 x 5 planar grid, wound counter-clockwise seen from +z, with stored +z normals.
    constexpr int kColumns = 8, kRows = 5;
    Geometry::HalfedgeMesh::Mesh mesh;
    std::vector<Geometry::VertexHandle> v;
    std::vector<glm::vec3> points;
    for (int r = 0; r < kRows; ++r)
        for (int c = 0; c < kColumns; ++c)
        {
            points.push_back({0.25f * float(c) - 0.9f, 0.25f * float(r) - 0.5f, 0.0f});
            v.push_back(mesh.AddVertex(points.back()));
        }
    for (int r = 0; r + 1 < kRows; ++r)
        for (int c = 0; c + 1 < kColumns; ++c)
        {
            const auto at = [&](int rr, int cc) { return v[std::size_t(rr * kColumns + cc)]; };
            ASSERT_TRUE(mesh.AddTriangle(at(r, c), at(r, c + 1), at(r + 1, c + 1)).has_value());
            ASSERT_TRUE(mesh.AddTriangle(at(r, c), at(r + 1, c + 1), at(r + 1, c)).has_value());
        }
    mesh.VertexProperties().GetOrAdd<glm::vec3>("v:normal").Vector().assign(points.size(), glm::vec3(0.0f, 0.0f, 1.0f));
    const auto source = s.Registry.Create();
    s.Registry.Raw().emplace<T::Component>(source);
    GS::PopulateFromMesh(s.Registry.Raw(), source, mesh);
    // The target is the grid tilted by 25 degrees about x.
    const glm::mat3 tilt = glm::mat3(glm::rotate(glm::mat4(1.0f), glm::radians(25.0f), glm::vec3(1.0f, 0.0f, 0.0f)));
    std::vector<glm::vec3> tilted;
    for (const auto& p : points) tilted.push_back(tilt * p);
    const auto target = Make(s.Registry, D::PointCloudPoint, tilted);

    const R::CoherentPointDriftConfig config{.SourceStableEntityId = Id(source), .TargetStableEntityId = Id(target),
        .SourcePositions = {D::MeshVertex, "v:position", Geometry::PropertyValueKind::Vec3},
        .OutlierWeight = 0.0, .Output = O::Positions};
    const auto result = R::ApplyEditorCoherentPointDriftCommand(s.Commands(), config);
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    auto* props = R::ResolveGeometryPropertySet(R::BuildGeometryAvailability(s.Registry.Raw(), source), D::MeshVertex);
    const glm::vec3 expected = tilt * glm::vec3(0.0f, 0.0f, 1.0f);
    for (const std::size_t i : {std::size_t{0}, std::size_t{17}, points.size() - 1u})
        EXPECT_GT(glm::dot(props->Get<glm::vec3>("v:normal")[i], expected), 0.999f) << "vertex " << i;
    EXPECT_TRUE(s.Registry.Raw().all_of<Extrinsic::ECS::Components::DirtyTags::DirtyVertexNormals>(source));
    ASSERT_TRUE(s.History.Undo().Succeeded());
    EXPECT_EQ(props->Get<glm::vec3>("v:normal")[17], glm::vec3(0.0f, 0.0f, 1.0f));
    EXPECT_EQ(props->Get<glm::vec3>("v:position")[17], points[17]);

    // Parented entities: the operation maps points with the local transform only.
    const auto parent = s.Registry.Create();
    s.Registry.Raw().emplace<Extrinsic::ECS::Components::Hierarchy::Component>(source).Parent = parent;
    const auto parented = R::ApplyEditorCoherentPointDriftCommand(s.Commands(), config);
    EXPECT_EQ(parented.Status, R::EditorCommandStatus::InvalidProcessingParameters);
    EXPECT_NE(parented.Message.find("unparented"), std::string::npos) << parented.Message;
}

TEST(CoherentPointDriftOperations, NystromEStepReportsItsBackendAndSampledError)
{
    Scene s;
    const auto points = Cloud(1500, 23);
    std::vector<glm::vec3> moved;
    const glm::mat3 turn = glm::mat3(glm::rotate(glm::mat4(1.0f), 0.25f, glm::vec3(0.0f, 0.0f, 1.0f)));
    for (const auto& p : points) moved.push_back(turn * p + glm::vec3(0.1f, -0.05f, 0.0f));
    const auto source = Make(s.Registry, D::PointCloudPoint, points);
    const auto target = Make(s.Registry, D::PointCloudPoint, moved);
    const auto result = R::ApplyEditorCoherentPointDriftCommand(s.Commands(), {
        .SourceStableEntityId = Id(source), .TargetStableEntityId = Id(target), .OutlierWeight = 0.0,
        .EStep = R::CoherentPointDriftEStep::Nystrom, .NystromLandmarks = 64});
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_EQ(result.Backend, "cpu_nystrom");
    EXPECT_GT(result.EStepSampledError, 0.0);
    EXPECT_LE(result.EStepSampledError, 1e-3);
    EXPECT_NEAR(s.Registry.Raw().get<T::Component>(source).Position.x, 0.1f, 1e-3f);
}

// METHOD-076: the landmark variant is part of the section and reaches the solver: the same run
// with box landmarks reports a different sampled error than with point landmarks.
TEST(CoherentPointDriftOperations, NystromLocalSigmaReachesTheSolverTrace)
{
    ShiftedPair pair(150u, 11u);
    const auto run = pair.Start({.SourceStableEntityId = Id(pair.Source),
        .TargetStableEntityId = Id(pair.Target),
        .EStep = R::CoherentPointDriftEStep::Nystrom,
        .NystromLocalSigma = 10.0});
    ASSERT_TRUE(run);
    ASSERT_TRUE(Control(pair.Jobs, R::SnapshotEditorCoherentPointDrift(run).StepRun, R::StepRunAction::Step, 1u));
    ASSERT_TRUE(pair.Rest(run));
    const auto snapshot = R::SnapshotEditorCoherentPointDrift(run);
    ASSERT_EQ(snapshot.Trace.size(), 1u);
    EXPECT_EQ(snapshot.Trace[0].EStep, "truncated");
    EXPECT_TRUE(snapshot.Trace[0].NystromLocal);
}

TEST(CoherentPointDriftOperations, NystromLandmarkVariantRoundTripsAndReachesTheSolver)
{
    R::CoherentPointDriftConfig roundTrip{};
    roundTrip.NystromSource = R::CoherentPointDriftNystromSource::UnionRandom;
    roundTrip.NystromSchedule = R::CoherentPointDriftNystromSchedule::Resampled;
    roundTrip.NystromBoxPattern = R::CoherentPointDriftNystromBoxPattern::Halton;
    const auto decoded = R::DecodeCoherentPointDriftConfig(R::SerializeCoherentPointDriftConfig(roundTrip));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->NystromSource, R::CoherentPointDriftNystromSource::UnionRandom);
    EXPECT_EQ(decoded->NystromSchedule, R::CoherentPointDriftNystromSchedule::Resampled);
    EXPECT_EQ(decoded->NystromBoxPattern, R::CoherentPointDriftNystromBoxPattern::Halton);
    const auto registration = R::MakeCoherentPointDriftConfigSectionRegistration();
    EXPECT_FALSE(registration.Validate(R"({"nystrom_landmark_schedule": 3})", {}, "test").Usable());
    EXPECT_FALSE(registration.Validate(R"({"nystrom_landmark_source": 3})", {}, "test").Usable());

    Scene s;
    const auto points = Cloud(1500, 23);
    std::vector<glm::vec3> moved;
    const glm::mat3 turn = glm::mat3(glm::rotate(glm::mat4(1.0f), 0.25f, glm::vec3(0.0f, 0.0f, 1.0f)));
    for (const auto& p : points) moved.push_back(turn * p + glm::vec3(0.1f, -0.05f, 0.0f));
    const auto source = Make(s.Registry, D::PointCloudPoint, points);
    const auto target = Make(s.Registry, D::PointCloudPoint, moved);
    R::CoherentPointDriftConfig fixedOrder;
    fixedOrder.NystromSchedule = R::CoherentPointDriftNystromSchedule::Resampled;
    fixedOrder.LandmarkSampling.Method = R::PointSamplingMethod::SampleElimination;
    EXPECT_FALSE(registration.Validate(R::SerializeCoherentPointDriftConfig(fixedOrder), {}, "test").Usable());

    // Source, schedule and pattern each reach the solver: every variant reports its own sampled error.
    using Src = R::CoherentPointDriftNystromSource;
    using Sch = R::CoherentPointDriftNystromSchedule;
    using Pat = R::CoherentPointDriftNystromBoxPattern;
    struct Choice { Src Source; Sch Schedule; Pat Pattern; const char* Name; };
    std::vector<R::EditorCoherentPointDriftResult> results;
    for (const Choice choice : {Choice{Src::Points, Sch::Mixed, Pat::Random, "cpd_points"},
                                Choice{Src::BoundingBox, Sch::Mixed, Pat::Random, "cpd_box"},
                                Choice{Src::BoundingBox, Sch::Resampled, Pat::Random, "cpd_box_resampled"},
                                Choice{Src::BoundingBox, Sch::Mixed, Pat::Halton, "cpd_box_halton"},
                                Choice{Src::UnionRandom, Sch::Fixed, Pat::Random, "cpd_union"}})
    {
        // Here every variant reaches its largest sampled error within the first 10 iterations
        // (measured), so 10 iterations keep the values below at half the runtime.
        R::CoherentPointDriftConfig config{.SourceStableEntityId = Id(source),
            .TargetStableEntityId = Id(target),
            .OutlierWeight = 0.0,
            .Execution = Cap(10u),
            .EStep = R::CoherentPointDriftEStep::Nystrom,
            .NystromLandmarks = 64};
        config.Output = O::DisplacementProperty;
        // Box landmarks miss the default 1e-3 (box Halton even 0.05) in every iteration here (measured).
        config.NystromErrorLimit = 0.5;
        config.NystromSource = choice.Source;
        config.NystromSchedule = choice.Schedule;
        config.NystromBoxPattern = choice.Pattern;
        config.DisplacementName = choice.Name;
        results.push_back(R::ApplyEditorCoherentPointDriftCommand(s.Commands(), config));
        ASSERT_TRUE(results.back().Succeeded()) << results.back().Message;
        EXPECT_EQ(results.back().Backend, "cpu_nystrom") << choice.Name;
    }
    // Every variant approximates some iterations (limit 0.5; box Halton missed 0.05 in every
    // iteration) and reports its own largest sampled error (measured 0.47, 0.42, 0.36, 0.49).
    for (std::size_t a = 0; a < results.size(); ++a)
        for (std::size_t b = a + 1; b < results.size(); ++b)
            EXPECT_NE(results[a].EStepSampledError, results[b].EStepSampledError) << a << " vs " << b;
}

TEST(CoherentPointDriftOperations, VulkanEStepWithoutADeviceRunsOnTheCpuAndSaysWhy)
{
    // METHOD-056: without a framed device (synchronous and queued runs alike) every device-bound
    // iteration runs the exact CPU choice, and the result names the requested backend's absence.
    Scene s;
    const auto points = Cloud(300, 29);
    std::vector<glm::vec3> moved;
    for (const auto& p : points) moved.push_back(p + glm::vec3(0.15f, -0.1f, 0.05f));
    const auto source = Make(s.Registry, D::PointCloudPoint, points);
    const auto target = Make(s.Registry, D::PointCloudPoint, moved);
    const R::CoherentPointDriftConfig config{.SourceStableEntityId = Id(source), .TargetStableEntityId = Id(target),
                                             .EStep = R::CoherentPointDriftEStep::Vulkan};
    // The panel learns the same reason before a run starts.
    EXPECT_NE(R::CoherentPointDriftVulkanUnavailableReason(s.Commands()).find("job lane"), std::string::npos);
    const auto immediate = R::ApplyEditorCoherentPointDriftCommand(s.Commands(), config);
    ASSERT_TRUE(immediate.Succeeded()) << immediate.Message;
    EXPECT_EQ(immediate.Backend, "cpu_auto");
    EXPECT_GT(immediate.EStepFallbacks, 0u);
    EXPECT_NE(immediate.GpuDiagnostic.find("job lane"), std::string::npos) << immediate.GpuDiagnostic;
    EXPECT_NEAR(s.Registry.Raw().get<T::Component>(source).Position.x, 0.15f, 1e-3f);

    Extrinsic::Tests::EditorJobHarness jobs;
    jobs.Attach(s.Context);
    EXPECT_NE(R::CoherentPointDriftVulkanUnavailableReason(s.Commands()).find("Vulkan device"), std::string::npos);
    R::EditorCoherentPointDriftResult queued;
    auto next = config;
    next.Output = O::DisplacementProperty;
    (void)R::ApplyEditorCoherentPointDriftCommand(s.Commands(), next,
        [&](R::EditorCoherentPointDriftResult result) { queued = std::move(result); });
    ASSERT_TRUE(jobs.DrainUntilTerminal());
    ASSERT_TRUE(queued.Succeeded()) << queued.Message;
    EXPECT_EQ(queued.Backend, "cpu_auto");
    EXPECT_GT(queued.EStepFallbacks, 0u);
    EXPECT_NE(queued.GpuDiagnostic.find("Vulkan device"), std::string::npos) << queued.GpuDiagnostic;
}

TEST(CoherentPointDriftOperations, SamplingMethodsAreSelectableForSubsamplesAndLandmarks)
{
    const auto registration = R::MakeCoherentPointDriftConfigSectionRegistration();
    R::CoherentPointDriftConfig config{.Method = M::Bayesian, .Output = O::Positions, .Subsample = 300,
                                       .SubsampleTarget = 250};
    config.SubsampleSampling.Method = R::PointSamplingMethod::ProgressivePoisson;
    config.SubsampleSampling.PoissonBalanced = true;
    config.LandmarkSampling.Method = R::PointSamplingMethod::CoupledSieve;
    config.LandmarkSampling.Eta = 0.9;
    const auto decoded = R::DecodeCoherentPointDriftConfig(R::SerializeCoherentPointDriftConfig(config));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->SubsampleTarget, 250u);
    EXPECT_EQ(decoded->SubsampleSampling.Method, R::PointSamplingMethod::ProgressivePoisson);
    EXPECT_TRUE(decoded->SubsampleSampling.PoissonBalanced);
    EXPECT_EQ(decoded->LandmarkSampling.Method, R::PointSamplingMethod::CoupledSieve);
    EXPECT_EQ(decoded->LandmarkSampling.Eta, 0.9);
    EXPECT_NE(R::FindConfigFieldSpec(R::CoherentPointDriftConfigFieldSpecs(), "landmark_beta"), nullptr);
    const auto invalid = [&](R::CoherentPointDriftConfig c) {
        return !registration.Validate(R::SerializeCoherentPointDriftConfig(c), {}, "test").Usable();
    };
    R::CoherentPointDriftConfig exactCap;
    exactCap.LandmarkSampling = {.Method = R::PointSamplingMethod::CoupledSieve, .Eta = 1.0, .CandidateCap = 4};
    EXPECT_TRUE(invalid(exactCap));
    R::CoherentPointDriftConfig priority;
    priority.SubsampleSampling = {.Method = R::PointSamplingMethod::ProgressivePoisson,
                                  .PoissonSelection = R::PointSamplingPoissonSelection::FeaturePriority};
    EXPECT_TRUE(invalid(priority));
    EXPECT_TRUE(invalid({.SubsampleTarget = 2}));
    EXPECT_FALSE(registration.Validate(R"({"subsample_method": 99})", {}, "test").Usable());

    // A Bayesian run registers random subsamples; a low-rank nonrigid run uses random landmarks.
    Scene s;
    const auto points = Cloud(800, 51);
    std::vector<glm::vec3> moved;
    for (const auto& p : points) moved.push_back(p + glm::vec3(0.05f, -0.02f, 0.03f));
    const auto source = Make(s.Registry, D::PointCloudPoint, points);
    const auto target = Make(s.Registry, D::PointCloudPoint, moved);
    R::CoherentPointDriftConfig bayesian{.SourceStableEntityId = Id(source),
        .TargetStableEntityId = Id(target),
        .Method = M::Bayesian,
        .OutlierWeight = 0.1,
        .Execution = Cap(5u),
        .Output = O::DisplacementProperty,
        .DisplacementName = "bcpd",
        .Subsample = 300};
    bayesian.SubsampleSampling.Method = R::PointSamplingMethod::Random;
    bayesian.SubsampleSampling.Seed = 4u;
    const auto subsampled = R::ApplyEditorCoherentPointDriftCommand(s.Commands(), bayesian);
    ASSERT_TRUE(subsampled.Succeeded()) << subsampled.Message;
    R::CoherentPointDriftConfig lowRank{.SourceStableEntityId = Id(source),
        .TargetStableEntityId = Id(target),
        .Method = M::Nonrigid,
        .OutlierWeight = 0.0,
        .Execution = Cap(5u),
        .Output = O::DisplacementProperty,
        .DisplacementName = "warp",
        .LowRank = 30};
    lowRank.LandmarkSampling.Method = R::PointSamplingMethod::Random;
    const auto result = R::ApplyEditorCoherentPointDriftCommand(s.Commands(), lowRank);
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_GT(result.KernelRank, 0u);
}

// RUNTIME-289: a subsample asked on Vulkan without a framed device is ordered on the CPU, per
// set, with the reason; sets that are not subsampled report no backend.
TEST(CoherentPointDriftOperations, VulkanSubsamplesWithoutADeviceRunOnTheCpuAndSayWhy)
{
    R::CoherentPointDriftConfig roundTrip{.Method = M::Bayesian, .Output = O::Positions};
    roundTrip.SubsampleBackend = R::PointSamplingBackend::Vulkan;
    const auto decoded = R::DecodeCoherentPointDriftConfig(R::SerializeCoherentPointDriftConfig(roundTrip));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->SubsampleBackend, R::PointSamplingBackend::Vulkan);

    Scene s;
    const auto points = Cloud(300, 61);
    std::vector<glm::vec3> moved;
    for (const auto& p : points) moved.push_back(p + glm::vec3(0.05f, -0.02f, 0.03f));
    const auto source = Make(s.Registry, D::PointCloudPoint, points);
    const auto target = Make(s.Registry, D::PointCloudPoint, moved);
    R::CoherentPointDriftConfig config{.SourceStableEntityId = Id(source),
        .TargetStableEntityId = Id(target),
        .Method = M::Bayesian,
        .OutlierWeight = 0.1,
        .Execution = Cap(5u),
        .Output = O::DisplacementProperty,
        .DisplacementName = "bcpd",
        .Subsample = 100};
    config.SubsampleBackend = R::PointSamplingBackend::Vulkan;
    const auto immediate = R::ApplyEditorCoherentPointDriftCommand(s.Commands(), config);
    ASSERT_TRUE(immediate.Succeeded()) << immediate.Message;
    EXPECT_EQ(immediate.SubsampleRequestedBackend, "gpu_vulkan_compute");
    EXPECT_EQ(immediate.SubsampleSourceBackend, "cpu_reference");
    EXPECT_EQ(immediate.SubsampleTargetBackend, "");
    EXPECT_EQ(immediate.SubsampleBackendDiagnostic, "Source: No job lane or spatial compute service; sampling ran on the CPU.");

    auto plainConfig = config;
    plainConfig.Method = M::Nonrigid;
    plainConfig.DisplacementName = "warp";
    const auto plain = R::ApplyEditorCoherentPointDriftCommand(s.Commands(), plainConfig);
    ASSERT_TRUE(plain.Succeeded()) << plain.Message;
    EXPECT_EQ(plain.SubsampleSourceBackend, "");
    EXPECT_EQ(plain.SubsampleTargetBackend, "");
    EXPECT_EQ(plain.SubsampleBackendDiagnostic, "");

    Extrinsic::Tests::EditorJobHarness jobs;
    jobs.Attach(s.Context);
    config.SubsampleTarget = 120;
    config.DisplacementName = "bcpd_queued";
    R::EditorCoherentPointDriftResult queued;
    (void)R::ApplyEditorCoherentPointDriftCommand(s.Commands(), config,
        [&](R::EditorCoherentPointDriftResult result) { queued = std::move(result); });
    ASSERT_TRUE(jobs.DrainUntilTerminal());
    ASSERT_TRUE(queued.Succeeded()) << queued.Message;
    EXPECT_EQ(queued.SubsampleSourceBackend, "cpu_reference");
    EXPECT_EQ(queued.SubsampleTargetBackend, "cpu_reference");
    EXPECT_NE(queued.SubsampleBackendDiagnostic.find("Target: No job lane or spatial compute service"), std::string::npos)
        << queued.SubsampleBackendDiagnostic;
}

TEST(CoherentPointDriftOperations, VulkanEStepBrokerFailsClosedWithoutAWorkspace)
{
    // ARCH-023: the run's Init prepares the workspace; without a device lease it closes the broker at once and every
    // later E-step answers false (the solver runs it on the CPU) without waiting.
    R::SpatialIndexCache cache; // no device: no workspace lease
    Extrinsic::Tests::MockDevice device;
    const std::vector<double> x{0.0, 1.0}, zero(2, 0.0), weights(2, 0.0);
    std::vector<double> logDen(2), pt1(2), p1(2), px(2), py(2), pz(2);
    const Geometry::CoherentPointDrift::EStep::ExternalRequest request{
        .Target = {x, zero, zero}, .Moved = {x, zero, zero}, .TargetGeneration = 1u, .Sigma2 = 1.0,
        .LogOutlier = -std::numeric_limits<double>::infinity(), .LogWeights = weights,
        .LogDenominator = logDen, .Pt1 = pt1, .P1 = p1, .PXx = px, .PXy = py, .PXz = pz};
    R::CoherentPointDriftGpuEStep broker(cache, device);
    EXPECT_FALSE(broker.Prepare(2u, 2u));
    EXPECT_TRUE(broker.Closed());
    EXPECT_FALSE(broker.Diagnostic().empty());
    EXPECT_EQ(broker.Stats().Failed, 1u);
    EXPECT_FALSE(broker.Evaluate(request)) << "a closed broker answers at once";
    EXPECT_EQ(broker.Stats().Requests, 0u);
}

namespace
{
    // ARCH-023: a broker on a cache bound to a mock device whose submissions record the real workspace. A page's
    // pulled readback completes at once, or, while Hold is set, stays pending until Release; the worker waits through
    // WaitCompute and takes its bytes through TakeCompletedReadback (every double is its index, or Answer's), with no
    // frame, drain or main-thread pump. Fields the worker's hooks touch are guarded by Gpu.
    class CpdBrokerWorkspaceReuse : public ::testing::Test
    {
    protected:
        explicit CpdBrokerWorkspaceReuse(unsigned workers = 2u) : Jobs(workers) {}
        Extrinsic::Tests::MockDevice Device;
        Extrinsic::Tests::EditorJobHarness Jobs;
        R::WorldRegistry Worlds;
        R::CommandBus Commands;
        R::KernelEventBus Events;
        R::ServiceRegistry Services;
        R::SpatialIndexCache Cache;
        std::mutex Gpu;
        bool Hold{false}; // the next page stays pending until Release
        std::uint64_t HeldToken{0u}; // the page held now (0: none)
        std::uint64_t Tokens{0u}, Taken{0u}, Released{0u};
        bool AllPulled{true}; // every E-step page asked for a pulled readback
        std::function<std::vector<double>(std::size_t)> Answer{};
        bool Stopped{false};

        // One request's storage: n targets on the x axis, m sources, a target generation.
        struct Case
        {
            std::vector<double> TX, TZero, SX, SZero, Weights, LogDen, Pt1, P1, PX, PY, PZ;
            std::uint64_t Generation{};
            Case(std::size_t n, std::size_t m, std::uint64_t generation, double offset = 0.0)
                : TX(n), TZero(n, 0.0), SX(m), SZero(m, 0.0), Weights(m, 0.0), LogDen(n), Pt1(n), P1(m), PX(m),
                  PY(m), PZ(m), Generation(generation)
            {
                for (std::size_t j = 0; j < n; ++j) TX[j] = offset + double(j);
                for (std::size_t i = 0; i < m; ++i) SX[i] = 0.5 + double(i);
            }
            [[nodiscard]] Geometry::CoherentPointDrift::EStep::ExternalRequest Request()
            {
                return {.Target = {TX, TZero, TZero}, .Moved = {SX, SZero, SZero}, .TargetGeneration = Generation,
                        .Sigma2 = 1.0, .LogOutlier = -std::numeric_limits<double>::infinity(), .LogWeights = Weights,
                        .LogDenominator = LogDen, .Pt1 = Pt1, .P1 = P1, .PXx = PX, .PXy = PY, .PXz = PZ};
            }
        };

        void SetUp() override
        {
            namespace RHI = Extrinsic::RHI;
            Device.ShaderFloat64 = true;
            Device.ComputeReadback = [this](auto record, std::uint64_t, RHI::ReadbackSink sink)
            {
                if (!record(Device.CommandContext).IsValid())
                    return RHI::ReadbackToken{};
                std::scoped_lock lock{Gpu};
                AllPulled = AllPulled && sink.Pulled;
                const RHI::ReadbackToken token{++Tokens};
                if (std::exchange(Hold, false))
                    HeldToken = token.Value;
                return token;
            };
            Device.TransferQueue.ComputeWait = [this](RHI::ReadbackToken token, std::uint64_t)
            {
                {
                    std::scoped_lock lock{Gpu};
                    if (token.Value != HeldToken)
                        return RHI::ComputeWaitStatus::Complete;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
                return RHI::ComputeWaitStatus::Pending;
            };
            // An invalid token names no submission and is complete (as the default mock answers): the workspace pool's
            // retirement stamp carries the compute fence (ARCH-023), which is 0 here, and must retire with its frames.
            Device.TransferQueue.ReadbackComplete = [this](RHI::ReadbackToken token)
            {
                std::scoped_lock lock{Gpu};
                return !token.IsValid() || token.Value != HeldToken;
            };
            Device.TransferQueue.TakeReadback = [this](RHI::ReadbackToken, std::span<std::byte> destination)
            {
                std::vector<double> values(destination.size() / sizeof(double));
                if (Answer)
                    values = Answer(values.size());
                else
                    for (std::size_t k = 0; k < values.size(); ++k)
                        values[k] = double(k);
                std::memcpy(
                    destination.data(), values.data(), std::min(destination.size(), values.size() * sizeof(double)));
                std::scoped_lock lock{Gpu};
                ++Taken;
                return true;
            };
            Device.TransferQueue.ReleaseReadbackHook = [this](RHI::ReadbackToken)
            {
                std::scoped_lock lock{Gpu};
                ++Released;
            };
            Services.BeginRegistration();
            ASSERT_TRUE(Services.Provide<Extrinsic::RHI::IDevice>(Device, "test").has_value());
            R::EngineSetup setup{Commands, Events, Jobs.Jobs(), Worlds, Services, [](R::FramePhase, R::RuntimeFrameHook) {}};
            ASSERT_TRUE(Cache.OnRegister(setup).has_value());
        }
        void TearDown() override
        {
            if (std::exchange(Stopped, true)) return;
            R::RuntimeModuleShutdownContext shutdown{Commands, Events, Jobs.Jobs(), Worlds, Services};
            Cache.OnShutdown(shutdown);
        }
        void HoldNext()
        {
            std::scoped_lock lock{Gpu};
            Hold = true;
        }
        // The held page completes (the device finished it).
        void Release()
        {
            std::scoped_lock lock{Gpu};
            HeldToken = 0u;
            Hold = false;
        }
        [[nodiscard]] bool Waiting()
        {
            std::scoped_lock lock{Gpu};
            return HeldToken != 0u;
        }
        [[nodiscard]] std::uint64_t Count(const std::uint64_t& counter)
        {
            std::scoped_lock lock{Gpu};
            return counter;
        }
        // Waits (bounded) for a page to be held, draining the job lane so a step run's pump can start its segment.
        [[nodiscard]] bool AwaitHeld(const bool drain)
        {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
            while (!Waiting() && std::chrono::steady_clock::now() < deadline)
            {
                if (drain)
                    (void)Jobs.Jobs().DrainCompletions(Jobs.Events());
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
            }
            return Waiting();
        }
        // Retires every frame that may have used released buffers.
        void RetireFrames()
        {
            for (int i = 0; i < 2; ++i)
            {
                Device.GlobalFrameNumber += Device.FramesInFlight + 1u;
                Jobs.Jobs().RecordGpuQueueFrameCommands(Device.CommandContext);
                (void)Jobs.Jobs().DrainGpuQueueCompletedTransfers();
                Cache.Prune();
            }
        }
        // ARCH-023: a worker's E-step writes its rows into the prepared staging (IDevice::WriteMappedBuffer) and copies
        // them on the compute lane; an owner WriteBuffer would be a regression.
        [[nodiscard]] std::size_t WritesOf(std::size_t bytes)
        {
            std::scoped_lock lock{Device.MappedWriteMutex};
            EXPECT_EQ(std::ranges::count_if(Device.BufferWrites,
                          [&](const auto& w)
                          {
                              return w.Data.size() == bytes;
                          }),
                0)
                << "no owner WriteBuffer of E-step rows";
            return std::size_t(std::ranges::count_if(Device.MappedWrites,
                [&](const auto& w)
                {
                    return w.Data.size() == bytes;
                }));
        }
    };

    class CpdSingleWorkerWait : public CpdBrokerWorkspaceReuse
    {
    protected:
        CpdSingleWorkerWait() : CpdBrokerWorkspaceReuse(1u) {}
    };
}

TEST_F(CpdBrokerWorkspaceReuse, TimedOutEStepClosesTheBrokerOnItsWorker)
{
    // The deadline is observed by the waiting worker itself (no owner pump, no timer thread): the page is given up,
    // the broker closes and the solver runs that E-step on the CPU.
    Case c(2, 2, 11);
    R::CoherentPointDriftGpuEStep broker(Cache, Device, std::chrono::milliseconds{20});
    ASSERT_TRUE(broker.Prepare(2u, 2u)) << broker.Diagnostic();
    HoldNext();
    const auto started = std::chrono::steady_clock::now();
    EXPECT_FALSE(broker.Evaluate(c.Request()));
    EXPECT_GE(std::chrono::steady_clock::now() - started, std::chrono::milliseconds{20});
    EXPECT_TRUE(broker.Closed());
    EXPECT_NE(broker.Diagnostic().find("in time"), std::string::npos) << broker.Diagnostic();
    EXPECT_EQ(Count(Released), 1u) << "the late page is given up, never taken";
    EXPECT_EQ(Count(Taken), 0u);
    EXPECT_TRUE(AllPulled) << "the E-step's statistics are pulled by its worker";
    Release();
}

TEST_F(CpdSingleWorkerWait, PendingGpuEStepCompletesOnItsWorkerWithoutDrains)
{
    // ARCH-023 replaces the parked coroutine and its main-thread pump: the step's worker waits for its own page
    // (one worker per active run, as a CPU step run), so the iteration completes with no drain, frame or pump once
    // the device finished.
    Scene scene;
    const auto points = Cloud(64, 31);
    auto shifted = points;
    for (auto& point : shifted) point += glm::vec3{0.02f, -0.01f, 0.03f};
    const auto source = Make(scene.Registry, D::PointCloudPoint, points);
    const auto target = Make(scene.Registry, D::PointCloudPoint, shifted);
    Jobs.Attach(scene.Context);
    scene.Context.SpatialIndices = &Cache;
    scene.Context.Device = &Device;
    R::EditorCoherentPointDriftResult failure;
    HoldNext();
    const auto run = R::StartEditorCoherentPointDrift(scene.Commands(),
        {.SourceStableEntityId = Id(source),
            .TargetStableEntityId = Id(target),
            .Execution = Cap(1u),
            .InitialSigma2 = 10.0,
            .EStep = R::CoherentPointDriftEStep::Vulkan,
            .Threads = 1u},
        failure);
    ASSERT_TRUE(run) << failure.Message;
    const auto id = R::SnapshotEditorCoherentPointDrift(run).StepRun;
    ASSERT_TRUE(Control(Jobs, id, R::StepRunAction::Step, 1u));
    const auto cleanup = [&]
    {
        Release();
        std::string refusal;
        (void)Jobs.StepRuns().Control(id, R::StepRunAction::Discard, 1u, refusal);
        (void)Jobs.Jobs().CancelAll();
        (void)Jobs.Jobs().DrainCompletions(Jobs.Events());
        (void)Jobs.Jobs().ShutdownGpuQueueParticipants([&] { Device.WaitIdle(); });
        Jobs.Jobs().CancelAndDrain();
    };
    if (!AwaitHeld(true))
    {
        cleanup();
        FAIL() << "The actual CPD operation never reached its held device E-step.";
    }
    Release(); // no drain, frame or pump from here on
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
    while (Jobs.StepRuns().Observe(id)->Steps < 1u && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    EXPECT_EQ(Jobs.StepRuns().Observe(id)->Steps, 1u) << "the iteration completed on its worker";
    EXPECT_EQ(Count(Taken), 1u);
    EXPECT_TRUE(AllPulled);
    cleanup();
}

// ARCH-023: a running Vulkan E-step run advances on its worker alone: once its segment started, three EM iterations
// (three device pages, each pulled and taken by the worker) complete with no drain, frame or pump on this thread.
// Prediction: Steps 3, Taken 3, three trace rows, every page pulled. Mutation: restoring a main-thread pump (or an
// ordinary sink) leaves Steps at 0 until a drain runs.
TEST_F(CpdSingleWorkerWait, ARunningVulkanRunAdvancesThreeIterationsWithNoDrainOrFrame)
{
    constexpr std::size_t kSources = 48u, kTargets = 64u;
    const auto sourcePoints = Cloud(kSources, 41);
    auto targetPoints = Cloud(kTargets, 43);
    for (std::size_t i = 0; i < kSources; ++i)
        targetPoints[i] = sourcePoints[i] + glm::vec3(0.1f, -0.05f, 0.02f);
    Scene scene;
    const auto source = Make(scene.Registry, D::PointCloudPoint, sourcePoints);
    const auto target = Make(scene.Registry, D::PointCloudPoint, targetPoints);
    Jobs.Attach(scene.Context);
    scene.Context.SpatialIndices = &Cache;
    scene.Context.Device = &Device;
    // A consistent soft E-step: source i gives weight 1-w to target i and w to target i+1 (cyclic over the first
    // kSources targets, so each of them still receives Pt1 = 1), w growing every call. The exact one-to-one answer
    // this replaces drove sigma^2 to its floor after the first M-step (SigmaFloor ended the run at one iteration); a
    // fixed soft answer repeats one objective and converges at the third E-step. This one keeps sigma^2 positive and
    // changes the objective every iteration, so only the cap ends the run (the solver's rules are unchanged).
    Answer = [&, call = 0u](const std::size_t doubles) mutable
    {
        std::vector<double> values(doubles, 0.0);
        if (values.size() >= 2u * kTargets + 4u * kSources)
        {
            const double w = 0.1 * double(++call);
            for (std::size_t i = 0; i < kSources; ++i)
            {
                const auto& own = targetPoints[i];
                const auto& following = targetPoints[(i + 1u) % kSources];
                values[kTargets + i] = 1.0;
                values[2u * kTargets + i] = 1.0;
                values[2u * kTargets + kSources + i] = (1.0 - w) * own.x + w * following.x;
                values[2u * kTargets + 2u * kSources + i] = (1.0 - w) * own.y + w * following.y;
                values[2u * kTargets + 3u * kSources + i] = (1.0 - w) * own.z + w * following.z;
            }
        }
        return values;
    };
    R::EditorCoherentPointDriftResult failure;
    HoldNext();
    const auto run = R::StartEditorCoherentPointDrift(scene.Commands(),
        {.SourceStableEntityId = Id(source),
            .TargetStableEntityId = Id(target),
            .Execution = Cap(3u),
            .Tolerance = 1.0e-12, // no early convergence: the cap ends the run
            .InitialSigma2 = 10.0,
            .NormalizeInputs = false,
            .EStep = R::CoherentPointDriftEStep::Vulkan,
            .Threads = 1u},
        failure);
    ASSERT_TRUE(run) << failure.Message;
    const auto id = R::SnapshotEditorCoherentPointDrift(run).StepRun;
    ASSERT_TRUE(Control(Jobs, id, R::StepRunAction::Run));
    const auto cleanup = [&]
    {
        Release();
        (void)Jobs.Jobs().CancelAll();
        (void)Jobs.Jobs().DrainCompletions(Jobs.Events());
        (void)Jobs.Jobs().ShutdownGpuQueueParticipants(
            [&]
            {
                Device.WaitIdle();
            });
        Jobs.Jobs().CancelAndDrain();
    };
    if (!AwaitHeld(true))
    {
        cleanup();
        FAIL() << "the run's segment never submitted its first device E-step";
    }
    Release(); // from here on this thread neither drains nor renders
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (Jobs.StepRuns().Observe(id)->Steps < 3u && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    EXPECT_EQ(Jobs.StepRuns().Observe(id)->Steps, 3u);
    EXPECT_EQ(Count(Taken), 3u) << "three device pages, each taken once by the worker";
    EXPECT_TRUE(AllPulled);
    (void)Jobs.Jobs().DrainCompletions(Jobs.Events());
    EXPECT_EQ(R::SnapshotEditorCoherentPointDrift(run).Trace.size(), 3u);
    EXPECT_EQ(R::SnapshotEditorCoherentPointDrift(run).Result.EStepFallbacks, 0u);
    cleanup();
}

TEST_F(CpdSingleWorkerWait, EditorDiscardWakesPendingEStepWithoutFallbackOrIteration)
{
    Scene scene;
    const auto points = Cloud(64, 31);
    auto shifted = points;
    for (auto& point : shifted) point += glm::vec3{0.02f, -0.01f, 0.03f};
    const auto source = Make(scene.Registry, D::PointCloudPoint, points);
    const auto target = Make(scene.Registry, D::PointCloudPoint, shifted);
    Jobs.Attach(scene.Context);
    scene.Context.SpatialIndices = &Cache;
    scene.Context.Device = &Device;
    R::EditorCoherentPointDriftResult failure;
    HoldNext();
    const auto run = R::StartEditorCoherentPointDrift(scene.Commands(),
        {.SourceStableEntityId = Id(source),
            .TargetStableEntityId = Id(target),
            .Execution = Cap(1u),
            .InitialSigma2 = 10.0,
            .EStep = R::CoherentPointDriftEStep::Vulkan,
            .Threads = 1u},
        failure);
    ASSERT_TRUE(run) << failure.Message;
    const auto id = R::SnapshotEditorCoherentPointDrift(run).StepRun;
    ASSERT_TRUE(Control(Jobs, id, R::StepRunAction::Step, 1u));
    const auto cleanup = [&]
    {
        Release();
        (void)Jobs.Jobs().CancelAll();
        (void)Jobs.Jobs().DrainCompletions(Jobs.Events());
        (void)Jobs.Jobs().ShutdownGpuQueueParticipants([&] { Device.WaitIdle(); });
        Jobs.Jobs().CancelAndDrain();
    };
    if (!AwaitHeld(true))
    {
        ADD_FAILURE() << "The real CPD step did not submit its device E-step.";
        cleanup();
        return;
    }
    ASSERT_TRUE(Control(Jobs, id, R::StepRunAction::Discard));
    const auto cancelDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{500};
    while (Jobs.StepRuns().Observe(id)->SegmentInFlight && std::chrono::steady_clock::now() < cancelDeadline)
    {
        (void)Jobs.Jobs().DrainCompletions(Jobs.Events());
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    EXPECT_FALSE(Jobs.StepRuns().Observe(id)->SegmentInFlight) << "Discard must wake the waiting step by itself.";
    EXPECT_TRUE(Waiting()) << "The GPU must still be pending until all assertions finish.";
    EXPECT_EQ(Count(Released), 1u) << "the abandoned page is given up";
    EXPECT_EQ(Count(Taken), 0u);
    const auto snapshot = R::SnapshotEditorCoherentPointDrift(run);
    EXPECT_EQ(Jobs.StepRuns().Observe(id)->Phase, R::StepRunPhase::Discarded);
    EXPECT_EQ(snapshot.Result.EStepFallbacks, 0u);
    EXPECT_TRUE(snapshot.Trace.empty());
    cleanup();
}

// ARCH-023 Q7: a run whose device subsample ordering is pending at its start is owner-driven (its pump job submits
// the iterations). Cancelled from the Jobs window while an iteration runs, its holder, pump and iteration end
// together, and the pump's finalizer can run first (here the pump row is cancelled and drained first). The run
// answers once, at its settled end after the held iteration returned: a same-source run started from that answer is
// admitted. Prediction, the pump finalizer abandoning the run: one delivery in the pump's drain while the iteration
// is held, the run no longer in flight, the start inside it refused as busy. Fixed: none while held, then exactly
// one cancelled result, and the new run is ready with its own step run.
TEST_F(CpdBrokerWorkspaceReuse, ACancelledOwnerDrivenRunAnswersOnceItsHeldIterationReturned)
{
    Scene scene;
    const auto source = Make(scene.Registry, D::PointCloudPoint, Cloud(64, 31));
    const auto target = Make(scene.Registry, D::PointCloudPoint, Cloud(64, 37));
    Jobs.Attach(scene.Context);
    scene.Context.SpatialIndices = &Cache;
    scene.Context.Device = &Device;
    // The iteration's device E-step waits on its worker until released; closing the broker does not end this wait.
    const auto held = std::make_shared<std::atomic<bool>>(true);
    const auto entered = std::make_shared<std::atomic<bool>>(false);
    Device.TransferQueue.ComputeWait = [held, entered, base = Device.TransferQueue.ComputeWait](
                                           const Extrinsic::RHI::ReadbackToken token, const std::uint64_t timeout)
    {
        *entered = true;
        while (*held)
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        return base(token, timeout);
    };
    unsigned delivered = 0u;
    std::optional<R::EditorCoherentPointDriftResult> ended;
    R::EditorCoherentPointDriftRunHandle next;
    R::EditorCoherentPointDriftResult refused;
    // The mock refuses uploads, so the pending source ordering falls back to the CPU when it is submitted: the run
    // stays owner-driven and its first iteration depends on nothing.
    R::CoherentPointDriftConfig config{.SourceStableEntityId = Id(source),
        .TargetStableEntityId = Id(target),
        .Method = M::Bayesian,
        .Execution = Cap(10000u),
        .InitialSigma2 = 10.0,
        .Output = O::DisplacementProperty,
        .DisplacementName = "bcpd",
        .EStep = R::CoherentPointDriftEStep::Vulkan,
        .Threads = 1u,
        .Subsample = 32u};
    config.SubsampleBackend = R::PointSamplingBackend::Vulkan;
    const auto pending = R::ApplyEditorCoherentPointDriftCommand(scene.Commands(),
        config,
        [&](R::EditorCoherentPointDriftResult result)
        {
            ++delivered;
            ended = std::move(result);
            next = R::StartEditorCoherentPointDrift(
                scene.Commands(), {.SourceStableEntityId = Id(source), .TargetStableEntityId = Id(target)}, refused);
        });
    const auto rows = [&]
    {
        return scene.Context.JobCommands.SnapshotAll();
    };
    R::JobToken pump{}, holder{};
    for (const auto& row : rows())
        if (row.Identity.OutputName == "coherent_point_drift.pump")
        {
            pump = row.Token;
            holder = row.Identity.Run;
        }
    // The old run's active jobs: its holder and every job that joined it.
    const auto active = [&]
    {
        return std::ranges::count_if(rows(),
            [&](const R::EditorJobRecord& row)
            {
                return (row.Token == holder || row.Identity.Run == holder) && R::IsActiveEditorJobState(row.State);
            });
    };
    const auto cleanup = [&]
    {
        *held = false;
        (void)Jobs.Jobs().CancelAll();
        (void)Jobs.Jobs().DrainCompletions(Jobs.Events());
        (void)Jobs.Jobs().ShutdownGpuQueueParticipants(
            [&]
            {
                Device.WaitIdle();
            });
        Jobs.Jobs().CancelAndDrain();
    };
    if (pending.Status != R::EditorCommandStatus::Pending || !pump.IsValid() || !holder.IsValid())
    {
        cleanup();
        FAIL() << "the pending device ordering makes the run owner-driven, its pump joining the holder: "
               << pending.Message;
    }
    EXPECT_NE(pending.SubsampleBackendDiagnostic.find("Resident sampling input acquisition refused"), std::string::npos)
        << pending.SubsampleBackendDiagnostic;
    const auto id = pending.StepRun;
    if (!DrainUntil(
            Jobs,
            [&]
            {
                return entered->load();
            },
            std::chrono::seconds{5}))
    {
        cleanup();
        FAIL() << "the pump's iteration never reached its held device E-step";
    }

    // The Jobs window: the pump row first (its finalizer runs in this drain), then the run (holder and iteration).
    EXPECT_EQ(R::CancelEditorRuns(scene.Context.JobCommands, std::span<const R::JobToken>{&pump, 1u}).Requested, 1u);
    (void)Jobs.Jobs().DrainCompletions(Jobs.Events());
    EXPECT_EQ(delivered, 0u) << "no answer from the pump's finalizer while the iteration runs";
    EXPECT_EQ(R::CancelEditorRuns(scene.Context.JobCommands, std::span<const R::JobToken>{&holder, 1u}).Requested, 2u)
        << "the holder and the held iteration";
    for (int drain = 0; drain < 20; ++drain)
    {
        (void)Jobs.Jobs().DrainCompletions(Jobs.Events());
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    EXPECT_EQ(delivered, 0u) << "no answer while the iteration is held";
    const auto whileHeld = Jobs.StepRuns().Observe(id);
    EXPECT_TRUE(whileHeld && whileHeld->Phase == R::StepRunPhase::Discarded && whileHeld->SegmentInFlight)
        << "discarded, its held iteration still in flight";
    EXPECT_EQ(active(), 1) << "only the held iteration: the holder and the pump are finalized";

    *held = false;
    EXPECT_TRUE(DrainUntil(Jobs,
        [&]
        {
            return delivered > 0u;
        }));
    for (int drain = 0; drain < 5; ++drain)
        (void)Jobs.Jobs().DrainCompletions(Jobs.Events());
    EXPECT_EQ(delivered, 1u);
    EXPECT_EQ(active(), 0) << "no stranded holder, pump or later iteration";
    const auto settled = Jobs.StepRuns().Observe(id);
    EXPECT_TRUE(settled && !settled->SegmentInFlight && settled->Steps == 0u);
    if (ended)
    {
        EXPECT_EQ(ended->Status, R::EditorCommandStatus::StaleEntity);
        EXPECT_NE(ended->Message.find("cancelled"), std::string::npos) << ended->Message;
        EXPECT_EQ(ended->StepRun, id);
    }
    EXPECT_TRUE(next) << "admitted, not refused as busy: " << refused.Message;
    if (next)
    {
        const auto nextId = R::SnapshotEditorCoherentPointDrift(next).StepRun;
        EXPECT_NE(nextId, 0u);
        EXPECT_NE(nextId, id);
        const auto ready = Jobs.StepRuns().Observe(nextId);
        EXPECT_TRUE(ready && ready->Phase == R::StepRunPhase::Ready);
    }
    cleanup();
    EXPECT_EQ(delivered, 1u) << "one terminal result for the cancelled run";
}

TEST_F(CpdBrokerWorkspaceReuse, StepsAndRunsReuseThePooledWorkspaceAndUploadEachTargetOnce)
{
    // n = 3 targets (96 bytes as float-float), m = 2 sources (64 bytes); the Init prepares 3 x 4.
    constexpr std::size_t kTargetBytes = 3u * 32u, kSourceBytes = 2u * 32u;
    Case first(3, 2, 101);
    R::CoherentPointDriftGpuEStep run(Cache, Device);
    ASSERT_TRUE(run.Prepare(3u, 4u)) << run.Diagnostic();
    EXPECT_EQ(Device.CreatePipelineCount, 2) << "the Init creates both pipelines";
    const int buffers = Device.CreateBufferCount;
    ASSERT_TRUE(run.Evaluate(first.Request())) << run.Diagnostic();
    // The readback lands in the solver's spans in the LogDenominator, Pt1, P1, PX order.
    EXPECT_EQ(first.LogDen, (std::vector<double>{0, 1, 2}));
    EXPECT_EQ(first.Pt1, (std::vector<double>{3, 4, 5}));
    EXPECT_EQ(first.P1, (std::vector<double>{6, 7}));
    EXPECT_EQ(first.PZ, (std::vector<double>{12, 13}));
    ASSERT_TRUE(run.Evaluate(first.Request()));
    EXPECT_EQ(Device.CreatePipelineCount, 2);
    EXPECT_EQ(Device.CreateBufferCount, buffers) << "a worker's E-step creates nothing";
    EXPECT_EQ(WritesOf(kTargetBytes), 1u) << "the fixed target is uploaded once per run";
    EXPECT_EQ(WritesOf(kSourceBytes), 2u) << "the moved source every iteration";

    // Frames retire meanwhile: the run keeps its lease, so the next step reuses its pipelines, buffers and the
    // uploaded target of this generation.
    RetireFrames();
    ASSERT_TRUE(run.Evaluate(first.Request()));
    EXPECT_EQ(Cache.Stats().WorkspaceLeases, 1u) << "one lease for the whole run";
    EXPECT_GT(Cache.Stats().ScratchActiveBytes, 0u) << "the run's buffers stay pinned between steps";
    EXPECT_EQ(Device.CreateBufferCount, buffers);
    EXPECT_EQ(WritesOf(kTargetBytes), 1u);

    // Every shape up to the prepared one fits without growth; a larger one closes the broker (the CPU goes on).
    Case wider(3, 4, 101);
    ASSERT_TRUE(run.Evaluate(wider.Request()));
    EXPECT_EQ(Device.CreateBufferCount, buffers);
    ASSERT_TRUE(run.Evaluate(first.Request()));
    EXPECT_EQ(first.P1, (std::vector<double>{6, 7})) << "the readback is the current shape's prefix";
    Case beyond(3, 5, 101);
    EXPECT_FALSE(run.Evaluate(beyond.Request()));
    EXPECT_TRUE(run.Closed());
    EXPECT_NE(run.Diagnostic().find("prepared"), std::string::npos) << run.Diagnostic();
    EXPECT_EQ(Device.CreateBufferCount, buffers);
    run.ReleaseDeviceResources();
    RetireFrames();

    // Another run (a new target generation of the same size) on the same pooled workspace uploads its own target;
    // generation 0 is never taken as current.
    Case other(3, 2, 202, 10.0);
    R::CoherentPointDriftGpuEStep next(Cache, Device);
    ASSERT_TRUE(next.Prepare(3u, 2u)) << next.Diagnostic();
    ASSERT_TRUE(next.Evaluate(other.Request())) << next.Diagnostic();
    EXPECT_EQ(Cache.Stats().WorkspaceReuses, 1u) << "the terminal release returned the run's workspace";
    EXPECT_EQ(Device.CreatePipelineCount, 2);
    ASSERT_EQ(WritesOf(kTargetBytes), 2u) << "a pooled workspace never keeps another run's target";
    const auto& upload = *std::ranges::find_if(Device.MappedWrites | std::views::reverse,
        [&](const auto& w)
        {
            return w.Data.size() == kTargetBytes;
        });
    float x0{};
    std::memcpy(&x0, upload.Data.data(), sizeof(float));
    EXPECT_EQ(x0, 10.0f);
    Case unversioned(3, 2, 0);
    ASSERT_TRUE(next.Evaluate(unversioned.Request()));
    ASSERT_TRUE(next.Evaluate(unversioned.Request()));
    EXPECT_EQ(WritesOf(kTargetBytes), 4u);
    EXPECT_EQ(next.Stats().Completed, 3u);
    EXPECT_EQ(next.Stats().Failed, 0u);
    EXPECT_TRUE(AllPulled);
}

// ARCH-023: a Vulkan E-step run holds its broker, and with it the workspace lease, from its Init to its end: across
// steps and a pause the fixed target is uploaded once while the moved source is uploaded every device iteration, and
// no step leases a workspace again. The mock answers each device E-step with exact one-to-one correspondences
// (source i to target i, world units), so every iteration completes on the device path.
TEST_F(CpdBrokerWorkspaceReuse, AStepRunKeepsItsTargetResidentAcrossStepsAndPauses)
{
    constexpr std::size_t kSources = 48u, kTargets = 64u;
    std::mt19937 random(5u);
    std::normal_distribution<float> noise(0.0f, 0.5f);
    const auto sourcePoints = Cloud(kSources, 31);
    auto targetPoints = Cloud(kTargets, 37);
    for (std::size_t i = 0; i < kSources; ++i)
        targetPoints[i] =
            sourcePoints[i] + glm::vec3(0.2f, -0.1f, 0.05f) + glm::vec3(noise(random), noise(random), noise(random));
    Scene scene;
    const auto source = Make(scene.Registry, D::PointCloudPoint, sourcePoints);
    const auto target = Make(scene.Registry, D::PointCloudPoint, targetPoints);
    Jobs.Attach(scene.Context);
    scene.Context.SpatialIndices = &Cache;
    scene.Context.Device = &Device;
    // Readback rows: LogDenominator and Pt1 per target, then P1, PX, PY, PZ per source.
    Answer = [&](const std::size_t doubles)
    {
        std::vector<double> values(doubles, 0.0);
        if (values.size() >= 2u * kTargets + 4u * kSources)
            for (std::size_t i = 0; i < kSources; ++i)
            {
                values[kTargets + i] = 1.0;
                values[2u * kTargets + i] = 1.0;
                values[2u * kTargets + kSources + i] = targetPoints[i].x;
                values[2u * kTargets + 2u * kSources + i] = targetPoints[i].y;
                values[2u * kTargets + 3u * kSources + i] = targetPoints[i].z;
            }
        return values;
    };
    R::EditorCoherentPointDriftResult failure;
    const auto run = R::StartEditorCoherentPointDrift(scene.Commands(),
        {.SourceStableEntityId = Id(source),
            .TargetStableEntityId = Id(target),
            .Execution = Cap(5u),
            .InitialSigma2 = 10.0,
            .NormalizeInputs = false,
            .EStep = R::CoherentPointDriftEStep::Vulkan,
            .Threads = 1u},
        failure);
    ASSERT_TRUE(run) << failure.Message;
    const auto id = R::SnapshotEditorCoherentPointDrift(run).StepRun;
    const auto cleanup = [&]
    {
        (void)Jobs.Jobs().CancelAll();
        (void)Jobs.Jobs().DrainCompletions(Jobs.Events());
        (void)Jobs.Jobs().ShutdownGpuQueueParticipants(
            [&]
            {
                Device.WaitIdle();
            });
        Jobs.Jobs().CancelAndDrain();
    };
    const auto step = [&]
    {
        EXPECT_TRUE(Control(Jobs, id, R::StepRunAction::Step, 1u));
        return DrainUntil(Jobs,
            [&]
            {
                const auto observed = Jobs.StepRuns().Observe(id);
                return observed && observed->Phase == R::StepRunPhase::Paused && !observed->SegmentInFlight;
            });
    };
    const std::size_t targetBytes = kTargets * 32u, sourceBytes = kSources * 32u;
    if (!step())
    {
        cleanup();
        FAIL() << "the first step did not rest";
    }
    EXPECT_EQ(WritesOf(targetBytes), 1u);
    EXPECT_EQ(WritesOf(sourceBytes), 1u);
    EXPECT_EQ(R::SnapshotEditorCoherentPointDrift(run).Trace.size(), 1u);
    // A pause: frames pass and the cache prunes; the run keeps its lease.
    for (int frame = 0; frame < 5; ++frame)
    {
        (void)Jobs.Jobs().DrainCompletions(Jobs.Events());
        RetireFrames();
    }
    EXPECT_EQ(Jobs.StepRuns().Observe(id)->Steps, 1u);
    if (!step())
    {
        cleanup();
        FAIL() << "the second step did not rest";
    }
    EXPECT_EQ(WritesOf(targetBytes), 1u) << "the target stays uploaded across steps and the pause";
    EXPECT_EQ(WritesOf(sourceBytes), 2u) << "the moved source is uploaded every device iteration";
    EXPECT_EQ(Cache.Stats().WorkspaceReuses, 0u) << "no step leased a workspace again";
    EXPECT_EQ(Jobs.StepRuns().Observe(id)->Steps, 2u);
    EXPECT_EQ(Count(Taken), 2u) << "each device iteration pulled its statistics once";
    EXPECT_TRUE(Control(Jobs, id, R::StepRunAction::Discard));
    cleanup();
    EXPECT_EQ(Jobs.StepRuns().Observe(id)->Phase, R::StepRunPhase::Discarded);
}

TEST_F(CpdBrokerWorkspaceReuse, CancelledAndRefusedComputationsKeepTheirWorkspaceUntilDoneAndFailClosed)
{
    Case c(3, 2, 303);
    {
        // Cancelled while in flight: the worker returns at once (the CPU runs the iteration), the late page is given
        // up, and the workspace returns to the pool only once that page completed (the compute fence).
        R::CoherentPointDriftGpuEStep run(Cache, Device);
        ASSERT_TRUE(run.Prepare(3u, 2u));
        HoldNext();
        bool evaluated = true;
        std::thread worker(
            [&]
            {
                evaluated = run.Evaluate(c.Request());
            });
        ASSERT_TRUE(AwaitHeld(false));
        Device.TransferQueue.ComputeFenceToken = {Count(Tokens)};
        run.Close("cancelled");
        worker.join();
        EXPECT_FALSE(evaluated);
        EXPECT_EQ(Count(Released), 1u);
        run.ReleaseDeviceResources();
        RetireFrames();
        R::CoherentPointDriftGpuEStep during(Cache, Device);
        Case probe(3, 2, 304);
        ASSERT_TRUE(during.Prepare(3u, 2u));
        ASSERT_TRUE(during.Evaluate(probe.Request()));
        EXPECT_EQ(Cache.Stats().WorkspaceReuses, 0u) << "the in-flight workspace is not served to another run";
        during.ReleaseDeviceResources();
        Release(); // the late page completed
        EXPECT_TRUE(run.Closed());
        EXPECT_EQ(run.Diagnostic(), "cancelled");
        EXPECT_FALSE(run.Evaluate(c.Request()));
        RetireFrames();
        R::CoherentPointDriftGpuEStep after(Cache, Device);
        ASSERT_TRUE(after.Prepare(3u, 2u));
        ASSERT_TRUE(after.Evaluate(c.Request()));
        EXPECT_EQ(Cache.Stats().WorkspaceReuses, 1u);
        after.ReleaseDeviceResources();
    }
    // Refused on the device (no shader float64): the Init fails closed, later iterations do not wait.
    Device.ShaderFloat64 = false;
    R::CoherentPointDriftGpuEStep refused(Cache, Device);
    EXPECT_FALSE(refused.Prepare(3u, 2u));
    EXPECT_TRUE(refused.Closed());
    EXPECT_FALSE(refused.Diagnostic().empty());
    EXPECT_EQ(refused.Stats().Failed, 1u);
    EXPECT_FALSE(refused.Evaluate(c.Request()));
}

TEST_F(CpdBrokerWorkspaceReuse, SerialRequestsSettleTheirLatencyAndAClosedBrokerRefusesLaterOnes)
{
    Case c(3, 2, 401);
    R::CoherentPointDriftGpuEStep broker(Cache, Device);
    ASSERT_TRUE(broker.Prepare(3u, 2u));
    ASSERT_TRUE(broker.Evaluate(c.Request()));
    ASSERT_TRUE(broker.Evaluate(c.Request()));
    const double settledRequestSeconds = broker.Stats().WaitSeconds;
    EXPECT_GT(settledRequestSeconds, 0.0);
    broker.Close("cancelled");
    broker.Close("cancelled again");
    EXPECT_EQ(broker.Stats().WaitSeconds, settledRequestSeconds);
    EXPECT_FALSE(broker.Evaluate(c.Request()));
    EXPECT_EQ(broker.Stats().Requests, 2u);
    EXPECT_EQ(broker.Stats().Completed, 2u);
    EXPECT_EQ(broker.Diagnostic(), "cancelled again");
    broker.ReleaseDeviceResources();
}

TEST(CoherentPointDriftOperations, DiscardAnswersAtOnceAndTraceRowsCarryElapsedTime)
{
    // UI-067: Discard while an iteration is queued ends the run at once; the iteration, when it completes, publishes
    // nothing that overrides it. Trace rows carry elapsed times.
    {
        ShiftedPair pair(200u, 13u);
        const auto timed = pair.Start(pair.Config());
        ASSERT_TRUE(Control(pair.Jobs, R::SnapshotEditorCoherentPointDrift(timed).StepRun, R::StepRunAction::Step, 3u));
        ASSERT_TRUE(pair.Rest(timed));
        const auto trace = R::SnapshotEditorCoherentPointDrift(timed).Trace;
        ASSERT_EQ(trace.size(), 3u);
        for (std::size_t k = 0; k < trace.size(); ++k)
        {
            EXPECT_GE(trace[k].IterationSeconds, 0.0);
            if (k > 0)
                EXPECT_GE(trace[k].Seconds, trace[k - 1].Seconds);
        }
        EXPECT_TRUE(R::SnapshotEditorCoherentPointDrift(timed).Stage.empty()) << "no stage once paused";
    }
    ShiftedPair pair(200u, 13u);
    const auto run = pair.Start(pair.Config());
    const auto id = R::SnapshotEditorCoherentPointDrift(run).StepRun;
    ASSERT_TRUE(Control(pair.Jobs, id, R::StepRunAction::Step, 1u));
    EXPECT_EQ(pair.Observe(run)->Phase, R::StepRunPhase::Running);
    ASSERT_TRUE(Control(pair.Jobs, id, R::StepRunAction::Discard));
    EXPECT_EQ(pair.Observe(run)->Phase, R::StepRunPhase::Discarded) << "the panel sees the discard before the job ends";
    ASSERT_TRUE(pair.Jobs.DrainUntilTerminal());
    EXPECT_EQ(pair.Observe(run)->Phase, R::StepRunPhase::Discarded);
    EXPECT_FALSE(R::SnapshotEditorCoherentPointDrift(run).Result.Succeeded());
    EXPECT_FALSE(pair.S.History.CanUndo());
}

// ARCH-023: a run that ends without a commit answers at its settled end (StepRunCallbacks::Finished), once its holder
// released the source, with its final observation: a same-source run started from inside that answer is admitted.
// Prediction, answered from Discard (the holder still active): one delivery before Control returns, and the start
// inside it is refused as busy (no handle, a Pending refusal). Fixed: none before the drain, then exactly one,
// and the new run has its own step run, ready.
TEST(CoherentPointDriftOperations, ADiscardedRunAnswersOnceTheSourceIsFree)
{
    ShiftedPair pair(100u, 11u, false);
    auto config = pair.Config();
    config.Execution.IterationCap = 10000u;
    config.Tolerance = 0.0;
    unsigned delivered = 0u;
    std::optional<R::EditorCoherentPointDriftResult> ended;
    R::EditorCoherentPointDriftRunHandle next;
    R::EditorCoherentPointDriftResult refused;
    // The one-shot command (the form with a result callback) computes at once; it is discarded before any drain.
    const auto pending = R::ApplyEditorCoherentPointDriftCommand(pair.S.Commands(),
        config,
        [&](R::EditorCoherentPointDriftResult result)
        {
            ++delivered;
            ended = std::move(result);
            next = R::StartEditorCoherentPointDrift(pair.S.Commands(), pair.Config(), refused);
        });
    ASSERT_EQ(pending.Status, R::EditorCommandStatus::Pending) << pending.Message;
    ASSERT_NE(pending.StepRun, 0u);
    ASSERT_TRUE(Control(pair.Jobs, pending.StepRun, R::StepRunAction::Discard));
    EXPECT_EQ(delivered, 0u) << "no answer while the holder still holds the source";
    ASSERT_TRUE(DrainUntil(pair.Jobs,
        [&]
        {
            return delivered > 0u;
        }));
    ASSERT_TRUE(ended);
    EXPECT_EQ(ended->Status, R::EditorCommandStatus::StaleEntity);
    EXPECT_NE(ended->Message.find("cancelled"), std::string::npos) << ended->Message;
    EXPECT_EQ(ended->StepRun, pending.StepRun);
    ASSERT_TRUE(next) << "admitted, not refused as busy: " << refused.Message;
    const auto id = R::SnapshotEditorCoherentPointDrift(next).StepRun;
    EXPECT_NE(id, 0u);
    EXPECT_NE(id, pending.StepRun);
    EXPECT_EQ(pair.Jobs.StepRuns().Observe(id)->Phase, R::StepRunPhase::Ready);
    ASSERT_TRUE(Control(pair.Jobs, id, R::StepRunAction::Discard));
    ASSERT_TRUE(pair.Jobs.DrainUntilTerminal());
    EXPECT_EQ(delivered, 1u) << "one terminal result for the discarded run";
    EXPECT_FALSE(pair.S.History.CanUndo());
}

TEST(CoherentPointDriftOperations, BayesianSubsamplesReachTheSnapshotForThePreview)
{
    // UI-067: the chosen source and target samples are published once, as indices into the
    // preview and target, so the panel can draw them.
    ShiftedPair pair(300u, 17u);
    const auto run = pair.Start({.SourceStableEntityId = Id(pair.Source),
        .TargetStableEntityId = Id(pair.Target),
        .Method = M::Bayesian,
        .Output = O::DisplacementProperty,
        .Subsample = 60,
        .SubsampleTarget = 80});
    ASSERT_TRUE(run);
    EXPECT_FALSE(R::SnapshotEditorCoherentPointDrift(run).SourceSamples) << "nothing chosen before a step";
    ASSERT_TRUE(Control(pair.Jobs, R::SnapshotEditorCoherentPointDrift(run).StepRun, R::StepRunAction::Step, 1u));
    ASSERT_TRUE(pair.Rest(run));
    const auto snapshot = R::SnapshotEditorCoherentPointDrift(run);
    ASSERT_TRUE(snapshot.SourceSamples && snapshot.TargetSamples);
    EXPECT_EQ(snapshot.SourceSamples->size(), 60u);
    EXPECT_EQ(snapshot.TargetSamples->size(), 80u);
    for (const std::uint32_t i : *snapshot.SourceSamples) ASSERT_LT(i, snapshot.SourcePreview->size());
    for (const std::uint32_t j : *snapshot.TargetSamples) ASSERT_LT(j, snapshot.Target->size());
}
