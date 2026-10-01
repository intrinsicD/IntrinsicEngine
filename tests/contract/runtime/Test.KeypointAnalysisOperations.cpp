#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>
#include "MockRHI.hpp"
#include "Modules/KeypointAnalysis/Runtime.KeypointPaging.TestSupport.hpp"
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

import Extrinsic.Runtime.PointAnalysisOperations;
import Extrinsic.Runtime.GpuPropertyBinding;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Graphics.PointKeypoints;
import Extrinsic.Graphics.GpuPropertyResidency;
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
import Extrinsic.Graphics.Component.VisualizationConfig;

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
    ASSERT_TRUE(R::PreviewEditorKeypointAnalysisCommand(commands, config).Enabled);
    EXPECT_FALSE(PointDomainProperties(scene, entity, D::MeshFace).Exists("saliency"));
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

TEST(KeypointAnalysisConfig, FullVulkanBackendRoundTripsConfig)
{
    auto config=Config(entt::entity{1},D::MeshVertex);
    config.Backend=R::KeypointAnalysisBackend::VulkanCompute;
    config.GpuQueryBatchSize=256;config.GpuRadiusCapacity=1024;
    const auto document=R::SerializeKeypointAnalysisConfig(config);
    EXPECT_NE(document.find("vulkan_compute"),std::string::npos);
    EXPECT_TRUE(R::ValidateKeypointAnalysisConfigSection(document,{},"test").Usable());
    Extrinsic::Core::Config::EngineConfig engine;
    R::SetKeypointAnalysisConfig(engine,config);
    const auto parsed=R::GetKeypointAnalysisConfig(engine);
    ASSERT_TRUE(parsed);
    EXPECT_EQ(R::SerializeKeypointAnalysisConfig(*parsed),document);
}

