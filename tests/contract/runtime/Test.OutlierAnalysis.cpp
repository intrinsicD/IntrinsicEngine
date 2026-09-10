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
            samples[i] = {float(i)*0.01f,0,0};
        samples[domain == D::PointCloudPoint ? 3 : samples.Size()-1] = {10,0,0};
        auto keep = props.GetOrAdd<float>("keep");
        std::ranges::fill(keep.Vector(), 42.f);
        if (domain == D::PointCloudPoint)
        {
            props.GetOrAdd<bool>("v:deleted")[4] = true;
            samples[4] = {std::numeric_limits<float>::quiet_NaN(), 0, 0};
        }
        return entity;
    }
    R::OutlierAnalysisConfig Config(entt::entity entity, D domain)
    {
        return {.StableEntityId = R::SelectionController::ToStableEntityId(entity),
                .Positions = {.Domain = domain,
                              .Name = "samples",
                              .ValueKind = Geometry::PropertyValueKind::Vec3},
                .Mask = {domain, "outliers", Geometry::PropertyValueKind::UInt32},
                .Score = {domain, "scores", Geometry::PropertyValueKind::Float},
                .KNeighbors = 2, .MinimumNeighbors = 1, .Radius = 0.3f};
    }
} // namespace

TEST(OutlierAnalysis, EveryDomainMatchesReferenceAndPublishesOnlyNamedProperties)
{
    for(unsigned d=1;d<=8;++d)
        for(auto method : {R::OutlierAnalysisMethod::Statistical,R::OutlierAnalysisMethod::Radius,R::OutlierAnalysisMethod::LocalDistanceRatio})
        {
            SCOPED_TRACE(std::to_string(d)+R::ToString(method));
            R::WorldRegistry worlds;auto world=worlds.CreateWorld("outliers");auto& scene=*worlds.Get(world);
            R::SpatialIndexCache cache(worlds);auto entity=Make(scene,D(d));auto config=Config(entity,D(d));config.Method=method;
            auto& props=Properties(scene,entity,D(d));const auto size=props.Size();
            const auto positionRevision=std::as_const(props).Get<glm::vec3>("samples").Revision();
            R::EditorCommandHistory history;
            R::EditorGeometryProcessingContext context{.Scene=&scene,.World=world,.CommandHistory=&history,.SpatialIndices=&cache};
            const auto catalog=R::GetEditorOutlierAnalysisInputCatalog(context,config.StableEntityId);
            EXPECT_TRUE(std::ranges::any_of(catalog.Entries,[&](auto& e){return e.Ref==config.Positions;}));
            ASSERT_TRUE(R::PreviewEditorOutlierAnalysisCommand(context,config).Ready);
            EXPECT_FALSE(props.Exists("outliers"));
            const auto reference=R::ApplyEditorOutlierAnalysisCommand(context,config);
            ASSERT_TRUE(reference.Succeeded())<<reference.Message;
            EXPECT_EQ(reference.ActualBackend,"cpu_octree");EXPECT_EQ(reference.RejectedCount,1);
            const auto mask=std::as_const(props).Get<std::uint32_t>("outliers").Vector();
            const auto scores=std::as_const(props).Get<float>("scores").Vector();
            EXPECT_EQ(props.Size(),size);
            EXPECT_EQ(std::as_const(props).Get<glm::vec3>("samples").Revision(),positionRevision);
            props.Get<float>("keep")[0]=99;
            ASSERT_TRUE(history.Undo().Succeeded());EXPECT_FALSE(props.Exists("outliers"));EXPECT_FALSE(props.Exists("scores"));
            ASSERT_TRUE(history.Redo().Succeeded());EXPECT_EQ(std::as_const(props).Get<float>("keep")[0],99);
            config.Backend=R::OutlierAnalysisBackend::CpuLBVH;
            const auto indexed=R::ApplyEditorOutlierAnalysisCommand(context,config);
            ASSERT_TRUE(indexed.Succeeded())<<indexed.Message;EXPECT_EQ(indexed.ActualBackend,"cpu_lbvh");
            EXPECT_EQ(std::as_const(props).Get<std::uint32_t>("outliers").Vector(),mask);
            const auto actual=std::as_const(props).Get<float>("scores");
            for(std::size_t i=0;i<size;++i)EXPECT_NEAR(actual[i],scores[i],1e-5);
            EXPECT_TRUE(R::ApplyEditorOutlierAnalysisCommand(context,config).IndexReused);
            if(D(d)==D::PointCloudPoint) EXPECT_EQ(mask[4],0);
            else {config.Operation=R::OutlierAnalysisOperation::RemoveMarked;EXPECT_FALSE(R::PreviewEditorOutlierAnalysisCommand(context,config).Ready);}
        }
}

