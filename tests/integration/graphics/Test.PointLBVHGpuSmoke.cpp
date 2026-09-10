#include "RuntimeTestModule.hpp"
#include <algorithm>
#include <bit>
#include <entt/entity/entity.hpp>
#include <glm/glm.hpp>
#include <gtest/gtest.h>
#include <limits>
#include <memory>
#include <iostream>
#include <random>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <nlohmann/json.hpp>
#include <optional>
#include <vector>
import Extrinsic.Graphics.PointLBVH;
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Profiler;
import Extrinsic.RHI.Types;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.EngineConfigBoot;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.WorldRegistry;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Components.GeometrySources;
import Geometry.PointLBVH;
import Geometry.HalfedgeMesh;
import Geometry.Graph;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Extrinsic.Runtime.GeometryProcessingOperations;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.ECS.Component.Transform;
namespace
{
    namespace G = Extrinsic::Graphics;
    namespace RHI = Extrinsic::RHI;
    namespace LB = Geometry::PointLBVH;
    class App final : public Intrinsic::Tests::RuntimeTestModule
    {
      public:
        void Resolve() override
        {
        }
        void Frame(double, double) override
        {
            if (++Frames == 4)
                Kernel().RequestExit();
        }
        void Shutdown() override
        {
        }
        unsigned Frames{};
    };
    struct Shutdown
    {
        Extrinsic::Runtime::Engine& Engine;
        ~Shutdown()
        {
            Engine.Shutdown();
        }
    };
    struct Buffer
    {
        RHI::IDevice& Device;
        RHI::BufferHandle Handle;
        Buffer(RHI::IDevice& d, std::uint64_t size)
            : Device(d), Handle(d.CreateBuffer({.SizeBytes = std::max(size, std::uint64_t(16)),
                                                .Usage = RHI::BufferUsage::Storage |
                                                         RHI::BufferUsage::TransferSrc |
                                                         RHI::BufferUsage::TransferDst,
                                                .HostVisible = true,
                                                .DebugName = "LBVH.Smoke"}))
        {
        }
        ~Buffer()
        {
            if (Handle.IsValid())
                Device.DestroyBuffer(Handle);
        }
    };
    void CheckEntityCache(Extrinsic::Runtime::Engine& engine)
    {
        namespace R = Extrinsic::Runtime;
        namespace GS = Extrinsic::ECS::Components::GeometrySources;
        auto* cache = engine.Services().Find<R::SpatialIndexCache>();
        ASSERT_NE(cache, nullptr);
        auto& device = engine.GetDevice();
        auto world = engine.ActiveWorld();
        auto& scene = *engine.Worlds().Get(world);
        const auto entity = scene.Create();
        auto& properties = scene.Raw().emplace<GS::Vertices>(entity).Properties;
        properties.Resize(3);
        auto positions = properties.GetOrAdd<glm::vec3>("samples");
        positions.Vector() = {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}};
        properties.GetOrAdd<bool>("v:deleted")[1] = true;
        const R::GeometryPropertyRef ref{.Domain = R::GeometryElementDomain::PointCloudPoint,
                                         .Name = "samples",
                                         .ValueKind = Geometry::PropertyValueKind::Vec3};
        auto acquired = cache->Acquire(world, entity, ref);
        ASSERT_TRUE(acquired.Ready());
        Buffer query(device, 16), neighbors(device, 16), headers(device, 16);
        glm::vec3 q{1.1f, 0, 0};
        device.WriteBuffer(query.Handle, &q, 12);
        for (int round = 0; round < 3; ++round)
        {
            if (round == 2)
            {
                positions[2] = {10, 0, 0};
                EXPECT_FALSE(cache->GpuView(acquired.Handle).NodesBDA);
                acquired = cache->Acquire(world, entity, ref);
                ASSERT_TRUE(acquired.Ready());
            }
            RHI::FrameHandle frame;
            ASSERT_TRUE(device.BeginFrame(frame));
            auto& cmd = device.GetGraphicsContext(frame.FrameIndex);
            cmd.Begin();
            const bool recorded =
                cache->RecordGpuQueries(acquired.Handle, cmd,
                                        {.Queries = {.Buffer = query.Handle, .Count = 1},
                                         .Neighbors = neighbors.Handle,
                                         .Headers = headers.Handle});
            cmd.End();
            device.EndFrame(frame);
            device.Present(frame);
            device.WaitIdle();
            ASSERT_TRUE(recorded);
            G::PointLbvhNeighbor result;
            device.ReadBuffer(neighbors.Handle, &result, 8);
            EXPECT_EQ(result.Index, round == 2 ? 0u : 2u);
            EXPECT_EQ(cache->Stats().GpuBuilds, round == 2 ? 2u : 1u);
        }
        scene.Destroy(entity);
        cache->Prune();
        EXPECT_FALSE(cache->GpuView(acquired.Handle).NodesBDA);
    }
    void Check(RHI::IDevice& device, G::PointLbvhWorkspace& tree,
               const std::vector<glm::vec3>& points, const std::vector<glm::vec3>& queries,
               float radius, std::uint32_t capacity, std::uint32_t kNearest = 0,
               const std::vector<std::uint32_t>& exclusions = {})
    {
        Buffer input(device, points.size() * 12), query(device, queries.size() * 12),
            output(device, queries.size() * std::max(capacity, 1u) * 8),
            headers(device, queries.size() * 8), excluded(device, queries.size() * 4);
        ASSERT_TRUE(input.Handle.IsValid());
        ASSERT_TRUE(query.Handle.IsValid());
        ASSERT_TRUE(output.Handle.IsValid());
        ASSERT_TRUE(headers.Handle.IsValid());
        if (!points.empty())
            device.WriteBuffer(input.Handle, points.data(), points.size() * 12);
        device.WriteBuffer(query.Handle, queries.data(), queries.size() * 12);
        if (!exclusions.empty())
        {
            ASSERT_EQ(exclusions.size(), queries.size());
            device.WriteBuffer(excluded.Handle, exclusions.data(), exclusions.size()*4);
        }
        ASSERT_TRUE(tree.Reserve(points.size()));
        RHI::FrameHandle frame;
        ASSERT_TRUE(device.BeginFrame(frame));
        auto& cmd = device.GetGraphicsContext(frame.FrameIndex);
        cmd.Begin();
        const bool built =
            tree.RecordBuild(cmd, {.Buffer = input.Handle, .Count = std::uint32_t(points.size())});
        const bool queried = tree.RecordQuery(
            cmd, {.Queries = {.Buffer = query.Handle, .Count = std::uint32_t(queries.size())},
                  .Neighbors = output.Handle,
                  .Headers = headers.Handle,
                  .Capacity = capacity,
                  .Radius = radius, .KNearestCount = kNearest,
                  .ExcludedIndices = exclusions.empty() ? RHI::BufferHandle{} : excluded.Handle});
        if (kNearest)
        {
            EXPECT_FALSE(tree.RecordQuery(cmd, {.Queries = {.Buffer = query.Handle, .Count = 1},
                .Neighbors = output.Handle, .Headers = headers.Handle, .Capacity = 65, .KNearestCount = 65}));
            EXPECT_FALSE(tree.RecordQuery(cmd, {.Queries = {.Buffer = query.Handle, .Count = 1},
                .Neighbors = output.Handle, .Headers = headers.Handle, .Capacity = 1, .KNearestCount = 2}));
        }
        cmd.End();
        device.EndFrame(frame);
        device.Present(frame);
        device.WaitIdle();
        ASSERT_TRUE(built);
        ASSERT_TRUE(queried);
        std::vector<G::PointLbvhNeighbor> results(queries.size() * std::max(capacity, 1u));
        std::vector<G::PointLbvhQueryHeader> counts(queries.size());
        device.ReadBuffer(output.Handle, results.data(), results.size() * 8);
        device.ReadBuffer(headers.Handle, counts.data(), counts.size() * 8);
        for (std::size_t i = 0; i < queries.size(); ++i)
        {
            const bool valid =
                LB::ValidPoint(queries[i]) && std::ranges::all_of(points, LB::ValidPoint);
            ASSERT_EQ(counts[i].Status, valid ? 0u : 1u) << i;
            if (!valid)
                continue;
            const auto skip = exclusions.empty() ? LB::InvalidIndex : exclusions[i];
            if (kNearest)
            {
                const auto oracle = LB::KNearestReference(points, queries[i], kNearest, skip);
                ASSERT_EQ(counts[i].TotalCount, oracle.size()) << i;
                for (std::size_t j=0; j<capacity; ++j)
                {
                    EXPECT_EQ(results[i*capacity+j].Index, j<oracle.size()?oracle[j].Index:LB::InvalidIndex) << i << ":" << j;
                    if (j<oracle.size()) EXPECT_NEAR(results[i*capacity+j].SquaredDistance, oracle[j].SquaredDistance, 1e-5f);
                }
            }
            else if (radius < 0)
            {
                auto oracle = LB::NearestReference(points, queries[i], skip);
                EXPECT_EQ(results[i].Index, oracle.Index) << i;
                EXPECT_EQ(counts[i].TotalCount, oracle.Index == LB::InvalidIndex ? 0u : 1u);
                if (oracle.Index != LB::InvalidIndex)
                    EXPECT_NEAR(results[i].SquaredDistance, oracle.SquaredDistance, 1e-5f);
            }
            else
            {
                auto oracle = LB::RadiusReference(points, queries[i], radius, capacity, skip);
                ASSERT_EQ(counts[i].TotalCount, oracle.TotalCount) << i;
                for (std::size_t j = 0; j < oracle.Neighbors.size(); ++j)
                {
                    EXPECT_EQ(results[i * std::max(capacity, 1u) + j].Index,
                              oracle.Neighbors[j].Index)
                        << i;
                    EXPECT_NEAR(results[i * std::max(capacity, 1u) + j].SquaredDistance,
                                oracle.Neighbors[j].SquaredDistance, 1e-5f);
                }
            }
        }
    }
} // namespace
TEST(PointLBVHGpuSmoke, VulkanBuildAndQueriesMatchExhaustiveCpuAndReuseAllocations)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize())
        GTEST_SKIP() << "GLFW unavailable";
    auto config = Extrinsic::Runtime::CreateReferenceEngineConfig();
    config.Window.Width = 64;
    config.Window.Height = 64;
    config.Render.EnableValidation = true;
    config.Render.EnableVSync = false;
    config.ReferenceScene.Enabled = false;
    Intrinsic::Tests::RuntimeTestKernel engine(config, std::make_unique<App>());
    engine.EmplaceModule<Extrinsic::Runtime::SpatialIndexCache>();
    engine.Initialize();
    Shutdown shutdown{engine};
    engine.Run();
    auto& device = engine.GetDevice();
    if (!device.IsOperational())
        GTEST_SKIP() << "Operational Vulkan device unavailable";
    if (auto* profiler = device.GetProfiler())
        std::cout << "LBVH device: " << profiler->GetStatus().Diagnostic << '\n';
    G::PointLbvhWorkspace tree(device);
    std::mt19937 rng(917);
    std::uniform_real_distribution<float> dist(-5, 5);
    std::vector<glm::vec3> points, queries;
    for (int i = 0; i < 1025; ++i)
        points.push_back({dist(rng), dist(rng), dist(rng)});
    for (int i = 0; i < 257; ++i)
        queries.push_back({dist(rng), dist(rng), dist(rng)});
    points[9] = points[2];
    queries.push_back(points[2]);
    Check(device, tree, points, queries, -1, 1);
    auto allocations = tree.AllocationCount();
    Check(device, tree, points, queries, 2, 7);
    Check(device, tree, points, queries, 2, 0);
    std::fill(points.begin(), points.end(), glm::vec3(0));
    Check(device, tree, points, {{0, 0, 0}}, 0, 3);
    Check(device, tree, points, {{0, 0, 0}}, -1, 1);
    Check(device, tree, {{0, 0, 0}}, {{1, 0, 0}}, 1, 1);
    Check(device, tree, {}, queries, -1, 1);
    Check(device, tree, {{std::numeric_limits<float>::quiet_NaN(), 0, 0}}, {{0, 0, 0}}, -1, 1);
    EXPECT_EQ(tree.AllocationCount(), allocations);
    EXPECT_EQ(tree.BuildCount(), 8);
    CheckEntityCache(engine);
}

namespace
{
    namespace Runtime = Extrinsic::Runtime;
    namespace GS = Extrinsic::ECS::Components::GeometrySources;
    namespace Transform = Extrinsic::ECS::Components::Transform;
    class RegistrationApp final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        void Resolve() override
        {
            RunStarted = std::chrono::steady_clock::now();
            auto& engine=Kernel(); auto& scene=*engine.Worlds().Get(engine.ActiveWorld());
            Source=scene.Create(); Target=scene.Create();
            scene.Raw().emplace<Transform::Component>(Source);
            scene.Raw().emplace<Transform::Component>(Target);
            std::mt19937 random(917); std::uniform_real_distribution<float> dist(-5,5);
            std::vector<glm::vec3> points;
            for(int i=0;i<1025;++i) points.push_back({dist(random),dist(random),dist(random)});
            for(auto entity:{Source,Target})
            {
                auto& props=scene.Raw().emplace<GS::Vertices>(entity).Properties; props.Resize(points.size());
                auto samples=props.GetOrAdd<glm::vec3>("samples");
                auto normals=props.GetOrAdd<glm::vec3>("normals");
                for(std::size_t i=0;i<points.size();++i)
                {
                    samples[i]=points[i]+(entity==Source?glm::vec3(.2f,-.3f,.1f):glm::vec3(0));
                    normals[i]=glm::normalize(glm::vec3(points[i].x, 2*points[i].y, 3*points[i].z));
                }
                props.GetOrAdd<bool>("v:deleted")[7]=true;
            }
            Context.Scene=&scene; Context.World=engine.ActiveWorld(); Context.Device=&engine.GetDevice();
            Context.SpatialIndices=engine.Services().Find<Runtime::SpatialIndexCache>();
            Context.CommandHistory=&History;
            Context.MethodResultSinks.Registration=[this](auto result){ CompletedAt=std::chrono::steady_clock::now(); Completion=std::move(result); };
            Command.SourceStableEntityId=Runtime::SelectionController::ToStableEntityId(Source);
            Command.TargetStableEntityId=Runtime::SelectionController::ToStableEntityId(Target);
            Command.SourcePositions={.Domain=Runtime::GeometryElementDomain::PointCloudPoint,.Name="samples",.ValueKind=Geometry::PropertyValueKind::Vec3};
            Command.TargetPositions=Command.SourcePositions;
            Command.TargetNormals={.Domain=Runtime::GeometryElementDomain::PointCloudPoint,.Name="normals",.ValueKind=Geometry::PropertyValueKind::Vec3};
            Command.InlierRatio=1.; Command.TrajectoryStep=50;
        }
        void Frame(double,double) override
        {
            if (++Frames > 2000 || std::chrono::steady_clock::now() - RunStarted > std::chrono::seconds(70))
            {
                TimedOut = true;
                Kernel().RequestExit();
                return;
            }
            if(!Kernel().GetDevice().IsOperational())return;
            if(Completion)
            {
                Results.push_back(*Completion);
                Poses.push_back(Transform::GetMatrix(Context.Scene->Raw().get<Transform::Component>(Source)));
                Micros.push_back(std::chrono::duration<double,std::micro>(CompletedAt-Started).count());
                Completion.reset(); Submitted=false; ++Round;
                if(Round==7){Stats=Context.SpatialIndices->Stats();Kernel().RequestExit();return;}
            }
            if(Submitted)return;
            Context.Scene->Raw().get<Transform::Component>(Source)={};
            Command.Backend=(Round==0 || Round==5)?Runtime::RegistrationBackend::CpuKDTree:
                            Round<=2?Runtime::RegistrationBackend::CpuLBVH:Runtime::RegistrationBackend::VulkanLBVH;
            Command.Variant=Round>=5?Runtime::EditorICPVariant::PointToPlane:Runtime::EditorICPVariant::PointToPoint;
            Context.JobCommands = {};
            if(Round>=3 && Round!=5)
            {
                auto* jobs=&Kernel().Jobs();
                if(!jobs){TimedOut=true;Kernel().RequestExit();return;}
                Context.JobCommands.Submit=[jobs](Runtime::JobDesc desc,Runtime::EditorJobIdentity){return jobs->Submit(std::move(desc));};
            }
            Started=std::chrono::steady_clock::now(); Submitted=true;
            const auto accepted=Runtime::ApplyEditorRegistrationCommand(Context,Command);
            if(Round>=3 && Round!=5 && accepted.Status!=Runtime::EditorCommandStatus::Pending)Completion=accepted;
        }
        void Shutdown() override { Context={}; }
        Runtime::EditorGeometryProcessingContext Context{};
        Runtime::EditorRegistrationCommand Command{};
        Runtime::EditorCommandHistory History{};
        Runtime::SpatialIndexCacheStats Stats{};
        entt::entity Source{},Target{};
        std::optional<Runtime::EditorRegistrationResult> Completion{};
        std::vector<Runtime::EditorRegistrationResult> Results{};
        std::vector<double> Micros{};
        std::vector<glm::mat4> Poses{};
        std::chrono::steady_clock::time_point Started{},CompletedAt{},RunStarted{};
        unsigned Frames{},Round{}; bool Submitted{},TimedOut{};
    };
}
TEST(PointLBVHGpuSmoke, FramedRegistrationReusesTargetAcrossRunsAndMatchesCpuSolve)
{
    if(!Extrinsic::Platform::Backends::Glfw::CanInitialize())GTEST_SKIP()<<"GLFW unavailable";
    auto config=Extrinsic::Runtime::CreateReferenceEngineConfig();
    config.Window.Width=64;config.Window.Height=64;config.Render.EnableValidation=true;
    config.Render.EnableVSync=false;config.ReferenceScene.Enabled=false;
    auto app=std::make_unique<RegistrationApp>();auto* run=app.get();
    Intrinsic::Tests::RuntimeTestKernel engine(config,std::move(app));
    engine.EmplaceModule<Extrinsic::Runtime::SpatialIndexCache>();
    engine.Initialize();Shutdown shutdown{engine};engine.Run();
    if(!engine.GetDevice().IsOperational())GTEST_SKIP()<<"Operational Vulkan unavailable";
    ASSERT_FALSE(run->TimedOut) << "frames=" << run->Frames << " completed_rounds=" << run->Round;
    ASSERT_EQ(run->Results.size(),7);
    double maxTransformError=0;
    for(std::size_t i=0;i<run->Results.size();++i)
    {
        const auto& result=run->Results[i];
        for(int column=0;column<4;++column)for(int row=0;row<4;++row)
            maxTransformError=std::max(maxTransformError,double(std::abs(run->Poses[i][column][row]-run->Poses[i>=5?5:0][column][row])));
        EXPECT_TRUE(result.Succeeded())<<result.Message<<" "<<result.BackendDiagnostic;
        EXPECT_EQ(result.SourcePointCount,1024);EXPECT_EQ(result.TargetPointCount,1024);
        EXPECT_NEAR(result.FinalRMSE,run->Results[i>=5?5:0].FinalRMSE,1e-5);
        if(i>=2 && i!=5)EXPECT_TRUE(result.TargetIndexReused);
        if(i>=3 && i!=5){EXPECT_EQ(result.ActualBackend,Runtime::RegistrationBackend::VulkanLBVH);EXPECT_FALSE(result.FellBackToCPU);}
        std::cout<<"ICP round="<<i<<" backend="<<Runtime::ToString(result.ActualBackend)<<" total_us="<<run->Micros[i]<<" rmse="<<result.FinalRMSE<<" iterations="<<result.IterationsPerformed<<'\n';
    }
    EXPECT_LE(maxTransformError,1e-4);
    EXPECT_EQ(run->Stats.Builds,1);EXPECT_EQ(run->Stats.GpuBuilds,1);
    EXPECT_EQ(run->Results[6].EffectiveVariant,Runtime::EditorICPVariant::PointToPlane);
    const auto& pose=run->Context.Scene->Raw().get<Transform::Component>(run->Source);
    EXPECT_NEAR(pose.Position.x,-.2,1e-4);EXPECT_NEAR(pose.Position.y,.3,1e-4);EXPECT_NEAR(pose.Position.z,-.1,1e-4);
    if(const auto* output=std::getenv("INTRINSIC_ICP_BENCHMARK_OUTPUT"))
    {
        const bool succeeded = !::testing::Test::HasFailure();
        nlohmann::json json{{"benchmark_id","geometry.registration.runtime_spatial_smoke"},
            {"method","geometry.registration"},{"backend","gpu_vulkan_compute"},
            {"dataset","builtin.random3d.1024_live.seed917"},{"commit","local-dev"},
            {"metrics",{{"runtime_ms",run->Micros[4]/1000},{"quality_error_linf",maxTransformError}}},
            {"diagnostics",{{"runner","IntrinsicPointLBVHGpuTests"},{"mode","smoke"},
                {"cpu_reference_total_ms",run->Micros[0]/1000},{"cpu_lbvh_cold_total_ms",run->Micros[1]/1000},
                {"cpu_lbvh_warm_total_ms",run->Micros[2]/1000},{"vulkan_lbvh_cold_total_ms",run->Micros[3]/1000},
                {"vulkan_lbvh_warm_total_ms",run->Micros[4]/1000},{"cpu_point_to_plane_total_ms",run->Micros[5]/1000},{"vulkan_point_to_plane_total_ms",run->Micros[6]/1000},
                {"cpu_target_builds",run->Stats.Builds},{"gpu_target_builds",run->Stats.GpuBuilds},
                {"frames",run->Frames},
                {"elapsed_wall_ms",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-run->RunStarted).count()},
                {"warmup_iterations",1},{"measured_iterations",1}}},
            {"status",succeeded?"passed":"failed"}};
        std::ofstream stream(output);ASSERT_TRUE(stream.good());stream<<json.dump(2)<<'\n';ASSERT_TRUE(stream.good());
    }
}

