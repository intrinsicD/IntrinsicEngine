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
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Component.DirtyTags;
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
            R::EditorProcessingContext context{.Scene=&scene,.World=world,.CommandHistory=&history,.SpatialIndices=&cache};
            const auto catalog=R::GetEditorPointInputCatalog(R::BindEditorProcessingCommands(context),config.StableEntityId);
            EXPECT_TRUE(std::ranges::any_of(catalog.Entries,[&](auto& e){return e.Ref==config.Positions;}));
            ASSERT_TRUE(R::PreviewEditorOutlierAnalysisCommand(R::BindEditorProcessingCommands(context),config).Enabled);
            EXPECT_FALSE(props.Exists("outliers"));
            const auto reference=R::ApplyEditorOutlierAnalysisCommand(R::BindEditorProcessingCommands(context),config);
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
            const auto indexed=R::ApplyEditorOutlierAnalysisCommand(R::BindEditorProcessingCommands(context),config);
            ASSERT_TRUE(indexed.Succeeded())<<indexed.Message;EXPECT_EQ(indexed.ActualBackend,"cpu_lbvh");
            EXPECT_EQ(std::as_const(props).Get<std::uint32_t>("outliers").Vector(),mask);
            const auto actual=std::as_const(props).Get<float>("scores");
            for(std::size_t i=0;i<size;++i)EXPECT_NEAR(actual[i],scores[i],1e-5);
            EXPECT_TRUE(R::ApplyEditorOutlierAnalysisCommand(R::BindEditorProcessingCommands(context),config).IndexReused);
            if(D(d)==D::PointCloudPoint) EXPECT_EQ(mask[4],0);
            else {config.Operation=R::OutlierAnalysisOperation::RemoveMarked;EXPECT_FALSE(R::PreviewEditorOutlierAnalysisCommand(R::BindEditorProcessingCommands(context),config).Enabled);}
        }
}

TEST(OutlierAnalysis, RemovalRequiresCurrentDetectionAndPreservesEveryPropertyThroughHistory)
{
    Extrinsic::ECS::Scene::Registry scene;auto entity=Make(scene,D::PointCloudPoint);
    auto& props=Properties(scene,entity,D::PointCloudPoint);auto config=Config(entity,D::PointCloudPoint);
    auto custom=props.GetOrAdd<std::string>("custom", "default");
    for(std::size_t i=0;i<props.Size();++i)custom[i]=std::to_string(i);
    R::EditorCommandHistory history;R::SelectionController selection;
    R::EditorProcessingContext context = [&] { R::EditorProcessingContext value{}; value.Scene = &scene; value.Selection = &selection; value.CommandHistory = &history; return value; }();
    auto remove=config;remove.Operation=R::OutlierAnalysisOperation::RemoveMarked;
    EXPECT_FALSE(R::PreviewEditorOutlierAnalysisCommand(R::BindEditorProcessingCommands(context),remove).Enabled);
    ASSERT_TRUE(R::ApplyEditorOutlierAnalysisCommand(R::BindEditorProcessingCommands(context),config).Succeeded());
    EXPECT_TRUE(R::PreviewEditorOutlierAnalysisCommand(R::BindEditorProcessingCommands(context),remove).Enabled);
    props.Get<float>("keep")[1]=17; // unrelated attribute edits do not invalidate detection.
    ASSERT_FALSE(selection.EditPrimitives(scene, config.StableEntityId, D::PointCloudPoint,
        R::PrimitiveSelectionEdit::All).Indices.empty());
    const auto result=R::ApplyEditorOutlierAnalysisCommand(R::BindEditorProcessingCommands(context),remove);
    ASSERT_TRUE(result.Succeeded())<<result.Message;EXPECT_EQ(result.RejectedCount,1);EXPECT_EQ(props.Size(),4);
    EXPECT_EQ(selection.ReadPrimitives(scene, config.StableEntityId, D::PointCloudPoint).Status,
              R::PrimitiveSelectionStatus::Empty);
    EXPECT_EQ(std::as_const(props).Get<std::string>("custom")[3],"4"); // deleted row preserved
    EXPECT_EQ(std::as_const(props).Get<float>("keep")[1],17);
    ASSERT_TRUE(history.Undo().Succeeded());EXPECT_EQ(props.Size(),5);EXPECT_EQ(std::as_const(props).Get<std::string>("custom")[3],"3");
    EXPECT_TRUE(R::PreviewEditorOutlierAnalysisCommand(R::BindEditorProcessingCommands(context),remove).Enabled);
    ASSERT_TRUE(history.Redo().Succeeded());EXPECT_EQ(props.Size(),4);
    props.Get<float>("keep")[0]=123;EXPECT_FALSE(history.Undo().Succeeded());
}

