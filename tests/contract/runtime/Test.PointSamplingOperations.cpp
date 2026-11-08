// RUNTIME-274: standalone point sampling through the editor command.
#include <algorithm>
#include <cstring>
#include <chrono>
#include <functional>
#include <memory>
#include <span>
#include <mutex>
#include <thread>
#include <cstdlib>
#include <optional>
#include <cmath>
#include <random>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>
#include <gtest/gtest.h>
#include "SandboxEditorJobHarness.hpp"
#include "MockRHI.hpp"
import Extrinsic.Runtime.PointSamplingGpu;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.GpuPropertyBinding;
import Extrinsic.Runtime.PointSamplingOperations;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.VertexChannelBindings;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Geometry.HalfedgeMesh;
import Geometry.PointSampling;
import Extrinsic.Runtime.RegistrationOperations;
import Extrinsic.Runtime.StepRuns;
import Extrinsic.Runtime.WorldRegistry;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Component.Hierarchy;
import Extrinsic.Runtime.StepRuns;
namespace R = Extrinsic::Runtime;
namespace GS = Extrinsic::ECS::Components::GeometrySources;
namespace T = Extrinsic::ECS::Components::Transform;
using D = R::GeometryElementDomain;
namespace
{
    struct Scene
    {
        R::WorldRegistry Worlds;
        R::WorldHandle World{Worlds.CreateWorld("sampling")};
        Extrinsic::ECS::Scene::Registry& Registry{*Worlds.Get(World)};
        R::EditorCommandHistory History;
        R::EditorProcessingContext Context{.Scene = &Registry, .World = World, .CommandHistory = &History};
        [[nodiscard]] R::EditorProcessingCommands Commands() { return R::BindEditorProcessingCommands(Context); }
    };

    entt::entity MakeCloud(Scene& s, std::size_t count)
    {
        const auto entity = s.Registry.Create();
        s.Registry.Raw().emplace<T::Component>(entity);
        auto& vertices = s.Registry.Raw().emplace<GS::Vertices>(entity).Properties;
        vertices.Resize(count);
        std::mt19937 random(3u);
        std::uniform_real_distribution<float> uniform(-1.0f, 1.0f);
        auto positions = vertices.GetOrAdd<glm::vec3>("v:position");
        for (auto& p : positions.Vector()) p = {uniform(random), uniform(random), 0.5f * uniform(random)};
        return entity;
    }

    std::uint32_t Id(entt::entity entity) { return R::SelectionController::ToStableEntityId(entity); }
}

TEST(PointSamplingOperations, PublishesRankAndSelectionPropertiesUndoably)
{
    Scene s;
    const auto cloud = MakeCloud(s, 500);
    R::PointSamplingOperationConfig config{.SourceStableEntityId = Id(cloud), .Count = 64};
    ASSERT_TRUE(R::PreviewEditorPointSamplingCommand(s.Commands(), config).Enabled);
    const auto result = R::ApplyEditorPointSamplingCommand(s.Commands(), config);
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_EQ(result.SampleCount, 64u);
    EXPECT_EQ(result.InputCount, 500u);
    EXPECT_GT(result.DistancePairs, 0u) << "farthest point by default";
    const auto& vertices = std::as_const(s.Registry.Raw().get<GS::Vertices>(cloud).Properties);
    const auto rank = vertices.Get<float>("v:sample_rank");
    const auto selected = vertices.Get<bool>("v:sample_selected");
    ASSERT_TRUE(rank && selected);
    EXPECT_EQ(std::count(selected.Vector().begin(), selected.Vector().end(), true), 64);
    EXPECT_EQ(std::count_if(rank.Vector().begin(), rank.Vector().end(), [](float r) { return r >= 0.0f; }), 64);
    ASSERT_TRUE(s.History.Undo().Succeeded());
    EXPECT_FALSE(vertices.Exists("v:sample_rank"));
    EXPECT_FALSE(vertices.Exists("v:sample_selected"));
}

TEST(PointSamplingOperations, RejectsWrongTypeOutputsInPreview)
{
    for (const bool rankConflict : {true, false})
    {
        Scene s;
        const auto cloud = MakeCloud(s, 20);
        auto& vertices = s.Registry.Raw().get<GS::Vertices>(cloud).Properties;
        (void)vertices.GetOrAdd<glm::vec3>("v:blocked", glm::vec3{7.0f});
        R::PointSamplingOperationConfig config{.SourceStableEntityId = Id(cloud), .Count = 8};
        (rankConflict ? config.RankName : config.SelectedName) = "v:blocked";
        EXPECT_FALSE(R::PreviewEditorPointSamplingCommand(s.Commands(), config).Enabled);
        EXPECT_FALSE(s.History.CanUndo());
        EXPECT_FALSE(vertices.Exists("v:sample_rank"));
        EXPECT_FALSE(vertices.Exists("v:sample_selected"));
    }
}

TEST(PointSamplingOperations, RejectsWrongTypeOutputsBeforeMutation)
{
    for (const bool rankConflict : {true, false})
    {
        Scene s;
        const auto cloud = MakeCloud(s, 20);
        auto& vertices = s.Registry.Raw().get<GS::Vertices>(cloud).Properties;
        (void)vertices.GetOrAdd<glm::vec3>("v:blocked", glm::vec3{7.0f});
        (void)vertices.GetOrAdd<float>("v:sample_rank", 42.0f);
        (void)vertices.GetOrAdd<bool>("v:sample_selected", true);
        R::PointSamplingOperationConfig config{.SourceStableEntityId = Id(cloud), .Count = 8};
        (rankConflict ? config.RankName : config.SelectedName) = "v:blocked";
        EXPECT_EXIT({
            const auto result = R::ApplyEditorPointSamplingCommand(s.Commands(), config);
            const auto& read = std::as_const(vertices);
            const bool unchanged = !result.Succeeded() && !s.History.CanUndo() &&
                read.Get<glm::vec3>("v:blocked").Vector() == std::vector<glm::vec3>(20, glm::vec3{7.0f}) &&
                read.Get<float>("v:sample_rank").Vector() == std::vector<float>(20, 42.0f) &&
                read.Get<bool>("v:sample_selected").Vector() == std::vector<bool>(20, true);
            std::_Exit(unchanged ? 0 : 1);
        }, ::testing::ExitedWithCode(0), "");
    }
}

TEST(PointSamplingOperations, SuccessiveEditsReplayAndRejectForeignOutputChanges)
{
    Scene s;
    const auto cloud = MakeCloud(s, 20);
    auto& properties = s.Registry.Raw().get<GS::Vertices>(cloud).Properties;
    const auto rank = [&] { return std::as_const(properties).Get<float>("v:sample_rank").Vector(); };
    const auto selected = [&] { return std::as_const(properties).Get<bool>("v:sample_selected").Vector(); };
    R::PointSamplingOperationConfig config{.SourceStableEntityId = Id(cloud), .Count = 8};
    ASSERT_TRUE(R::ApplyEditorPointSamplingCommand(s.Commands(), config).Succeeded());
    const auto firstRank = rank();
    const auto firstSelected = selected();
    config.Count = 5;
    ASSERT_TRUE(R::ApplyEditorPointSamplingCommand(s.Commands(), config).Succeeded());
    const auto secondRank = rank();
    const auto secondSelected = selected();
    ASSERT_TRUE(s.History.Undo().Succeeded());
    EXPECT_EQ(rank(), firstRank);
    EXPECT_EQ(selected(), firstSelected);
    ASSERT_TRUE(s.History.Undo().Succeeded());
    EXPECT_FALSE(properties.Exists("v:sample_rank"));
    EXPECT_FALSE(properties.Exists("v:sample_selected"));
    ASSERT_TRUE(s.History.Redo().Succeeded());
    EXPECT_EQ(rank(), firstRank);
    EXPECT_EQ(selected(), firstSelected);
    ASSERT_TRUE(s.History.Redo().Succeeded());
    EXPECT_EQ(rank(), secondRank);
    EXPECT_EQ(selected(), secondSelected);
    properties.Get<float>("v:sample_rank")[0] = 99.0f;
    EXPECT_EQ(s.History.Undo().Status, R::EditorCommandHistoryStatus::StaleEntity);
    EXPECT_EQ(s.History.UndoCount(), 2u);
    EXPECT_EQ(rank()[0], 99.0f);
    EXPECT_EQ(selected(), secondSelected);

    for (const bool replaceRank : {true, false})
    for (const bool redo : {false, true})
    {
        SCOPED_TRACE(replaceRank);
        SCOPED_TRACE(redo);
        Scene foreign;
        const auto entity = MakeCloud(foreign, 20);
        auto& props = foreign.Registry.Raw().get<GS::Vertices>(entity).Properties;
        (void)props.GetOrAdd<float>("v:sample_rank", 42.0f);
        (void)props.GetOrAdd<bool>("v:sample_selected", true);
        unsigned invalidations = 0;
        foreign.Context.InvalidateWorkspaceSnapshotCache = [&] { ++invalidations; };
        R::PointSamplingOperationConfig request{.SourceStableEntityId = Id(entity), .Count = 5};
        ASSERT_TRUE(R::ApplyEditorPointSamplingCommand(foreign.Commands(), request).Succeeded());
        if (redo) ASSERT_TRUE(foreign.History.Undo().Succeeded());
        const auto expectedRank = std::as_const(props).Get<float>("v:sample_rank").Vector();
        const auto expectedSelected = std::as_const(props).Get<bool>("v:sample_selected").Vector();
        const auto name = replaceRank ? "v:sample_rank" : "v:sample_selected";
        const auto previousId = props.Registry().Find(name);
        if (replaceRank)
        {
            auto old = props.Get<float>(name);
            props.Remove(old);
            props.GetOrAdd<float>(name).Vector() = expectedRank;
        }
        else
        {
            auto old = props.Get<bool>(name);
            props.Remove(old);
            props.GetOrAdd<bool>(name).Vector() = expectedSelected;
        }
        ASSERT_NE(props.Registry().Find(name), previousId);
        const auto undoCount = foreign.History.UndoCount();
        const auto redoCount = foreign.History.RedoCount();
        invalidations = 0;
        EXPECT_EQ((redo ? foreign.History.Redo() : foreign.History.Undo()).Status,
                  R::EditorCommandHistoryStatus::StaleEntity);
        EXPECT_EQ(foreign.History.UndoCount(), undoCount);
        EXPECT_EQ(foreign.History.RedoCount(), redoCount);
        EXPECT_EQ(std::as_const(props).Get<float>("v:sample_rank").Vector(), expectedRank);
        EXPECT_EQ(std::as_const(props).Get<bool>("v:sample_selected").Vector(), expectedSelected);
        EXPECT_EQ(invalidations, 0u);
    }
}