TEST(PointLBVHGpuSmoke, KNearestAndExclusionMatchExhaustiveOracle)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize()) GTEST_SKIP() << "GLFW unavailable";
    auto config = Extrinsic::Runtime::CreateReferenceEngineConfig();
    config.Window.Width=64; config.Window.Height=64; config.Render.EnableValidation=true;
    config.Render.EnableVSync=false; config.ReferenceScene.Enabled=false;
    Intrinsic::Tests::RuntimeTestKernel engine(config, std::make_unique<App>());
    engine.Initialize(); Shutdown shutdown{engine}; engine.Run();
    auto& device=engine.GetDevice();
    if (!device.IsOperational()) GTEST_SKIP() << "Operational Vulkan unavailable";
    G::PointLbvhWorkspace tree(device);
    std::mt19937 rng(381); std::uniform_real_distribution<float> dist(-5,5);
    std::vector<glm::vec3> points(513),queries;
    std::vector<std::uint32_t> excluded;
    for (auto& p:points) p={dist(rng),dist(rng),dist(rng)};
    std::fill_n(points.begin(),70,glm::vec3(0));
    for (std::uint32_t i=0;i<128;++i) { queries.push_back(points[i]);excluded.push_back(i); }
    queries.push_back({std::numeric_limits<float>::infinity(),0,0});excluded.push_back(LB::InvalidIndex);
    for (auto k:{1u,16u,64u}) Check(device,tree,points,queries,-1,k,k,excluded);
    Check(device,tree,points,queries,-1,1,0,excluded);
    Check(device,tree,points,queries,2,3,0,excluded);
    Check(device,tree,{{0,0,0}},{{0,0,0},{0,0,0}},-1,16,16,{0,LB::InvalidIndex});
    Check(device,tree,{},{{0,0,0}},-1,16,16);
    EXPECT_EQ(tree.AllocationCount(),1);
}
namespace
{
    class KnnBatchApp final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        explicit KnnBatchApp(bool radius = false) : RadiusMode(radius) {}
        void Resolve() override
        {
            Started=std::chrono::steady_clock::now();
            auto& scene=*Kernel().Worlds().Get(Kernel().ActiveWorld());
            Entity=scene.Create();
            auto& props=scene.Raw().emplace<Extrinsic::ECS::Components::GeometrySources::Vertices>(Entity).Properties;
            props.Resize(5);
            Positions=props.GetOrAdd<glm::vec3>("samples");
            Positions.Vector()={{0,0,0},{0,0,0},{0,0,0},{1,0,0},{-1,0,0}};
            props.GetOrAdd<bool>("v:deleted")[1]=true;
            Cache=Kernel().Services().Find<Runtime::SpatialIndexCache>();
            Handle=Cache->Acquire(Kernel().ActiveWorld(),Entity,
                {.Domain=Runtime::GeometryElementDomain::PointCloudPoint,.Name="samples",.ValueKind=Geometry::PropertyValueKind::Vec3}).Handle;
        }
        void Frame(double,double) override
        {
            if (std::chrono::steady_clock::now()-Started>std::chrono::seconds(40))
            { TimedOut=true; Kernel().RequestExit();return; }
            if (!Kernel().GetDevice().IsOperational())
            {
                if (RadiusMode && ++ColdFrames > 4) { TimedOut=true;Kernel().RequestExit(); }
                return;
            }
            if (Batch)
            {
                if (Batch->State==Runtime::SpatialQueryState::Queued || Batch->State==Runtime::SpatialQueryState::Submitted) return;
                if (Round==2)
                {
                    EXPECT_EQ(Batch->State,Runtime::SpatialQueryState::Failed);
                    EXPECT_FALSE(Batch->Diagnostic.empty());
                    Stats=Cache->Stats();Done=true;Kernel().RequestExit();return;
                }
                EXPECT_EQ(Batch->State,Runtime::SpatialQueryState::Ready) << Batch->Diagnostic;
                if (Batch->State!=Runtime::SpatialQueryState::Ready) { Kernel().RequestExit();return; }
                if (RadiusMode)
                {
                    EXPECT_EQ(Batch->Counts, Round==0 ? (std::vector<std::uint32_t>{1,2})
                                                       : (std::vector<std::uint32_t>{3,4}));
                    ASSERT_EQ(Batch->Neighbors.size(),2);
                    EXPECT_EQ(Batch->Neighbors[0].Index,Round==0?2u:0u);
                    EXPECT_EQ(Batch->Neighbors[1].Index,0u);
                }
                else
                {
                EXPECT_EQ(Batch->Counts,(std::vector<std::uint32_t>{3,4}));
                const std::vector<std::uint32_t> expected=Round==0?
                    std::vector<std::uint32_t>{2,3,4,LB::InvalidIndex,0,2,3,4}:
                    std::vector<std::uint32_t>{0,3,4,LB::InvalidIndex,0,2,3,4};
                EXPECT_EQ(Batch->Neighbors.size(),expected.size());
                for (std::size_t i=0;i<expected.size();++i) EXPECT_EQ(Batch->Neighbors[i].Index,expected[i]);
                }
                ++Round;
            }
            const std::vector<glm::vec3> queries{{0,0,0},{0,0,0}};
            const std::vector<std::uint32_t> exclusions{Round==0?0u:2u,1u};
            if (Round==0)
            {
                EXPECT_EQ(Cache->QueueGpuKNearest(Handle,queries,0)->State,Runtime::SpatialQueryState::Failed);
                EXPECT_EQ(Cache->QueueGpuKNearest(Handle,queries,65)->State,Runtime::SpatialQueryState::Failed);
                EXPECT_EQ(Cache->QueueGpuKNearest(Handle,queries,4,std::vector<std::uint32_t>{0})->State,Runtime::SpatialQueryState::Failed);
            }
            auto old=Batch;
            Batch=RadiusMode ? Cache->QueueGpuRadius(Handle,queries,Round==0?.5f:1.f,1,exclusions,Batch)
                             : Cache->QueueGpuKNearest(Handle,queries,4,exclusions,Batch);
            if (old) EXPECT_EQ(old,Batch);
            if (Round==2) Positions[4]={-2,0,0}; // Queued work must reject a stale target before submission.
        }
        void Shutdown() override { Batch.reset();Cache=nullptr; }
        Runtime::SpatialIndexCache* Cache{};
        Runtime::SpatialIndexHandle Handle{};
        std::shared_ptr<Runtime::SpatialNearestBatch> Batch{};
        Geometry::Property<glm::vec3> Positions{};
        Runtime::SpatialIndexCacheStats Stats{};
        entt::entity Entity{};
        std::chrono::steady_clock::time_point Started{};
        unsigned Round{},ColdFrames{}; bool TimedOut{},Done{},RadiusMode{};
    };
}
TEST(PointLBVHGpuSmoke, FramedKNearestReusesBuffersAndRejectsStaleTarget)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize()) GTEST_SKIP() << "GLFW unavailable";
    auto config=Runtime::CreateReferenceEngineConfig();
    config.Window.Width=64;config.Window.Height=64;config.Render.EnableValidation=true;
    config.Render.EnableVSync=false;config.ReferenceScene.Enabled=false;
    auto app=std::make_unique<KnnBatchApp>();auto* run=app.get();
    Intrinsic::Tests::RuntimeTestKernel engine(config,std::move(app));
    engine.EmplaceModule<Runtime::SpatialIndexCache>();
    engine.Initialize();Shutdown shutdown{engine};engine.Run();
    if (!engine.GetDevice().IsOperational()) GTEST_SKIP() << "Operational Vulkan unavailable";
    ASSERT_FALSE(run->TimedOut);ASSERT_TRUE(run->Done);
    EXPECT_EQ(run->Stats.Builds,1);EXPECT_EQ(run->Stats.GpuBuilds,1);
}

TEST(PointLBVHGpuSmoke, FramedRadiusPreservesOverflowExclusionAndBufferReuse)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize()) GTEST_SKIP() << "GLFW unavailable";
    auto config=Runtime::CreateReferenceEngineConfig();
    config.Window.Width=64;config.Window.Height=64;config.Render.EnableValidation=true;
    config.Render.EnableVSync=false;config.ReferenceScene.Enabled=false;
    auto app=std::make_unique<KnnBatchApp>(true);auto* run=app.get();
    Intrinsic::Tests::RuntimeTestKernel engine(config,std::move(app));
    engine.EmplaceModule<Runtime::SpatialIndexCache>();
    engine.Initialize();Shutdown shutdown{engine};
    engine.Run();ASSERT_TRUE(engine.GetDevice().IsOperational());
    ASSERT_FALSE(run->TimedOut);ASSERT_TRUE(run->Done);
    EXPECT_EQ(run->Stats.Builds,1);EXPECT_EQ(run->Stats.GpuBuilds,1);
}

namespace
{
    using Domain = Runtime::GeometryElementDomain;
    class NormalApp final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        Geometry::PropertySet& Props(unsigned d)
        {
            return *const_cast<Geometry::PropertySet*>(Runtime::ResolveGeometryPropertySet(
                Runtime::BuildGeometryAvailability(Context.Scene->Raw(), Entities[d-1]), Domain(d)));
        }
        Runtime::NormalEstimationConfig Config(unsigned d) const
        {
            Runtime::NormalEstimationConfig c;
            c.StableEntityId=Runtime::SelectionController::ToStableEntityId(Entities[d-1]);
            c.Positions={.Domain=Domain(d),.Name="samples",.ValueKind=Geometry::PropertyValueKind::Vec3};
            c.Output={.Domain=Domain(d),.Name="estimated",.ValueKind=Geometry::PropertyValueKind::Vec3};
            c.KNeighbors=15;c.UseRadiusSearch=Phase==2;c.Radius=1.1f;
            c.Backend=Runtime::NormalEstimationBackend::VulkanLBVH;
            c.GpuQueryBatchSize=64; // Exercise a final partial chunk and buffer lifetime.
            return c;
        }
        void Resolve() override
        {
            Started=std::chrono::steady_clock::now();
            Context.Scene=Kernel().Worlds().Get(Kernel().ActiveWorld());Context.World=Kernel().ActiveWorld();
            Context.SpatialIndices=Kernel().Services().Find<Runtime::SpatialIndexCache>();
            std::mt19937 random(241);std::uniform_real_distribution<float> dist(-1,1);
            std::vector<glm::vec3> points;
            for(unsigned i=0;i<66;++i){const float x=dist(random),y=dist(random);points.push_back({x,y,.2f*x*x+.1f*y*y});}
            points[2]=points[0];
            for(unsigned d=1;d<=8;++d)
            {
                auto entity=Context.Scene->Create();Entities.push_back(entity);
                if(d<=unsigned(Domain::MeshFace))
                {
                    Geometry::HalfedgeMesh::Mesh mesh;
                    auto a=mesh.AddVertex({0,0,0}),b=mesh.AddVertex({1,0,0}),c=mesh.AddVertex({0,1,0});
                    (void)mesh.AddTriangle(a,b,c);GS::PopulateFromMesh(Context.Scene->Raw(),entity,mesh);
                }
                else if(d<unsigned(Domain::PointCloudPoint))
                {
                    Geometry::Graph::Graph graph;
                    auto a=graph.AddVertex({0,0,0}),b=graph.AddVertex({1,0,0});
                    (void)graph.AddEdge(a,b);GS::PopulateFromGraph(Context.Scene->Raw(),entity,graph);
                }
                else Context.Scene->Raw().emplace<GS::Vertices>(entity);
                auto& p=Props(d);p.Resize(points.size());p.GetOrAdd<glm::vec3>("samples").Vector()=points;
                p.GetOrAdd<float>("keep").Vector().assign(points.size(),42.f);
                p.GetOrAdd<glm::vec3>("estimated").Vector().assign(points.size(),glm::vec3(7,8,9));
                const bool half=d==unsigned(Domain::MeshHalfedge)||d==unsigned(Domain::GraphHalfedge);
                if(half)
                {
                    auto& edges=Context.Scene->Raw().get<GS::Edges>(entity).Properties;
                    edges.Resize(points.size()/2);edges.GetOrAdd<bool>("e:deleted")[2]=true;
                }
                else p.GetOrAdd<bool>(d==unsigned(Domain::MeshFace)?"f:deleted":
                    (d==unsigned(Domain::MeshEdge)||d==unsigned(Domain::GraphEdge))?"e:deleted":"v:deleted")[4]=true;
                p.Get<glm::vec3>("samples")[4]={std::numeric_limits<float>::quiet_NaN(),0,0};
            }
            Context.MethodResultSinks.NormalEstimation=[this](auto result){Results.push_back(std::move(result));};
        }
        void Frame(double,double) override
        {
            if(std::chrono::steady_clock::now()-Started>std::chrono::seconds(95))
            {TimedOut=true;Kernel().RequestExit();return;}
            if(!Kernel().GetDevice().IsOperational())
            {
                if(++ColdFrames>4){TimedOut=true;Kernel().RequestExit();}
                return;
            }
            if(Submitted)
            {
                if(Phase==4 && ++CancelFrames==3)
                    EXPECT_TRUE(Kernel().Jobs().Cancel(FitToken));
                if(Results.size()<ExpectedResults)return;
                if(Phase>=3)
                {
                    EXPECT_FALSE(Results.back().Succeeded())<<Results.back().Message;
                    EXPECT_EQ(Props(8).Get<glm::vec3>("estimated")[0],glm::vec3(7,8,9));
                    if(Phase==5)
                    {
                        EXPECT_NE(Results.back().Message.find("1024"),std::string::npos);
                        Done=true;Kernel().RequestExit();return;
                    }
                }
                else
                {
                    PhaseMs.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-PhaseStarted).count());
                    double neighborhoodMs=0,fitMs=0;
                    std::size_t batches=0;
                    for(const auto& result:Results)
                    {
                        neighborhoodMs+=result.GpuNeighborhoodMilliseconds;
                        fitMs+=result.CpuComputeMilliseconds;
                        batches+=result.GpuQueryBatches;
                        EXPECT_TRUE(result.Succeeded())<<result.Message;
                        EXPECT_EQ(result.ActualBackend,"vulkan_lbvh");
                        EXPECT_GT(result.GpuQueryBatches,0);
                        if(Phase>0)EXPECT_TRUE(result.IndexReused);
                    }
                    NeighborhoodMs.push_back(neighborhoodMs);FitMs.push_back(fitMs);BatchCounts.push_back(batches);
                    for(unsigned d=1;d<=8;++d)
                    {
                        const auto& values=std::as_const(Props(d)).Get<glm::vec3>("estimated").Vector();
                        ASSERT_EQ(values.size(),Reference[d-1].size());
                        for(std::size_t i=0;i<values.size();++i)for(unsigned axis=0;axis<3;++axis)
                        {
                            EXPECT_TRUE(std::isfinite(values[i][axis]));
                            const double error=std::abs(values[i][axis]-Reference[d-1][i][axis]);
                            MaxError=std::isfinite(error)?std::max(MaxError,error):std::numeric_limits<double>::infinity();
                        }
                        EXPECT_EQ(std::as_const(Props(d)).Get<float>("keep")[0],42.f);
                    }
                    EXPECT_LE(MaxError,1e-5);
                    EXPECT_EQ(History.UndoCount(),8);
                    for(unsigned i=0;i<8;++i)EXPECT_EQ(History.Undo().Status,Runtime::EditorCommandHistoryStatus::Undone);
                    for(unsigned d=1;d<=8;++d)EXPECT_EQ(std::as_const(Props(d)).Get<glm::vec3>("estimated")[0],glm::vec3(7,8,9));
                    for(unsigned i=0;i<8;++i)EXPECT_EQ(History.Redo().Status,Runtime::EditorCommandHistoryStatus::Redone);
                }
                Results.clear();Submitted=false;++Phase;
            }
            if(Phase<3)
            {
                Reference.clear();History.ClearHistory();Context.JobCommands={};Context.CommandHistory=nullptr;
                const auto cpuStart=std::chrono::steady_clock::now();
                for(unsigned d=1;d<=8;++d)
                {
                    auto c=Config(d);c.Backend=Runtime::NormalEstimationBackend::CpuKDTree;
                    const auto result=Runtime::ApplyEditorNormalEstimationCommand(Context,c);
                    ASSERT_TRUE(result.Succeeded())<<result.Message;
                    Reference.push_back(std::as_const(Props(d)).Get<glm::vec3>("estimated").Vector());
                    Props(d).Get<glm::vec3>("estimated").Vector().assign(66,glm::vec3(7,8,9));
                }
                CpuMs.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-cpuStart).count());
            }
            Context.JobCommands.Submit=[this](Runtime::JobDesc desc,Runtime::EditorJobIdentity){FitToken=Kernel().Jobs().Submit(std::move(desc));return FitToken;};
            Context.CommandHistory=&History;ExpectedResults=Phase<3?8:1;Submitted=true;PhaseStarted=std::chrono::steady_clock::now();
            if(Phase<3)
            {
                if(Phase==0)
                {
                    auto unsupported=Config(8);unsupported.KNeighbors=64;
                    EXPECT_FALSE(Runtime::PreviewEditorNormalEstimationCommand(Context,unsupported).Ready);
                }
                for(unsigned d=1;d<=8;++d)
                {
                    const auto result=Runtime::ApplyEditorNormalEstimationCommand(Context,Config(d));
                    if(result.Status!=Runtime::EditorCommandStatus::Pending)Results.push_back(result);
                }
            }
            else
            {
                auto c=Config(8);
                if(Phase==4)c.GpuQueryBatchSize=1; // Cancel after GPU submission, before all 65 rows complete.
                if(Phase==5)
                {
                    auto& p=Props(8);p.Resize(1026);
                    p.Get<glm::vec3>("samples").Vector().assign(1026,glm::vec3(0));
                    p.Get<glm::vec3>("estimated").Vector().assign(1026,glm::vec3(7,8,9));
                    c.UseRadiusSearch=true;c.Radius=1;c.GpuQueryBatchSize=1;
                }
                else Props(8).Get<glm::vec3>("estimated").Vector().assign(66,glm::vec3(7,8,9));
                const auto result=Runtime::ApplyEditorNormalEstimationCommand(Context,c);
                if(result.Status!=Runtime::EditorCommandStatus::Pending)Results.push_back(result);
                if(Phase==3)Props(8).Get<glm::vec3>("samples")[0]+=.1f; // stale before GPU recording
            }
        }
        void Shutdown() override {Context={};}
        Runtime::EditorGeometryProcessingContext Context{};
        Runtime::EditorCommandHistory History{};
        std::vector<entt::entity> Entities;
        std::vector<std::vector<glm::vec3>> Reference;
        std::vector<Runtime::EditorNormalEstimationResult> Results;
        std::vector<double> PhaseMs,CpuMs,NeighborhoodMs,FitMs;
        std::vector<std::size_t> BatchCounts;
        std::chrono::steady_clock::time_point Started{},PhaseStarted{};
        Runtime::JobToken FitToken{};
        std::size_t ExpectedResults{};unsigned Phase{},CancelFrames{},ColdFrames{};bool Submitted{},Done{},TimedOut{};double MaxError{};
    };
}
TEST(PointLBVHGpuSmoke, NormalNeighborhoodsPublishAcrossDomainsAndRejectIncompleteSupport)
{
    if(!Extrinsic::Platform::Backends::Glfw::CanInitialize())GTEST_SKIP()<<"GLFW unavailable";
    auto config=Runtime::CreateReferenceEngineConfig();
    config.Window.Width=64;config.Window.Height=64;config.Render.EnableValidation=true;
    config.Render.EnableVSync=false;config.ReferenceScene.Enabled=false;
    auto app=std::make_unique<NormalApp>();auto* run=app.get();
    Intrinsic::Tests::RuntimeTestKernel engine(config,std::move(app));
    engine.EmplaceModule<Runtime::SpatialIndexCache>();engine.Initialize();Shutdown shutdown{engine};
    engine.Run();ASSERT_TRUE(engine.GetDevice().IsOperational());
    ASSERT_FALSE(run->TimedOut)<<"phase="<<run->Phase;ASSERT_TRUE(run->Done);
    EXPECT_LE(run->MaxError,1e-5);ASSERT_EQ(run->PhaseMs.size(),3);
    if(const auto* output=std::getenv("INTRINSIC_NORMAL_BENCHMARK_OUTPUT"))
    {
        nlohmann::json json{{"benchmark_id","geometry.point_lbvh.normal_runtime_smoke"},
            {"method","geometry.point_lbvh"},{"backend","gpu_vulkan_compute"},
            {"dataset","builtin.paraboloid.66_slots.eight_domains.seed241"},{"commit","local-dev"},
            {"metrics",{{"runtime_ms",run->PhaseMs[1]},{"quality_error_linf",run->MaxError}}},
            {"diagnostics",{{"runner","IntrinsicPointLBVHGpuTests"},{"mode","smoke"},
                {"cpu_reference_total_ms",run->CpuMs[1]},{"vulkan_cold_total_ms",run->PhaseMs[0]},
                {"vulkan_warm_total_ms",run->PhaseMs[1]},{"vulkan_radius_total_ms",run->PhaseMs[2]},
                {"cpu_radius_total_ms",run->CpuMs[2]},{"warmup_iterations",1},{"measured_iterations",1},
                {"cpu_fit_and_orientation",true},{"gpu_neighborhood_elapsed_sum_ms",run->NeighborhoodMs},
                {"cpu_fit_sum_ms",run->FitMs},{"gpu_query_batch_counts",run->BatchCounts}}},{"status",::testing::Test::HasFailure()?"failed":"passed"}};
        std::ofstream stream(output);ASSERT_TRUE(stream.good());stream<<json.dump(2)<<'\n';ASSERT_TRUE(stream.good());
    }
}

