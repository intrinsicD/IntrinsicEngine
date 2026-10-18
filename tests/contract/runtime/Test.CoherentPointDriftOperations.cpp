// RUNTIME-273: the Coherent Point Drift editor operation. Config validation, every variant
// on point-cloud and graph-node domains with its publication path and undo/redo, step
// mode (synchronous and on the job service), cancellation, and stale-input rejection.
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

TEST(CoherentPointDriftOperations, StepModeTracesEachIterationAndAppliesTheCurrentEstimate)
{
    Scene s;
    const auto points = Cloud(100, 11);
    std::vector<glm::vec3> moved;
    for (const auto& p : points) moved.push_back(p + glm::vec3(0.3f, 0.1f, 0.0f));
    const auto source = Make(s.Registry, D::PointCloudPoint, points);
    const auto target = Make(s.Registry, D::PointCloudPoint, moved);
    R::EditorCoherentPointDriftResult failure;
    const auto run = R::StartEditorCoherentPointDrift(s.Commands(), {.SourceStableEntityId = Id(source),
        .TargetStableEntityId = Id(target)}, failure);
    ASSERT_TRUE(run) << failure.Message;
    EXPECT_EQ(R::SnapshotEditorCoherentPointDrift(run).Phase, R::EditorCoherentPointDriftPhase::Ready);
    EXPECT_EQ(R::SnapshotEditorCoherentPointDrift(run).Target->size(), 100u);
    EXPECT_EQ(R::ApplyEditorCoherentPointDrift(s.Commands(), run).Status, R::EditorCommandStatus::InvalidProcessingParameters)
        << "nothing to apply before a step";

    EXPECT_EQ(R::StepEditorCoherentPointDrift(s.Commands(), run, 2), R::EditorCommandStatus::Pending);
    auto snapshot = R::SnapshotEditorCoherentPointDrift(run);
    EXPECT_EQ(snapshot.Phase, R::EditorCoherentPointDriftPhase::Paused);
    EXPECT_EQ(snapshot.Trace.size(), 2u);
    EXPECT_EQ(snapshot.Result.Iterations, 2u);
    EXPECT_LT(snapshot.Trace[1].Sigma2, snapshot.Trace[0].Sigma2);
    EXPECT_NE((*snapshot.SourcePreview)[0], points[0]) << "the preview moves before anything is published";
    EXPECT_EQ(s.Registry.Raw().get<T::Component>(source).Position, glm::vec3(0.0f));

    EXPECT_EQ(R::StepEditorCoherentPointDrift(s.Commands(), run, 0), R::EditorCommandStatus::Pending);
    snapshot = R::SnapshotEditorCoherentPointDrift(run);
    EXPECT_EQ(snapshot.Phase, R::EditorCoherentPointDriftPhase::Finished);
    EXPECT_NE(snapshot.Result.Termination, "none");
    EXPECT_EQ(R::StepEditorCoherentPointDrift(s.Commands(), run, 1), R::EditorCommandStatus::InvalidProcessingParameters)
        << "a finished run takes no more steps";
    const auto applied = R::ApplyEditorCoherentPointDrift(s.Commands(), run);
    ASSERT_TRUE(applied.Succeeded()) << applied.Message;
    EXPECT_NEAR(s.Registry.Raw().get<T::Component>(source).Position.x, 0.3f, 1e-3f);
    EXPECT_EQ(R::SnapshotEditorCoherentPointDrift(run).Phase, R::EditorCoherentPointDriftPhase::Applied);
}