TEST(PointSamplingOperations, CreatesAPointCloudWithEveryMethod)
{
    Scene s;
    const auto cloud = MakeCloud(s, 400);
    for (const auto method : {R::PointSamplingMethod::Random, R::PointSamplingMethod::FarthestPoint,
                              R::PointSamplingMethod::ProgressivePoisson, R::PointSamplingMethod::CoupledSieve,
                              R::PointSamplingMethod::FlatGreedy, R::PointSamplingMethod::SampleElimination,
                              R::PointSamplingMethod::LazyGreedy, R::PointSamplingMethod::Tournament})
    {
        R::PointSamplingOperationConfig config{.SourceStableEntityId = Id(cloud), .Count = 50,
                                               .Output = R::PointSamplingOutput::PointCloud};
        config.Sampling.Method = method;
        const auto result = R::ApplyEditorPointSamplingCommand(s.Commands(), config);
        ASSERT_TRUE(result.Succeeded()) << result.Message << " (" << result.Method << ")";
        EXPECT_GT(result.OutputEntityId, 0u);
        EXPECT_LE(result.SampleCount, 50u);
        EXPECT_GT(result.SampleCount, 0u);
        ASSERT_TRUE(s.History.Undo().Succeeded()) << result.Method;
    }
}

TEST(PointSamplingOperations, WeightsAndPriorityScoresComeFromAFloatProperty)
{
    Scene s;
    const auto cloud = MakeCloud(s, 300);
    auto& vertices = s.Registry.Raw().get<GS::Vertices>(cloud).Properties;
    auto weights = vertices.GetOrAdd<float>("v:importance");
    for (std::size_t i = 0; i < weights.Vector().size(); ++i) weights.Vector()[i] = 1.0f + float(i % 7);
    R::PointSamplingOperationConfig config{.SourceStableEntityId = Id(cloud), .Count = 40, .WeightsName = "v:importance"};
    EXPECT_TRUE(R::ApplyEditorPointSamplingCommand(s.Commands(), config).Succeeded());
    config.Sampling.Method = R::PointSamplingMethod::ProgressivePoisson;
    config.Sampling.PoissonSelection = R::PointSamplingPoissonSelection::FeaturePriority;
    EXPECT_TRUE(R::ApplyEditorPointSamplingCommand(s.Commands(), config).Succeeded());
    config.WeightsName = "v:missing";
    EXPECT_FALSE(R::PreviewEditorPointSamplingCommand(s.Commands(), config).Enabled);
    config.WeightsName.clear();
    EXPECT_FALSE(R::PreviewEditorPointSamplingCommand(s.Commands(), config).Enabled) << "priority needs scores";
}

TEST(PointSamplingOperations, ConfigRoundTripsAndRejectsUnusableSettings)
{
    R::PointSamplingOperationConfig config{.Count = 0, .Output = R::PointSamplingOutput::PointCloud};
    config.Sampling.Method = R::PointSamplingMethod::FlatGreedy;
    config.Sampling.Beta = 1.4;
    const auto decoded = R::DecodePointSamplingOperationConfig(R::SerializePointSamplingOperationConfig(config));
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded->Sampling.Method, R::PointSamplingMethod::FlatGreedy);
    EXPECT_EQ(decoded->Sampling.Beta, 1.4);
    EXPECT_EQ(decoded->Output, R::PointSamplingOutput::PointCloud);
    EXPECT_EQ(decoded->Count, 0u);
    const auto invalid = [](const R::PointSamplingOperationConfig& c) {
        return !R::ValidatePointSamplingOperationConfigSection(R::SerializePointSamplingOperationConfig(c), {}, "test").Usable();
    };
    EXPECT_TRUE(invalid({.RankName = "v:sample_selected"}));
    R::PointSamplingOperationConfig exact;
    exact.Sampling = {.Method = R::PointSamplingMethod::CoupledSieve, .Eta = 1.0, .CandidateCap = 3};
    EXPECT_TRUE(invalid(exact));
    Scene s;
    const auto cloud = MakeCloud(s, 20);
    const auto parent = s.Registry.Create();
    s.Registry.Raw().emplace<Extrinsic::ECS::Components::Hierarchy::Component>(cloud).Parent = parent;
    // Topology names are refused once the domain is resolved (here: point-cloud points).
    const auto plain = MakeCloud(s, 20);
    EXPECT_FALSE(R::PreviewEditorPointSamplingCommand(s.Commands(), {.SourceStableEntityId = Id(plain),
                                                                     .RankName = "v:deleted"}).Enabled);
    EXPECT_FALSE(R::PreviewEditorPointSamplingCommand(s.Commands(), {.SourceStableEntityId = Id(cloud)}).Enabled);
}

TEST(PointSamplingOperations, VulkanBackendWithoutADeviceRunsOnTheCpuAndSaysWhy)
{
    // RUNTIME-290: the requested backend, the one that ran and why, for every fallback path.
    Scene s;
    const auto cloud = MakeCloud(s, 300);
    R::PointSamplingOperationConfig config{.SourceStableEntityId = Id(cloud), .Count = 40,
                                           .Backend = R::PointSamplingBackend::Vulkan};
    const auto decoded = R::DecodePointSamplingOperationConfig(R::SerializePointSamplingOperationConfig(config));
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded->Backend, R::PointSamplingBackend::Vulkan);

    const auto cpu = R::ApplyEditorPointSamplingCommand(s.Commands(), {.SourceStableEntityId = Id(cloud), .Count = 40});
    ASSERT_TRUE(cpu.Succeeded()) << cpu.Message;
    EXPECT_EQ(cpu.RequestedBackend, "cpu_reference");
    EXPECT_TRUE(cpu.BackendDiagnostic.empty());
    const auto rank = [&] { return std::as_const(s.Registry.Raw().get<GS::Vertices>(cloud).Properties).Get<float>("v:sample_rank").Vector(); };
    const auto cpuRank = rank();

    int delivered = 0;
    const auto noLane = R::ApplyEditorPointSamplingCommand(s.Commands(), config, [&](R::EditorPointSamplingResult) { ++delivered; });
    ASSERT_TRUE(noLane.Succeeded()) << noLane.Message;
    EXPECT_EQ(delivered, 1) << "immediate results reach the sink too";
    EXPECT_EQ(noLane.RequestedBackend, "gpu_vulkan_compute");
    EXPECT_EQ(noLane.Backend, "cpu_reference");
    EXPECT_NE(noLane.BackendDiagnostic.find("job lane"), std::string::npos) << noLane.BackendDiagnostic;
    EXPECT_EQ(rank(), cpuRank) << "the fallback is the CPU order";

    Extrinsic::Tests::EditorJobHarness jobs;
    jobs.Attach(s.Context);
    const auto noDevice = R::ApplyEditorPointSamplingCommand(s.Commands(), config);
    EXPECT_EQ(noDevice.Backend, "cpu_reference");
    EXPECT_NE(noDevice.BackendDiagnostic.find("spatial compute service"), std::string::npos) << noDevice.BackendDiagnostic;

    namespace PS = Geometry::PointSampling;
    EXPECT_NE(R::PointSamplingGpuUnsupportedReason({.Method = PS::Method::Random}, 300u, 40u, nullptr).find("no Vulkan kernel"),
              std::string::npos);
    EXPECT_NE(R::PointSamplingGpuUnsupportedReason({}, 300u, 40u, nullptr).find("No operational Vulkan device"),
              std::string::npos);
}