namespace
{
    class OutlierApp final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        explicit OutlierApp(bool ratio=false) : Ratio(ratio) {}
        Geometry::PropertySet& Props(unsigned d)
        {
            return *const_cast<Geometry::PropertySet*>(Runtime::ResolveGeometryPropertySet(
                Runtime::BuildGeometryAvailability(Context.Scene->Raw(), Entities[d-1]), Domain(d)));
        }
        Runtime::OutlierAnalysisConfig Config(unsigned d) const
        {
            Runtime::OutlierAnalysisConfig c;
            c.StableEntityId=Runtime::SelectionController::ToStableEntityId(Entities[d-1]);
            c.Positions={.Domain=Domain(d),.Name="samples",.ValueKind=Geometry::PropertyValueKind::Vec3};
            c.Mask={Domain(d),"outliers",Geometry::PropertyValueKind::UInt32};
            c.Score={Domain(d),"scores",Geometry::PropertyValueKind::Float};
            c.KNeighbors=Ratio && Phase==2 ? 63 : 8;
            c.Method=Ratio ? Runtime::OutlierAnalysisMethod::LocalDistanceRatio :
                Phase==2 ? Runtime::OutlierAnalysisMethod::Radius : Runtime::OutlierAnalysisMethod::Statistical;c.Radius=.5f;c.MinimumNeighbors=1;
            c.Backend=Runtime::OutlierAnalysisBackend::VulkanLBVH;
            c.GpuQueryBatchSize=64; // Exercise a final partial chunk and buffer lifetime.
            return c;
        }
        void Resolve() override
        {
            Started=std::chrono::steady_clock::now();
            Context.Scene=Kernel().Worlds().Get(Kernel().ActiveWorld());Context.World=Kernel().ActiveWorld();
            Context.SpatialIndices=Kernel().Services().Find<Runtime::SpatialIndexCache>();
            std::mt19937 random(241);std::uniform_real_distribution<float> dist(-1,1);
            std::vector<glm::vec3> points;
            for(unsigned i=0;i<66;++i){const float x=dist(random),y=dist(random);points.push_back({.1f*x,.1f*y,0});}
            points[0]={0,0,0};points[1]={.3f,.4f,0};points[2]=points[0];
            points[3]={std::nextafter(.5f,1.f),0,0};points[65]={10,0,0};
            for(unsigned d=1;d<=8;++d)
            {
                auto entity=Context.Scene->Create();Entities.push_back(entity);
                if(d<=unsigned(Domain::MeshFace))
                {
                    Geometry::HalfedgeMesh::Mesh mesh;
                    auto a=mesh.AddVertex({0,0,0}),b=mesh.AddVertex({1,0,0}),c=mesh.AddVertex({0,1,0});
                    (void)mesh.AddTriangle(a,b,c);GS::PopulateFromMesh(Context.Scene->Raw(),entity,mesh);
                }
                else if(d<unsigned(Domain::PointCloudPoint))
                {
                    Geometry::Graph::Graph graph;
                    auto a=graph.AddVertex({0,0,0}),b=graph.AddVertex({1,0,0});
                    (void)graph.AddEdge(a,b);GS::PopulateFromGraph(Context.Scene->Raw(),entity,graph);
                }
                else Context.Scene->Raw().emplace<GS::Vertices>(entity);
                auto& p=Props(d);p.Resize(points.size());p.GetOrAdd<glm::vec3>("samples").Vector()=points;
                p.GetOrAdd<float>("keep").Vector().assign(points.size(),42.f);
                p.GetOrAdd<std::uint32_t>("outliers").Vector().assign(points.size(),77);
                p.GetOrAdd<float>("scores").Vector().assign(points.size(),77.f);
                const bool half=d==unsigned(Domain::MeshHalfedge)||d==unsigned(Domain::GraphHalfedge);
                if(half)
                {
                    auto& edges=Context.Scene->Raw().get<GS::Edges>(entity).Properties;
                    edges.Resize(points.size()/2);edges.GetOrAdd<bool>("e:deleted")[2]=true;
                }
                else p.GetOrAdd<bool>(d==unsigned(Domain::MeshFace)?"f:deleted":
                    (d==unsigned(Domain::MeshEdge)||d==unsigned(Domain::GraphEdge))?"e:deleted":"v:deleted")[4]=true;
                p.Get<glm::vec3>("samples")[4]={std::numeric_limits<float>::quiet_NaN(),0,0};
            }
            Context.MethodResultSinks.OutlierAnalysis=[this](auto result){Results.push_back(std::move(result));};
        }
        void Frame(double,double) override
        {
            if(std::chrono::steady_clock::now()-Started>std::chrono::seconds(95))
            {TimedOut=true;Kernel().RequestExit();return;}
            if(!Kernel().GetDevice().IsOperational())
            {
                if(++ColdFrames>4){TimedOut=true;Kernel().RequestExit();}
                return;
            }
            if(Submitted)
            {
                if(Phase==4 && ++CancelFrames==3)
                    EXPECT_TRUE(Kernel().Jobs().Cancel(FitToken));
                if(Results.size()<ExpectedResults)return;
                if(Phase>=3)
                {
                    if(Phase==5)
                    {
                        EXPECT_TRUE(Results.back().Succeeded())<<Results.back().Message;
                        EXPECT_EQ(Results.back().ActualBackend,"vulkan_lbvh");
                        EXPECT_EQ(Results.back().RejectedCount,Ratio ? 0u : 1u);
                        EXPECT_EQ(std::as_const(Props(8)).Get<float>("scores")[0],Ratio ? 0.f : 1027.f);
                        EXPECT_EQ(std::as_const(Props(8)).Get<std::uint32_t>("outliers")[0],0);
                        Done=true;Kernel().RequestExit();return;
                    }
                    EXPECT_FALSE(Results.back().Succeeded())<<Results.back().Message;
                    EXPECT_EQ(std::as_const(Props(8)).Get<std::uint32_t>("outliers")[0],77);
                }
                else
                {
                    PhaseMs.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-PhaseStarted).count());
                    double neighborhoodMs=0,fitMs=0;
                    std::size_t batches=0;
                    for(const auto& result:Results)
                    {
                        neighborhoodMs+=result.GpuNeighborhoodMilliseconds;
                        fitMs+=result.CpuComputeMilliseconds;
                        batches+=result.GpuQueryBatches;
                        EXPECT_TRUE(result.Succeeded())<<result.Message;
                        EXPECT_EQ(result.ActualBackend,"vulkan_lbvh");
                        EXPECT_GT(result.GpuQueryBatches,0);
                        if(Phase>0)EXPECT_TRUE(result.IndexReused);
                    }
                    NeighborhoodMs.push_back(neighborhoodMs);FitMs.push_back(fitMs);BatchCounts.push_back(batches);
                    for(unsigned d=1;d<=8;++d)
                    {
                        const auto& values=std::as_const(Props(d)).Get<float>("scores").Vector();
                        EXPECT_EQ(std::as_const(Props(d)).Get<std::uint32_t>("outliers").Vector(),ReferenceMasks[d-1]);
                        ASSERT_EQ(values.size(),Reference[d-1].size());
                        for(std::size_t i=0;i<values.size();++i)
                        {
                            EXPECT_TRUE(std::isfinite(values[i]));
                            const double error=std::abs(values[i]-Reference[d-1][i]);
                            MaxError=std::isfinite(error)?std::max(MaxError,error):std::numeric_limits<double>::infinity();
                        }
                        EXPECT_EQ(std::as_const(Props(d)).Get<float>("keep")[0],42.f);
                    }
                    EXPECT_LE(MaxError,1e-5);
                    EXPECT_EQ(History.UndoCount(),8);
                    for(unsigned i=0;i<8;++i)EXPECT_EQ(History.Undo().Status,Runtime::EditorCommandHistoryStatus::Undone);
                    for(unsigned d=1;d<=8;++d)EXPECT_EQ(std::as_const(Props(d)).Get<std::uint32_t>("outliers")[0],77);
                    for(unsigned i=0;i<8;++i)EXPECT_EQ(History.Redo().Status,Runtime::EditorCommandHistoryStatus::Redone);
                }
                Results.clear();Submitted=false;++Phase;
            }
            if(Phase<3)
            {
                Reference.clear();ReferenceMasks.clear();History.ClearHistory();Context.JobCommands={};Context.CommandHistory=nullptr;
                const auto cpuStart=std::chrono::steady_clock::now();
                for(unsigned d=1;d<=8;++d)
                {
                    auto c=Config(d);c.Backend=Runtime::OutlierAnalysisBackend::CpuOctree;
                    const auto result=Runtime::ApplyEditorOutlierAnalysisCommand(Context,c);
                    ASSERT_TRUE(result.Succeeded())<<result.Message;
                    Reference.push_back(std::as_const(Props(d)).Get<float>("scores").Vector());
                    ReferenceMasks.push_back(std::as_const(Props(d)).Get<std::uint32_t>("outliers").Vector());
                    Props(d).Get<float>("scores").Vector().assign(66,77.f);
                    Props(d).Get<std::uint32_t>("outliers").Vector().assign(66,77);
                }
                CpuMs.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-cpuStart).count());
            }
            Context.JobCommands.Submit=[this](Runtime::JobDesc desc,Runtime::EditorJobIdentity){FitToken=Kernel().Jobs().Submit(std::move(desc));return FitToken;};
            Context.CommandHistory=&History;ExpectedResults=Phase<3?8:1;Submitted=true;PhaseStarted=std::chrono::steady_clock::now();
            if(Phase<3)
            {
                if(Phase==0)
                {
                    auto unsupported=Config(8);unsupported.KNeighbors=Ratio ? 64 : 65;
                    EXPECT_FALSE(Runtime::PreviewEditorOutlierAnalysisCommand(Context,unsupported).Ready);
                }
                for(unsigned d=1;d<=8;++d)
                {
                    const auto result=Runtime::ApplyEditorOutlierAnalysisCommand(Context,Config(d));
                    if(result.Status!=Runtime::EditorCommandStatus::Pending)Results.push_back(result);
                }
            }
            else
            {
                auto c=Config(8);
                if(Phase==4)c.GpuQueryBatchSize=1; // Cancel after GPU submission, before all 65 rows complete.
                if(Phase==5)
                {
                    auto& p=Props(8);p.Resize(1030);
                    p.Get<glm::vec3>("samples").Vector().assign(1030,glm::vec3(0));
                    p.Get<glm::vec3>("samples")[1029]={10,0,0};
                    c.Method=Ratio ? Runtime::OutlierAnalysisMethod::LocalDistanceRatio : Runtime::OutlierAnalysisMethod::Radius;c.KNeighbors=2;c.Radius=1;c.MinimumNeighbors=1027;c.GpuQueryBatchSize=4096;
                }
                else Props(8).Get<std::uint32_t>("outliers").Vector().assign(66,77);
                const auto result=Runtime::ApplyEditorOutlierAnalysisCommand(Context,c);
                if(result.Status!=Runtime::EditorCommandStatus::Pending)Results.push_back(result);
                if(Phase==3)Props(8).Get<glm::vec3>("samples")[0]+=.1f; // stale before GPU recording
            }
        }
        void Shutdown() override {Context={};}
        Runtime::EditorGeometryProcessingContext Context{};
        Runtime::EditorCommandHistory History{};
        std::vector<entt::entity> Entities;
        std::vector<std::vector<float>> Reference;
        std::vector<std::vector<std::uint32_t>> ReferenceMasks;
        std::vector<Runtime::EditorOutlierAnalysisResult> Results;
        std::vector<double> PhaseMs,CpuMs,NeighborhoodMs,FitMs;
        std::vector<std::size_t> BatchCounts;
        std::chrono::steady_clock::time_point Started{},PhaseStarted{};
        Runtime::JobToken FitToken{};
        std::size_t ExpectedResults{};unsigned Phase{},CancelFrames{},ColdFrames{};bool Ratio{},Submitted{},Done{},TimedOut{};double MaxError{};
    };
}
TEST(PointLBVHGpuSmoke, OutlierNeighborhoodsPublishAcrossDomainsAndCountDenseSupport)
{
    if(!Extrinsic::Platform::Backends::Glfw::CanInitialize())GTEST_SKIP()<<"GLFW unavailable";
    auto config=Runtime::CreateReferenceEngineConfig();
    config.Window.Width=64;config.Window.Height=64;config.Render.EnableValidation=true;
    config.Render.EnableVSync=false;config.ReferenceScene.Enabled=false;
    auto app=std::make_unique<OutlierApp>();auto* run=app.get();
    Intrinsic::Tests::RuntimeTestKernel engine(config,std::move(app));
    engine.EmplaceModule<Runtime::SpatialIndexCache>();engine.Initialize();Shutdown shutdown{engine};
    engine.Run();ASSERT_TRUE(engine.GetDevice().IsOperational());
    ASSERT_FALSE(run->TimedOut)<<"phase="<<run->Phase;ASSERT_TRUE(run->Done);
    EXPECT_LE(run->MaxError,1e-5);ASSERT_EQ(run->PhaseMs.size(),3);
    if(const auto* output=std::getenv("INTRINSIC_OUTLIER_BENCHMARK_OUTPUT"))
    {
        nlohmann::json json{{"benchmark_id","geometry.point_lbvh.outlier_runtime_smoke"},
            {"method","geometry.point_lbvh"},{"backend","gpu_vulkan_compute"},
            {"dataset","builtin.outlier_clusters.eight_domains.seed241"},{"commit","local-dev"},
            {"metrics",{{"runtime_ms",run->PhaseMs[1]},{"quality_error_linf",run->MaxError}}},
            {"diagnostics",{{"runner","IntrinsicPointLBVHGpuTests"},{"mode","smoke"},
                {"cpu_reference_total_ms",run->CpuMs[1]},{"vulkan_cold_total_ms",run->PhaseMs[0]},
                {"vulkan_warm_total_ms",run->PhaseMs[1]},{"vulkan_radius_total_ms",run->PhaseMs[2]},
                {"cpu_radius_total_ms",run->CpuMs[2]},{"warmup_iterations",1},{"measured_iterations",1},
                {"cpu_classification",true},{"gpu_neighborhood_elapsed_sum_ms",run->NeighborhoodMs},
                {"cpu_classification_sum_ms",run->FitMs},{"gpu_query_batch_counts",run->BatchCounts}}},{"status",::testing::Test::HasFailure()?"failed":"passed"}};
        std::ofstream stream(output);ASSERT_TRUE(stream.good());stream<<json.dump(2)<<'\n';ASSERT_TRUE(stream.good());
    }
}
TEST(PointLBVHGpuSmoke, LocalDistanceRatioPublishesAcrossDomains)
{
    if(!Extrinsic::Platform::Backends::Glfw::CanInitialize())GTEST_SKIP()<<"GLFW unavailable";
    auto config=Runtime::CreateReferenceEngineConfig();
    config.Window.Width=64;config.Window.Height=64;config.Render.EnableValidation=true;
    config.Render.EnableVSync=false;config.ReferenceScene.Enabled=false;
    auto app=std::make_unique<OutlierApp>(true);auto* run=app.get();
    Intrinsic::Tests::RuntimeTestKernel engine(config,std::move(app));
    engine.EmplaceModule<Runtime::SpatialIndexCache>();engine.Initialize();Shutdown shutdown{engine};
    engine.Run();ASSERT_TRUE(engine.GetDevice().IsOperational());
    ASSERT_FALSE(run->TimedOut)<<"phase="<<run->Phase;ASSERT_TRUE(run->Done);
    EXPECT_LE(run->MaxError,1e-5);ASSERT_EQ(run->PhaseMs.size(),3);
    if(const auto* output=std::getenv("INTRINSIC_DISTANCE_RATIO_BENCHMARK_OUTPUT"))
    {
        nlohmann::json json{{"benchmark_id","geometry.point_lbvh.distance_ratio_runtime_smoke"},
            {"method","geometry.point_lbvh"},{"backend","gpu_vulkan_compute"},
            {"dataset","builtin.outlier_clusters.eight_domains.seed241"},{"commit","local-dev"},
            {"metrics",{{"runtime_ms",run->PhaseMs[1]},{"quality_error_linf",run->MaxError}}},
            {"diagnostics",{{"runner","IntrinsicPointLBVHGpuTests"},{"mode","smoke"},
                {"cpu_reference_total_ms",run->CpuMs[1]},{"vulkan_cold_total_ms",run->PhaseMs[0]},
                {"vulkan_warm_total_ms",run->PhaseMs[1]},{"vulkan_k63_total_ms",run->PhaseMs[2]},
                {"cpu_k63_total_ms",run->CpuMs[2]},{"warmup_iterations",1},{"measured_iterations",1},
                {"cpu_classification",true},{"gpu_neighborhood_elapsed_sum_ms",run->NeighborhoodMs},
                {"cpu_classification_sum_ms",run->FitMs},{"gpu_query_batch_counts",run->BatchCounts}}},{"status",::testing::Test::HasFailure()?"failed":"passed"}};
        std::ofstream stream(output);ASSERT_TRUE(stream.good());stream<<json.dump(2)<<'\n';ASSERT_TRUE(stream.good());
    }
}