TEST(KeypointAnalysisOperations, EveryDomainReferenceCacheHistoryAndDeletedRows)
{
    for(unsigned d=1;d<=8;++d)
    {
        SCOPED_TRACE(d);
        R::WorldRegistry worlds;auto world=worlds.CreateWorld("keypoints");auto& scene=*worlds.Get(world);
        R::SpatialIndexCache cache(worlds);auto entity=Make(scene,D(d));auto c=Config(entity,D(d));
        auto& props=PointDomainProperties(scene,entity,D(d));const auto size=props.Size();
        const bool half=D(d)==D::MeshHalfedge || D(d)==D::GraphHalfedge;
        if(half)scene.Raw().get<GS::Edges>(entity).Properties.GetOrAdd<bool>("e:deleted")[1]=true;
        else props.GetOrAdd<bool>(D(d)==D::MeshFace?"f:deleted":(D(d)==D::MeshEdge || D(d)==D::GraphEdge)?"e:deleted":"v:deleted")[2]=true;
        props.GetOrAdd<std::uint32_t>("keypoints").Vector().assign(size,77);
        props.GetOrAdd<float>("saliency").Vector().assign(size,77);
        const auto revision=std::as_const(props).Get<glm::vec3>("samples").Revision();
        R::EditorCommandHistory history;
        R::EditorProcessingContext context{.Scene=&scene,.World=world,.CommandHistory=&history,.SpatialIndices=&cache};
        const auto catalog=R::GetEditorPointInputCatalog(R::BindEditorProcessingCommands(context),c.StableEntityId);
        EXPECT_TRUE(std::ranges::any_of(catalog.Entries,[&](auto& e){return e.Ref==c.Positions;}));
        ASSERT_TRUE(R::PreviewEditorKeypointAnalysisCommand(R::BindEditorProcessingCommands(context),c).Enabled);
        const auto reference=R::ApplyEditorKeypointAnalysisCommand(R::BindEditorProcessingCommands(context),c);
        ASSERT_TRUE(reference.Succeeded())<<reference.Message;EXPECT_EQ(reference.ActualBackend,"cpu_kdtree");
        EXPECT_GT(reference.MeanSpacing,0.f);
        EXPECT_FLOAT_EQ(reference.SalientRadius,c.SalientRadius);
        EXPECT_FLOAT_EQ(reference.NonMaxRadius,c.NonMaxRadius);
        const auto mask=std::as_const(props).Get<std::uint32_t>("keypoints").Vector();
        const auto score=std::as_const(props).Get<float>("saliency").Vector();
        EXPECT_EQ(mask[2],77);EXPECT_EQ(score[2],77);if(half){EXPECT_EQ(mask[3],77);EXPECT_EQ(score[3],77);}
        EXPECT_EQ(props.Size(),size);EXPECT_EQ(std::as_const(props).Get<glm::vec3>("samples").Revision(),revision);
        props.Get<float>("keep")[0]=99;
        ASSERT_TRUE(history.Undo().Succeeded());EXPECT_EQ(std::as_const(props).Get<float>("saliency").Vector(),std::vector<float>(size,77));
        ASSERT_TRUE(history.Redo().Succeeded());EXPECT_EQ(std::as_const(props).Get<float>("keep")[0],99);
        c.Backend=R::KeypointAnalysisBackend::CpuLBVH;
        const auto indexed=R::ApplyEditorKeypointAnalysisCommand(R::BindEditorProcessingCommands(context),c);ASSERT_TRUE(indexed.Succeeded())<<indexed.Message;
        EXPECT_EQ(indexed.ActualBackend,"cpu_lbvh");EXPECT_EQ(indexed.KeypointCount,reference.KeypointCount);
        EXPECT_EQ(std::as_const(props).Get<std::uint32_t>("keypoints").Vector(),mask);
        for(std::size_t i=0;i<size;++i)EXPECT_NEAR(std::as_const(props).Get<float>("saliency")[i],score[i],1e-5);
        EXPECT_FLOAT_EQ(indexed.MeanSpacing,reference.MeanSpacing);
        EXPECT_FLOAT_EQ(indexed.SalientRadius,reference.SalientRadius);
        EXPECT_FLOAT_EQ(indexed.NonMaxRadius,reference.NonMaxRadius);
        EXPECT_TRUE(R::ApplyEditorKeypointAnalysisCommand(R::BindEditorProcessingCommands(context),c).IndexReused);
        Intrinsic::Tests::EditorFeatureTestContext visualization;visualization.Scene=&scene;visualization.VisualizationCommandsAvailable=true;
        std::optional<R::VisualizationRecipe> stored;
        visualization.VisualizationRecipes.GetRecipe=[&](std::uint32_t){return stored;};
        visualization.VisualizationRecipes.SetRecipe=[&](std::uint32_t,R::VisualizationRecipe value){stored=std::move(value);};
        visualization.VisualizationRecipes.ClearRecipe=[&](std::uint32_t){stored.reset();};
        const auto shown=R::ApplyEditorVisualizationRecipeCommand(visualization,{.StableEntityId=c.StableEntityId,
            .Recipe={.Data=R::ScalarVisualizationRecipe{.Source=c.Score,.OutputName="saliency_colors"}}});
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
            EXPECT_EQ(lane->ScalarFieldName,c.Score.Name);
        }
    }
}
TEST(KeypointAnalysisOperations, JobsRejectChangedInputsOutputsAndCancellation)
{
    for(unsigned change=0;change<6;++change)
    {
        SCOPED_TRACE(change);Extrinsic::ECS::Scene::Registry scene;auto entity=Make(scene,D::MeshVertex);auto c=Config(entity,D::MeshVertex);
        auto& props=PointDomainProperties(scene,entity,D::MeshVertex);
        Intrinsic::Tests::EditorFeatureTestContext context;context.Scene=&scene;R::EditorCommandHistory history;context.CommandHistory=&history;
        std::optional<R::EditorKeypointAnalysisResult> delivered;std::function<void(R::EditorKeypointAnalysisResult)> sink=[&](auto r){delivered=std::move(r);};
        Extrinsic::Tests::EditorJobHarness jobs;jobs.Attach(context);
        ASSERT_EQ(R::ApplyEditorKeypointAnalysisCommand(R::BindEditorProcessingCommands(context),c, sink).Status,R::EditorCommandStatus::Pending);
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
    auto& props=PointDomainProperties(scene,entity,D::MeshVertex);R::EditorProcessingContext context{.Scene=&scene};
    for(auto name:{"samples","v:deleted","v:connectivity"}){auto bad=c;bad.Score.Name=name;EXPECT_FALSE(R::PreviewEditorKeypointAnalysisCommand(R::BindEditorProcessingCommands(context),bad).Enabled);}
    auto unrelatedName=c;unrelatedName.Score.Name="h:connectivity";
    EXPECT_TRUE(R::PreviewEditorKeypointAnalysisCommand(R::BindEditorProcessingCommands(context),unrelatedName).Enabled);
    auto gpu=c;gpu.Backend=R::KeypointAnalysisBackend::VulkanLBVH;EXPECT_FALSE(R::PreviewEditorKeypointAnalysisCommand(R::BindEditorProcessingCommands(context),gpu).Enabled);
    gpu.Backend=R::KeypointAnalysisBackend::VulkanCompute;
    EXPECT_FALSE(R::PreviewEditorKeypointAnalysisCommand(R::BindEditorProcessingCommands(context),gpu).Enabled);
    EXPECT_FALSE(R::ApplyEditorKeypointAnalysisCommand(R::BindEditorProcessingCommands(context),gpu).Succeeded());
    props.GetOrAdd<float>("saliency").Vector().assign(props.Size(),77);
    props.Get<glm::vec3>("samples").Vector().assign(props.Size(),glm::vec3(0));
    EXPECT_FALSE(R::ApplyEditorKeypointAnalysisCommand(R::BindEditorProcessingCommands(context),c).Succeeded());
    EXPECT_EQ(std::as_const(props).Get<float>("saliency").Vector(),std::vector<float>(props.Size(),77));EXPECT_FALSE(props.Exists("keypoints"));
}

TEST(KeypointAnalysisOperations, ExpiredQueuedCommandsNeverBorrowFreedSceneOrDeliver)
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
    ASSERT_EQ(R::ApplyEditorKeypointAnalysisCommand(commands, config,
        [&](R::EditorKeypointAnalysisResult) { ++deliveries; }).Status, R::EditorCommandStatus::Pending);
    active = false;
    scene.reset();
    ASSERT_TRUE(jobs.DrainUntilTerminal());
    EXPECT_EQ(deliveries, 0u);
    EXPECT_FALSE(history.CanUndo());
    EXPECT_FALSE(commands.IsBound());
    EXPECT_TRUE(R::GetEditorPointInputCatalog(commands, config.StableEntityId).Entries.empty());
}

