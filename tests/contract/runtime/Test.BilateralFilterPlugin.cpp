// ARCH-022: the bilateral filter plugin's contract, in an engine composed as the Sandbox composes it (in-process
// startup plugin, so it runs in static builds too): its section, its MCP tools, its Jobs window rows, every point
// domain on both CPU backends with one undo entry, its stale and cancelled runs, and its window's Start button.
// ARCH-023: a run is a step run (one pass a step); the batch settings below run at once and accept at their end, the
// lifecycle cases drive a ready run through run_control.
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>

#include "BilateralFilterHarness.hpp"
#include "PluginWindowHarness.hpp"
#include "TestImGuiFrameScope.hpp"
#include "PointDomainFixture.hpp"

import Extrinsic.Core.Tasks;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Runtime.EditorUiHost;
import Extrinsic.Runtime.GeometryProperty.Types;
import Extrinsic.Runtime.SelectionController;

namespace
{
    using namespace Intrinsic::Tests::BilateralFilter;
    using D = R::GeometryElementDomain;
    using Intrinsic::Tests::PointDomainProperties;

    // The shared sample grid; a point cloud also gets a deleted NaN row 4.
    entt::entity Make(Extrinsic::ECS::Scene::Registry& scene, const D domain)
    {
        const auto entity = Intrinsic::Tests::PointCloudEditing::MakeOrientedSamples(scene, domain);
        if (domain == D::PointCloudPoint)
        {
            auto& props = PointDomainProperties(scene, entity, domain);
            props.GetOrAdd<bool>("v:deleted")[4] = true;
            props.Get<glm::vec3>("samples")[4] = {std::numeric_limits<float>::quiet_NaN(), 0, 0};
        }
        return entity;
    }

    BF::Config Settings(const entt::entity entity, const D domain)
    {
        return {.StableEntityId = R::SelectionController::ToStableEntityId(entity),
            .Positions = {.Domain = domain, .Name = "samples", .ValueKind = Geometry::PropertyValueKind::Vec3},
            .Normals = {.Domain = domain, .Name = "directions", .ValueKind = Geometry::PropertyValueKind::Vec3},
            .Output = {.Domain = domain, .Name = "filtered", .ValueKind = Geometry::PropertyValueKind::Vec3},
            .KNeighbors = 2,
            .SpatialSigma = 2,
            .Execution = {.IterationCap = 3u,
                .PreviewEvery = 0u,
                .PreviewIntervalMs = 0u,
                .AutoAccept = R::StepRunAutoAccept::End,
                .Start = R::StepRunStart::Run}};
    }

    // A run_control or run_observe reply's JSON (an error's text otherwise).
    Json Control(Harness& harness, const std::uint64_t run, const std::string& action, const std::uint32_t count = 1u)
    {
        const auto reply = harness.Call("run_control", {{"run", run}, {"action", action}, {"count", count}});
        EXPECT_FALSE(reply.IsError) << action << ": " << reply.Text;
        return reply.IsError ? Json{} : Json::parse(reply.Text);
    }
    Json Observe(Harness& harness, const std::uint64_t run)
    {
        return Json::parse(harness.Call("run_observe", {{"run", run}}).Text);
    }
    // Frames until a requested restart began the run's generation `generation`.
    Json Restarted(Harness& harness, const std::uint64_t run, const std::uint64_t generation)
    {
        Json observed;
        for (int frame = 0; frame < 200; ++frame)
        {
            observed = Observe(harness, run);
            if (observed.value("generation", 0u) == generation)
                break;
            harness.Frame();
        }
        return observed;
    }
    // Frames until the run rests (paused or ended, nothing in flight).
    Json Rest(Harness& harness, const std::uint64_t run)
    {
        Json observed;
        for (int frame = 0; frame < 2000; ++frame)
        {
            observed = Observe(harness, run);
            if (observed.value("phase", "") != "running" && !observed.value("computing", true))
                break;
            harness.Frame();
        }
        return observed;
    }

    bool Usable(const std::string& payload)
    {
        return R::ValidateDerivedSection<BF::kSection>(payload, {}, "test").Usable();
    }
}