namespace
{
    class DensityApp final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        Geometry::PropertySet& Props(unsigned d)
        {
            return *const_cast<Geometry::PropertySet*>(Runtime::ResolveGeometryPropertySet(
                Runtime::BuildGeometryAvailability(Context.Scene->Raw(), Entities[d-1]), Domain(d)));
        }
        Runtime::KernelDensityConfig Config(unsigned d) const
        {
            Runtime::KernelDensityConfig c;
            c.StableEntityId=Runtime::SelectionController::ToStableEntityId(Entities[d-1]);
            c.Positions={.Domain=Domain(d),.Name="samples",.ValueKind=Geometry::PropertyValueKind::Vec3};
            c.Density={Domain(d),"density",Geometry::PropertyValueKind::Float};
            c.KNeighbors=Phase==2?63:8;c.Bandwidth=Phase==2?.2f:0;
            c.Backend=Runtime::KernelDensityBackend::VulkanLBVH;
            c.GpuQueryBatchSize=64; // Exercise a final partial chunk and buffer lifetime.
            return c;
        }
        void Resolve() override
        {
            Started=std::chrono::steady_clock::now();
            Context.Scene=Kernel().Worlds().Get(Kernel().ActiveWorld());Context.World=Kernel().ActiveWorld();
            Context.SpatialIndices=Kernel().Services().Find<Runtime::SpatialIndexCache>();
            std::mt19937 random(241);std::uniform_real_distribution<float> dist(-1,1);
            std::vector<glm::vec3> points;
            for(unsigned i=0;i<66;++i){const float x=dist(random),y=dist(random);points.push_back({.1f*x,.1f*y,0});}
            points[0]={0,0,0};points[1]={.3f,.4f,0};points[2]=points[0];
            points[3]={std::nextafter(.5f,1.f),0,0};points[65]={10,0,0};
            for(unsigned d=1;d<=8;++d)
            {
                auto entity=Context.Scene->Create();Entities.push_back(entity);
                if(d<=unsigned(Domain::MeshFace))
                {
                    Geometry::HalfedgeMesh::Mesh mesh;
                    auto a=mesh.AddVertex({0,0,0}),b=mesh.AddVertex({1,0,0}),c=mesh.AddVertex({0,1,0});
                    (void)mesh.AddTriangle(a,b,c);GS::PopulateFromMesh(Context.Scene->Raw(),entity,mesh);
                }
                else if(d<unsigned(Domain::PointCloudPoint))
                {
                    Geometry::Graph::Graph graph;
                    auto a=graph.AddVertex({0,0,0}),b=graph.AddVertex({1,0,0});
                    (void)graph.AddEdge(a,b);GS::PopulateFromGraph(Context.Scene->Raw(),entity,graph);
                }
                else Context.Scene->Raw().emplace<GS::Vertices>(entity);
                auto& p=Props(d);p.Resize(points.size());p.GetOrAdd<glm::vec3>("samples").Vector()=points;
                p.GetOrAdd<float>("keep").Vector().assign(points.size(),42.f);
                p.GetOrAdd<float>("density").Vector().assign(points.size(),77.f);
                const bool half=d==unsigned(Domain::MeshHalfedge)||d==unsigned(Domain::GraphHalfedge);
                if(half)
                {
                    auto& edges=Context.Scene->Raw().get<GS::Edges>(entity).Properties;
                    edges.Resize(points.size()/2);edges.GetOrAdd<bool>("e:deleted")[2]=true;
                }
                else p.GetOrAdd<bool>(d==unsigned(Domain::MeshFace)?"f:deleted":
                    (d==unsigned(Domain::MeshEdge)||d==unsigned(Domain::GraphEdge))?"e:deleted":"v:deleted")[4]=true;
                p.Get<glm::vec3>("samples")[4]={std::numeric_limits<float>::quiet_NaN(),0,0};
            }
            Context.MethodResultSinks.KernelDensity=[this](auto result){Results.push_back(std::move(result));};
        }
        void Frame(double,double) override
        {
            if(std::chrono::steady_clock::now()-Started>std::chrono::seconds(95))
            {TimedOut=true;Kernel().RequestExit();return;}
            if(!Kernel().GetDevice().IsOperational())
            {
                if(++ColdFrames>4){TimedOut=true;Kernel().RequestExit();}
                return;
            }
            if(Submitted)
            {
                if(Phase==4 && ++CancelFrames==3)
                    EXPECT_TRUE(Kernel().Jobs().Cancel(FitToken));
                if(Results.size()<ExpectedResults)return;
                if(Phase>=3)
                {
                    if(Phase==5)
                    {
                        EXPECT_TRUE(Results.back().Succeeded())<<Results.back().Message;
                        EXPECT_EQ(Results.back().ActualBackend,"vulkan_lbvh");
                        EXPECT_NEAR(std::as_const(Props(8)).Get<float>("density")[0],std::pow(2*std::acos(-1.),-1.5),1e-7);
                        Done=true;Kernel().RequestExit();return;
                    }
                    EXPECT_FALSE(Results.back().Succeeded())<<Results.back().Message;
                    EXPECT_EQ(std::as_const(Props(8)).Get<float>("density")[0],77);
                }
                else
                {
                    PhaseMs.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-PhaseStarted).count());
                    double neighborhoodMs=0,fitMs=0;
                    std::size_t batches=0;
                    for(const auto& result:Results)
                    {
                        neighborhoodMs+=result.GpuNeighborhoodMilliseconds;
                        fitMs+=result.CpuComputeMilliseconds;
                        batches+=result.GpuQueryBatches;
                        EXPECT_TRUE(result.Succeeded())<<result.Message;
                        EXPECT_EQ(result.ActualBackend,"vulkan_lbvh");
                        EXPECT_GT(result.GpuQueryBatches,0);
                        if(Phase>0)EXPECT_TRUE(result.IndexReused);
                    }
                    NeighborhoodMs.push_back(neighborhoodMs);FitMs.push_back(fitMs);BatchCounts.push_back(batches);
                    for(unsigned d=1;d<=8;++d)
                    {
                        const auto& values=std::as_const(Props(d)).Get<float>("density").Vector();
                        ASSERT_EQ(values.size(),Reference[d-1].size());
                        for(std::size_t i=0;i<values.size();++i)
                        {
                            EXPECT_TRUE(std::isfinite(values[i]));
                            const double error=std::abs(values[i]-Reference[d-1][i]);
                            MaxError=std::isfinite(error)?std::max(MaxError,error):std::numeric_limits<double>::infinity();
                        }
                        EXPECT_EQ(std::as_const(Props(d)).Get<float>("keep")[0],42.f);
                    }
                    EXPECT_LE(MaxError,1e-5);
                    EXPECT_EQ(History.UndoCount(),8);
                    for(unsigned i=0;i<8;++i)EXPECT_EQ(History.Undo().Status,Runtime::EditorCommandHistoryStatus::Undone);
                    for(unsigned d=1;d<=8;++d)EXPECT_EQ(std::as_const(Props(d)).Get<float>("density")[0],77);
                    for(unsigned i=0;i<8;++i)EXPECT_EQ(History.Redo().Status,Runtime::EditorCommandHistoryStatus::Redone);
                }
                Results.clear();Submitted=false;++Phase;
            }
            if(Phase<3)
            {
                Reference.clear();History.ClearHistory();Context.JobCommands={};Context.CommandHistory=nullptr;
                const auto cpuStart=std::chrono::steady_clock::now();
                for(unsigned d=1;d<=8;++d)
                {
                    auto c=Config(d);c.Backend=Runtime::KernelDensityBackend::CpuOctree;
                    const auto result=Runtime::ApplyEditorKernelDensityCommand(Context,c);
                    ASSERT_TRUE(result.Succeeded())<<result.Message;
                    Reference.push_back(std::as_const(Props(d)).Get<float>("density").Vector());
                    Props(d).Get<float>("density").Vector().assign(66,77.f);
                }
                CpuMs.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-cpuStart).count());
            }
            Context.JobCommands.Submit=[this](Runtime::JobDesc desc,Runtime::EditorJobIdentity){FitToken=Kernel().Jobs().Submit(std::move(desc));return FitToken;};
            Context.CommandHistory=&History;ExpectedResults=Phase<3?8:1;Submitted=true;PhaseStarted=std::chrono::steady_clock::now();
            if(Phase<3)
            {
                if(Phase==0)
                {
                    auto unsupported=Config(8);unsupported.KNeighbors=64;
                    EXPECT_FALSE(Runtime::PreviewEditorKernelDensityCommand(Context,unsupported).Ready);
                }
                for(unsigned d=1;d<=8;++d)
                {
                    const auto result=Runtime::ApplyEditorKernelDensityCommand(Context,Config(d));
                    if(result.Status!=Runtime::EditorCommandStatus::Pending)Results.push_back(result);
                }
            }
            else
            {
                auto c=Config(8);
                if(Phase==4)c.GpuQueryBatchSize=1; // Cancel after GPU submission, before all 65 rows complete.
                if(Phase==5)
                {
                    auto& p=Props(8);p.Resize(1030);
                    p.Get<glm::vec3>("samples").Vector().assign(1030,glm::vec3(0));
                    p.Get<glm::vec3>("samples")[1029]={10,0,0};
                    c.Bandwidth=1;c.GpuQueryBatchSize=4096;
                }
                else Props(8).Get<float>("density").Vector().assign(66,77);
                const auto result=Runtime::ApplyEditorKernelDensityCommand(Context,c);
                if(result.Status!=Runtime::EditorCommandStatus::Pending)Results.push_back(result);
                if(Phase==3)Props(8).Get<glm::vec3>("samples")[0]+=.1f; // stale before GPU recording
            }
        }
        void Shutdown() override {Context={};}
        Runtime::EditorGeometryProcessingContext Context{};
        Runtime::EditorCommandHistory History{};
        std::vector<entt::entity> Entities;
        std::vector<std::vector<float>> Reference;
        std::vector<Runtime::EditorKernelDensityResult> Results;
        std::vector<double> PhaseMs,CpuMs,NeighborhoodMs,FitMs;
        std::vector<std::size_t> BatchCounts;
        std::chrono::steady_clock::time_point Started{},PhaseStarted{};
        Runtime::JobToken FitToken{};
        std::size_t ExpectedResults{};unsigned Phase{},CancelFrames{},ColdFrames{};bool Submitted{},Done{},TimedOut{};double MaxError{};
    };
}
TEST(PointLBVHGpuSmoke, KernelDensityPublishesAcrossDomainsAndPreservesCandidatePolicy)
{
    if(!Extrinsic::Platform::Backends::Glfw::CanInitialize())GTEST_SKIP()<<"GLFW unavailable";
    auto config=Runtime::CreateReferenceEngineConfig();
    config.Window.Width=64;config.Window.Height=64;config.Render.EnableValidation=true;
    config.Render.EnableVSync=false;config.ReferenceScene.Enabled=false;
    auto app=std::make_unique<DensityApp>();auto* run=app.get();
    Intrinsic::Tests::RuntimeTestKernel engine(config,std::move(app));
    engine.EmplaceModule<Runtime::SpatialIndexCache>();engine.Initialize();Shutdown shutdown{engine};
    engine.Run();ASSERT_TRUE(engine.GetDevice().IsOperational());
    ASSERT_FALSE(run->TimedOut)<<"phase="<<run->Phase;ASSERT_TRUE(run->Done);
    EXPECT_LE(run->MaxError,1e-5);ASSERT_EQ(run->PhaseMs.size(),3);
    if(const auto* output=std::getenv("INTRINSIC_DENSITY_BENCHMARK_OUTPUT"))
    {
        nlohmann::json json{{"benchmark_id","geometry.point_lbvh.density_runtime_smoke"},
            {"method","geometry.point_lbvh"},{"backend","gpu_vulkan_compute"},
            {"dataset","builtin.density_clusters.eight_domains.seed241"},{"commit","local-dev"},
            {"metrics",{{"runtime_ms",run->PhaseMs[1]},{"quality_error_linf",run->MaxError}}},
            {"diagnostics",{{"runner","IntrinsicPointLBVHGpuTests"},{"mode","smoke"},
                {"cpu_reference_total_ms",run->CpuMs[1]},{"vulkan_cold_total_ms",run->PhaseMs[0]},
                {"vulkan_warm_total_ms",run->PhaseMs[1]},{"vulkan_manual_bandwidth_k63_total_ms",run->PhaseMs[2]},
                {"cpu_manual_bandwidth_k63_total_ms",run->CpuMs[2]},{"warmup_iterations",1},{"measured_iterations",1},
                {"cpu_bandwidth_and_gaussian",true},{"gpu_neighborhood_elapsed_sum_ms",run->NeighborhoodMs},
                {"cpu_bandwidth_and_gaussian_sum_ms",run->FitMs},{"gpu_query_batch_counts",run->BatchCounts}}},{"status",::testing::Test::HasFailure()?"failed":"passed"}};
        std::ofstream stream(output);ASSERT_TRUE(stream.good());stream<<json.dump(2)<<'\n';ASSERT_TRUE(stream.good());
    }
}

