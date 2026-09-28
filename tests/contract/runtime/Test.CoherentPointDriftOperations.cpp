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
                                       .NystromErrorLimit = 1e-4, .LowRank = 40};
    const auto decoded = R::DecodeCoherentPointDriftConfig(R::SerializeCoherentPointDriftConfig(config));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->Method, M::Nonrigid);
    EXPECT_EQ(decoded->DisplacementName, "warp");
    EXPECT_EQ(decoded->Beta, 1.5);
    EXPECT_EQ(decoded->EStep, R::CoherentPointDriftEStep::Truncated);
    EXPECT_EQ(decoded->EStepTolerance, 1e-4);
    EXPECT_EQ(decoded->Threads, 3u);
    EXPECT_EQ(decoded->LowRank, 40u);
    EXPECT_EQ(decoded->NystromLandmarks, 128u);
    EXPECT_EQ(decoded->NystromErrorLimit, 1e-4);
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
    const auto immediate = R::ApplyEditorCoherentPointDriftCommand(s.Commands(), config);
    ASSERT_TRUE(immediate.Succeeded()) << immediate.Message;
    EXPECT_EQ(immediate.Backend, "cpu_auto");
    EXPECT_GT(immediate.EStepFallbacks, 0u);
    EXPECT_NE(immediate.GpuDiagnostic.find("job lane"), std::string::npos) << immediate.GpuDiagnostic;
    EXPECT_NEAR(s.Registry.Raw().get<T::Component>(source).Position.x, 0.15f, 1e-3f);

    Extrinsic::Tests::EditorJobHarness jobs;
    jobs.Attach(s.Context);
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
                                         .Method = M::Bayesian, .OutlierWeight = 0.1, .Output = O::DisplacementProperty,
                                         .DisplacementName = "bcpd", .Subsample = 300};
    bayesian.SubsampleSampling.Method = R::PointSamplingMethod::Random;
    bayesian.SubsampleSampling.Seed = 4u;
    const auto subsampled = R::ApplyEditorCoherentPointDriftCommand(s.Commands(), bayesian);
    ASSERT_TRUE(subsampled.Succeeded()) << subsampled.Message;
    R::CoherentPointDriftConfig lowRank{.SourceStableEntityId = Id(source), .TargetStableEntityId = Id(target),
                                        .Method = M::Nonrigid, .OutlierWeight = 0.0, .Output = O::DisplacementProperty,
                                        .DisplacementName = "warp", .LowRank = 30};
    lowRank.LandmarkSampling.Method = R::PointSamplingMethod::Random;
    const auto result = R::ApplyEditorCoherentPointDriftCommand(s.Commands(), lowRank);
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_GT(result.KernelRank, 0u);
}
