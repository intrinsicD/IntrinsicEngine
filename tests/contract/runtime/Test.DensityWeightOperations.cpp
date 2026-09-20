#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>
#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>
#include <variant>
#include <gtest/gtest.h>
#include "EditorFeatureTestContext.hpp"
#include "SandboxEditorJobHarness.hpp"

import Extrinsic.Runtime.PointAnalysisOperations;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.WorldRegistry;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.VisualizationEditingOperations;
import Extrinsic.Runtime.VisualizationRecipes;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Geometry.HalfedgeMesh;
import Geometry.Graph;

namespace R = Extrinsic::Runtime;
namespace GS = Extrinsic::ECS::Components::GeometrySources;
using D = R::GeometryElementDomain;
namespace
{
    constexpr std::array<glm::vec3, 4> plane{{{0, 0, 0}, {2, 0, 0}, {0, 3, 0}, {2, 3, 0}}};
    Geometry::PropertySet &Properties(Extrinsic::ECS::Scene::Registry &scene, entt::entity entity, D domain)
    {
        return *const_cast<Geometry::PropertySet *>(
            R::ResolveGeometryPropertySet(R::BuildGeometryAvailability(scene.Raw(), entity), domain));
    }
    entt::entity Make(Extrinsic::ECS::Scene::Registry &scene, D domain)
    {
        auto entity = scene.Create();
        if (domain >= D::MeshVertex && domain <= D::MeshFace)
        {
            Geometry::HalfedgeMesh::Mesh mesh;
            auto a = mesh.AddVertex(plane[0]), b = mesh.AddVertex(plane[1]), c = mesh.AddVertex(plane[2]),
                 d = mesh.AddVertex(plane[3]);
            (void)mesh.AddTriangle(a, b, c);
            (void)mesh.AddTriangle(c, b, d);
            GS::PopulateFromMesh(scene.Raw(), entity, mesh);
            if (domain == D::MeshFace)
                scene.Raw().get<GS::Faces>(entity).Properties.Resize(4);
        }
        else if (domain != D::PointCloudPoint)
        {
            Geometry::Graph::Graph graph;
            auto a = graph.AddVertex(plane[0]), b = graph.AddVertex(plane[1]), c = graph.AddVertex(plane[2]),
                 d = graph.AddVertex(plane[3]);
            (void)graph.AddEdge(a, b);
            (void)graph.AddEdge(a, c);
            (void)graph.AddEdge(a, d);
            (void)graph.AddEdge(b, c);
            (void)graph.AddEdge(b, d);
            (void)graph.AddEdge(c, d);
            GS::PopulateFromGraph(scene.Raw(), entity, graph);
        }
        else
            scene.Raw().emplace<GS::Vertices>(entity).Properties.Resize(5);
        auto &props = Properties(scene, entity, domain);
        auto samples = props.GetOrAdd<glm::vec3>("samples");
        for (std::size_t i = 0; i < samples.Size(); ++i)
            samples[i] = {float(i%3),float(i/3),.15f*float(int(i%3)-1)};
        props.GetOrAdd<glm::vec3>("directions").Vector().assign(props.Size(),glm::vec3(0,0,1));
        auto keep = props.GetOrAdd<float>("keep");
        std::ranges::fill(keep.Vector(), 42.f);
        if (domain == D::PointCloudPoint)
        {
            props.GetOrAdd<bool>("v:deleted")[4] = true;
            samples[4] = {std::numeric_limits<float>::quiet_NaN(), 0, 0};
        }
        return entity;
    }
    R::DensityWeightConfig Config(entt::entity entity,D domain)
    {
        R::DensityWeightConfig c;c.StableEntityId=R::SelectionController::ToStableEntityId(entity);
        c.Positions={domain,"samples",Geometry::PropertyValueKind::Vec3};
        c.Weights={domain,"weights",Geometry::PropertyValueKind::Float};c.SupportRadius=2;return c;
    }
}
TEST(DensityWeightConfig, RoundTripAndSharedPreviewApplyRun)
{
    namespace C=Extrinsic::Core::Config;Extrinsic::ECS::Scene::Registry scene;
    const auto entity=Make(scene,D::MeshFace);auto c=Config(entity,D::MeshFace);
    c.Kernel=decltype(c.Kernel)::Gaussian;c.Mode=decltype(c.Mode)::Reciprocal;c.SupportRadius=.75;
    C::EngineConfigSectionRegistry registry;ASSERT_TRUE(registry.Register(R::MakeDensityWeightConfigSectionRegistration()));
    R::RuntimeEngineConfigControlState state;C::PopulateEngineConfigSectionDefaults(state.ActiveConfig,registry);
    R::EditorProcessingContext context{.Scene=&scene};context.EngineConfigControlState=&state;context.EngineConfigCommandsAvailable=true;
    unsigned previews=0,applies=0;
    context.PreviewEngineConfigDocument=[&](const auto& document,const auto& origin){++previews;return C::PreviewEngineConfig(document,state.ActiveConfig,{origin,&registry});};
    context.ApplyEngineConfigHotSubset=[&](const auto& preview){++applies;state.ActiveConfig=preview.Preview.Config;return R::RuntimeEngineConfigApplyResult{.Status=R::RuntimeEngineConfigApplyStatus::Applied};};
    auto commands=R::BindEditorProcessingCommands(context);
    ASSERT_TRUE(R::PreviewEditorDensityWeightCommand(commands,c).Enabled);EXPECT_FALSE(Properties(scene,entity,D::MeshFace).Exists(c.Weights.Name));
    ASSERT_TRUE(R::ApplyEditorDensityWeightConfig(commands,c).Succeeded());ASSERT_TRUE(R::GetEditorDensityWeightConfig(commands));
    EXPECT_EQ(R::SerializeDensityWeightConfig(*R::GetEditorDensityWeightConfig(commands)),R::SerializeDensityWeightConfig(c));
    ASSERT_TRUE(R::ApplyEditorConfiguredDensityWeight(commands).Succeeded());EXPECT_EQ(previews,1);EXPECT_EQ(applies,1);
    for(auto payload:{R"({"backend":"vulkan"})",R"({"support_radius":0})",R"({"support_radius":-1})",R"({"support_radius":1e100})",
        R"({"kernel":"unknown"})",R"({"mode":"unknown"})",R"({"gpu_query_batch_size":0})",R"({"gpu_radius_capacity":0})",
        R"({"gpu_radius_capacity":1025})",R"({"unknown":1})"})
        EXPECT_FALSE(R::ValidateDensityWeightConfigSection(payload,{},"test").Usable())<<payload;
    auto bad=c;bad.Weights.Name=bad.Positions.Name;EXPECT_FALSE(R::ValidateDensityWeightConfigSection(R::SerializeDensityWeightConfig(bad),{},"test").Usable());
    bad=c;bad.Weights.Domain=D::GraphEdge;EXPECT_FALSE(R::ValidateDensityWeightConfigSection(R::SerializeDensityWeightConfig(bad),{},"test").Usable());
    c.SupportRadius=-1;EXPECT_FALSE(R::ApplyEditorDensityWeightConfig(commands,c).Succeeded());EXPECT_EQ(applies,1);
    EXPECT_TRUE(R::ValidateDensityWeightConfigSection(R"({"support_radius":1e-310})",{},"test").Usable());
}
TEST(DensityWeightOperations, EveryDomainKernelModeCacheHistoryAndDeletedRows)
{
    for(unsigned d=1;d<=8;++d)for(unsigned kernel=0;kernel<3;++kernel)for(unsigned mode=0;mode<2;++mode)
    {
        SCOPED_TRACE(d);
        SCOPED_TRACE(kernel);
        SCOPED_TRACE(mode);
        R::WorldRegistry worlds;const auto world=worlds.CreateWorld("weights");auto& scene=*worlds.Get(world);R::SpatialIndexCache cache(worlds);
        const auto entity=Make(scene,D(d));auto c=Config(entity,D(d));c.Kernel=decltype(c.Kernel)(kernel);c.Mode=decltype(c.Mode)(mode);
        auto& props=Properties(scene,entity,D(d));const auto size=props.Size();const bool half=D(d)==D::MeshHalfedge || D(d)==D::GraphHalfedge;
        if(half)scene.Raw().get<GS::Edges>(entity).Properties.GetOrAdd<bool>("e:deleted")[1]=true;
        else props.GetOrAdd<bool>(D(d)==D::MeshFace?"f:deleted":(D(d)==D::MeshEdge || D(d)==D::GraphEdge)?"e:deleted":"v:deleted")[2]=true;
        props.GetOrAdd<float>(c.Weights.Name).Vector().assign(size,77);
        const auto revision=std::as_const(props).Get<glm::vec3>("samples").Revision();R::EditorCommandHistory history;
        R::EditorProcessingContext context{.Scene=&scene,.World=world,.CommandHistory=&history,.SpatialIndices=&cache};
        const auto catalog=R::GetEditorPointInputCatalog(R::BindEditorProcessingCommands(context),c.StableEntityId);
        EXPECT_TRUE(std::ranges::any_of(catalog.Entries,[&](auto& e){return e.Ref==c.Positions;}));
        ASSERT_TRUE(R::PreviewEditorDensityWeightCommand(R::BindEditorProcessingCommands(context),c).Enabled);
        const auto reference=R::ApplyEditorDensityWeightCommand(R::BindEditorProcessingCommands(context),c);ASSERT_TRUE(reference.Succeeded())<<reference.Message;
        const auto expected=std::as_const(props).Get<float>(c.Weights.Name).Vector();EXPECT_EQ(expected[2],77);if(half)EXPECT_EQ(expected[3],77);
        EXPECT_EQ(reference.ActualBackend,"cpu_kdtree");EXPECT_EQ(props.Size(),size);EXPECT_EQ(std::as_const(props).Get<glm::vec3>("samples").Revision(),revision);
        props.Get<float>("keep")[0]=99;ASSERT_TRUE(history.Undo().Succeeded());EXPECT_EQ(std::as_const(props).Get<float>(c.Weights.Name).Vector(),std::vector<float>(size,77));
        ASSERT_TRUE(history.Redo().Succeeded());EXPECT_EQ(std::as_const(props).Get<float>("keep")[0],99);
        c.Backend=R::DensityWeightBackend::CpuLBVH;const auto indexed=R::ApplyEditorDensityWeightCommand(R::BindEditorProcessingCommands(context),c);
        ASSERT_TRUE(indexed.Succeeded())<<indexed.Message;EXPECT_EQ(indexed.ActualBackend,"cpu_lbvh");EXPECT_EQ(indexed.WrittenCount,reference.WrittenCount);
        EXPECT_EQ(indexed.Diagnostics.NeighborContributionCount,reference.Diagnostics.NeighborContributionCount);
        EXPECT_EQ(std::as_const(props).Get<float>(c.Weights.Name).Vector(),expected);EXPECT_TRUE(R::ApplyEditorDensityWeightCommand(R::BindEditorProcessingCommands(context),c).IndexReused);
    }
}
TEST(DensityWeightOperations, JobsRejectChangedSourceOutputAndCancellation)
{
    for(unsigned change=0;change<5;++change)
    {
        SCOPED_TRACE(change);Extrinsic::ECS::Scene::Registry scene;const auto entity=Make(scene,D::MeshVertex);auto c=Config(entity,D::MeshVertex);
        auto& props=Properties(scene,entity,D::MeshVertex);Intrinsic::Tests::EditorFeatureTestContext context;context.Scene=&scene;
        R::EditorCommandHistory history;context.CommandHistory=&history;std::optional<R::EditorDensityWeightResult> delivered;
        std::function<void(R::EditorDensityWeightResult)> sink=[&](auto r){delivered=std::move(r);};Extrinsic::Tests::EditorJobHarness jobs;jobs.Attach(context);
        ASSERT_EQ(R::ApplyEditorDensityWeightCommand(R::BindEditorProcessingCommands(context),c, sink).Status,R::EditorCommandStatus::Pending);
        switch(change){case 0:props.Get<float>("keep")[0]=99;break;case 1:props.Get<glm::vec3>("samples")[0].x+=1;break;
            case 2:props.GetOrAdd<bool>("v:deleted")[0]=true;break;case 3:props.GetOrAdd<float>(c.Weights.Name)[0]=77;break;
            case 4:(void)jobs.Jobs().Cancel(jobs.Snapshot().Entries[0].Token);break;}
        ASSERT_TRUE(jobs.DrainUntilTerminal());ASSERT_TRUE(delivered);EXPECT_EQ(delivered->Succeeded(),change==0)<<delivered->Message;
        EXPECT_EQ(history.CanUndo(),change==0);EXPECT_EQ(props.Exists(c.Weights.Name),change==0 || change==3);
        if(change==3)EXPECT_EQ(std::as_const(props).Get<float>(c.Weights.Name)[0],77);
    }
}
TEST(DensityWeightOperations, OneSampleAndBackendPreflightPreserveOutputs)
{
    R::WorldRegistry worlds;const auto world=worlds.CreateWorld("weights");auto& scene=*worlds.Get(world);R::SpatialIndexCache cache(worlds);
    const auto entity=Make(scene,D::PointCloudPoint);auto c=Config(entity,D::PointCloudPoint);auto& props=Properties(scene,entity,D::PointCloudPoint);
    props.Resize(1);props.Get<glm::vec3>("samples")[0]={0,0,0};R::EditorCommandHistory history;
    R::EditorProcessingContext context{.Scene=&scene,.World=world,.CommandHistory=&history,.SpatialIndices=&cache};
    for(auto backend:{R::DensityWeightBackend::CpuKDTree,R::DensityWeightBackend::CpuLBVH})
    {
        c.Backend=backend;ASSERT_TRUE(R::ApplyEditorDensityWeightCommand(R::BindEditorProcessingCommands(context),c).Succeeded());
        EXPECT_EQ(std::as_const(props).Get<float>(c.Weights.Name)[0],1);ASSERT_TRUE(history.Undo().Succeeded());EXPECT_FALSE(props.Exists(c.Weights.Name));
    }
    props.GetOrAdd<float>(c.Weights.Name)[0]=77;
    for(auto name:{"samples","directions","v:deleted"})
    {auto bad=c;bad.Weights.Name=name;EXPECT_FALSE(R::PreviewEditorDensityWeightCommand(R::BindEditorProcessingCommands(context),bad).Enabled);}
    auto unrelatedName=c;unrelatedName.Weights.Name="h:connectivity";
    EXPECT_TRUE(R::PreviewEditorDensityWeightCommand(R::BindEditorProcessingCommands(context),unrelatedName).Enabled);
    c.SupportRadius=double(Geometry::PointLBVH::CoordinateLimit);EXPECT_FALSE(R::PreviewEditorDensityWeightCommand(R::BindEditorProcessingCommands(context),c).Enabled);
    c.SupportRadius=1;c.Backend=R::DensityWeightBackend::VulkanLBVH;
    for(float value:{std::numeric_limits<float>::denorm_min(),-std::numeric_limits<float>::denorm_min()})
    {props.Get<glm::vec3>("samples")[0]={value,0,0};const auto ready=R::PreviewEditorDensityWeightCommand(R::BindEditorProcessingCommands(context),c);EXPECT_FALSE(ready.Enabled);EXPECT_NE(ready.DisabledReason.find("subnormal"),std::string::npos);}
    c.Backend=R::DensityWeightBackend::CpuKDTree;props.Get<glm::vec3>("samples")[0]={std::numeric_limits<float>::quiet_NaN(),0,0};
    EXPECT_FALSE(R::ApplyEditorDensityWeightCommand(R::BindEditorProcessingCommands(context),c).Succeeded());EXPECT_EQ(std::as_const(props).Get<float>(c.Weights.Name)[0],77);
}