TEST(CoherentPointDriftOperations, StaleInputsAndCancelledRunsPublishNothing)
{
    Scene s;
    const auto points = Cloud(80, 13);
    std::vector<glm::vec3> moved;
    for (const auto& p : points) moved.push_back(p + glm::vec3(0.2f, 0.0f, 0.0f));
    const auto source = Make(s.Registry, D::PointCloudPoint, points);
    const auto target = Make(s.Registry, D::PointCloudPoint, moved);
    const R::CoherentPointDriftConfig config{.SourceStableEntityId = Id(source), .TargetStableEntityId = Id(target)};
    R::EditorCoherentPointDriftResult failure;

    auto run = R::StartEditorCoherentPointDrift(s.Commands(), config, failure);
    ASSERT_TRUE(run);
    EXPECT_EQ(R::StepEditorCoherentPointDrift(s.Commands(), run, 0), R::EditorCommandStatus::Pending);
    s.Registry.Raw().get<T::Component>(target).Position = {0.0f, 5.0f, 0.0f};
    EXPECT_EQ(R::ApplyEditorCoherentPointDrift(s.Commands(), run).Status, R::EditorCommandStatus::StaleEntity);
    s.Registry.Raw().get<T::Component>(target).Position = {};

    run = R::StartEditorCoherentPointDrift(s.Commands(), config, failure);
    ASSERT_TRUE(run);
    EXPECT_EQ(R::StepEditorCoherentPointDrift(s.Commands(), run, 0), R::EditorCommandStatus::Pending);
    s.Registry.Raw().get<GS::Vertices>(source).Properties.GetOrAdd<glm::vec3>("v:position").Vector()[0] += glm::vec3(1.0f);
    EXPECT_EQ(R::ApplyEditorCoherentPointDrift(s.Commands(), run).Status, R::EditorCommandStatus::StaleEntity);

    run = R::StartEditorCoherentPointDrift(s.Commands(), config, failure);
    ASSERT_TRUE(run);
    R::CancelEditorCoherentPointDrift(run);
    EXPECT_EQ(R::SnapshotEditorCoherentPointDrift(run).Phase, R::EditorCoherentPointDriftPhase::Cancelled);
    EXPECT_EQ(R::StepEditorCoherentPointDrift(s.Commands(), run, 1), R::EditorCommandStatus::InvalidProcessingParameters);
    EXPECT_FALSE(s.History.CanUndo()) << "nothing was published";
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
    R::CoherentPointDriftConfig config{.SourceStableEntityId = Id(source), .TargetStableEntityId = Id(target),
                                       .Method = M::Nonrigid, .MaxIterations = 2u, .Output = O::DisplacementProperty};
    const auto automatic = R::ApplyEditorCoherentPointDriftCommand(s.Commands(), config);
    ASSERT_TRUE(automatic.Succeeded()) << automatic.Message;
    EXPECT_GT(automatic.KernelRank, 0u);
    EXPECT_LE(automatic.KernelRank, R::kCoherentPointDriftAutoLowRank);

    // Below the threshold the default stays exact.
    const auto small = Make(s.Registry, D::PointCloudPoint, Cloud(200, 43));
    config = {.SourceStableEntityId = Id(small), .TargetStableEntityId = Id(target), .Method = M::Nonrigid,
              .MaxIterations = 2u, .Output = O::DisplacementProperty};
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
        {.SourceStableEntityId = Id(source), .TargetStableEntityId = Id(target), .Method = M::Nonrigid,
         .MaxIterations = 1u, .Output = O::DisplacementProperty, .AutoLowRank = false});
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
    Scene s;
    const auto points = Cloud(150, 11);
    const auto source = Make(s.Registry, D::PointCloudPoint, points);
    const auto target = Make(s.Registry, D::PointCloudPoint, points);
    R::EditorCoherentPointDriftResult failure;
    const auto run = R::StartEditorCoherentPointDrift(s.Commands(), {.SourceStableEntityId = Id(source),
        .TargetStableEntityId = Id(target), .EStep = R::CoherentPointDriftEStep::Nystrom,
        .NystromLocalSigma = 10.0}, failure);
    ASSERT_TRUE(run) << failure.Message;
    (void)R::StepEditorCoherentPointDrift(s.Commands(), run, 1);
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
        R::CoherentPointDriftConfig config{.SourceStableEntityId = Id(source), .TargetStableEntityId = Id(target),
                                           .OutlierWeight = 0.0, .MaxIterations = 10u,
                                           .EStep = R::CoherentPointDriftEStep::Nystrom, .NystromLandmarks = 64};
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
    R::CoherentPointDriftConfig bayesian{.SourceStableEntityId = Id(source), .TargetStableEntityId = Id(target),
                                         .Method = M::Bayesian, .OutlierWeight = 0.1, .MaxIterations = 5u,
                                         .Output = O::DisplacementProperty, .DisplacementName = "bcpd", .Subsample = 300};
    bayesian.SubsampleSampling.Method = R::PointSamplingMethod::Random;
    bayesian.SubsampleSampling.Seed = 4u;
    const auto subsampled = R::ApplyEditorCoherentPointDriftCommand(s.Commands(), bayesian);
    ASSERT_TRUE(subsampled.Succeeded()) << subsampled.Message;
    R::CoherentPointDriftConfig lowRank{.SourceStableEntityId = Id(source), .TargetStableEntityId = Id(target),
                                        .Method = M::Nonrigid, .OutlierWeight = 0.0, .MaxIterations = 5u,
                                        .Output = O::DisplacementProperty, .DisplacementName = "warp", .LowRank = 30};
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
    R::CoherentPointDriftConfig config{.SourceStableEntityId = Id(source), .TargetStableEntityId = Id(target),
                                       .Method = M::Bayesian, .OutlierWeight = 0.1, .MaxIterations = 5,
                                       .Output = O::DisplacementProperty, .DisplacementName = "bcpd", .Subsample = 100};
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