TEST(OutlierAnalysis, RemovalRequiresCurrentDetectionAndPreservesEveryPropertyThroughHistory)
{
    Extrinsic::ECS::Scene::Registry scene;auto entity=Make(scene,D::PointCloudPoint);
    auto& props=Properties(scene,entity,D::PointCloudPoint);auto config=Config(entity,D::PointCloudPoint);
    auto custom=props.GetOrAdd<std::string>("custom", "default");
    for(std::size_t i=0;i<props.Size();++i)custom[i]=std::to_string(i);
    R::EditorCommandHistory history;R::SelectionController selection;
    R::EditorGeometryProcessingContext context{.Scene=&scene,.Selection=&selection,.CommandHistory=&history};
    auto remove=config;remove.Operation=R::OutlierAnalysisOperation::RemoveMarked;
    EXPECT_FALSE(R::PreviewEditorOutlierAnalysisCommand(context,remove).Ready);
    ASSERT_TRUE(R::ApplyEditorOutlierAnalysisCommand(context,config).Succeeded());
    EXPECT_TRUE(R::PreviewEditorOutlierAnalysisCommand(context,remove).Ready);
    props.Get<float>("keep")[1]=17; // unrelated attribute edits do not invalidate detection.
    const auto result=R::ApplyEditorOutlierAnalysisCommand(context,remove);
    ASSERT_TRUE(result.Succeeded())<<result.Message;EXPECT_EQ(result.RejectedCount,1);EXPECT_EQ(props.Size(),4);
    EXPECT_EQ(std::as_const(props).Get<std::string>("custom")[3],"4"); // deleted row preserved
    EXPECT_EQ(std::as_const(props).Get<float>("keep")[1],17);
    ASSERT_TRUE(history.Undo().Succeeded());EXPECT_EQ(props.Size(),5);EXPECT_EQ(std::as_const(props).Get<std::string>("custom")[3],"3");
    EXPECT_TRUE(R::PreviewEditorOutlierAnalysisCommand(context,remove).Ready);
    ASSERT_TRUE(history.Redo().Succeeded());EXPECT_EQ(props.Size(),4);
    props.Get<float>("keep")[0]=123;EXPECT_FALSE(history.Undo().Succeeded());
}

TEST(OutlierAnalysis, DeletedRowsRemainUntouchedAndMaskOrInputEditsInvalidateRemoval)
{
    Extrinsic::ECS::Scene::Registry scene;auto entity=Make(scene,D::PointCloudPoint);auto& props=Properties(scene,entity,D::PointCloudPoint);
    auto config=Config(entity,D::PointCloudPoint),remove=config;remove.Operation=R::OutlierAnalysisOperation::RemoveMarked;
    props.GetOrAdd<std::uint32_t>("outliers")[4]=77;props.GetOrAdd<float>("scores")[4]=std::numeric_limits<float>::quiet_NaN();
    R::EditorCommandHistory history;R::EditorGeometryProcessingContext context{.Scene=&scene,.CommandHistory=&history};
    ASSERT_TRUE(R::ApplyEditorOutlierAnalysisCommand(context,config).Succeeded());
    EXPECT_EQ(std::as_const(props).Get<std::uint32_t>("outliers")[4],77);EXPECT_TRUE(std::isnan(std::as_const(props).Get<float>("scores")[4]));
    props.Get<std::uint32_t>("outliers")[0]=1;
    EXPECT_FALSE(R::ApplyEditorOutlierAnalysisCommand(context,remove).Succeeded());EXPECT_FALSE(history.Undo().Succeeded());
    ASSERT_TRUE(R::ApplyEditorOutlierAnalysisCommand(context,config).Succeeded());
    props.Get<glm::vec3>("samples")[0].x+=1;
    EXPECT_FALSE(R::PreviewEditorOutlierAnalysisCommand(context,remove).Ready);
    ASSERT_TRUE(R::ApplyEditorOutlierAnalysisCommand(context,config).Succeeded());
    props.Get<bool>("v:deleted")[1]=true;EXPECT_FALSE(R::PreviewEditorOutlierAnalysisCommand(context,remove).Ready);
}

