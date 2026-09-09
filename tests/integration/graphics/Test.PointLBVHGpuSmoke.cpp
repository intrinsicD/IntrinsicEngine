#include "RuntimeTestModule.hpp"
#include <algorithm>
#include <entt/entity/entity.hpp>
#include <glm/glm.hpp>
#include <gtest/gtest.h>
#include <limits>
#include <memory>
#include <iostream>
#include <random>
#include <chrono>
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
            if (!Kernel().GetDevice().IsOperational()) return;
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
                EXPECT_EQ(Batch->Counts,(std::vector<std::uint32_t>{3,4}));
                const std::vector<std::uint32_t> expected=Round==0?
                    std::vector<std::uint32_t>{2,3,4,LB::InvalidIndex,0,2,3,4}:
                    std::vector<std::uint32_t>{0,3,4,LB::InvalidIndex,0,2,3,4};
                EXPECT_EQ(Batch->Neighbors.size(),expected.size());
                for (std::size_t i=0;i<expected.size();++i) EXPECT_EQ(Batch->Neighbors[i].Index,expected[i]);
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
            Batch=Cache->QueueGpuKNearest(Handle,queries,4,exclusions,Batch);
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
        unsigned Round{}; bool TimedOut{},Done{};
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