TEST(KeypointAnalysisOperations, ResolvedScaleDiagnosticsPreserveAutomaticAndExplicitRadii)
{
    R::WorldRegistry worlds;
    const auto world=worlds.CreateWorld("resolved-scale");
    auto& scene=*worlds.Get(world);
    R::SpatialIndexCache cache(worlds);
    const auto entity=Make(scene,D::PointCloudPoint);
    auto positions=PointDomainProperties(scene,entity,D::PointCloudPoint).Get<glm::vec3>("samples");
    for(std::size_t i=0;i<plane.size();++i)positions[i]=plane[i];
    const auto commands=R::BindEditorProcessingCommands(R::EditorProcessingContext{
        .Scene=&scene,.World=world,.SpatialIndices=&cache});
    for(const auto backend:{R::KeypointAnalysisBackend::CpuKDTree,R::KeypointAnalysisBackend::CpuLBVH})
    {
        for(const bool automatic:{false,true})
        {
            auto config=Config(entity,D::PointCloudPoint);
            config.Backend=backend;
            if(automatic)config.SalientRadius=config.NonMaxRadius=0;
            const auto result=R::ApplyEditorKeypointAnalysisCommand(commands,config);
            ASSERT_TRUE(result.Succeeded())<<result.Message;
            EXPECT_EQ(result.LiveCount,4u);
            EXPECT_FLOAT_EQ(result.MeanSpacing,2.f);
            EXPECT_FLOAT_EQ(result.SalientRadius,automatic?12.f:config.SalientRadius);
            EXPECT_FLOAT_EQ(result.NonMaxRadius,automatic?8.f:config.NonMaxRadius);
        }
    }
}

TEST(KeypointAnalysis, AlternateScalarStoragePublishesAndRestoresBothOutputs)
{
    R::WorldRegistry worlds;
    const auto world=worlds.CreateWorld("typed keypoint outputs");
    auto& scene=*worlds.Get(world);
    const auto entity=Make(scene,D::MeshFace);
    auto config=Config(entity,D::MeshFace);
    config.Mask.ValueKind=Geometry::PropertyValueKind::Int32;
    config.Score.ValueKind=Geometry::PropertyValueKind::Double;
    Extrinsic::Core::Config::EngineConfig document;
    R::SetKeypointAnalysisConfig(document,config);
    const auto decoded=R::GetKeypointAnalysisConfig(document);
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded->Mask,config.Mask);
    EXPECT_EQ(decoded->Score,config.Score);
    R::EditorCommandHistory history;
    R::SpatialIndexCache cache(worlds);
    const R::EditorProcessingContext context{.Scene=&scene,.World=world,.CommandHistory=&history,.SpatialIndices=&cache};
    const auto result=R::ApplyEditorKeypointAnalysisCommand(R::BindEditorProcessingCommands(context),config);
    ASSERT_TRUE(result.Succeeded())<<result.Message;
    const auto& props=PointDomainProperties(scene,entity,D::MeshFace);
    EXPECT_TRUE(props.Get<std::int32_t>(config.Mask.Name));
    EXPECT_TRUE(props.Get<double>(config.Score.Name));
    ASSERT_TRUE(history.Undo().Succeeded());
    EXPECT_FALSE(props.Exists(config.Mask.Name));
    EXPECT_FALSE(props.Exists(config.Score.Name));
}