TEST(BilateralFilterPlugin, TheSectionKeepsItsNameAndRejectsInvalidSettings)
{
    EXPECT_EQ(BF::kSectionName, "sandbox.bilateral_filter"); // saved configs of the built-in filter still load
    for (const auto* payload : {R"({"backend":"vulkan"})",
             R"({"k_neighbors":-1})",
             R"({"spatial_sigma":-1})",
             R"({"gpu_query_batch_size":0})",
             R"({"gpu_query_batch_size":16385})",
             R"({"spatial_sigma":1e100})",
             R"({"unknown":1})",
             R"({"output":{"domain":"unknown","name":"x","kind":"uint32"}})",
             R"({"max_iterations":101})",
             R"({"iterations":3})",
             R"({"auto_accept":"sometimes"})",
             R"({"start":"later"})",
             R"({"normal_sigma":-1})"})
        EXPECT_FALSE(Usable(payload)) << payload;
    for (const auto* token : {"cpu_octree", "cpu_lbvh", "vulkan_lbvh", "vulkan_brute_force"})
        EXPECT_TRUE(Usable(Json{{"backend", token}}.dump())) << token;
    const auto brute = R::GetDerivedSection<BF::kSection>(
        []
        {
            auto c = HeadlessConfig();
            R::SetDerivedSection<BF::kSection>(c, BF::Config{.Backend = BF::FilterBackend::VulkanBruteForce});
            return c;
        }());
    ASSERT_TRUE(brute);
    EXPECT_EQ(brute->Backend, BF::FilterBackend::VulkanBruteForce); // the strategy round-trips
    EXPECT_STREQ(BF::ToString(BF::FilterBackend::VulkanBruteForce), "vulkan_brute_force");
    EXPECT_TRUE(Usable(R"({"max_iterations":100,"gpu_query_batch_size":16384,"start":"ready","auto_accept":"end"})"));
    BF::Config mixed{};
    mixed.Normals.Domain = D::MeshFace;
    EXPECT_FALSE(Usable(R::SerializeDerivedSection<BF::kSection>(mixed))); // inputs on two domains
    BF::Config overwrite{};
    overwrite.Output.Name = overwrite.Normals.Name;
    EXPECT_FALSE(Usable(R::SerializeDerivedSection<BF::kSection>(overwrite)));
    const auto config = R::GetDerivedSection<BF::kSection>([]
    {
        auto c = HeadlessConfig();
        R::SetDerivedSection<BF::kSection>(c,
            BF::Config{.StableEntityId = 7u,
                .Execution = {.IterationCap = 4u,
                    .PreviewEvery = 2u,
                    .PreviewIntervalMs = 5u,
                    .AutoAccept = R::StepRunAutoAccept::Converged,
                    .Start = R::StepRunStart::Ready}});
        return c;
    }());
    ASSERT_TRUE(config);
    EXPECT_EQ(config->StableEntityId, 7u);
    // The shared execution options round-trip through their bound rows (BindStepRunExecution).
    EXPECT_EQ(config->Execution,
        (R::StepRunExecution{.IterationCap = 4u,
            .PreviewEvery = 2u,
            .PreviewIntervalMs = 5u,
            .AutoAccept = R::StepRunAutoAccept::Converged,
            .Start = R::StepRunStart::Ready}));
}

TEST(BilateralFilterPlugin, TheToolAnswersWithTheLastLivePassAndTheRunIsAJobsWindowRow)
{
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = Make(scene, D::PointCloudPoint);
    auto& props = PointDomainProperties(scene, entity, D::PointCloudPoint);
    constexpr std::array<glm::vec3, 4> plane{{{0, 0, 0}, {2, 0, 0}, {0, 3, 0}, {2, 3, 0}}};
    std::ranges::copy(plane, props.Get<glm::vec3>("samples").Vector().begin());
    props.Get<glm::vec3>("directions")[0] = {}; // a zero normal: that point stays where it is
    ASSERT_NE(harness.Registry.Find("bilateral_filter"), nullptr);
    auto config = Settings(entity, D::PointCloudPoint);
    // The section's start and auto_accept say "ready, wait": the waiting tool runs and accepts anyway.
    config.Execution.Start = R::StepRunStart::Ready;
    config.Execution.AutoAccept = R::StepRunAutoAccept::Never;
    for (const auto backend : {BF::FilterBackend::CpuOctree, BF::FilterBackend::CpuLBVH})
        for (const auto iterations : {0u, 1u, 3u})
        {
            SCOPED_TRACE(BF::ToString(backend));
            SCOPED_TRACE(iterations);
            config.Backend = backend;
            config.Execution.IterationCap = iterations;
            ASSERT_TRUE(harness.Apply(config));
            const auto reply = harness.Call("bilateral_filter");
            ASSERT_FALSE(reply.IsError) << reply.ErrorCode << ": " << reply.Text;
            const auto result = Json::parse(reply.Text);
            EXPECT_EQ(result["status"], "applied");
            EXPECT_EQ(result["actual_backend"], BF::ToString(backend));
            EXPECT_EQ(result["slot_count"], 5u);
            EXPECT_EQ(result["live_count"], 4u);
            EXPECT_EQ(result["completed_iterations"], iterations);
            EXPECT_GT(result["run"].get<std::uint64_t>(), 0u);
            EXPECT_EQ(result["points_filtered"], iterations == 0u ? 0u : 3u);
            EXPECT_EQ(result["degenerate_normals"], iterations == 0u ? 0u : 1u);
            EXPECT_EQ(result["average_displacement"], 0.0); // a plane with normals along z does not move
            EXPECT_EQ(result["max_displacement"], 0.0);
        }
    const auto jobs = harness.Jobs();
    EXPECT_TRUE(std::ranges::any_of(jobs, [](const auto& job) { return job.Name == "Bilateral point filter"; }))
        << "plugin runs are rows of the shell's Jobs window";
    EXPECT_EQ(harness.Settled(config), ""); // the tool answers what the window shows once validation settled
    const auto ready = Json::parse(harness.Call("bilateral_filter_preview").Text);
    EXPECT_EQ(ready["ready"], true) << ready.dump();
}

