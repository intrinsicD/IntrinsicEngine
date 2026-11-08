// ARCH-022: the bilateral filter plugin's Vulkan passes (ARCH-023: each pass a worker step that pages its GPU kNN
// rows through the prepared query sessions, then updates on the CPU) in a Vulkan engine composed as the Sandbox
// composes it: three passes agree with the CPU LBVH run, page their queries and report their host traffic; inputs
// changed before the start end the run stale; a cancel while a page is held on the device ends it cancelled; in both
// nothing is written. A ready run steps like its batch run, and a discard with a page in flight writes nothing.
// Replaces the built-in filter's PointLBVHGpuSmoke.Bilateral* cases.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <chrono>
#include <functional>
#include <limits>
#include <thread>
#include <utility>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include "BilateralFilterHarness.hpp"
#include "PluginGpuSmoke.hpp"

import Extrinsic.Backends.Vulkan;
import Extrinsic.RHI.Device;
#include "HeldFramesProgress.hpp"
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.Runtime.SelectionController;

namespace
{
    using namespace Intrinsic::Tests::BilateralFilter;
    using D = R::GeometryElementDomain;

    // 66 points on a noisy patch (one far outlier) with normals along z; returns the entity.
    Geometry::PropertySet& Points(Extrinsic::ECS::Scene::Registry& scene, const entt::entity entity)
    {
        return scene.Raw().get<Extrinsic::ECS::Components::GeometrySources::Vertices>(entity).Properties;
    }

    entt::entity MakeCloud(Extrinsic::ECS::Scene::Registry& scene)
    {
        const auto entity = scene.Create();
        auto& props = scene.Raw().emplace<Extrinsic::ECS::Components::GeometrySources::Vertices>(entity).Properties;
        std::mt19937 random(241);
        std::uniform_real_distribution<float> dist(-1, 1);
        std::vector<glm::vec3> points;
        for (unsigned i = 0; i < 66; ++i) points.push_back({dist(random), dist(random), .1f * dist(random)});
        points[65] = {10, 0, 0};
        props.Resize(points.size());
        props.GetOrAdd<glm::vec3>("samples").Vector() = points;
        props.GetOrAdd<glm::vec3>("directions").Vector().assign(points.size(), glm::vec3(0, 0, 1));
        return entity;
    }

    BF::Config Settings(const entt::entity entity, const BF::FilterBackend backend, std::string output)
    {
        return {.StableEntityId = R::SelectionController::ToStableEntityId(entity),
            .Backend = backend,
            .Positions = {.Domain = D::PointCloudPoint,
                .Name = "samples",
                .ValueKind = Geometry::PropertyValueKind::Vec3},
            .Normals = {.Domain = D::PointCloudPoint,
                .Name = "directions",
                .ValueKind = Geometry::PropertyValueKind::Vec3},
            .Output = {.Domain = D::PointCloudPoint,
                .Name = std::move(output),
                .ValueKind = Geometry::PropertyValueKind::Vec3},
            .KNeighbors = 8,
            .GpuQueryBatchSize = 16,
            .SpatialSigma = 2,
            .Execution = {.IterationCap = 3u,
                .PreviewEvery = 0u,
                .PreviewIntervalMs = 0u,
                .AutoAccept = R::StepRunAutoAccept::End,
                .Start = R::StepRunStart::Run}};
    }

    using Gpu = Intrinsic::Tests::Plugins::Gpu<Harness>;

    // ARCH-023: holds the separate method queue so a worker step's page stays in flight (empty without such a lane).
    std::function<bool()> HoldMethodQueue(Gpu& gpu)
    {
        namespace Vk = Extrinsic::Backends::Vulkan;
        return Vk::HoldVulkanQueue(&gpu.H->Engine.GetDevice(), Vk::VulkanGatedQueue::MethodCompute);
    }
}