// Exercise the production framed producer and paired scalar publication on MockRHI.
namespace
{
    class KeypointResident : public ::testing::Test
    {
    public:
        Extrinsic::Tests::MockDevice Device;
        R::WorldRegistry Worlds;
        R::CommandBus Bus;
        R::KernelEventBus Events;
        R::ServiceRegistry Services;
        Extrinsic::Tests::EditorJobHarness Jobs;
        R::SpatialIndexCache Cache;
        R::EditorProcessingContext Context;
        R::EditorCommandHistory History;
        R::KeypointAnalysisConfig C;
        R::EditorPointScalarTransactionHandle Run;
        R::EditorKeypointAnalysisResult Initial;
        std::vector<R::EditorKeypointAnalysisResult> Results;
        std::vector<std::function<void()>> Completions;
        entt::entity Entity{};
        bool Hold{}, HoldCopy{}, FreezeFrame{}, Attached{true}, ResumeSeen{}, ForceResume{true};
        unsigned Submissions{}, Copies{};
        float ScoreValue{3};
        std::uint64_t InputAddress{};
        R::KeypointPagingLimits Saved=R::KeypointPagingForTesting;
        auto Commands(){return R::BindEditorProcessingCommands(Context);}
        auto& Rows(){return PointDomainProperties(*Context.Scene,Entity,D::PointCloudPoint);}
        auto Phase(){return R::SnapshotEditorPointScalar(Commands(),Run).Phase;}
        void SetUp() override
        {
            Context.World=Worlds.CreateWorld("keypoint resident");Context.Scene=Worlds.Get(Context.World);
            Entity=Make(*Context.Scene,D::PointCloudPoint);C=Config(Entity,D::PointCloudPoint);
            Rows().Resize(20);Rows().GetOrAdd<bool>("v:deleted").Vector().assign(20,false);
            for(unsigned i=0;i<20;++i)Rows().Get<glm::vec3>("samples")[i]={float(i),float(i%3),float(i%5)};
            C.Backend=R::KeypointAnalysisBackend::VulkanCompute;C.GpuRadiusCapacity=2;
            Device.ShaderFloat64=true;Device.TransferQueue.AcceptBufferUploads=true;
            Context.Device=&Device;Context.SpatialIndices=&Cache;Context.CommandHistory=&History;
            Context.AttachmentActive=[this]{return Attached;};Jobs.Attach(Context);
            Services.BeginRegistration();ASSERT_TRUE(Services.Provide<Extrinsic::RHI::IDevice>(Device,"test"));
            R::EngineSetup setup{Bus,Events,Jobs.Jobs(),Worlds,Services,[](R::FramePhase,R::RuntimeFrameHook){}};
            ASSERT_TRUE(Cache.OnRegister(setup));
            R::KeypointPagingForTesting={.Rows=3,.Visits=8,.Pairs=24};
            Device.ComputeReadback=[this](auto record,auto bytes,auto sink){
                auto buffer=record(Device.CommandContext);EXPECT_TRUE(buffer.IsValid());
                Queue(buffer,bytes,std::move(sink));return Extrinsic::RHI::ReadbackToken{2};
            };
            Device.TransferQueue.BufferDownload=[this](auto buffer,auto bytes,auto,auto sink){
                Queue(buffer,bytes,std::move(sink));return Extrinsic::RHI::ReadbackToken{3};
            };
        }
        void Queue(Extrinsic::RHI::BufferHandle buffer,std::uint64_t bytes,Extrinsic::RHI::ReadbackSink sink)
        {
            std::vector<std::byte> data(bytes);
            if(bytes==sizeof(Extrinsic::Graphics::PointKeypointHeader)) {
                ++Submissions;
                const auto& payload=Device.CommandContext.PushConstantPayloads.back();ASSERT_EQ(payload.size(),120u);
                std::uint32_t mode{},first{},rows{},visits{},resume{};std::uint64_t input{};
                std::memcpy(&input,payload.data()+8,8);InputAddress=input;
                std::memcpy(&first,payload.data()+44,4);std::memcpy(&rows,payload.data()+48,4);
                std::memcpy(&mode,payload.data()+84,4);
                std::memcpy(&visits,payload.data()+112,4);std::memcpy(&resume,payload.data()+116,4);
                if(mode==0 || mode==2 || mode==3){
                    EXPECT_LE(rows,R::KeypointPagingForTesting.Rows);
                    EXPECT_LE(std::uint64_t(rows)*visits,R::KeypointPagingForTesting.Pairs);
                    EXPECT_LE(visits,R::KeypointPagingForTesting.Visits);
                } else if(mode==4 || mode==5)EXPECT_EQ(rows,Rows().Size());
                Extrinsic::Graphics::PointKeypointHeader h{.MeanSpacing=1,.SalientRadius=10,.NonMaxRadius=.5};
                if(ForceResume && mode==0 && first==0 && !resume)h.Reserved=1;
                if(resume)ResumeSeen=true;
                h.MaximumNeighbors=19; // Diagnostic support exceeds the configured hybrid capacity.
                if(mode==4) {
                    ++Copies;if(HoldCopy)Hold=true;
                    std::uint64_t score{},mask{};std::memcpy(&score,payload.data()+96,8);std::memcpy(&mask,payload.data()+104,8);
                    auto& scores=Device.BufferContents[std::uint32_t((score-0x100000000ull)/0x1000ull)];scores.resize(Rows().Size()*4);
                    auto& masks=Device.BufferContents[std::uint32_t((mask-0x100000000ull)/0x1000ull)];masks.resize(Rows().Size()*4);
                    for(auto i=first;i<first+rows;++i) {
                        const float value=ScoreValue;std::memcpy(scores.data()+i*4,&value,4);
                        const std::uint32_t maskValue=i%2;std::memcpy(masks.data()+i*4,&maskValue,4);
                    }
                }
                std::memcpy(data.data(),&h,sizeof(h));
            } else {
                const auto& contents=Device.BufferContents[buffer.Index];
                if(contents.size()>=bytes)std::memcpy(data.data(),contents.data(),bytes);
            }
            Completions.push_back([sink=std::move(sink),data=std::move(data)]()mutable{sink.Deliver(data);});
        }
        void Tick()
        {
            if(!Hold){auto pending=std::exchange(Completions,{});for(auto& complete:pending)complete();}
            (void)Jobs.Jobs().DrainCompletions(Events);
            Jobs.Jobs().RecordGpuQueueFrameCommands(Device.CommandContext);if(!FreezeFrame)++Device.GlobalFrameNumber;
            (void)Jobs.Jobs().DrainGpuQueueCompletedTransfers();
        }
        template<class P>bool Until(P predicate)
        {
            const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            while(!predicate()&&std::chrono::steady_clock::now()<end){Tick();std::this_thread::yield();}
            return predicate();
        }
        void Start(bool automatic=false){Run=R::StartEditorKeypointAnalysisTransaction(Commands(),C,Initial,[this](auto r){Results.push_back(r);},automatic);}
        void TearDown() override
        {
            R::DiscardEditorPointScalar(Commands(),Run);Hold=false;
            for(int i=0;i<5;++i)Tick();
            Jobs.Jobs().CancelAndDrain();(void)Jobs.Jobs().ShutdownGpuQueueParticipants([this]{Device.WaitIdle();});
            R::RuntimeModuleShutdownContext shutdown{Bus,Events,Jobs.Jobs(),Worlds,Services};Cache.OnShutdown(shutdown);
            Run.reset();R::KeypointPagingForTesting=Saved;
        }
    };
}
TEST_F(KeypointResident, ResidentPagingPreviewAcceptAtomicHistoryAndRepeatZeroUpload)
{
    Start();ASSERT_TRUE(Run)<<Initial.Message;
    EXPECT_EQ(Initial.GpuInputUploadBytes,20u*12);
    auto* residency=Cache.PropertyResidency();
    const auto score=R::MakeGpuPropertyKey(Context.World,Entity,C.Score),mask=R::MakeGpuPropertyKey(Context.World,Entity,C.Mask);
    EXPECT_TRUE(residency->HasRing(score));EXPECT_TRUE(residency->HasRing(mask));EXPECT_FALSE(residency->Front(score));
    ASSERT_TRUE(Until([&]{return Phase()==R::EditorGpuTransactionPhase::ReadyToAccept;}));
    EXPECT_TRUE(ResumeSeen);EXPECT_GT(Submissions,20u);EXPECT_EQ(Copies,1u);
    EXPECT_TRUE(residency->Front(score));EXPECT_TRUE(residency->Front(mask));EXPECT_FALSE(Rows().Exists(C.Score.Name));
    EXPECT_EQ(R::AcceptEditorPointScalar(Commands(),Run).Status,R::EditorCommandStatus::Pending);
    ASSERT_TRUE(Until([&]{return !Results.empty();}));EXPECT_EQ(Results.size(),1u);EXPECT_TRUE(Results[0].Succeeded());EXPECT_EQ(Results[0].MaximumNeighbors,19u);
    EXPECT_EQ(History.UndoCount(),1u);EXPECT_EQ(std::as_const(Rows()).Get<float>(C.Score.Name)[0],3);
    EXPECT_EQ(std::as_const(Rows()).Get<std::uint32_t>(C.Mask.Name)[1],1u);
    EXPECT_GT(Results[0].CpuStageReadbackBytes,20u*8);EXPECT_EQ(Results[0].ImplementationId,"vulkan.keypoints.resident.paged.v1");
    EXPECT_EQ(History.Undo().Status,R::EditorCommandHistoryStatus::Undone);
    EXPECT_FALSE(Rows().Exists(C.Score.Name));EXPECT_FALSE(Rows().Exists(C.Mask.Name));
    for (unsigned i = 0; i < Device.FramesInFlight + 2u; ++i) { Cache.Prune(); Tick(); }
    const auto reuses = Cache.Stats().WorkspaceReuses;
    const auto pipelines = Device.CreatePipelineCount;
    Start();ASSERT_TRUE(Run);EXPECT_EQ(Initial.GpuInputUploadBytes,0u);EXPECT_GT(Initial.GpuInputCacheHits,0u);
    EXPECT_EQ(Cache.Stats().WorkspaceReuses, reuses + 1);
    ASSERT_TRUE(Until([&]{return Phase()==R::EditorGpuTransactionPhase::ReadyToAccept;}));
    EXPECT_EQ(Device.CreatePipelineCount, pipelines);
}
TEST_F(KeypointResident, CompletionGatesPreviewAndDiscardKeepsResourcesUntilIdle)
{
    Hold=true;Start();ASSERT_TRUE(Run);
    ASSERT_TRUE(Until([&]{return !Completions.empty();}));const auto submissions=Submissions;
    for(int i=0;i<5;++i)Tick();EXPECT_EQ(Submissions,submissions);
    R::DiscardEditorPointScalar(Commands(),Run);EXPECT_EQ(Results.size(),1u);
    EXPECT_FALSE(Rows().Exists(C.Score.Name));
    unsigned idle=0;(void)Jobs.Jobs().ShutdownGpuQueueParticipants([&]{++idle;Device.WaitIdle();});EXPECT_EQ(idle,1u);
}
TEST_F(KeypointResident, StaleAcceptRefusalAndDetachDiscard)
{
    Start();ASSERT_TRUE(Until([&]{return Phase()==R::EditorGpuTransactionPhase::ReadyToAccept;}));
    Rows().Get<glm::vec3>("samples")[0].x+=1;
    const auto snapshot=R::SnapshotEditorPointScalar(Commands(),Run);EXPECT_FALSE(snapshot.CanAccept);EXPECT_FALSE(snapshot.AcceptRefusalReason.empty());
    EXPECT_EQ(R::AcceptEditorPointScalar(Commands(),Run).Status,R::EditorCommandStatus::StaleEntity);
    R::DiscardEditorPointScalar(Commands(),Run);EXPECT_FALSE(Rows().Exists(C.Mask.Name));
    Start();ASSERT_TRUE(Run);Attached=false;
    ASSERT_TRUE(Until([&]{return Phase()==R::EditorGpuTransactionPhase::Discarded;}));EXPECT_FALSE(Rows().Exists(C.Mask.Name));
}
TEST_F(KeypointResident, AutomaticAcceptAndInitialSubmissionRejectExactlyOnce)
{
    Start(true);ASSERT_TRUE(Run);ASSERT_TRUE(Until([&]{return !Results.empty();}));EXPECT_TRUE(Results.back().Succeeded());
    R::DiscardEditorPointScalar(Commands(),Run);EXPECT_EQ(Results.size(),1u);
    Context.JobCommands.Submit=[](R::JobDesc,R::EditorJobIdentity){return R::JobToken{};};
    Start();EXPECT_FALSE(Run);EXPECT_EQ(Initial.Status,R::EditorCommandStatus::GeometryProcessingFailed);EXPECT_EQ(Results.size(),1u);
}
TEST_F(KeypointResident, ReentrantDiscardDuringAcceptStillPublishesPairExactlyOnce)
{
    Context.InvalidateWorkspaceSnapshotCache=[this]{R::DiscardEditorPointScalar(Commands(),Run);};
    Start();ASSERT_TRUE(Until([&]{return Phase()==R::EditorGpuTransactionPhase::ReadyToAccept;}));
    (void)R::AcceptEditorPointScalar(Commands(),Run);
    ASSERT_TRUE(Until([&]{return !Results.empty();}));
    EXPECT_EQ(Results.size(),1u);EXPECT_TRUE(Results.back().Succeeded());EXPECT_EQ(History.UndoCount(),1u);
    EXPECT_TRUE(Rows().Exists(C.Score.Name));EXPECT_TRUE(Rows().Exists(C.Mask.Name));
}
TEST_F(KeypointResident, RejectedAcceptSubmissionPreservesFailureWithoutHanging)
{
    const auto submit=Context.JobCommands.Submit;
    Context.JobCommands.Submit=[submit](R::JobDesc job,R::EditorJobIdentity id) {
        if(job.DebugName=="Accept point scalar")return R::JobToken{};
        return submit(std::move(job),std::move(id));
    };
    Start(true);ASSERT_TRUE(Run);ASSERT_TRUE(Until([&]{return !Results.empty();}));
    EXPECT_EQ(Results.size(),1u);EXPECT_EQ(Results.back().Status,R::EditorCommandStatus::GeometryProcessingFailed);
    EXPECT_FALSE(Rows().Exists(C.Score.Name));EXPECT_FALSE(Rows().Exists(C.Mask.Name));
}

