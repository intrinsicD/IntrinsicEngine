#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <optional>
#include <thread>
#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <gtest/gtest.h>
#include <limits>
#include <string>
#include <vector>
#include "SandboxEditorJobHarness.hpp"
#include "PointDomainFixture.hpp"
import Extrinsic.Runtime.RegistrationOperations;
import Extrinsic.Runtime.StepRuns;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.WorldRegistry;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Extrinsic.ECS.Component.Transform;
import Geometry.HalfedgeMesh;
import Geometry.Graph;
namespace R = Extrinsic::Runtime;
namespace GS = Extrinsic::ECS::Components::GeometrySources;
namespace T = Extrinsic::ECS::Components::Transform;
using D = R::GeometryElementDomain;
namespace
{
    constexpr std::array<glm::vec3, 4> points{{{0,0,0},{2,0,0},{0,3,0},{0,0,4}}};
    auto Make(Extrinsic::ECS::Scene::Registry& scene, D domain, glm::vec3 offset)
    {
        auto entity = scene.Create();
        scene.Raw().emplace<T::Component>(entity);
        if (domain >= D::MeshVertex && domain <= D::MeshFace)
        {
            Geometry::HalfedgeMesh::Mesh mesh;
            auto a=mesh.AddVertex(points[0]), b=mesh.AddVertex(points[1]), c=mesh.AddVertex(points[2]), d=mesh.AddVertex(points[3]);
            (void)mesh.AddTriangle(a,b,c); (void)mesh.AddTriangle(b,a,d);
            (void)mesh.AddTriangle(c,b,d); (void)mesh.AddTriangle(a,c,d);
            GS::PopulateFromMesh(scene.Raw(), entity, mesh);
        }
        else if (domain != D::PointCloudPoint)
        {
            Geometry::Graph::Graph graph;
            auto a=graph.AddVertex(points[0]), b=graph.AddVertex(points[1]), c=graph.AddVertex(points[2]), d=graph.AddVertex(points[3]);
            (void)graph.AddEdge(a,b); (void)graph.AddEdge(b,c); (void)graph.AddEdge(c,d); (void)graph.AddEdge(d,a);
            GS::PopulateFromGraph(scene.Raw(), entity, graph);
        }
        else scene.Raw().emplace<GS::Vertices>(entity).Properties.Resize(5);
        auto* properties = &Intrinsic::Tests::PointDomainProperties(scene, entity, domain);
        auto samples=properties->GetOrAdd<glm::vec3>("samples");
        for (std::size_t i=0; i<samples.Size(); ++i) samples[i]=points[i%4]+offset;
        auto keep=properties->GetOrAdd<float>("keep");
        for(std::size_t i=0;i<keep.Size();++i) keep[i]=42.f;
        if(domain==D::PointCloudPoint)
        {
            properties->GetOrAdd<bool>("v:deleted")[4]=true;
            samples[4]={std::numeric_limits<float>::quiet_NaN(),0,0};
        }
        return entity;
    }
    R::GeometryPropertyRef Ref(D domain)
    { return {.Domain=domain,.Name="samples",.ValueKind=Geometry::PropertyValueKind::Vec3}; }

    // ICP's default execution options (start at once, accept at the end) with another cap or start mode.
    R::StepRunExecution Execution(const std::uint32_t cap,
        const R::StepRunStart start = R::StepRunStart::Run,
        const R::StepRunAutoAccept accept = R::StepRunAutoAccept::End)
    {
        auto execution = R::kDefaultRegistrationExecution;
        execution.IterationCap = cap;
        execution.Start = start;
        execution.AutoAccept = accept;
        return execution;
    }

    // Drains until `done` holds (a paused run's holder never ends, so DrainUntilTerminal cannot).
    template <class Done> bool DrainUntil(Extrinsic::Tests::EditorJobHarness& jobs, Done done)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
        while (std::chrono::steady_clock::now() < deadline)
        {
            (void)jobs.Jobs().DrainCompletions(jobs.Events());
            if (done())
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        return false;
    }

    // A point cloud of 200 deterministic samples ("samples"), mapped by `pose`.
    entt::entity MakeCloud(Extrinsic::ECS::Scene::Registry& scene, const glm::mat4& pose)
    {
        const auto entity = scene.Create();
        scene.Raw().emplace<T::Component>(entity);
        auto& props = scene.Raw().emplace<GS::Vertices>(entity).Properties;
        props.Resize(200);
        auto samples = props.GetOrAdd<glm::vec3>("samples");
        for (std::size_t i = 0; i < 200; ++i)
        {
            const float t = float(i);
            samples[i] =
                glm::vec3(pose * glm::vec4(std::sin(1.3f * t), std::cos(0.7f * t), 0.5f * std::sin(2.1f * t), 1.f));
        }
        return entity;
    }