TEST(BilateralFilterPluginGpuSmoke, VulkanPassesAgreeWithCpuLbvhAndPageTheirQueries)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize()) GTEST_SKIP() << "GLFW unavailable";
    Gpu gpu;
    const auto cloud = MakeCloud(gpu.H->Scene());
    auto& props = Points(gpu.H->Scene(), cloud);
    std::shared_ptr<BF::Run> cpu, vulkan;
    std::string refusal;
    ASSERT_TRUE(gpu.Run([&]
    {
        if (!cpu) cpu = BF::Start(Settings(cloud, BF::FilterBackend::CpuLBVH, "cpu"));
        if (cpu->Final && !vulkan) vulkan = BF::Start(Settings(cloud, BF::FilterBackend::VulkanLBVH, "gpu"));
        if (!vulkan || !vulkan->Final) return false;
        // Then, once the input validation answered (it settles over frames): 65 candidates exceed the GPU's 64.
        auto refused = Settings(cloud, BF::FilterBackend::VulkanLBVH, "gpu");
        refused.KNeighbors = 64;
        refusal = BF::Preview(refused);
        return !refusal.starts_with("Checking");
    })) << "the frames ran out";
    EXPECT_NE(refusal.find("k<=63"), std::string::npos) << refusal;
    ASSERT_EQ(cpu->Final->Status, "applied") << cpu->Final->Message;
    ASSERT_EQ(vulkan->Final->Status, "applied") << vulkan->Final->Message;
    EXPECT_EQ(vulkan->Final->ActualBackend, "vulkan_lbvh");
    EXPECT_EQ(vulkan->Final->CompletedIterations, 3u);
    EXPECT_EQ(vulkan->Final->GpuQueryBatches, 3u * 5u) << "66 live points in pages of 16, three passes";
    // The hybrid's host traffic (ARCH-023 worker sessions, predicted before the first run): the cached index's 66
    // queries of 12 bytes once (pass 1); 66 packed rows of 9 neighbours plus a header (8 bytes each) per pass; the
    // moving target's rows (12 bytes each) once at prepare and for passes 2 and 3, which are also its queries (identity
    // IDs: no row map). Mutation: per-pass query uploads beside the rows give 3 x 66 x 12 query bytes.
    EXPECT_EQ(vulkan->Final->GpuQueryUploadBytes, 66u * 12u);
    EXPECT_EQ(vulkan->Final->GpuReadbackBytes, 3u * 66u * 10u * 8u);
    EXPECT_EQ(vulkan->Final->GpuWorkspaceUploadBytes, 3u * 66u * 12u);
    EXPECT_NE(vulkan->Final->StepRun, 0u);
    const auto a = std::as_const(props).Get<glm::vec3>("cpu").Vector(), b = std::as_const(props).Get<glm::vec3>("gpu").Vector();
    ASSERT_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i) EXPECT_LE(glm::length(a[i] - b[i]), 1e-5f) << i;
    EXPECT_GT(glm::length(b[0] - std::as_const(props).Get<glm::vec3>("samples")[0]), 0.f) << "the passes moved points";
}