TEST(OutlierAnalysis, DeletedRowsRemainUntouchedAndMaskOrInputEditsInvalidateRemoval)
{
    Extrinsic::ECS::Scene::Registry scene;auto entity=Make(scene,D::PointCloudPoint);auto& props=Properties(scene,entity,D::PointCloudPoint);
    auto config=Config(entity,D::PointCloudPoint),remove=config;remove.Operation=R::OutlierAnalysisOperation::RemoveMarked;
    props.GetOrAdd<std::uint32_t>("outliers")[4]=77;props.GetOrAdd<float>("scores")[4]=std::numeric_limits<float>::quiet_NaN();
    R::EditorCommandHistory history;R::EditorProcessingContext context{.Scene=&scene,.CommandHistory=&history};
    ASSERT_TRUE(R::ApplyEditorOutlierAnalysisCommand(R::BindEditorProcessingCommands(context),config).Succeeded());
    EXPECT_EQ(std::as_const(props).Get<std::uint32_t>("outliers")[4],77);EXPECT_TRUE(std::isnan(std::as_const(props).Get<float>("scores")[4]));
    props.Get<std::uint32_t>("outliers")[0]=1;
    EXPECT_FALSE(R::ApplyEditorOutlierAnalysisCommand(R::BindEditorProcessingCommands(context),remove).Succeeded());EXPECT_FALSE(history.Undo().Succeeded());
    ASSERT_TRUE(R::ApplyEditorOutlierAnalysisCommand(R::BindEditorProcessingCommands(context),config).Succeeded());
    props.Get<glm::vec3>("samples")[0].x+=1;
    EXPECT_FALSE(R::PreviewEditorOutlierAnalysisCommand(R::BindEditorProcessingCommands(context),remove).Enabled);
    ASSERT_TRUE(R::ApplyEditorOutlierAnalysisCommand(R::BindEditorProcessingCommands(context),config).Succeeded());
    props.Get<bool>("v:deleted")[1]=true;EXPECT_FALSE(R::PreviewEditorOutlierAnalysisCommand(R::BindEditorProcessingCommands(context),remove).Enabled);
}

TEST(OutlierAnalysis, RadiusUsesAllCountsAndRetainsCoincidentPeers)
{
    R::WorldRegistry worlds;auto world=worlds.CreateWorld("dense");auto& scene=*worlds.Get(world);
    auto entity=scene.Create();auto& props=scene.Raw().emplace<GS::Vertices>(entity).Properties;props.Resize(1030);
    (void)props.GetOrAdd<glm::vec3>("samples",glm::vec3(0));props.Get<glm::vec3>("samples")[1029]={10,0,0};
    R::SpatialIndexCache cache(worlds);R::EditorProcessingContext context{.Scene=&scene,.World=world,.SpatialIndices=&cache};
    auto config=Config(entity,D::PointCloudPoint);config.Method=R::OutlierAnalysisMethod::Radius;config.MinimumNeighbors=1028;
    for(auto backend:{R::OutlierAnalysisBackend::CpuOctree,R::OutlierAnalysisBackend::CpuLBVH})
    {
        config.Backend=backend;const auto result=R::ApplyEditorOutlierAnalysisCommand(R::BindEditorProcessingCommands(context),config);
        ASSERT_TRUE(result.Succeeded())<<result.Message;EXPECT_EQ(result.RejectedCount,1);
        EXPECT_EQ(std::as_const(props).Get<float>("scores")[0],1028.f);
        EXPECT_EQ(std::as_const(props).Get<float>("scores")[1029],0.f);
    }
    props.Resize(1);config.MinimumNeighbors=0;
    EXPECT_TRUE(R::ApplyEditorOutlierAnalysisCommand(R::BindEditorProcessingCommands(context),config).Succeeded());
    EXPECT_EQ(std::as_const(props).Get<float>("scores")[0],0);
}