    // A queued ICP run whose target is the source turned by 0.4 rad about z and moved, so the solve takes several
    // iterations: source and target, the job lane with the session's step runs, and the run's controls.
    struct IcpRunFixture
    {
        Extrinsic::ECS::Scene::Registry Scene;
        entt::entity Source{}, Target{};
        R::EditorCommandHistory History;
        R::EditorProcessingContext Context{.Scene = &Scene, .CommandHistory = &History};
        Extrinsic::Tests::EditorJobHarness Jobs;
        std::optional<R::EditorRegistrationResult> Delivered{};
        unsigned Deliveries{0u};
        std::uint64_t Id{0u};
        IcpRunFixture()
        {
            const float c = std::cos(0.4f), s = std::sin(0.4f);
            glm::mat4 pose(1.f);
            pose[0] = {c, s, 0, 0};
            pose[1] = {-s, c, 0, 0};
            pose[3] = {0.2f, -0.1f, 0.05f, 1};
            Source = MakeCloud(Scene, glm::mat4(1.f));
            Target = MakeCloud(Scene, pose);
            Jobs.Attach(Context);
        }
        R::EditorRegistrationResult Start(const R::StepRunExecution& execution,
            R::EditorRegistrationProgressHandle progress = {},
            const double convergenceThreshold = 1e-6)
        {
            auto result = R::ApplyEditorRegistrationCommand(
                R::BindEditorProcessingCommands(Context),
                {.SourceStableEntityId = R::SelectionController::ToStableEntityId(Source),
                    .TargetStableEntityId = R::SelectionController::ToStableEntityId(Target),
                    .Execution = execution,
                    .InlierRatio = 1.,
                    .TrajectoryStep = 1000u,
                    .SourcePositions = Ref(D::PointCloudPoint),
                    .TargetPositions = Ref(D::PointCloudPoint),
                    .ConvergenceThreshold = convergenceThreshold},
                [this](R::EditorRegistrationResult r)
                {
                    ++Deliveries;
                    Delivered = std::move(r);
                },
                std::move(progress));
            Id = result.StepRun;
            return result;
        }
        [[nodiscard]] R::StepRunObservation Observe()
        {
            return *Jobs.StepRuns().Observe(Id);
        }
        bool Control(const R::StepRunAction action, const std::uint32_t count = 1u)
        {
            std::string refusal;
            const bool accepted = Jobs.StepRuns().Control(Id, action, count, refusal);
            EXPECT_TRUE(accepted) << refusal;
            return accepted;
        }
        bool Rest()
        {
            return DrainUntil(Jobs,
                [&]
                {
                    const auto o = Observe();
                    return (o.Phase == R::StepRunPhase::Paused || R::IsTerminal(o.Phase)) && !o.SegmentInFlight;
                });
        }
        bool Terminal()
        {
            return DrainUntil(Jobs,
                [&]
                {
                    return R::IsTerminal(Observe().Phase);
                });
        }
        [[nodiscard]] glm::vec3 SourcePosition()
        {
            return Scene.Raw().get<T::Component>(Source).Position;
        }
    };
}
TEST(RegistrationDomains, EveryCanonicalPointDomainAcceptsCrossDomainBindingsAndPreservesProperties)
{
    for (unsigned sourceDomain=1; sourceDomain<=8; ++sourceDomain)
    for (unsigned targetDomain=1; targetDomain<=8; ++targetDomain)
    {
        SCOPED_TRACE(std::to_string(sourceDomain)+" -> "+std::to_string(targetDomain));
        R::WorldRegistry worlds;
        auto world=worlds.CreateWorld("icp"); auto& scene=*worlds.Get(world);
        R::SpatialIndexCache cache(worlds);
        auto source=Make(scene,D(sourceDomain),{}), target=Make(scene,D(targetDomain),{7,-2,3});
        R::EditorCommandHistory history;
        R::EditorProcessingContext context{.Scene=&scene,.World=world,.CommandHistory=&history,.SpatialIndices=&cache};
        const auto commands=R::BindEditorProcessingCommands(context);
        auto before=R::GetEditorRegistrationInputCatalog(commands,R::SelectionController::ToStableEntityId(source));
        auto result=R::ApplyEditorRegistrationCommand(commands,{
            .SourceStableEntityId=R::SelectionController::ToStableEntityId(source),
            .TargetStableEntityId=R::SelectionController::ToStableEntityId(target),
            .InlierRatio=1.,.TrajectoryStep=50,.SourcePositions=Ref(D(sourceDomain)),
            .TargetPositions=Ref(D(targetDomain)),.Backend=R::RegistrationBackend::CpuLBVH});
        ASSERT_TRUE(result.Succeeded())<<result.Message;
        EXPECT_EQ(result.ActualBackend,R::RegistrationBackend::CpuLBVH);
        auto& transform=scene.Raw().get<T::Component>(source);
        EXPECT_NEAR(transform.Position.x,7,1e-4); EXPECT_NEAR(transform.Position.y,-2,1e-4); EXPECT_NEAR(transform.Position.z,3,1e-4);
        EXPECT_TRUE(history.CanUndo());
        ASSERT_TRUE(history.Undo().Succeeded());
        EXPECT_EQ(transform.Position,glm::vec3(0));
        ASSERT_TRUE(history.Redo().Succeeded());
        EXPECT_NEAR(transform.Position.x,7,1e-4);
        const auto* props=R::ResolveGeometryPropertySet(R::BuildGeometryAvailability(scene.Raw(),source),D(sourceDomain));
        EXPECT_EQ(props->Get<float>("keep")[0],42.f);
        EXPECT_EQ(props->Get<glm::vec3>("samples")[0],points[0]);
        EXPECT_EQ(before.Entries.size(),R::GetEditorRegistrationInputCatalog(commands,R::SelectionController::ToStableEntityId(source)).Entries.size());
        EXPECT_FALSE(result.TargetIndexReused);
    }
}
TEST(RegistrationDomains, CacheUsesEntityMetricAndRebuildsAfterTargetTransformOrPropertyChanges)
{
    R::WorldRegistry worlds; auto world=worlds.CreateWorld("icp"); auto& scene=*worlds.Get(world);
    R::SpatialIndexCache cache(worlds);
    auto source=Make(scene,D::PointCloudPoint,{}), target=Make(scene,D::PointCloudPoint,{});
    scene.Raw().get<T::Component>(target).Scale={2,3,4};
    const auto ref=Ref(D::PointCloudPoint);
    auto acquired=cache.Acquire(world,target,ref,R::SpatialIndexSpace::EntityTransform);
    auto snapshot=cache.Snapshot(acquired.Handle); ASSERT_TRUE(snapshot);
    EXPECT_EQ(snapshot->Index.Points()[1],glm::vec3(4,0,0));
    EXPECT_TRUE(cache.Acquire(world,target,ref,R::SpatialIndexSpace::EntityTransform).Reused);
    scene.Raw().get<T::Component>(target).Position={10,0,0};
    EXPECT_FALSE(cache.Snapshot(acquired.Handle));
    EXPECT_FALSE(cache.Acquire(world,target,ref,R::SpatialIndexSpace::EntityTransform).Reused);
    EXPECT_EQ(snapshot->Index.Points()[1],glm::vec3(4,0,0)); // retained CPU lease
    R::EditorProcessingContext context{.Scene=&scene,.World=world,.SpatialIndices=&cache};
    const auto commands=R::BindEditorProcessingCommands(context);
    auto result=R::ApplyEditorRegistrationCommand(commands,{
        .SourceStableEntityId=R::SelectionController::ToStableEntityId(source),
        .TargetStableEntityId=R::SelectionController::ToStableEntityId(target),
        .SourcePositions=ref,.TargetPositions=ref,.Backend=R::RegistrationBackend::VulkanLBVH});
    ASSERT_TRUE(result.Succeeded())<<result.Message;
    EXPECT_TRUE(result.FellBackToCPU); EXPECT_EQ(result.ActualBackend,R::RegistrationBackend::CpuLBVH);
    EXPECT_TRUE(result.TargetIndexReused); EXPECT_FALSE(result.BackendDiagnostic.empty());
    // The panel learns the same reason before a run starts.
    EXPECT_NE(R::RegistrationVulkanUnavailableReason(commands).find("job lane"),std::string::npos);
    EXPECT_NE(result.BackendDiagnostic.find("job lane"),std::string::npos)<<result.BackendDiagnostic;
    // The brute-force strategy takes the same Vulkan admission and reports its own request.
    const auto brute = R::ApplyEditorRegistrationCommand(commands,
        {.SourceStableEntityId = R::SelectionController::ToStableEntityId(source),
            .TargetStableEntityId = R::SelectionController::ToStableEntityId(target),
            .SourcePositions = ref,
            .TargetPositions = ref,
            .Backend = R::RegistrationBackend::VulkanBruteForce});
    ASSERT_TRUE(brute.Succeeded()) << brute.Message;
    EXPECT_EQ(brute.RequestedBackend, R::RegistrationBackend::VulkanBruteForce);
    EXPECT_TRUE(brute.FellBackToCPU);
    EXPECT_EQ(brute.ActualBackend, R::RegistrationBackend::CpuLBVH);
    EXPECT_NE(brute.BackendDiagnostic.find("job lane"), std::string::npos) << brute.BackendDiagnostic;
}
// Brute-force ICP work beyond the shared engineering budget (4096 sources x 4096 targets x 2 > 2^24) is admitted
// to no GPU path: it falls back to the CPU LBVH and says why, before any device check.
TEST(RegistrationDomains, BruteForceBeyondTheWorkBudgetFallsBackToCpuWithItsReason)
{
    R::WorldRegistry worlds;
    auto world = worlds.CreateWorld("icp");
    auto& scene = *worlds.Get(world);
    R::SpatialIndexCache cache(worlds);
    const auto cloud = [&](glm::vec3 offset)
    {
        auto entity = scene.Create();
        scene.Raw().emplace<T::Component>(entity);
        auto& props = scene.Raw().emplace<GS::Vertices>(entity).Properties;
        props.Resize(4096);
        auto samples = props.GetOrAdd<glm::vec3>("samples");
        for (std::size_t i = 0; i < 4096; ++i)
            samples[i] = glm::vec3(float(i % 64), float(i / 64), float(i % 7)) + offset;
        return entity;
    };
    const auto source = cloud({.25f, 0, 0}), target = cloud({});
    R::EditorProcessingContext context{.Scene = &scene, .World = world, .SpatialIndices = &cache};
    const auto result = R::ApplyEditorRegistrationCommand(R::BindEditorProcessingCommands(context),
        {.SourceStableEntityId = R::SelectionController::ToStableEntityId(source),
            .TargetStableEntityId = R::SelectionController::ToStableEntityId(target),
            .Execution = Execution(1u),
            .SourcePositions = Ref(D::PointCloudPoint),
            .TargetPositions = Ref(D::PointCloudPoint),
            .Backend = R::RegistrationBackend::VulkanBruteForce});
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_EQ(result.RequestedBackend, R::RegistrationBackend::VulkanBruteForce);
    EXPECT_TRUE(result.FellBackToCPU);
    EXPECT_EQ(result.ActualBackend, R::RegistrationBackend::CpuLBVH);
    EXPECT_NE(result.BackendDiagnostic.find("2^24"), std::string::npos) << result.BackendDiagnostic;
}
TEST(RegistrationConfig, RoundTripCanonicalBindingsAndRejectMalformedControls)
{
    R::RegistrationConfig config;
    config.SourcePositions=Ref(D::GraphHalfedge); config.TargetPositions=Ref(D::MeshFace);
    config.TargetNormals={.Domain=D::MeshFace,.Name="face_normals",.ValueKind=Geometry::PropertyValueKind::Vec3};
    for (const auto backend : {R::RegistrationBackend::VulkanLBVH, R::RegistrationBackend::VulkanBruteForce})
    {
        config.Backend = backend;
        Extrinsic::Core::Config::EngineConfig engine;
        R::SetRegistrationConfig(engine, config);
        auto decoded = R::GetRegistrationConfig(engine);
        ASSERT_TRUE(decoded);
        EXPECT_EQ(decoded->SourcePositions, config.SourcePositions);
        EXPECT_EQ(decoded->TargetNormals, config.TargetNormals);
        EXPECT_EQ(decoded->Backend, config.Backend) << R::ToString(backend);
    }
    for(auto payload:{R"({"backend":"automatic"})",R"({"max_iterations":0})",R"({"max_iterations":-1})",
                     R"({"inlier_ratio":2})",R"({"convergence_threshold":-1})",R"({"unknown":1})",
                     R"({"source_positions":{"domain":"MeshFace","name":"x","kind":"float"}})"})
        EXPECT_FALSE(R::ValidateRegistrationConfigSection(payload,{},"test").Usable())<<payload;
}