TEST(OutlierAnalysis, RadiusUsesAllCountsAndRetainsCoincidentPeers)
{
    R::WorldRegistry worlds;auto world=worlds.CreateWorld("dense");auto& scene=*worlds.Get(world);
    auto entity=scene.Create();auto& props=scene.Raw().emplace<GS::Vertices>(entity).Properties;props.Resize(1030);
    (void)props.GetOrAdd<glm::vec3>("samples",glm::vec3(0));props.Get<glm::vec3>("samples")[1029]={10,0,0};
    R::SpatialIndexCache cache(worlds);R::EditorGeometryProcessingContext context{.Scene=&scene,.World=world,.SpatialIndices=&cache};
    auto config=Config(entity,D::PointCloudPoint);config.Method=R::OutlierAnalysisMethod::Radius;config.MinimumNeighbors=1028;
    for(auto backend:{R::OutlierAnalysisBackend::CpuOctree,R::OutlierAnalysisBackend::CpuLBVH})
    {
        config.Backend=backend;const auto result=R::ApplyEditorOutlierAnalysisCommand(context,config);
        ASSERT_TRUE(result.Succeeded())<<result.Message;EXPECT_EQ(result.RejectedCount,1);
        EXPECT_EQ(std::as_const(props).Get<float>("scores")[0],1028.f);
        EXPECT_EQ(std::as_const(props).Get<float>("scores")[1029],0.f);
    }
    props.Resize(1);config.MinimumNeighbors=0;
    EXPECT_TRUE(R::ApplyEditorOutlierAnalysisCommand(context,config).Succeeded());
    EXPECT_EQ(std::as_const(props).Get<float>("scores")[0],0);
}