namespace
{
    class SpacingApp final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        Geometry::PropertySet& Props(unsigned d)
        {
            return *const_cast<Geometry::PropertySet*>(Runtime::ResolveGeometryPropertySet(
                Runtime::BuildGeometryAvailability(Context.Scene->Raw(), Entities[d-1]), Domain(d)));
        }
        Runtime::PointSpacingConfig Config(unsigned d) const
        {
            Runtime::PointSpacingConfig c;
            c.StableEntityId=Runtime::SelectionController::ToStableEntityId(Entities[d-1]);
            c.Positions={.Domain=Domain(d),.Name="samples",.ValueKind=Geometry::PropertyValueKind::Vec3};
            c.Radii={Domain(d),"radii",Geometry::PropertyValueKind::Float};
            c.KNeighbors=Phase==2?63:8;c.ScaleFactor=Phase==2?2.f:1.f;
            c.Backend=Runtime::PointSpacingBackend::VulkanLBVH;
            c.GpuQueryBatchSize=64; // Exercise a final partial chunk and buffer lifetime.
            return c;
        }
        void Resolve() override
        {
            Started=std::chrono::steady_clock::now();
            Context.Scene=Kernel().Worlds().Get(Kernel().ActiveWorld());Context.World=Kernel().ActiveWorld();
            Context.SpatialIndices=Kernel().Services().Find<Runtime::SpatialIndexCache>();
            std::mt19937 random(241);std::uniform_real_distribution<float> dist(-1,1);
            std::vector<glm::vec3> points;
            for(unsigned i=0;i<66;++i){const float x=dist(random),y=dist(random);points.push_back({.1f*x,.1f*y,0});}
            points[0]={0,0,0};points[1]={.3f,.4f,0};points[2]=points[0];
            points[3]={std::nextafter(.5f,1.f),0,0};points[65]={10,0,0};
            for(unsigned d=1;d<=8;++d)
            {
                auto entity=Context.Scene->Create();Entities.push_back(entity);
                if(d<=unsigned(Domain::MeshFace))
                {
                    Geometry::HalfedgeMesh::Mesh mesh;
                    auto a=mesh.AddVertex({0,0,0}),b=mesh.AddVertex({1,0,0}),c=mesh.AddVertex({0,1,0});
                    (void)mesh.AddTriangle(a,b,c);GS::PopulateFromMesh(Context.Scene->Raw(),entity,mesh);
                }
                else if(d<unsigned(Domain::PointCloudPoint))
                {
                    Geometry::Graph::Graph graph;
                    auto a=graph.AddVertex({0,0,0}),b=graph.AddVertex({1,0,0});
                    (void)graph.AddEdge(a,b);GS::PopulateFromGraph(Context.Scene->Raw(),entity,graph);
                }
                else Context.Scene->Raw().emplace<GS::Vertices>(entity);
                auto& p=Props(d);p.Resize(points.size());p.GetOrAdd<glm::vec3>("samples").Vector()=points;
                p.GetOrAdd<float>("keep").Vector().assign(points.size(),42.f);
                p.GetOrAdd<float>("radii").Vector().assign(points.size(),77.f);
                const bool half=d==unsigned(Domain::MeshHalfedge)||d==unsigned(Domain::GraphHalfedge);
                if(half)
                {
                    auto& edges=Context.Scene->Raw().get<GS::Edges>(entity).Properties;
                    edges.Resize(points.size()/2);edges.GetOrAdd<bool>("e:deleted")[2]=true;
                }
                else p.GetOrAdd<bool>(d==unsigned(Domain::MeshFace)?"f:deleted":
                    (d==unsigned(Domain::MeshEdge)||d==unsigned(Domain::GraphEdge))?"e:deleted":"v:deleted")[4]=true;
                p.Get<glm::vec3>("samples")[4]={std::numeric_limits<float>::quiet_NaN(),0,0};
            }
            Context.MethodResultSinks.PointSpacing=[this](auto result){Results.push_back(std::move(result));};
        }
        void Frame(double,double) override
        {
            if(std::chrono::steady_clock::now()-Started>std::chrono::seconds(95))
            {TimedOut=true;Kernel().RequestExit();return;}
            if(!Kernel().GetDevice().IsOperational())
            {
                if(++ColdFrames>4){TimedOut=true;Kernel().RequestExit();}
                return;
            }
            if(Submitted)
            {
                if(Phase==4 && ++CancelFrames==3)
                    EXPECT_TRUE(Kernel().Jobs().Cancel(FitToken));
                if(Results.size()<ExpectedResults)return;
                if(Phase>=3)
                {
                    if(Phase==5)
                    {
                        EXPECT_TRUE(Results.back().Succeeded())<<Results.back().Message;
                        EXPECT_EQ(Results.back().ActualBackend,"vulkan_lbvh");
                        EXPECT_NEAR(std::as_const(Props(8)).Get<float>("radii")[0],0.f,1e-7);
                        Done=true;Kernel().RequestExit();return;
                    }
                    EXPECT_FALSE(Results.back().Succeeded())<<Results.back().Message;
                    EXPECT_EQ(std::as_const(Props(8)).Get<float>("radii")[0],77);
                }
                else
                {
                    PhaseMs.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-PhaseStarted).count());
                    double neighborhoodMs=0,fitMs=0;
                    std::size_t batches=0;
                    for(const auto& result:Results)
                    {
                        neighborhoodMs+=result.GpuNeighborhoodMilliseconds;
                        fitMs+=result.CpuComputeMilliseconds;
                        batches+=result.GpuQueryBatches;
                        EXPECT_TRUE(result.Succeeded())<<result.Message;
                        EXPECT_EQ(result.ActualBackend,"vulkan_lbvh");
                        EXPECT_GT(result.GpuQueryBatches,0);
                        if(Phase>0)EXPECT_TRUE(result.IndexReused);
                        const auto& ref=ReferenceResults[unsigned(result.Radii.Domain)-1];
                        for(auto [actual,expected] : {std::pair{result.MeanRadius,ref.MeanRadius},
                            std::pair{result.MinRadius,ref.MinRadius},std::pair{result.MaxRadius,ref.MaxRadius},
                            std::pair{result.Statistics.AverageSpacing,ref.Statistics.AverageSpacing},
                            std::pair{result.Statistics.MinSpacing,ref.Statistics.MinSpacing},
                            std::pair{result.Statistics.MaxSpacing,ref.Statistics.MaxSpacing}})
                        {
                            EXPECT_NEAR(actual,expected,1e-5);
                            MaxError=std::max(MaxError,double(std::abs(actual-expected)));
                        }

                    }
                    NeighborhoodMs.push_back(neighborhoodMs);FitMs.push_back(fitMs);BatchCounts.push_back(batches);
                    for(unsigned d=1;d<=8;++d)
                    {
                        const auto& values=std::as_const(Props(d)).Get<float>("radii").Vector();
                        ASSERT_EQ(values.size(),Reference[d-1].size());
                        for(std::size_t i=0;i<values.size();++i)
                        {
                            EXPECT_TRUE(std::isfinite(values[i]));
                            const double error=std::abs(values[i]-Reference[d-1][i]);
                            MaxError=std::isfinite(error)?std::max(MaxError,error):std::numeric_limits<double>::infinity();
                        }
                        EXPECT_EQ(std::as_const(Props(d)).Get<float>("keep")[0],42.f);
                    }
                    EXPECT_LE(MaxError,1e-5);
                    EXPECT_EQ(History.UndoCount(),8);
                    for(unsigned i=0;i<8;++i)EXPECT_EQ(History.Undo().Status,Runtime::EditorCommandHistoryStatus::Undone);
                    for(unsigned d=1;d<=8;++d)EXPECT_EQ(std::as_const(Props(d)).Get<float>("radii")[0],77);
                    for(unsigned i=0;i<8;++i)EXPECT_EQ(History.Redo().Status,Runtime::EditorCommandHistoryStatus::Redone);
                }
                Results.clear();Submitted=false;++Phase;
            }
            if(Phase<3)
            {
                Reference.clear();ReferenceResults.clear();History.ClearHistory();Context.JobCommands={};Context.CommandHistory=nullptr;
                const auto cpuStart=std::chrono::steady_clock::now();
                for(unsigned d=1;d<=8;++d)
                {
                    auto c=Config(d);c.Backend=Runtime::PointSpacingBackend::CpuOctree;
                    const auto result=Runtime::ApplyEditorPointSpacingCommand(Context,c);
                    ASSERT_TRUE(result.Succeeded())<<result.Message;
                    ReferenceResults.push_back(result);
                    Reference.push_back(std::as_const(Props(d)).Get<float>("radii").Vector());
                    Props(d).Get<float>("radii").Vector().assign(66,77.f);
                }
                CpuMs.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-cpuStart).count());
            }
            Context.JobCommands.Submit=[this](Runtime::JobDesc desc,Runtime::EditorJobIdentity){FitToken=Kernel().Jobs().Submit(std::move(desc));return FitToken;};
            Context.CommandHistory=&History;ExpectedResults=Phase<3?8:1;Submitted=true;PhaseStarted=std::chrono::steady_clock::now();
            if(Phase<3)
            {
                if(Phase==0)
                {
                    auto unsupported=Config(8);unsupported.KNeighbors=64;
                    EXPECT_FALSE(Runtime::PreviewEditorPointSpacingCommand(Context,unsupported).Ready);
                }
                for(unsigned d=1;d<=8;++d)
                {
                    const auto result=Runtime::ApplyEditorPointSpacingCommand(Context,Config(d));
                    if(result.Status!=Runtime::EditorCommandStatus::Pending)Results.push_back(result);
                }
            }
            else
            {
                auto c=Config(8);
                if(Phase==4)c.GpuQueryBatchSize=1; // Cancel after GPU submission, before all 65 rows complete.
                if(Phase==5)
                {
                    auto& p=Props(8);p.Resize(1030);
                    p.Get<glm::vec3>("samples").Vector().assign(1030,glm::vec3(0));
                    p.Get<glm::vec3>("samples")[1029]={10,0,0};
                    c.ScaleFactor=1;c.GpuQueryBatchSize=4096;
                }
                else Props(8).Get<float>("radii").Vector().assign(66,77);
                const auto result=Runtime::ApplyEditorPointSpacingCommand(Context,c);
                if(result.Status!=Runtime::EditorCommandStatus::Pending)Results.push_back(result);
                if(Phase==3)Props(8).Get<glm::vec3>("samples")[0]+=.1f; // stale before GPU recording
            }
        }
        void Shutdown() override {Context={};}
        Runtime::EditorGeometryProcessingContext Context{};
        Runtime::EditorCommandHistory History{};
        std::vector<entt::entity> Entities;
        std::vector<std::vector<float>> Reference;
        std::vector<Runtime::EditorPointSpacingResult> Results, ReferenceResults;
        std::vector<double> PhaseMs,CpuMs,NeighborhoodMs,FitMs;
        std::vector<std::size_t> BatchCounts;
        std::chrono::steady_clock::time_point Started{},PhaseStarted{};
        Runtime::JobToken FitToken{};
        std::size_t ExpectedResults{};unsigned Phase{},CancelFrames{},ColdFrames{};bool Submitted{},Done{},TimedOut{};double MaxError{};
    };
}
TEST(PointLBVHGpuSmoke, PointSpacingPublishesAcrossDomainsAndPreservesCandidatePolicy)
{
    if(!Extrinsic::Platform::Backends::Glfw::CanInitialize())GTEST_SKIP()<<"GLFW unavailable";
    auto config=Runtime::CreateReferenceEngineConfig();
    config.Window.Width=64;config.Window.Height=64;config.Render.EnableValidation=true;
    config.Render.EnableVSync=false;config.ReferenceScene.Enabled=false;
    auto app=std::make_unique<SpacingApp>();auto* run=app.get();
    Intrinsic::Tests::RuntimeTestKernel engine(config,std::move(app));
    engine.EmplaceModule<Runtime::SpatialIndexCache>();engine.Initialize();Shutdown shutdown{engine};
    engine.Run();ASSERT_TRUE(engine.GetDevice().IsOperational());
    ASSERT_FALSE(run->TimedOut)<<"phase="<<run->Phase;ASSERT_TRUE(run->Done);
    EXPECT_LE(run->MaxError,1e-5);ASSERT_EQ(run->PhaseMs.size(),3);
    if(const auto* output=std::getenv("INTRINSIC_SPACING_BENCHMARK_OUTPUT"))
    {
        nlohmann::json json{{"benchmark_id","geometry.point_lbvh.spacing_runtime_smoke"},
            {"method","geometry.point_lbvh"},{"backend","gpu_vulkan_compute"},
            {"dataset","builtin.spacing_clusters.eight_domains.seed241"},{"commit","local-dev"},
            {"metrics",{{"runtime_ms",run->PhaseMs[1]},{"quality_error_linf",run->MaxError}}},
            {"diagnostics",{{"runner","IntrinsicPointLBVHGpuTests"},{"mode","smoke"},
                {"cpu_reference_total_ms",run->CpuMs[1]},{"vulkan_cold_total_ms",run->PhaseMs[0]},
                {"vulkan_warm_total_ms",run->PhaseMs[1]},{"vulkan_scale2_k63_total_ms",run->PhaseMs[2]},
                {"cpu_scale2_k63_total_ms",run->CpuMs[2]},{"warmup_iterations",1},{"measured_iterations",1},
                {"cpu_spacing_and_radii",true},{"gpu_neighborhood_elapsed_sum_ms",run->NeighborhoodMs},
                {"cpu_spacing_and_radii_sum_ms",run->FitMs},{"gpu_query_batch_counts",run->BatchCounts}}},{"status",::testing::Test::HasFailure()?"failed":"passed"}};
        std::ofstream stream(output);ASSERT_TRUE(stream.good());stream<<json.dump(2)<<'\n';ASSERT_TRUE(stream.good());
    }
}

namespace
{
    class BilateralApp final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        Geometry::PropertySet& Props(unsigned d)
        {
            return *const_cast<Geometry::PropertySet*>(Runtime::ResolveGeometryPropertySet(
                Runtime::BuildGeometryAvailability(Context.Scene->Raw(), Entities[d-1]), Domain(d)));
        }
        Runtime::BilateralFilterConfig Config(unsigned d) const
        {
            Runtime::BilateralFilterConfig c;
            c.StableEntityId=Runtime::SelectionController::ToStableEntityId(Entities[d-1]);
            c.Positions={.Domain=Domain(d),.Name="samples",.ValueKind=Geometry::PropertyValueKind::Vec3};
            c.Normals={Domain(d),"directions",Geometry::PropertyValueKind::Vec3};
            c.Output={Domain(d),Phase==2?"samples":"filtered",Geometry::PropertyValueKind::Vec3};
            c.KNeighbors=Phase==2?63:8;c.SpatialSigma=2;c.NormalSigma=.25f;c.Iterations=3;
            c.Backend=Runtime::BilateralFilterBackend::VulkanLBVH;
            c.GpuQueryBatchSize=128; // One partial batch per small domain keeps nine moving passes bounded.
            return c;
        }
        void Resolve() override
        {
            Started=std::chrono::steady_clock::now();
            Context.Scene=Kernel().Worlds().Get(Kernel().ActiveWorld());Context.World=Kernel().ActiveWorld();
            Context.SpatialIndices=Kernel().Services().Find<Runtime::SpatialIndexCache>();
            std::mt19937 random(241);std::uniform_real_distribution<float> dist(-1,1);
            std::vector<glm::vec3> points;
            for(unsigned i=0;i<66;++i){const float x=dist(random),y=dist(random);points.push_back({x,y,.1f*dist(random)});}
            points[0]={0,0,0};points[1]={.3f,.4f,0};points[2]=points[0];
            points[3]={std::nextafter(.5f,1.f),0,0};points[65]={10,0,0};
            for(unsigned d=1;d<=8;++d)
            {
                auto entity=Context.Scene->Create();Entities.push_back(entity);
                if(d<=unsigned(Domain::MeshFace))
                {
                    Geometry::HalfedgeMesh::Mesh mesh;
                    auto a=mesh.AddVertex({0,0,0}),b=mesh.AddVertex({1,0,0}),c=mesh.AddVertex({0,1,0});
                    (void)mesh.AddTriangle(a,b,c);GS::PopulateFromMesh(Context.Scene->Raw(),entity,mesh);
                }
                else if(d<unsigned(Domain::PointCloudPoint))
                {
                    Geometry::Graph::Graph graph;
                    auto a=graph.AddVertex({0,0,0}),b=graph.AddVertex({1,0,0});
                    (void)graph.AddEdge(a,b);GS::PopulateFromGraph(Context.Scene->Raw(),entity,graph);
                }
                else Context.Scene->Raw().emplace<GS::Vertices>(entity);
                auto& p=Props(d);p.Resize(points.size());p.GetOrAdd<glm::vec3>("samples").Vector()=points;
                p.GetOrAdd<float>("keep").Vector().assign(points.size(),42.f);
                p.GetOrAdd<glm::vec3>("filtered").Vector().assign(points.size(),glm::vec3(77));
                p.GetOrAdd<glm::vec3>("directions").Vector().assign(points.size(),glm::vec3(0,0,1));
                const bool half=d==unsigned(Domain::MeshHalfedge)||d==unsigned(Domain::GraphHalfedge);
                if(half)
                {
                    auto& edges=Context.Scene->Raw().get<GS::Edges>(entity).Properties;
                    edges.Resize(points.size()/2);edges.GetOrAdd<bool>("e:deleted")[2]=true;
                }
                else p.GetOrAdd<bool>(d==unsigned(Domain::MeshFace)?"f:deleted":
                    (d==unsigned(Domain::MeshEdge)||d==unsigned(Domain::GraphEdge))?"e:deleted":"v:deleted")[4]=true;
                p.Get<glm::vec3>("samples")[4]={std::numeric_limits<float>::quiet_NaN(),0,0};
            }
            Context.MethodResultSinks.BilateralFilter=[this](auto result){Results.push_back(std::move(result));};
        }
        void Frame(double,double) override
        {
            if(std::chrono::steady_clock::now()-Started>std::chrono::seconds(95))
            {
                ADD_FAILURE() << "Bilateral timeout: phase=" << Phase << " phase_ms="
                              << std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-PhaseStarted).count()
                              << " results=" << Results.size() << " expected=" << ExpectedResults;
                for(auto token:StageTokens)
                    ADD_FAILURE() << "Unfinished bilateral stage state: " << Runtime::ToString(Kernel().Jobs().GetState(token));
                TimedOut=true;Kernel().RequestExit();return;
            }
            if(!Kernel().GetDevice().IsOperational())
            {
                if(++ColdFrames>4){TimedOut=true;Kernel().RequestExit();}
                return;
            }
            if(Submitted)
            {
                if(Phase==4 && !CancelledIntermediate && Kernel().Jobs().GetState(IntermediateToken)==Runtime::JobState::AwaitingApply)
                {EXPECT_TRUE(Kernel().Jobs().Cancel(IntermediateToken));CancelledIntermediate=true;}
                if(Phase==4)(void)Kernel().Jobs().ReapCompleted();
                if(Phase<3)
                    for(unsigned d=1;d<=8;++d)
                    {
                        const bool published=std::ranges::any_of(Results,[&](const auto& r){return unsigned(r.Output.Domain)==d;});
                        if(Phase==2 && published)continue;
                        const auto current=std::as_const(Props(d)).Get<glm::vec3>("samples");
                        for(std::size_t i=0;i<current.Size();++i)
                            if(i!=4)EXPECT_EQ(current[i],BeforePositions[d-1][i]);
                    }
                if(Results.size()<ExpectedResults)return;
                if(Phase>=3)
                {
                    if(Phase==5)
                    {
                        EXPECT_TRUE(Results.back().Succeeded())<<Results.back().Message;
                        EXPECT_EQ(Results.back().ActualBackend,"vulkan_lbvh");
                        EXPECT_NEAR(glm::length(std::as_const(Props(8)).Get<glm::vec3>("filtered")[0]),0.f,1e-7);
                        Done=true;Kernel().RequestExit();return;
                    }
                    EXPECT_FALSE(Results.back().Succeeded())<<Results.back().Message;
                    EXPECT_EQ(std::as_const(Props(8)).Get<glm::vec3>("filtered")[0],glm::vec3(77));
                    if(Phase==4){EXPECT_TRUE(CancelledIntermediate);EXPECT_EQ(History.UndoCount(),0);}
                }
                else
                {
                    PhaseMs.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-PhaseStarted).count());
                    double neighborhoodMs=0,fitMs=0;
                    std::size_t batches=0;
                    for(const auto& result:Results)
                    {
                        neighborhoodMs+=result.GpuNeighborhoodMilliseconds;
                        fitMs+=result.CpuComputeMilliseconds;
                        batches+=result.GpuQueryBatches;
                        EXPECT_TRUE(result.Succeeded())<<result.Message;
                        EXPECT_EQ(result.ActualBackend,"vulkan_lbvh");
                        EXPECT_GT(result.GpuQueryBatches,0);
                        if(Phase==1)EXPECT_TRUE(result.IndexReused);
                        EXPECT_EQ(result.CompletedIterations,3);EXPECT_EQ(result.WorkspaceBuilds,2);
                        const auto& ref=ReferenceResults[unsigned(result.Output.Domain)-1];
                        EXPECT_NEAR(result.Diagnostics.AverageDisplacement,ref.Diagnostics.AverageDisplacement,1e-5);
                        EXPECT_NEAR(result.Diagnostics.MaxDisplacement,ref.Diagnostics.MaxDisplacement,1e-5);

                    }
                    NeighborhoodMs.push_back(neighborhoodMs);FitMs.push_back(fitMs);BatchCounts.push_back(batches);
                    for(unsigned d=1;d<=8;++d)
                    {
                        const auto& values=std::as_const(Props(d)).Get<glm::vec3>(Config(d).Output.Name).Vector();
                        ASSERT_EQ(values.size(),Reference[d-1].size());
                        for(std::size_t i=0;i<values.size();++i)
                        {
                            if(Phase==2 && i==4) {EXPECT_TRUE(std::isnan(values[i].x));continue;}
                            const double error=glm::length(values[i]-Reference[d-1][i]);
                            MaxError=std::isfinite(error)?std::max(MaxError,error):std::numeric_limits<double>::infinity();
                        }
                        EXPECT_EQ(std::as_const(Props(d)).Get<float>("keep")[0],42.f);
                    }
                    EXPECT_LE(MaxError,1e-5);
                    EXPECT_EQ(History.UndoCount(),8);
                    for(unsigned i=0;i<8;++i)EXPECT_EQ(History.Undo().Status,Runtime::EditorCommandHistoryStatus::Undone);
                    for(unsigned d=1;d<=8;++d)EXPECT_EQ(std::as_const(Props(d)).Get<glm::vec3>(Config(d).Output.Name)[0],Phase==2?BeforePositions[d-1][0]:glm::vec3(77));
                    for(unsigned i=0;i<8;++i)EXPECT_EQ(History.Redo().Status,Runtime::EditorCommandHistoryStatus::Redone);
                    if(Phase==2){Done=true;Kernel().RequestExit();return;}
                }
                Results.clear();Submitted=false;++Phase;
            }
            if(Phase<3)
            {
                Reference.clear();ReferenceResults.clear();BeforePositions.clear();History.ClearHistory();Context.JobCommands={};Context.CommandHistory=nullptr;
                const auto cpuStart=std::chrono::steady_clock::now();
                for(unsigned d=1;d<=8;++d)
                {
                    BeforePositions.push_back(std::as_const(Props(d)).Get<glm::vec3>("samples").Vector());
                    auto c=Config(d);c.Backend=Runtime::BilateralFilterBackend::CpuOctree;
                    const auto result=Runtime::ApplyEditorBilateralFilterCommand(Context,c);
                    ASSERT_TRUE(result.Succeeded())<<result.Message;
                    ReferenceResults.push_back(result);
                    Reference.push_back(std::as_const(Props(d)).Get<glm::vec3>(Config(d).Output.Name).Vector());
                    if(Phase==2)Props(d).Get<glm::vec3>("samples").Vector()=BeforePositions.back();
                    else Props(d).Get<glm::vec3>("filtered").Vector().assign(66,glm::vec3(77));
                }
                CpuMs.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-cpuStart).count());
            }
            Context.JobCommands.Submit=[this](Runtime::JobDesc desc,Runtime::EditorJobIdentity){
                const bool intermediate=Phase==4 && desc.DebugName=="Bilateral position update" && !IntermediateToken.IsValid();
                if(intermediate)desc.IsReadyToApply=[] {return false;};
                FitToken=Kernel().Jobs().Submit(std::move(desc));StageTokens.push_back(FitToken);if(intermediate)IntermediateToken=FitToken;return FitToken;};
            StageTokens.clear();Context.CommandHistory=&History;ExpectedResults=Phase<3?8:1;Submitted=true;PhaseStarted=std::chrono::steady_clock::now();
            if(Phase<3)
            {
                if(Phase==0)
                {
                    auto unsupported=Config(8);unsupported.KNeighbors=64;
                    EXPECT_FALSE(Runtime::PreviewEditorBilateralFilterCommand(Context,unsupported).Ready);
                }
                for(unsigned d=1;d<=8;++d)
                {
                    const auto result=Runtime::ApplyEditorBilateralFilterCommand(Context,Config(d));
                    if(result.Status!=Runtime::EditorCommandStatus::Pending)Results.push_back(result);
                }
            }
            else
            {
                auto c=Config(8);
                if(Phase==4)
                {
                    // A completed update with zero normals leaves full, still-valid
                    // neighborhoods behind. Cancel it at the publication gate.
                    Props(8).Get<glm::vec3>("directions").Vector().assign(66,glm::vec3(0));
                    c.Iterations=2; // The parked update directly precedes the final query/update pair.
                    History.ClearHistory();
                }
                if(Phase==5)
                {
                    auto& p=Props(8);p.Resize(1030);
                    p.Get<glm::vec3>("samples").Vector().assign(1030,glm::vec3(0));
                    p.Get<glm::vec3>("samples")[1029]={10,0,0};
                    p.Get<glm::vec3>("directions").Vector().assign(1030,glm::vec3(0,0,1));
                    c.GpuQueryBatchSize=4096;
                }
                else Props(8).Get<glm::vec3>("filtered").Vector().assign(66,glm::vec3(77));
                const auto result=Runtime::ApplyEditorBilateralFilterCommand(Context,c);
                if(result.Status!=Runtime::EditorCommandStatus::Pending)Results.push_back(result);
                if(Phase==3)Props(8).Get<glm::vec3>("samples")[0]+=.1f; // stale before GPU recording
            }
        }
        void Shutdown() override {Context={};}
        Runtime::EditorGeometryProcessingContext Context{};
        Runtime::EditorCommandHistory History{};
        std::vector<Runtime::JobToken> StageTokens;
        std::vector<entt::entity> Entities;
        std::vector<std::vector<glm::vec3>> Reference,BeforePositions;
        std::vector<Runtime::EditorBilateralFilterResult> Results, ReferenceResults;
        std::vector<double> PhaseMs,CpuMs,NeighborhoodMs,FitMs;
        std::vector<std::size_t> BatchCounts;
        std::chrono::steady_clock::time_point Started{},PhaseStarted{};
        Runtime::JobToken FitToken{},IntermediateToken{};
        std::size_t ExpectedResults{};unsigned Phase{},CancelFrames{},ColdFrames{};bool Submitted{},Done{},TimedOut{},CancelledIntermediate{};double MaxError{};
    };
}
TEST(PointLBVHGpuSmoke, BilateralPublishesMovingPassesAcrossDomains)
{
    if(!Extrinsic::Platform::Backends::Glfw::CanInitialize())GTEST_SKIP()<<"GLFW unavailable";
    auto config=Runtime::CreateReferenceEngineConfig();
    config.Window.Width=64;config.Window.Height=64;config.Render.EnableValidation=true;
    config.Render.EnableVSync=false;config.ReferenceScene.Enabled=false;
    auto app=std::make_unique<BilateralApp>();auto* run=app.get();
    Intrinsic::Tests::RuntimeTestKernel engine(config,std::move(app));
    engine.EmplaceModule<Runtime::SpatialIndexCache>();engine.Initialize();Shutdown shutdown{engine};
    engine.Run();ASSERT_TRUE(engine.GetDevice().IsOperational());
    ASSERT_FALSE(run->TimedOut)<<"phase="<<run->Phase;ASSERT_TRUE(run->Done);
    EXPECT_LE(run->MaxError,1e-5);ASSERT_EQ(run->PhaseMs.size(),3);
    if(const auto* output=std::getenv("INTRINSIC_BILATERAL_BENCHMARK_OUTPUT"))
    {
        nlohmann::json json{{"benchmark_id","geometry.point_lbvh.bilateral_runtime_smoke"},
            {"method","geometry.point_lbvh"},{"backend","gpu_vulkan_compute"},
            {"dataset","builtin.bilateral_samples.eight_domains.seed241"},{"commit","local-dev"},
            {"metrics",{{"runtime_ms",run->PhaseMs[1]},{"quality_error_linf",run->MaxError}}},
            {"diagnostics",{{"runner","IntrinsicPointLBVHGpuTests"},{"mode","smoke"},
                {"cpu_reference_total_ms",run->CpuMs[1]},{"vulkan_cold_total_ms",run->PhaseMs[0]},
                {"vulkan_warm_total_ms",run->PhaseMs[1]},{"vulkan_in_place_k63_total_ms",run->PhaseMs[2]},
                {"cpu_in_place_k63_total_ms",run->CpuMs[2]},{"warmup_iterations",1},{"measured_iterations",1},
                {"cpu_weights_and_position_updates",true},{"gpu_neighborhood_elapsed_sum_ms",run->NeighborhoodMs},
                {"cpu_weights_and_position_updates_sum_ms",run->FitMs},{"gpu_query_batch_counts",run->BatchCounts}}},{"status",::testing::Test::HasFailure()?"failed":"passed"}};
        std::ofstream stream(output);ASSERT_TRUE(stream.good());stream<<json.dump(2)<<'\n';ASSERT_TRUE(stream.good());
    }
}