TEST(RegistrationDomains, QueuedMixedDomainBindingRevisionAndDeletionChangesDiscardResults)
{
    for (bool deletion : {false,true})
    {
        Extrinsic::ECS::Scene::Registry scene;
        auto source=Make(scene,D::GraphHalfedge,{}), target=Make(scene,D::MeshFace,{2,3,4});
        R::EditorProcessingContext context;
        R::EditorCommandHistory history;context.Scene=&scene;context.CommandHistory=&history;
        Extrinsic::Tests::EditorJobHarness jobs;jobs.Attach(context);
        const auto commands=R::BindEditorProcessingCommands(context);
        auto result=R::ApplyEditorRegistrationCommand(commands,{
            .SourceStableEntityId=R::SelectionController::ToStableEntityId(source),
            .TargetStableEntityId=R::SelectionController::ToStableEntityId(target),
            .TrajectoryStep=50,.SourcePositions=Ref(D::GraphHalfedge),.TargetPositions=Ref(D::MeshFace)});
        ASSERT_EQ(result.Status,R::EditorCommandStatus::Pending);
        ASSERT_NE(result.StepRun, 0u);
        if(deletion) scene.Raw().get<GS::Edges>(source).Properties.GetOrAdd<bool>("e:deleted")[0]=true;
        else
        {
            auto samples=scene.Raw().get<GS::Faces>(target).Properties.Get<glm::vec3>("samples");
            const auto same=samples[0];samples[0]=same; // unchanged values still have a newer property revision
        }
        ASSERT_TRUE(jobs.DrainUntilTerminal());
        // The run's Valid sees the newer revision before it computes: discarded as stale, nothing published.
        const auto run = jobs.StepRuns().Observe(result.StepRun);
        ASSERT_TRUE(run);
        EXPECT_EQ(run->Phase, R::StepRunPhase::Discarded);
        EXPECT_NE(run->Diagnostic.find("stale"), std::string::npos) << run->Diagnostic;
        EXPECT_FALSE(history.CanUndo());
    }
}
TEST(RegistrationConfig, SharedPreviewApplyAndConfiguredRunUseCanonicalOperands)
{
    namespace Config=Extrinsic::Core::Config;
    Extrinsic::ECS::Scene::Registry scene;
    auto source=Make(scene,D::GraphEdge,{}), target=Make(scene,D::MeshFace,{2,3,4});
    R::EditorProcessingContext context{.Scene=&scene};
    Config::EngineConfigSectionRegistry registry;
    ASSERT_TRUE(registry.Register(R::MakeRegistrationConfigSectionRegistration()));
    R::RuntimeEngineConfigControlState state;
    Config::PopulateEngineConfigSectionDefaults(state.ActiveConfig,registry);
    context.EngineConfigControlState=&state;context.EngineConfigCommandsAvailable=true;
    unsigned previews=0,applies=0;
    context.PreviewEngineConfigDocument=[&](const std::string& document,const std::string& origin){
        ++previews;return Config::PreviewEngineConfig(document,state.ActiveConfig,{origin,&registry});};
    context.ApplyEngineConfigHotSubset=[&](const Config::EngineConfigLoadResult& preview){
        ++applies;state.ActiveConfig=preview.Preview.Config;
        return R::RuntimeEngineConfigApplyResult{.Status=R::RuntimeEngineConfigApplyStatus::Applied};};
    auto commands=R::BindEditorProcessingCommands(context);
    R::RegistrationConfig config{
        .SourceStableEntityId=R::SelectionController::ToStableEntityId(source),
        .TargetStableEntityId=R::SelectionController::ToStableEntityId(target),
        .InlierRatio=1.,.TrajectoryStep=50,.SourcePositions=Ref(D::GraphEdge),.TargetPositions=Ref(D::MeshFace)};
    const auto ready = R::PreviewEditorRegistrationCommand(commands,config);
    ASSERT_TRUE(ready.Enabled) << ready.DisabledReason;
    EXPECT_TRUE(ready.DisabledReason.empty());
    EXPECT_TRUE(R::ResolveEditorProcessingActionReadiness(commands,ready).Enabled);
    EXPECT_EQ(previews,0);EXPECT_EQ(applies,0);
    EXPECT_EQ(scene.Raw().get<T::Component>(source).Position,glm::vec3(0));
    auto invalid = config;
    invalid.InlierRatio = -1;
    const auto blocked = R::PreviewEditorRegistrationCommand(commands,invalid);
    EXPECT_FALSE(blocked.Enabled);
    EXPECT_FALSE(blocked.DisabledReason.empty());
    EXPECT_EQ(R::ApplyEditorRegistrationCommand(commands,invalid).Message,blocked.DisabledReason);
    EXPECT_EQ(R::ResolveEditorProcessingActionReadiness(commands,blocked).DisabledReason,
              blocked.DisabledReason);
    EXPECT_EQ(previews,0);EXPECT_EQ(applies,0);
    EXPECT_EQ(scene.Raw().get<T::Component>(source).Position,glm::vec3(0));
    ASSERT_TRUE(R::ApplyEditorRegistrationConfig(commands,config).Succeeded());
    EXPECT_EQ(previews,1);EXPECT_EQ(applies,1);
    ASSERT_TRUE(R::ApplyEditorConfiguredRegistrationCommand(commands).Succeeded());
    EXPECT_NEAR(scene.Raw().get<T::Component>(source).Position.x,2,1e-4);
    config.InlierRatio=-1;
    EXPECT_FALSE(R::ApplyEditorRegistrationConfig(commands,config).Succeeded());
    EXPECT_EQ(applies,1);
}