TEST(BilateralFilterPlugin, EveryDomainFiltersLiveRowsWithOneUndoEntryAndTheBackendsAgree)
{
    for (unsigned d = 1; d <= 8; ++d)
        for (const bool inPlace : {false, true})
        {
            SCOPED_TRACE(d);
            SCOPED_TRACE(inPlace);
            Harness harness{};
            auto& scene = harness.Scene();
            const auto entity = Make(scene, D(d));
            auto& props = PointDomainProperties(scene, entity, D(d));
            const auto size = props.Size();
            const bool half = D(d) == D::MeshHalfedge || D(d) == D::GraphHalfedge;
            if (half)
                scene.Raw().get<Extrinsic::ECS::Components::GeometrySources::Edges>(entity)
                    .Properties.GetOrAdd<bool>("e:deleted")[1] = true;
            else if (D(d) != D::PointCloudPoint)
                props.GetOrAdd<bool>(D(d) == D::MeshFace ? "f:deleted"
                                     : (D(d) == D::MeshEdge || D(d) == D::GraphEdge) ? "e:deleted"
                                                                                     : "v:deleted")[2] = true;
            const auto original = std::as_const(props).Get<glm::vec3>("samples").Vector();
            props.GetOrAdd<glm::vec3>("filtered").Vector().assign(size, glm::vec3(77));
            auto config = Settings(entity, D(d));
            if (inPlace) config.Output = config.Positions;

            const auto octree = harness.Wait(*BF::Start(config));
            ASSERT_EQ(octree.Status, "applied") << octree.Message;
            EXPECT_EQ(octree.CompletedIterations, 3u);
            const auto values = std::as_const(props).Get<glm::vec3>(config.Output.Name).Vector();
            ASSERT_EQ(values.size(), size);
            if (D(d) != D::PointCloudPoint) // a deleted row keeps what it had (row 2 is deleted in every domain)
                EXPECT_EQ(values[2], inPlace ? original[2] : glm::vec3(77));
            if (!inPlace) EXPECT_NE(values[0], glm::vec3(77)) << "a live row was written";
            props.Get<float>("keep")[0] = 99; // an unrelated edit does not make the entry stale
            ASSERT_TRUE(harness.History().Undo().Succeeded());
            EXPECT_EQ(std::as_const(props).Get<glm::vec3>(config.Output.Name)[0], inPlace ? original[0] : glm::vec3(77));
            ASSERT_TRUE(harness.History().Redo().Succeeded());
            EXPECT_EQ(std::as_const(props).Get<float>("keep")[0], 99);
            ASSERT_TRUE(harness.History().Undo().Succeeded());

            config.Backend = BF::FilterBackend::CpuLBVH;
            for (int frame = 0; frame < 20; ++frame) harness.Frame(); // the spatial cache builds its index
            const auto lbvh = harness.Wait(*BF::Start(config));
            ASSERT_EQ(lbvh.Status, "applied") << lbvh.Message;
            EXPECT_EQ(lbvh.ActualBackend, "cpu_lbvh");
            EXPECT_EQ(lbvh.WorkspaceBuilds, 2u) << "the cached index serves the first pass, rebuilt ones the others";
            const auto actual = std::as_const(props).Get<glm::vec3>(config.Output.Name).Vector();
            for (std::size_t i = 0; i < size; ++i)
                if (i != 4 || D(d) != D::PointCloudPoint) EXPECT_LE(glm::length(actual[i] - values[i]), 1e-5) << i;
            EXPECT_FLOAT_EQ(lbvh.AverageDisplacement, octree.AverageDisplacement);
            EXPECT_FLOAT_EQ(lbvh.MaxDisplacement, octree.MaxDisplacement);
            EXPECT_EQ(lbvh.PointsFiltered, octree.PointsFiltered);
            EXPECT_EQ(lbvh.DegenerateNormals, octree.DegenerateNormals);
            props.Get<glm::vec3>("directions")[0].x = 1; // an input changed: the entry is stale
            EXPECT_EQ(harness.History().Undo().Status, R::EditorCommandHistoryStatus::StaleEntity);
        }
}

TEST(BilateralFilterPlugin, AChangedInputOrOutputOrACancelEndsTheRunWithoutAWrite)
{
    // 0 unrelated edit (applies), 1 positions, 2 a deletion, 3 the output written, 4 cancel, 5 normals.
    const char* expected[]{"applied", "stale", "stale", "stale", "cancelled", "stale"};
    for (unsigned change = 0; change < 6; ++change)
    {
        SCOPED_TRACE(change);
        Harness harness{};
        auto& scene = harness.Scene();
        const auto entity = Make(scene, D::MeshVertex);
        auto& props = PointDomainProperties(scene, entity, D::MeshVertex);
        const auto run = BF::Start(Settings(entity, D::MeshVertex));
        ASSERT_FALSE(run->Final) << run->Final->Message;
        EXPECT_FALSE(props.Exists("filtered"));
        switch (change)
        {
        case 0: props.Get<float>("keep")[0] = 99; break;
        case 1: props.Get<glm::vec3>("samples")[0].x += 1; break;
        case 2: props.GetOrAdd<bool>("v:deleted")[0] = true; break;
        case 3: props.GetOrAdd<glm::vec3>("filtered")[0] = glm::vec3(77); break;
        case 4: EXPECT_TRUE(R::CancelPluginRun(*run)); break;
        case 5: props.Get<glm::vec3>("directions")[0].x = 1; break;
        }
        const auto result = harness.Wait(*run);
        EXPECT_EQ(result.Status, expected[change]) << result.Message;
        EXPECT_EQ(harness.History().CanUndo(), change == 0);
        EXPECT_EQ(props.Exists("filtered"), change == 0 || change == 3);
        if (change == 3) EXPECT_EQ(std::as_const(props).Get<glm::vec3>("filtered")[0], glm::vec3(77));
    }
}