TEST(OutlierAnalysis, InvalidParametersAndReservedOutputsFailBeforeMutation)
{
    Extrinsic::ECS::Scene::Registry scene;auto entity=Make(scene,D::MeshFace);auto config=Config(entity,D::MeshFace);
    R::EditorProcessingContext context{.Scene=&scene};
    for(auto name:{"f:halfedge","f:deleted","f:connectivity"})
    {auto c=config;c.Mask.Name=name;EXPECT_FALSE(R::PreviewEditorOutlierAnalysisCommand(R::BindEditorProcessingCommands(context),c).Enabled);}
    for(auto name:{"v:deleted","e:v0"})
    {auto c=config;c.Mask.Name=name;EXPECT_TRUE(R::PreviewEditorOutlierAnalysisCommand(R::BindEditorProcessingCommands(context),c).Enabled);}
    auto c=config;c.Backend=R::OutlierAnalysisBackend::VulkanLBVH;
    EXPECT_FALSE(R::ApplyEditorOutlierAnalysisCommand(R::BindEditorProcessingCommands(context),c).Succeeded());
    auto& props=Properties(scene,entity,D::MeshFace);(void)props.GetOrAdd<float>("outliers");
    EXPECT_FALSE(R::PreviewEditorOutlierAnalysisCommand(R::BindEditorProcessingCommands(context),config).Enabled);
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
        std::function<void(R::EditorOutlierAnalysisResult)> sink=[&](auto r){delivered=std::move(r);};
        Extrinsic::Tests::EditorJobHarness jobs;jobs.Attach(context);
        ASSERT_EQ(R::ApplyEditorOutlierAnalysisCommand(R::BindEditorProcessingCommands(context),config, sink).Status,R::EditorCommandStatus::Pending);
        unsigned duplicateDeliveries = 0;
        EXPECT_EQ(R::ApplyEditorOutlierAnalysisCommand(R::BindEditorProcessingCommands(context), config,
            [&](R::EditorOutlierAnalysisResult) { ++duplicateDeliveries; }).Status, R::EditorCommandStatus::Pending);
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
        EXPECT_EQ(duplicateDeliveries, 0u);
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
        R::EditorProcessingContext context{.Scene = &scene};
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
        auto commands = R::BindEditorProcessingCommands(context);
        ASSERT_TRUE(R::PreviewEditorOutlierAnalysisCommand(commands, config).Enabled);
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
        ASSERT_TRUE(R::ApplyEditorOutlierAnalysisCommand(R::BindEditorProcessingCommands(context),config).Succeeded());
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
    R::SpatialIndexCache cache(worlds);R::EditorProcessingContext context{.Scene=&scene,.World=world,.SpatialIndices=&cache};
    ASSERT_TRUE(R::ApplyEditorOutlierAnalysisCommand(R::BindEditorProcessingCommands(context),config).Succeeded());
    const auto scores=std::as_const(props).Get<float>("scores").Vector();EXPECT_EQ(scores[0],2);
    config.Backend=R::OutlierAnalysisBackend::CpuLBVH;
    ASSERT_TRUE(R::ApplyEditorOutlierAnalysisCommand(R::BindEditorProcessingCommands(context),config).Succeeded());
    EXPECT_EQ(std::as_const(props).Get<float>("scores").Vector(),scores);
    config.Method=R::OutlierAnalysisMethod::Statistical;config.KNeighbors=5;
    EXPECT_FALSE(R::PreviewEditorOutlierAnalysisCommand(R::BindEditorProcessingCommands(context),config).Enabled);
    config.Backend=R::OutlierAnalysisBackend::CpuOctree;
    EXPECT_FALSE(R::PreviewEditorOutlierAnalysisCommand(R::BindEditorProcessingCommands(context),config).Enabled);
}
TEST(OutlierAnalysis, UndoAnotherOutputCannotMakeAnEditedMaskCurrent)
{
    Extrinsic::ECS::Scene::Registry scene;auto entity=Make(scene,D::PointCloudPoint);auto& props=Properties(scene,entity,D::PointCloudPoint);
    auto first=Config(entity,D::PointCloudPoint), second=first;
    second.Mask.Name="other_mask";second.Score.Name="other_score";
    R::EditorCommandHistory history;R::EditorProcessingContext context{.Scene=&scene,.CommandHistory=&history};
    ASSERT_TRUE(R::ApplyEditorOutlierAnalysisCommand(R::BindEditorProcessingCommands(context),first).Succeeded());
    ASSERT_TRUE(R::ApplyEditorOutlierAnalysisCommand(R::BindEditorProcessingCommands(context),second).Succeeded());
    props.Get<std::uint32_t>(first.Mask.Name)[0]=1;
    ASSERT_TRUE(history.Undo().Succeeded());
    first.Operation=R::OutlierAnalysisOperation::RemoveMarked;
    EXPECT_FALSE(R::PreviewEditorOutlierAnalysisCommand(R::BindEditorProcessingCommands(context),first).Enabled);
    EXPECT_FALSE(R::ApplyEditorOutlierAnalysisCommand(R::BindEditorProcessingCommands(context),first).Succeeded());
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
    for (const auto sampleCount : {5u, 70u})
    {
        SCOPED_TRACE(sampleCount);
        R::WorldRegistry worlds;auto world=worlds.CreateWorld("distance ratio");auto& scene=*worlds.Get(world);
        R::SpatialIndexCache cache(worlds);auto entity=Make(scene,D::PointCloudPoint);
        auto& props = Properties(scene, entity, D::PointCloudPoint);
        props.Resize(sampleCount);
        auto samples = props.Get<glm::vec3>("samples");
        for (std::size_t i = 0; i < samples.Size(); ++i)
            samples[i] = {float(i * i) * 0.01f, 0, 0};
        samples[1] = samples[0];
        props.Get<bool>("v:deleted").Vector().assign(sampleCount, false);
        props.Get<bool>("v:deleted")[2] = true;
        samples[2] = {std::numeric_limits<float>::quiet_NaN(), 0, 0};
        auto c=Config(entity,D::PointCloudPoint);c.Method=config.Method;
        R::EditorProcessingContext context{.Scene=&scene,.World=world,.SpatialIndices=&cache};
        const auto commands = R::BindEditorProcessingCommands(context);
        for (const auto k : {1u, 2u, 63u})
        {
            SCOPED_TRACE(k);
            c.KNeighbors = k;
            c.Backend = R::OutlierAnalysisBackend::CpuOctree;
            ASSERT_TRUE(R::ApplyEditorOutlierAnalysisCommand(commands, c).Succeeded());
            const auto expected = std::as_const(Properties(scene, entity, D::PointCloudPoint))
                                      .Get<float>("scores").Vector();
            c.Backend = R::OutlierAnalysisBackend::CpuLBVH;
            EXPECT_TRUE(R::PreviewEditorOutlierAnalysisCommand(commands, c).Enabled);
            const auto result = R::ApplyEditorOutlierAnalysisCommand(commands, c);
            ASSERT_TRUE(result.Succeeded()) << result.Message;
            const auto actual = std::as_const(Properties(scene, entity, D::PointCloudPoint)).Get<float>("scores");
            ASSERT_EQ(actual.Vector().size(), expected.size());
            for (std::size_t i = 0; i < expected.size(); ++i) EXPECT_NEAR(actual[i], expected[i], 1e-5f);
        }
    }
}