// ARCH-023: the CPU counterpart is a worker step run of the same lifecycle: one exact farthest-point sample per
// step (the reference sieve, extended a sample at a time), restart from the captured input, Accept of the prefix in
// one undo entry, and the batch command's Run publishing the whole order at its end.
TEST(PointSamplingOperations, CpuFarthestPointStepsOnAWorkerAndAcceptsItsPrefix)
{
    using namespace std::chrono_literals;
    Scene s;
    const auto cloud = MakeCloud(s, 200);
    Extrinsic::Tests::EditorJobHarness jobs;
    jobs.Attach(s.Context);
    const R::PointSamplingOperationConfig config{.SourceStableEntityId = Id(cloud), .Count = 30};
    unsigned calls = 0;
    R::EditorPointSamplingResult last;
    const auto sink = [&](R::EditorPointSamplingResult result)
    {
        ++calls;
        last = std::move(result);
    };
    const auto started = R::StartEditorPointSampling(s.Commands(), config, sink);
    ASSERT_EQ(started.Status, R::EditorCommandStatus::Pending) << started.Message;
    ASSERT_NE(started.StepRun, 0u);
    auto& runs = jobs.StepRuns();
    const auto observe = [&]
    {
        return *runs.Observe(started.StepRun);
    };
    const auto pump = [&](const auto& done)
    {
        for (int i = 0; i < 4000 && !done(); ++i)
        {
            (void)jobs.Jobs().DrainCompletions(jobs.Events());
            std::this_thread::sleep_for(200us);
        }
        return done();
    };
    const auto settled = [&]
    {
        const auto o = observe();
        return o.Phase == R::StepRunPhase::Paused && !o.SegmentInFlight;
    };
    std::string why;
    EXPECT_EQ(observe().Phase, R::StepRunPhase::Ready) << "an interactive start computes nothing";
    ASSERT_TRUE(runs.Control(started.StepRun, R::StepRunAction::Step, 5u, why)) << why;
    ASSERT_TRUE(pump(settled));
    EXPECT_EQ(observe().Steps, 5u) << "five steps, five samples";
    ASSERT_TRUE(runs.Control(started.StepRun, R::StepRunAction::Restart, 1u, why)) << why;
    ASSERT_TRUE(pump(
        [&]
        {
            return observe().Generation == 1u;
        }));
    EXPECT_EQ(observe().Phase, R::StepRunPhase::Ready);
    EXPECT_EQ(observe().Steps, 0u) << "a restart begins from the captured input";
    ASSERT_TRUE(runs.Control(started.StepRun, R::StepRunAction::Step, 7u, why)) << why;
    ASSERT_TRUE(pump(settled));
    EXPECT_EQ(observe().Steps, 7u);
    ASSERT_TRUE(runs.Control(started.StepRun, R::StepRunAction::Accept, 1u, why)) << why;
    ASSERT_TRUE(pump(
        [&]
        {
            return calls == 1u;
        }));
    ASSERT_EQ(last.Status, R::EditorCommandStatus::Applied) << last.Message;
    EXPECT_EQ(last.Steps, 7u);
    EXPECT_EQ(last.StepRun, started.StepRun);
    const auto points =
        std::as_const(s.Registry.Raw().get<GS::Vertices>(cloud).Properties).Get<glm::vec3>("v:position");
    const auto reference = Geometry::PointSampling::Order(std::span<const glm::vec3>(points.Vector()), {}, 30u);
    const auto rank = [&]
    {
        return std::as_const(s.Registry.Raw().get<GS::Vertices>(cloud).Properties).Get<float>("v:sample_rank").Vector();
    };
    for (std::size_t i = 0; i < reference.Order.size(); ++i)
        EXPECT_EQ(rank()[reference.Order[i]], i < 7u ? float(i) : -1.0f) << "sample " << i;
    ASSERT_TRUE(s.History.Undo().Succeeded());
    EXPECT_FALSE(std::as_const(s.Registry.Raw().get<GS::Vertices>(cloud).Properties).Exists("v:sample_rank"));
    EXPECT_FALSE(s.History.Undo().Succeeded()) << "the Accept was one undo entry";
    // The batch command: Run at once, accepted at its end: the whole reference order.
    const auto batch = R::ApplyEditorPointSamplingCommand(s.Commands(), config, sink);
    ASSERT_EQ(batch.Status, R::EditorCommandStatus::Pending) << batch.Message;
    ASSERT_TRUE(pump(
        [&]
        {
            return calls == 2u;
        }));
    ASSERT_EQ(last.Status, R::EditorCommandStatus::Applied) << last.Message;
    EXPECT_EQ(last.Steps, 30u);
    for (std::size_t i = 0; i < reference.Order.size(); ++i)
        EXPECT_EQ(rank()[reference.Order[i]], float(i));
}

namespace
{
    class ResidentPointSampling : public ::testing::Test
    {
    protected:
        Scene S;
        Extrinsic::Tests::MockDevice Device;
        Extrinsic::Tests::EditorJobHarness Jobs;
        R::SpatialIndexCache Cache;
        R::CommandBus Commands;
        R::KernelEventBus Events;
        R::ServiceRegistry Services;
        entt::entity Entity{};
        R::PointSamplingOperationConfig Config;
        unsigned Calls{};
        R::EditorPointSamplingResult Last;
        // ARCH-023: the accepted compute-lane submissions, oldest first, completed only by Complete (a worker step's
        // pulled page waits for it through WaitCompute and takes its bytes; an owner page's sink is delivered). The
        // worker submits and waits on its own thread, so the model is guarded by Gpu.
        struct Page
        {
            Extrinsic::RHI::ReadbackToken Token{};
            Extrinsic::RHI::ReadbackSink Sink{};
        };
        std::mutex Gpu;
        std::vector<Page> InFlight{};
        std::vector<std::pair<std::uint64_t, std::vector<std::byte>>> Pulled{}; // completed pulled pages' bytes
        std::uint64_t Submissions{0u};
        bool Stopping{false}; // teardown: every page completes (a waiting worker step returns)
        Extrinsic::RHI::ReadbackToken Track(Extrinsic::RHI::ReadbackSink sink)
        {
            std::scoped_lock lock{Gpu};
            const Extrinsic::RHI::ReadbackToken token{(1ull << 63u) | ++Submissions};
            InFlight.push_back({token, std::move(sink)});
            return token;
        }
        [[nodiscard]] bool HasInFlight()
        {
            std::scoped_lock lock{Gpu};
            return !InFlight.empty();
        }
        void SetUp() override
        {
            Entity = MakeCloud(S, 12);
            Config = {.SourceStableEntityId = Id(Entity), .Count = 4, .Backend = R::PointSamplingBackend::Vulkan};
            Device.ShaderFloat64 = true;
            Device.TransferQueue.AcceptBufferUploads = true;
            // ARCH-023: the compute lane accepts and keeps work in flight unless a test installs its own.
            Device.ComputeReadback = [this](auto record, std::uint64_t, Extrinsic::RHI::ReadbackSink sink)
            {
                if (!record(Device.AsyncComputeContext).IsValid())
                    return Extrinsic::RHI::ReadbackToken{};
                return Track(std::move(sink));
            };
            Device.TransferQueue.ComputeWait = [this](Extrinsic::RHI::ReadbackToken token, std::uint64_t)
            {
                {
                    std::scoped_lock lock{Gpu};
                    if (Stopping ||
                        std::ranges::any_of(Pulled,
                            [&](const auto& page)
                            {
                                return page.first == token.Value;
                            }))
                        return Extrinsic::RHI::ComputeWaitStatus::Complete;
                }
                std::this_thread::sleep_for(std::chrono::microseconds(100));
                return Extrinsic::RHI::ComputeWaitStatus::Pending;
            };
            Device.TransferQueue.TakeReadback =
                [this](Extrinsic::RHI::ReadbackToken token, std::span<std::byte> destination)
            {
                std::scoped_lock lock{Gpu};
                const auto page = std::ranges::find_if(Pulled,
                    [&](const auto& p)
                    {
                        return p.first == token.Value;
                    });
                std::ranges::fill(destination, std::byte{0});
                if (page != Pulled.end())
                {
                    if (page->second.size() != destination.size())
                        return false;
                    std::ranges::copy(page->second, destination.begin());
                }
                else if (!Stopping)
                    return false;
                return true;
            };
            S.Context.Device = &Device;
            S.Context.SpatialIndices = &Cache;
            // ARCH-023: the real job lane and the session's step runs (the Vulkan run is a step run).
            Jobs.Attach(S.Context);
            Services.BeginRegistration();
            ASSERT_TRUE(Services.Provide<Extrinsic::RHI::IDevice>(Device, "test").has_value());
            R::EngineSetup setup{Commands, Events, Jobs.Jobs(), S.Worlds, Services, [](R::FramePhase, R::RuntimeFrameHook) {}};
            ASSERT_TRUE(Cache.OnRegister(setup).has_value());
        }
        void TearDown() override
        {
            {
                std::scoped_lock lock{Gpu};
                Stopping = true;
            }
            Jobs.Jobs().CancelAndDrain();
            R::RuntimeModuleShutdownContext shutdown{Commands, Events, Jobs.Jobs(), S.Worlds, Services};
            Cache.OnShutdown(shutdown);
        }
        auto Start()
        {
            Started = R::ApplyEditorPointSamplingCommand(S.Commands(),
                Config,
                [this](auto result)
                {
                    ++Calls;
                    Last = std::move(result);
                });
            return Started;
        }
        auto& Rows() { return S.Registry.Raw().get<GS::Vertices>(Entity).Properties; }
        // The job lane's frames (the Run job's poll, the run's pump) until `done`; false after a bounded number.
        bool Pump(const std::function<bool()>& done)
        {
            for (int i = 0; i < 400 && !done(); ++i)
            {
                (void)Jobs.Jobs().DrainCompletions(Jobs.Events());
                std::this_thread::sleep_for(std::chrono::microseconds(200));
            }
            return done();
        }
        // Completes the oldest accepted compute submission with `bytes`: a pulled page's are taken by its worker.
        void Complete(const std::vector<std::byte>& bytes = {})
        {
            Page page;
            {
                std::scoped_lock lock{Gpu};
                ASSERT_FALSE(InFlight.empty());
                page = std::move(InFlight.front());
                InFlight.erase(InFlight.begin());
                if (page.Sink.Pulled)
                {
                    Pulled.emplace_back(page.Token.Value, bytes);
                    return;
                }
            }
            page.Sink.Deliver(bytes);
        }
        bool Control(const R::StepRunAction action, const std::uint32_t count = 1u)
        {
            std::string refusal;
            const bool accepted = Jobs.StepRuns().Control(Started.StepRun, action, count, refusal);
            EXPECT_TRUE(accepted) << refusal;
            return accepted;
        }
        R::StepRunObservation Observe()
        {
            return *Jobs.StepRuns().Observe(Started.StepRun);
        }
        // The reference order of `count` samples as the device result buffer holds it (clearances, then indices).
        std::vector<std::byte> DeviceBytes(const Geometry::PointSampling::Result& reference) const
        {
            std::vector<std::byte> bytes(Config.Count * 12u);
            std::memcpy(bytes.data(), reference.Clearance.data(), reference.Clearance.size() * sizeof(double));
            std::memcpy(bytes.data() + Config.Count * sizeof(double),
                reference.Order.data(),
                reference.Order.size() * sizeof(std::uint32_t));
            return bytes;
        }
        R::EditorPointSamplingResult Started{};
    };
}