TEST(BilateralFilterPlugin, RunsOnDifferentOutputsAreConcurrentAndTheSameOutputIsRefused)
{
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = Make(scene, D::MeshVertex);
    auto& props = PointDomainProperties(scene, entity, D::MeshVertex);
    auto first = Settings(entity, D::MeshVertex), second = first;
    second.Output.Name = "filtered_too";
    const auto a = BF::Start(first), b = BF::Start(second), same = BF::Start(first);
    ASSERT_FALSE(a->Final);
    ASSERT_FALSE(b->Final) << b->Final->Message;
    ASSERT_TRUE(same->Final);
    EXPECT_EQ(same->Final->Status, "failed");
    EXPECT_EQ(same->Final->StepRun, 0u) << "a refused start names no run";
    EXPECT_NE(same->Final->Message.find("already running"), std::string::npos) << same->Final->Message;
    EXPECT_EQ(harness.Wait(*a).Status, "applied");
    EXPECT_EQ(harness.Wait(*b).Status, "applied");
    EXPECT_TRUE(props.Exists("filtered"));
    EXPECT_TRUE(props.Exists("filtered_too"));
}

TEST(BilateralFilterPlugin, SuccessiveInPlaceRunsUndoInTurn)
{
    // Both entries guard the positions through the history slot they share: undoing the second stamps it, so the
    // first still applies (the guard a GPU position accept shares, too).
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = Make(scene, D::MeshVertex);
    auto& props = PointDomainProperties(scene, entity, D::MeshVertex);
    const auto original = std::as_const(props).Get<glm::vec3>("samples").Vector();
    auto config = Settings(entity, D::MeshVertex);
    config.Output = config.Positions;
    config.Execution.IterationCap = 1;
    ASSERT_EQ(harness.Wait(*BF::Start(config)).Status, "applied");
    const auto once = std::as_const(props).Get<glm::vec3>("samples").Vector();
    ASSERT_NE(once, original);
    ASSERT_EQ(harness.Wait(*BF::Start(config)).Status, "applied");
    ASSERT_TRUE(harness.History().Undo().Succeeded());
    EXPECT_EQ(std::as_const(props).Get<glm::vec3>("samples").Vector(), once);
    ASSERT_TRUE(harness.History().Undo().Succeeded()) << "the first run's entry is not stale";
    EXPECT_EQ(std::as_const(props).Get<glm::vec3>("samples").Vector(), original);
    ASSERT_TRUE(harness.History().Redo().Succeeded());
    ASSERT_TRUE(harness.History().Redo().Succeeded());
}

TEST(BilateralFilterPlugin, TheJobsWindowCancelsARun)
{
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = Make(scene, D::MeshVertex);
    auto& props = PointDomainProperties(scene, entity, D::MeshVertex);
    const auto run = BF::Start(Settings(entity, D::MeshVertex));
    ASSERT_FALSE(run->Final);
    const auto jobs = harness.Jobs();
    const auto row = std::ranges::find_if(jobs, [](const auto& job) { return job.Name == "Bilateral point filter"; });
    ASSERT_NE(row, jobs.end());
    // As the Jobs window's Cancel button does, on the shell's session.
    const auto commands = R::PrepareEditorProcessingCommands(harness.Shell);
    ASSERT_TRUE(R::ResolveEditorJobCancelReadiness(commands, *row).Enabled);
    EXPECT_EQ(R::CancelEditorJobRun(commands, *row), R::EditorJobCancelStatus::Requested);
    EXPECT_EQ(harness.Wait(*run).Status, "cancelled") << run->Final->Message;
    EXPECT_FALSE(props.Exists("filtered"));
    EXPECT_FALSE(harness.History().CanUndo());
}

TEST(BilateralFilterPlugin, PreviewChecksTheOutputAndAVulkanRequestHasNoCpuSubstitute)
{
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = Make(scene, D::MeshVertex);
    auto config = Settings(entity, D::MeshVertex);
    auto refused = config;
    refused.Output = refused.Normals;
    EXPECT_FALSE(BF::Preview(refused).empty());
    refused.Output = refused.Positions;
    refused.Output.Name = "v:deleted";
    EXPECT_NE(BF::Preview(refused).find("topology"), std::string::npos) << BF::Preview(refused);
    refused = config;
    refused.Backend = BF::FilterBackend::VulkanLBVH; // no GPU here: refused before anything runs
    EXPECT_NE(harness.Settled(refused).find("Vulkan"), std::string::npos) << harness.Settled(refused);
    const auto run = BF::Start(refused);
    ASSERT_TRUE(run->Final);
    EXPECT_EQ(run->Final->Status, "invalid");
    refused.Backend = BF::FilterBackend::VulkanBruteForce; // the same Vulkan admission, no CPU substitute
    const auto brute = BF::Start(refused);
    ASSERT_TRUE(brute->Final);
    EXPECT_EQ(brute->Final->Status, "invalid");
    EXPECT_EQ(brute->Final->RequestedBackend, BF::FilterBackend::VulkanBruteForce);
    EXPECT_NE(brute->Final->Message.find("Vulkan"), std::string::npos) << brute->Final->Message;
    EXPECT_FALSE(harness.History().CanUndo());
}