namespace
{
    bool EvaluateBrokerBlocking(R::CoherentPointDriftGpuEStep& broker,
        const Geometry::CoherentPointDrift::EStep::ExternalRequest& request)
    {
        auto ready = broker.Queue(request);
        if (!ready) return false;
        ready->WaitForProgress(1u);
        return broker.Complete(ready, request);
    }
}

TEST(CoherentPointDriftOperations, VulkanEStepBrokerFailsClosedAndTimesOut)
{
    // Refusal and an elapsed deadline both close the broker. The owner observes the deadline
    // during Pump; Queue never blocks and there is no timer thread.
    R::SpatialIndexCache cache; // no device: no workspace lease, so the computation is refused
    const std::vector<double> x{0.0, 1.0}, zero(2, 0.0), weights(2, 0.0);
    std::vector<double> logDen(2), pt1(2), p1(2), px(2), py(2), pz(2);
    const Geometry::CoherentPointDrift::EStep::ExternalRequest request{
        .Target = {x, zero, zero}, .Moved = {x, zero, zero}, .TargetGeneration = 1u, .Sigma2 = 1.0,
        .LogOutlier = -std::numeric_limits<double>::infinity(), .LogWeights = weights,
        .LogDenominator = logDen, .Pt1 = pt1, .P1 = p1, .PXx = px, .PXy = py, .PXz = pz};
    {
        R::CoherentPointDriftGpuEStep broker(cache);
        std::atomic<bool> done{false};
        bool evaluated = true;
        std::thread worker([&] { evaluated = EvaluateBrokerBlocking(broker, request); done = true; });
        while (!done) { broker.Pump(); std::this_thread::yield(); }
        worker.join();
        EXPECT_FALSE(evaluated);
        EXPECT_TRUE(broker.Closed());
        EXPECT_FALSE(broker.Diagnostic().empty());
        EXPECT_EQ(broker.Stats().Failed, 1u);
        EXPECT_FALSE(EvaluateBrokerBlocking(broker, request)) << "a closed broker answers at once";
    }
    {
        R::CoherentPointDriftGpuEStep broker(cache, std::chrono::milliseconds{20});
        auto ready = broker.Queue(request);
        ASSERT_TRUE(ready);
        std::this_thread::sleep_for(std::chrono::milliseconds{25});
        EXPECT_FALSE(broker.Closed()) << "only the owner pump observes the deadline";
        broker.Pump();
        EXPECT_EQ(ready->PendingCount(), 0u);
        EXPECT_FALSE(broker.Complete(ready, request));
        EXPECT_TRUE(broker.Closed());
        EXPECT_NE(broker.Diagnostic().find("in time"), std::string::npos) << broker.Diagnostic();
    }
}