TEST_F(ResidentPointSampling, SecondAdmissionReusesCanonicalInputAndDiscardKeepsCpuUnchanged)
{
    auto first = Start();
    ASSERT_EQ(first.Status, R::EditorCommandStatus::Pending) << first.Message;
    EXPECT_NE(first.StepRun, 0u) << "the result names its step run";
    EXPECT_EQ(first.GpuInputUploadBytes, 12u * 12u);
    ASSERT_TRUE(Control(R::StepRunAction::Discard));
    ASSERT_TRUE(Pump(
        [&]
        {
            return Calls == 1u;
        }));
    EXPECT_EQ(Last.Status, R::EditorCommandStatus::StaleEntity) << Last.Message;
    EXPECT_FALSE(Rows().Exists(Config.RankName));
    EXPECT_FALSE(Rows().Exists(Config.SelectedName));
    ASSERT_TRUE(Jobs.DrainUntilTerminal()) << "the discarded run must release output admission";
    auto second = Start();
    ASSERT_EQ(second.Status, R::EditorCommandStatus::Pending) << second.Message;
    EXPECT_EQ(second.GpuInputUploadBytes, 0u);
    EXPECT_EQ(second.GpuInputCacheHits, 1u);
    EXPECT_EQ(second.CpuStageReadbackBytes, 0u);
    ASSERT_TRUE(Control(R::StepRunAction::Discard));
    ASSERT_TRUE(Pump(
        [&]
        {
            return Calls == 2u;
        }));
}

TEST_F(ResidentPointSampling, InputOutputAndTransformEditsInvalidateQueuedPublication)
{
    for (unsigned edit = 0; edit < 4; ++edit)
    {
        ASSERT_EQ(Start().Status, R::EditorCommandStatus::Pending);
        if (edit == 0) Rows().Get<glm::vec3>("v:position")[0].x += 1;
        if (edit == 1) Rows().GetOrAdd<float>(Config.RankName)[0] = 2;
        if (edit == 2) Rows().GetOrAdd<bool>(Config.SelectedName)[0] = true;
        if (edit == 3) S.Registry.Raw().get<T::Component>(Entity).Position.x += 1;
        ASSERT_TRUE(Pump(
            [&]
            {
                return Calls == edit + 1u;
            }))
            << "edit " << edit;
        EXPECT_EQ(Last.Status, R::EditorCommandStatus::StaleEntity) << Last.Message;
        if (edit == 1)
        {
            auto rank = Rows().Get<float>(Config.RankName);
            Rows().Remove(rank);
        }
        if (edit == 2)
        {
            auto selected = Rows().Get<bool>(Config.SelectedName);
            Rows().Remove(selected);
        }
    }
    EXPECT_EQ(Calls, 4u);
}

TEST_F(ResidentPointSampling, RefusedUploadAndRejectedJobNotifyExactlyOnceWithoutCpuPublication)
{
    Device.TransferQueue.AcceptBufferUploads = false;
    const auto refused = Start();
    EXPECT_EQ(refused.Status, R::EditorCommandStatus::GeometryProcessingFailed);
    EXPECT_EQ(Calls, 1u);
    EXPECT_FALSE(Rows().Exists(Config.RankName));
    Device.TransferQueue.AcceptBufferUploads = true;
    S.Context.JobCommands.Submit = [](R::JobDesc, R::EditorJobIdentity) { return R::JobToken{}; };
    EXPECT_EQ(Start().Status, R::EditorCommandStatus::GeometryProcessingFailed);
    EXPECT_EQ(Calls, 2u);
    EXPECT_FALSE(Rows().Exists(Config.RankName));
}

// RUNTIME-313: the Vulkan run is a queued editor job like the others: a second run on the
// same output is refused before any upload or submission, and a cancelled run reports once
// with the shared wording.
TEST_F(ResidentPointSampling, DuplicateOutputIsRefusedAndCancelUsesTheSharedWording)
{
    const auto findActive = S.Context.JobCommands.FindActive;
    S.Context.JobCommands.FindActive = [](const R::EditorJobIdentity& identity) {
        return std::optional{R::EditorJobRecord{.Token = R::JobToken{7, 2}, .Identity = identity,
                                                .State = R::JobState::AwaitingApply}};
    };
    const auto duplicate = Start();
    EXPECT_EQ(duplicate.Status, R::EditorCommandStatus::Pending);
    EXPECT_EQ(duplicate.Message, "Point sampling already has an active awaiting-apply job (job 7:2).");
    EXPECT_EQ(duplicate.StepRun, 0u);
    EXPECT_EQ(duplicate.GpuInputUploadBytes, 0u) << "refused before acquiring device input";
    EXPECT_EQ(Calls, 0u);
    // A CPU run would overwrite the same rank output too.
    Config.Backend = R::PointSamplingBackend::Cpu;
    const auto cpuDuplicate = Start();
    EXPECT_EQ(cpuDuplicate.Status, R::EditorCommandStatus::Pending);
    EXPECT_FALSE(Rows().Exists(Config.RankName));
    EXPECT_EQ(Calls, 0u);
    S.Context.JobCommands.FindActive = findActive;
    Config.Backend = R::PointSamplingBackend::Vulkan;
    ASSERT_EQ(Start().Status, R::EditorCommandStatus::Pending);
    const auto rows = S.Context.JobCommands.SnapshotAll();
    const auto run = std::ranges::find_if(rows,
        [](const auto& row)
        {
            return row.Name == "Vulkan point sampling";
        });
    ASSERT_NE(run, rows.end());
    EXPECT_EQ(S.Context.JobCommands.Cancel(run->Token), R::EditorJobCancelStatus::Requested);
    ASSERT_TRUE(Pump(
        [&]
        {
            return Calls == 1u;
        }));
    EXPECT_EQ(Last.Status, R::EditorCommandStatus::StaleEntity);
    EXPECT_EQ(Last.Message, "Vulkan point sampling cancelled or stale; previous output retained.");
}

// RUNTIME-317: a cancelled Vulkan run keeps its GPU request but resolved to no backend; the cancellation is its
// diagnostic.
TEST_F(ResidentPointSampling, CancelledVulkanRunResolvesToNoBackend)
{
    ASSERT_EQ(Start().Status, R::EditorCommandStatus::Pending);
    const auto submitted = S.Context.JobCommands.SnapshotAll();
    const auto run = std::ranges::find_if(submitted,
        [](const auto& row)
        {
            return row.Name == "Vulkan point sampling";
        });
    ASSERT_NE(run, submitted.end());
    EXPECT_EQ(run->RequestedJobDomain, R::EditorJobDomain::GpuCompute);
    const auto token = run->Token;
    EXPECT_EQ(S.Context.JobCommands.Cancel(token), R::EditorJobCancelStatus::Requested);
    ASSERT_TRUE(Jobs.DrainUntilTerminal());
    ASSERT_EQ(Calls, 1u);
    EXPECT_EQ(Last.Status, R::EditorCommandStatus::StaleEntity);
    const auto rows = S.Context.JobCommands.SnapshotAll();
    const auto cancelled = std::ranges::find_if(rows,
        [&](const auto& row)
        {
            return row.Token == token;
        });
    ASSERT_NE(cancelled, rows.end());
    EXPECT_EQ(cancelled->State, R::JobState::Cancelled);
    EXPECT_FALSE(cancelled->ResolvedJobDomain.has_value());
    EXPECT_EQ(cancelled->Diagnostic, Last.Message);
}

// ARCH-023: a completion-only computation is one compute-lane submission of zero readback bytes. It is
// Ready when its own sink runs (the device finished), not after a count of graphics frames: however many
// frames retire before that, it stays Submitted.
TEST_F(ResidentPointSampling, CompletionOnlyComputeIsReadyWhenItsSubmissionCompletes)
{
    std::optional<Extrinsic::RHI::ReadbackSink> sink;
    Device.ComputeReadback = [&](auto record, std::uint64_t bytes, auto completion)
    {
        EXPECT_EQ(bytes, 0u);
        EXPECT_TRUE(record(Device.AsyncComputeContext).IsValid());
        sink = std::move(completion);
        return Extrinsic::RHI::ReadbackToken{(1ull << 63u) | 1u};
    };
    auto result = Cache.QueueGpuCompute(0u, [](auto&, const auto&) { return Extrinsic::RHI::BufferHandle{1, 1}; });
    ASSERT_TRUE(result && sink);
    EXPECT_EQ(result->State, R::SpatialQueryState::Submitted);
    for (int i = 0; i < 4; ++i)
    {
        Jobs.Jobs().RecordGpuQueueFrameCommands(Device.CommandContext);
        Device.GlobalFrameNumber += Device.FramesInFlight + 1u;
        (void)Jobs.Jobs().DrainGpuQueueCompletedTransfers();
    }
    EXPECT_EQ(result->State, R::SpatialQueryState::Submitted);
    sink->Deliver({});
    EXPECT_EQ(result->State, R::SpatialQueryState::Ready);
    EXPECT_TRUE(result->Data.empty());
    EXPECT_EQ(Device.CommandContext.DispatchCalls + Device.CommandContext.FillBufferCalls, 0);
}