TEST(DensityWeightOperations, PublicationUndoAndRedoAdvancePropertyWithoutInvalidatingGeometry)
{
    namespace Dirty = Extrinsic::ECS::Components::DirtyTags;
    for (const bool existing : {false, true})
    {
        Extrinsic::ECS::Scene::Registry scene;
        const auto entity = Make(scene, D::PointCloudPoint);
        const auto config = Config(entity, D::PointCloudPoint);
        auto& props = Properties(scene, entity, D::PointCloudPoint);
        if (existing) props.GetOrAdd<float>(config.Weights.Name).Vector().assign(props.Size(), 77);
        R::EditorCommandHistory history;
        unsigned invalidations = 0;
        R::EditorProcessingContext context{.Scene=&scene, .CommandHistory=&history};
        context.InvalidateWorkspaceSnapshotCache = [&] { ++invalidations; };
        const auto clear = [&] { scene.Raw().remove<Dirty::GpuDirty, Dirty::DirtyVertexAttributes>(entity); };
        auto revision = props.FindPropertyRevision(config.Weights.Name);
        const auto notified = [&](unsigned expected) {
            EXPECT_FALSE((scene.Raw().any_of<Dirty::GpuDirty, Dirty::DirtyVertexAttributes>(entity)));
            const auto current = props.FindPropertyRevision(config.Weights.Name);
            EXPECT_NE(current, revision);
            revision = current;
            EXPECT_EQ(invalidations, expected);
        };
        clear();
        ASSERT_TRUE(R::PreviewEditorDensityWeightCommand(R::BindEditorProcessingCommands(context), config).Enabled);
        EXPECT_EQ(invalidations, 0);
        EXPECT_FALSE(scene.Raw().any_of<Dirty::GpuDirty>(entity));
        EXPECT_FALSE(scene.Raw().any_of<Dirty::DirtyVertexAttributes>(entity));
        ASSERT_TRUE(R::ApplyEditorDensityWeightCommand(R::BindEditorProcessingCommands(context), config).Succeeded());
        notified(1);
        const auto computed = std::as_const(props).Get<float>(config.Weights.Name).Vector();
        clear();
        ASSERT_TRUE(history.Undo().Succeeded());
        notified(2);
        EXPECT_EQ(props.Exists(config.Weights.Name), existing);
        if (existing)
            EXPECT_EQ(std::as_const(props).Get<float>(config.Weights.Name).Vector(), std::vector<float>(props.Size(), 77));
        clear();
        ASSERT_TRUE(history.Redo().Succeeded());
        notified(3);
        EXPECT_EQ(std::as_const(props).Get<float>(config.Weights.Name).Vector(), computed);
    }
}