// ARCH-023: a ready run computes nothing; Step(n) runs exactly n passes; Stop keeps an acceptable partial result;
// Restart (also after the cap) begins a new generation from the captured positions; one Accept writes once. Every
// pass sequence equals the batch run of as many passes.
TEST(BilateralFilterPlugin, AStepRunStepsStopsRestartsAndAcceptsLikeTheBatchRun)
{
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = Make(scene, D::MeshVertex);
    auto& props = PointDomainProperties(scene, entity, D::MeshVertex);
    auto batch = Settings(entity, D::MeshVertex);
    std::array<std::vector<glm::vec3>, 4> expected{}; // the batch outputs of 1, 2 and 3 passes
    for (std::uint32_t passes = 1; passes <= 3; ++passes)
    {
        batch.Execution.IterationCap = passes;
        batch.Output.Name = "batch" + std::to_string(passes);
        ASSERT_EQ(harness.Wait(*BF::Start(batch)).Status, "applied");
        expected[passes] = std::as_const(props).Get<glm::vec3>(batch.Output.Name).Vector();
    }
    ASSERT_NE(expected[1], expected[3]) << "the passes move the points (not a no-op)";
    const auto undos = harness.History().UndoCount();

    auto config = Settings(entity, D::MeshVertex);
    config.Execution.AutoAccept = R::StepRunAutoAccept::Never;
    config.Execution.Start = R::StepRunStart::Ready;
    ASSERT_TRUE(harness.Apply(config));
    const auto started = harness.Call("bilateral_filter_start", {{"max_iterations", 3}, {"preview_buffers", 2}});
    ASSERT_FALSE(started.IsError) << started.Text;
    const auto run = Json::parse(started.Text)["run"].get<std::uint64_t>();
    ASSERT_NE(run, 0u);
    for (int frame = 0; frame < 20; ++frame)
        harness.Frame();
    auto observed = Observe(harness, run);
    EXPECT_EQ(observed["phase"], "ready") << observed.dump();
    EXPECT_EQ(observed["steps"], 0u) << "a ready run computes nothing";
    EXPECT_TRUE(observed["can_restart"].get<bool>());
    EXPECT_EQ(observed["execution"]["max_iterations"], 3u);
    EXPECT_EQ(observed["execution"]["preview_buffers"], 2u);
    EXPECT_EQ(observed["device_preview_buffers"], 0u) << "live preview is off";
    EXPECT_FALSE(props.Exists("filtered"));

    (void)Control(harness, run, "step", 2u);
    observed = Rest(harness, run);
    EXPECT_EQ(observed["phase"], "paused");
    EXPECT_EQ(observed["steps"], 2u) << "step 2 ran exactly two passes";
    (void)Control(harness, run, "stop");
    observed = Rest(harness, run);
    EXPECT_EQ(observed["end"], "stopped");
    EXPECT_TRUE(harness.Call("run_control", {{"run", run}, {"action", "run"}}).IsError) << "a stopped run ended";
    (void)Control(harness, run, "restart");
    observed = Restarted(harness, run, 1u);
    EXPECT_EQ(observed["generation"], 1u);
    EXPECT_EQ(observed["steps"], 0u);
    EXPECT_EQ(observed["phase"], "ready") << "a ready run restarts ready";
    (void)Control(harness, run, "run");
    observed = Rest(harness, run);
    EXPECT_EQ(observed["end"], "iteration_cap");
    EXPECT_EQ(observed["steps"], 3u);
    (void)Control(harness, run, "restart"); // after its end, before Accept
    observed = Restarted(harness, run, 2u);
    EXPECT_EQ(observed["generation"], 2u);
    (void)Control(harness, run, "step", 1u);
    observed = Rest(harness, run);
    EXPECT_EQ(observed["steps"], 1u);
    EXPECT_FALSE(props.Exists("filtered")) << "nothing is published before Accept";
    EXPECT_EQ(harness.History().UndoCount(), undos);
    (void)Control(harness, run, "accept");
    for (int frame = 0; frame < 50 && Observe(harness, run)["phase"] != "committed"; ++frame)
        harness.Frame();
    EXPECT_EQ(Observe(harness, run)["phase"], "committed");
    ASSERT_TRUE(BF::LastRun() && BF::LastRun()->Final);
    EXPECT_EQ(BF::LastRun()->Final->Status, "applied");
    EXPECT_EQ(BF::LastRun()->Final->CompletedIterations, 1u);
    EXPECT_EQ(BF::LastRun()->Final->StepRun, run);
    EXPECT_EQ(std::as_const(props).Get<glm::vec3>("filtered").Vector(), expected[1])
        << "a restarted run's one pass equals the one-pass batch run";
    EXPECT_EQ(harness.History().UndoCount(), undos + 1u) << "one Accept, one undo entry";
    const auto again = harness.Call("run_control", {{"run", run}, {"action", "accept"}});
    EXPECT_TRUE(again.IsError) << "a committed run refuses a second Accept";

    // Two passes stepped one at a time, stopped and accepted: the two-pass batch output.
    config.Output.Name = "stepped";
    ASSERT_TRUE(harness.Apply(config));
    const auto second = Json::parse(harness.Call("bilateral_filter_start").Text)["run"].get<std::uint64_t>();
    (void)Control(harness, second, "step", 1u);
    (void)Rest(harness, second);
    (void)Control(harness, second, "step", 1u);
    (void)Rest(harness, second);
    (void)Control(harness, second, "stop");
    (void)Rest(harness, second);
    (void)Control(harness, second, "accept");
    for (int frame = 0; frame < 50 && Observe(harness, second)["phase"] != "committed"; ++frame)
        harness.Frame();
    EXPECT_EQ(std::as_const(props).Get<glm::vec3>("stepped").Vector(), expected[2])
        << "a stopped run's partial result equals the batch run of its passes";
}