TEST(PointLBVHGpuSmoke, BilateralRejectsStaleAndCancelledPasses)
{
    if(!Extrinsic::Platform::Backends::Glfw::CanInitialize())GTEST_SKIP()<<"GLFW unavailable";
    auto config=Runtime::CreateReferenceEngineConfig();
    config.Window.Width=64;config.Window.Height=64;config.Render.EnableValidation=true;
    config.Render.EnableVSync=false;config.ReferenceScene.Enabled=false;
    auto app=std::make_unique<BilateralApp>();auto* run=app.get();run->Phase=3;
    Intrinsic::Tests::RuntimeTestKernel engine(config,std::move(app));
    engine.EmplaceModule<Runtime::SpatialIndexCache>();engine.Initialize();Shutdown shutdown{engine};
    engine.Run();ASSERT_TRUE(engine.GetDevice().IsOperational());
    ASSERT_FALSE(run->TimedOut)<<"phase="<<run->Phase;ASSERT_TRUE(run->Done);
    EXPECT_TRUE(run->CancelledIntermediate);
}

namespace
{
    class KeypointApp final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        Geometry::PropertySet& Props(unsigned d)
        {
            return *const_cast<Geometry::PropertySet*>(Runtime::ResolveGeometryPropertySet(
                Runtime::BuildGeometryAvailability(Context.Scene->Raw(),Entities[d-1]),Domain(d)));
        }
        Runtime::KeypointAnalysisConfig Config(unsigned d) const
        {
            Runtime::KeypointAnalysisConfig c;
            c.StableEntityId=Runtime::SelectionController::ToStableEntityId(Entities[d-1]);
            c.Positions={Domain(d),"samples",Geometry::PropertyValueKind::Vec3};
            c.Mask={Domain(d),"keypoints",Geometry::PropertyValueKind::UInt32};
            c.Score={Domain(d),"saliency",Geometry::PropertyValueKind::Float};
            c.SalientRadius=Phase==2?0:2;c.NonMaxRadius=Phase==2?0:.5f;
            c.Backend=Runtime::KeypointAnalysisBackend::VulkanLBVH;c.GpuQueryBatchSize=128;
            return c;
        }
        void Resolve() override
        {
            Started=std::chrono::steady_clock::now();
            Context.Scene=Kernel().Worlds().Get(Kernel().ActiveWorld());Context.World=Kernel().ActiveWorld();
            Context.SpatialIndices=Kernel().Services().Find<Runtime::SpatialIndexCache>();
            std::mt19937 random(241);std::uniform_real_distribution<float> dist(-1,1);
            std::vector<glm::vec3> points;
            for(unsigned i=0;i<66;++i){const float x=dist(random),y=dist(random);points.push_back({x,y,.1f*dist(random)});}
            points[0]={0,0,0};points[1]={.3f,.4f,0};points[2]=points[0];
            points[3]={std::nextafter(.5f,1.f),0,0};points[65]={10,0,0};
            for(unsigned d=1;d<=8;++d)
            {
                auto entity=Context.Scene->Create();Entities.push_back(entity);
                if(d<=unsigned(Domain::MeshFace))
                {
                    Geometry::HalfedgeMesh::Mesh mesh;
                    auto a=mesh.AddVertex({0,0,0}),b=mesh.AddVertex({1,0,0}),c=mesh.AddVertex({0,1,0});
                    (void)mesh.AddTriangle(a,b,c);GS::PopulateFromMesh(Context.Scene->Raw(),entity,mesh);
                }
                else if(d<unsigned(Domain::PointCloudPoint))
                {
                    Geometry::Graph::Graph graph;auto a=graph.AddVertex({0,0,0}),b=graph.AddVertex({1,0,0});
                    (void)graph.AddEdge(a,b);GS::PopulateFromGraph(Context.Scene->Raw(),entity,graph);
                }
                else Context.Scene->Raw().emplace<GS::Vertices>(entity);
                auto& p=Props(d);p.Resize(points.size());p.GetOrAdd<glm::vec3>("samples").Vector()=points;
                p.GetOrAdd<float>("keep").Vector().assign(points.size(),42);
                p.GetOrAdd<float>("saliency").Vector().assign(points.size(),77);
                p.GetOrAdd<std::uint32_t>("keypoints").Vector().assign(points.size(),77);
                const bool half=d==unsigned(Domain::MeshHalfedge)||d==unsigned(Domain::GraphHalfedge);
                if(half)
                {
                    auto& edges=Context.Scene->Raw().get<GS::Edges>(entity).Properties;
                    edges.Resize(points.size()/2);edges.GetOrAdd<bool>("e:deleted")[2]=true;
                }
                else p.GetOrAdd<bool>(d==unsigned(Domain::MeshFace)?"f:deleted":
                    (d==unsigned(Domain::MeshEdge)||d==unsigned(Domain::GraphEdge))?"e:deleted":"v:deleted")[4]=true;
                p.Get<glm::vec3>("samples")[4]={std::numeric_limits<float>::quiet_NaN(),0,0};
            }
            Context.MethodResultSinks.KeypointAnalysis=[this](auto result){Results.push_back(std::move(result));};
        }
        void Frame(double,double) override
        {
            if(std::chrono::steady_clock::now()-Started>std::chrono::seconds(95))
            {ADD_FAILURE()<<"Keypoint timeout: phase="<<Phase<<" results="<<Results.size();TimedOut=true;Kernel().RequestExit();return;}
            if(!Kernel().GetDevice().IsOperational())
            {if(++ColdFrames>4){TimedOut=true;Kernel().RequestExit();}return;}
            if(Submitted)
            {
                if(Phase==4 && !Cancelled && Kernel().Jobs().GetState(ScaleToken)==Runtime::JobState::AwaitingApply)
                {EXPECT_TRUE(Kernel().Jobs().Cancel(ScaleToken));Cancelled=true;}
                if(Phase==4)(void)Kernel().Jobs().ReapCompleted();
                if(Results.size()<(Phase<3?8:1))return;
                if(Phase>=6)
                    for(auto token:SubmissionTokens)
                        if(!Kernel().Jobs().IsComplete(token))return;
                if(Phase<3)
                {
                    PhaseMs.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-PhaseStarted).count());
                    for(const auto& result:Results)
                    {
                        EXPECT_TRUE(result.Succeeded())<<result.Message;EXPECT_EQ(result.ActualBackend,"vulkan_lbvh");
                        EXPECT_GT(result.GpuQueryBatches,0);if(Phase>0)EXPECT_TRUE(result.IndexReused);
                        const auto& reference=ReferenceResults[unsigned(result.Mask.Domain)-1];
                        EXPECT_EQ(result.KeypointCount,reference.KeypointCount);
                        EXPECT_FLOAT_EQ(result.Scale.MeanSpacing,reference.Scale.MeanSpacing);
                        EXPECT_FLOAT_EQ(result.Scale.SalientRadius,reference.Scale.SalientRadius);
                        EXPECT_FLOAT_EQ(result.Scale.NonMaxRadius,reference.Scale.NonMaxRadius);
                    }
                    for(unsigned d=1;d<=8;++d)
                    {
                        const auto& p=std::as_const(Props(d));
                        EXPECT_EQ(p.Get<std::uint32_t>("keypoints").Vector(),ReferenceMasks[d-1]);
                        const auto scores=p.Get<float>("saliency");
                        for(std::size_t i=0;i<scores.Size();++i)
                        {
                            const double error=std::abs(double(scores[i])-ReferenceScores[d-1][i]);
                            MaxError=std::isfinite(error)?std::max(MaxError,error):std::numeric_limits<double>::infinity();
                        }
                        EXPECT_EQ(p.Get<float>("keep")[0],42);EXPECT_EQ(p.Get<float>("saliency")[4],77);
                    }
                    EXPECT_LE(MaxError,1e-5);EXPECT_EQ(History.UndoCount(),8);
                    for(unsigned i=0;i<8;++i)EXPECT_EQ(History.Undo().Status,Runtime::EditorCommandHistoryStatus::Undone);
                    for(unsigned d=1;d<=8;++d)
                    {EXPECT_EQ(std::as_const(Props(d)).Get<float>("saliency")[0],77);EXPECT_EQ(std::as_const(Props(d)).Get<std::uint32_t>("keypoints")[0],77);}
                    for(unsigned i=0;i<8;++i)EXPECT_EQ(History.Redo().Status,Runtime::EditorCommandHistoryStatus::Redone);
                    if(Phase==2){Done=true;Kernel().RequestExit();return;}
                }
                else
                {
                    EXPECT_FALSE(Results.back().Succeeded())<<Results.back().Message;
                    EXPECT_EQ(std::as_const(Props(8)).Get<float>("saliency")[0],77);
                    EXPECT_EQ(std::as_const(Props(8)).Get<std::uint32_t>("keypoints")[0],77);EXPECT_EQ(History.UndoCount(),0);
                    if(Phase==4)EXPECT_TRUE(Cancelled);
                    if(Phase>=6)
                    {
                        EXPECT_EQ(Results.back().Status,Runtime::EditorCommandStatus::GeometryProcessingFailed);
                        EXPECT_NE(Results.back().Message.find("submission rejected"),std::string::npos);
                        if(Phase==7){Done=true;Kernel().RequestExit();return;}
                    }
                    if(Phase==5)
                    {
                        EXPECT_EQ(Results.back().Status,Runtime::EditorCommandStatus::GeometryProcessingFailed);
                        EXPECT_NE(Results.back().Message.find("overflow"),std::string::npos);
                        EXPECT_GT(Results.back().MaximumNeighbors,1024);EXPECT_GT(Results.back().GpuQueryBatches,0);
                    }
                }
                Results.clear();Submitted=false;++Phase;
            }
            History.ClearHistory();Context.JobCommands={};Context.CommandHistory=nullptr;
            if(Phase<3)
            {
                ReferenceScores.clear();ReferenceMasks.clear();ReferenceResults.clear();const auto cpuStart=std::chrono::steady_clock::now();
                for(unsigned d=1;d<=8;++d)
                {
                    auto c=Config(d);c.Backend=Runtime::KeypointAnalysisBackend::CpuKDTree;
                    const auto result=Runtime::ApplyEditorKeypointAnalysisCommand(Context,c);ASSERT_TRUE(result.Succeeded())<<result.Message;
                    ReferenceResults.push_back(result);ReferenceScores.push_back(std::as_const(Props(d)).Get<float>("saliency").Vector());
                    ReferenceMasks.push_back(std::as_const(Props(d)).Get<std::uint32_t>("keypoints").Vector());
                    Props(d).Get<float>("saliency").Vector().assign(66,77);Props(d).Get<std::uint32_t>("keypoints").Vector().assign(66,77);
                }
                CpuMs.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-cpuStart).count());
            }
            SubmissionCount=0;SubmissionTokens.clear();
            Context.JobCommands.Submit=[this](Runtime::JobDesc desc,Runtime::EditorJobIdentity)
            {
                ++SubmissionCount;
                if((Phase==6 && SubmissionCount==2) || (Phase==7 && SubmissionCount==3))return Runtime::JobToken{};
                const bool scale=Phase==4 && desc.DebugName=="Keypoint scale";
                if(scale)desc.IsReadyToApply=[] {return false;};
                const auto token=Kernel().Jobs().Submit(std::move(desc));SubmissionTokens.push_back(token);if(scale)ScaleToken=token;return token;
            };
            Context.CommandHistory=&History;Submitted=true;PhaseStarted=std::chrono::steady_clock::now();
            if(Phase<3)
                for(unsigned d=1;d<=8;++d)
                {const auto r=Runtime::ApplyEditorKeypointAnalysisCommand(Context,Config(d));if(r.Status!=Runtime::EditorCommandStatus::Pending)Results.push_back(r);}
            else
            {
                auto c=Config(8);auto& p=Props(8);
                if(Phase==5)
                {
                    p.Resize(1030);p.Get<glm::vec3>("samples").Vector().assign(1030,glm::vec3(0));
                    p.Get<glm::vec3>("samples")[1029]={10,0,0};c.GpuRadiusCapacity=1024;
                }
                p.Get<float>("saliency").Vector().assign(p.Size(),77);p.Get<std::uint32_t>("keypoints").Vector().assign(p.Size(),77);
                const auto r=Runtime::ApplyEditorKeypointAnalysisCommand(Context,c);if(r.Status!=Runtime::EditorCommandStatus::Pending)Results.push_back(r);
                if(Phase==3)p.Get<glm::vec3>("samples")[0]+=.1f;
            }
        }
        void Shutdown() override {Context={};}
        Runtime::EditorGeometryProcessingContext Context{};Runtime::EditorCommandHistory History{};
        std::vector<entt::entity> Entities;
        std::vector<std::vector<float>> ReferenceScores;
        std::vector<std::vector<std::uint32_t>> ReferenceMasks;
        std::vector<Runtime::EditorKeypointAnalysisResult> Results,ReferenceResults;
        std::vector<double> PhaseMs,CpuMs;
        std::chrono::steady_clock::time_point Started{},PhaseStarted{};
        Runtime::JobToken ScaleToken{};std::vector<Runtime::JobToken> SubmissionTokens;unsigned Phase{},ColdFrames{},SubmissionCount{};
        bool Submitted{},Done{},TimedOut{},Cancelled{};double MaxError{};
    };
}
TEST(PointLBVHGpuSmoke, KeypointPublishesAcrossDomainsWithCompleteSupport)
{
    if(!Extrinsic::Platform::Backends::Glfw::CanInitialize())GTEST_SKIP()<<"GLFW unavailable";
    auto config=Runtime::CreateReferenceEngineConfig();config.Window.Width=64;config.Window.Height=64;
    config.Render.EnableValidation=true;config.Render.EnableVSync=false;config.ReferenceScene.Enabled=false;
    auto app=std::make_unique<KeypointApp>();auto* run=app.get();
    Intrinsic::Tests::RuntimeTestKernel engine(config,std::move(app));engine.EmplaceModule<Runtime::SpatialIndexCache>();
    engine.Initialize();Shutdown shutdown{engine};engine.Run();ASSERT_TRUE(engine.GetDevice().IsOperational());
    ASSERT_FALSE(run->TimedOut)<<"phase="<<run->Phase;ASSERT_TRUE(run->Done);ASSERT_EQ(run->PhaseMs.size(),3);
    EXPECT_LE(run->MaxError,1e-5);
    if(const auto* output=std::getenv("INTRINSIC_KEYPOINT_BENCHMARK_OUTPUT"))
    {
        nlohmann::json json{{"benchmark_id","geometry.point_lbvh.keypoint_runtime_smoke"},{"method","geometry.point_lbvh"},
            {"backend","gpu_vulkan_compute"},{"dataset","builtin.keypoint_samples.eight_domains.seed241"},{"commit","local-dev"},
            {"metrics",{{"runtime_ms",run->PhaseMs[1]},{"quality_error_linf",run->MaxError}}},
            {"diagnostics",{{"runner","IntrinsicPointLBVHGpuTests"},{"mode","smoke"},{"cpu_reference_total_ms",run->CpuMs[1]},
                {"vulkan_cold_total_ms",run->PhaseMs[0]},{"vulkan_warm_total_ms",run->PhaseMs[1]},
                {"vulkan_automatic_radius_total_ms",run->PhaseMs[2]},{"warmup_iterations",1},{"measured_iterations",1},
                {"cpu_scale_covariance_suppression",true}}},{"status",::testing::Test::HasFailure()?"failed":"passed"}};
        std::ofstream stream(output);ASSERT_TRUE(stream.good());stream<<json.dump(2)<<'\n';ASSERT_TRUE(stream.good());
    }
}
TEST(PointLBVHGpuSmoke, KeypointRejectsStaleCancellationAndRadiusOverflow)
{
    if(!Extrinsic::Platform::Backends::Glfw::CanInitialize())GTEST_SKIP()<<"GLFW unavailable";
    auto config=Runtime::CreateReferenceEngineConfig();config.Window.Width=64;config.Window.Height=64;
    config.Render.EnableValidation=true;config.Render.EnableVSync=false;config.ReferenceScene.Enabled=false;
    auto app=std::make_unique<KeypointApp>();auto* run=app.get();run->Phase=3;
    Intrinsic::Tests::RuntimeTestKernel engine(config,std::move(app));engine.EmplaceModule<Runtime::SpatialIndexCache>();
    engine.Initialize();Shutdown shutdown{engine};engine.Run();ASSERT_TRUE(engine.GetDevice().IsOperational());
    ASSERT_FALSE(run->TimedOut)<<"phase="<<run->Phase;ASSERT_TRUE(run->Done);EXPECT_TRUE(run->Cancelled);
}

