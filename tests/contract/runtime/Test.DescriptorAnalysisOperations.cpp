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
import Extrinsic.Graphics.Component.VisualizationConfig;
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
    R::DescriptorAnalysisConfig Config(entt::entity entity,D domain)
    {
        R::DescriptorAnalysisConfig c;c.StableEntityId=R::SelectionController::ToStableEntityId(entity);
        c.Positions={domain,"samples",Geometry::PropertyValueKind::Vec3};
        c.Normals={domain,"directions",Geometry::PropertyValueKind::Vec3};
        c.Outputs=R::MakeDescriptorOutputProperties(domain,"descriptor");c.FeatureRadius=10;return c;
    }
}
TEST(DescriptorAnalysisConfig, RoundTripAndSharedPreviewApplyRun)
{
    namespace C = Extrinsic::Core::Config;
    Extrinsic::ECS::Scene::Registry scene;
    auto entity = Make(scene, D::MeshFace);
    auto config = Config(entity, D::MeshFace);
    config.FeatureRadius = 5;
    C::EngineConfigSectionRegistry registry;
    ASSERT_TRUE(registry.Register(R::MakeDescriptorAnalysisConfigSectionRegistration()));
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
    ASSERT_TRUE(R::PreviewEditorDescriptorAnalysisCommand(commands, config).Ready);
    EXPECT_FALSE(Properties(scene, entity, D::MeshFace).Exists("descriptor.alpha0"));
    ASSERT_TRUE(R::ApplyEditorDescriptorAnalysisConfig(commands, config).Succeeded());
    ASSERT_TRUE(R::GetEditorDescriptorAnalysisConfig(commands));
    EXPECT_EQ(R::SerializeDescriptorAnalysisConfig(*R::GetEditorDescriptorAnalysisConfig(commands)),
              R::SerializeDescriptorAnalysisConfig(config));
    ASSERT_TRUE(R::ApplyEditorConfiguredDescriptorAnalysis(commands).Succeeded());
    EXPECT_EQ(previews, 1);
    EXPECT_EQ(applies, 1);
    for(auto payload:{R"({"backend":"vulkan"})",R"({"max_neighbors":-1})",R"({"outputs":[]})",
                      R"({"gpu_query_batch_size":0})",R"({"gpu_radius_capacity":0})",R"({"gpu_radius_capacity":1025})",
                      R"({"feature_radius":1e100})",R"({"feature_radius":1e-100})",R"({"unknown":1})"})
        EXPECT_FALSE(R::ValidateDescriptorAnalysisConfigSection(payload,{},"test").Usable())<<payload;
    auto invalid=config;invalid.Outputs[32]=invalid.Outputs[0];
    EXPECT_FALSE(R::ValidateDescriptorAnalysisConfigSection(R::SerializeDescriptorAnalysisConfig(invalid),{},"test").Usable());
    invalid=config;invalid.Normals.Domain=D::GraphEdge;
    EXPECT_FALSE(R::ValidateDescriptorAnalysisConfigSection(R::SerializeDescriptorAnalysisConfig(invalid),{},"test").Usable());
    config.FeatureRadius = -1;
    EXPECT_FALSE(R::ApplyEditorDescriptorAnalysisConfig(commands, config).Succeeded());
    EXPECT_EQ(applies, 1);
}