// The brute-force strategy runs the same paged Vulkan chain: identical neighbourhood rows to the LBVH run (so
// bit-identical outputs), its own requested/actual token, and no GPU hierarchy build on the cached index or the
// private moved-point workspaces.
TEST(BilateralFilterPluginGpuSmoke, VulkanBruteForcePassesMatchLbvhWithoutGpuBuilds)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize())
        GTEST_SKIP() << "GLFW unavailable";
    Gpu gpu;
    const auto cloud = MakeCloud(gpu.H->Scene());
    auto& props = Points(gpu.H->Scene(), cloud);
    auto* cache = gpu.H->Engine.Services().Find<R::SpatialIndexCache>();
    ASSERT_NE(cache, nullptr);
    std::shared_ptr<BF::Run> lbvh, brute;
    std::uint64_t buildsBefore = 0, cpuBuildsBefore = 0;
    ASSERT_TRUE(gpu.Run(
        [&]
        {
            if (!lbvh)
                lbvh = BF::Start(Settings(cloud, BF::FilterBackend::VulkanLBVH, "lbvh"));
            if (lbvh->Final && !brute)
            {
                buildsBefore = cache->Stats().GpuBuilds;
                cpuBuildsBefore = cache->Stats().Builds;
                brute = BF::Start(Settings(cloud, BF::FilterBackend::VulkanBruteForce, "brute"));
            }
            return brute && brute->Final;
        }))
        << "the frames ran out";
    ASSERT_EQ(lbvh->Final->Status, "applied") << lbvh->Final->Message;
    ASSERT_EQ(brute->Final->Status, "applied") << brute->Final->Message;
    EXPECT_GT(buildsBefore, 0u) << "the LBVH run built its hierarchies";
    EXPECT_EQ(cache->Stats().GpuBuilds, buildsBefore) << "brute force must not build a GPU hierarchy";
    EXPECT_EQ(cache->Stats().Builds, cpuBuildsBefore)
        << "nor a CPU one: the moving workspace only GPU queries read has no CPU hierarchy";
    EXPECT_EQ(brute->Final->RequestedBackend, BF::FilterBackend::VulkanBruteForce);
    EXPECT_EQ(brute->Final->ActualBackend, "vulkan_brute_force");
    EXPECT_EQ(brute->Final->CompletedIterations, 3u);
    EXPECT_EQ(brute->Final->GpuQueryBatches, 3u * 5u) << "66 live points in pages of 16, three passes";
    const auto a = std::as_const(props).Get<glm::vec3>("lbvh").Vector(),
               b = std::as_const(props).Get<glm::vec3>("brute").Vector();
    ASSERT_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i)
        EXPECT_EQ(a[i], b[i]) << i;
    EXPECT_GT(glm::length(b[0] - std::as_const(props).Get<glm::vec3>("samples")[0]), 0.f) << "the passes moved points";
}

TEST(BilateralFilterPluginGpuSmoke, StaleInputsAndACancelBetweenStagesWriteNothing)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize()) GTEST_SKIP() << "GLFW unavailable";
    Gpu gpu;
    const auto cloud = MakeCloud(gpu.H->Scene());
    auto& props = Points(gpu.H->Scene(), cloud);
    auto* steps = gpu.H->Engine.Services().Find<R::StepRunsService>();
    ASSERT_NE(steps, nullptr);
    std::shared_ptr<BF::Run> stale, cancelled;
    std::function<bool()> release;
    bool cancelRequested = false, held = false;
    ASSERT_TRUE(gpu.Run([&]
    {
        if (!stale)
        {
            stale = BF::Start(Settings(cloud, BF::FilterBackend::VulkanLBVH, "stale_output"));
            props.Get<glm::vec3>("samples")[0].x += .1f; // before its first frame: the run's Valid ends it stale
        }
        if (stale->Final && !cancelled)
        {
            // The method queue held first: the run's first page stays on the device, so the cancel finds a step
            // waiting for it (ungated without a separate method queue: the cancel may then arrive at a rest).
            release = HoldMethodQueue(gpu);
            held = bool(release);
            cancelled = BF::Start(Settings(cloud, BF::FilterBackend::VulkanLBVH, "cancel_output"));
        }
        if (cancelled && !cancelRequested)
        {
            const auto o = steps->Observe(cancelled->Pending.StepRun);
            if (!held || (o && o->SegmentInFlight))
                cancelRequested = R::CancelPluginRun(*cancelled);
        }
        if (cancelled && cancelled->Final && release)
            (void)std::exchange(release, {})(); // the abandoned page completes; its bytes were released
        return cancelled && cancelled->Final;
    })) << "the frames ran out";
    if (release)
        (void)release();
    EXPECT_EQ(stale->Final->Status, "stale") << stale->Final->Message;
    EXPECT_TRUE(cancelRequested);
    EXPECT_EQ(cancelled->Final->Status, "cancelled") << cancelled->Final->Message;
    EXPECT_FALSE(props.Exists("stale_output"));
    EXPECT_FALSE(props.Exists("cancel_output"));
    EXPECT_FALSE(gpu.H->History().CanUndo());
}