TEST_F(KeypointResident, RingCopyMustCompleteBeforeObservedFrontPublication)
{
    HoldCopy=true;Start();ASSERT_TRUE(Run);
    ASSERT_TRUE(Until([&]{return Copies>0;}));
    const auto key=R::MakeGpuPropertyKey(Context.World,Entity,C.Score);
    EXPECT_FALSE(Cache.PropertyResidency()->Front(key));
    EXPECT_EQ(Phase(),R::EditorGpuTransactionPhase::Running);
    HoldCopy=false;Hold=false;
    ASSERT_TRUE(Until([&]{return Phase()==R::EditorGpuTransactionPhase::ReadyToAccept;}));
    EXPECT_TRUE(Cache.PropertyResidency()->Front(key));
}
TEST_F(KeypointResident, EitherOutputRevisionRefusesPairAccept)
{
    Start();ASSERT_TRUE(Until([&]{return Phase()==R::EditorGpuTransactionPhase::ReadyToAccept;}));
    (void)Rows().GetOrAdd<std::uint32_t>(C.Mask.Name,9);
    EXPECT_FALSE(R::SnapshotEditorPointScalar(Commands(),Run).CanAccept);
    EXPECT_EQ(R::AcceptEditorPointScalar(Commands(),Run).Status,R::EditorCommandStatus::StaleEntity);
    R::DiscardEditorPointScalar(Commands(),Run);
    EXPECT_FALSE(Rows().Exists(C.Score.Name));EXPECT_EQ(std::as_const(Rows()).Get<std::uint32_t>(C.Mask.Name)[0],9u);
}