TEST(OutlierAnalysis, InvalidParametersAndReservedOutputsFailBeforeMutation)
{
    Extrinsic::ECS::Scene::Registry scene;auto entity=Make(scene,D::MeshFace);auto config=Config(entity,D::MeshFace);
    R::EditorGeometryProcessingContext context{.Scene=&scene};
    for(auto name:{"f:halfedge","v:deleted","e:v0"})
    {auto c=config;c.Mask.Name=name;EXPECT_FALSE(R::PreviewEditorOutlierAnalysisCommand(context,c).Ready);}
    auto c=config;c.Backend=R::OutlierAnalysisBackend::VulkanLBVH;
    EXPECT_FALSE(R::ApplyEditorOutlierAnalysisCommand(context,c).Succeeded());
    auto& props=Properties(scene,entity,D::MeshFace);(void)props.GetOrAdd<float>("outliers");
    EXPECT_FALSE(R::PreviewEditorOutlierAnalysisCommand(context,config).Ready);
}
TEST(OutlierAnalysis, QueuedJobsRejectStaleInputsOutputsAndCancellation)
{
    for(auto method : {R::OutlierAnalysisMethod::Statistical,R::OutlierAnalysisMethod::LocalDistanceRatio})
    for(unsigned change=0;change<6;++change)
    {
        SCOPED_TRACE(change);
        Extrinsic::ECS::Scene::Registry scene;auto entity=Make(scene,D::MeshVertex);auto config=Config(entity,D::MeshVertex);config.Method=method;
        auto& props=Properties(scene,entity,D::MeshVertex);
        Intrinsic::Tests::EditorFeatureTestContext context;context.Scene=&scene;
        R::EditorCommandHistory history;context.CommandHistory=&history;
        std::optional<R::EditorOutlierAnalysisResult> delivered;
        context.MethodResultSinks.OutlierAnalysis=[&](auto r){delivered=std::move(r);};
        Extrinsic::Tests::EditorJobHarness jobs;jobs.Attach(context);
        ASSERT_EQ(R::ApplyEditorOutlierAnalysisCommand(context,config).Status,R::EditorCommandStatus::Pending);
        EXPECT_EQ(R::ApplyEditorOutlierAnalysisCommand(context,config).Status,R::EditorCommandStatus::Pending);
        EXPECT_EQ(jobs.Snapshot().Entries.size(),1);
        switch(change)
        {
        case 0:props.Get<float>("keep")[0]=99;break;
        case 1:props.Get<glm::vec3>("samples")[0].x+=1;break;
        case 2:props.GetOrAdd<bool>("v:deleted")[0]=true;break;
        case 3:props.GetOrAdd<std::uint32_t>("outliers")[0]=77;break;
        case 4:props.GetOrAdd<float>("scores")[0]=77;break;
        case 5:(void)jobs.Jobs().Cancel(jobs.Snapshot().Entries[0].Token);break;
        }
        ASSERT_TRUE(jobs.DrainUntilTerminal());ASSERT_TRUE(delivered);
        EXPECT_EQ(delivered->Succeeded(),change==0)<<delivered->Message;
        EXPECT_EQ(history.CanUndo(),change==0);
        EXPECT_EQ(props.Exists("outliers"),change==0 || change==3);
        EXPECT_EQ(props.Exists("scores"),change==0 || change==4);
    }
}
TEST(OutlierAnalysisConfig, RoundTripAndSharedPreviewApplyRun)
{
    for (auto method : {R::OutlierAnalysisMethod::Radius, R::OutlierAnalysisMethod::LocalDistanceRatio})
    {
        namespace C = Extrinsic::Core::Config;
        Extrinsic::ECS::Scene::Registry scene;
        auto entity = Make(scene, D::MeshFace);
        auto config = Config(entity, D::MeshFace);
        config.Method = method;
        config.ScoreThreshold = 1.25f;
        config.Radius = 5;
        C::EngineConfigSectionRegistry registry;
        ASSERT_TRUE(registry.Register(R::MakeOutlierAnalysisConfigSectionRegistration()));
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
        ASSERT_TRUE(R::PreviewEditorOutlierAnalysisCommand(commands, config).Ready);
        EXPECT_FALSE(Properties(scene, entity, D::MeshFace).Exists("outliers"));
        ASSERT_TRUE(R::ApplyEditorOutlierAnalysisConfig(commands, config).Succeeded());
        ASSERT_TRUE(R::GetEditorOutlierAnalysisConfig(commands));
        EXPECT_EQ(R::SerializeOutlierAnalysisConfig(*R::GetEditorOutlierAnalysisConfig(commands)),
                  R::SerializeOutlierAnalysisConfig(config));
        ASSERT_TRUE(R::ApplyEditorConfiguredOutlierAnalysis(commands).Succeeded());
        EXPECT_EQ(previews, 1);
        EXPECT_EQ(applies, 1);
        for (auto payload : {R"({"method":"automatic"})", R"({"backend":"vulkan"})", R"({"k_neighbors":0})",
                             R"({"minimum_neighbors":-1})", R"({"operation":"delete"})", R"({"gpu_query_batch_size":0})",
                             R"({"method":"radius","radius":0})", R"({"radius":1e100})", R"({"unknown":1})", R"({"score_threshold":-1})", R"({"score_threshold":1e100})",
                             R"({"mask":{"domain":"unknown","name":"outliers","kind":"float"}})"})
            EXPECT_FALSE(R::ValidateOutlierAnalysisConfigSection(payload, {}, "test").Usable()) << payload;
        config.KNeighbors = 0;
        EXPECT_FALSE(R::ApplyEditorOutlierAnalysisConfig(commands, config).Succeeded());
        EXPECT_EQ(applies, 1);
    }
}