// ARCH-023: a ready Vulkan run computes nothing; step 3 (two steps, then one) and Accept equal the batch run
// bit for bit (the same passes); a run discarded while a kNN batch is in flight writes nothing, and the frames after
// it (the batch's completion, the run's retirement) stay clean.
TEST(BilateralFilterPluginGpuSmoke, AReadyVulkanRunStepsLikeItsBatchRunAndADiscardInFlightWritesNothing)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize())
        GTEST_SKIP() << "GLFW unavailable";
    Gpu gpu;
    const auto cloud = MakeCloud(gpu.H->Scene());
    auto& props = Points(gpu.H->Scene(), cloud);
    auto* steps = gpu.H->Engine.Services().Find<R::StepRunsService>();
    auto* cache = gpu.H->Engine.Services().Find<R::SpatialIndexCache>();
    ASSERT_NE(steps, nullptr);
    ASSERT_NE(cache, nullptr);
    const auto control = [&](const std::uint64_t run, const R::StepRunAction action, const std::uint32_t count = 1u)
    {
        std::string refusal;
        return steps->Control(run, action, count, refusal);
    };
    const auto rests = [&](const std::uint64_t run, const std::uint32_t count)
    {
        const auto o = steps->Observe(run);
        return o && o->Phase == R::StepRunPhase::Paused && !o->SegmentInFlight && o->Steps == count;
    };
    std::shared_ptr<BF::Run> batch, stepped, discarded;
    std::uint64_t submissionsAtReady = 0;
    int readyFrames = 0, stage = 0, settleFrames = 0;
    bool discardSent = false, held = false;
    std::function<bool()> release;
    ASSERT_TRUE(gpu.Run(
        [&]
        {
            if (!batch)
                batch = BF::Start(Settings(cloud, BF::FilterBackend::VulkanLBVH, "batch"));
            if (!batch->Final)
                return false;
            if (!stepped)
            {
                auto ready = Settings(cloud, BF::FilterBackend::VulkanLBVH, "stepped");
                ready.Execution.Start = R::StepRunStart::Ready;
                ready.Execution.AutoAccept = R::StepRunAutoAccept::Never;
                stepped = BF::Start(ready);
                submissionsAtReady = cache->Stats().GpuQuerySubmissions;
                return stepped->Final.has_value();
            }
            const auto id = stepped->Pending.StepRun;
            if (stage == 0 && ++readyFrames < 10)
                return false; // ready: nothing is queried meanwhile
            if (stage == 0)
            {
                EXPECT_EQ(cache->Stats().GpuQuerySubmissions, submissionsAtReady) << "a ready run queries nothing";
                EXPECT_TRUE(control(id, R::StepRunAction::Step, 2u));
                stage = 1;
            }
            if (stage == 1 && rests(id, 2u))
                stage = 2, (void)control(id, R::StepRunAction::Step, 1u);
            if (stage == 2 && rests(id, 3u))
                stage = 3, (void)control(id, R::StepRunAction::Accept);
            if (stage < 3 || !stepped->Final)
                return false;
            if (!discarded)
            {
                // A pass's page held on the device (ungated without a separate method queue).
                release = HoldMethodQueue(gpu);
                held = bool(release);
                discarded = BF::Start(Settings(cloud, BF::FilterBackend::VulkanLBVH, "discarded"));
                return discarded->Final.has_value();
            }
            if (!discardSent)
            {
                // A pass is in flight: its worker step waits for its held kNN page.
                const auto o = steps->Observe(discarded->Pending.StepRun);
                if (o && (o->SegmentInFlight || !held))
                    discardSent = control(discarded->Pending.StepRun, R::StepRunAction::Discard);
                return false;
            }
            if (discarded->Final && release)
                (void)std::exchange(release, {})();
            return discarded->Final && ++settleFrames > 30;
        }))
        << "the frames ran out";
    if (release)
        (void)release();
    ASSERT_EQ(batch->Final->Status, "applied") << batch->Final->Message;
    ASSERT_EQ(stepped->Final->Status, "applied") << stepped->Final->Message;
    EXPECT_EQ(stepped->Final->CompletedIterations, 3u);
    EXPECT_EQ(
        std::as_const(props).Get<glm::vec3>("stepped").Vector(), std::as_const(props).Get<glm::vec3>("batch").Vector())
        << "two steps and one equal the three-pass batch run";
    EXPECT_TRUE(discardSent);
    EXPECT_EQ(discarded->Final->Status, "discarded") << discarded->Final->Message;
    EXPECT_FALSE(props.Exists("discarded"));
    EXPECT_EQ(gpu.H->History().UndoCount(), 2u) << "the batch run and the stepped run's one Accept";
    const auto retired = steps->Observe(discarded->Pending.StepRun);
    ASSERT_TRUE(retired);
    EXPECT_EQ(retired->Phase, R::StepRunPhase::Discarded);
    EXPECT_FALSE(retired->SegmentInFlight) << "the in-flight pass settled";
    EXPECT_FALSE(retired->CanRestart) << "and the run dropped the plugin's callables";
}

