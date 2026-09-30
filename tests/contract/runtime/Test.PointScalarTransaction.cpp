#include <cstdint>
#include <limits>
#include <vector>
#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>
#include <gtest/gtest.h>
#include "MockRHI.hpp"
#include "PointDomainFixture.hpp"
#include "SandboxEditorJobHarness.hpp"
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.PointFieldOperations;
import Extrinsic.Runtime.PointAnalysisOperations;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.WorldRegistry;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Runtime.EngineConfigBoot;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.GpuPropertyBinding;
import Extrinsic.Graphics.GpuPropertyResidency;
import Geometry.Properties;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.WorldRegistry;
namespace R=Extrinsic::Runtime;
namespace G=Extrinsic::Graphics;
namespace
{
    class PointScalarTransaction:public ::testing::TestWithParam<int>
    {
    public:
        Extrinsic::ECS::Scene::Registry Scene;
        R::EditorCommandHistory History;
        Extrinsic::Tests::MockDevice Device;
        G::GpuPropertyResidency Residency{Device};
        Extrinsic::Tests::EditorJobHarness Jobs;
        R::EditorProcessingContext Context;
        R::KernelDensityConfig Density;
        R::PointSpacingConfig Spacing;
        R::DensityWeightConfig Weight;
        entt::entity Entity;
        void SetUp() override
        {
            Entity=Intrinsic::Tests::MakePointDomainSource(Scene,R::GeometryElementDomain::PointCloudPoint);
            const auto id=R::SelectionController::ToStableEntityId(Entity);
            Density.StableEntityId=Spacing.StableEntityId=Weight.StableEntityId=id;
            Density.Positions.Domain=Spacing.Positions.Domain=Weight.Positions.Domain=R::GeometryElementDomain::PointCloudPoint;
            Density.Density.Domain=Spacing.Radii.Domain=Weight.Weights.Domain=R::GeometryElementDomain::PointCloudPoint;
            Density.Density.Name=Spacing.Radii.Name=Weight.Weights.Name="result";
            Density.Backend=R::KernelDensityBackend::VulkanLBVH;Spacing.Backend=R::PointSpacingBackend::VulkanLBVH;Weight.Backend=R::DensityWeightBackend::VulkanLBVH;
            (void)Rows().GetOrAdd<glm::vec3>("v:position",glm::vec3{0});
            Context.Scene=&Scene;Context.CommandHistory=&History;Context.Device=&Device;
            Device.TransferQueue.AcceptBufferUploads=true;Jobs.Attach(Context);
        }
        auto Commands(){return R::BindEditorProcessingCommands(Context);}
        Geometry::PropertySet& Rows(){return Intrinsic::Tests::PointDomainProperties(Scene,Entity,R::GeometryElementDomain::PointCloudPoint);}
        auto Key(){return R::MakeGpuPropertyKey(Context.World,Entity,Density.Density);}
        auto Ready()
        {
            std::vector<float> values(Rows().Size(),3);
            if(GetParam()==0)return R::MakeEditorKernelDensityTransactionForTest(Commands(),Density,values,Residency);
            if(GetParam()==1)return R::MakeEditorPointSpacingTransactionForTest(Commands(),Spacing,values,Residency);
            return R::MakeEditorDensityWeightTransactionForTest(Commands(),Weight,values,Residency);
        }
    };
}
TEST_P(PointScalarTransaction,AcceptBindsRevisionAndUndoRestoresRows)
{
    auto run=Ready();ASSERT_TRUE(run);EXPECT_FALSE(Rows().Exists("result"));
    ASSERT_EQ(R::AcceptEditorPointScalar(Commands(),run).Status,R::EditorCommandStatus::Pending);
    ASSERT_TRUE(Jobs.DrainUntilTerminal());
    EXPECT_EQ(R::SnapshotEditorPointScalar(Commands(),run).Phase,R::EditorGpuTransactionPhase::Applied);
    const auto values=std::as_const(Rows()).Get<float>("result");ASSERT_TRUE(values);
    for(std::size_t i=0;i<Rows().Size();++i)EXPECT_EQ(values[i],3);
    const auto front=Residency.Front(Key());ASSERT_TRUE(front);
    const auto before=Residency.Stats().UploadBytes;
    auto input=R::ResolveGpuPropertyInput(Residency,Scene,Context.World,Entity,Density.Density);
    ASSERT_TRUE(input);EXPECT_EQ(input->Buffer,front->Buffer);EXPECT_EQ(Residency.Stats().UploadBytes,before);
    ASSERT_TRUE(History.Undo().Succeeded());EXPECT_FALSE(Rows().Exists("result"));
    ASSERT_TRUE(History.Redo().Succeeded());EXPECT_TRUE(Rows().Exists("result"));
}
TEST_P(PointScalarTransaction,DiscardKeepsRowsAndReleasesRing)
{
    (void)Rows().GetOrAdd<float>("result",7);auto run=Ready();ASSERT_TRUE(run);
    R::DiscardEditorPointScalar(Commands(),run);
    EXPECT_FALSE(Residency.HasRing(Key()));EXPECT_EQ(std::as_const(Rows()).Get<float>("result")[0],7);
    EXPECT_EQ(R::SnapshotEditorPointScalar(Commands(),run).Phase,R::EditorGpuTransactionPhase::Discarded);
}
TEST_P(PointScalarTransaction,ChangedOutputRefusesAccept)
{
    auto run=Ready();ASSERT_TRUE(run);(void)Rows().GetOrAdd<float>("result",7);
    EXPECT_FALSE(R::SnapshotEditorPointScalar(Commands(),run).CanAccept);
    EXPECT_EQ(R::AcceptEditorPointScalar(Commands(),run).Status,R::EditorCommandStatus::StaleEntity);
    R::DiscardEditorPointScalar(Commands(),run);EXPECT_EQ(std::as_const(Rows()).Get<float>("result")[0],7);
}
TEST_P(PointScalarTransaction,ChangedInputRefusesAccept)
{
    auto run=Ready();ASSERT_TRUE(run);Rows().Get<glm::vec3>("v:position")[0]={1,2,3};
    EXPECT_FALSE(R::SnapshotEditorPointScalar(Commands(),run).CanAccept);
    EXPECT_EQ(R::AcceptEditorPointScalar(Commands(),run).Status,R::EditorCommandStatus::StaleEntity);
    R::DiscardEditorPointScalar(Commands(),run);EXPECT_FALSE(Rows().Exists("result"));
}
TEST_P(PointScalarTransaction,MissingDeviceRefusesAdmission)
{
    Context.Device=nullptr;
    if(GetParam()==0)EXPECT_FALSE(R::PreviewEditorKernelDensityCommand(Commands(),Density).Enabled);
    if(GetParam()==1)EXPECT_FALSE(R::PreviewEditorPointSpacingCommand(Commands(),Spacing).Enabled);
    if(GetParam()==2)EXPECT_FALSE(R::PreviewEditorDensityWeightCommand(Commands(),Weight).Enabled);
}
TEST_P(PointScalarTransaction,ConfigRoundTrip)
{
    auto config=R::CreateReferenceEngineConfig();
    if(GetParam()==0){R::SetKernelDensityConfig(config,Density);const auto restored=R::GetKernelDensityConfig(config);ASSERT_TRUE(restored);EXPECT_EQ(R::SerializeKernelDensityConfig(*restored),R::SerializeKernelDensityConfig(Density));}
    if(GetParam()==1){R::SetPointSpacingConfig(config,Spacing);const auto restored=R::GetPointSpacingConfig(config);ASSERT_TRUE(restored);EXPECT_EQ(R::SerializePointSpacingConfig(*restored),R::SerializePointSpacingConfig(Spacing));}
    if(GetParam()==2){R::SetDensityWeightConfig(config,Weight);const auto restored=R::GetDensityWeightConfig(config);ASSERT_TRUE(restored);EXPECT_EQ(R::SerializeDensityWeightConfig(*restored),R::SerializeDensityWeightConfig(Weight));}
}
INSTANTIATE_TEST_SUITE_P(ThreeMethods,PointScalarTransaction,::testing::Values(0,1,2));
TEST_P(PointScalarTransaction,UnsupportedStorageAndSubnormalCoordinatesRefuseAdmission)
{
    R::SpatialIndexCache cache;Context.SpatialIndices=&cache;Device.ShaderFloat64=true;
    const auto preview=[&]{
        if(GetParam()==0)return R::PreviewEditorKernelDensityCommand(Commands(),Density);
        if(GetParam()==1)return R::PreviewEditorPointSpacingCommand(Commands(),Spacing);
        return R::PreviewEditorDensityWeightCommand(Commands(),Weight);};
    Density.Density.ValueKind=Spacing.Radii.ValueKind=Weight.Weights.ValueKind=Geometry::PropertyValueKind::Double;
    auto result=preview();EXPECT_FALSE(result.Enabled);EXPECT_NE(result.DisabledReason.find("float outputs"),std::string::npos);
    Density.Density.ValueKind=Spacing.Radii.ValueKind=Weight.Weights.ValueKind=Geometry::PropertyValueKind::Float;
    Rows().Get<glm::vec3>("v:position")[0]={std::numeric_limits<float>::denorm_min(),0,0};
    result=preview();EXPECT_FALSE(result.Enabled);EXPECT_NE(result.DisabledReason.find("subnormal"),std::string::npos);
}
TEST_P(PointScalarTransaction,DeletionChangeRefusesAccept)
{
    auto run=Ready();ASSERT_TRUE(run);Rows().GetOrAdd<bool>("v:deleted",false)[0]=true;
    EXPECT_FALSE(R::SnapshotEditorPointScalar(Commands(),run).CanAccept);
    EXPECT_EQ(R::AcceptEditorPointScalar(Commands(),run).Status,R::EditorCommandStatus::StaleEntity);
    R::DiscardEditorPointScalar(Commands(),run);EXPECT_FALSE(Rows().Exists("result"));
}