TEST(DensityWeightOperations, ReplacedStorageRejectsHistoryWithoutNotifications)
{
    namespace Dirty = Extrinsic::ECS::Components::DirtyTags;
    for (unsigned replacement = 0; replacement < 4; ++replacement)
    for (const bool redo : {false, true})
    {
        SCOPED_TRACE(replacement);
        SCOPED_TRACE(redo);
        Extrinsic::ECS::Scene::Registry scene;
        const auto entity = Make(scene, D::PointCloudPoint);
        const auto config = Config(entity, D::PointCloudPoint);
        auto& props = Properties(scene, entity, D::PointCloudPoint);
        props.GetOrAdd<float>(config.Weights.Name).Vector().assign(props.Size(), 77);
        R::EditorCommandHistory history;
        unsigned invalidations = 0;
        R::EditorProcessingContext context{.Scene=&scene, .CommandHistory=&history};
        context.InvalidateWorkspaceSnapshotCache = [&] { ++invalidations; };
        ASSERT_TRUE(R::ApplyEditorDensityWeightCommand(R::BindEditorProcessingCommands(context), config).Succeeded());
        if (redo) ASSERT_TRUE(history.Undo().Succeeded());
        const auto unchanged = std::as_const(props).Get<float>(config.Weights.Name).Vector();
        if (replacement == 0)
        {
            const auto values = std::as_const(props).Get<glm::vec3>(config.Positions.Name).Vector();
            auto old = props.Get<glm::vec3>(config.Positions.Name);
            props.Remove(old);
            props.GetOrAdd<glm::vec3>(config.Positions.Name).Vector() = values;
        }
        else if (replacement == 3)
        {
            const auto values = std::as_const(props).Get<bool>("v:deleted").Vector();
            auto old = props.Get<bool>("v:deleted");
            props.Remove(old);
            props.GetOrAdd<bool>("v:deleted").Vector() = values;
        }
        else
        {
            auto old = props.Get<float>(config.Weights.Name);
            props.Remove(old);
            if (replacement == 1) props.GetOrAdd<glm::vec3>(config.Weights.Name).Vector().assign(props.Size(), glm::vec3(17));
            else props.GetOrAdd<float>(config.Weights.Name).Vector() = unchanged;
        }
        scene.Raw().remove<Dirty::GpuDirty, Dirty::DirtyVertexAttributes>(entity);
        const auto previousInvalidations = invalidations;
        EXPECT_EQ((redo ? history.Redo() : history.Undo()).Status, R::EditorCommandHistoryStatus::StaleEntity);
        EXPECT_EQ(invalidations, previousInvalidations);
        EXPECT_FALSE(scene.Raw().any_of<Dirty::GpuDirty>(entity));
        EXPECT_FALSE(scene.Raw().any_of<Dirty::DirtyVertexAttributes>(entity));
        if (replacement == 1) EXPECT_EQ(std::as_const(props).Get<glm::vec3>(config.Weights.Name)[0], glm::vec3(17));
        else EXPECT_EQ(std::as_const(props).Get<float>(config.Weights.Name).Vector(), unchanged);
    }
}