TEST(OutlierAnalysisOperations, ExpiredQueuedCommandsNeverBorrowFreedSceneOrDeliver)
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
    ASSERT_EQ(R::ApplyEditorOutlierAnalysisCommand(commands, config,
        [&](R::EditorOutlierAnalysisResult) { ++deliveries; }).Status, R::EditorCommandStatus::Pending);
    active = false;
    scene.reset();
    ASSERT_TRUE(jobs.DrainUntilTerminal());
    EXPECT_EQ(deliveries, 0u);
    EXPECT_FALSE(history.CanUndo());
    EXPECT_FALSE(commands.IsBound());
    EXPECT_TRUE(R::GetEditorPointInputCatalog(commands, config.StableEntityId).Entries.empty());
}

TEST(OutlierAnalysis, RemovalHistoryRejectsExpiredScene)
{
    auto scene = std::make_unique<Extrinsic::ECS::Scene::Registry>();
    const auto entity = Make(*scene, D::PointCloudPoint);
    auto config = Config(entity, D::PointCloudPoint);
    bool active = true;
    R::EditorCommandHistory history;
    const auto commands = R::BindEditorProcessingCommands({
        .Scene = scene.get(), .CommandHistory = &history, .AttachmentActive = [&] { return active; }});
    ASSERT_TRUE(R::ApplyEditorOutlierAnalysisCommand(commands, config).Succeeded());
    config.Operation = R::OutlierAnalysisOperation::RemoveMarked;
    const auto removal = R::ApplyEditorOutlierAnalysisCommand(commands, config);
    ASSERT_EQ(removal.Status, R::EditorCommandStatus::Applied) << removal.Message;
    ASSERT_TRUE(history.CanUndo());
    active = false;
    scene.reset();
    EXPECT_EQ(history.Undo().Status, R::EditorCommandHistoryStatus::StaleEntity);
}