namespace
{
    // A broker on a cache bound to a mock framed device whose immediate submits record the real
    // workspace and hold the readback until the test delivers it (as the GPU would, later).
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
        std::optional<Extrinsic::RHI::ReadbackSink> Held{};
        std::uint64_t HeldBytes{0u}, Tokens{0u};
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
            Device.ShaderFloat64 = true;
            Device.ComputeReadback = [this](auto record, std::uint64_t bytes, Extrinsic::RHI::ReadbackSink sink) {
                if (!record(Device.CommandContext).IsValid()) return Extrinsic::RHI::ReadbackToken{};
                Held = std::move(sink);
                HeldBytes = bytes;
                return Extrinsic::RHI::ReadbackToken{++Tokens};
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
        // The GPU finishes: every double of the readback is its index.
        void Deliver()
        {
            std::vector<double> values(HeldBytes / sizeof(double));
            for (std::size_t k = 0; k < values.size(); ++k) values[k] = double(k);
            Held->Deliver(std::as_bytes(std::span<const double>(values)));
            Held.reset();
        }
        // Drains completed computations and retires every frame that may have used their buffers.
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
        // A worker evaluates while this (main) thread pumps and completes the device work.
        bool Evaluate(R::CoherentPointDriftGpuEStep& broker, Case& c)
        {
            std::atomic<bool> done{false};
            bool evaluated = false;
            std::thread worker([&] { evaluated = EvaluateBrokerBlocking(broker, c.Request()); done = true; });
            while (!done)
            {
                broker.Pump();
                if (Held) Deliver();
                std::this_thread::yield();
            }
            worker.join();
            return evaluated;
        }
        [[nodiscard]] std::size_t WritesOf(std::size_t bytes) const
        {
            return std::size_t(std::ranges::count_if(Device.BufferWrites, [&](const auto& w) { return w.Data.size() == bytes; }));
        }
    };

    class CpdSingleWorkerWait : public CpdBrokerWorkspaceReuse
    {
    protected:
        CpdSingleWorkerWait() : CpdBrokerWorkspaceReuse(1u) {}
    };
}

