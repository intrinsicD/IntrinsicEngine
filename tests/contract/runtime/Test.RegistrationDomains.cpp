#include <algorithm>
#include <array>
#include <cmath>
#include <glm/glm.hpp>
#include <gtest/gtest.h>
#include <limits>
#include <vector>
#include "SandboxEditorJobHarness.hpp"
#include "PointDomainFixture.hpp"
import Extrinsic.Runtime.RegistrationOperations;
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
}
TEST(RegistrationConfig, RoundTripCanonicalBindingsAndRejectMalformedControls)
{
    R::RegistrationConfig config;
    config.SourcePositions=Ref(D::GraphHalfedge); config.TargetPositions=Ref(D::MeshFace);
    config.TargetNormals={.Domain=D::MeshFace,.Name="face_normals",.ValueKind=Geometry::PropertyValueKind::Vec3};
    config.Backend=R::RegistrationBackend::VulkanLBVH;
    Extrinsic::Core::Config::EngineConfig engine;
    R::SetRegistrationConfig(engine,config); auto decoded=R::GetRegistrationConfig(engine);
    ASSERT_TRUE(decoded); EXPECT_EQ(decoded->SourcePositions,config.SourcePositions); EXPECT_EQ(decoded->TargetNormals,config.TargetNormals);
    EXPECT_EQ(decoded->Backend,config.Backend);
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
        if(deletion) scene.Raw().get<GS::Edges>(source).Properties.GetOrAdd<bool>("e:deleted")[0]=true;
        else
        {
            auto samples=scene.Raw().get<GS::Faces>(target).Properties.Get<glm::vec3>("samples");
            const auto same=samples[0];samples[0]=same; // unchanged values still have a newer property revision
        }
        ASSERT_TRUE(jobs.DrainUntilTerminal());
        ASSERT_EQ(jobs.Snapshot().Entries.size(),1);
        EXPECT_EQ(jobs.Snapshot().Entries[0].State,R::JobState::StaleDiscarded);
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
        const auto pending = R::ApplyEditorRegistrationCommand(commands, command,
            [&](const auto&) { ++completions; });
        ASSERT_EQ(pending.Status, R::EditorCommandStatus::Pending);
        borrowed[0].x += 0.5f;
        ASSERT_TRUE(jobs.DrainUntilTerminal());
        ASSERT_EQ(jobs.Snapshot().Entries.size(), 1u);
        EXPECT_EQ(jobs.Snapshot().Entries[0].State, R::JobState::StaleDiscarded);
        EXPECT_EQ(completions, 1u);
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
    ASSERT_EQ(R::ApplyEditorRegistrationCommand(commands, command).Status, R::EditorCommandStatus::Pending);
    // Deliberately bypass revision marking: compact values/count stay equal,
    // but the captured source-row mapping must still make publication stale.
    mask[0] = false;
    mask[1] = true;
    ASSERT_TRUE(jobs.DrainUntilTerminal());
    ASSERT_EQ(jobs.Snapshot().Entries.size(), 1u);
    EXPECT_EQ(jobs.Snapshot().Entries[0].State, R::JobState::StaleDiscarded);
    EXPECT_FALSE(history.CanUndo());
}