TEST(OutlierAnalysis, SharedPointCatalogAdmitsOnlyUsableLiveVec3Inputs)
{
    for (unsigned scenario = 0; scenario < 5; ++scenario)
    {
        SCOPED_TRACE(scenario);
        Extrinsic::ECS::Scene::Registry scene;
        const auto entity = Make(scene, D::PointCloudPoint);
        const auto config = Config(entity, D::PointCloudPoint);
        auto& props = Properties(scene, entity, D::PointCloudPoint);
        // The fixture includes a deleted NaN row; only live rows determine eligibility.
        if (scenario == 1) props.Get<bool>("v:deleted").Vector().assign(props.Size(), true);
        if (scenario == 2) props.Get<glm::vec3>("samples")[0].x = std::numeric_limits<float>::quiet_NaN();
        if (scenario == 3)
        {
            auto deleted = props.Get<bool>("v:deleted");
            props.Remove(deleted);
            (void)props.GetOrAdd<float>("v:deleted");
        }
        if (scenario == 4) props.Get<bool>("v:deleted").Vector().resize(1);
        const auto commands = R::BindEditorProcessingCommands({.Scene=&scene});
        // Outlier, keypoint and density-weight inputs share one catalog owner, so
        // this single snapshot is what every one of those panels resolves.
        const auto catalog = R::GetEditorPointInputCatalog(commands, config.StableEntityId);
        EXPECT_EQ(std::ranges::any_of(catalog.Entries, [&](const auto& e) { return e.Ref == config.Positions; }), scenario == 0);
        EXPECT_TRUE(std::ranges::all_of(catalog.Entries, [](const auto& e) { return e.Ref.ValueKind == Geometry::PropertyValueKind::Vec3; }));
        EXPECT_FALSE(std::ranges::any_of(catalog.Entries, [](const auto& e) { return e.Ref.Name == "keep"; }));
    }
}

namespace
{
    namespace Dirty = Extrinsic::ECS::Components::DirtyTags;