TEST_F(KeypointResident, SubmissionCountUsesPairBudgetAndLinearStagesUseOneDispatch)
{
    constexpr unsigned count=32769;
    Rows().Resize(count);Rows().GetOrAdd<bool>("v:deleted").Vector().assign(count,false);
    for(unsigned i=0;i<count;++i)Rows().Get<glm::vec3>("samples")[i]={float(i),float(i%3),float(i%5)};
    R::KeypointPagingForTesting={};ForceResume=false;
    Start();ASSERT_TRUE(Run)<<Initial.Message;
    ASSERT_TRUE(Until([&]{return Phase()==R::EditorGpuTransactionPhase::ReadyToAccept;}));
    // Three traversal stages, each completing in one visit budget per row,
    // plus spacing partials, final scale reduction and ring copy.
    EXPECT_EQ(Submissions,3u*((count*1024ull+(1u<<24)-1)/(1u<<24))+3u);
    EXPECT_EQ(Copies,1u);
}
TEST_F(KeypointResident, AlternateStorageConvertsOnAcceptWithoutBindingWrongTypedFront)
{
    C.Mask.ValueKind=Geometry::PropertyValueKind::Int32;
    C.Score.ValueKind=Geometry::PropertyValueKind::Double;
    (void)Rows().GetOrAdd<std::int32_t>(C.Mask.Name,77);
    (void)Rows().GetOrAdd<double>(C.Score.Name,77);
    Start(true);ASSERT_TRUE(Run)<<Initial.Message;
    ASSERT_TRUE(Until([&]{return !Results.empty();}));
    ASSERT_TRUE(Results.back().Succeeded())<<Results.back().Message;
    EXPECT_EQ(Results.back().ActualBackend,"vulkan_compute");
    EXPECT_EQ(std::as_const(Rows()).Get<double>(C.Score.Name)[0],3);
    EXPECT_EQ(std::as_const(Rows()).Get<std::int32_t>(C.Mask.Name)[1],1);
    auto score=C.Score,mask=C.Mask;
    score.ValueKind=Geometry::PropertyValueKind::Float;mask.ValueKind=Geometry::PropertyValueKind::UInt32;
    EXPECT_FALSE(Cache.PropertyResidency()->Front(R::MakeGpuPropertyKey(Context.World,Entity,score)));
    EXPECT_FALSE(Cache.PropertyResidency()->Front(R::MakeGpuPropertyKey(Context.World,Entity,mask)));
    ASSERT_TRUE(History.Undo().Succeeded());
    EXPECT_EQ(std::as_const(Rows()).Get<double>(C.Score.Name)[0],77);
    EXPECT_EQ(std::as_const(Rows()).Get<std::int32_t>(C.Mask.Name)[1],77);
}
TEST_F(KeypointResident, AutomaticAcceptPreservesCheckedConversionFailure)
{
    C.Score.ValueKind=Geometry::PropertyValueKind::Int32;ScoreValue=.25f;
    (void)Rows().GetOrAdd<std::int32_t>(C.Score.Name,77);
    Start(true);ASSERT_TRUE(Run)<<Initial.Message;
    ASSERT_TRUE(Until([&]{return !Results.empty();}));
    EXPECT_EQ(Results.size(),1u);
    EXPECT_EQ(Results.back().Status,R::EditorCommandStatus::InvalidProcessingParameters);
    EXPECT_EQ(std::as_const(Rows()).Get<std::int32_t>(C.Score.Name)[0],77);
    EXPECT_FALSE(Rows().Exists(C.Mask.Name));EXPECT_EQ(History.UndoCount(),0u);
}
TEST_F(KeypointResident, DuplicateGpuRequestIsTerminalAndDoesNotStealCompletion)
{
    Hold=true;Start();ASSERT_TRUE(Run);
    unsigned duplicates{};
    const auto duplicate=R::ApplyEditorKeypointAnalysisCommand(Commands(),C,[&](auto){++duplicates;});
    EXPECT_EQ(duplicate.Status,R::EditorCommandStatus::GeometryProcessingFailed);
    EXPECT_NE(duplicate.Message.find("already active"),std::string::npos);
    EXPECT_EQ(duplicates,0u);
    Hold=false;ASSERT_TRUE(Until([&]{return Phase()==R::EditorGpuTransactionPhase::ReadyToAccept;}));
    R::DiscardEditorPointScalar(Commands(),Run);EXPECT_EQ(Results.size(),1u);
}