TEST(BilateralFilterPluginGpuSmoke, ZeroPassesAcceptTheInitializedStateWithoutGpuQueryTraffic)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize())
        GTEST_SKIP() << "GLFW unavailable";
    Gpu gpu;
    const auto cloud = MakeCloud(gpu.H->Scene());
    auto& props = Points(gpu.H->Scene(), cloud);
    const auto input = std::as_const(props).Get<glm::vec3>("samples").Vector();
    std::shared_ptr<BF::Run> run;
    ASSERT_TRUE(gpu.Run(
        [&]
        {
            if (!run)
            {
                auto config = Settings(cloud, BF::FilterBackend::VulkanLBVH, "identity");
                config.Execution.IterationCap = 0;
                run = BF::Start(config);
            }
            return run->Final.has_value();
        }));
    ASSERT_EQ(run->Final->Status, "applied") << run->Final->Message;
    EXPECT_EQ(run->Final->CompletedIterations, 0u);
    EXPECT_EQ(run->Final->PointsFiltered, 0u);
    EXPECT_EQ(run->Final->GpuQueryBatches, 0u);
    EXPECT_EQ(run->Final->GpuQueryUploadBytes, 0u);
    EXPECT_EQ(run->Final->GpuWorkspaceUploadBytes, 0u);
    EXPECT_EQ(run->Final->GpuReadbackBytes, 0u);
    EXPECT_EQ(std::as_const(props).Get<glm::vec3>("identity").Vector(), input);
}

