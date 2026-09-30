#include <cstdint>
#include <cmath>
#include <limits>
#include <vector>
#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>
#include <gtest/gtest.h>
#include "MockRHI.hpp"
#include "PointDomainFixture.hpp"
#include "SandboxEditorJobHarness.hpp"
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.PointAnalysisOperations;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.WorldRegistry;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.GpuPropertyBinding;
import Extrinsic.Graphics.GpuPropertyResidency;
import Extrinsic.Graphics.OutlierAnalysis;
import Geometry.Properties;
namespace R=Extrinsic::Runtime;
namespace G=Extrinsic::Graphics;
namespace
{
    struct Harness
    {
        Extrinsic::ECS::Scene::Registry Scene;
        R::EditorCommandHistory History;
        Extrinsic::Tests::MockDevice Device;
        G::GpuPropertyResidency Residency{Device};
        Extrinsic::Tests::EditorJobHarness Jobs;
        R::EditorProcessingContext Context;
        R::OutlierAnalysisConfig Config;
        entt::entity Entity;
        Harness()
        {
            Entity=Intrinsic::Tests::MakePointDomainSource(Scene,R::GeometryElementDomain::PointCloudPoint);
            Config.StableEntityId=R::SelectionController::ToStableEntityId(Entity);
            Config.Method=R::OutlierAnalysisMethod::Radius;Config.Backend=R::OutlierAnalysisBackend::VulkanLBVH;
            Config.Positions.Domain=Config.Mask.Domain=Config.Score.Domain=R::GeometryElementDomain::PointCloudPoint;
            (void)Rows().GetOrAdd<glm::vec3>("v:position",glm::vec3{0});
            Context.Scene=&Scene;Context.CommandHistory=&History;Context.Device=&Device;
            Device.TransferQueue.AcceptBufferUploads=true;Jobs.Attach(Context);
        }
        auto Commands(){return R::BindEditorProcessingCommands(Context);}
        Geometry::PropertySet& Rows(){return Intrinsic::Tests::PointDomainProperties(Scene,Entity,Config.Positions.Domain);}
        auto Ready(){return R::MakeEditorOutlierTransactionForTest(Commands(),Config,std::vector<float>(Rows().Size(),3),
            std::vector<std::uint32_t>(Rows().Size(),1),Residency);}
        auto Key(const R::GeometryPropertyRef& ref){return R::MakeGpuPropertyKey(Context.World,Entity,ref);}
    };
}
TEST(OutlierTransaction, AcceptPublishesBothFieldsUndoablyAndBindsRevisions)
{
    Harness h;auto run=h.Ready();ASSERT_TRUE(run);
    EXPECT_FALSE(h.Rows().Exists(h.Config.Score.Name));
    ASSERT_EQ(R::AcceptEditorOutlierAnalysis(h.Commands(),run).Status,R::EditorCommandStatus::Pending);
    ASSERT_TRUE(h.Jobs.DrainUntilTerminal());
    EXPECT_EQ(R::SnapshotEditorOutlierAnalysis(h.Commands(),run).Phase,R::EditorGpuTransactionPhase::Applied);
    const auto score=std::as_const(h.Rows()).Get<float>(h.Config.Score.Name);
    const auto mask=std::as_const(h.Rows()).Get<std::uint32_t>(h.Config.Mask.Name);
    ASSERT_TRUE(score);ASSERT_TRUE(mask);
    for(std::size_t i=0;i<h.Rows().Size();++i){EXPECT_EQ(score[i],3);EXPECT_EQ(mask[i],1u);}
    for(const auto& ref:{h.Config.Score,h.Config.Mask}){
        const auto front=h.Residency.Front(h.Key(ref));ASSERT_TRUE(front);
        const auto before=h.Residency.Stats().UploadBytes;
        const auto input=R::ResolveGpuPropertyInput(h.Residency,h.Scene,h.Context.World,h.Entity,ref);
        ASSERT_TRUE(input);EXPECT_EQ(front->Buffer,input->Buffer);EXPECT_EQ(h.Residency.Stats().UploadBytes,before);
    }
    ASSERT_TRUE(h.History.Undo().Succeeded());EXPECT_FALSE(h.Rows().Exists(h.Config.Score.Name));EXPECT_FALSE(h.Rows().Exists(h.Config.Mask.Name));
    ASSERT_TRUE(h.History.Redo().Succeeded());EXPECT_TRUE(h.Rows().Exists(h.Config.Mask.Name));
}
TEST(OutlierTransaction, DiscardRetainsCpuRowsAndReleasesBothRings)
{
    Harness h;auto run=h.Ready();ASSERT_TRUE(run);
    R::DiscardEditorOutlierAnalysis(h.Commands(),run);
    EXPECT_EQ(R::SnapshotEditorOutlierAnalysis(h.Commands(),run).Phase,R::EditorGpuTransactionPhase::Discarded);
    EXPECT_FALSE(h.Rows().Exists(h.Config.Mask.Name));EXPECT_FALSE(h.Residency.HasRing(h.Key(h.Config.Mask)));
    EXPECT_FALSE(h.Residency.HasRing(h.Key(h.Config.Score)));
}
TEST(OutlierTransaction, OutputChangeRefusesAccept)
{
    Harness h;auto run=h.Ready();ASSERT_TRUE(run);
    (void)h.Rows().GetOrAdd<float>(h.Config.Score.Name,7);
    EXPECT_FALSE(R::SnapshotEditorOutlierAnalysis(h.Commands(),run).CanAccept);
    EXPECT_EQ(R::AcceptEditorOutlierAnalysis(h.Commands(),run).Status,R::EditorCommandStatus::StaleEntity);
    R::DiscardEditorOutlierAnalysis(h.Commands(),run);
    EXPECT_EQ(std::as_const(h.Rows()).Get<float>(h.Config.Score.Name)[0],7);
}
TEST(OutlierTransaction, MissingDeviceCapabilitiesRefuseAdmission)
{
    Harness h;R::EditorOutlierAnalysisResult failure;
    EXPECT_FALSE(R::StartEditorOutlierAnalysisTransaction(h.Commands(),h.Config,failure));
    EXPECT_EQ(failure.Status,R::EditorCommandStatus::InvalidProcessingParameters);EXPECT_FALSE(failure.Message.empty());
}
TEST(OutlierTransaction, InputOrDeletionChangeRefusesAccept)
{
    for(bool deletion:{false,true}){
        Harness h;auto run=h.Ready();ASSERT_TRUE(run);
        if(deletion)h.Rows().GetOrAdd<bool>("v:deleted",false)[0]=true;
        else h.Rows().Get<glm::vec3>("v:position")[0]={1,2,3};
        EXPECT_FALSE(R::SnapshotEditorOutlierAnalysis(h.Commands(),run).CanAccept);
        EXPECT_EQ(R::AcceptEditorOutlierAnalysis(h.Commands(),run).Status,R::EditorCommandStatus::StaleEntity);
        R::DiscardEditorOutlierAnalysis({},run);EXPECT_FALSE(h.Rows().Exists(h.Config.Mask.Name));
    }
}
TEST(OutlierTransaction, ConfigRoundTripRetainsVulkanAndTypedOutputs)
{
    Harness h;Extrinsic::Core::Config::EngineConfig engine;
    R::SetOutlierAnalysisConfig(engine,h.Config);
    const auto restored=R::GetOutlierAnalysisConfig(engine);ASSERT_TRUE(restored);
    EXPECT_EQ(R::SerializeOutlierAnalysisConfig(*restored),R::SerializeOutlierAnalysisConfig(h.Config));
}
TEST(OutlierTransaction, UnsupportedStorageAndSubnormalInputsRefuseAdmission)
{
    Harness h;R::SpatialIndexCache cache;h.Context.SpatialIndices=&cache;h.Device.ShaderFloat64=true;
    R::EditorOutlierAnalysisResult failure;
    h.Config.Score.ValueKind=Geometry::PropertyValueKind::Double;
    EXPECT_FALSE(R::StartEditorOutlierAnalysisTransaction(h.Commands(),h.Config,failure));
    EXPECT_NE(failure.Message.find("float score"),std::string::npos);
    h.Config.Score.ValueKind=Geometry::PropertyValueKind::Float;
    h.Rows().Get<glm::vec3>("v:position")[0]={std::numeric_limits<float>::denorm_min(),0,0};
    EXPECT_FALSE(R::StartEditorOutlierAnalysisTransaction(h.Commands(),h.Config,failure));
    EXPECT_NE(failure.Message.find("subnormal positions"),std::string::npos);
}