TEST(RegistrationDomains, RigidPublicationPreservesSignedNonuniformAndZeroScale)
{
    for (const auto scale : {glm::vec3(-2.f,3.f,.5f), glm::vec3(2.f,3.f,.5f), glm::vec3(0.f,3.f,.5f)})
    {
        Extrinsic::ECS::Scene::Registry scene;
        const auto source = Make(scene,D::PointCloudPoint,{});
        const auto target = Make(scene,D::PointCloudPoint,{});
        scene.Raw().get<T::Component>(source).Scale = scale;
        auto& targetPose = scene.Raw().get<T::Component>(target);
        targetPose.Scale = scale;
        targetPose.Position = {4.f,-2.f,1.f};
        R::EditorCommandHistory history;
        R::EditorProcessingContext context{.Scene=&scene,.CommandHistory=&history};
        const auto commands=R::BindEditorProcessingCommands(context);
        const auto result = R::ApplyEditorRegistrationCommand(commands,{
            .SourceStableEntityId=R::SelectionController::ToStableEntityId(source),
            .TargetStableEntityId=R::SelectionController::ToStableEntityId(target),
            .InlierRatio=1.,.TrajectoryStep=50,
            .SourcePositions=Ref(D::PointCloudPoint),.TargetPositions=Ref(D::PointCloudPoint)});
        ASSERT_TRUE(result.Succeeded()) << result.Message;
        const auto& pose = scene.Raw().get<T::Component>(source);
        EXPECT_EQ(pose.Scale,scale);
        const auto actual = T::GetMatrix(pose), expected = T::GetMatrix(targetPose);
        for(int column=0;column<4;++column)for(int row=0;row<4;++row)
            EXPECT_NEAR(actual[column][row],expected[column][row],1e-4);
        ASSERT_TRUE(history.Undo().Succeeded());
        EXPECT_EQ(scene.Raw().get<T::Component>(source).Scale,scale);
        ASSERT_TRUE(history.Redo().Succeeded());
        EXPECT_EQ(scene.Raw().get<T::Component>(source).Scale,scale);
    }
}

