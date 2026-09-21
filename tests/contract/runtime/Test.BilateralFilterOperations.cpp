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
#include "PointDomainFixture.hpp"

import Extrinsic.Runtime.PointSetOperations;
import Extrinsic.Runtime.EditorProcessing;
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

namespace R = Extrinsic::Runtime;
namespace GS = Extrinsic::ECS::Components::GeometrySources;
using D = R::GeometryElementDomain;
using Intrinsic::Tests::PointDomainProperties;
namespace
{
    constexpr std::array<glm::vec3, 4> plane{{{0, 0, 0}, {2, 0, 0}, {0, 3, 0}, {2, 3, 0}}};
    entt::entity Make(Extrinsic::ECS::Scene::Registry &scene, D domain)
    {
        auto entity = Intrinsic::Tests::MakePointDomainSource(scene, domain);
        auto &props = PointDomainProperties(scene, entity, domain);
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

TEST(BilateralFilterOperations, InputCatalogPreservesPropertyRevisionsAcrossOtherPropertyEdits)
{
    for (unsigned domain = 1; domain <= unsigned(D::PointCloudPoint); ++domain)
    {
        SCOPED_TRACE(domain);
        Extrinsic::ECS::Scene::Registry scene;
        const auto entity = Make(scene, D(domain));
        auto& props = PointDomainProperties(scene, entity, D(domain));
        const auto config = Config(entity, D(domain));
        R::EditorProcessingContext context{.Scene = &scene};
        const auto commands = R::BindEditorProcessingCommands(context);
        const auto sampleRevision = props.FindPropertyRevision("samples").value();
        const auto before = R::GetEditorBilateralFilterInputCatalog(commands, config.StableEntityId);
        props.Get<float>("keep")[0] = 99.f;
        const auto propertySetRevision = props.Revision();
        const auto after = R::GetEditorBilateralFilterInputCatalog(commands, config.StableEntityId);
        EXPECT_EQ(after.SourceStableId, config.StableEntityId);
        EXPECT_NE(after.SourceGeneration, before.SourceGeneration);
        EXPECT_EQ(props.Revision(), propertySetRevision);
        const auto entry = std::ranges::find_if(after.Entries, [&](const auto& row) {
            return row.Ref == config.Positions;
        });
        ASSERT_NE(entry, after.Entries.end());
        EXPECT_EQ(entry->PropertyGeneration, sampleRevision);
        EXPECT_EQ(entry->ElementCount, props.Size());
    }
}

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
    ASSERT_TRUE(R::PreviewEditorBilateralFilterCommand(commands, config).Enabled);
    EXPECT_FALSE(PointDomainProperties(scene, entity, D::MeshFace).Exists("filtered"));
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

TEST(BilateralFilterOperations, CopiedDiagnosticsDescribeTheLastLiveSamplePass)
{
    R::WorldRegistry worlds;
    const auto world = worlds.CreateWorld("bilateral diagnostics");
    auto& scene = *worlds.Get(world);
    R::SpatialIndexCache cache(worlds);
    const auto entity = Make(scene, D::PointCloudPoint);
    auto& props = PointDomainProperties(scene, entity, D::PointCloudPoint);
    std::ranges::copy(plane, props.Get<glm::vec3>("samples").Vector().begin());
    props.Get<glm::vec3>("directions")[0] = {};
    auto config = Config(entity, D::PointCloudPoint);
    const auto commands = R::BindEditorProcessingCommands(R::EditorProcessingContext{
        .Scene = &scene, .World = world, .SpatialIndices = &cache});
    for (const auto backend : {R::BilateralFilterBackend::CpuOctree, R::BilateralFilterBackend::CpuLBVH})
    {
        SCOPED_TRACE(int(backend));
        config.Backend = backend;
        for (const auto iterations : {0u, 3u})
        {
            SCOPED_TRACE(iterations);
            config.Iterations = iterations;
            const auto result = R::ApplyEditorBilateralFilterCommand(commands, config);
            ASSERT_TRUE(result.Succeeded()) << result.Message;
            EXPECT_EQ(result.SlotCount, 5u);
            EXPECT_EQ(result.LiveCount, 4u);
            EXPECT_EQ(result.CompletedIterations, iterations);
            EXPECT_EQ(result.PointsFiltered, iterations ? 3u : 0u);
            EXPECT_EQ(result.DegenerateNormals, iterations ? 1u : 0u);
            EXPECT_FLOAT_EQ(result.AverageDisplacement, 0.f);
            EXPECT_FLOAT_EQ(result.MaxDisplacement, 0.f);
        }
    }
}

TEST(BilateralFilterOperations, EveryDomainCopyAndInPlaceHistory)
{
    for(unsigned d=1;d<=8;++d)for(bool inPlace:{false,true})
    {
        SCOPED_TRACE(d);
        SCOPED_TRACE(inPlace);
        R::WorldRegistry worlds;auto world=worlds.CreateWorld("bilateral");auto& scene=*worlds.Get(world);
        R::SpatialIndexCache cache(worlds);auto entity=Make(scene,D(d));auto config=Config(entity,D(d));
        auto& props=PointDomainProperties(scene,entity,D(d));const auto size=props.Size();
        const bool half=D(d)==D::MeshHalfedge || D(d)==D::GraphHalfedge;
        if(half)scene.Raw().get<GS::Edges>(entity).Properties.GetOrAdd<bool>("e:deleted")[1]=true;
        else props.GetOrAdd<bool>(D(d)==D::MeshFace?"f:deleted":(D(d)==D::MeshEdge || D(d)==D::GraphEdge)?"e:deleted":"v:deleted")[2]=true;
        const auto original=std::as_const(props).Get<glm::vec3>("samples").Vector();
        props.GetOrAdd<glm::vec3>("filtered").Vector().assign(size,glm::vec3(77));
        if(inPlace)config.Output=config.Positions;
        R::EditorCommandHistory history;
        const auto commands=R::BindEditorProcessingCommands(R::EditorProcessingContext{
            .Scene=&scene,.World=world,.CommandHistory=&history,.SpatialIndices=&cache});
        const auto catalog=R::GetEditorBilateralFilterInputCatalog(commands,config.StableEntityId);
        EXPECT_TRUE(std::ranges::any_of(catalog.Entries,[&](auto& e){return e.Ref==config.Positions;}));
        EXPECT_TRUE(std::ranges::any_of(catalog.Entries,[&](auto& e){return e.Ref==config.Normals;}));
        ASSERT_TRUE(R::PreviewEditorBilateralFilterCommand(commands,config).Enabled);
        const auto reference=R::ApplyEditorBilateralFilterCommand(commands,config);
        ASSERT_TRUE(reference.Succeeded())<<reference.Message;EXPECT_EQ(reference.CompletedIterations,3);
        const auto values=std::as_const(props).Get<glm::vec3>(config.Output.Name).Vector();
        EXPECT_EQ(values[2],inPlace?original[2]:glm::vec3(77));if(half)EXPECT_EQ(values[3],inPlace?original[3]:glm::vec3(77));
        EXPECT_EQ(props.Size(),size);props.Get<float>("keep")[0]=99;
        ASSERT_TRUE(history.Undo().Succeeded());
        EXPECT_EQ(std::as_const(props).Get<glm::vec3>(config.Output.Name)[0],inPlace?original[0]:glm::vec3(77));
        ASSERT_TRUE(history.Redo().Succeeded());EXPECT_EQ(std::as_const(props).Get<float>("keep")[0],99);
        ASSERT_TRUE(history.Undo().Succeeded());
        config.Backend=R::BilateralFilterBackend::CpuLBVH;
        const auto indexed=R::ApplyEditorBilateralFilterCommand(commands,config);
        ASSERT_TRUE(indexed.Succeeded())<<indexed.Message;EXPECT_EQ(indexed.ActualBackend,"cpu_lbvh");EXPECT_EQ(indexed.WorkspaceBuilds,2);
        const auto actual=std::as_const(props).Get<glm::vec3>(config.Output.Name);
        for(std::size_t i=0;i<size;++i)if(i!=4 || D(d)!=D::PointCloudPoint)EXPECT_LE(glm::length(actual[i]-values[i]),1e-5);
        EXPECT_FLOAT_EQ(indexed.AverageDisplacement,reference.AverageDisplacement);
        EXPECT_FLOAT_EQ(indexed.MaxDisplacement,reference.MaxDisplacement);
        EXPECT_EQ(indexed.PointsFiltered,reference.PointsFiltered);
        EXPECT_EQ(indexed.DegenerateNormals,reference.DegenerateNormals);
        props.Get<glm::vec3>("directions")[0].x=1;
        EXPECT_EQ(history.Undo().Status,R::EditorCommandHistoryStatus::StaleEntity);
    }
}
TEST(BilateralFilterOperations, JobsRejectChangedInputsOutputsAndCancellation)
{
    for(unsigned change=0;change<6;++change)
    {
        SCOPED_TRACE(change);Extrinsic::ECS::Scene::Registry scene;auto entity=Make(scene,D::MeshVertex);auto config=Config(entity,D::MeshVertex);
        auto& props=PointDomainProperties(scene,entity,D::MeshVertex);
        Intrinsic::Tests::EditorFeatureTestContext context;context.Scene=&scene;R::EditorCommandHistory history;context.CommandHistory=&history;
        std::optional<R::EditorBilateralFilterResult> delivered;int deliveries=0;
        auto onComplete=[&](R::EditorBilateralFilterResult r){++deliveries;delivered=std::move(r);};
        Extrinsic::Tests::EditorJobHarness jobs;jobs.Attach(context);
        ASSERT_EQ(R::ApplyEditorBilateralFilterCommand(context,config,onComplete).Status,R::EditorCommandStatus::Pending);
        EXPECT_FALSE(props.Exists("filtered"));
        switch(change){case 0:props.Get<float>("keep")[0]=99;break;case 1:props.Get<glm::vec3>("samples")[0].x+=1;break;case 2:props.GetOrAdd<bool>("v:deleted")[0]=true;break;case 3:props.GetOrAdd<glm::vec3>("filtered")[0]=glm::vec3(77);break;case 4:(void)jobs.Jobs().Cancel(jobs.Snapshot().Entries[0].Token);break;case 5:props.Get<glm::vec3>("directions")[0].x=1;break;}
        ASSERT_TRUE(jobs.DrainUntilTerminal());ASSERT_TRUE(delivered);
        EXPECT_EQ(deliveries,1)<<"a queued bilateral job owes exactly one terminal result";
        EXPECT_EQ(delivered->Succeeded(),change==0)<<delivered->Message;
        EXPECT_EQ(history.CanUndo(),change==0);EXPECT_EQ(props.Exists("filtered"),change==0 || change==3);
    }
}
TEST(BilateralFilterOperations, ZeroPassAndOutputPreflight)
{
    Extrinsic::ECS::Scene::Registry scene;auto entity=Make(scene,D::MeshVertex);auto config=Config(entity,D::MeshVertex);
    auto& props=PointDomainProperties(scene,entity,D::MeshVertex);
    const auto context=R::BindEditorProcessingCommands(R::EditorProcessingContext{.Scene=&scene});
    config.Iterations=0;const auto result=R::ApplyEditorBilateralFilterCommand(context,config);ASSERT_TRUE(result.Succeeded());EXPECT_EQ(result.CompletedIterations,0);
    EXPECT_EQ(std::as_const(props).Get<glm::vec3>("filtered").Vector(),std::as_const(props).Get<glm::vec3>("samples").Vector());
    config.Output=config.Normals;EXPECT_FALSE(R::PreviewEditorBilateralFilterCommand(context,config).Enabled);
    config.Output=config.Positions;config.Output.Name="v:deleted";EXPECT_FALSE(R::PreviewEditorBilateralFilterCommand(context,config).Enabled);
    config.Output=config.Positions;config.Backend=R::BilateralFilterBackend::VulkanLBVH;EXPECT_FALSE(R::PreviewEditorBilateralFilterCommand(context,config).Enabled);
}