TEST(DensityWeightOperations, InvalidDeletionSourcesRejectPreviewAndExecution)
{
    for (unsigned domain = 1; domain <= 8; ++domain)
    for (const bool wrongType : {false, true})
    {
        SCOPED_TRACE(domain);
        SCOPED_TRACE(wrongType);
        Extrinsic::ECS::Scene::Registry scene;
        const auto entity = Make(scene, D(domain));
        const auto config = Config(entity, D(domain));
        const bool half = D(domain) == D::MeshHalfedge || D(domain) == D::GraphHalfedge;
        const auto deletionDomain = half ? (D(domain) == D::MeshHalfedge ? D::MeshEdge : D::GraphEdge) : D(domain);
        auto& deletionProps = Properties(scene, entity, deletionDomain);
        const auto* name = deletionDomain == D::MeshFace ? "f:deleted" :
            (deletionDomain == D::MeshEdge || deletionDomain == D::GraphEdge) ? "e:deleted" : "v:deleted";
        auto deleted = deletionProps.GetOrAdd<bool>(name);
        if (wrongType)
        {
            deletionProps.Remove(deleted);
            (void)deletionProps.GetOrAdd<float>(name);
        }
        else deleted.Vector().pop_back();
        R::EditorCommandHistory history;
        R::EditorProcessingContext context{.Scene=&scene, .CommandHistory=&history};
        EXPECT_FALSE(R::PreviewEditorDensityWeightCommand(R::BindEditorProcessingCommands(context), config).Enabled);
        EXPECT_EQ(R::ApplyEditorDensityWeightCommand(R::BindEditorProcessingCommands(context), config).Status, R::EditorCommandStatus::InvalidProcessingParameters);
        EXPECT_FALSE(history.CanUndo());
        EXPECT_FALSE(Properties(scene, entity, D(domain)).Exists(config.Weights.Name));
    }
}

