// Device scoring, atomic Accept and resident-input reuse, including a deleted row.
#include "RuntimeTestModule.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <gtest/gtest.h>
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.RHI.Device;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.EngineConfigBoot;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.WorldRegistry;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.PointAnalysisOperations;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Core.Config.Engine;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Components.GeometrySources;
import Geometry.Properties;
namespace
{
    namespace R=Extrinsic::Runtime;
    namespace GS=Extrinsic::ECS::Components::GeometrySources;
    using Phase=R::EditorGpuTransactionPhase;
    class OutlierApp final:public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        R::EditorProcessingContext Context{};
        R::EditorCommandHistory History{};
        R::OutlierAnalysisConfig Config{};
        R::EditorOutlierTransactionHandle Run{};
        std::optional<R::EditorOutlierAnalysisResult> Accepted{};
        entt::entity Entity{};
        int Step{},Method{};
        bool Done{},Boundary{};
        std::vector<float> SavedScores{};
        std::vector<std::uint32_t> SavedMask{};
        std::chrono::steady_clock::time_point Started{};
        auto Commands(){return R::BindEditorProcessingCommands(Context);}
        auto& Rows(){return Context.Scene->Raw().get<GS::Vertices>(Entity).Properties;}
        void Resolve() override
        {
            Started=std::chrono::steady_clock::now();
            Context.Scene=Kernel().Worlds().Get(Kernel().ActiveWorld());Context.World=Kernel().ActiveWorld();
            Context.Device=&Kernel().GetDevice();Context.SpatialIndices=Kernel().Services().Find<R::SpatialIndexCache>();
            Context.CommandHistory=&History;
            Context.JobCommands.Submit=[this](R::JobDesc job,R::EditorJobIdentity){return Kernel().Jobs().Submit(std::move(job));};
            Entity=Context.Scene->Create();Context.Scene->Raw().emplace<GS::Vertices>(Entity).Properties.Resize(67);
            auto positions=Rows().GetOrAdd<glm::vec3>("v:position");
            for(std::size_t i=0;i<64;++i)positions[i]={float(i%8)*0.25f,float(i/8)*0.25f,float(i%3)*0.03125f};
            positions[64]={8,9,7};positions[65]={-5,7,-6};positions[66]={500,500,500};
            Rows().GetOrAdd<bool>("v:deleted",false)[66]=true;
            Config.StableEntityId=R::SelectionController::ToStableEntityId(Entity);
            Config.Backend=R::OutlierAnalysisBackend::VulkanLBVH;
            Config.Positions.Domain=Config.Score.Domain=Config.Mask.Domain=R::GeometryElementDomain::PointCloudPoint;
            Rows().GetOrAdd<float>("outlier_score",0)[66]=123;
            Rows().GetOrAdd<float>("cpu_score",0)[66]=123;
            Rows().GetOrAdd<std::uint32_t>("outlier_mask",0)[66]=7;
            Rows().GetOrAdd<std::uint32_t>("cpu_mask",0)[66]=7;
            Config.KNeighbors=8;Config.Radius=0.6f;Config.MinimumNeighbors=3;
        }
        void Fail(const std::string& message){ADD_FAILURE()<<message;Kernel().RequestExit();}
        void Start()
        {
            R::EditorOutlierAnalysisResult failure;Run=R::StartEditorOutlierAnalysisTransaction(Commands(),Config,failure);
            if(!Run)Fail(failure.Message);
        }
        void Frame(double,double) override
        {
            if(std::chrono::steady_clock::now()-Started>std::chrono::seconds(120)){Fail("Outlier smoke timed out");return;}
            if(!Kernel().GetDevice().IsOperational()||!Context.SpatialIndices)return;
            if(Step==0){
                if(Boundary){
                    auto p=Rows().Get<glm::vec3>("v:position");
                    p[0]={0,0,0};p[1]={1e-20f,0,0};p[2]={3e-20f,0,0};
                    auto deleted=Rows().Get<bool>("v:deleted");for(unsigned j=3;j<67;++j)deleted[j]=true;
                    Config.KNeighbors=1;Config.Radius=1.5e-20f;Config.MinimumNeighbors=1;
                }
                Config.Method=R::OutlierAnalysisMethod(Method);
                auto cpu=Config;cpu.Backend=R::OutlierAnalysisBackend::CpuOctree;cpu.Score.Name="cpu_score";cpu.Mask.Name="cpu_mask";
                auto context=Context;context.JobCommands={};
                const auto result=R::ApplyEditorOutlierAnalysisCommand(R::BindEditorProcessingCommands(context),cpu);
                if(!result.Succeeded()){Fail(result.Message);return;}
                // A rejected initial submission returns one terminal result without calling its sink.
                const auto submit=Context.JobCommands.Submit;
                Context.JobCommands.Submit=[](R::JobDesc,R::EditorJobIdentity){return R::JobToken{};};
                unsigned callbacks=0;
                const auto rejected=R::ApplyEditorOutlierAnalysisCommand(Commands(),Config,[&](auto){++callbacks;});
                EXPECT_EQ(rejected.Status,R::EditorCommandStatus::GeometryProcessingFailed);EXPECT_EQ(callbacks,0u);
                Context.JobCommands.Submit=submit;
                Start();Step=1;return;
            }
            const auto snapshot=R::SnapshotEditorOutlierAnalysis(Commands(),Run);
            if(snapshot.Phase==Phase::Failed||snapshot.Phase==Phase::Discarded){Fail(snapshot.Result.Message);return;}
            if(Step==1){
                if(snapshot.Phase!=Phase::ReadyToAccept)return;
                Accepted.reset();
                const auto result=R::AcceptEditorOutlierAnalysis(Commands(),Run,[this](auto r){Accepted=std::move(r);});
                if(result.Status!=R::EditorCommandStatus::Pending){Fail(result.Message);return;}
                Step=2;return;
            }
            if(Step==2){
                if(!Accepted)return;
                if(!Accepted->Succeeded()){Fail(Accepted->Message);return;}
                const auto& rows=std::as_const(Rows());
                SavedScores=rows.Get<float>(Config.Score.Name).Vector();SavedMask=rows.Get<std::uint32_t>(Config.Mask.Name).Vector();
                const auto scores=rows.Get<float>("cpu_score");const auto mask=rows.Get<std::uint32_t>("cpu_mask");
                double delta=0;
                for(std::size_t i=0;i<67;++i){
                    EXPECT_TRUE(std::isfinite(SavedScores[i]));
                    delta=std::max(delta,std::abs(double(SavedScores[i])-scores[i]));
                    EXPECT_EQ(SavedMask[i],mask[i])<<"method "<<Method<<" row "<<i;
                }
                // Double sums and classification precede float publication. Relative checks
                // detect a flushed tiny statistical distance, with exact counts and masks.
                if(Boundary)for(unsigned j=0;j<3;++j)
                    EXPECT_NEAR(SavedScores[j],scores[j],std::abs(double(scores[j]))*2e-6);
                ::testing::Test::RecordProperty(std::string(Boundary?"boundary_outlier_method_":"outlier_method_")+std::to_string(Method)+"_max_abs_delta",std::to_string(delta));
                EXPECT_LE(delta,2e-5);EXPECT_EQ(SavedScores[66],scores[66]);
                Start();Step=3;return;
            }
            if(Step==3){
                if(snapshot.Phase!=Phase::ReadyToAccept)return;
                EXPECT_EQ(snapshot.Result.GpuInputUploadBytes,0u);
                R::DiscardEditorOutlierAnalysis(Commands(),Run);
                EXPECT_EQ(std::as_const(Rows()).Get<float>(Config.Score.Name).Vector(),SavedScores);
                EXPECT_EQ(std::as_const(Rows()).Get<std::uint32_t>(Config.Mask.Name).Vector(),SavedMask);
                Run.reset();if(++Method==3){if(Boundary){Done=true;Kernel().RequestExit();}else{Boundary=true;Method=0;Step=0;}}else Step=0;
            }
        }
        void Shutdown() override {R::DiscardEditorOutlierAnalysis({},Run);Run.reset();Context={};}
    };
}
TEST(RUNTIME297OutlierResidency, ParityResidentSecondRunAndDiscard)
{
    if(!Extrinsic::Platform::Backends::Glfw::CanInitialize())GTEST_SKIP()<<"GLFW unavailable";
    auto config=R::CreateReferenceEngineConfig();config.Window.Width=128;config.Window.Height=128;
    config.Render.EnableValidation=true;config.Render.EnableVSync=false;config.ReferenceScene.Enabled=false;
    auto app=std::make_unique<OutlierApp>();auto* run=app.get();
    Intrinsic::Tests::RuntimeTestKernel engine(config,std::move(app));engine.EmplaceModule<R::SpatialIndexCache>();engine.Initialize();
    struct Shutdown { R::Engine& Engine;~Shutdown(){Engine.Shutdown();} } shutdown{engine};
    if(!engine.GetDevice().SupportsShaderFloat64())GTEST_SKIP()<<"Shader float64 unavailable";
    engine.Run();ASSERT_TRUE(engine.GetDevice().IsOperational());ASSERT_TRUE(run->Done)<<"step "<<run->Step;
}