// ARCH-023: Discard publishes nothing; live preview off and on give the same result (an in-place output, where
// preview applies: without an operational device no copy is shown, and none is made with preview_every 0).
// ARCH-023: a cancel while the step is held answers at the run's settled end (StepRunCallbacks::Finished), not from
// Discard: no result while the step is in flight and the output still held, and once the result arrives a run on the
// same output is admitted at once. Removing Finished (answering from Discard) fails the early-result check.
TEST(BilateralFilterPlugin, ACancelWithItsStepHeldAnswersAtTheSettledEndWithTheOutputFree)
{
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = Make(scene, D::MeshVertex);
    auto& props = PointDomainProperties(scene, entity, D::MeshVertex);
    auto config = Settings(entity, D::MeshVertex);
    config.Execution.Start = R::StepRunStart::Ready;
    const auto run = BF::Start(config);
    ASSERT_FALSE(run->Final) << run->Final->Message;
    ASSERT_TRUE(static_cast<Intrinsic::Tests::PointCloudEditing::Harness&>(harness).Settled(
        [&]
        {
            return harness.Engine.Jobs().GetState(run->Handle.Token) == R::JobState::AwaitingApply;
        }));
    config.Execution.Start = R::StepRunStart::Run;
    // The harness's one worker is held, so the run's step stays in flight (a main-thread frame never runs it).
    std::atomic_bool held{false}, release{false};
    struct Release
    {
        std::atomic_bool& Flag;
        ~Release()
        {
            Flag.store(true, std::memory_order_release);
        }
    } const releaseAtExit{release}; // before the harness's own shutdown waits for the worker
    Extrinsic::Core::Tasks::Scheduler::Dispatch(
        [&]
        {
            held.store(true, std::memory_order_release);
            while (!release.load(std::memory_order_acquire))
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        });
    for (int wait = 0; wait < 5000 && !held.load(std::memory_order_acquire); ++wait)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    ASSERT_TRUE(held.load(std::memory_order_acquire));

    (void)Control(harness, run->Pending.StepRun, "run");
    harness.Frame(2);
    ASSERT_TRUE(Observe(harness, run->Pending.StepRun)["computing"].get<bool>()) << "the step is in flight";
    EXPECT_TRUE(R::CancelPluginRun(*run));
    harness.Frame(5);
    EXPECT_FALSE(run->Final) << "the step is still held: Discard does not answer";

    release.store(true, std::memory_order_release);
    EXPECT_EQ(harness.Wait(*run).Status, "cancelled") << run->Final->Message;
    const auto next = BF::Start(config); // in the frame the result arrived: the output is free
    ASSERT_FALSE(next->Final) << next->Final->Message;
    EXPECT_EQ(harness.Wait(*next).Status, "applied");
    EXPECT_TRUE(props.Exists("filtered"));
}

TEST(BilateralFilterPlugin, DiscardWritesNothingAndPreviewDoesNotChangeTheResult)
{
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = Make(scene, D::MeshVertex);
    auto& props = PointDomainProperties(scene, entity, D::MeshVertex);
    const auto original = std::as_const(props).Get<glm::vec3>("samples").Vector();
    auto config = Settings(entity, D::MeshVertex);
    config.Output = config.Positions;
    config.Execution.AutoAccept = R::StepRunAutoAccept::Never;
    ASSERT_TRUE(harness.Apply(config));
    const auto discarded = Json::parse(harness.Call("bilateral_filter_start").Text)["run"].get<std::uint64_t>();
    (void)Rest(harness, discarded);
    (void)Control(harness, discarded, "discard");
    for (int frame = 0; frame < 20; ++frame)
        harness.Frame();
    EXPECT_EQ(Observe(harness, discarded)["phase"], "discarded");
    ASSERT_TRUE(BF::LastRun() && BF::LastRun()->Final);
    EXPECT_EQ(BF::LastRun()->Final->Status, "discarded");
    EXPECT_EQ(std::as_const(props).Get<glm::vec3>("samples").Vector(), original);
    EXPECT_FALSE(harness.History().CanUndo());

    std::array<std::vector<glm::vec3>, 2> results{};
    std::array<std::uint64_t, 2> copies{};
    for (const std::uint32_t every : {0u, 1u})
    {
        config.Execution.PreviewEvery = every;
        config.Execution.AutoAccept = R::StepRunAutoAccept::End;
        ASSERT_TRUE(harness.Apply(config));
        const auto run = Json::parse(harness.Call("bilateral_filter_start").Text)["run"].get<std::uint64_t>();
        for (int frame = 0; frame < 2000 && Observe(harness, run)["phase"] != "committed"; ++frame)
            harness.Frame();
        copies[every] = Observe(harness, run)["preview_copies"].get<std::uint64_t>();
        results[every] = std::as_const(props).Get<glm::vec3>("samples").Vector();
        ASSERT_TRUE(harness.History().Undo().Succeeded());
    }
    EXPECT_EQ(results[0], results[1]);
    EXPECT_EQ(copies[0], 0u) << "preview off makes no copy";
    EXPECT_GT(copies[1], 0u) << "preview on copies the moving points";
}

// ARCH-023: an unload ends a live run (cancelled, nothing written) and the host drops its callables.
TEST(BilateralFilterPlugin, AnUnloadEndsALiveRunAndDropsItsCallables)
{
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = Make(scene, D::MeshVertex);
    auto& props = PointDomainProperties(scene, entity, D::MeshVertex);
    auto config = Settings(entity, D::MeshVertex);
    config.Execution.Start = R::StepRunStart::Ready;
    ASSERT_TRUE(harness.Apply(config));
    const auto started = BF::Start(config);
    ASSERT_FALSE(started->Final);
    const auto run = started->Pending.StepRun;
    EXPECT_TRUE(Observe(harness, run)["can_restart"].get<bool>());
    const auto unloaded = harness.Call("plugin_unload", {{"plugin", "bilateral_filter"}});
    ASSERT_FALSE(unloaded.IsError) << unloaded.Text;
    const auto observed = Observe(harness, run);
    EXPECT_EQ(observed["phase"], "discarded") << observed.dump();
    EXPECT_FALSE(observed["can_restart"].get<bool>()) << "the run retired: the plugin's callables are gone";
    ASSERT_TRUE(started->Final);
    EXPECT_EQ(started->Final->Status, "cancelled");
    EXPECT_TRUE(harness.Call("run_control", {{"run", run}, {"action", "run"}}).IsError);
    EXPECT_FALSE(props.Exists("filtered"));
}