    // A tight grid of live points plus, optionally, one trailing slot that is
    // already marked deleted. The dead slot sits far away, so it would look like
    // an outlier if removal ever treated it as live.
    entt::entity MakeRemovalCloud(Extrinsic::ECS::Scene::Registry& scene,
                                  const std::size_t liveCount,
                                  const bool withDeletedSlot,
                                  const glm::vec3 outlier = glm::vec3{12, -7, 4})
    {
        const auto entity = scene.Create();
        auto& vertices = scene.Raw().emplace<GS::Vertices>(entity);
        const std::size_t slots = liveCount + (withDeletedSlot ? 1u : 0u);
        vertices.Properties.Resize(slots);
        auto samples = vertices.Properties.GetOrAdd<glm::vec3>("samples");
        for (std::size_t i = 0; i < slots; ++i)
            samples[i] = {float(i % 8) * 0.05f, float(i / 8) * 0.05f, 0};
        samples[liveCount - 1u] = outlier;
        auto labels = vertices.Properties.GetOrAdd<float>("keep", 0.f);
        for (std::size_t i = 0; i < slots; ++i) labels[i] = float(i);
        if (withDeletedSlot)
        {
            auto deleted = vertices.Properties.GetOrAdd<bool>("v:deleted", false);
            deleted[slots - 1u] = true;
            samples[slots - 1u] = {50, 50, 50};
            vertices.NumDeleted = 1u;
        }
        return entity;
    }
    R::OutlierAnalysisConfig RemovalConfig(entt::entity entity)
    {
        return {.StableEntityId = R::SelectionController::ToStableEntityId(entity),
                .Method = R::OutlierAnalysisMethod::Radius,
                .Positions = {.Domain = D::PointCloudPoint, .Name = "samples",
                              .ValueKind = Geometry::PropertyValueKind::Vec3},
                .Mask = {D::PointCloudPoint, "outliers", Geometry::PropertyValueKind::UInt32},
                .Score = {D::PointCloudPoint, "scores", Geometry::PropertyValueKind::Float},
                .MinimumNeighbors = 1, .Radius = 0.2f};
    }
}

TEST(OutlierAnalysis, RemovalNotifiesRenderersAndKeepsDeletedRowAccountingExact)
{
    Extrinsic::ECS::Scene::Registry scene;
    const auto entity = MakeRemovalCloud(scene, 16u, true);
    auto config = RemovalConfig(entity);
    const auto slotCount = Properties(scene, entity, D::PointCloudPoint).Size();
    R::EditorCommandHistory history;
    const auto commands = R::BindEditorProcessingCommands({.Scene = &scene, .CommandHistory = &history});
    const auto detect = R::ApplyEditorOutlierAnalysisCommand(commands, config);
    ASSERT_EQ(detect.Status, R::EditorCommandStatus::Applied) << detect.Message;
    ASSERT_EQ(detect.RejectedCount, 1u);
    scene.Raw().remove<Dirty::GpuDirty>(entity);
    scene.Raw().remove<Dirty::DirtyVertexPositions>(entity);
    scene.Raw().remove<Dirty::DirtyVertexAttributes>(entity);
    scene.Raw().remove<Dirty::DirtyVertexNormals>(entity);

    config.Operation = R::OutlierAnalysisOperation::RemoveMarked;
    const auto removal = R::ApplyEditorOutlierAnalysisCommand(commands, config);
    ASSERT_EQ(removal.Status, R::EditorCommandStatus::Applied) << removal.Message;
    EXPECT_EQ(removal.RejectedCount, 1u);
    EXPECT_EQ(removal.WrittenCount + removal.RejectedCount, slotCount);

    const auto& vertices = scene.Raw().get<GS::Vertices>(entity);
    EXPECT_EQ(vertices.Properties.Size(), slotCount - removal.RejectedCount);
    // Removing live marked points neither resurrects nor forgets a dead row.
    EXPECT_EQ(vertices.NumDeleted, 1u);
    const auto deleted = std::as_const(vertices.Properties).Get<bool>("v:deleted");
    ASSERT_TRUE(deleted);
    EXPECT_EQ(std::ranges::count(deleted.Vector(), true), 1);
    EXPECT_TRUE(scene.Raw().all_of<Dirty::GpuDirty>(entity));
    EXPECT_TRUE(scene.Raw().all_of<Dirty::DirtyVertexPositions>(entity));
    EXPECT_TRUE(scene.Raw().all_of<Dirty::DirtyVertexAttributes>(entity));
    EXPECT_TRUE(scene.Raw().all_of<Dirty::DirtyVertexNormals>(entity));
    EXPECT_TRUE(history.IsDirty());

    ASSERT_TRUE(history.Undo().Succeeded());
    const auto& restored = scene.Raw().get<GS::Vertices>(entity);
    EXPECT_EQ(restored.Properties.Size(), slotCount);
    EXPECT_EQ(restored.NumDeleted, 1u);
    const auto restoredDeleted = std::as_const(restored.Properties).Get<bool>("v:deleted");
    ASSERT_TRUE(restoredDeleted);
    EXPECT_TRUE(restoredDeleted[slotCount - 1u]);
    ASSERT_TRUE(history.Redo().Succeeded());
    EXPECT_EQ(scene.Raw().get<GS::Vertices>(entity).Properties.Size(), slotCount - removal.RejectedCount);
    EXPECT_EQ(scene.Raw().get<GS::Vertices>(entity).NumDeleted, 1u);
}

