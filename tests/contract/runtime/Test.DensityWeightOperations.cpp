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
#include "SandboxEditorJobHarness.hpp"

import Extrinsic.Runtime.GeometryProcessingOperations;
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
    R::EditorGeometryProcessingContext context{.Scene=&scene};context.EngineConfigControlState=&state;context.EngineConfigCommandsAvailable=true;
    unsigned previews=0,applies=0;
    context.PreviewEngineConfigDocument=[&](const auto& document,const auto& origin){++previews;return C::PreviewEngineConfig(document,state.ActiveConfig,{origin,&registry});};
    context.ApplyEngineConfigHotSubset=[&](const auto& preview){++applies;state.ActiveConfig=preview.Preview.Config;return R::RuntimeEngineConfigApplyResult{.Status=R::RuntimeEngineConfigApplyStatus::Applied};};
    auto commands=R::BindEditorGeometryProcessingCommands(context);
    ASSERT_TRUE(R::PreviewEditorDensityWeightCommand(commands,c).Ready);EXPECT_FALSE(Properties(scene,entity,D::MeshFace).Exists(c.Weights.Name));
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
        R::EditorGeometryProcessingContext context{.Scene=&scene,.World=world,.CommandHistory=&history,.SpatialIndices=&cache};
        const auto catalog=R::GetEditorDensityWeightInputCatalog(context,c.StableEntityId);
        EXPECT_TRUE(std::ranges::any_of(catalog.Entries,[&](auto& e){return e.Ref==c.Positions;}));
        ASSERT_TRUE(R::PreviewEditorDensityWeightCommand(context,c).Ready);
        const auto reference=R::ApplyEditorDensityWeightCommand(context,c);ASSERT_TRUE(reference.Succeeded())<<reference.Message;
        const auto expected=std::as_const(props).Get<float>(c.Weights.Name).Vector();EXPECT_EQ(expected[2],77);if(half)EXPECT_EQ(expected[3],77);
        EXPECT_EQ(reference.ActualBackend,"cpu_kdtree");EXPECT_EQ(props.Size(),size);EXPECT_EQ(std::as_const(props).Get<glm::vec3>("samples").Revision(),revision);
        props.Get<float>("keep")[0]=99;ASSERT_TRUE(history.Undo().Succeeded());EXPECT_EQ(std::as_const(props).Get<float>(c.Weights.Name).Vector(),std::vector<float>(size,77));
        ASSERT_TRUE(history.Redo().Succeeded());EXPECT_EQ(std::as_const(props).Get<float>("keep")[0],99);
        c.Backend=R::DensityWeightBackend::CpuLBVH;const auto indexed=R::ApplyEditorDensityWeightCommand(context,c);
        ASSERT_TRUE(indexed.Succeeded())<<indexed.Message;EXPECT_EQ(indexed.ActualBackend,"cpu_lbvh");EXPECT_EQ(indexed.WrittenCount,reference.WrittenCount);
        EXPECT_EQ(indexed.Diagnostics.NeighborContributionCount,reference.Diagnostics.NeighborContributionCount);
        EXPECT_EQ(std::as_const(props).Get<float>(c.Weights.Name).Vector(),expected);EXPECT_TRUE(R::ApplyEditorDensityWeightCommand(context,c).IndexReused);
    }
}
TEST(DensityWeightOperations, JobsRejectChangedSourceOutputAndCancellation)
{
    for(unsigned change=0;change<5;++change)
    {
        SCOPED_TRACE(change);Extrinsic::ECS::Scene::Registry scene;const auto entity=Make(scene,D::MeshVertex);auto c=Config(entity,D::MeshVertex);
        auto& props=Properties(scene,entity,D::MeshVertex);Intrinsic::Tests::EditorFeatureTestContext context;context.Scene=&scene;
        R::EditorCommandHistory history;context.CommandHistory=&history;std::optional<R::EditorDensityWeightResult> delivered;
        context.MethodResultSinks.DensityWeight=[&](auto r){delivered=std::move(r);};Extrinsic::Tests::EditorJobHarness jobs;jobs.Attach(context);
        ASSERT_EQ(R::ApplyEditorDensityWeightCommand(context,c).Status,R::EditorCommandStatus::Pending);
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
    R::EditorGeometryProcessingContext context{.Scene=&scene,.World=world,.CommandHistory=&history,.SpatialIndices=&cache};
    for(auto backend:{R::DensityWeightBackend::CpuKDTree,R::DensityWeightBackend::CpuLBVH})
    {
        c.Backend=backend;ASSERT_TRUE(R::ApplyEditorDensityWeightCommand(context,c).Succeeded());
        EXPECT_EQ(std::as_const(props).Get<float>(c.Weights.Name)[0],1);ASSERT_TRUE(history.Undo().Succeeded());EXPECT_FALSE(props.Exists(c.Weights.Name));
    }
    props.GetOrAdd<float>(c.Weights.Name)[0]=77;
    for(auto name:{"samples","directions","v:deleted","h:connectivity"})
    {auto bad=c;bad.Weights.Name=name;EXPECT_FALSE(R::PreviewEditorDensityWeightCommand(context,bad).Ready);}
    c.SupportRadius=double(Geometry::PointLBVH::CoordinateLimit);EXPECT_FALSE(R::PreviewEditorDensityWeightCommand(context,c).Ready);
    c.SupportRadius=1;c.Backend=R::DensityWeightBackend::VulkanLBVH;
    for(float value:{std::numeric_limits<float>::denorm_min(),-std::numeric_limits<float>::denorm_min()})
    {props.Get<glm::vec3>("samples")[0]={value,0,0};const auto ready=R::PreviewEditorDensityWeightCommand(context,c);EXPECT_FALSE(ready.Ready);EXPECT_NE(ready.Diagnostic.find("subnormal"),std::string::npos);}
    c.Backend=R::DensityWeightBackend::CpuKDTree;props.Get<glm::vec3>("samples")[0]={std::numeric_limits<float>::quiet_NaN(),0,0};
    EXPECT_FALSE(R::ApplyEditorDensityWeightCommand(context,c).Succeeded());EXPECT_EQ(std::as_const(props).Get<float>(c.Weights.Name)[0],77);
}