TEST(OutlierAnalysis, MaskAndScoreUseSharedVisualizationRecipesOnEveryDomain)
{
    for(unsigned d=1;d<=8;++d)
    {
        Extrinsic::ECS::Scene::Registry scene;auto entity=Make(scene,D(d));auto config=Config(entity,D(d));
        Intrinsic::Tests::EditorFeatureTestContext context;context.Scene=&scene;context.VisualizationCommandsAvailable=true;
        std::optional<R::VisualizationRecipe> stored;
        context.VisualizationRecipes.GetRecipe=[&](std::uint32_t){return stored;};
        context.VisualizationRecipes.SetRecipe=[&](std::uint32_t,R::VisualizationRecipe r){stored=std::move(r);};
        context.VisualizationRecipes.ClearRecipe=[&](std::uint32_t){stored.reset();};
        ASSERT_TRUE(R::ApplyEditorOutlierAnalysisCommand(context,config).Succeeded());
        const bool half = D(d)==D::MeshHalfedge || D(d)==D::GraphHalfedge;
        const auto maskStatus = R::ApplyEditorVisualizationRecipeCommand(context,
            {.StableEntityId=config.StableEntityId,
             .Recipe={.Data=R::LabelVisualizationRecipe{.Source=config.Mask,.OutputName="mask_colors"}}});
        EXPECT_FALSE(stored);
        if (half)
            EXPECT_EQ(maskStatus,R::EditorCommandStatus::InvalidVisualizationProperty);
        else
        {
            EXPECT_EQ(maskStatus,R::EditorCommandStatus::Applied);
            const auto* overrides=scene.Raw().try_get<Extrinsic::Graphics::Components::VisualizationLaneOverrides>(entity);
            ASSERT_NE(overrides,nullptr);
            const auto& lane=D(d)==D::MeshVertex || D(d)==D::MeshFace ? overrides->Surface
                : D(d)==D::PointCloudPoint ? overrides->Points : overrides->Edges;
            ASSERT_TRUE(lane);
            EXPECT_EQ(lane->ColorBufferName,config.Mask.Name);
        }
        const auto shown = R::ApplyEditorVisualizationRecipeCommand(context,
            {.StableEntityId=config.StableEntityId,
             .Recipe={.Data=R::ScalarVisualizationRecipe{.Source=config.Score,.OutputName="score_colors"}}});
        EXPECT_FALSE(stored);
        if (half)
            EXPECT_EQ(shown,R::EditorCommandStatus::InvalidVisualizationProperty);
        else
        {
            EXPECT_EQ(shown,R::EditorCommandStatus::Applied);
            const auto* overrides=scene.Raw().try_get<Extrinsic::Graphics::Components::VisualizationLaneOverrides>(entity);
            ASSERT_NE(overrides,nullptr);
            const auto& lane=D(d)==D::MeshVertex || D(d)==D::MeshFace ? overrides->Surface
                : D(d)==D::PointCloudPoint ? overrides->Points : overrides->Edges;
            ASSERT_TRUE(lane);
            EXPECT_EQ(lane->Source,Extrinsic::Graphics::Components::VisualizationConfig::ColorSource::ScalarField);
            EXPECT_EQ(lane->ScalarFieldName,config.Score.Name);
        }

    }
}
TEST(OutlierAnalysis, RadiusBoundaryAndStatisticalMinimumMatchAcrossCpuBackends)
{
    R::WorldRegistry worlds;auto world=worlds.CreateWorld("boundaries");auto& scene=*worlds.Get(world);
    auto entity=scene.Create();auto& props=scene.Raw().emplace<GS::Vertices>(entity).Properties;props.Resize(5);
    props.GetOrAdd<glm::vec3>("samples").Vector()={{0,0,0},{.3f,.4f,0},{.5f,0,0},{std::nextafter(.5f,1.f),0,0},{10,0,0}};
    auto config=Config(entity,D::PointCloudPoint);config.Method=R::OutlierAnalysisMethod::Radius;config.Radius=.5f;
    R::SpatialIndexCache cache(worlds);R::EditorGeometryProcessingContext context{.Scene=&scene,.World=world,.SpatialIndices=&cache};
    ASSERT_TRUE(R::ApplyEditorOutlierAnalysisCommand(context,config).Succeeded());
    const auto scores=std::as_const(props).Get<float>("scores").Vector();EXPECT_EQ(scores[0],2);
    config.Backend=R::OutlierAnalysisBackend::CpuLBVH;
    ASSERT_TRUE(R::ApplyEditorOutlierAnalysisCommand(context,config).Succeeded());
    EXPECT_EQ(std::as_const(props).Get<float>("scores").Vector(),scores);
    config.Method=R::OutlierAnalysisMethod::Statistical;config.KNeighbors=5;
    EXPECT_FALSE(R::PreviewEditorOutlierAnalysisCommand(context,config).Ready);
    config.Backend=R::OutlierAnalysisBackend::CpuOctree;
    EXPECT_FALSE(R::PreviewEditorOutlierAnalysisCommand(context,config).Ready);
}
TEST(OutlierAnalysis, UndoAnotherOutputCannotMakeAnEditedMaskCurrent)
{
    Extrinsic::ECS::Scene::Registry scene;auto entity=Make(scene,D::PointCloudPoint);auto& props=Properties(scene,entity,D::PointCloudPoint);
    auto first=Config(entity,D::PointCloudPoint), second=first;
    second.Mask.Name="other_mask";second.Score.Name="other_score";
    R::EditorCommandHistory history;R::EditorGeometryProcessingContext context{.Scene=&scene,.CommandHistory=&history};
    ASSERT_TRUE(R::ApplyEditorOutlierAnalysisCommand(context,first).Succeeded());
    ASSERT_TRUE(R::ApplyEditorOutlierAnalysisCommand(context,second).Succeeded());
    props.Get<std::uint32_t>(first.Mask.Name)[0]=1;
    ASSERT_TRUE(history.Undo().Succeeded());
    first.Operation=R::OutlierAnalysisOperation::RemoveMarked;
    EXPECT_FALSE(R::PreviewEditorOutlierAnalysisCommand(context,first).Ready);
    EXPECT_FALSE(R::ApplyEditorOutlierAnalysisCommand(context,first).Succeeded());
    EXPECT_EQ(props.Size(),5);
}

