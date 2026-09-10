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
    R::KeypointAnalysisConfig Config(entt::entity entity, D domain)
    {
        return {.StableEntityId = R::SelectionController::ToStableEntityId(entity),
                .Positions = {.Domain = domain,
                              .Name = "samples",
                              .ValueKind = Geometry::PropertyValueKind::Vec3},
                .Mask = {domain,"keypoints",Geometry::PropertyValueKind::UInt32},
                .Score = {domain,"saliency",Geometry::PropertyValueKind::Float},
                .MinimumNeighbors=1, .SalientRadius=10, .NonMaxRadius=.5f};
    }
} // namespace

TEST(KeypointAnalysisConfig, RoundTripAndSharedPreviewApplyRun)
{
    namespace C = Extrinsic::Core::Config;
    Extrinsic::ECS::Scene::Registry scene;
    auto entity = Make(scene, D::MeshFace);
    auto config = Config(entity, D::MeshFace);
    config.SalientRadius = 5;
    C::EngineConfigSectionRegistry registry;
    ASSERT_TRUE(registry.Register(R::MakeKeypointAnalysisConfigSectionRegistration()));
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
    ASSERT_TRUE(R::PreviewEditorKeypointAnalysisCommand(commands, config).Ready);
    EXPECT_FALSE(Properties(scene, entity, D::MeshFace).Exists("saliency"));
    ASSERT_TRUE(R::ApplyEditorKeypointAnalysisConfig(commands, config).Succeeded());
    ASSERT_TRUE(R::GetEditorKeypointAnalysisConfig(commands));
    EXPECT_EQ(R::SerializeKeypointAnalysisConfig(*R::GetEditorKeypointAnalysisConfig(commands)),
              R::SerializeKeypointAnalysisConfig(config));
    ASSERT_TRUE(R::ApplyEditorConfiguredKeypointAnalysis(commands).Succeeded());
    EXPECT_EQ(previews, 1);
    EXPECT_EQ(applies, 1);
    for(auto payload:{R"({"backend":"vulkan"})",R"({"minimum_neighbors":-1})",R"({"gamma21":1.1})",R"({"gamma32":-0.1})",
                      R"({"gpu_query_batch_size":0})",R"({"gpu_radius_capacity":0})",R"({"gpu_radius_capacity":1025})",
                      R"({"salient_radius":1e100})",R"({"salient_radius":1e-100})",R"({"unknown":1})"})
        EXPECT_FALSE(R::ValidateKeypointAnalysisConfigSection(payload,{},"test").Usable())<<payload;
    config.SalientRadius = -1;
    EXPECT_FALSE(R::ApplyEditorKeypointAnalysisConfig(commands, config).Succeeded());
    EXPECT_EQ(applies, 1);
}