TEST(OutlierTransaction, FloatSubnormalRadiusSquaredPassesNumericalAdmission)
{
    Harness h; R::SpatialIndexCache cache;
    h.Context.SpatialIndices=&cache; h.Device.ShaderFloat64=true;
    h.Rows().Resize(2);
    h.Rows().Get<glm::vec3>("v:position")[1]={2e-20f,0,0};
    const float boundary=std::sqrt(std::numeric_limits<float>::min());
    for (const float radius : {1e-20f, std::nextafter(boundary,0.f), boundary})
    {
        h.Config.Radius=radius;
        const auto preview=R::PreviewEditorOutlierAnalysisCommand(h.Commands(),h.Config);
        R::EditorOutlierAnalysisResult failure;
        EXPECT_FALSE(R::StartEditorOutlierAnalysisTransaction(h.Commands(),h.Config,failure));
        // Double radius squares are normal: 1e-40 is ~268 orders above DBL_MIN.
        EXPECT_EQ(preview.DisabledReason.find("radius squared"),std::string::npos);
        EXPECT_NE(failure.Message.find("spatial cache and job service"),std::string::npos);
    }
}

TEST(OutlierTransaction, RadiusAdmissionDoesNotChargeUnusedKNeighbors)
{
    Harness h; R::SpatialIndexCache cache;
    h.Context.SpatialIndices=&cache; h.Device.ShaderFloat64=true;
    R::CommandBus commands;
    R::KernelEventBus events;
    R::WorldRegistry worlds;
    R::ServiceRegistry services;
    services.BeginRegistration();
    ASSERT_TRUE(services.Provide<Extrinsic::RHI::IDevice>(h.Device,"test").has_value());
    R::EngineSetup setup{commands,events,h.Jobs.Jobs(),worlds,services,[](R::FramePhase,R::RuntimeFrameHook){}};
    ASSERT_TRUE(cache.OnRegister(setup).has_value());
    h.Config.KNeighbors=std::numeric_limits<std::uint32_t>::max();
    const auto preview=R::PreviewEditorOutlierAnalysisCommand(h.Commands(),h.Config);
    EXPECT_TRUE(preview.Enabled)<<preview.DisabledReason;
    R::RuntimeModuleShutdownContext shutdown{commands,events,h.Jobs.Jobs(),worlds,services};
    cache.OnShutdown(shutdown);
}