namespace
{
    class DescriptorApp final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        Geometry::PropertySet& Props(unsigned d)
        {
            return *const_cast<Geometry::PropertySet*>(Runtime::ResolveGeometryPropertySet(
                Runtime::BuildGeometryAvailability(Context.Scene->Raw(),Entities[d-1]),Domain(d)));
        }
        Runtime::DescriptorAnalysisConfig Config(unsigned d) const
        {
            Runtime::DescriptorAnalysisConfig c;
            c.StableEntityId=Runtime::SelectionController::ToStableEntityId(Entities[d-1]);
            c.Positions={Domain(d),"samples",Geometry::PropertyValueKind::Vec3};
            c.Normals={Domain(d),"directions",Geometry::PropertyValueKind::Vec3};
            c.Outputs=Runtime::MakeDescriptorOutputProperties(Domain(d),"descriptor");
            c.FeatureRadius=Phase==2?0:2;c.MaxNeighbors=Phase==2?0:8;
            c.Backend=Runtime::DescriptorAnalysisBackend::VulkanLBVH;c.GpuQueryBatchSize=128;
            return c;
        }
        void Resolve() override
        {
            Started=std::chrono::steady_clock::now();
            Context.Scene=Kernel().Worlds().Get(Kernel().ActiveWorld());Context.World=Kernel().ActiveWorld();
            Context.SpatialIndices=Kernel().Services().Find<Runtime::SpatialIndexCache>();
            std::mt19937 random(241);std::uniform_real_distribution<float> dist(-1,1);
            std::vector<glm::vec3> points;
            for(unsigned i=0;i<66;++i){const float x=dist(random),y=dist(random);points.push_back({x,y,.1f*dist(random)});}
            points[0]={0,0,0};points[1]={1.2f,1.6f,0};points[2]=points[0];
            points[3]={std::nextafter(2.f,3.f),0,0};points[65]={10,0,0};
            for(unsigned d=1;d<=8;++d)
            {
                auto entity=Context.Scene->Create();Entities.push_back(entity);
                if(d<=unsigned(Domain::MeshFace))
                {
                    Geometry::HalfedgeMesh::Mesh mesh;
                    auto a=mesh.AddVertex({0,0,0}),b=mesh.AddVertex({1,0,0}),c=mesh.AddVertex({0,1,0});
                    (void)mesh.AddTriangle(a,b,c);GS::PopulateFromMesh(Context.Scene->Raw(),entity,mesh);
                }
                else if(d<unsigned(Domain::PointCloudPoint))
                {
                    Geometry::Graph::Graph graph;auto a=graph.AddVertex({0,0,0}),b=graph.AddVertex({1,0,0});
                    (void)graph.AddEdge(a,b);GS::PopulateFromGraph(Context.Scene->Raw(),entity,graph);
                }
                else Context.Scene->Raw().emplace<GS::Vertices>(entity);
                auto& p=Props(d);p.Resize(points.size());p.GetOrAdd<glm::vec3>("samples").Vector()=points;
                p.GetOrAdd<float>("keep").Vector().assign(points.size(),42);
                auto normals=p.GetOrAdd<glm::vec3>("directions");
                for(std::size_t i=0;i<points.size();++i)normals[i]={.2f*points[i].x,-.1f*points[i].y,1};
                for(const auto& output:Config(d).Outputs)p.GetOrAdd<float>(output.Name).Vector().assign(points.size(),77);
                const bool half=d==unsigned(Domain::MeshHalfedge)||d==unsigned(Domain::GraphHalfedge);
                if(half)
                {
                    auto& edges=Context.Scene->Raw().get<GS::Edges>(entity).Properties;
                    edges.Resize(points.size()/2);edges.GetOrAdd<bool>("e:deleted")[2]=true;
                }
                else p.GetOrAdd<bool>(d==unsigned(Domain::MeshFace)?"f:deleted":
                    (d==unsigned(Domain::MeshEdge)||d==unsigned(Domain::GraphEdge))?"e:deleted":"v:deleted")[4]=true;
                p.Get<glm::vec3>("samples")[4]={std::numeric_limits<float>::quiet_NaN(),0,0};
            }
            Context.MethodResultSinks.DescriptorAnalysis=[this](auto result){Results.push_back(std::move(result));};
        }
        void Frame(double,double) override
        {
            if(std::chrono::steady_clock::now()-Started>std::chrono::seconds(95))
            {ADD_FAILURE()<<"Descriptor timeout: phase="<<Phase<<" results="<<Results.size();TimedOut=true;Kernel().RequestExit();return;}
            if(!Kernel().GetDevice().IsOperational())
            {if(++ColdFrames>4){TimedOut=true;Kernel().RequestExit();}return;}
            if(Submitted)
            {
                if(Phase==4 && !Cancelled && Kernel().Jobs().GetState(ScaleToken)==Runtime::JobState::AwaitingApply)
                {EXPECT_TRUE(Kernel().Jobs().Cancel(ScaleToken));Cancelled=true;}
                if(Phase==4)(void)Kernel().Jobs().ReapCompleted();
                if(Results.size()<(Phase<3?8:1))return;
                // Partial submission must settle accepted jobs. A successful
                // chain's parents may already be reaped when its result arrives.
                if(Phase==6 || Phase==7)
                    for(auto token:SubmissionTokens)
                        if(!Kernel().Jobs().IsComplete(token))return;
                if(Phase==8)
                {
                    ASSERT_TRUE(Results.back().Succeeded())<<Results.back().Message;
                    EXPECT_EQ(Results.back().ActualBackend,"vulkan_lbvh");EXPECT_GT(Results.back().MaximumNeighbors,1024);
                    for(unsigned b=0;b<33;++b)
                    {
                        const auto values=std::as_const(Props(8)).Get<float>(Config(8).Outputs[b].Name);
                        for(std::size_t i=0;i<values.Size();++i)EXPECT_NEAR(values[i],DenseReference[b][i],1e-5);
                    }
                    EXPECT_EQ(History.UndoCount(),1);EXPECT_TRUE(History.Undo().Succeeded());
                    for(const auto& output:Config(8).Outputs)EXPECT_EQ(std::as_const(Props(8)).Get<float>(output.Name)[0],77);
                    EXPECT_TRUE(History.Redo().Succeeded());Done=true;Kernel().RequestExit();return;
                }
                if(Phase<3)
                {
                    PhaseMs.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-PhaseStarted).count());
                    for(const auto& result:Results)
                    {
                        EXPECT_TRUE(result.Succeeded())<<result.Message;EXPECT_EQ(result.ActualBackend,"vulkan_lbvh");
                        EXPECT_GT(result.GpuQueryBatches,0);if(Phase>0)EXPECT_TRUE(result.IndexReused);
                        const auto& reference=ReferenceResults[unsigned(result.Outputs[0].Domain)-1];
                        EXPECT_EQ(result.WrittenCount,reference.WrittenCount);
                        EXPECT_FLOAT_EQ(result.Scale.MeanSpacing,reference.Scale.MeanSpacing);
                        EXPECT_FLOAT_EQ(result.Scale.FeatureRadius,reference.Scale.FeatureRadius);
                    }
                    for(unsigned d=1;d<=8;++d)
                    {
                        const auto& p=std::as_const(Props(d));
                        for(unsigned b=0;b<33;++b)
                        {
                            const auto values=p.Get<float>(Config(d).Outputs[b].Name);
                            for(std::size_t i=0;i<values.Size();++i)
                            {
                                const double error=std::abs(double(values[i])-ReferenceColumns[d-1][b][i]);
                                MaxError=std::isfinite(error)?std::max(MaxError,error):std::numeric_limits<double>::infinity();
                            }
                            EXPECT_EQ(values[4],77);
                            if(d==unsigned(Domain::MeshHalfedge)||d==unsigned(Domain::GraphHalfedge))EXPECT_EQ(values[5],77);
                        }
                        EXPECT_EQ(p.Get<float>("keep")[0],42);
                    }
                    EXPECT_LE(MaxError,1e-5);EXPECT_EQ(History.UndoCount(),8);
                    for(unsigned i=0;i<8;++i)EXPECT_EQ(History.Undo().Status,Runtime::EditorCommandHistoryStatus::Undone);
                    for(unsigned d=1;d<=8;++d)
                        for(const auto& output:Config(d).Outputs)EXPECT_EQ(std::as_const(Props(d)).Get<float>(output.Name)[0],77);
                    for(unsigned i=0;i<8;++i)EXPECT_EQ(History.Redo().Status,Runtime::EditorCommandHistoryStatus::Redone);
                    if(Phase==2){Done=true;Kernel().RequestExit();return;}
                }
                else
                {
                    EXPECT_FALSE(Results.back().Succeeded())<<Results.back().Message;
                    for(const auto& output:Config(8).Outputs)EXPECT_EQ(std::as_const(Props(8)).Get<float>(output.Name)[0],77);
                    EXPECT_EQ(History.UndoCount(),0);
                    if(Phase==4)EXPECT_TRUE(Cancelled);
                    if(Phase>=6)
                    {
                        EXPECT_EQ(Results.back().Status,Runtime::EditorCommandStatus::GeometryProcessingFailed);
                        EXPECT_NE(Results.back().Message.find("submission rejected"),std::string::npos);
                        if(Phase==7){Done=true;Kernel().RequestExit();return;}
                    }
                    if(Phase==5)
                    {
                        EXPECT_EQ(Results.back().Status,Runtime::EditorCommandStatus::GeometryProcessingFailed);
                        EXPECT_NE(Results.back().Message.find("overflow"),std::string::npos);
                        EXPECT_GT(Results.back().MaximumNeighbors,1024);EXPECT_GT(Results.back().GpuQueryBatches,0);
                    }
                }
                Results.clear();Submitted=false;++Phase;
            }
            History.ClearHistory();Context.JobCommands={};Context.CommandHistory=nullptr;
            if(Phase<3)
            {
                ReferenceColumns.clear();ReferenceResults.clear();const auto cpuStart=std::chrono::steady_clock::now();
                for(unsigned d=1;d<=8;++d)
                {
                    auto c=Config(d);c.Backend=Runtime::DescriptorAnalysisBackend::CpuKDTree;
                    const auto result=Runtime::ApplyEditorDescriptorAnalysisCommand(Context,c);ASSERT_TRUE(result.Succeeded())<<result.Message;
                    ReferenceResults.push_back(result);std::array<std::vector<float>,33> columns;
                    for(unsigned b=0;b<33;++b)
                    {
                        columns[b]=std::as_const(Props(d)).Get<float>(c.Outputs[b].Name).Vector();
                        Props(d).Get<float>(c.Outputs[b].Name).Vector().assign(66,77);
                    }
                    ReferenceColumns.push_back(std::move(columns));
                }
                CpuMs.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-cpuStart).count());
            }
            SubmissionCount=0;SubmissionTokens.clear();
            Context.JobCommands.Submit=[this](Runtime::JobDesc desc,Runtime::EditorJobIdentity)
            {
                ++SubmissionCount;
                if((Phase==6 && SubmissionCount==2) || (Phase==7 && SubmissionCount==3))return Runtime::JobToken{};
                const bool scale=Phase==4 && desc.DebugName=="Descriptor scale";
                if(scale)desc.IsReadyToApply=[] {return false;};
                const auto token=Kernel().Jobs().Submit(std::move(desc));SubmissionTokens.push_back(token);if(scale)ScaleToken=token;return token;
            };
            Context.CommandHistory=&History;Submitted=true;PhaseStarted=std::chrono::steady_clock::now();
            if(Phase<3)
                for(unsigned d=1;d<=8;++d)
                {const auto r=Runtime::ApplyEditorDescriptorAnalysisCommand(Context,Config(d));if(r.Status!=Runtime::EditorCommandStatus::Pending)Results.push_back(r);}
            else
            {
                auto c=Config(8);auto& p=Props(8);
                if(Phase==5)
                {
                    p.Resize(1030);p.Get<glm::vec3>("samples").Vector().assign(1030,glm::vec3(0));
                    p.Get<glm::vec3>("samples")[1029]={10,0,0};c.GpuRadiusCapacity=1024;c.MaxNeighbors=0;
                    p.Get<glm::vec3>("directions").Vector().assign(1030,glm::vec3(0,0,1));
                }
                if(Phase==8)
                {
                    p.Resize(1030);auto positions=p.Get<glm::vec3>("samples"),normals=p.Get<glm::vec3>("directions");
                    for(unsigned i=0;i<1030;++i)
                    {positions[i]={.001f*float(i%31),.001f*float(i/31),.0001f*float(i%7)};normals[i]={.01f*float(i%13),.01f*float(i%17),1};}
                    positions[1029]={10,0,0};c.MaxNeighbors=1;c.GpuRadiusCapacity=1;
                    auto referenceContext=Context;referenceContext.JobCommands={};referenceContext.CommandHistory=nullptr;
                    auto referenceConfig=c;referenceConfig.Backend=Runtime::DescriptorAnalysisBackend::CpuKDTree;
                    const auto reference=Runtime::ApplyEditorDescriptorAnalysisCommand(referenceContext,referenceConfig);ASSERT_TRUE(reference.Succeeded())<<reference.Message;
                    for(unsigned b=0;b<33;++b)DenseReference[b]=std::as_const(p).Get<float>(c.Outputs[b].Name).Vector();
                    EXPECT_GT(DenseReference[5][0],0);
                }
                for(const auto& output:c.Outputs)p.Get<float>(output.Name).Vector().assign(p.Size(),77);
                const auto r=Runtime::ApplyEditorDescriptorAnalysisCommand(Context,c);if(r.Status!=Runtime::EditorCommandStatus::Pending)Results.push_back(r);
                if(Phase==3)p.Get<glm::vec3>("directions")[0]+=.1f;
            }
        }
        void Shutdown() override {Context={};}
        Runtime::EditorGeometryProcessingContext Context{};Runtime::EditorCommandHistory History{};
        std::vector<entt::entity> Entities;
        std::vector<std::array<std::vector<float>,33>> ReferenceColumns;
        std::array<std::vector<float>,33> DenseReference;
        std::vector<Runtime::EditorDescriptorAnalysisResult> Results,ReferenceResults;
        std::vector<double> PhaseMs,CpuMs;
        std::chrono::steady_clock::time_point Started{},PhaseStarted{};
        Runtime::JobToken ScaleToken{};std::vector<Runtime::JobToken> SubmissionTokens;unsigned Phase{},ColdFrames{},SubmissionCount{};
        bool Submitted{},Done{},TimedOut{},Cancelled{};double MaxError{};
    };
}
TEST(PointLBVHGpuSmoke, DescriptorPublishesAcrossDomainsWithCompleteSupport)
{
    if(!Extrinsic::Platform::Backends::Glfw::CanInitialize())GTEST_SKIP()<<"GLFW unavailable";
    auto config=Runtime::CreateReferenceEngineConfig();config.Window.Width=64;config.Window.Height=64;
    config.Render.EnableValidation=true;config.Render.EnableVSync=false;config.ReferenceScene.Enabled=false;
    auto app=std::make_unique<DescriptorApp>();auto* run=app.get();
    Intrinsic::Tests::RuntimeTestKernel engine(config,std::move(app));engine.EmplaceModule<Runtime::SpatialIndexCache>();
    engine.Initialize();Shutdown shutdown{engine};engine.Run();ASSERT_TRUE(engine.GetDevice().IsOperational());
    ASSERT_FALSE(run->TimedOut)<<"phase="<<run->Phase;ASSERT_TRUE(run->Done);ASSERT_EQ(run->PhaseMs.size(),3);
    EXPECT_LE(run->MaxError,1e-5);
    if(const auto* output=std::getenv("INTRINSIC_DESCRIPTOR_BENCHMARK_OUTPUT"))
    {
        nlohmann::json json{{"benchmark_id","geometry.point_lbvh.descriptor_runtime_smoke"},{"method","geometry.point_lbvh"},
            {"backend","gpu_vulkan_compute"},{"dataset","builtin.descriptor_samples.eight_domains.seed241"},{"commit","local-dev"},
            {"metrics",{{"runtime_ms",run->PhaseMs[1]},{"quality_error_linf",run->MaxError}}},
            {"diagnostics",{{"runner","IntrinsicPointLBVHGpuTests"},{"mode","smoke"},{"cpu_reference_total_ms",run->CpuMs[1]},
                {"vulkan_cold_total_ms",run->PhaseMs[0]},{"vulkan_warm_total_ms",run->PhaseMs[1]},
                {"vulkan_automatic_radius_total_ms",run->PhaseMs[2]},{"warmup_iterations",1},{"measured_iterations",1},
                {"cpu_scale_spfh_fpfh",true}}},{"status",::testing::Test::HasFailure()?"failed":"passed"}};
        std::ofstream stream(output);ASSERT_TRUE(stream.good());stream<<json.dump(2)<<'\n';ASSERT_TRUE(stream.good());
    }
}
TEST(PointLBVHGpuSmoke, DescriptorRejectsStaleCancellationAndRadiusOverflow)
{
    if(!Extrinsic::Platform::Backends::Glfw::CanInitialize())GTEST_SKIP()<<"GLFW unavailable";
    auto config=Runtime::CreateReferenceEngineConfig();config.Window.Width=64;config.Window.Height=64;
    config.Render.EnableValidation=true;config.Render.EnableVSync=false;config.ReferenceScene.Enabled=false;
    auto app=std::make_unique<DescriptorApp>();auto* run=app.get();run->Phase=3;
    Intrinsic::Tests::RuntimeTestKernel engine(config,std::move(app));engine.EmplaceModule<Runtime::SpatialIndexCache>();
    engine.Initialize();Shutdown shutdown{engine};engine.Run();ASSERT_TRUE(engine.GetDevice().IsOperational());
    ASSERT_FALSE(run->TimedOut)<<"phase="<<run->Phase;ASSERT_TRUE(run->Done);EXPECT_TRUE(run->Cancelled);
}