TEST(RegistrationDomains, HalfedgeNormalsIgnoreBothRowsOfDeletedEdges)
{
    Extrinsic::ECS::Scene::Registry scene;
    const auto source = Make(scene, D::GraphHalfedge, {});
    const auto target = Make(scene, D::MeshHalfedge, {});
    auto& props = scene.Raw().get<GS::Halfedges>(target).Properties;
    auto normals = props.GetOrAdd<glm::vec3>("directions", {0,0,1});
    normals[0] = {NAN,0,0};
    normals[1] = {0,0,0};
    scene.Raw().get<GS::Edges>(target).Properties.GetOrAdd<bool>("e:deleted")[0] = true;
    R::EditorProcessingContext context{.Scene = &scene};
    const auto commands = R::BindEditorProcessingCommands(context);
    const R::EditorRegistrationCommand command{
        .SourceStableEntityId = R::SelectionController::ToStableEntityId(source),
        .TargetStableEntityId = R::SelectionController::ToStableEntityId(target),
        .Variant = R::EditorICPVariant::PointToPlane,
        .SourcePositions = Ref(D::GraphHalfedge), .TargetPositions = Ref(D::MeshHalfedge),
        .TargetNormals = {D::MeshHalfedge, "directions", Geometry::PropertyValueKind::Vec3}};
    const auto ready = R::PreviewEditorRegistrationCommand(commands, command);
    EXPECT_TRUE(ready.Enabled) << ready.DisabledReason;
    const auto applied = R::ApplyEditorRegistrationCommand(commands, command);
    EXPECT_EQ(applied.TargetPointCount, props.Size() - 2);
    EXPECT_EQ(applied.TargetNormalCount, props.Size() - 2);
    scene.Raw().get<GS::Edges>(target).Properties.Get<bool>("e:deleted")[0] = false;
    const auto invalid = R::PreviewEditorRegistrationCommand(commands, command);
    EXPECT_FALSE(invalid.Enabled);
    EXPECT_EQ(R::ApplyEditorRegistrationCommand(commands, command).Message, invalid.DisabledReason);
}