// The shared brute-force work budget is checked before the device: 1024 live points in one page with k=14
// (15 retained slots) cost exactly 2^24 and pass to the Vulkan availability refusal; k=15 exceeds it.
TEST(BilateralFilterPlugin, BruteForceAdmissionUsesTheSharedWorkBudget)
{
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = scene.Create();
    auto& props = scene.Raw().emplace<Extrinsic::ECS::Components::GeometrySources::Vertices>(entity).Properties;
    props.Resize(1024);
    auto samples = props.GetOrAdd<glm::vec3>("samples");
    auto directions = props.GetOrAdd<glm::vec3>("directions");
    for (std::size_t i = 0; i < 1024; ++i)
    {
        samples[i] = {float(i % 32), float(i / 32), 0};
        directions[i] = {0, 0, 1};
    }
    auto config = Settings(entity, D::PointCloudPoint);
    config.Backend = BF::FilterBackend::VulkanBruteForce;
    config.GpuQueryBatchSize = 1024;
    config.KNeighbors = 15;
    auto refused = BF::Start(config);
    ASSERT_TRUE(refused->Final);
    EXPECT_EQ(refused->Final->Status, "invalid");
    EXPECT_NE(refused->Final->Message.find("2^24"), std::string::npos) << refused->Final->Message;
    config.KNeighbors = 14;
    auto admitted = BF::Start(config);
    ASSERT_TRUE(admitted->Final);
    EXPECT_EQ(admitted->Final->Message.find("2^24"), std::string::npos) << admitted->Final->Message;
    EXPECT_NE(admitted->Final->Message.find("Vulkan bilateral"), std::string::npos) << admitted->Final->Message;
    config.KNeighbors = 15;
    config.GpuQueryBatchSize = 960; // a smaller page: 960 x 1024 x 17 < 2^24
    EXPECT_EQ(BF::Start(config)->Final->Message.find("2^24"), std::string::npos);
    config.Backend = BF::FilterBackend::VulkanLBVH; // LBVH limits are unchanged
    config.GpuQueryBatchSize = 1024;
    EXPECT_EQ(BF::Start(config)->Final->Message.find("2^24"), std::string::npos);
}

TEST(BilateralFilterPlugin, TheWindowRunsTheAppliedSettings)
{
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = Make(scene, D::MeshVertex);
    auto& props = PointDomainProperties(scene, entity, D::MeshVertex);
    ASSERT_TRUE(harness.Apply(Settings(entity, D::MeshVertex)));
    auto* ui = harness.Engine.Services().Find<R::EditorUiHost>();
    ASSERT_NE(ui, nullptr);
    ASSERT_TRUE(ui->SetWindowOpen("view.bilateral_filter", true));

    IMGUI_CHECKVERSION();
    ImGuiContext* context = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280, 900);
    io.IniFilename = nullptr;
    unsigned char* pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    auto second = Settings(entity, D::MeshVertex);
    second.Output.Name = "filtered_too";
    ASSERT_EQ(harness.Settled(second), ""); // input validation answered, as after a few frames in the Sandbox
    for (int frame = 0; frame < 2000 && !(props.Exists("filtered") && props.Exists("filtered_too")); ++frame)
    {
        io.DeltaTime = 1.f / 60.f;
        ImGui::NewFrame();
        // Start, apply settings for another output while the first run is still pending (no completion is applied
        // before frame 10) and start again: Start stays enabled.
        if (frame == 6) ASSERT_TRUE(harness.Apply(second)); // an activation lands the frame after it is requested
        if (frame == 2 || frame == 8)
            if (auto* window = ImGui::FindWindowByName("Bilateral Point Filter###view.bilateral_filter"))
                ImGui::ActivateItemByID(window->GetID("Start"));
        (void)ui->Windows().DrawOpenWindows();
        ImGui::Render();
        if (frame < 10) harness.Engine.Commands().Drain(harness.Scene());
        else harness.Frame();
    }
    ImGui::DestroyContext(context);
    EXPECT_TRUE(props.Exists("filtered")) << "the Start button started a run that published at its end";
    EXPECT_TRUE(props.Exists("filtered_too")) << "a second Start with other applied settings";
    EXPECT_EQ(harness.History().UndoCount(), 2u);
}

TEST(BilateralFilterPlugin, TheWindowFollowsSettingsAppliedElsewhere)
{
    Harness harness{};
    const auto entity = Make(harness.Scene(), D::MeshVertex);
    auto first = Settings(entity, D::MeshVertex);
    first.Output.Name = "window_first";
    auto elsewhere = first;
    elsewhere.Output.Name = "window_elsewhere";
    const auto active = Intrinsic::Tests::Plugins::ApplyElsewhereThenPressApply<BF::kSection>(
        harness, "view.bilateral_filter", "Bilateral Point Filter###view.bilateral_filter", first, elsewhere);
    ASSERT_TRUE(active);
    EXPECT_EQ(active->Output.Name, "window_elsewhere") << "the window applied a stale draft";
}