TEST(DensityWeightOperations, ExpiredQueuedCommandsNeverBorrowFreedSceneOrDeliver)
{
    auto scene = std::make_unique<Extrinsic::ECS::Scene::Registry>();
    const auto entity = Make(*scene, D::MeshVertex);
    const auto config = Config(entity, D::MeshVertex);
    bool active = true;
    Intrinsic::Tests::EditorFeatureTestContext context;
    context.Scene = scene.get();
    context.AttachmentActive = [&] { return active; };
    R::EditorCommandHistory history;
    context.CommandHistory = &history;
    Extrinsic::Tests::EditorJobHarness jobs;
    jobs.Attach(context);
    unsigned deliveries = 0;
    const auto commands = R::BindEditorProcessingCommands(context);
    ASSERT_EQ(R::ApplyEditorDensityWeightCommand(commands, config,
        [&](R::EditorDensityWeightResult) { ++deliveries; }).Status, R::EditorCommandStatus::Pending);
    active = false;
    scene.reset();
    ASSERT_TRUE(jobs.DrainUntilTerminal());
    EXPECT_EQ(deliveries, 0u);
    EXPECT_FALSE(history.CanUndo());
    EXPECT_FALSE(commands.IsBound());
    EXPECT_TRUE(R::GetEditorPointInputCatalog(commands, config.StableEntityId).Entries.empty());
}

TEST(DensityWeightOperations, SynchronousOutcomeIsReturnedWithoutQueuedDelivery)
{
    Extrinsic::ECS::Scene::Registry scene;
    const auto entity = Make(scene, D::PointCloudPoint);
    unsigned deliveries = 0;
    const auto result = R::ApplyEditorDensityWeightCommand(R::BindEditorProcessingCommands({.Scene=&scene}),
        Config(entity, D::PointCloudPoint), [&](R::EditorDensityWeightResult) { ++deliveries; });
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_EQ(deliveries, 0u);
}