TEST(PointLBVHGpuSmoke, DescriptorLowestIdCapSupportsDenseRadius)
{
    if(!Extrinsic::Platform::Backends::Glfw::CanInitialize())GTEST_SKIP()<<"GLFW unavailable";
    auto config=Runtime::CreateReferenceEngineConfig();config.Window.Width=64;config.Window.Height=64;
    config.Render.EnableValidation=true;config.Render.EnableVSync=false;config.ReferenceScene.Enabled=false;
    auto app=std::make_unique<DescriptorApp>();auto* run=app.get();run->Phase=8;
    Intrinsic::Tests::RuntimeTestKernel engine(config,std::move(app));engine.EmplaceModule<Runtime::SpatialIndexCache>();
    engine.Initialize();Shutdown shutdown{engine};engine.Run();ASSERT_TRUE(engine.GetDevice().IsOperational());
    ASSERT_FALSE(run->TimedOut)<<"phase="<<run->Phase;ASSERT_TRUE(run->Done);
}

namespace
{
    class DensityWeightApp final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        Geometry::PropertySet& Props(unsigned d)
        {
            return *const_cast<Geometry::PropertySet*>(Runtime::ResolveGeometryPropertySet(
                Runtime::BuildGeometryAvailability(Context.Scene->Raw(),Entities[d-1]),Domain(d)));
        }

        Runtime::DensityWeightConfig Config(unsigned d) const
        {
            Runtime::DensityWeightConfig c;
            c.StableEntityId=Runtime::SelectionController::ToStableEntityId(Entities[d-1]);
            c.Positions={Domain(d),"samples",Geometry::PropertyValueKind::Vec3};
            c.Weights={Domain(d),"weights",Geometry::PropertyValueKind::Float};c.SupportRadius=2;
            c.Backend=Runtime::DensityWeightBackend::VulkanLBVH;c.GpuQueryBatchSize=128;
            if(Phase==2 || Phase==3 || Phase>=20)c.Kernel=decltype(c.Kernel)::Gaussian;
            if(Phase==4 || Phase==5)c.Kernel=decltype(c.Kernel)::WendlandC2;
            if(Phase==3 || Phase==5 || Phase==6)c.Mode=decltype(c.Mode)::Reciprocal;
            if(Phase==12)c.GpuRadiusCapacity=1024;
            if(Phase==20 || Phase==21)c.SupportRadius=4.6766236639043417e-23;
            if(Phase==22)c.SupportRadius=1e-310;
            return c;
        }
        void Resolve() override
        {
            Started=std::chrono::steady_clock::now();
            Context.Scene=Kernel().Worlds().Get(Kernel().ActiveWorld());Context.World=Kernel().ActiveWorld();
            Context.SpatialIndices=Kernel().Services().Find<Runtime::SpatialIndexCache>();
            std::mt19937 random(251);std::uniform_real_distribution<float> dist(-1,1);
            std::vector<glm::vec3> points;
            for(unsigned i=0;i<66;++i){const float x=dist(random),y=dist(random);points.push_back({x,y,.1f*dist(random)});}
            points[0]={0,0,0};points[1]={2,0,0};points[2]=points[0];
            points[3]={std::nextafter(2.f,3.f),0,0};points[65]={10,0,0};
            for(unsigned d=1;d<=8;++d)
            {
                auto entity=Context.Scene->Create();Entities.push_back(entity);
                if(d<=unsigned(Domain::MeshFace))
                {
                    Geometry::HalfedgeMesh::Mesh mesh;
                    auto a=mesh.AddVertex({0,0,0}),b=mesh.AddVertex({1,0,0}),c=mesh.AddVertex({0,1,0});
                    (void)mesh.AddTriangle(a,b,c);GS::PopulateFromMesh(Context.Scene->Raw(),entity,mesh);
                }
                else if(d<unsigned(Domain::PointCloudPoint))
                {
                    Geometry::Graph::Graph graph;auto a=graph.AddVertex({0,0,0}),b=graph.AddVertex({1,0,0});
                    (void)graph.AddEdge(a,b);GS::PopulateFromGraph(Context.Scene->Raw(),entity,graph);
                }
                else Context.Scene->Raw().emplace<GS::Vertices>(entity);
                auto& p=Props(d);p.Resize(points.size());p.GetOrAdd<glm::vec3>("samples").Vector()=points;
                p.GetOrAdd<float>("keep").Vector().assign(points.size(),42);
                auto normals=p.GetOrAdd<glm::vec3>("directions");
                for(std::size_t i=0;i<points.size();++i)normals[i]={.2f*points[i].x,-.1f*points[i].y,1};
                p.GetOrAdd<float>("weights").Vector().assign(points.size(),77);
                const bool half=d==unsigned(Domain::MeshHalfedge)||d==unsigned(Domain::GraphHalfedge);
                if(half)
                {
                    auto& edges=Context.Scene->Raw().get<GS::Edges>(entity).Properties;
                    edges.Resize(points.size()/2);edges.GetOrAdd<bool>("e:deleted")[2]=true;
                }
                else p.GetOrAdd<bool>(d==unsigned(Domain::MeshFace)?"f:deleted":
                    (d==unsigned(Domain::MeshEdge)||d==unsigned(Domain::GraphEdge))?"e:deleted":"v:deleted")[4]=true;
                p.Get<glm::vec3>("samples")[4]={std::numeric_limits<float>::quiet_NaN(),0,0};
            }

            Context.MethodResultSinks.DensityWeight=[this](auto result){Results.push_back(std::move(result));};
        }
        void Frame(double,double) override
        {
            if(std::chrono::steady_clock::now()-Started>std::chrono::seconds(95))
            {ADD_FAILURE()<<"Density weight timeout: phase="<<Phase<<" results="<<Results.size();TimedOut=true;Kernel().RequestExit();return;}
            if(!Kernel().GetDevice().IsOperational())
            {if(++ColdFrames>4){TimedOut=true;Kernel().RequestExit();}return;}
            const bool all=Phase<7,edge=Phase>=20;
            if(Submitted)
            {
                if(Phase==11 && !Cancelled && SupportReadyForCancel)
                {EXPECT_TRUE(Kernel().Jobs().Cancel(SupportToken));Cancelled=true;}
                if(Phase==11)(void)Kernel().Jobs().ReapCompleted();
                if(Results.size()<(all?8:1))return;
                if(Phase==13)
                    for(auto token:SubmissionTokens)
                        if(Kernel().Jobs().GetState(token)!=Runtime::JobState::Invalid && !Kernel().Jobs().IsComplete(token))return;
                if(all || edge)
                {
                    PhaseMs.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-PhaseStarted).count());
                    for(const auto& result:Results)
                    {
                        if(!result.Succeeded()){ADD_FAILURE()<<result.Message;Kernel().RequestExit();return;}
                        EXPECT_EQ(result.ActualBackend,"vulkan_lbvh");EXPECT_GT(result.GpuQueryBatches,0);
                        if(all && Phase>0)EXPECT_TRUE(result.IndexReused);
                        const auto slot=all?unsigned(result.Weights.Domain)-1:0;
                        EXPECT_EQ(result.WrittenCount,ReferenceResults[slot].WrittenCount);
                        EXPECT_EQ(result.Diagnostics.NeighborContributionCount,ReferenceResults[slot].Diagnostics.NeighborContributionCount);
                    }
                    for(unsigned d=all?1:8;d<=8;++d)
                    {
                        const auto& p=std::as_const(Props(d));const auto values=p.Get<float>("weights");
                        const auto& reference=ReferenceColumns[all?d-1:0];ASSERT_EQ(values.Size(),reference.size());
                        for(std::size_t i=0;i<values.Size();++i)
                        {
                            const double error=std::abs(double(values[i])-reference[i]);
                            MaxError=std::isfinite(error)?std::max(MaxError,error):std::numeric_limits<double>::infinity();
                        }
                        if(all)
                        {
                            EXPECT_EQ(values[4],77);
                            if(d==unsigned(Domain::MeshHalfedge)||d==unsigned(Domain::GraphHalfedge))EXPECT_EQ(values[5],77);
                            EXPECT_EQ(p.Get<float>("keep")[0],42);
                        }
                        if(Phase==20 || Phase==21)EXPECT_GT(values[0],1.0003f);
                        if(Phase==22){EXPECT_EQ(values[0],1);EXPECT_EQ(Results[0].Diagnostics.NeighborContributionCount,0);}
                    }
                    EXPECT_LE(MaxError,1e-5);EXPECT_EQ(History.UndoCount(),all?8:1);
                    for(unsigned i=0;i<(all?8:1);++i)EXPECT_TRUE(History.Undo().Succeeded());
                    for(unsigned d=all?1:8;d<=8;++d)EXPECT_EQ(std::as_const(Props(d)).Get<float>("weights")[0],77);
                    for(unsigned i=0;i<(all?8:1);++i)EXPECT_TRUE(History.Redo().Succeeded());
                    if(Phase==6 || Phase==22){Done=true;Kernel().RequestExit();return;}
                }
                else
                {
                    EXPECT_FALSE(Results.back().Succeeded())<<Results.back().Message;
                    EXPECT_EQ(std::as_const(Props(8)).Get<float>("weights")[0],77);EXPECT_EQ(History.UndoCount(),0);
                    if(Phase==11){EXPECT_TRUE(Cancelled);EXPECT_TRUE(SupportReadyForCancel);EXPECT_EQ(Results.back().Status,Runtime::EditorCommandStatus::StaleEntity);}
                    if(Phase==12)
                    {
                        EXPECT_NE(Results.back().Message.find("overflow"),std::string::npos);
                        EXPECT_GT(Results.back().MaximumNeighbors,1024);EXPECT_GT(Results.back().GpuQueryBatches,0);
                    }
                    if(Phase==13)
                    {
                        EXPECT_EQ(SubmissionCount,2);EXPECT_EQ(SubmissionTokens.size(),1);
                        EXPECT_NE(Results.back().Message.find("submission rejected"),std::string::npos);
                        Done=true;Kernel().RequestExit();return;
                    }
                }
                Results.clear();Submitted=false;++Phase;
            }
            History.ClearHistory();Context.JobCommands={};Context.CommandHistory=nullptr;
            if(Phase>=20)
            {
                auto& p=Props(8);const auto size=Phase==20?2:Phase==21?33:1;p.Resize(size);
                const float v=std::bit_cast<float>(0x1a01460fu);
                p.Get<glm::vec3>("samples").Vector().assign(size,glm::vec3(v));p.Get<glm::vec3>("samples")[0]={0,0,0};
                p.GetOrAdd<bool>("v:deleted").Vector().assign(size,false);p.Get<float>("weights").Vector().assign(size,77);
                if(Phase==22)p.Get<glm::vec3>("samples")[0]={0,-0.f,std::numeric_limits<float>::min()};
                if(Phase==20)
                {
                    for(unsigned axis=0;axis<3;++axis)for(float sign:{1.f,-1.f})
                    {
                        glm::vec3 subnormal(0);subnormal[axis]=sign*std::numeric_limits<float>::denorm_min();p.Get<glm::vec3>("samples")[0]=subnormal;
                        const auto ready=Runtime::PreviewEditorDensityWeightCommand(Context,Config(8));
                        EXPECT_FALSE(ready.Ready);EXPECT_NE(ready.Diagnostic.find("subnormal"),std::string::npos);
                    }
                    p.Get<glm::vec3>("samples")[0]={0,0,0};
                    auto c=Config(8);c.SupportRadius=double(Geometry::PointLBVH::CoordinateLimit);
                    EXPECT_FALSE(Runtime::PreviewEditorDensityWeightCommand(Context,c).Ready);
                    EXPECT_EQ(std::as_const(p).Get<float>("weights")[0],77);EXPECT_EQ(History.UndoCount(),0);
                }
            }
            if(Phase<7 || Phase>=20)
            {
                ReferenceColumns.clear();ReferenceResults.clear();const auto cpuStart=std::chrono::steady_clock::now();
                for(unsigned d=Phase<7?1:8;d<=8;++d)
                {
                    auto c=Config(d);c.Backend=Runtime::DensityWeightBackend::CpuKDTree;
                    const auto result=Runtime::ApplyEditorDensityWeightCommand(Context,c);
                    if(!result.Succeeded()){ADD_FAILURE()<<result.Message;Kernel().RequestExit();return;}
                    ReferenceResults.push_back(result);ReferenceColumns.push_back(std::as_const(Props(d)).Get<float>("weights").Vector());
                    Props(d).Get<float>("weights").Vector().assign(Props(d).Size(),77);
                }
                CpuMs.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-cpuStart).count());
            }
            SubmissionCount=0;SupportReadyForCancel=false;SubmissionTokens.clear();
            Context.JobCommands.Submit=[this](Runtime::JobDesc desc,Runtime::EditorJobIdentity)
            {
                ++SubmissionCount;if(Phase==13 && SubmissionCount==2)return Runtime::JobToken{};
                const bool support=desc.DebugName=="Density radius support (Vulkan)";
                if(Phase==11 && support)
                {
                    auto ready=std::move(desc.IsReadyToApply);
                    desc.IsReadyToApply=[this,ready=std::move(ready)]() mutable
                    {if(!SupportReadyForCancel)SupportReadyForCancel=ready();return false;};
                }
                const auto token=Kernel().Jobs().Submit(std::move(desc));SubmissionTokens.push_back(token);if(support)SupportToken=token;return token;
            };
            Context.CommandHistory=&History;Submitted=true;PhaseStarted=std::chrono::steady_clock::now();
            if(Phase<7)
                for(unsigned d=1;d<=8;++d)
                {const auto r=Runtime::ApplyEditorDensityWeightCommand(Context,Config(d));if(r.Status!=Runtime::EditorCommandStatus::Pending)Results.push_back(r);}
            else
            {
                auto& p=Props(8);
                if(Phase==12)
                {
                    p.Resize(1030);p.Get<glm::vec3>("samples").Vector().assign(1030,glm::vec3(0));
                    p.GetOrAdd<bool>("v:deleted").Vector().assign(1030,false);
                }
                p.Get<float>("weights").Vector().assign(p.Size(),77);
                const auto r=Runtime::ApplyEditorDensityWeightCommand(Context,Config(8));if(r.Status!=Runtime::EditorCommandStatus::Pending)Results.push_back(r);
                if(Phase==10)p.Get<glm::vec3>("samples")[0]+=.1f;
            }
        }
        void Shutdown() override {Context={};}
        Runtime::EditorGeometryProcessingContext Context{};Runtime::EditorCommandHistory History{};
        std::vector<entt::entity> Entities;std::vector<std::vector<float>> ReferenceColumns;
        std::vector<Runtime::EditorDensityWeightResult> Results,ReferenceResults;std::vector<double> PhaseMs,CpuMs;
        std::chrono::steady_clock::time_point Started{},PhaseStarted{};
        Runtime::JobToken SupportToken{};std::vector<Runtime::JobToken> SubmissionTokens;
        unsigned Phase{},ColdFrames{},SubmissionCount{};
        bool Submitted{},Done{},TimedOut{},Cancelled{},SupportReadyForCancel{};double MaxError{};
    };
}
TEST(PointLBVHGpuSmoke, DensityWeightPublishesAllKernelsAcrossDomains)
{
    if(!Extrinsic::Platform::Backends::Glfw::CanInitialize())GTEST_SKIP()<<"GLFW unavailable";
    auto config=Runtime::CreateReferenceEngineConfig();config.Window.Width=64;config.Window.Height=64;
    config.Render.EnableValidation=true;config.Render.EnableVSync=false;config.ReferenceScene.Enabled=false;
    auto app=std::make_unique<DensityWeightApp>();auto* run=app.get();
    Intrinsic::Tests::RuntimeTestKernel engine(config,std::move(app));engine.EmplaceModule<Runtime::SpatialIndexCache>();
    engine.Initialize();Shutdown shutdown{engine};engine.Run();ASSERT_TRUE(engine.GetDevice().IsOperational());
    ASSERT_FALSE(run->TimedOut)<<"phase="<<run->Phase;ASSERT_TRUE(run->Done);ASSERT_EQ(run->PhaseMs.size(),7);
    EXPECT_LE(run->MaxError,1e-5);
    if(const auto* output=std::getenv("INTRINSIC_DENSITY_WEIGHT_BENCHMARK_OUTPUT"))
    {
        nlohmann::json json{{"benchmark_id","geometry.point_lbvh.density_weight_runtime_smoke"},{"method","geometry.point_lbvh"},
            {"backend","gpu_vulkan_compute"},{"dataset","builtin.density_weight_samples.eight_domains.seed251"},{"commit","local-dev"},
            {"metrics",{{"runtime_ms",run->PhaseMs[1]},{"quality_error_linf",run->MaxError}}},
            {"diagnostics",{{"runner","IntrinsicPointLBVHGpuTests"},{"mode","smoke"},{"cpu_reference_total_ms",run->CpuMs[1]},
                {"vulkan_cold_total_ms",run->PhaseMs[0]},{"vulkan_warm_total_ms",run->PhaseMs[1]},
                {"warmup_iterations",1},{"measured_iterations",1},{"cpu_kernel_reduction",true}}},
            {"status",::testing::Test::HasFailure()?"failed":"passed"}};
        std::ofstream stream(output);ASSERT_TRUE(stream.good());stream<<json.dump(2)<<'\n';ASSERT_TRUE(stream.good());
    }
}
TEST(PointLBVHGpuSmoke, DensityWeightRejectsStaleCancellationOverflowAndSubmission)
{
    if(!Extrinsic::Platform::Backends::Glfw::CanInitialize())GTEST_SKIP()<<"GLFW unavailable";
    auto config=Runtime::CreateReferenceEngineConfig();config.Window.Width=64;config.Window.Height=64;
    config.Render.EnableValidation=true;config.Render.EnableVSync=false;config.ReferenceScene.Enabled=false;
    auto app=std::make_unique<DensityWeightApp>();auto* run=app.get();run->Phase=10;
    Intrinsic::Tests::RuntimeTestKernel engine(config,std::move(app));engine.EmplaceModule<Runtime::SpatialIndexCache>();
    engine.Initialize();Shutdown shutdown{engine};engine.Run();ASSERT_TRUE(engine.GetDevice().IsOperational());
    ASSERT_FALSE(run->TimedOut)<<"phase="<<run->Phase;ASSERT_TRUE(run->Done);EXPECT_TRUE(run->Cancelled);
}
TEST(PointLBVHGpuSmoke, DensityWeightTinySupportAndOneSample)
{
    if(!Extrinsic::Platform::Backends::Glfw::CanInitialize())GTEST_SKIP()<<"GLFW unavailable";
    auto config=Runtime::CreateReferenceEngineConfig();config.Window.Width=64;config.Window.Height=64;
    config.Render.EnableValidation=true;config.Render.EnableVSync=false;config.ReferenceScene.Enabled=false;
    auto app=std::make_unique<DensityWeightApp>();auto* run=app.get();run->Phase=20;
    Intrinsic::Tests::RuntimeTestKernel engine(config,std::move(app));engine.EmplaceModule<Runtime::SpatialIndexCache>();
    engine.Initialize();Shutdown shutdown{engine};engine.Run();ASSERT_TRUE(engine.GetDevice().IsOperational());
    ASSERT_FALSE(run->TimedOut)<<"phase="<<run->Phase;ASSERT_TRUE(run->Done);EXPECT_LE(run->MaxError,1e-5);
}