TEST(RegistrationDomains, CanonicalMetadataRejectsMalformedDeletionAndPositionTypes)
{
    for (unsigned defect = 0; defect < 4; ++defect)
    {
        SCOPED_TRACE(defect);
        Extrinsic::ECS::Scene::Registry scene;
        const auto source = Make(scene, D::GraphHalfedge, {});
        const auto target = Make(scene, D::MeshFace, {});
        auto& edges = scene.Raw().get<GS::Edges>(source).Properties;
        auto& halfedges = scene.Raw().get<GS::Halfedges>(source).Properties;
        R::EditorRegistrationCommand command{
            .SourceStableEntityId = R::SelectionController::ToStableEntityId(source),
            .TargetStableEntityId = R::SelectionController::ToStableEntityId(target),
            .SourcePositions = Ref(D::GraphHalfedge), .TargetPositions = Ref(D::MeshFace)};
        if (defect == 0)
        {
            auto deleted = edges.Get<bool>("e:deleted");
            if (deleted) edges.Remove(deleted);
            (void)edges.GetOrAdd<float>("e:deleted");
        }
        else if (defect == 1) edges.Get<bool>("e:deleted").Vector().pop_back();
        else if (defect == 2) halfedges.Resize(halfedges.Size() - 1);
        else command.SourcePositions = {D::GraphHalfedge, "keep", Geometry::PropertyValueKind::Float};
        R::EditorProcessingContext context{.Scene = &scene};
        const auto commands = R::BindEditorProcessingCommands(context);
        const auto invalid = R::PreviewEditorRegistrationCommand(commands, command);
        EXPECT_FALSE(invalid.Enabled);
        EXPECT_EQ(R::ApplyEditorRegistrationCommand(commands, command).Message, invalid.DisabledReason);
        const auto catalog = R::GetEditorRegistrationInputCatalog(commands, command.SourceStableEntityId);
        EXPECT_TRUE(std::ranges::none_of(catalog.Entries, [&](const auto& entry) {
            return entry.Ref == command.SourcePositions;
        }));
    }
}

TEST(RegistrationDomains, QueuedSnapshotsDetectUnversionedValueEditsInEveryOperand)
{
    for (unsigned operand = 0; operand < 3; ++operand)
    {
        SCOPED_TRACE(operand);
        Extrinsic::ECS::Scene::Registry scene;
        const auto source = Make(scene, D::PointCloudPoint, {});
        const auto target = Make(scene, D::PointCloudPoint, {});
        auto& targetProps = scene.Raw().get<GS::Vertices>(target).Properties;
        (void)targetProps.GetOrAdd<glm::vec3>("directions", {0,0,1});
        auto& changed = operand == 0 ? scene.Raw().get<GS::Vertices>(source).Properties : targetProps;
        auto& borrowed = changed.Get<glm::vec3>(operand == 2 ? "directions" : "samples").Vector();
        R::EditorCommandHistory history;
        R::EditorProcessingContext context{.Scene = &scene, .CommandHistory = &history};
        Extrinsic::Tests::EditorJobHarness jobs;
        jobs.Attach(context);
        const auto commands = R::BindEditorProcessingCommands(context);
        const R::EditorRegistrationCommand command{
            .SourceStableEntityId = R::SelectionController::ToStableEntityId(source),
            .TargetStableEntityId = R::SelectionController::ToStableEntityId(target),
            .Variant = R::EditorICPVariant::PointToPlane,
            .SourcePositions = Ref(D::PointCloudPoint), .TargetPositions = Ref(D::PointCloudPoint),
            .TargetNormals = {D::PointCloudPoint, "directions", Geometry::PropertyValueKind::Vec3}};
        unsigned completions{};
        std::optional<R::EditorRegistrationResult> delivered;
        const auto pending = R::ApplyEditorRegistrationCommand(commands,
            command,
            [&](const R::EditorRegistrationResult& result)
            {
                ++completions;
                delivered = result;
            });
        ASSERT_EQ(pending.Status, R::EditorCommandStatus::Pending);
        borrowed[0].x += 0.5f;
        ASSERT_TRUE(jobs.DrainUntilTerminal());
        // An unversioned edit passes the per-frame revision check; Accept's full comparison refuses it.
        EXPECT_EQ(jobs.StepRuns().Observe(pending.StepRun)->Phase, R::StepRunPhase::Failed);
        EXPECT_EQ(completions, 1u);
        ASSERT_TRUE(delivered);
        EXPECT_EQ(delivered->Status, R::EditorCommandStatus::StaleEntity) << delivered->Message;
        EXPECT_NE(delivered->Message.find("did not apply"), std::string::npos) << delivered->Message;
        EXPECT_FALSE(history.CanUndo());
    }
}

