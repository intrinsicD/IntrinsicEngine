#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>
#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>
#include <gtest/gtest.h>
#include "MockRHI.hpp"
#include "PointDomainFixture.hpp"
#include "SandboxEditorJobHarness.hpp"
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.PointFieldOperations;
import Extrinsic.Runtime.PointSamplingOperations;
import Extrinsic.Runtime.MeshFieldOperations;
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
#include "Editor/internal/Runtime.EditorPointInputReadiness.hpp"
namespace R=Extrinsic::Runtime;
namespace G=Extrinsic::Graphics;
namespace
{
    // The entity and the scalar output the cases name besides the method's config (kernel density's fields before it
    // became a plugin, ARCH-022 Q89).
    struct ScalarTarget
    {
        std::uint32_t StableEntityId{};
        R::GeometryPropertyRef Positions{.Domain = R::GeometryElementDomain::Unknown, .Name = "v:position",
                                         .ValueKind = Geometry::PropertyValueKind::Vec3};
        R::GeometryPropertyRef Density{.Domain = R::GeometryElementDomain::Unknown, .Name = "density",
                                       .ValueKind = Geometry::PropertyValueKind::Float};
    };
    class PointScalarTransaction:public ::testing::TestWithParam<int>
    {
    public:
        Extrinsic::ECS::Scene::Registry Scene;
        R::EditorCommandHistory History;
        Extrinsic::Tests::MockDevice Device;
        G::GpuPropertyResidency Residency{Device};
        Extrinsic::Tests::EditorJobHarness Jobs;
        R::EditorProcessingContext Context;
        ScalarTarget Density;
        entt::entity Entity;
        void SetUp() override
        {
            Entity=Intrinsic::Tests::MakePointDomainSource(Scene,R::GeometryElementDomain::PointCloudPoint);
            const auto id=R::SelectionController::ToStableEntityId(Entity);
            Density.StableEntityId=id;
            Density.Positions.Domain=R::GeometryElementDomain::PointCloudPoint;
            Density.Density.Domain=R::GeometryElementDomain::PointCloudPoint;
            Density.Density.Name="result";
            (void)Rows().GetOrAdd<glm::vec3>("v:position",glm::vec3{0});
            Context.Scene=&Scene;Context.CommandHistory=&History;Context.Device=&Device;
            Device.TransferQueue.AcceptBufferUploads=true;Jobs.Attach(Context);
        }
        auto Commands(){return R::BindEditorProcessingCommands(Context);}
        Geometry::PropertySet& Rows(){return Intrinsic::Tests::PointDomainProperties(Scene,Entity,Density.Positions.Domain);}
        auto Key(){return R::MakeGpuPropertyKey(Context.World,Entity,Density.Density);}
        auto Ready(float value = 3.0f)
        {
            std::vector<float> values(Rows().Size(),value);
            return R::MakeEditorPointScalarTransactionForTest(Commands(),Density.StableEntityId,Density.Positions,
                                                              Density.Density,values,&Residency);
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

TEST_P(PointScalarTransaction, SuccessiveEditsReplayAndRejectForeignOutputChanges)
{
    for (const bool existing : {true, false})
    {
        SCOPED_TRACE(existing);
        History.ClearHistory();
        if (auto old = Rows().Get<float>("result")) Rows().Remove(old);
        if (existing) (void)Rows().GetOrAdd<float>("result", 1.0f);
        const auto values = [&] { return std::as_const(Rows()).Get<float>("result").Vector(); };
        for (const auto value : {3.0f, 7.0f})
        {
            const auto run = Ready(value);
            ASSERT_TRUE(run);
            ASSERT_EQ(R::AcceptEditorPointScalar(Commands(), run).Status, R::EditorCommandStatus::Pending);
            ASSERT_TRUE(Jobs.DrainUntilTerminal());
            ASSERT_EQ(R::SnapshotEditorPointScalar(Commands(), run).Phase, R::EditorGpuTransactionPhase::Applied);
        }
        ASSERT_TRUE(History.Undo().Succeeded());
        EXPECT_EQ(values(), std::vector<float>(Rows().Size(), 3.0f));
        ASSERT_TRUE(History.Undo().Succeeded());
        if (existing) EXPECT_EQ(values(), std::vector<float>(Rows().Size(), 1.0f));
        else EXPECT_FALSE(Rows().Exists("result"));
        ASSERT_TRUE(History.Redo().Succeeded());
        EXPECT_EQ(values(), std::vector<float>(Rows().Size(), 3.0f));
        ASSERT_TRUE(History.Redo().Succeeded());
        EXPECT_EQ(values(), std::vector<float>(Rows().Size(), 7.0f));
        Rows().Get<float>("result")[0] = 13.0f;
        EXPECT_EQ(History.Undo().Status, R::EditorCommandHistoryStatus::StaleEntity);
        EXPECT_EQ(History.UndoCount(), 2u);
        EXPECT_EQ(values()[0], 13.0f);
    }

    // A later legitimate command must not legitimize an older foreign replacement.
    History.ClearHistory();
    if (auto old = Rows().Get<float>("result")) Rows().Remove(old);
    (void)Rows().GetOrAdd<float>("result", 1.0f);
    unsigned invalidations = 0;
    Context.InvalidateWorkspaceSnapshotCache = [&] { ++invalidations; };
    for (const auto value : {3.0f, 7.0f, 11.0f})
    {
        if (value == 11.0f)
        {
            const auto previousId = Rows().Registry().Find("result");
            if (auto old = Rows().Get<float>("result")) Rows().Remove(old);
            (void)Rows().GetOrAdd<float>("result", 7.0f);
            ASSERT_NE(Rows().Registry().Find("result"), previousId);
        }
        const auto run = Ready(value);
        ASSERT_TRUE(run);
        ASSERT_EQ(R::AcceptEditorPointScalar(Commands(), run).Status, R::EditorCommandStatus::Pending);
        ASSERT_TRUE(Jobs.DrainUntilTerminal());
        ASSERT_EQ(R::SnapshotEditorPointScalar(Commands(), run).Phase, R::EditorGpuTransactionPhase::Applied);
    }
    ASSERT_TRUE(History.Undo().Succeeded()); // C restores the foreign B-valued column.
    const auto unchanged = std::as_const(Rows()).Get<float>("result").Vector();
    ASSERT_EQ(unchanged, std::vector<float>(Rows().Size(), 7.0f));
    invalidations = 0;
    EXPECT_EQ(History.Undo().Status, R::EditorCommandHistoryStatus::StaleEntity);
    EXPECT_EQ(History.UndoCount(), 2u);
    EXPECT_EQ(History.RedoCount(), 1u);
    EXPECT_EQ(std::as_const(Rows()).Get<float>("result").Vector(), unchanged);
    EXPECT_EQ(invalidations, 0u);

    // Sampling owns the same float column before the scalar command edits it.
    History.ClearHistory();
    if (auto old = Rows().Get<float>("result")) Rows().Remove(old);
    R::PointSamplingOperationConfig sampling;
    sampling.SourceStableEntityId = Density.StableEntityId;
    sampling.Positions = Density.Positions;
    sampling.RankName = "result";
    sampling.Count = 2;
    ASSERT_TRUE(R::ApplyEditorPointSamplingCommand(Commands(), sampling).Succeeded());
    const auto sampled = std::as_const(Rows()).Get<float>("result").Vector();
    const auto sampledId = Rows().Registry().Find("result");
    const auto run = Ready(17.0f);
    ASSERT_TRUE(run);
    ASSERT_EQ(R::AcceptEditorPointScalar(Commands(), run).Status, R::EditorCommandStatus::Pending);
    ASSERT_TRUE(Jobs.DrainUntilTerminal());
    ASSERT_EQ(R::SnapshotEditorPointScalar(Commands(), run).Phase, R::EditorGpuTransactionPhase::Applied);
    ASSERT_TRUE(History.Undo().Succeeded());
    EXPECT_EQ(std::as_const(Rows()).Get<float>("result").Vector(), sampled);
    ASSERT_TRUE(History.Undo().Succeeded());
    EXPECT_FALSE(Rows().Exists("result"));
    ASSERT_TRUE(History.Redo().Succeeded());
    EXPECT_EQ(std::as_const(Rows()).Get<float>("result").Vector(), sampled);
    EXPECT_NE(Rows().Registry().Find("result"), sampledId);
    ASSERT_TRUE(History.Redo().Succeeded());
    EXPECT_EQ(std::as_const(Rows()).Get<float>("result").Vector(), std::vector<float>(Rows().Size(), 17.0f));

    // An independent mesh-field author creates a float mask before B edits it.
    History.ClearHistory();
    Entity = Intrinsic::Tests::MakePointDomainSource(Scene, R::GeometryElementDomain::MeshVertex);
    Density.StableEntityId = R::SelectionController::ToStableEntityId(Entity);
    Density.Positions.Domain = R::GeometryElementDomain::MeshVertex;
    Density.Density.Domain = R::GeometryElementDomain::MeshVertex;
    R::EditorGeodesicsCommand geodesics;
    geodesics.StableEntityId = Density.StableEntityId;
    geodesics.Config.SourceVertices = {0};
    geodesics.Config.SourceMaskProperty = Density.Density;
    ASSERT_TRUE(R::ApplyEditorGeodesicsCommand(Commands(), geodesics).Succeeded());
    const auto sourceMask = std::as_const(Rows()).Get<float>("result").Vector();
    const auto scalar = Ready(19.0f);
    ASSERT_TRUE(scalar);
    ASSERT_EQ(R::AcceptEditorPointScalar(Commands(), scalar).Status, R::EditorCommandStatus::Pending);
    ASSERT_TRUE(Jobs.DrainUntilTerminal());
    ASSERT_EQ(R::SnapshotEditorPointScalar(Commands(), scalar).Phase, R::EditorGpuTransactionPhase::Applied);
    ASSERT_TRUE(History.Undo().Succeeded());
    EXPECT_EQ(std::as_const(Rows()).Get<float>("result").Vector(), sourceMask);
    ASSERT_TRUE(History.Undo().Succeeded());
    EXPECT_FALSE(Rows().Exists("result"));
    ASSERT_TRUE(History.Redo().Succeeded());
    EXPECT_EQ(std::as_const(Rows()).Get<float>("result").Vector(), sourceMask);
    ASSERT_TRUE(History.Redo().Succeeded());
    EXPECT_EQ(std::as_const(Rows()).Get<float>("result").Vector(), std::vector<float>(Rows().Size(), 19.0f));

    // A legitimate mesh-field command must not rebase an older, foreign-replaced scalar slot.
    const auto foreignValues = std::as_const(Rows()).Get<float>("result").Vector();
    const auto previousId = Rows().Registry().Find("result");
    auto old = Rows().Get<float>("result");
    Rows().Remove(old);
    Rows().GetOrAdd<float>("result").Vector() = foreignValues;
    ASSERT_NE(Rows().Registry().Find("result"), previousId);
    const auto newGeodesics = R::ApplyEditorGeodesicsCommand(Commands(), geodesics);
    ASSERT_TRUE(newGeodesics.Succeeded()) << newGeodesics.Message;
    ASSERT_EQ(History.UndoCount(), 3u);
    ASSERT_TRUE(History.Undo().Succeeded());
    ASSERT_EQ(std::as_const(Rows()).Get<float>("result").Vector(), foreignValues);
    invalidations = 0;
    EXPECT_EQ(History.Undo().Status, R::EditorCommandHistoryStatus::StaleEntity);
    EXPECT_EQ(History.UndoCount(), 2u);
    EXPECT_EQ(History.RedoCount(), 1u);
    EXPECT_EQ(std::as_const(Rows()).Get<float>("result").Vector(), foreignValues);
    EXPECT_EQ(invalidations, 0u);

    // A scalar edit followed by outlier analysis retains the legal UndoB -> UndoA prefix.
    History.ClearHistory();
    Entity = Intrinsic::Tests::MakePointDomainSource(Scene, R::GeometryElementDomain::PointCloudPoint);
    Density.StableEntityId = R::SelectionController::ToStableEntityId(Entity);
    Density.Positions.Domain = R::GeometryElementDomain::PointCloudPoint;
    Density.Density.Domain = R::GeometryElementDomain::PointCloudPoint;
    Rows().GetOrAdd<glm::vec3>("v:position").Vector() = {{0, 0, 0}, {.01f, 0, 0}, {.02f, 0, 0}, {10, 0, 0}, {.03f, 0, 0}};
    const auto beforeOutliers = Ready(29.0f);
    ASSERT_TRUE(beforeOutliers);
    ASSERT_EQ(R::AcceptEditorPointScalar(Commands(), beforeOutliers).Status, R::EditorCommandStatus::Pending);
    ASSERT_TRUE(Jobs.DrainUntilTerminal());
    ASSERT_EQ(R::SnapshotEditorPointScalar(Commands(), beforeOutliers).Phase, R::EditorGpuTransactionPhase::Applied);
    const auto scalarValues = std::as_const(Rows()).Get<float>("result").Vector();
    auto direct = Context;
    direct.JobCommands = {};
    R::OutlierAnalysisConfig outliers;
    outliers.StableEntityId = Density.StableEntityId;
    outliers.Positions = Density.Positions;
    outliers.Mask = {R::GeometryElementDomain::PointCloudPoint, "outlier_mask", Geometry::PropertyValueKind::UInt32};
    outliers.Score = Density.Density;
    outliers.KNeighbors = 2;
    const auto analyzed = R::ApplyEditorOutlierAnalysisCommand(R::BindEditorProcessingCommands(direct), outliers);
    ASSERT_TRUE(analyzed.Succeeded()) << analyzed.Message;
    ASSERT_NE(std::as_const(Rows()).Get<float>("result").Vector(), scalarValues);
    ASSERT_EQ(History.UndoCount(), 2u);
    ASSERT_TRUE(History.Undo().Succeeded());
    EXPECT_EQ(std::as_const(Rows()).Get<float>("result").Vector(), scalarValues);
    EXPECT_TRUE(History.Undo().Succeeded());
    EXPECT_FALSE(Rows().Exists("result"));
    EXPECT_EQ(History.UndoCount(), 0u);
    EXPECT_EQ(History.RedoCount(), 2u);
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
// ARCH-022 Q89/Q90: kernel density (0), point spacing (1) and density weights (2) are plugins; their admission and
// run cases are the plugins' (Test.*Plugin). The transaction's own cases run on a test transaction with method 2.
INSTANTIATE_TEST_SUITE_P(ThreeMethods,PointScalarTransaction,::testing::Values(2));
TEST_P(PointScalarTransaction,DeletionChangeRefusesAccept)
{
    auto run=Ready();ASSERT_TRUE(run);Rows().GetOrAdd<bool>("v:deleted",false)[0]=true;
    EXPECT_FALSE(R::SnapshotEditorPointScalar(Commands(),run).CanAccept);
    EXPECT_EQ(R::AcceptEditorPointScalar(Commands(),run).Status,R::EditorCommandStatus::StaleEntity);
    R::DiscardEditorPointScalar(Commands(),run);EXPECT_FALSE(Rows().Exists("result"));
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
// A device run whose initial submission is rejected fails (not an invalid request) and holds no ring (restored from the
// density weights operation's InitialSubmissionRejectsWithoutCallback, ARCH-022 Q90).
TEST_P(PointScalarTransaction, ARejectedInitialSubmissionFailsAndHoldsNoRing)
{
    R::SpatialIndexCache cache;Context.SpatialIndices=&cache;Device.ShaderFloat64=true;
    R::CommandBus commands;R::KernelEventBus events;R::WorldRegistry worlds;R::ServiceRegistry services;
    Context.World=worlds.CreateWorld("initial-rejection");Context.Scene=worlds.Get(Context.World);
    const auto entity=Intrinsic::Tests::MakePointDomainSource(*Context.Scene,R::GeometryElementDomain::PointCloudPoint);
    auto& rows=Intrinsic::Tests::PointDomainProperties(*Context.Scene,entity,R::GeometryElementDomain::PointCloudPoint);
    (void)rows.GetOrAdd<glm::vec3>("v:position",glm::vec3{0});
    Density.StableEntityId=R::SelectionController::ToStableEntityId(entity);
    services.BeginRegistration();
    ASSERT_TRUE(services.Provide<Extrinsic::RHI::IDevice>(Device,"test").has_value());
    R::EngineSetup setup{commands,events,Jobs.Jobs(),worlds,services,[](R::FramePhase,R::RuntimeFrameHook){}};
    ASSERT_TRUE(cache.OnRegister(setup).has_value());
    unsigned submissions=0;
    Context.JobCommands.Submit=[&](R::JobDesc,R::EditorJobIdentity){++submissions;return R::JobToken{};};
    R::EditorPointScalarTransactionSnapshot refused;
    const auto run=R::MakeEditorPointScalarTransactionForTest(Commands(),Density.StableEntityId,Density.Positions,
        Density.Density,{},nullptr,{},{.Method=2,.Capacity=256,.Kernel=1,.QueryRadius=1.f,.SupportRadius=1},&refused);
    EXPECT_FALSE(run);
    EXPECT_EQ(refused.Status,R::EditorCommandStatus::GeometryProcessingFailed)<<refused.Message;
    EXPECT_NE(refused.Message.find("submission was rejected"),std::string::npos)<<refused.Message;
    EXPECT_EQ(submissions,1u);
    EXPECT_FALSE(rows.Exists("result"));
    EXPECT_FALSE(cache.PropertyResidency()->HasRing(R::MakeGpuPropertyKey(Context.World,entity,Density.Density)));
    R::RuntimeModuleShutdownContext shutdown{commands,events,Jobs.Jobs(),worlds,services};cache.OnShutdown(shutdown);
}

// A run whose Accept cannot be submitted fails, delivers once and publishes nothing.
TEST_P(PointScalarTransaction, ARejectedAcceptSubmissionFailsAndDeliversOnce)
{
    Context.JobCommands.Submit=[](R::JobDesc,R::EditorJobIdentity){return R::JobToken{};};
    auto run=Ready();ASSERT_TRUE(run);unsigned calls=0;
    (void)R::AcceptEditorPointScalar(Commands(),run,[&](const R::EditorPointScalarTransactionSnapshot& s){
        ++calls;EXPECT_EQ(s.Phase,R::EditorGpuTransactionPhase::Failed);});
    const auto state=R::SnapshotEditorPointScalar(Commands(),run);
    EXPECT_EQ(state.Phase,R::EditorGpuTransactionPhase::Failed);
    EXPECT_NE(state.Message.find("submission was rejected"),std::string::npos)<<state.Message;
    EXPECT_EQ(calls,1u);EXPECT_FALSE(Rows().Exists("result"));EXPECT_FALSE(Residency.HasRing(Key()));
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

TEST_P(PointScalarTransaction, DestroyedWorldIsCheckedBeforeSceneAccess)
{
    R::WorldRegistry worlds;R::CommandBus bus;
    Context.World=worlds.CreateWorld("scalar source");Context.Scene=worlds.Get(Context.World);
    Context.PointInputReadiness=R::MakeEditorPointInputReadiness(worlds,&bus,&Jobs.Jobs());
    Entity=Intrinsic::Tests::MakePointDomainSource(*Context.Scene,R::GeometryElementDomain::PointCloudPoint);
    auto& rows=Intrinsic::Tests::PointDomainProperties(*Context.Scene,Entity,R::GeometryElementDomain::PointCloudPoint);
    (void)rows.GetOrAdd<glm::vec3>("v:position",glm::vec3{0});
    Density.StableEntityId=R::SelectionController::ToStableEntityId(Entity);
    auto run=R::MakeEditorPointScalarTransactionForTest(Commands(),Density.StableEntityId,Density.Positions,
                                                        Density.Density,std::vector<float>(rows.Size(),3),&Residency);
    ASSERT_TRUE(run);
    worlds.Clear(); // Captured context deliberately retains the dead Scene address.
    ASSERT_EQ(worlds.Get(Context.World),nullptr);
    EXPECT_FALSE(R::SnapshotEditorPointScalar(Commands(),run).CanAccept);
    EXPECT_EQ(R::AcceptEditorPointScalar(Commands(),run).Status,R::EditorCommandStatus::StaleEntity);
    R::DiscardEditorPointScalar(Commands(),run);
}


// RUNTIME-311: the scalar transaction runs on the shared GPU transaction lifecycle. A second
// Accept while the first is under way answers Pending without a sink (before:
// InvalidProcessingParameters) and is refused with one, whose sink is never called;
// cancelling the Accept stage through the editor job surface delivers exactly once, as stale,
// publishes nothing and releases the ring; a later Discard changes nothing.
TEST_P(PointScalarTransaction, AcceptUnderWayIsPendingAndItsCancelDeliversOnce)
{
    auto run=Ready();ASSERT_TRUE(run);
    unsigned delivered=0;R::EditorPointScalarTransactionSnapshot last;
    ASSERT_EQ(R::AcceptEditorPointScalar(Commands(),run,[&](auto s){++delivered;last=s;}).Status,R::EditorCommandStatus::Pending);
    const auto again=R::AcceptEditorPointScalar(Commands(),run);
    EXPECT_EQ(again.Status,R::EditorCommandStatus::Pending)<<again.Message;
    EXPECT_EQ(again.Phase,R::EditorGpuTransactionPhase::Accepting);
    EXPECT_EQ(R::SnapshotEditorPointScalar(Commands(),run).AcceptRefusalReason,"Accept is already under way.");
    // A caller with its own sink is refused: the result goes to the first caller only.
    unsigned second=0;
    EXPECT_EQ(R::AcceptEditorPointScalar(Commands(),run,[&](auto){++second;}).Status,R::EditorCommandStatus::InvalidProcessingParameters);
    const auto jobs=Context.JobCommands.SnapshotAll();ASSERT_EQ(jobs.size(),1u);
    EXPECT_EQ(Context.JobCommands.Cancel(jobs.front().Token),R::EditorJobCancelStatus::Requested);
    ASSERT_TRUE(Jobs.DrainUntilTerminal());
    EXPECT_EQ(delivered,1u);
    EXPECT_EQ(last.Status,R::EditorCommandStatus::StaleEntity)<<last.Message;
    EXPECT_EQ(last.Phase,R::EditorGpuTransactionPhase::Discarded);
    EXPECT_FALSE(Rows().Exists("result"));EXPECT_FALSE(Residency.HasRing(Key()));
    R::DiscardEditorPointScalar(Commands(),run);
    EXPECT_EQ(delivered,1u);EXPECT_EQ(second,0u);
    EXPECT_EQ(R::SnapshotEditorPointScalar(Commands(),run).Status,R::EditorCommandStatus::StaleEntity);
}