TEST_F(CpdSingleWorkerWait, PendingGpuEStepReleasesTheWorkerForOrdinaryJobs)
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
    const auto run = R::StartEditorCoherentPointDrift(scene.Commands(), {
        .SourceStableEntityId = Id(source), .TargetStableEntityId = Id(target),
        .MaxIterations = 1u, .InitialSigma2 = 10.0,
        .EStep = R::CoherentPointDriftEStep::Vulkan, .Threads = 1u}, failure);
    ASSERT_TRUE(run) << failure.Message;
    ASSERT_EQ(R::StepEditorCoherentPointDrift(scene.Commands(), run, 1u), R::EditorCommandStatus::Pending);
    const auto cleanup = [&] {
        R::CancelEditorCoherentPointDrift(run);
        (void)Jobs.Jobs().CancelAll();
        // Closing the auxiliary pump also unblocks the pre-coroutine baseline.
        (void)Jobs.Jobs().DrainCompletions(Jobs.Events());
        if (Held) Deliver();
        (void)Jobs.Jobs().ShutdownGpuQueueParticipants([&] { Device.WaitIdle(); });
        Jobs.Jobs().CancelAndDrain();
    };
    const auto requestDeadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
    while (!Held && std::chrono::steady_clock::now() < requestDeadline)
    {
        (void)Jobs.Jobs().DrainCompletions(Jobs.Events());
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    if (!Held)
    {
        cleanup();
        FAIL() << "The actual CPD operation never reached its held GPU readback.";
    }
    std::atomic<bool> ordinaryRan{false};
    const auto token = Jobs.Jobs().Submit(R::JobDesc{
        .DebugName = "CPD.worker-availability-control",
        .Work = [&](const R::JobCancellation&) {
            ordinaryRan.store(true, std::memory_order_release);
            return R::JobResultEnvelope::Make(true);
        },
        .PublishCompletion = [](R::KernelEventBus&, const R::JobResultEnvelope&) { return true; },
    });
    EXPECT_TRUE(token.IsValid());
    const auto ordinaryDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{500};
    while (!ordinaryRan.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < ordinaryDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    EXPECT_TRUE(ordinaryRan.load(std::memory_order_acquire))
        << "A pending E-step must suspend its coroutine, freeing the sole worker.";
    EXPECT_TRUE(Held.has_value()) << "The control must finish before the GPU result is delivered.";
    cleanup();
}

TEST_F(CpdSingleWorkerWait, EditorCancelWakesPendingEStepWithoutFallbackOrIteration)
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
    const auto run = R::StartEditorCoherentPointDrift(scene.Commands(), {
        .SourceStableEntityId = Id(source), .TargetStableEntityId = Id(target),
        .MaxIterations = 1u, .InitialSigma2 = 10.0,
        .EStep = R::CoherentPointDriftEStep::Vulkan, .Threads = 1u}, failure);
    ASSERT_TRUE(run) << failure.Message;
    ASSERT_EQ(R::StepEditorCoherentPointDrift(scene.Commands(), run, 1u), R::EditorCommandStatus::Pending);
    const auto cleanup = [&] {
        (void)Jobs.Jobs().CancelAll();
        (void)Jobs.Jobs().DrainCompletions(Jobs.Events());
        if (Held) Deliver();
        (void)Jobs.Jobs().ShutdownGpuQueueParticipants([&] { Device.WaitIdle(); });
        Jobs.Jobs().CancelAndDrain();
    };
    const auto requestDeadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
    while (!Held && std::chrono::steady_clock::now() < requestDeadline)
    {
        (void)Jobs.Jobs().DrainCompletions(Jobs.Events());
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    if (!Held)
    {
        ADD_FAILURE() << "The real CPD step did not queue its device request.";
        cleanup();
        return;
    }
    R::JobToken step;
    for (const auto& job : Jobs.Jobs().SnapshotAll())
        if (job.DebugName == "Sandbox.CoherentPointDrift") step = job.Token;
    EXPECT_TRUE(step.IsValid());
    R::CancelEditorCoherentPointDrift(run);
    const auto cancelDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{500};
    while (!Jobs.Jobs().IsComplete(step) && std::chrono::steady_clock::now() < cancelDeadline)
    {
        (void)Jobs.Jobs().DrainCompletions(Jobs.Events());
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    EXPECT_TRUE(Jobs.Jobs().IsComplete(step)) << "Editor cancel must wake the parked step by itself.";
    EXPECT_TRUE(Held.has_value()) << "The GPU must still be pending until all assertions finish.";
    const auto snapshot = R::SnapshotEditorCoherentPointDrift(run);
    EXPECT_EQ(snapshot.Phase, R::EditorCoherentPointDriftPhase::Cancelled);
    EXPECT_EQ(snapshot.Result.EStepFallbacks, 0u);
    EXPECT_TRUE(snapshot.Trace.empty());
    cleanup();
}

TEST_F(CpdBrokerWorkspaceReuse, StepsAndRunsReuseThePooledWorkspaceAndUploadEachTargetOnce)
{
    // n = 3 targets (96 bytes as float-float), m = 2 sources (64 bytes).
    constexpr std::size_t kTargetBytes = 3u * 32u, kSourceBytes = 2u * 32u;
    Case first(3, 2, 101);
    R::CoherentPointDriftGpuEStep run(Cache);
    ASSERT_TRUE(Evaluate(run, first)) << run.Diagnostic();
    // The readback lands in the solver's spans in the LogDenominator, Pt1, P1, PX order.
    EXPECT_EQ(first.LogDen, (std::vector<double>{0, 1, 2}));
    EXPECT_EQ(first.Pt1, (std::vector<double>{3, 4, 5}));
    EXPECT_EQ(first.P1, (std::vector<double>{6, 7}));
    EXPECT_EQ(first.PZ, (std::vector<double>{12, 13}));
    ASSERT_TRUE(Evaluate(run, first));
    EXPECT_EQ(Device.CreatePipelineCount, 2);
    const int buffers = Device.CreateBufferCount;
    EXPECT_EQ(WritesOf(kTargetBytes), 1u) << "the fixed target is uploaded once per step";
    EXPECT_EQ(WritesOf(kSourceBytes), 2u) << "the moved source every iteration";

    // The step's pump ends: the lease returns to the pool and the next step is served by it,
    // keeping pipelines, buffers and the uploaded target of this generation.
    run.ReleaseDeviceResources();
    RetireFrames();
    ASSERT_TRUE(Evaluate(run, first));
    EXPECT_EQ(Cache.Stats().WorkspaceReuses, 1u);
    EXPECT_EQ(Device.CreatePipelineCount, 2);
    EXPECT_EQ(Device.CreateBufferCount, buffers);
    EXPECT_EQ(WritesOf(kTargetBytes), 1u);

    // Growth replaces only undersized buffers (source, results); shrinking keeps them.
    Case wider(3, 4, 101);
    ASSERT_TRUE(Evaluate(run, wider));
    EXPECT_EQ(Device.CreateBufferCount, buffers + 2);
    ASSERT_TRUE(Evaluate(run, first));
    EXPECT_EQ(Device.CreateBufferCount, buffers + 2);
    EXPECT_EQ(first.P1, (std::vector<double>{6, 7})) << "the readback is the current shape's prefix";
    EXPECT_EQ(WritesOf(kTargetBytes), 1u);
    run.ReleaseDeviceResources();
    RetireFrames();

    // Another run (a new target generation of the same size) on the same pooled workspace
    // uploads its own target; generation 0 is never taken as current.
    Case other(3, 2, 202, 10.0);
    R::CoherentPointDriftGpuEStep next(Cache);
    ASSERT_TRUE(Evaluate(next, other)) << next.Diagnostic();
    EXPECT_EQ(Cache.Stats().WorkspaceReuses, 2u);
    EXPECT_EQ(Device.CreatePipelineCount, 2);
    ASSERT_EQ(WritesOf(kTargetBytes), 2u) << "a pooled workspace never keeps another run's target";
    const auto& upload = *std::ranges::find_if(Device.BufferWrites | std::views::reverse,
                                               [&](const auto& w) { return w.Data.size() == kTargetBytes; });
    float x0{};
    std::memcpy(&x0, upload.Data.data(), sizeof(float));
    EXPECT_EQ(x0, 10.0f);
    Case unversioned(3, 2, 0);
    ASSERT_TRUE(Evaluate(next, unversioned));
    ASSERT_TRUE(Evaluate(next, unversioned));
    EXPECT_EQ(WritesOf(kTargetBytes), 4u);
    EXPECT_EQ(next.Stats().Completed, 3u);
    EXPECT_EQ(next.Stats().Failed, 0u);
}

TEST_F(CpdBrokerWorkspaceReuse, CancelledAndRefusedComputationsKeepTheirLeaseUntilDoneAndFailClosed)
{
    Case c(3, 2, 303);
    {
        // Cancelled while in flight: the worker returns at once (the CPU runs the iteration),
        // the late result is dropped, and the workspace only returns once the work completed.
        R::CoherentPointDriftGpuEStep run(Cache);
        std::atomic<bool> done{false};
        bool evaluated = true;
        std::thread worker([&] { evaluated = EvaluateBrokerBlocking(run, c.Request()); done = true; });
        while (!Held) { run.Pump(); std::this_thread::yield(); }
        run.Close("cancelled");
        worker.join();
        EXPECT_FALSE(evaluated);
        auto late = std::move(*Held);
        const auto lateBytes = HeldBytes;
        Held.reset();
        run.ReleaseDeviceResources();
        RetireFrames();
        R::CoherentPointDriftGpuEStep during(Cache);
        Case probe(3, 2, 304);
        ASSERT_TRUE(Evaluate(during, probe));
        EXPECT_EQ(Cache.Stats().WorkspaceReuses, 0u) << "the in-flight lease is not served to another run";
        during.ReleaseDeviceResources();
        Held = std::move(late);
        HeldBytes = lateBytes;
        Deliver();
        run.Pump();
        EXPECT_EQ(run.Stats().Completed, 1u);
        EXPECT_TRUE(run.Closed());
        EXPECT_EQ(run.Diagnostic(), "cancelled");
        EXPECT_FALSE(EvaluateBrokerBlocking(run, c.Request()));
        RetireFrames();
        R::CoherentPointDriftGpuEStep after(Cache);
        ASSERT_TRUE(Evaluate(after, c));
        EXPECT_EQ(Cache.Stats().WorkspaceReuses, 1u);
        after.ReleaseDeviceResources();
    }
    // Refused on the device (no shader float64): fail closed, later iterations do not wait.
    Device.ShaderFloat64 = false;
    R::CoherentPointDriftGpuEStep refused(Cache);
    EXPECT_FALSE(Evaluate(refused, c));
    EXPECT_TRUE(refused.Closed());
    EXPECT_FALSE(refused.Diagnostic().empty());
    EXPECT_EQ(refused.Stats().Failed, 1u);
    EXPECT_FALSE(EvaluateBrokerBlocking(refused, c.Request()));
}

TEST_F(CpdBrokerWorkspaceReuse, RequestEventsKeepTheirIdentityAndSettledLatency)
{
    Case c(3, 2, 401);
    R::CoherentPointDriftGpuEStep broker(Cache);
    const auto first = broker.Queue(c.Request());
    ASSERT_TRUE(first);
    broker.Pump();
    ASSERT_TRUE(Held);
    Deliver();
    broker.Pump();
    ASSERT_TRUE(first->IsReady());
    ASSERT_TRUE(broker.Complete(first, c.Request()));

    const auto second = broker.Queue(c.Request());
    ASSERT_TRUE(second);
    EXPECT_NE(first, second);
    EXPECT_FALSE(second->IsReady());
    broker.Pump();
    ASSERT_TRUE(Held);
    Deliver();
    broker.Pump();
    ASSERT_TRUE(second->IsReady());
    EXPECT_FALSE(broker.Complete(first, c.Request()));
    const double settledRequestSeconds = broker.Stats().WaitSeconds;
    EXPECT_GT(settledRequestSeconds, 0.0);
    broker.Close("cancelled");
    broker.Close("cancelled again");
    EXPECT_EQ(broker.Stats().WaitSeconds, settledRequestSeconds);
    EXPECT_EQ(first->PendingCount(), 0u);
    EXPECT_EQ(second->PendingCount(), 0u);
    EXPECT_FALSE(broker.Complete(second, c.Request()));
    EXPECT_FALSE(broker.Queue(c.Request()));
    EXPECT_EQ(broker.Stats().Requests, 2u);
    EXPECT_EQ(broker.Stats().Completed, 2u);
    broker.ReleaseDeviceResources();
}

TEST(CoherentPointDriftOperations, CancelAnswersAtOnceAndTraceRowsCarryElapsedTime)
{
    // UI-067: Cancel while a step job is queued turns the phase to Cancelled immediately; the
    // job, when it runs, publishes nothing that overrides it. Trace rows carry elapsed times.
    Scene s;
    const auto points = Cloud(200, 13);
    std::vector<glm::vec3> moved;
    for (const auto& p : points) moved.push_back(p + glm::vec3(0.2f, 0.0f, 0.1f));
    const auto source = Make(s.Registry, D::PointCloudPoint, points);
    const auto target = Make(s.Registry, D::PointCloudPoint, moved);
    const R::CoherentPointDriftConfig config{.SourceStableEntityId = Id(source), .TargetStableEntityId = Id(target)};

    R::EditorCoherentPointDriftResult failure;
    const auto timed = R::StartEditorCoherentPointDrift(s.Commands(), config, failure);
    ASSERT_TRUE(timed) << failure.Message;
    ASSERT_EQ(R::StepEditorCoherentPointDrift(s.Commands(), timed, 3), R::EditorCommandStatus::Pending);
    const auto trace = R::SnapshotEditorCoherentPointDrift(timed).Trace;
    ASSERT_EQ(trace.size(), 3u);
    for (std::size_t k = 0; k < trace.size(); ++k)
    {
        EXPECT_GE(trace[k].IterationSeconds, 0.0);
        if (k > 0) EXPECT_GE(trace[k].Seconds, trace[k - 1].Seconds);
    }
    EXPECT_TRUE(R::SnapshotEditorCoherentPointDrift(timed).Stage.empty()) << "no stage once paused";

    Extrinsic::Tests::EditorJobHarness jobs;
    jobs.Attach(s.Context);
    const auto run = R::StartEditorCoherentPointDrift(s.Commands(), config, failure);
    ASSERT_TRUE(run) << failure.Message;
    ASSERT_EQ(R::StepEditorCoherentPointDrift(s.Commands(), run, 1), R::EditorCommandStatus::Pending);
    EXPECT_EQ(R::SnapshotEditorCoherentPointDrift(run).Phase, R::EditorCoherentPointDriftPhase::Running);
    R::CancelEditorCoherentPointDrift(run);
    EXPECT_EQ(R::SnapshotEditorCoherentPointDrift(run).Phase, R::EditorCoherentPointDriftPhase::Cancelled)
        << "the panel sees the cancel before the job ends";
    ASSERT_TRUE(jobs.DrainUntilTerminal());
    const auto after = R::SnapshotEditorCoherentPointDrift(run);
    EXPECT_EQ(after.Phase, R::EditorCoherentPointDriftPhase::Cancelled);
    EXPECT_FALSE(R::ApplyEditorCoherentPointDrift(s.Commands(), run).Succeeded());
}

TEST(CoherentPointDriftOperations, BayesianSubsamplesReachTheSnapshotForThePreview)
{
    // UI-067: the chosen source and target samples are published once, as indices into the
    // preview and target, so the panel can draw them.
    Scene s;
    const auto points = Cloud(300, 17);
    std::vector<glm::vec3> moved;
    for (const auto& p : points) moved.push_back(p + glm::vec3(0.05f, 0.0f, 0.0f));
    const auto source = Make(s.Registry, D::PointCloudPoint, points);
    const auto target = Make(s.Registry, D::PointCloudPoint, moved);
    R::EditorCoherentPointDriftResult failure;
    const auto run = R::StartEditorCoherentPointDrift(s.Commands(), {.SourceStableEntityId = Id(source),
        .TargetStableEntityId = Id(target), .Method = M::Bayesian, .Output = O::DisplacementProperty,
        .Subsample = 60, .SubsampleTarget = 80}, failure);
    ASSERT_TRUE(run) << failure.Message;
    EXPECT_FALSE(R::SnapshotEditorCoherentPointDrift(run).SourceSamples) << "nothing chosen before a step";
    ASSERT_EQ(R::StepEditorCoherentPointDrift(s.Commands(), run, 1), R::EditorCommandStatus::Pending);
    const auto snapshot = R::SnapshotEditorCoherentPointDrift(run);
    ASSERT_TRUE(snapshot.SourceSamples && snapshot.TargetSamples);
    EXPECT_EQ(snapshot.SourceSamples->size(), 60u);
    EXPECT_EQ(snapshot.TargetSamples->size(), 80u);
    for (const std::uint32_t i : *snapshot.SourceSamples) ASSERT_LT(i, snapshot.SourcePreview->size());
    for (const std::uint32_t j : *snapshot.TargetSamples) ASSERT_LT(j, snapshot.Target->size());
}
