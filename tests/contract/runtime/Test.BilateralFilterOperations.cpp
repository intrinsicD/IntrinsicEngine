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
    R::BilateralFilterConfig Config(entt::entity entity, D domain)
    {
        return {.StableEntityId = R::SelectionController::ToStableEntityId(entity),
                .Positions = {.Domain = domain,
                              .Name = "samples",
                              .ValueKind = Geometry::PropertyValueKind::Vec3},
                .Normals = {domain,"directions",Geometry::PropertyValueKind::Vec3},
                .Output = {domain,"filtered",Geometry::PropertyValueKind::Vec3},
                .KNeighbors = 2, .SpatialSigma=2, .Iterations=3};
    }
} // namespace

TEST(BilateralFilterConfig, RoundTripAndSharedPreviewApplyRun)
{
    namespace C = Extrinsic::Core::Config;
    Extrinsic::ECS::Scene::Registry scene;
    auto entity = Make(scene, D::MeshFace);
    auto config = Config(entity, D::MeshFace);
    config.SpatialSigma = 5;
    C::EngineConfigSectionRegistry registry;
    ASSERT_TRUE(registry.Register(R::MakeBilateralFilterConfigSectionRegistration()));
    R::RuntimeEngineConfigControlState state;
    C::PopulateEngineConfigSectionDefaults(state.ActiveConfig, registry);
    R::EditorGeometryProcessingContext context{.Scene = &scene};
    context.EngineConfigControlState = &state;
    context.EngineConfigCommandsAvailable = true;
    unsigned previews = 0, applies = 0;
    context.PreviewEngineConfigDocument = [&](const auto &document, const auto &origin) {
        ++previews;
        return C::PreviewEngineConfig(document, state.ActiveConfig, {origin, &registry});
    };
    context.ApplyEngineConfigHotSubset = [&](const auto &preview) {
        ++applies;
        state.ActiveConfig = preview.Preview.Config;
        return R::RuntimeEngineConfigApplyResult{.Status = R::RuntimeEngineConfigApplyStatus::Applied};
    };
    auto commands = R::BindEditorGeometryProcessingCommands(context);
    ASSERT_TRUE(R::PreviewEditorBilateralFilterCommand(commands, config).Ready);
    EXPECT_FALSE(Properties(scene, entity, D::MeshFace).Exists("filtered"));
    ASSERT_TRUE(R::ApplyEditorBilateralFilterConfig(commands, config).Succeeded());
    ASSERT_TRUE(R::GetEditorBilateralFilterConfig(commands));
    EXPECT_EQ(R::SerializeBilateralFilterConfig(*R::GetEditorBilateralFilterConfig(commands)),
              R::SerializeBilateralFilterConfig(config));
    ASSERT_TRUE(R::ApplyEditorConfiguredBilateralFilter(commands).Succeeded());
    EXPECT_EQ(previews, 1);
    EXPECT_EQ(applies, 1);
    for (auto payload : {R"({"backend":"vulkan"})", R"({"k_neighbors":-1})", R"({"spatial_sigma":-1})",
                         R"({"gpu_query_batch_size":0})", R"({"spatial_sigma":1e100})", R"({"spatial_sigma":1e-100})", R"({"unknown":1})",
                         R"({"output":{"domain":"unknown","name":"x","kind":"uint32"}})", R"({"iterations":101})", R"({"normal_sigma":-1})"})
        EXPECT_FALSE(R::ValidateBilateralFilterConfigSection(payload, {}, "test").Usable()) << payload;
    config.SpatialSigma = -1;
    EXPECT_FALSE(R::ApplyEditorBilateralFilterConfig(commands, config).Succeeded());
    EXPECT_EQ(applies, 1);
}