TEST(OutlierAnalysisConfig, DistanceRatioRoundTripsDefaultsAndClampsKToSmallInputs)
{
    namespace C = Extrinsic::Core::Config;
    C::EngineConfig engine;
    R::OutlierAnalysisConfig config;
    config.Method=R::OutlierAnalysisMethod::LocalDistanceRatio;
    config.ScoreThreshold=1.25f;config.KNeighbors=63;
    R::SetOutlierAnalysisConfig(engine,config);
    const auto restored=R::GetOutlierAnalysisConfig(engine);ASSERT_TRUE(restored);
    EXPECT_EQ(restored->Method,config.Method);EXPECT_EQ(restored->ScoreThreshold,1.25f);
    const auto defaults=R::ValidateOutlierAnalysisConfigSection(R"({"method":"local_distance_ratio"})",{},"test");
    EXPECT_TRUE(defaults.Usable());
    R::WorldRegistry worlds;auto world=worlds.CreateWorld("distance ratio");auto& scene=*worlds.Get(world);
    R::SpatialIndexCache cache(worlds);auto entity=Make(scene,D::PointCloudPoint);
    auto c=Config(entity,D::PointCloudPoint);c.Method=config.Method;c.KNeighbors=63;c.Backend=R::OutlierAnalysisBackend::CpuLBVH;
    R::EditorGeometryProcessingContext context{.Scene=&scene,.World=world,.SpatialIndices=&cache};
    EXPECT_TRUE(R::PreviewEditorOutlierAnalysisCommand(context,c).Ready);
    EXPECT_TRUE(R::ApplyEditorOutlierAnalysisCommand(context,c).Succeeded());
}