TEST(RegistrationDomains, QueuedEqualValueMaskRemappingStillDiscardsResults)
{
    Extrinsic::ECS::Scene::Registry scene;
    const auto source = Make(scene, D::PointCloudPoint, {});
    const auto target = Make(scene, D::PointCloudPoint, {});
    auto& props = scene.Raw().get<GS::Vertices>(source).Properties;
    auto samples = props.Get<glm::vec3>("samples");
    samples[1] = samples[0];
    samples[4] = points[1];
    auto& mask = props.Get<bool>("v:deleted").Vector();
    mask[0] = true;
    mask[4] = false;
    R::EditorCommandHistory history;
    R::EditorProcessingContext context{.Scene = &scene, .CommandHistory = &history};
    Extrinsic::Tests::EditorJobHarness jobs;
    jobs.Attach(context);
    const auto commands = R::BindEditorProcessingCommands(context);
    const R::EditorRegistrationCommand command{
        .SourceStableEntityId = R::SelectionController::ToStableEntityId(source),
        .TargetStableEntityId = R::SelectionController::ToStableEntityId(target),
        .SourcePositions = Ref(D::PointCloudPoint), .TargetPositions = Ref(D::PointCloudPoint)};
    const auto pending = R::ApplyEditorRegistrationCommand(commands, command);
    ASSERT_EQ(pending.Status, R::EditorCommandStatus::Pending);
    // Deliberately bypass revision marking: compact values/count stay equal,
    // but the captured source-row mapping must still make publication stale.
    mask[0] = false;
    mask[1] = true;
    ASSERT_TRUE(jobs.DrainUntilTerminal());
    EXPECT_EQ(jobs.StepRuns().Observe(pending.StepRun)->Phase, R::StepRunPhase::Failed) << "Accept refused it as stale";
    EXPECT_FALSE(history.CanUndo());
}
TEST(RegistrationDomains, ProgressShowsThePoseAndTheMetricLogHasOneRowPerIteration)
{
    // UI-067: the caller's view gets the run's id, the pre-aligned source and shown poses; the trace is the step run's
    // metric log (rmse, inliers, seconds, the step's seconds), one row per iteration. A discarded run publishes nothing.
    IcpRunFixture f;
    const auto progress = R::MakeEditorRegistrationProgress();
    const auto pending = f.Start(Execution(50u), progress);
    ASSERT_EQ(pending.Status, R::EditorCommandStatus::Pending) << pending.Message;
    ASSERT_TRUE(f.Jobs.DrainUntilTerminal());
    ASSERT_TRUE(f.Delivered && f.Delivered->Succeeded()) << (f.Delivered ? f.Delivered->Message : "none");
    const auto live = R::SnapshotEditorRegistrationProgress(progress);
    EXPECT_EQ(live.StepRun, pending.StepRun);
    ASSERT_TRUE(live.SourceWorld);
    EXPECT_GT(live.Revision, 1u) << "a pose was shown";
    const auto records = f.Observe().Metrics->Records();
    EXPECT_EQ(records.size(), f.Delivered->IterationsPerformed);
    for (std::size_t k = 1; k < records.size(); ++k)
        EXPECT_GE(records[k].Values[2], records[k - 1].Values[2]);
    EXPECT_EQ(records.back().Values[0], f.Delivered->FinalRMSE);
    EXPECT_EQ(f.Deliveries, 1u);

    IcpRunFixture discarded;
    const auto ready = discarded.Start(Execution(50u, R::StepRunStart::Ready, R::StepRunAutoAccept::Never));
    ASSERT_TRUE(discarded.Control(R::StepRunAction::Discard));
    ASSERT_TRUE(discarded.Jobs.DrainUntilTerminal());
    EXPECT_EQ(discarded.SourcePosition(), glm::vec3(0)) << "a discarded run publishes nothing";
    ASSERT_TRUE(discarded.Delivered);
    EXPECT_NE(discarded.Delivered->Message.find("ICP was cancelled"), std::string::npos)
        << discarded.Delivered->Message;
    EXPECT_TRUE(discarded.Jobs.StepRuns().Observe(ready.StepRun)->Metrics->Records().empty());
    EXPECT_FALSE(discarded.History.CanUndo());
}

// ARCH-023: a run that ends without a commit answers at its settled end (StepRunCallbacks::Finished), when its holder
// released the source: a same-source run started from inside that answer is admitted. Answered from Discard (the
// holder still active), that start is refused as busy and names no run.
TEST(RegistrationDomains, ADiscardedRunAnswersOnceTheSourceIsFree)
{
    IcpRunFixture f;
    const R::EditorRegistrationCommand command{
        .SourceStableEntityId = R::SelectionController::ToStableEntityId(f.Source),
        .TargetStableEntityId = R::SelectionController::ToStableEntityId(f.Target),
        .Execution = Execution(50u, R::StepRunStart::Ready, R::StepRunAutoAccept::Never),
        .InlierRatio = 1.,
        .TrajectoryStep = 1000u,
        .SourcePositions = Ref(D::PointCloudPoint),
        .TargetPositions = Ref(D::PointCloudPoint)};
    auto batch = command; // the next run computes and accepts at once, so the drain below ends
    batch.Execution = Execution(1u);
    std::optional<R::EditorRegistrationResult> ended, next;
    const auto first = R::ApplyEditorRegistrationCommand(R::BindEditorProcessingCommands(f.Context),
        command,
        [&](R::EditorRegistrationResult r)
        {
            ended = std::move(r);
            next = R::ApplyEditorRegistrationCommand(R::BindEditorProcessingCommands(f.Context), batch, {});
        });
    ASSERT_EQ(first.Status, R::EditorCommandStatus::Pending) << first.Message;
    f.Id = first.StepRun;
    ASSERT_TRUE(f.Control(R::StepRunAction::Discard));
    ASSERT_TRUE(f.Jobs.DrainUntilTerminal());
    ASSERT_TRUE(ended && next);
    EXPECT_NE(ended->Message.find("ICP was cancelled"), std::string::npos) << ended->Message;
    EXPECT_EQ(next->Status, R::EditorCommandStatus::Pending) << next->Message;
    EXPECT_NE(next->StepRun, 0u) << "admitted, not refused as busy: " << next->Message;
    EXPECT_TRUE(f.History.CanUndo()) << "the admitted run applied its pose";
}