TEST_P(PointScalarTransaction, InitialSubmissionRejectsWithoutCallback)
{
    
    R::SpatialIndexCache cache;Context.SpatialIndices=&cache;Device.ShaderFloat64=true;
    R::CommandBus commands;R::KernelEventBus events;R::WorldRegistry worlds;R::ServiceRegistry services;
    Context.World=worlds.CreateWorld("initial-rejection");Context.Scene=worlds.Get(Context.World);
    const auto entity=Intrinsic::Tests::MakePointDomainSource(*Context.Scene,R::GeometryElementDomain::PointCloudPoint);
    (void)Intrinsic::Tests::PointDomainProperties(*Context.Scene,entity,R::GeometryElementDomain::PointCloudPoint).GetOrAdd<glm::vec3>("v:position",glm::vec3{0});
    Density.StableEntityId=Spacing.StableEntityId=Weight.StableEntityId=R::SelectionController::ToStableEntityId(entity);
    services.BeginRegistration();
    ASSERT_TRUE(services.Provide<Extrinsic::RHI::IDevice>(Device,"test").has_value());
    R::EngineSetup setup{commands,events,Jobs.Jobs(),worlds,services,[](R::FramePhase,R::RuntimeFrameHook){}};
    ASSERT_TRUE(cache.OnRegister(setup).has_value());
    unsigned callbacks=0,submissions=0;
    Context.JobCommands.Submit=[&](R::JobDesc,R::EditorJobIdentity){++submissions;return R::JobToken{};};
    const auto result=GetParam()==0 ? R::ApplyEditorKernelDensityCommand(Commands(),Density,[&](auto){++callbacks;}).Status : GetParam()==1 ? R::ApplyEditorPointSpacingCommand(Commands(),Spacing,[&](auto){++callbacks;}).Status : R::ApplyEditorDensityWeightCommand(Commands(),Weight,[&](auto){++callbacks;}).Status;
    EXPECT_EQ(result,R::EditorCommandStatus::GeometryProcessingFailed);
    EXPECT_EQ(submissions,1u);EXPECT_EQ(callbacks,0u); // One terminal outcome, returned directly.
    R::RuntimeModuleShutdownContext shutdown{commands,events,Jobs.Jobs(),worlds,services};cache.OnShutdown(shutdown);
}