TEST_F(KeypointResident, ImmediateCompletionsAdvanceWithoutAnotherFrame)
{
    Start();ASSERT_TRUE(Run);
    // Warm the cached index and complete the first traversal chunk.
    ASSERT_TRUE(Until([&]{return ResumeSeen;}));
    FreezeFrame=true;const auto frame=Device.GlobalFrameNumber;
    ASSERT_TRUE(Until([&]{return Phase()==R::EditorGpuTransactionPhase::ReadyToAccept;}));
    EXPECT_EQ(Device.GlobalFrameNumber,frame);EXPECT_EQ(Copies,1u);
}
TEST_F(KeypointResident, RecordPageRejectsInvalidParametersAndBudgets)
{
    auto input=R::ResolveGpuPropertyInput(*Cache.PropertyResidency(),*Context.Scene,Context.World,Entity,C.Positions);
    ASSERT_TRUE(input);
    Extrinsic::Graphics::PointKeypointWorkspace workspace(Device);
    Extrinsic::Graphics::PointKeypointParams params;
    Extrinsic::Graphics::PointKeypointPage page{.Rows=20};
    const auto record=[&] {return workspace.RecordPage(Device.CommandContext,1,*input,1,20,params,page,{},{}).IsValid();};
    for(double gamma:{-1.,1.1,std::numeric_limits<double>::quiet_NaN()}) {
        params.Gamma21=gamma;EXPECT_FALSE(record());params.Gamma21=.975;
        params.Gamma32=gamma;EXPECT_FALSE(record());params.Gamma32=.975;
    }
    for(float radius:{-1.f,2e18f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
        params.SalientRadius=radius;EXPECT_FALSE(record());params.SalientRadius=0;
        params.NonMaxRadius=radius;EXPECT_FALSE(record());params.NonMaxRadius=0;
    }
    page.Visits=1025;EXPECT_FALSE(record());page.Visits=1024;
    params.MinimumNeighbors=20;EXPECT_FALSE(record());params.MinimumNeighbors=5;
    params.Gamma21=params.Gamma32=0;EXPECT_TRUE(record());
    C.GpuRadiusCapacity=0;Start();EXPECT_FALSE(Run);
    EXPECT_EQ(Initial.Status,R::EditorCommandStatus::InvalidProcessingParameters);
}
