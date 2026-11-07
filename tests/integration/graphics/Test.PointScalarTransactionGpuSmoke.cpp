// Three scalar kernels through the real resident transaction, including deleted rows.
#include "RuntimeTestModule.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <sstream>
#include <iomanip>
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
import Extrinsic.Runtime.PointFieldOperations;
import Geometry.PointCloud.Utils;
import Geometry.PointCloud.Kernels;
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
    class ScalarApp final:public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        R::EditorProcessingContext Context{};
        R::EditorCommandHistory History{};
        // The density weights kernel (method 2) as its plugin starts it (ARCH-022 Q90: no engine method starts one).
        R::GeometryPropertyRef Positions{.Domain = R::GeometryElementDomain::PointCloudPoint, .Name = "v:position",
                                         .ValueKind = Geometry::PropertyValueKind::Vec3};
        R::GeometryPropertyRef Output{.Domain = R::GeometryElementDomain::PointCloudPoint, .Name = "scalar",
                                      .ValueKind = Geometry::PropertyValueKind::Float};
        std::uint32_t Id{};
        double SupportRadius{0.6};
        Geometry::PointCloud::Kernels::KernelType WeightKernel{};
        Geometry::PointCloud::Kernels::DensityWeightMode WeightMode{};
        R::EditorPointScalarTransactionHandle Run{};
        entt::entity Entity{};
        int Step{},Method{2}; // 2..7: the density weight variants (0, 1 are plugins)
        bool Done{};int Boundary{};
        unsigned Starts{};
        R::SpatialIndexCacheStats Final{};
        std::vector<float> Saved{},Reference{};
        std::vector<glm::vec3> Points{};
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
            for(std::size_t i=0;i<66;++i)Points.push_back(positions[i]);
            Rows().GetOrAdd<bool>("v:deleted",false)[66]=true;
            const auto id=R::SelectionController::ToStableEntityId(Entity);
            Id=id;
            Rows().GetOrAdd<float>("scalar",0)[66]=123;
        }
        void Fail(const std::string& message){ADD_FAILURE()<<message;Kernel().RequestExit();}
        void Start()
        {
            std::string why;R::EditorPointScalarTransactionSnapshot refused;
            const auto query=Geometry::PointCloud::Kernels::ConservativeQueryRadius(SupportRadius);
            if(!query){Fail("no query radius");return;}
            Run=R::MakeEditorPointScalarTransactionForTest(Commands(),Id,Positions,Output,{},nullptr,{},
                {.Method=2,.Capacity=256,.Kernel=std::uint32_t(WeightKernel),.Inverse=std::uint32_t(WeightMode),.QueryRadius=*query,
                 .SupportRadius=SupportRadius},&refused);why=refused.Message;
            if(!Run)Fail(why);else ++Starts;
        }
        void Frame(double,double) override
        {
            if(std::chrono::steady_clock::now()-Started>std::chrono::seconds(120)){Fail("Scalar smoke timed out");return;}
            if(!Kernel().GetDevice().IsOperational()||!Context.SpatialIndices)return;
            if(Step==0){
                if(Boundary){
                    const float separation=1e-20f;
                    Points={{0,0,0},{separation,0,0}};
                    auto p=Rows().Get<glm::vec3>("v:position");p[0]=Points[0];p[1]=Points[1];
                    auto deleted=Rows().Get<bool>("v:deleted");for(unsigned j=2;j<67;++j)deleted[j]=true;
                    SupportRadius=2e-20;
                }
                {WeightKernel=Geometry::PointCloud::Kernels::KernelType((Method-2)/2);WeightMode=Geometry::PointCloud::Kernels::DensityWeightMode((Method-2)%2);
                    auto r=Geometry::PointCloud::Kernels::ComputeDensityWeights(Points,SupportRadius,WeightKernel,WeightMode);if(!r.Succeeded()){Fail("CPU weights failed");return;}Reference=r.Weights;}
                Start();Step=1;return;
            }
            const auto snapshot=R::SnapshotEditorPointScalar(Commands(),Run);
            if(snapshot.Phase==Phase::Failed||snapshot.Phase==Phase::Discarded){Fail(snapshot.Message);return;}
            if(Step==1){
                if(snapshot.Phase!=Phase::ReadyToAccept)return;
                EXPECT_EQ(std::as_const(Rows()).Get<float>("scalar")[66],123);
                const auto result=R::AcceptEditorPointScalar(Commands(),Run);
                if(result.Status!=R::EditorCommandStatus::Pending){Fail(result.Message);return;}
                Step=2;return;
            }
            if(Step==2){
                if(snapshot.Phase!=Phase::Applied)return;
                Saved=std::as_const(Rows()).Get<float>("scalar").Vector();double delta=0;
                for(std::size_t i=0;i<Points.size();++i){EXPECT_TRUE(std::isfinite(Saved[i]));delta=std::max(delta,std::abs(double(Saved[i])-Reference[i]));}
                // Double expressions round only at publication. Relative checks on the tiny KDE
                // fixture must reject a flushed-to-zero result (an absolute 2e-5 would not).
                for(std::size_t i=0;i<Points.size();++i)
                    EXPECT_NEAR(Saved[i],Reference[i],std::abs(double(Reference[i]))*2e-6);
                std::ostringstream measured;measured<<std::setprecision(17)<<delta;
                const std::string name=(Boundary?"boundary_":"")+std::string("density_weights_variant_"+std::to_string(Method-2));
                ::testing::Test::RecordProperty(name+"_max_abs_delta",measured.str());
                EXPECT_LE(delta,2e-5);EXPECT_EQ(Saved[66],123);EXPECT_EQ(snapshot.CpuStageReadbackBytes,67u*4u);
                Start();Step=3;return;
            }
            if(Step==3){
                if(snapshot.Phase!=Phase::ReadyToAccept)return;
                EXPECT_EQ(snapshot.GpuInputUploadBytes,0u);
                R::DiscardEditorPointScalar(Commands(),Run);EXPECT_EQ(std::as_const(Rows()).Get<float>("scalar").Vector(),Saved);
                Run.reset();
                // ARCH-022 Q89: methods 0 and 1 (kernel density, point spacing) are plugins; their parity and
                // boundary runs are in KernelDensityPluginGpuSmoke and PointSpacingPluginGpuSmoke.
                if(++Method==8){
                    if(++Boundary==2){Done=true;Final=Context.SpatialIndices->Stats();Kernel().RequestExit();return;}
                    Method=2;}
                Step=0;
            }
        }
        void Shutdown() override {R::DiscardEditorPointScalar({},Run);Run.reset();Context={};}
    };
    // RUNTIME-350: a GPU start whose shared index snapshot does not match the captured samples
    // refuses with a reason; it refused with an empty message.
    class MismatchApp final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        R::EditorProcessingContext Context{};
        Extrinsic::ECS::Scene::Registry Captured{};
        std::uint32_t Id{}; // a GPU scalar start of the density weights kernel (ARCH-022 Q90: a plugin's)
        std::optional<std::string> Refused{};
        bool Started{};
        static void Fill(Extrinsic::ECS::Scene::Registry& scene, entt::entity entity, float shift)
        {
            auto& rows = scene.Raw().emplace<GS::Vertices>(entity).Properties;
            rows.Resize(16);
            auto positions = rows.GetOrAdd<glm::vec3>("v:position");
            for (std::size_t i = 0; i < 16; ++i)
                positions[i] = {float(i % 4) * 0.25f + shift, float(i / 4) * 0.25f, 0};
        }
        void Resolve() override
        {
            auto& indexed = *Kernel().Worlds().Get(Kernel().ActiveWorld());
            const auto entity = indexed.Create();
            Fill(indexed, entity, 0);
            // The captured scene has the same entity id with shifted positions.
            for (auto e = Captured.Create(); e != entity; e = Captured.Create())
                if (entt::to_integral(e) > entt::to_integral(entity))
                {
                    ADD_FAILURE() << "entity ids differ";
                    return;
                }
            Fill(Captured, entity, 1);
            Context.Scene = &Captured;
            Context.World = Kernel().ActiveWorld();
            Context.Device = &Kernel().GetDevice();
            Context.SpatialIndices = Kernel().Services().Find<R::SpatialIndexCache>();
            Context.JobCommands.Submit = [this](R::JobDesc job, R::EditorJobIdentity)
            {
                return Kernel().Jobs().Submit(std::move(job));
            };
            Id = R::SelectionController::ToStableEntityId(entity);
        }
        void Frame(double, double) override
        {
            if (Started || !Kernel().GetDevice().IsOperational() || !Context.SpatialIndices)
                return;
            Started = true;
            R::EditorPointScalarTransactionSnapshot why;
            const auto domain = R::GeometryElementDomain::PointCloudPoint;
            const auto run = R::MakeEditorPointScalarTransactionForTest(R::BindEditorProcessingCommands(Context), Id,
                {domain, "v:position", Geometry::PropertyValueKind::Vec3},
                {domain, "scalar", Geometry::PropertyValueKind::Float}, {}, nullptr, {},
                {.Method = 2, .Capacity = 256, .Kernel = 1, .QueryRadius = 0.6f, .SupportRadius = 0.6}, &why);
            EXPECT_FALSE(run);
            Refused = why.Message;
            Kernel().RequestExit();
        }
        void Shutdown() override
        {
            Context = {};
        }
    };
}
TEST(RUNTIME298PointScalarResidency,ParityResidentSecondRunAndDiscard)
{
    if(!Extrinsic::Platform::Backends::Glfw::CanInitialize())GTEST_SKIP()<<"GLFW unavailable";
    auto config=R::CreateReferenceEngineConfig();config.Window.Width=128;config.Window.Height=128;
    config.Render.EnableValidation=true;config.Render.EnableVSync=false;config.ReferenceScene.Enabled=false;
    auto app=std::make_unique<ScalarApp>();auto* run=app.get();
    Intrinsic::Tests::RuntimeTestKernel engine(config,std::move(app));engine.EmplaceModule<R::SpatialIndexCache>();engine.Initialize();
    struct Shutdown { R::Engine& Engine;~Shutdown(){Engine.Shutdown();} } shutdown{engine};
    if(!engine.GetDevice().SupportsShaderFloat64())GTEST_SKIP()<<"Shader float64 unavailable";
    engine.Run();ASSERT_TRUE(engine.GetDevice().IsOperational());ASSERT_TRUE(run->Done)<<"step "<<run->Step;
    // Every run leased the scalar workspace from the spatial cache; later runs of every method,
    // count and width reuse a retired one and still match the CPU reference above.
    EXPECT_EQ(run->Final.WorkspaceLeases,run->Starts);
    EXPECT_GT(run->Final.WorkspaceReuses,0u);
}
TEST(RUNTIME350IndexMismatch, GpuStartExplainsAMismatchedSnapshot)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize())
        GTEST_SKIP() << "GLFW unavailable";
    auto config = R::CreateReferenceEngineConfig();
    config.Window.Width = 128;
    config.Window.Height = 128;
    config.Render.EnableValidation = true;
    config.Render.EnableVSync = false;
    config.ReferenceScene.Enabled = false;
    auto app = std::make_unique<MismatchApp>();
    auto* run = app.get();
    Intrinsic::Tests::RuntimeTestKernel engine(config, std::move(app));
    engine.EmplaceModule<R::SpatialIndexCache>();
    engine.Initialize();
    struct Shutdown
    {
        R::Engine& Engine;
        ~Shutdown()
        {
            Engine.Shutdown();
        }
    } shutdown{engine};
    if (!engine.GetDevice().SupportsShaderFloat64())
        GTEST_SKIP() << "Shader float64 unavailable";
    engine.Run();
    ASSERT_TRUE(engine.GetDevice().IsOperational());
    ASSERT_TRUE(run->Refused);
    EXPECT_EQ(*run->Refused, "Spatial index snapshot does not match the selected samples.");
}