TEST(BilateralFilterPlugin, AWindowDraftFollowsTheSectionThroughAMissingOne)
{
    BF::Config section{};
    section.Output.Name = "followed";
    std::optional<BF::Config> draft;
    std::string seen;
    EXPECT_TRUE(R::FollowPluginSection<BF::kSection>(section, draft, seen));
    ASSERT_TRUE(draft);
    draft->Output.Name = "edited"; // the window's edit
    EXPECT_FALSE(R::FollowPluginSection<BF::kSection>(section, draft, seen)) << "an unchanged section keeps the edit";
    EXPECT_EQ(draft->Output.Name, "edited");
    EXPECT_TRUE(R::FollowPluginSection<BF::kSection>(std::nullopt, draft, seen));
    EXPECT_FALSE(draft) << "a missing section leaves nothing stale to show or apply";
    EXPECT_TRUE(R::FollowPluginSection<BF::kSection>(section, draft, seen)) << "the same section again is taken again";
    ASSERT_TRUE(draft);
    EXPECT_EQ(draft->Output.Name, "followed");
}

TEST(BilateralFilterPlugin, ZeroPassesCopyThePositionsAndOneUndoRemovesTheOutput)
{
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = Make(scene, D::MeshVertex);
    auto& props = PointDomainProperties(scene, entity, D::MeshVertex);
    const auto input = std::as_const(props).Get<glm::vec3>("samples").Vector();
    auto config = Settings(entity, D::MeshVertex);
    config.Execution.IterationCap = 0;
    ASSERT_TRUE(harness.Apply(config));
    const auto result = harness.Wait(*BF::Start(config));
    ASSERT_EQ(result.Status, "applied") << result.Message;
    EXPECT_EQ(result.CompletedIterations, 0u);
    EXPECT_EQ(result.PointsFiltered, 0u);
    EXPECT_EQ(std::as_const(props).Get<glm::vec3>("filtered").Vector(), input);
    const auto observed = Observe(harness, result.StepRun);
    EXPECT_EQ(observed["steps"], 0u);
    EXPECT_EQ(observed["end"], "iteration_cap");
    ASSERT_TRUE(harness.History().Undo().Succeeded());
    EXPECT_FALSE(props.Exists("filtered"));
    EXPECT_FALSE(harness.History().CanUndo());
}

TEST(BilateralFilterPlugin, ZeroPassStartUsesTheCommonControlsAndStillChecksTheRequestedBackend)
{
    Harness harness{};
    const auto entity = Make(harness.Scene(), D::MeshVertex);
    auto config = Settings(entity, D::MeshVertex);
    config.Execution.IterationCap = 0;
    ASSERT_TRUE(harness.Apply(config));
    const auto started =
        harness.Call("bilateral_filter_start", {{"max_iterations", 0}, {"start", "ready"}, {"auto_accept", "never"}});
    ASSERT_FALSE(started.IsError) << started.Text;
    const auto id = Json::parse(started.Text).at("run").get<std::uint64_t>();
    EXPECT_EQ(Observe(harness, id)["phase"], "ready");
    EXPECT_EQ(Control(harness, id, "run")["end"], "iteration_cap");
    (void)Control(harness, id, "accept");
    for (int frame = 0; frame < 5000 && Observe(harness, id)["phase"] != "committed"; ++frame)
        harness.Frame();
    ASSERT_EQ(Observe(harness, id)["phase"], "committed");
    for (const auto backend : {BF::FilterBackend::VulkanLBVH, BF::FilterBackend::VulkanBruteForce})
    {
        config.Backend = backend;
        const auto run = BF::Start(config);
        ASSERT_TRUE(run->Final);
        EXPECT_EQ(run->Final->Status, "invalid");
        EXPECT_NE(run->Final->Message.find("Vulkan"), std::string::npos);
    }
}

TEST(BilateralFilterPlugin, TheWindowCanApplyAZeroIterationCap)
{
    Harness harness{};
    const auto entity = Make(harness.Scene(), D::MeshVertex);
    ASSERT_TRUE(harness.Apply(Settings(entity, D::MeshVertex)));
    auto* ui = harness.Engine.Services().Find<R::EditorUiHost>();
    ASSERT_NE(ui, nullptr);
    ASSERT_TRUE(ui->SetWindowOpen("view.bilateral_filter", true));
    TestSupport::ImGuiFrameScope gui;
    ImGui::GetIO().DisplaySize = ImVec2(1280, 1600);
    for (int frame = 0; frame < 16; ++frame)
    {
        if (frame)
            gui.NextFrame();
        if (auto* window = ImGui::FindWindowByName("Bilateral Point Filter###view.bilateral_filter"))
        {
            const auto field = window->GetID("Max iterations##Bilateral");
            if (frame == 3)
            {
                ImGui::FocusWindow(window);
                ImGui::ActivateItemByID(field);
                ImGui::GetCurrentContext()->NavNextActivateFlags = ImGuiActivateFlags_PreferInput;
            }
            if (frame == 5)
            {
                EXPECT_EQ(ImGui::GetActiveID(), field);
                ImGui::GetIO().AddInputCharactersUTF8("0");
            }
            if (frame == 7)
                ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, true);
            if (frame == 8)
                ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, false);
            if (frame == 11)
                ImGui::ActivateItemByID(window->GetID("Apply settings"));
        }
        (void)ui->Windows().DrawOpenWindows();
        harness.Frame();
    }
    const auto active = R::ActivePluginSection<BF::kSection>();
    ASSERT_TRUE(active);
    EXPECT_EQ(active->Execution.IterationCap, 0u);
}