// ARCH-023 consumer no-frame progress (the plugin adapter, not only its session): a Vulkan run of six passes is
// started with the method queue held, so its first worker step waits on its kNN page; then graphics is held and the
// owner thread blocks (no frame, maintenance, collection or plugin callback) while the worker completes at least three
// passes (rows paged and the CPU update). The owner then pauses it (it rests ten frames without advancing), runs it
// to the cap and accepts: the output equals the six-pass batch run bit for bit (same backend and passes). Predictions:
// PassesWithoutFrames >= 3; paused steps unchanged; "held" == "batch". Mutations: the former main-thread driver job
// (0 passes while frames are held); moved points uploaded on the graphics lane (0 passes while graphics is held).
TEST(BilateralFilterPluginGpuSmoke, VulkanPassesAdvanceWithoutFramesThenPauseAndAcceptLikeTheBatchRun)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize())
        GTEST_SKIP() << "GLFW unavailable";
    Gpu gpu;
    const auto cloud = MakeCloud(gpu.H->Scene());
    auto& props = Points(gpu.H->Scene(), cloud);
    auto* steps = gpu.H->Engine.Services().Find<R::StepRunsService>();
    ASSERT_NE(steps, nullptr);
    const auto control = [&](const std::uint64_t run, const R::StepRunAction action)
    {
        std::string refusal;
        return steps->Control(run, action, 1u, refusal);
    };
    auto batchConfig = Settings(cloud, BF::FilterBackend::VulkanLBVH, "batch");
    batchConfig.Execution.IterationCap = 6u;
    auto heldConfig = Settings(cloud, BF::FilterBackend::VulkanLBVH, "held");
    heldConfig.Execution.IterationCap = 6u;
    heldConfig.Execution.AutoAccept = R::StepRunAutoAccept::Never;
    std::shared_ptr<BF::Run> batch, held;
    std::function<bool()> release;
    int stage = 0;
    bool gated = false, noLane = false;
    std::uint32_t passesWithoutFrames = 0u, pausedAt = 0u, pausedAfter = 0u, pausedFrames = 0u;
    ASSERT_TRUE(gpu.Run(
        [&]
        {
            if (!batch)
                batch = BF::Start(batchConfig);
            if (!batch->Final)
                return false;
            if (stage == 0)
            {
                release = HoldMethodQueue(gpu);
                noLane = !release;
                held = BF::Start(heldConfig);
                stage = 1;
                return held->Final.has_value() || noLane;
            }
            const auto id = held->Pending.StepRun;
            const auto o = steps->Observe(id);
            if (!o)
                return false;
            const bool resting = o->Phase == R::StepRunPhase::Paused && !o->SegmentInFlight;
            if (stage == 1 && o->SegmentInFlight)
            {
                const auto from = o->Steps;
                const auto outcome = Intrinsic::Tests::ProgressWithoutFrames(gpu.H->Engine.GetDevice(),
                    release,
                    [&]
                    {
                        const auto now = steps->Observe(id);
                        return now && (now->Steps >= from + 3u || now->End != R::StepRunEnd::None);
                    });
                gated = outcome.Gated;
                passesWithoutFrames = steps->Observe(id)->Steps - from;
                (void)control(id, R::StepRunAction::Pause); // at the next pass boundary
                stage = 2;
                return false;
            }
            if (stage == 2 && resting)
            {
                if (pausedFrames++ == 0u)
                    pausedAt = o->Steps;
                if (pausedFrames < 10u)
                    return false;
                pausedAfter = o->Steps;
                (void)control(id, R::StepRunAction::Run);
                stage = 3;
                return false;
            }
            if (stage == 3 && resting && o->End != R::StepRunEnd::None)
            {
                EXPECT_TRUE(control(id, R::StepRunAction::Accept));
                stage = 4;
            }
            return held->Final.has_value();
        }))
        << "the frames ran out";
    if (release)
        (void)release();
    if (noLane || !gated)
        GTEST_SKIP() << "No separate method queue: graphics cannot be held while method compute proceeds.";
    ASSERT_EQ(batch->Final->Status, "applied") << batch->Final->Message;
    ASSERT_EQ(held->Final->Status, "applied") << held->Final->Message;
    EXPECT_GE(passesWithoutFrames, 3u) << "passes with graphics held and no owner frame";
    EXPECT_EQ(pausedAfter, pausedAt) << "the owner's pause holds at a pass boundary";
    EXPECT_EQ(held->Final->CompletedIterations, 6u);
    EXPECT_EQ(
        std::as_const(props).Get<glm::vec3>("held").Vector(), std::as_const(props).Get<glm::vec3>("batch").Vector())
        << "the driven run equals the batch run";
}