TEST(BilateralFilterOperations, EveryDomainCopyAndInPlaceHistory)
{
    for(unsigned d=1;d<=8;++d)for(bool inPlace:{false,true})
    {
        SCOPED_TRACE(d);
        SCOPED_TRACE(inPlace);
        R::WorldRegistry worlds;auto world=worlds.CreateWorld("bilateral");auto& scene=*worlds.Get(world);
        R::SpatialIndexCache cache(worlds);auto entity=Make(scene,D(d));auto config=Config(entity,D(d));
        auto& props=Properties(scene,entity,D(d));const auto size=props.Size();
        const bool half=D(d)==D::MeshHalfedge || D(d)==D::GraphHalfedge;
        if(half)scene.Raw().get<GS::Edges>(entity).Properties.GetOrAdd<bool>("e:deleted")[1]=true;
        else props.GetOrAdd<bool>(D(d)==D::MeshFace?"f:deleted":(D(d)==D::MeshEdge || D(d)==D::GraphEdge)?"e:deleted":"v:deleted")[2]=true;
        const auto original=std::as_const(props).Get<glm::vec3>("samples").Vector();
        props.GetOrAdd<glm::vec3>("filtered").Vector().assign(size,glm::vec3(77));
        if(inPlace)config.Output=config.Positions;
        R::EditorCommandHistory history;
        R::EditorGeometryProcessingContext context{.Scene=&scene,.World=world,.CommandHistory=&history,.SpatialIndices=&cache};
        const auto catalog=R::GetEditorBilateralFilterInputCatalog(context,config.StableEntityId);
        EXPECT_TRUE(std::ranges::any_of(catalog.Entries,[&](auto& e){return e.Ref==config.Positions;}));
        EXPECT_TRUE(std::ranges::any_of(catalog.Entries,[&](auto& e){return e.Ref==config.Normals;}));
        ASSERT_TRUE(R::PreviewEditorBilateralFilterCommand(context,config).Ready);
        const auto reference=R::ApplyEditorBilateralFilterCommand(context,config);
        ASSERT_TRUE(reference.Succeeded())<<reference.Message;EXPECT_EQ(reference.CompletedIterations,3);
        const auto values=std::as_const(props).Get<glm::vec3>(config.Output.Name).Vector();
        EXPECT_EQ(values[2],inPlace?original[2]:glm::vec3(77));if(half)EXPECT_EQ(values[3],inPlace?original[3]:glm::vec3(77));
        EXPECT_EQ(props.Size(),size);props.Get<float>("keep")[0]=99;
        ASSERT_TRUE(history.Undo().Succeeded());
        EXPECT_EQ(std::as_const(props).Get<glm::vec3>(config.Output.Name)[0],inPlace?original[0]:glm::vec3(77));
        ASSERT_TRUE(history.Redo().Succeeded());EXPECT_EQ(std::as_const(props).Get<float>("keep")[0],99);
        ASSERT_TRUE(history.Undo().Succeeded());
        config.Backend=R::BilateralFilterBackend::CpuLBVH;
        const auto indexed=R::ApplyEditorBilateralFilterCommand(context,config);
        ASSERT_TRUE(indexed.Succeeded())<<indexed.Message;EXPECT_EQ(indexed.ActualBackend,"cpu_lbvh");EXPECT_EQ(indexed.WorkspaceBuilds,2);
        const auto actual=std::as_const(props).Get<glm::vec3>(config.Output.Name);
        for(std::size_t i=0;i<size;++i)if(i!=4 || D(d)!=D::PointCloudPoint)EXPECT_LE(glm::length(actual[i]-values[i]),1e-5);
        EXPECT_FLOAT_EQ(indexed.Diagnostics.AverageDisplacement,reference.Diagnostics.AverageDisplacement);
        props.Get<glm::vec3>("directions")[0].x=1;
        EXPECT_EQ(history.Undo().Status,R::EditorCommandHistoryStatus::StaleEntity);
    }
}
TEST(BilateralFilterOperations, JobsRejectChangedInputsOutputsAndCancellation)
{
    for(unsigned change=0;change<6;++change)
    {
        SCOPED_TRACE(change);Extrinsic::ECS::Scene::Registry scene;auto entity=Make(scene,D::MeshVertex);auto config=Config(entity,D::MeshVertex);
        auto& props=Properties(scene,entity,D::MeshVertex);
        Intrinsic::Tests::EditorFeatureTestContext context;context.Scene=&scene;R::EditorCommandHistory history;context.CommandHistory=&history;
        std::optional<R::EditorBilateralFilterResult> delivered;context.MethodResultSinks.BilateralFilter=[&](auto r){delivered=std::move(r);};
        Extrinsic::Tests::EditorJobHarness jobs;jobs.Attach(context);
        ASSERT_EQ(R::ApplyEditorBilateralFilterCommand(context,config).Status,R::EditorCommandStatus::Pending);
        EXPECT_FALSE(props.Exists("filtered"));
        switch(change){case 0:props.Get<float>("keep")[0]=99;break;case 1:props.Get<glm::vec3>("samples")[0].x+=1;break;case 2:props.GetOrAdd<bool>("v:deleted")[0]=true;break;case 3:props.GetOrAdd<glm::vec3>("filtered")[0]=glm::vec3(77);break;case 4:(void)jobs.Jobs().Cancel(jobs.Snapshot().Entries[0].Token);break;case 5:props.Get<glm::vec3>("directions")[0].x=1;break;}
        ASSERT_TRUE(jobs.DrainUntilTerminal());ASSERT_TRUE(delivered);EXPECT_EQ(delivered->Succeeded(),change==0)<<delivered->Message;
        EXPECT_EQ(history.CanUndo(),change==0);EXPECT_EQ(props.Exists("filtered"),change==0 || change==3);
    }
}
TEST(BilateralFilterOperations, ZeroPassAndOutputPreflight)
{
    Extrinsic::ECS::Scene::Registry scene;auto entity=Make(scene,D::MeshVertex);auto config=Config(entity,D::MeshVertex);
    auto& props=Properties(scene,entity,D::MeshVertex);R::EditorGeometryProcessingContext context{.Scene=&scene};
    config.Iterations=0;const auto result=R::ApplyEditorBilateralFilterCommand(context,config);ASSERT_TRUE(result.Succeeded());EXPECT_EQ(result.CompletedIterations,0);
    EXPECT_EQ(std::as_const(props).Get<glm::vec3>("filtered").Vector(),std::as_const(props).Get<glm::vec3>("samples").Vector());
    config.Output=config.Normals;EXPECT_FALSE(R::PreviewEditorBilateralFilterCommand(context,config).Ready);
    config.Output=config.Positions;config.Output.Name="v:deleted";EXPECT_FALSE(R::PreviewEditorBilateralFilterCommand(context,config).Ready);
    config.Output=config.Positions;config.Backend=R::BilateralFilterBackend::VulkanLBVH;EXPECT_FALSE(R::PreviewEditorBilateralFilterCommand(context,config).Ready);
}