TEST(DescriptorAnalysisOperations, EveryDomainReferenceCacheHistoryAndDeletedRows)
{
    for(unsigned d=1;d<=8;++d)
    {
        SCOPED_TRACE(d);
        R::WorldRegistry worlds;auto world=worlds.CreateWorld("descriptors");auto& scene=*worlds.Get(world);
        R::SpatialIndexCache cache(worlds);auto entity=Make(scene,D(d));auto c=Config(entity,D(d));
        auto& props=Properties(scene,entity,D(d));const auto size=props.Size();
        const bool half=D(d)==D::MeshHalfedge || D(d)==D::GraphHalfedge;
        if(half)scene.Raw().get<GS::Edges>(entity).Properties.GetOrAdd<bool>("e:deleted")[1]=true;
        else props.GetOrAdd<bool>(D(d)==D::MeshFace?"f:deleted":(D(d)==D::MeshEdge || D(d)==D::GraphEdge)?"e:deleted":"v:deleted")[2]=true;
        for(const auto& output:c.Outputs)props.GetOrAdd<float>(output.Name).Vector().assign(size,77);
        const auto positionRevision=std::as_const(props).Get<glm::vec3>("samples").Revision();
        const auto normalRevision=std::as_const(props).Get<glm::vec3>("directions").Revision();
        R::EditorCommandHistory history;
        R::EditorGeometryProcessingContext context{.Scene=&scene,.World=world,.CommandHistory=&history,.SpatialIndices=&cache};
        const auto catalog=R::GetEditorDescriptorAnalysisInputCatalog(context,c.StableEntityId);
        EXPECT_TRUE(std::ranges::any_of(catalog.Entries,[&](auto& e){return e.Ref==c.Positions;}));
        EXPECT_TRUE(std::ranges::any_of(catalog.Entries,[&](auto& e){return e.Ref==c.Normals;}));
        ASSERT_TRUE(R::PreviewEditorDescriptorAnalysisCommand(context,c).Ready);
        const auto reference=R::ApplyEditorDescriptorAnalysisCommand(context,c);
        ASSERT_TRUE(reference.Succeeded())<<reference.Message;EXPECT_EQ(reference.ActualBackend,"cpu_kdtree");
        std::array<std::vector<float>,33> expected;
        for(unsigned b=0;b<33;++b)
        {
            expected[b]=std::as_const(props).Get<float>(c.Outputs[b].Name).Vector();
            EXPECT_EQ(expected[b][2],77);if(half)EXPECT_EQ(expected[b][3],77);
        }
        EXPECT_EQ(props.Size(),size);EXPECT_EQ(std::as_const(props).Get<glm::vec3>("samples").Revision(),positionRevision);
        EXPECT_EQ(std::as_const(props).Get<glm::vec3>("directions").Revision(),normalRevision);
        props.Get<float>("keep")[0]=99;ASSERT_TRUE(history.Undo().Succeeded());
        for(const auto& output:c.Outputs)EXPECT_EQ(std::as_const(props).Get<float>(output.Name).Vector(),std::vector<float>(size,77));
        ASSERT_TRUE(history.Redo().Succeeded());EXPECT_EQ(std::as_const(props).Get<float>("keep")[0],99);
        c.Backend=R::DescriptorAnalysisBackend::CpuLBVH;
        const auto indexed=R::ApplyEditorDescriptorAnalysisCommand(context,c);ASSERT_TRUE(indexed.Succeeded())<<indexed.Message;
        EXPECT_EQ(indexed.ActualBackend,"cpu_lbvh");EXPECT_EQ(indexed.WrittenCount,reference.WrittenCount);
        for(unsigned b=0;b<33;++b)EXPECT_EQ(std::as_const(props).Get<float>(c.Outputs[b].Name).Vector(),expected[b]);
        EXPECT_TRUE(R::ApplyEditorDescriptorAnalysisCommand(context,c).IndexReused);
        Intrinsic::Tests::EditorFeatureTestContext visualization;visualization.Scene=&scene;visualization.VisualizationCommandsAvailable=true;
        std::optional<R::VisualizationRecipe> stored;
        visualization.VisualizationRecipes.GetRecipe=[&](std::uint32_t){return stored;};
        visualization.VisualizationRecipes.SetRecipe=[&](std::uint32_t,R::VisualizationRecipe value){stored=std::move(value);};
        visualization.VisualizationRecipes.ClearRecipe=[&](std::uint32_t){stored.reset();};
        const auto displayStatus = R::ApplyEditorVisualizationRecipeCommand(
            visualization, {.StableEntityId=c.StableEntityId,
                .Recipe={.Data=R::ScalarVisualizationRecipe{.Source=c.Outputs[32],.OutputName="histogram_colors"}}});
        EXPECT_FALSE(stored);
        if (half)
        {
            EXPECT_EQ(displayStatus, R::EditorCommandStatus::InvalidVisualizationProperty);
            continue;
        }
        EXPECT_EQ(displayStatus, R::EditorCommandStatus::Applied);
        const auto* overrides = scene.Raw().try_get<
            Extrinsic::Graphics::Components::VisualizationLaneOverrides>(entity);
        ASSERT_NE(overrides, nullptr);
        const auto& lane = D(d) == D::MeshVertex || D(d) == D::MeshFace
            ? overrides->Surface : D(d) == D::PointCloudPoint ? overrides->Points : overrides->Edges;
        ASSERT_TRUE(lane);
        EXPECT_EQ(lane->Source,
                  Extrinsic::Graphics::Components::VisualizationConfig::ColorSource::ScalarField);
        EXPECT_EQ(lane->ScalarFieldName, c.Outputs[32].Name);
    }
}
TEST(DescriptorAnalysisOperations, JobsRejectChangedNormalsEveryOutputAndCancellation)
{
    for(unsigned change=0;change<7;++change)
    {
        SCOPED_TRACE(change);Extrinsic::ECS::Scene::Registry scene;auto entity=Make(scene,D::MeshVertex);auto c=Config(entity,D::MeshVertex);
        auto& props=Properties(scene,entity,D::MeshVertex);
        Intrinsic::Tests::EditorFeatureTestContext context;context.Scene=&scene;R::EditorCommandHistory history;context.CommandHistory=&history;
        std::optional<R::EditorDescriptorAnalysisResult> delivered;context.MethodResultSinks.DescriptorAnalysis=[&](auto r){delivered=std::move(r);};
        Extrinsic::Tests::EditorJobHarness jobs;jobs.Attach(context);
        ASSERT_EQ(R::ApplyEditorDescriptorAnalysisCommand(context,c).Status,R::EditorCommandStatus::Pending);
        switch(change){case 0:props.Get<float>("keep")[0]=99;break;case 1:props.Get<glm::vec3>("samples")[0].x+=1;break;
            case 2:props.GetOrAdd<bool>("v:deleted")[0]=true;break;case 3:props.GetOrAdd<float>(c.Outputs[0].Name)[0]=77;break;
            case 4:(void)jobs.Jobs().Cancel(jobs.Snapshot().Entries[0].Token);break;
            case 5:props.Get<glm::vec3>("directions")[0].x+=1;break;case 6:props.GetOrAdd<float>(c.Outputs[32].Name)[0]=77;break;}
        ASSERT_TRUE(jobs.DrainUntilTerminal());ASSERT_TRUE(delivered);EXPECT_EQ(delivered->Succeeded(),change==0)<<delivered->Message;
        EXPECT_EQ(history.CanUndo(),change==0);
        for(unsigned b=0;b<33;++b)EXPECT_EQ(props.Exists(c.Outputs[b].Name),change==0 || (change==3 && b==0) || (change==6 && b==32));
    }
}
TEST(DescriptorAnalysisOperations, InvalidNormalsScaleAndOutputPreflightRetainData)
{
    Extrinsic::ECS::Scene::Registry scene;auto entity=Make(scene,D::MeshVertex);auto c=Config(entity,D::MeshVertex);
    auto& props=Properties(scene,entity,D::MeshVertex);R::EditorGeometryProcessingContext context{.Scene=&scene};
    for(auto name:{"samples","directions","v:deleted","h:connectivity"})
    {auto bad=c;bad.Outputs[32].Name=name;EXPECT_FALSE(R::PreviewEditorDescriptorAnalysisCommand(context,bad).Ready);}
    auto gpu=c;gpu.Backend=R::DescriptorAnalysisBackend::VulkanLBVH;EXPECT_FALSE(R::PreviewEditorDescriptorAnalysisCommand(context,gpu).Ready);
    props.GetOrAdd<float>(c.Outputs[0].Name).Vector().assign(props.Size(),77);
    props.Get<glm::vec3>("directions")[0]={0,0,0};EXPECT_FALSE(R::PreviewEditorDescriptorAnalysisCommand(context,c).Ready);
    EXPECT_FALSE(R::ApplyEditorDescriptorAnalysisCommand(context,c).Succeeded());
    props.Get<glm::vec3>("directions")[0]={0,0,1};props.Get<glm::vec3>("samples").Vector().assign(props.Size(),glm::vec3(0));
    EXPECT_FALSE(R::ApplyEditorDescriptorAnalysisCommand(context,c).Succeeded());
    EXPECT_EQ(std::as_const(props).Get<float>(c.Outputs[0].Name).Vector(),std::vector<float>(props.Size(),77));
    for(unsigned b=1;b<33;++b)EXPECT_FALSE(props.Exists(c.Outputs[b].Name));
}