// ARCH-023: a ready run computes nothing until asked; Step n takes exactly n ICP iterations; a paused run stays at its
// boundary; Stop keeps the partial pose, which Accept publishes as one undo entry; Run equals the same steps one at a
// time; Restart begins a new generation that publishes nothing of the older one.
TEST(RegistrationDomains, StepRunsStepExactlyAndAcceptOncePublishesTheChosenPose)
{
    IcpRunFixture f;
    auto execution = Execution(50u, R::StepRunStart::Ready, R::StepRunAutoAccept::Never);
    execution.PreviewBuffers = 2u;
    const auto ready = f.Start(execution);
    ASSERT_EQ(ready.Status, R::EditorCommandStatus::Pending) << ready.Message;
    for (int i = 0; i < 20; ++i)
        (void)f.Jobs.Jobs().DrainCompletions(f.Jobs.Events());
    EXPECT_EQ(f.Observe().Phase, R::StepRunPhase::Ready);
    EXPECT_EQ(f.Observe().Steps, 0u);
    EXPECT_EQ(f.Observe().Execution.PreviewBuffers, 2u);
    EXPECT_EQ(f.Observe().DevicePreviewBuffers, 0u) << "ICP previews a CPU pose";
    EXPECT_EQ(f.Jobs.Snapshot().Entries.size(), 1u) << "only the run's holder: no step was submitted";
    ASSERT_TRUE(f.Control(R::StepRunAction::Step, 2u));
    ASSERT_TRUE(f.Rest());
    EXPECT_EQ(f.Observe().Phase, R::StepRunPhase::Paused);
    EXPECT_EQ(f.Observe().Steps, 2u) << "Step 2 is two ICP iterations";
    for (int i = 0; i < 20; ++i)
        (void)f.Jobs.Jobs().DrainCompletions(f.Jobs.Events());
    EXPECT_EQ(f.Observe().Steps, 2u) << "a paused run does not advance";
    EXPECT_EQ(f.SourcePosition(), glm::vec3(0)) << "nothing before Accept";
    ASSERT_TRUE(f.Control(R::StepRunAction::Stop));
    ASSERT_TRUE(f.Rest());
    EXPECT_EQ(f.Observe().End, R::StepRunEnd::Stopped);
    ASSERT_TRUE(f.Control(R::StepRunAction::Accept));
    ASSERT_TRUE(f.Terminal());
    EXPECT_EQ(f.Observe().Phase, R::StepRunPhase::Committed);
    ASSERT_TRUE(f.Delivered && f.Delivered->Succeeded()) << (f.Delivered ? f.Delivered->Message : "none");
    EXPECT_EQ(f.Delivered->IterationsPerformed, 2u);
    EXPECT_EQ(f.Delivered->AppliedStep, 2u) << "the trajectory step clamps to the iterations done";
    const glm::vec3 partial = f.SourcePosition();
    EXPECT_NE(partial, glm::vec3(0));
    ASSERT_TRUE(f.History.Undo().Succeeded());
    EXPECT_EQ(f.SourcePosition(), glm::vec3(0));
    EXPECT_FALSE(f.History.CanUndo()) << "Accept is one undo entry";

    // Run to the end versus one step at a time: the same published pose.
    const auto accepted = [](const bool stepwise, const bool restart)
    {
        IcpRunFixture run;
        (void)run.Start(Execution(50u, R::StepRunStart::Ready, R::StepRunAutoAccept::Never));
        if (restart)
        {
            EXPECT_TRUE(run.Control(R::StepRunAction::Step, 3u));
            EXPECT_TRUE(run.Rest());
            EXPECT_TRUE(run.Control(R::StepRunAction::Restart));
            EXPECT_TRUE(DrainUntil(run.Jobs,
                [&]
                {
                    return run.Observe().Generation == 1u;
                }));
            EXPECT_EQ(run.Observe().Steps, 0u);
        }
        if (stepwise)
            while (run.Observe().End == R::StepRunEnd::None)
            {
                EXPECT_TRUE(run.Control(R::StepRunAction::Step, 1u));
                EXPECT_TRUE(run.Rest());
            }
        else
        {
            EXPECT_TRUE(run.Control(R::StepRunAction::Run));
            EXPECT_TRUE(run.Rest());
        }
        EXPECT_TRUE(run.Control(R::StepRunAction::Accept));
        EXPECT_TRUE(run.Terminal());
        EXPECT_EQ(run.Deliveries, 1u);
        return std::pair{run.SourcePosition(), run.Delivered ? run.Delivered->IterationsPerformed : 0u};
    };
    const auto [runPose, runIterations] = accepted(false, false);
    EXPECT_GT(runIterations, 2u);
    EXPECT_EQ(accepted(true, false), (std::pair{runPose, runIterations}));
    EXPECT_EQ(accepted(false, true), (std::pair{runPose, runIterations}))
        << "a restarted run publishes its own generation";
}

// A shutdown with a paused run and with a computing one (a CPU segment between steps) drains both: one terminal
// result each, no publication.
TEST(RegistrationDomains, PausedAndComputingRunsDrainAtShutdownWithoutPublication)
{
    for (const bool computing : {false, true})
    {
        SCOPED_TRACE(computing ? "computing" : "paused");
        IcpRunFixture f;
        // Never converges (threshold 0): only the cap or the shutdown ends it.
        const auto pending = f.Start(Execution(100000u), {}, 0.0);
        ASSERT_EQ(pending.Status, R::EditorCommandStatus::Pending) << pending.Message;
        ASSERT_TRUE(DrainUntil(f.Jobs,
            [&]
            {
                return f.Observe().Steps >= 2u;
            }));
        if (!computing)
        {
            ASSERT_TRUE(f.Control(R::StepRunAction::Pause));
            ASSERT_TRUE(f.Rest());
            EXPECT_EQ(f.Observe().Phase, R::StepRunPhase::Paused);
        }
        f.Jobs.Jobs().CancelAndDrain();
        EXPECT_TRUE(R::IsTerminal(f.Observe().Phase));
        EXPECT_EQ(f.Deliveries, 1u) << "one terminal result";
        ASSERT_TRUE(f.Delivered);
        EXPECT_FALSE(f.Delivered->Succeeded());
        EXPECT_FALSE(f.History.CanUndo());
        EXPECT_EQ(f.SourcePosition(), glm::vec3(0));
    }
}