TEST(OutlierAnalysis, RemovalWithNothingMarkedReportsNoChangeAndAddsNoUndoEntry)
{
    Extrinsic::ECS::Scene::Registry scene;
    // Every point has a close neighbour, so detection marks nothing.
    const auto entity = MakeRemovalCloud(scene, 16u, false, glm::vec3{0.05f, 0.05f, 0});
    auto config = RemovalConfig(entity);
    config.Radius = 1.0f;
    const auto slotCount = Properties(scene, entity, D::PointCloudPoint).Size();
    R::EditorCommandHistory history;
    const auto commands = R::BindEditorProcessingCommands({.Scene = &scene, .CommandHistory = &history});
    const auto detect = R::ApplyEditorOutlierAnalysisCommand(commands, config);
    ASSERT_TRUE(detect.Succeeded()) << detect.Message;
    ASSERT_EQ(detect.RejectedCount, 0u);
    const auto undoCountAfterDetection = history.UndoCount();

    config.Operation = R::OutlierAnalysisOperation::RemoveMarked;
    const auto removal = R::ApplyEditorOutlierAnalysisCommand(commands, config);
    EXPECT_EQ(removal.Status, R::EditorCommandStatus::NoChange) << removal.Message;
    EXPECT_EQ(removal.RejectedCount, 0u);
    EXPECT_NE(removal.Message.find("No live points are marked"), std::string::npos) << removal.Message;
    EXPECT_EQ(Properties(scene, entity, D::PointCloudPoint).Size(), slotCount);
    EXPECT_EQ(history.UndoCount(), undoCountAfterDetection)
        << "a removal that rejected nothing must not leave an undo entry";
}

TEST(OutlierAnalysis, RadiusParametersAndMissingSceneFailBeforeAnyMutation)
{
    for (const float radius : {0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN(),
                               std::numeric_limits<float>::infinity()})
    {
        SCOPED_TRACE(radius);
        Extrinsic::ECS::Scene::Registry scene;
        const auto entity = MakeRemovalCloud(scene, 16u, false);
        auto config = RemovalConfig(entity);
        config.Radius = radius;
        R::EditorCommandHistory history;
        const auto commands = R::BindEditorProcessingCommands({.Scene = &scene, .CommandHistory = &history});
        const auto result = R::ApplyEditorOutlierAnalysisCommand(commands, config);
        EXPECT_FALSE(result.Succeeded()) << result.Message;
        auto& props = Properties(scene, entity, D::PointCloudPoint);
        EXPECT_FALSE(props.Exists("outliers"));
        EXPECT_FALSE(props.Exists("scores"));
        EXPECT_FALSE(history.CanUndo());
        if (radius <= 0.0f)
        {
            // The serialized config lane rejects the same parameter up front.
            EXPECT_FALSE(R::ValidateOutlierAnalysisConfigSection(
                R::SerializeOutlierAnalysisConfig(config), {},
                R::kOutlierAnalysisConfigSectionName).Usable());
        }
    }

    Extrinsic::ECS::Scene::Registry scene;
    const auto entity = MakeRemovalCloud(scene, 16u, false);
    const auto config = RemovalConfig(entity);
    const auto unbound = R::BindEditorProcessingCommands({});
    const auto missingScene = R::ApplyEditorOutlierAnalysisCommand(unbound, config);
    EXPECT_FALSE(missingScene.Succeeded());
    EXPECT_FALSE(R::PreviewEditorOutlierAnalysisCommand(unbound, config).Enabled);
    EXPECT_FALSE(Properties(scene, entity, D::PointCloudPoint).Exists("outliers"));
}