TEST_F(ResidentPointSampling, WorkspaceReadsResidentPropertiesAndUploadsOnlyMetadata)
{
    namespace G = Extrinsic::Graphics;
    auto& residency = *Cache.PropertyResidency();
    const auto view = R::ResolveGpuPropertyInput(residency, S.Registry, S.World, Entity,
        {.Domain = D::PointCloudPoint, .Name = "v:position", .ValueKind = Geometry::PropertyValueKind::Vec3});
    ASSERT_TRUE(view);
    const std::vector<std::uint32_t> rows{2, 4, 8};
    G::FarthestPointSamplingWorkspace workspace(Device, *Cache.MethodScratch());
    ASSERT_TRUE(workspace.Begin({.Positions = *view, .Rows = rows, .Count = 2}));
    Device.BufferWrites.clear();
    ASSERT_TRUE(workspace.RecordNext(Device.CommandContext).IsValid());
    // The only host writes are the live-row map and the world matrix; values are device inputs.
    ASSERT_EQ(Device.BufferWrites.size(), 2u);
    EXPECT_EQ(workspace.Produced(), 2u);
    EXPECT_TRUE(workspace.Finished());
    auto wrong = *view;
    wrong.Layout.Channels = 4;
    EXPECT_FALSE(workspace.Begin({.Positions = wrong, .Count = 2}));
    const std::vector<std::uint32_t> invalidRows{12};
    EXPECT_FALSE(workspace.Begin({.Positions = *view, .Rows = invalidRows, .Count = 1}));
}

TEST_F(ResidentPointSampling, TerminalGpuOrderPublishesAtomicallyAndUndoRestoresBothFields)
{
    std::vector<std::uint64_t> bytesAsked; // written by the submitting thread before Track publishes the page
    Device.ComputeReadback = [&](auto record, std::uint64_t bytes, auto completion)
    {
        bytesAsked.push_back(bytes);
        EXPECT_TRUE(record(Device.CommandContext).IsValid());
        return Track(std::move(completion));
    };
    const auto& rows = std::as_const(Rows());
    const auto points = rows.Get<glm::vec3>("v:position");
    const auto reference = Geometry::PointSampling::Order(std::span<const glm::vec3>(points.Vector()), {}, Config.Count);
    ASSERT_TRUE(reference.Succeeded());
    ASSERT_EQ(Start().Status, R::EditorCommandStatus::Pending);
    ASSERT_TRUE(Pump(
        [&]
        {
            return HasInFlight();
        }));
    ASSERT_EQ(bytesAsked, std::vector<std::uint64_t>{Config.Count * 12u}) << "one chunk, the terminal readback";
    EXPECT_FALSE(Rows().Exists(Config.RankName));
    Complete(DeviceBytes(reference));
    ASSERT_TRUE(Pump(
        [&]
        {
            return Calls == 1u;
        }));
    EXPECT_EQ(Last.Status, R::EditorCommandStatus::Applied) << Last.Message;
    EXPECT_EQ(Last.Backend, "gpu_vulkan_compute");
    EXPECT_EQ(Last.Steps, Config.Count) << "one step per selected sample";
    EXPECT_EQ(Last.StepRun, Started.StepRun);
    for (std::size_t i = 0; i < reference.Order.size(); ++i)
    {
        EXPECT_EQ(rows.Get<float>(Config.RankName)[reference.Order[i]], float(i));
        EXPECT_TRUE(rows.Get<bool>(Config.SelectedName)[reference.Order[i]]);
    }
    ASSERT_TRUE(S.History.Undo().Succeeded());
    EXPECT_FALSE(rows.Exists(Config.RankName));
    EXPECT_FALSE(rows.Exists(Config.SelectedName));
    ASSERT_TRUE(S.History.Redo().Succeeded());
    EXPECT_TRUE(rows.Exists(Config.RankName));
    EXPECT_FALSE(S.History.Redo().Succeeded()) << "one history entry";
    EXPECT_EQ(Calls, 1u);
}

// ARCH-023: one selected sample is one step. A ready run computes nothing; Step(1) selects the first sample and
// Step(2) two more, each chunk capped by the steps left and read back by none; Accept reads the three samples back
// once and publishes exactly them; a Pause during a chunk counts every sample of it.
TEST_F(ResidentPointSampling, StepsSelectExactlyAndAPausedChunkCountsWhole)
{
    std::vector<std::uint64_t> bytesAsked; // written by the submitting thread before Track publishes the page
    Device.ComputeReadback = [&](auto record, std::uint64_t bytes, auto completion)
    {
        bytesAsked.push_back(bytes);
        EXPECT_TRUE(record(Device.CommandContext).IsValid());
        return Track(std::move(completion));
    };
    const auto points = std::as_const(Rows()).Get<glm::vec3>("v:position");
    const auto reference =
        Geometry::PointSampling::Order(std::span<const glm::vec3>(points.Vector()), {}, Config.Count);
    Started = R::StartEditorPointSampling(S.Commands(),
        Config,
        [this](auto result)
        {
            ++Calls;
            Last = std::move(result);
        });
    ASSERT_EQ(Started.Status, R::EditorCommandStatus::Pending) << Started.Message;
    (void)Pump(
        []
        {
            return false;
        }); // frames pass
    EXPECT_TRUE(bytesAsked.empty()) << "ready: nothing is submitted";
    EXPECT_EQ(Observe().Phase, R::StepRunPhase::Ready);
    ASSERT_TRUE(Control(R::StepRunAction::Step, 1u));
    ASSERT_TRUE(Pump(
        [&]
        {
            return HasInFlight();
        }));
    Complete();
    ASSERT_TRUE(Pump(
        [&]
        {
            return Observe().Phase == R::StepRunPhase::Paused && !Observe().SegmentInFlight;
        }));
    EXPECT_EQ(Observe().Steps, 1u);
    ASSERT_TRUE(Control(R::StepRunAction::Step, 2u));
    ASSERT_TRUE(Pump(
        [&]
        {
            return HasInFlight();
        }));
    Complete();
    ASSERT_TRUE(Pump(
        [&]
        {
            return Observe().Phase == R::StepRunPhase::Paused && !Observe().SegmentInFlight;
        }));
    EXPECT_EQ(Observe().Steps, 3u);
    EXPECT_EQ(bytesAsked, (std::vector<std::uint64_t>{0u, 0u})) << "no chunk before Accept reads samples back";
    ASSERT_TRUE(Control(R::StepRunAction::Accept));
    ASSERT_TRUE(Pump(
        [&]
        {
            return HasInFlight();
        }));
    EXPECT_EQ(bytesAsked.back(), Config.Count * 12u) << "Accept's one readback";
    Complete(DeviceBytes(reference)); // the device holds the first three; the rest is not read
    ASSERT_TRUE(Pump(
        [&]
        {
            return Calls == 1u;
        }));
    ASSERT_EQ(Last.Status, R::EditorCommandStatus::Applied) << Last.Message;
    EXPECT_EQ(Last.Steps, 3u);
    const auto rank = std::as_const(Rows()).Get<float>(Config.RankName);
    for (std::size_t i = 0; i < reference.Order.size(); ++i)
        EXPECT_EQ(rank[reference.Order[i]], i < 3u ? float(i) : -1.0f) << "sample " << i;
    // Run, then Pause while the chunk is in flight: its samples all count.
    Config.RankName = "v:rank_paused";
    Config.SelectedName = "v:selected_paused";
    Started = R::StartEditorPointSampling(S.Commands(),
        Config,
        [this](auto result)
        {
            ++Calls;
            Last = std::move(result);
        });
    ASSERT_TRUE(Control(R::StepRunAction::Run));
    ASSERT_TRUE(Pump(
        [&]
        {
            return HasInFlight();
        }));
    ASSERT_TRUE(Control(R::StepRunAction::Pause));
    Complete(DeviceBytes(reference));
    ASSERT_TRUE(Pump(
        [&]
        {
            return Observe().Phase == R::StepRunPhase::Paused && !Observe().SegmentInFlight;
        }));
    EXPECT_EQ(Observe().Steps, Config.Count) << "the chunk in flight at the pause selected every sample";
    EXPECT_EQ(Observe().End, R::StepRunEnd::IterationCap);
    ASSERT_TRUE(Control(R::StepRunAction::Discard));
    ASSERT_TRUE(Pump(
        [&]
        {
            return Calls == 2u;
        }));
    EXPECT_EQ(Last.Status, R::EditorCommandStatus::StaleEntity);
    EXPECT_FALSE(Rows().Exists("v:rank_paused")) << "a discarded run publishes nothing";
}

