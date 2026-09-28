// RUNTIME-273: the Coherent Point Drift editor operation. Config validation, every variant
// on point-cloud and graph-node domains with its publication path and undo/redo, step
// mode (synchronous and on the job service), cancellation, and stale-input rejection.
#include <cmath>
#include <random>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <entt/entity/registry.hpp>
#include <gtest/gtest.h>
#include "SandboxEditorJobHarness.hpp"
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
import Geometry.Graph;
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
                                       .DisplacementName = "warp"};
    const auto decoded = R::DecodeCoherentPointDriftConfig(R::SerializeCoherentPointDriftConfig(config));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->Method, M::Nonrigid);
    EXPECT_EQ(decoded->DisplacementName, "warp");
    EXPECT_EQ(decoded->Beta, 1.5);
    const auto invalid = [&](R::CoherentPointDriftConfig c) {
        return !registration.Validate(R::SerializeCoherentPointDriftConfig(c), {}, "test").Usable();
    };
    EXPECT_TRUE(invalid({.Method = M::Affine, .Output = O::SourceTransform}));
    EXPECT_TRUE(invalid({.Method = M::Nonrigid, .Output = O::SourceTransform}));
    EXPECT_TRUE(invalid({.Output = O::DisplacementProperty, .DisplacementName = "v:position"}));
    EXPECT_TRUE(invalid({.SourceStableEntityId = 5, .TargetStableEntityId = 5}));
    EXPECT_TRUE(invalid({.OutlierWeight = 1.0}));
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
            EXPECT_EQ(result.Backend, "cpu_reference");
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
    EXPECT_EQ(R::SnapshotEditorCoherentPointDrift(run).Target.size(), 100u);
    EXPECT_EQ(R::ApplyEditorCoherentPointDrift(s.Commands(), run).Status, R::EditorCommandStatus::InvalidProcessingParameters)
        << "nothing to apply before a step";

    EXPECT_EQ(R::StepEditorCoherentPointDrift(s.Commands(), run, 2), R::EditorCommandStatus::Pending);
    auto snapshot = R::SnapshotEditorCoherentPointDrift(run);
    EXPECT_EQ(snapshot.Phase, R::EditorCoherentPointDriftPhase::Paused);
    EXPECT_EQ(snapshot.Trace.size(), 2u);
    EXPECT_EQ(snapshot.Result.Iterations, 2u);
    EXPECT_LT(snapshot.Trace[1].Sigma2, snapshot.Trace[0].Sigma2);
    EXPECT_NE(snapshot.SourcePreview[0], points[0]) << "the preview moves before anything is published";
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
    const auto nonrigid = R::PreviewEditorCoherentPointDriftCommand(s.Commands(), {.SourceStableEntityId = Id(large),
        .TargetStableEntityId = Id(target), .Method = M::Nonrigid, .Output = O::Positions});
    EXPECT_FALSE(nonrigid.Enabled);
    EXPECT_NE(nonrigid.DisabledReason.find("8192"), std::string::npos) << nonrigid.DisabledReason;
    EXPECT_TRUE(R::PreviewEditorCoherentPointDriftCommand(s.Commands(), {.SourceStableEntityId = Id(target),
        .TargetStableEntityId = Id(large)}).Enabled);
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