TEST_P(PointScalarTransaction, DoubleSubnormalSupportReachesDeviceAdmission)
{
    if(GetParam()!=2)return;
    R::SpatialIndexCache cache;Context.SpatialIndices=&cache;Device.ShaderFloat64=true;
    Weight.SupportRadius=std::numeric_limits<double>::denorm_min();
    const auto ready=R::PreviewEditorDensityWeightCommand(Commands(),Weight);
    EXPECT_FALSE(ready.Enabled);EXPECT_NE(ready.DisabledReason.find("operational float64 device"),std::string::npos);
}

TEST_P(PointScalarTransaction, AcceptSinkDeliveredExactlyOnce)
{
    auto run=Ready();ASSERT_TRUE(run);unsigned calls=0;
    ASSERT_EQ(R::AcceptEditorPointScalar(Commands(),run,[&](auto result){
        ++calls;EXPECT_EQ(result.Phase,R::EditorGpuTransactionPhase::Applied);
    }).Status,R::EditorCommandStatus::Pending);
    ASSERT_TRUE(Jobs.DrainUntilTerminal());EXPECT_EQ(calls,1u);
    R::DiscardEditorPointScalar(Commands(),run);EXPECT_EQ(calls,1u);
}
TEST_P(PointScalarTransaction, ReplacedRingRefusesAcceptAndDiscardPreservesReplacement)
{
    auto run=Ready();ASSERT_TRUE(run);
    ASSERT_TRUE(Residency.Discard(Key(),Residency.RingGeneration(Key())));
    auto replacement=Ready();ASSERT_TRUE(replacement);
    const auto generation=Residency.RingGeneration(Key());
    EXPECT_FALSE(R::SnapshotEditorPointScalar(Commands(),run).CanAccept);
    EXPECT_EQ(R::AcceptEditorPointScalar(Commands(),run).Status,R::EditorCommandStatus::StaleEntity);
    R::DiscardEditorPointScalar(Commands(),run);
    EXPECT_EQ(Residency.RingGeneration(Key()),generation);
    EXPECT_TRUE(R::SnapshotEditorPointScalar(Commands(),replacement).CanAccept);
    R::DiscardEditorPointScalar(Commands(),replacement);
}