TEST(KeypointAnalysisOperations, EveryDomainReferenceCacheHistoryAndDeletedRows)
{
    for(unsigned d=1;d<=8;++d)
    {
        SCOPED_TRACE(d);
        R::WorldRegistry worlds;auto world=worlds.CreateWorld("keypoints");auto& scene=*worlds.Get(world);
        R::SpatialIndexCache cache(worlds);auto entity=Make(scene,D(d));auto c=Config(entity,D(d));
        auto& props=Properties(scene,entity,D(d));const auto size=props.Size();
        const bool half=D(d)==D::MeshHalfedge || D(d)==D::GraphHalfedge;
        if(half)scene.Raw().get<GS::Edges>(entity).Properties.GetOrAdd<bool>("e:deleted")[1]=true;
        else props.GetOrAdd<bool>(D(d)==D::MeshFace?"f:deleted":(D(d)==D::MeshEdge || D(d)==D::GraphEdge)?"e:deleted":"v:deleted")[2]=true;
        props.GetOrAdd<std::uint32_t>("keypoints").Vector().assign(size,77);
        props.GetOrAdd<float>("saliency").Vector().assign(size,77);
        const auto revision=std::as_const(props).Get<glm::vec3>("samples").Revision();
        R::EditorCommandHistory history;
        R::EditorGeometryProcessingContext context{.Scene=&scene,.World=world,.CommandHistory=&history,.SpatialIndices=&cache};
        const auto catalog=R::GetEditorKeypointAnalysisInputCatalog(context,c.StableEntityId);
        EXPECT_TRUE(std::ranges::any_of(catalog.Entries,[&](auto& e){return e.Ref==c.Positions;}));
        ASSERT_TRUE(R::PreviewEditorKeypointAnalysisCommand(context,c).Ready);
        const auto reference=R::ApplyEditorKeypointAnalysisCommand(context,c);
        ASSERT_TRUE(reference.Succeeded())<<reference.Message;EXPECT_EQ(reference.ActualBackend,"cpu_kdtree");
        const auto mask=std::as_const(props).Get<std::uint32_t>("keypoints").Vector();
        const auto score=std::as_const(props).Get<float>("saliency").Vector();
        EXPECT_EQ(mask[2],77);EXPECT_EQ(score[2],77);if(half){EXPECT_EQ(mask[3],77);EXPECT_EQ(score[3],77);}
        EXPECT_EQ(props.Size(),size);EXPECT_EQ(std::as_const(props).Get<glm::vec3>("samples").Revision(),revision);
        props.Get<float>("keep")[0]=99;
        ASSERT_TRUE(history.Undo().Succeeded());EXPECT_EQ(std::as_const(props).Get<float>("saliency").Vector(),std::vector<float>(size,77));
        ASSERT_TRUE(history.Redo().Succeeded());EXPECT_EQ(std::as_const(props).Get<float>("keep")[0],99);
        c.Backend=R::KeypointAnalysisBackend::CpuLBVH;
        const auto indexed=R::ApplyEditorKeypointAnalysisCommand(context,c);ASSERT_TRUE(indexed.Succeeded())<<indexed.Message;
        EXPECT_EQ(indexed.ActualBackend,"cpu_lbvh");EXPECT_EQ(indexed.KeypointCount,reference.KeypointCount);
        EXPECT_EQ(std::as_const(props).Get<std::uint32_t>("keypoints").Vector(),mask);
        for(std::size_t i=0;i<size;++i)EXPECT_NEAR(std::as_const(props).Get<float>("saliency")[i],score[i],1e-5);
        EXPECT_TRUE(R::ApplyEditorKeypointAnalysisCommand(context,c).IndexReused);
        Intrinsic::Tests::EditorFeatureTestContext visualization;visualization.Scene=&scene;visualization.VisualizationCommandsAvailable=true;
        std::optional<R::VisualizationRecipe> stored;
        visualization.VisualizationRecipes.GetRecipe=[&](std::uint32_t){return stored;};
        visualization.VisualizationRecipes.SetRecipe=[&](std::uint32_t,R::VisualizationRecipe value){stored=std::move(value);};
        visualization.VisualizationRecipes.ClearRecipe=[&](std::uint32_t){stored.reset();};
        EXPECT_EQ(R::ApplyEditorVisualizationRecipeCommand(visualization,{.StableEntityId=c.StableEntityId,
            .Recipe={.Data=R::ScalarVisualizationRecipe{.Source=c.Score,.OutputName="saliency_colors"}}}),R::EditorCommandStatus::Applied);
        ASSERT_TRUE(stored);ASSERT_TRUE(std::get_if<R::ScalarVisualizationRecipe>(&stored->Data));
    }
}
TEST(KeypointAnalysisOperations, JobsRejectChangedInputsOutputsAndCancellation)
{
    for(unsigned change=0;change<6;++change)
    {
        SCOPED_TRACE(change);Extrinsic::ECS::Scene::Registry scene;auto entity=Make(scene,D::MeshVertex);auto c=Config(entity,D::MeshVertex);
        auto& props=Properties(scene,entity,D::MeshVertex);
        Intrinsic::Tests::EditorFeatureTestContext context;context.Scene=&scene;R::EditorCommandHistory history;context.CommandHistory=&history;
        std::optional<R::EditorKeypointAnalysisResult> delivered;context.MethodResultSinks.KeypointAnalysis=[&](auto r){delivered=std::move(r);};
        Extrinsic::Tests::EditorJobHarness jobs;jobs.Attach(context);
        ASSERT_EQ(R::ApplyEditorKeypointAnalysisCommand(context,c).Status,R::EditorCommandStatus::Pending);
        switch(change){case 0:props.Get<float>("keep")[0]=99;break;case 1:props.Get<glm::vec3>("samples")[0].x+=1;break;
            case 2:props.GetOrAdd<bool>("v:deleted")[0]=true;break;case 3:props.GetOrAdd<float>("saliency")[0]=77;break;
            case 4:(void)jobs.Jobs().Cancel(jobs.Snapshot().Entries[0].Token);break;case 5:props.GetOrAdd<std::uint32_t>("keypoints")[0]=77;break;}
        ASSERT_TRUE(jobs.DrainUntilTerminal());ASSERT_TRUE(delivered);EXPECT_EQ(delivered->Succeeded(),change==0)<<delivered->Message;
        EXPECT_EQ(history.CanUndo(),change==0);EXPECT_EQ(props.Exists("saliency"),change==0 || change==3);
        EXPECT_EQ(props.Exists("keypoints"),change==0 || change==5);
    }
}
TEST(KeypointAnalysisOperations, InvalidScaleAndOutputPreflightRetainExistingData)
{
    Extrinsic::ECS::Scene::Registry scene;auto entity=Make(scene,D::MeshVertex);auto c=Config(entity,D::MeshVertex);
    auto& props=Properties(scene,entity,D::MeshVertex);R::EditorGeometryProcessingContext context{.Scene=&scene};
    for(auto name:{"samples","v:deleted","h:connectivity"}){auto bad=c;bad.Score.Name=name;EXPECT_FALSE(R::PreviewEditorKeypointAnalysisCommand(context,bad).Ready);}
    auto gpu=c;gpu.Backend=R::KeypointAnalysisBackend::VulkanLBVH;EXPECT_FALSE(R::PreviewEditorKeypointAnalysisCommand(context,gpu).Ready);
    props.GetOrAdd<float>("saliency").Vector().assign(props.Size(),77);
    props.Get<glm::vec3>("samples").Vector().assign(props.Size(),glm::vec3(0));
    EXPECT_FALSE(R::ApplyEditorKeypointAnalysisCommand(context,c).Succeeded());
    EXPECT_EQ(std::as_const(props).Get<float>("saliency").Vector(),std::vector<float>(props.Size(),77));EXPECT_FALSE(props.Exists("keypoints"));
}