TEST(OutlierTransaction, NeighborhoodWidthMatchesEachMethodAndClampsSmallInputs)
{
    EXPECT_EQ(G::OutlierNeighborWidth(0,64,100),65u);
    EXPECT_EQ(G::OutlierNeighborWidth(1,64,1u<<20),1u);
    EXPECT_EQ(G::OutlierNeighborWidth(2,63,100),64u);
    EXPECT_EQ(G::OutlierNeighborWidth(2,1,100),3u);
    EXPECT_EQ(G::OutlierNeighborWidth(2,63,2),2u);
    EXPECT_EQ(G::OutlierNeighborWidth(0,~0u,5),5u);
}

TEST(OutlierTransaction, InitialSubmissionRejectsWithoutCallback)
{
    Harness h; auto& Context=h.Context; auto& Device=h.Device; auto& Jobs=h.Jobs;
    R::SpatialIndexCache cache;Context.SpatialIndices=&cache;Device.ShaderFloat64=true;
    R::CommandBus commands;R::KernelEventBus events;R::WorldRegistry worlds;R::ServiceRegistry services;
    Context.World=worlds.CreateWorld("initial-rejection");Context.Scene=worlds.Get(Context.World);
    const auto entity=Intrinsic::Tests::MakePointDomainSource(*Context.Scene,R::GeometryElementDomain::PointCloudPoint);
    (void)Intrinsic::Tests::PointDomainProperties(*Context.Scene,entity,R::GeometryElementDomain::PointCloudPoint).GetOrAdd<glm::vec3>("v:position",glm::vec3{0});
    h.Config.StableEntityId=R::SelectionController::ToStableEntityId(entity);
    services.BeginRegistration();
    ASSERT_TRUE(services.Provide<Extrinsic::RHI::IDevice>(Device,"test").has_value());
    R::EngineSetup setup{commands,events,Jobs.Jobs(),worlds,services,[](R::FramePhase,R::RuntimeFrameHook){}};
    ASSERT_TRUE(cache.OnRegister(setup).has_value());
    unsigned callbacks=0,submissions=0;
    Context.JobCommands.Submit=[&](R::JobDesc,R::EditorJobIdentity){++submissions;return R::JobToken{};};
    const auto result=R::ApplyEditorOutlierAnalysisCommand(h.Commands(),h.Config,[&](auto){++callbacks;});
    EXPECT_EQ(result.Status,R::EditorCommandStatus::GeometryProcessingFailed);
    EXPECT_EQ(submissions,1u);EXPECT_EQ(callbacks,0u); // One terminal outcome, returned directly.
    R::RuntimeModuleShutdownContext shutdown{commands,events,Jobs.Jobs(),worlds,services};cache.OnShutdown(shutdown);
}

TEST(OutlierTransaction, ReplacedRingRefusesAcceptAndDiscardPreservesReplacement)
{
    for(unsigned slot=0;slot<3;++slot){
        Harness h;auto run=h.Ready();ASSERT_TRUE(run);
        const auto ref=slot==0?h.Config.Score:slot==1?h.Config.Mask:R::GpuPropertyPresentationRef(h.Config.Mask);
        const auto key=h.Key(ref);
        ASSERT_TRUE(h.Residency.Discard(key,h.Residency.RingGeneration(key)));
        auto replacement=R::AcquireGpuPropertyOutput(h.Residency,h.Context.World,h.Entity,ref,
            std::uint32_t(h.Rows().Size()),3);
        ASSERT_TRUE(replacement);const auto generation=h.Residency.RingGeneration(key);
        EXPECT_FALSE(R::SnapshotEditorOutlierAnalysis(h.Commands(),run).CanAccept);
        EXPECT_EQ(R::AcceptEditorOutlierAnalysis(h.Commands(),run).Status,R::EditorCommandStatus::StaleEntity);
        R::DiscardEditorOutlierAnalysis(h.Commands(),run);
        EXPECT_EQ(h.Residency.RingGeneration(key),generation);
    }
}