// ARCH-023: a batch Run selects its samples on the step run's worker: once its chunk was submitted, the chunk's
// completion alone (no drain, frame or pump on this thread) counts every sample, the worker taking the order it pulled.
// Prediction: Steps == Count before any further drain, the chunk asked for a pulled readback of Count * 12 bytes.
// Mutation: an owner-polled chunk (an ordinary sink) leaves Steps at 0 until a drain runs.
TEST_F(ResidentPointSampling, ABatchRunCountsItsSamplesOnItsWorkerWithoutDrains)
{
    std::vector<std::uint64_t> bytesAsked;
    bool pulled = false;
    Device.ComputeReadback = [&](auto record, std::uint64_t bytes, Extrinsic::RHI::ReadbackSink completion)
    {
        bytesAsked.push_back(bytes);
        pulled = completion.Pulled;
        EXPECT_TRUE(record(Device.CommandContext).IsValid());
        return Track(std::move(completion));
    };
    const auto& rows = std::as_const(Rows());
    const auto points = rows.Get<glm::vec3>("v:position");
    const auto reference =
        Geometry::PointSampling::Order(std::span<const glm::vec3>(points.Vector()), {}, Config.Count);
    ASSERT_EQ(Start().Status, R::EditorCommandStatus::Pending);
    ASSERT_TRUE(Pump(
        [&]
        {
            return HasInFlight();
        }));
    EXPECT_EQ(bytesAsked, std::vector<std::uint64_t>{Config.Count * 12u});
    EXPECT_TRUE(pulled) << "the worker pulls its chunk's order";
    Complete(DeviceBytes(reference)); // from here on this thread does not drain
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
    while (Observe().Steps < Config.Count && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    EXPECT_EQ(Observe().Steps, Config.Count);
    EXPECT_EQ(Calls, 0u) << "the publication stays the owner's";
    ASSERT_TRUE(Pump(
        [&]
        {
            return Calls == 1u;
        }));
    EXPECT_EQ(Last.Status, R::EditorCommandStatus::Applied) << Last.Message;
    EXPECT_EQ(Last.Steps, Config.Count);
}

TEST_F(ResidentPointSampling, RefusedRecordedSubmissionIsNotReplayedOrPublished)
{
    unsigned records = 0;
    Device.ComputeReadback = [&](auto record, std::uint64_t, auto) {
        ++records;
        EXPECT_TRUE(record(Device.CommandContext).IsValid());
        return Extrinsic::RHI::ReadbackToken{};
    };
    ASSERT_EQ(Start().Status, R::EditorCommandStatus::Pending);
    ASSERT_TRUE(Pump(
        [&]
        {
            return Calls == 1u;
        }));
    Jobs.Jobs().RecordGpuQueueFrameCommands(Device.CommandContext);
    EXPECT_EQ(Last.Status, R::EditorCommandStatus::GeometryProcessingFailed) << Last.Message;
    EXPECT_FALSE(Rows().Exists(Config.RankName));
    (void)Pump(
        []
        {
            return false;
        });
    EXPECT_EQ(Calls, 1u);
    EXPECT_EQ(records, 1u) << "a refused submission is not replayed";
}

TEST_F(ResidentPointSampling, WeightAndDeletionChangesRefusePublication)
{
    Config.WeightsName = "v:importance";
    (void)Rows().GetOrAdd<float>(Config.WeightsName, 1.0f);
    for (unsigned edit = 0; edit < 2; ++edit)
    {
        ASSERT_EQ(Start().Status, R::EditorCommandStatus::Pending);
        if (edit == 0) Rows().Get<float>(Config.WeightsName)[0] = 2.0f;
        else Rows().GetOrAdd<bool>("v:deleted", false)[0] = true;
        ASSERT_TRUE(Pump(
            [&]
            {
                return Calls == edit + 1u;
            }));
        EXPECT_EQ(Last.Status, R::EditorCommandStatus::StaleEntity);
        EXPECT_FALSE(Rows().Exists(Config.RankName));
    }
    EXPECT_EQ(Calls, 2u);
}

TEST_F(ResidentPointSampling, WorkspacePagesRoundsWithinTheSubmissionBudget)
{
    namespace G = Extrinsic::Graphics;
    const auto count = G::FarthestPointSamplingWorkspace::MaxPoints;
    const auto buffer = Device.CreateBuffer({.SizeBytes = std::uint64_t(count) * 12u,
        .Usage = Extrinsic::RHI::BufferUsage::Storage});
    const G::GpuPropertyView input{.Buffer = buffer, .Address = Device.GetBufferDeviceAddress(buffer),
        .Bytes = std::uint64_t(count) * 12u, .Layout = {.Channels = 3, .Count = count}};
    G::FarthestPointSamplingWorkspace workspace(Device, *Cache.MethodScratch());
    ASSERT_TRUE(workspace.Begin({.Positions = input, .Count = count}));
    EXPECT_FALSE(workspace.NextChunkFinishes());
    ASSERT_TRUE(workspace.RecordNext(Device.CommandContext).IsValid());
    const auto first = workspace.Produced();
    EXPECT_LE(std::uint64_t(first - 1u) * count, G::FarthestPointSamplingWorkspace::MaxPairsPerSubmission);
    EXPECT_FALSE(workspace.Finished());
    ASSERT_TRUE(workspace.RecordNext(Device.CommandContext).IsValid());
    EXPECT_LE(std::uint64_t(workspace.Produced() - first) * count, G::FarthestPointSamplingWorkspace::MaxPairsPerSubmission);
    EXPECT_GT(workspace.Produced(), first);
    for (const auto& dispatch : Device.CommandContext.DispatchRecords) EXPECT_LE(dispatch.X, 65535u);
}

// ARCH-023: a chunk adds at most the samples its step run allows (the first one's initial sample counts), the count
// caps it too, and a zero allowance is a readback-only chunk, refused before the first sample.
TEST_F(ResidentPointSampling, CappedChunksAddExactlyTheAllowedSamples)
{
    namespace G = Extrinsic::Graphics;
    const auto view = R::ResolveGpuPropertyInput(*Cache.PropertyResidency(),
        S.Registry,
        S.World,
        Entity,
        {.Domain = D::PointCloudPoint, .Name = "v:position", .ValueKind = Geometry::PropertyValueKind::Vec3});
    ASSERT_TRUE(view);
    G::FarthestPointSamplingWorkspace workspace(Device, *Cache.MethodScratch());
    auto& commands = Device.CommandContext;
    ASSERT_TRUE(workspace.Begin({.Positions = *view, .Count = 5}));
    EXPECT_EQ(workspace.NextChunkSamples(2u), 2u);
    EXPECT_FALSE(workspace.NextChunkFinishes(2u));
    EXPECT_FALSE(workspace.RecordNext(commands, 0u).IsValid()) << "nothing to read before the first sample";
    ASSERT_TRUE(workspace.RecordNext(commands, 2u).IsValid());
    EXPECT_EQ(workspace.Produced(), 2u);
    EXPECT_EQ(workspace.NextChunkSamples(10u), 3u) << "the count caps a chunk too";
    EXPECT_TRUE(workspace.NextChunkFinishes(10u));
    ASSERT_TRUE(workspace.RecordNext(commands, 0u).IsValid()) << "a readback-only chunk";
    EXPECT_EQ(workspace.Produced(), 2u);
    ASSERT_TRUE(workspace.RecordNext(commands, 3u).IsValid());
    EXPECT_TRUE(workspace.Finished());
    EXPECT_TRUE(workspace.RecordNext(commands, 0u).IsValid()) << "after the last sample only a readback";
    EXPECT_FALSE(workspace.RecordNext(commands).IsValid()) << "no sample left to add";
}

TEST_F(ResidentPointSampling, WarmWorkspaceKeepsCapacityAndUnbindsPriorRowMap)
{
    namespace G = Extrinsic::Graphics;
    auto view = R::ResolveGpuPropertyInput(*Cache.PropertyResidency(), S.Registry, S.World, Entity,
        {.Domain = D::PointCloudPoint, .Name = "v:position", .ValueKind = Geometry::PropertyValueKind::Vec3});
    ASSERT_TRUE(view);
    std::vector<std::uint32_t> rows(view->Layout.Count);
    for (std::uint32_t i = 0; i < rows.size(); ++i) rows[i] = std::uint32_t(rows.size()) - i - 1;
    G::FarthestPointSamplingWorkspace workspace(Device, *Cache.MethodScratch());
    ASSERT_TRUE(workspace.Begin({.Positions = *view, .Rows = rows, .Count = 3}));
    ASSERT_TRUE(workspace.RecordNext(Device.CommandContext).IsValid());
    const auto buffers = Device.CreateBufferCount;
    const auto pipelines = Device.CreatePipelineCount;
    Device.BufferWrites.clear();
    ASSERT_TRUE(workspace.Begin({.Positions = *view, .FirstIndex = 1, .Count = 2}));
    ASSERT_TRUE(workspace.RecordNext(Device.CommandContext).IsValid());
    EXPECT_EQ(Device.CreateBufferCount, buffers);
    EXPECT_EQ(Device.CreatePipelineCount, pipelines);
    EXPECT_EQ(workspace.Produced(), 2u);
    ASSERT_EQ(Device.BufferWrites.size(), 1u); // New transform, no stale row map upload.
    const auto& push = Device.CommandContext.PushConstantPayloads.back();
    ASSERT_EQ(push.size(), 112u);
    std::uint64_t rowAddress{};
    std::memcpy(&rowAddress, push.data() + 88, sizeof(rowAddress));
    EXPECT_EQ(rowAddress, 0u);
    // Growing only the result count preserves the other scratch buffers and pipeline.
    ASSERT_TRUE(workspace.Begin({.Positions = *view, .Count = 4}));
    ASSERT_TRUE(workspace.RecordNext(Device.CommandContext).IsValid());
    EXPECT_EQ(Device.CreateBufferCount, buffers + 1);
    EXPECT_EQ(Device.CreatePipelineCount, pipelines);
}

// RUNTIME-289, ARCH-023 (RUNTIME-355 F3): a Bayesian CPD run orders its target subsample on the device in its first
// step, on the step run's worker: no pump job and no framed ordering job; the start queues only the run's holder, and
// neither the start nor the holder's pump (which only starts the worker segment) submits a page. The segment's step
// submits exactly one bounded page, ordered after the issued input uploads and pulled by that worker. The solver
// registers exactly the device order once its verified prefix (64 samples) matches the CPU: a delivered order that
// differs only after the prefix is what the run uses. A mismatch inside the prefix, or a refused page, leaves the CPU
// order with the reason; a discard or a cancelled segment before the step submits nothing. The untouched source set
// reports no backend. Red before F3: the start queued three jobs (holder, pump, framed ordering).
TEST_F(ResidentPointSampling, CpdTargetSubsampleIsOrderedOnTheDeviceBeforeTheFirstStep)
{
    const auto source = MakeCloud(S, 40);
    const auto target = MakeCloud(S, 100);
    std::vector<R::JobDesc> jobs;
    // ARCH-023: the captured lane also lists and cancels its own jobs once a run is discarded, as the real surface does,
    // so the discarded run's holder is cancelled (its gate answers false) rather than applied. Until then the real
    // listing answers (the step run copies the surface at its start, so these are installed before it). Shared, so an
    // early return leaves nothing dangling in the scene's surface.
    struct CapturedLane
    {
        bool List{false};
        std::vector<R::EditorJobIdentity> Identities{};
        std::vector<R::JobToken> Cancelled{};
    };
    const auto lane = std::make_shared<CapturedLane>();
    S.Context.JobCommands.Submit = [&jobs, lane](R::JobDesc desc, R::EditorJobIdentity identity)
    {
        jobs.push_back(std::move(desc));
        lane->Identities.push_back(std::move(identity));
        return R::JobToken{std::uint32_t(jobs.size()), 1};
    };
    S.Context.JobCommands.SnapshotAll = [lane, real = S.Context.JobCommands.SnapshotAll]
    {
        if (!lane->List)
            return real ? real() : std::vector<R::EditorJobRecord>{};
        std::vector<R::EditorJobRecord> records;
        for (std::size_t i = 0; i < lane->Identities.size(); ++i)
        {
            const R::JobToken token{std::uint32_t(i + 1u), 1};
            records.push_back({.Token = token,
                .Identity = lane->Identities[i],
                .State = std::ranges::find(lane->Cancelled, token) != lane->Cancelled.end()
                    ? R::JobState::Cancelled
                    : R::JobState::AwaitingApply});
        }
        return records;
    };
    S.Context.JobCommands.Cancel = [lane, real = S.Context.JobCommands.Cancel](const R::JobToken token)
    {
        if (!lane->List)
            return real ? real(token) : R::EditorJobCancelStatus::Unavailable;
        lane->Cancelled.push_back(token);
        return R::EditorJobCancelStatus::Requested;
    };
    S.Context.JobCommands.Retire = {}; // the step run's retirement falls back to this lane's Cancel
    constexpr std::uint32_t kCount = 80;
    const auto points = std::as_const(S.Registry.Raw().get<GS::Vertices>(target).Properties).Get<glm::vec3>("v:position");
    const auto reference = Geometry::PointSampling::Order(std::span<const glm::vec3>(points.Vector()), {}, kCount);
    ASSERT_TRUE(reference.Succeeded());
    // The device: a pulled page completes at once with `answer` (its worker takes the bytes), unless it is refused.
    std::vector<std::byte> answer;
    bool refuse = false;
    std::vector<std::uint64_t> pageBytes; // every accepted or refused submission's readback size, in order
    std::size_t pulledPages = 0u;
    std::function<void()> cancelSecondOrdering;
    Device.ComputeReadback = [&](auto record, const std::uint64_t bytes, Extrinsic::RHI::ReadbackSink completion)
    {
        EXPECT_TRUE(record(Device.CommandContext).IsValid());
        std::scoped_lock lock{Gpu};
        pageBytes.push_back(bytes);
        if (pageBytes.size() == 2u && cancelSecondOrdering)
            std::exchange(cancelSecondOrdering, {})();
        if (refuse)
            return Extrinsic::RHI::ReadbackToken{};
        const Extrinsic::RHI::ReadbackToken token{(1ull << 63u) | ++Submissions};
        if (completion.Pulled)
        {
            ++pulledPages;
            Pulled.emplace_back(token.Value, answer);
        }
        return token;
    };
    const auto submitted = [&]
    {
        std::scoped_lock lock{Gpu};
        return pageBytes.size();
    };
    R::CoherentPointDriftConfig config{.SourceStableEntityId = Id(source), .TargetStableEntityId = Id(target),
                                       .Method = R::CoherentPointDriftMethod::Bayesian, .OutlierWeight = 0.1,
                                       .Output = R::CoherentPointDriftOutput::DisplacementProperty,
                                       .SubsampleTarget = kCount};
    config.SubsampleBackend = R::PointSamplingBackend::Vulkan;
    R::StepRunsService runs;
    S.Context.StepRuns = &runs;
    enum class Delivery
    {
        AfterPrefix,
        InsidePrefix,
        Refused,
        Cancelled,
        StepCancelled,
        CancelledAfterTarget
    };
    for (const auto delivery : {Delivery::AfterPrefix,
             Delivery::InsidePrefix,
             Delivery::Refused,
             Delivery::Cancelled,
             Delivery::StepCancelled,
             Delivery::CancelledAfterTarget})
    {
        SCOPED_TRACE(static_cast<int>(delivery));
        config.Subsample = delivery == Delivery::CancelledAfterTarget ? 16u : 0u;
        jobs.clear();
        *lane = {};
        auto order = reference.Order;
        if (delivery == Delivery::AfterPrefix)
            std::swap(order[70], order[71]);
        else if (delivery == Delivery::InsidePrefix)
            std::swap(order[1], order[2]);
        {
            std::scoped_lock lock{Gpu};
            pageBytes.clear();
            pulledPages = 0u;
            Pulled.clear();
            answer.assign(kCount * 12u, std::byte{0});
            std::memcpy(answer.data(), reference.Clearance.data(), kCount * sizeof(double));
            std::memcpy(answer.data() + kCount * sizeof(double), order.data(), kCount * sizeof(std::uint32_t));
            refuse = delivery == Delivery::Refused;
        }
        {
            std::scoped_lock lock{Device.ComputeInputOrderMutex};
            Device.ComputeInputOrders.clear();
        }
        R::EditorCoherentPointDriftResult failure;
        const auto run = R::StartEditorCoherentPointDrift(S.Commands(), config, failure);
        ASSERT_TRUE(run) << failure.Message;
        const auto id = R::SnapshotEditorCoherentPointDrift(run).StepRun;
        EXPECT_EQ(R::SnapshotEditorCoherentPointDrift(run).Result.SubsampleRequestedBackend, "gpu_vulkan_compute");
        ASSERT_EQ(jobs.size(), 1u) << "the start queues only the step run's holder: no pump, no framed ordering job";
        EXPECT_EQ(submitted(), 0u) << "a ready run submits no sampling page";
        std::string refusal;
        ASSERT_TRUE(runs.Control(id, R::StepRunAction::Step, 1u, refusal)) << refusal;
        EXPECT_FALSE(jobs[0].IsReadyToApply()); // the holder's pump starts the run's worker segment
        ASSERT_EQ(jobs.size(), 2u) << "the pump submitted one worker segment";
        EXPECT_EQ(submitted(), 0u) << "the owner's pump submits no sampling page";
        if (delivery == Delivery::CancelledAfterTarget)
            cancelSecondOrdering = [&]
            {
                lane->List = true;
                EXPECT_TRUE(runs.Control(id, R::StepRunAction::Discard, 1u, refusal));
                EXPECT_FALSE(jobs[0].IsReadyToApply()); // owner cancellation while the second page is being submitted
            };
        if (delivery == Delivery::Cancelled || delivery == Delivery::StepCancelled)
        {
            // Through the run's controls (its segment then runs no step), or by cancelling the segment (job lane /
            // panel) before it ran.
            lane->List = true;
            if (delivery == Delivery::Cancelled)
            {
                ASSERT_TRUE(runs.Control(id, R::StepRunAction::Discard, 1u, refusal));
                (void)jobs[1].Work(R::JobCancellation{});
                (void)jobs[1].PublishCompletion(Events, R::JobResultEnvelope::Make(true));
            }
            else
                jobs[1].FinalizeUnpublishedOnMainThread();
            EXPECT_FALSE(jobs[0].IsReadyToApply()); // the discarded run settles: its holder is cancelled
            jobs[0].FinalizeUnpublishedOnMainThread(); // the cancelled holder ends: the run's settled end
            EXPECT_EQ(submitted(), 0u) << "a run that ended before its first step submits no sampling page";
            EXPECT_EQ(runs.Observe(id)->Phase, R::StepRunPhase::Discarded);
            EXPECT_EQ(runs.Observe(id)->Steps, 0u);
            EXPECT_EQ(R::SnapshotEditorCoherentPointDrift(run).Result.SubsampleRequestedBackend, "gpu_vulkan_compute");
            continue;
        }
        (void)jobs[1].Work(R::JobCancellation{}); // the worker segment: its first step, here on this thread
        if (delivery == Delivery::CancelledAfterTarget)
        {
            (void)jobs[1].PublishCompletion(Events, R::JobResultEnvelope::Make(true));
            jobs[0].FinalizeUnpublishedOnMainThread();
            const auto ended = R::SnapshotEditorCoherentPointDrift(run);
            EXPECT_EQ(runs.Observe(id)->Phase, R::StepRunPhase::Discarded);
            EXPECT_EQ(runs.Observe(id)->Steps, 0u);
            EXPECT_EQ(ended.Result.SubsampleTargetBackend, "gpu_vulkan_compute")
                << "cancelling the source order must retain the completed target's actual backend";
            EXPECT_TRUE(ended.Result.SubsampleBackendDiagnostic.empty()) << "cancellation is no CPU fallback";
            EXPECT_EQ(submitted(), 2u);
            continue;
        }
        {
            std::scoped_lock lock{Gpu};
            ASSERT_EQ(pageBytes.size(), 1u) << "one bounded page orders the 80 samples";
            EXPECT_EQ(pageBytes[0], kCount * 12u) << "it reads back the order and clearances, once";
            EXPECT_EQ(pulledPages, delivery == Delivery::Refused ? 0u : 1u) << "the step's worker pulls its own page";
        }
        {
            std::scoped_lock lock{Device.ComputeInputOrderMutex};
            ASSERT_FALSE(Device.ComputeInputOrders.empty());
            EXPECT_EQ(Device.ComputeInputOrders.front(), Extrinsic::RHI::ComputeInputOrder::AfterIssuedTransfers)
                << "the run's first page waits for the issued input uploads";
        }
        ASSERT_TRUE(jobs[1].PublishCompletion(Events, R::JobResultEnvelope::Make(true)));
        const auto snapshot = R::SnapshotEditorCoherentPointDrift(run);
        ASSERT_TRUE(snapshot.TargetSamples);
        const bool device = delivery == Delivery::AfterPrefix;
        EXPECT_EQ(*snapshot.TargetSamples, device ? order : reference.Order);
        EXPECT_EQ(snapshot.Result.SubsampleSourceBackend, "");
        EXPECT_EQ(snapshot.Result.SubsampleTargetBackend, device ? "gpu_vulkan_compute" : "cpu_reference");
        if (delivery == Delivery::AfterPrefix)
            EXPECT_EQ(snapshot.Result.SubsampleBackendDiagnostic, "");
        else if (delivery == Delivery::InsidePrefix)
            EXPECT_NE(snapshot.Result.SubsampleBackendDiagnostic.find("differs from the CPU reference at sample 1"),
                std::string::npos)
                << snapshot.Result.SubsampleBackendDiagnostic;
        else
            EXPECT_EQ(snapshot.Result.SubsampleBackendDiagnostic,
                "Target: GPU sampling submission refused; sampling ran on the CPU.");
        EXPECT_EQ(runs.Observe(id)->Steps, 1u);
        lane->List = true;
        ASSERT_TRUE(runs.Control(id, R::StepRunAction::Discard, 1u, refusal));
        EXPECT_FALSE(jobs[0].IsReadyToApply()) << "the discarded run's holder is cancelled, not applied";
        EXPECT_NE(std::ranges::find(lane->Cancelled, R::JobToken{1, 1}), lane->Cancelled.end());
        jobs[0].FinalizeUnpublishedOnMainThread(); // the cancelled holder ends: the run's settled end
    }
    S.Context.StepRuns = nullptr;
}

// UI-081: the arrow order ranks every live element of a mesh domain, starts with the method's
// order (farthest point by default), is undoable, and goes stale when elements change.
TEST(PointSamplingOperations, GlyphOrderRanksEveryLiveElementOfEachMeshDomain)
{
    Scene s;
    const auto entity = s.Registry.Create();
    Geometry::HalfedgeMesh::Mesh mesh;
    std::vector<Geometry::VertexHandle> v;
    for (int j = 0; j < 6; ++j)
        for (int i = 0; i < 6; ++i)
            v.push_back(mesh.AddVertex({float(i), float(j), 0.1f * float(i * j)}));
    for (int j = 0; j + 1 < 6; ++j)
        for (int i = 0; i + 1 < 6; ++i)
            ASSERT_TRUE(mesh.AddQuad(v[j * 6 + i], v[j * 6 + i + 1], v[(j + 1) * 6 + i + 1], v[(j + 1) * 6 + i]));
    GS::PopulateFromMesh(s.Registry.Raw(), entity, mesh);
    for (const D domain : {D::MeshVertex, D::MeshEdge, D::MeshFace})
    {
        SCOPED_TRACE(std::string(R::GeometryGlyphOrderPropertyName(domain)));
        const auto result =
            R::ApplyEditorGlyphOrderCommand(s.Commands(), {.StableEntityId = Id(entity), .Domain = domain});
        ASSERT_TRUE(result.Succeeded()) << result.Message;
        const auto* properties = domain == D::MeshVertex ? &s.Registry.Raw().get<GS::Vertices>(entity).Properties
            : domain == D::MeshEdge                      ? &s.Registry.Raw().get<GS::Edges>(entity).Properties
                                                         : &s.Registry.Raw().get<GS::Faces>(entity).Properties;
        const auto rows = R::SortGeometryGlyphOrderRows(*properties, domain);
        ASSERT_TRUE(rows.has_value());
        EXPECT_EQ(rows->size(), properties->Size());
        EXPECT_EQ(result.LiveCount, properties->Size());
        if (domain == D::MeshVertex)
        {
            const auto positions = std::as_const(*properties).Get<glm::vec3>("v:position").Vector();
            const auto expected = Geometry::PointSampling::Order(std::span<const glm::vec3>(positions),
                Geometry::PointSampling::Params{.Method = Geometry::PointSampling::Method::FarthestPoint},
                positions.size());
            EXPECT_EQ(*rows, expected.Order) << "vertex rows follow the farthest-point order";
        }
    }
    const auto& vertices = std::as_const(s.Registry.Raw().get<GS::Vertices>(entity).Properties);
    ASSERT_TRUE(s.History.Undo().Succeeded());
    ASSERT_TRUE(s.History.Undo().Succeeded());
    ASSERT_TRUE(s.History.Undo().Succeeded());
    EXPECT_FALSE(vertices.Exists("v:glyph_rank"));
    EXPECT_FALSE(std::as_const(s.Registry.Raw().get<GS::Faces>(entity).Properties).Exists("f:glyph_rank"));
}

TEST(PointSamplingOperations, GlyphOrderSkipsDeletedRowsAndGoesStaleWithNewElements)
{
    Scene s;
    const auto cloud = MakeCloud(s, 50);
    auto& vertices = s.Registry.Raw().get<GS::Vertices>(cloud).Properties;
    auto deleted = vertices.GetOrAdd<bool>("v:deleted", false);
    deleted[7] = true;
    const auto result =
        R::ApplyEditorGlyphOrderCommand(s.Commands(), {.StableEntityId = Id(cloud), .Domain = D::PointCloudPoint});
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_EQ(result.LiveCount, 49u);
    EXPECT_EQ(std::as_const(vertices).Get<float>("v:glyph_rank")[7], -1.0f);
    const auto rows = R::SortGeometryGlyphOrderRows(vertices, D::PointCloudPoint);
    ASSERT_TRUE(rows.has_value());
    EXPECT_EQ(rows->size(), 49u);
    EXPECT_EQ(std::ranges::find(*rows, 7u), rows->end());
    // Ranks must be exactly 0..L-1 over the live rows.
    auto ranks = vertices.Get<float>("v:glyph_rank");
    const float kept = ranks[0];
    ranks[0] = ranks[1];
    EXPECT_FALSE(R::SortGeometryGlyphOrderRows(vertices, D::PointCloudPoint)) << "duplicate rank";
    ranks[0] = kept + 0.5f;
    EXPECT_FALSE(R::SortGeometryGlyphOrderRows(vertices, D::PointCloudPoint)) << "fractional rank";
    EXPECT_EQ(R::CountGeometryGlyphOrderRows(vertices, D::PointCloudPoint), 0u) << "memo sees the rank edit";
    ranks[0] = kept;
    ASSERT_TRUE(R::SortGeometryGlyphOrderRows(vertices, D::PointCloudPoint));
    EXPECT_EQ(R::CountGeometryGlyphOrderRows(vertices, D::PointCloudPoint), 49u);
    // Deleting the first-ranked element leaves rank 48 among 48 live rows: stale.
    deleted[rows->front()] = true;
    EXPECT_FALSE(R::SortGeometryGlyphOrderRows(vertices, D::PointCloudPoint)) << "stale after a deletion";
    EXPECT_EQ(R::CountGeometryGlyphOrderRows(vertices, D::PointCloudPoint), 0u) << "memo sees the deletion";
    deleted[rows->front()] = false;
    EXPECT_EQ(R::CountGeometryGlyphOrderRows(vertices, D::PointCloudPoint), 49u);
    vertices.Resize(51u); // a new element arrives with the property default
    EXPECT_FALSE(R::SortGeometryGlyphOrderRows(vertices, D::PointCloudPoint)) << "stale after an element change";
    EXPECT_EQ(R::CountGeometryGlyphOrderRows(vertices, D::PointCloudPoint), 0u) << "memo sees the new element";
}

// A rank name already taken by another type is refused, not overwritten (and not dereferenced).
TEST(PointSamplingOperations, GlyphOrderRefusesARankPropertyOfAnotherType)
{
    Scene s;
    const auto cloud = MakeCloud(s, 20);
    auto& vertices = s.Registry.Raw().get<GS::Vertices>(cloud).Properties;
    (void)vertices.GetOrAdd<std::int32_t>("v:glyph_rank", 3);
    const auto result =
        R::ApplyEditorGlyphOrderCommand(s.Commands(), {.StableEntityId = Id(cloud), .Domain = D::PointCloudPoint});
    EXPECT_EQ(result.Status, R::EditorCommandStatus::InvalidProcessingParameters) << result.Message;
    EXPECT_EQ(std::as_const(vertices).Get<std::int32_t>("v:glyph_rank")[0], 3);
    EXPECT_EQ(R::CountGeometryGlyphOrderRows(vertices, D::PointCloudPoint), 0u);
}

// The order is computed where the arrows are drawn: on a bound Position source when it resolves.
TEST(PointSamplingOperations, GlyphOrderUsesTheDisplayedPositions)
{
    Scene s;
    const auto cloud = MakeCloud(s, 60);
    auto& vertices = s.Registry.Raw().get<GS::Vertices>(cloud).Properties;
    std::vector<glm::vec3> shown(60u);
    for (std::size_t i = 0; i < shown.size(); ++i)
        shown[i] = {float(i % 6u), float(i / 6u), 0.0f};
    vertices.GetOrAdd<glm::vec3>("v:shown", glm::vec3{0.0f}).Vector() = shown;
    s.Registry.Raw().emplace<R::VertexChannelBindingSet>(cloud,
        R::VertexChannelBindingSet{
            .Points = {.Position = {.Enabled = true,
                           .Property = {D::PointCloudPoint, "v:shown", Geometry::PropertyValueKind::Vec3}}}});
    const auto result =
        R::ApplyEditorGlyphOrderCommand(s.Commands(), {.StableEntityId = Id(cloud), .Domain = D::PointCloudPoint});
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    const auto rows = R::SortGeometryGlyphOrderRows(vertices, D::PointCloudPoint);
    ASSERT_TRUE(rows.has_value());
    const auto fps = [](const std::vector<glm::vec3>& points)
    {
        return Geometry::PointSampling::Order(std::span<const glm::vec3>(points),
            Geometry::PointSampling::Params{.Method = Geometry::PointSampling::Method::FarthestPoint},
            points.size())
            .Order;
    };
    EXPECT_EQ(*rows, fps(shown));
    EXPECT_NE(*rows, fps(std::as_const(vertices).Get<glm::vec3>("v:position").Vector()));
}
