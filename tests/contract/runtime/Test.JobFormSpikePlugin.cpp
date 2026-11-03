// ARCH-022 S3b: the job form spike plugin (coherent point drift as a step run) in an engine composed as the Sandbox
// composes it, driven through its MCP tools and its window: the run equals the solver's own step sequence bit for bit,
// Accept writes it once with one undo entry, changed inputs or another world end it without a write, a paused run
// holds its output, and an unload ends it.
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>

#include "PluginHarness.hpp"
#include "TestImGuiFrameScope.hpp"

import Geometry.Registration.CoherentPointDrift;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Runtime.EditorUiHost;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.StepRuns;
import Intrinsic.Plugin.JobFormSpike;

namespace
{
    namespace R = Extrinsic::Runtime;
    namespace CPD = Geometry::CoherentPointDrift;
    namespace GS = Extrinsic::ECS::Components::GeometrySources;
    namespace JFS = Intrinsic::Plugins::JobFormSpike;
    using Json = nlohmann::json;

    // A helix-like cloud (no symmetry to confuse a rigid fit), and the same cloud turned 10 degrees and shifted, with
    // a small deterministic jitter (so the fit never becomes exact and a large cloud takes many steps).
    std::vector<glm::vec3> TargetPoints(const int count = 60)
    {
        std::vector<glm::vec3> points;
        for (int i = 0; i < count; ++i)
        {
            const float t = 0.21f * float(i) * 60.f / float(count), u = float(i) / float(count);
            points.emplace_back(std::cos(t) * (1.f + 1.2f * u), std::sin(t) * (1.f + 0.6f * u), 3.f * u);
        }
        return points;
    }

    std::vector<glm::vec3> SourcePoints(const int count = 60)
    {
        const float c = std::cos(0.1745f), s = std::sin(0.1745f);
        std::vector<glm::vec3> points;
        int i = 0;
        for (const auto& p : TargetPoints(count))
        {
            const float jitter = 0.01f * float((i++ * 37) % 11 - 5);
            points.emplace_back(c * p.x - s * p.y + 0.3f + jitter, s * p.x + c * p.y - 0.2f, p.z + 0.1f - jitter);
        }
        return points;
    }

    entt::entity Cloud(Extrinsic::ECS::Scene::Registry& scene, const std::vector<glm::vec3>& points)
    {
        const auto entity = scene.Create();
        auto& props = scene.Raw().emplace<GS::Vertices>(entity).Properties;
        props.Resize(points.size());
        props.GetOrAdd<glm::vec3>("samples").Vector().assign(points.begin(), points.end());
        return entity;
    }

    std::uint32_t Id(const entt::entity entity) { return R::SelectionController::ToStableEntityId(entity); }

    struct Harness : Intrinsic::Tests::Plugins::Harness
    {
        entt::entity Source{}, Target{};

        explicit Harness(const int points = 60) : Intrinsic::Tests::Plugins::Harness(&JFS::Register, &JFS::Shutdown)
        {
            Source = Cloud(Scene(), SourcePoints(points));
            Target = Cloud(Scene(), TargetPoints(points));
        }

        // Read only: a mutable access would count as a change of the property.
        std::vector<glm::vec3> Samples(const entt::entity entity)
        {
            return std::as_const(Scene().Raw()).get<GS::Vertices>(entity).Properties.Get<glm::vec3>("samples").Vector();
        }

        std::uint64_t Start(const Json& extra = Json::object())
        {
            Json args{{"source", Id(Source)}, {"target", Id(Target)}, {"positions", "samples"}, {"max_iterations", 200},
                      {"tolerance", 1e-8}, {"preview", false}};
            args.update(extra);
            const auto out = Call("job_form_spike_start", args);
            EXPECT_FALSE(out.IsError) << out.Text;
            return out.IsError ? 0u : Json::parse(out.Text)["run"].get<std::uint64_t>();
        }

        Json Control(const std::uint64_t run, const std::string& action, const int count = 1)
        {
            const auto out = Call("run_control", {{"run", run}, {"action", action}, {"count", count}});
            return out.IsError ? Json{{"error", out.ErrorCode}, {"text", out.Text}} : Json::parse(out.Text);
        }

        Json Observe(const std::uint64_t run, const bool metrics = false)
        {
            return Json::parse(Call("run_observe", {{"run", run}, {"metrics", metrics}}).Text);
        }

        // Frames until `done(observation)`; false after the frame limit.
        template <class Done>
        bool Until(const std::uint64_t run, Done done, const int frames = 3000)
        {
            for (int frame = 0; frame < frames; ++frame)
            {
                if (done(Observe(run))) return true;
                Frame();
            }
            return false;
        }

        bool Stopped(const std::uint64_t run)
        {
            return Until(run, [](const Json& o) { return !o["computing"].get<bool>() && o["phase"] != "running"; });
        }
    };

    // The solver's own run on the same points and settings.
    struct Reference
    {
        std::vector<CPD::IterationTrace> Trace{};
        CPD::Result Result{};
    };

    Reference Solve()
    {
        CPD::Solver solver;
        const auto target = TargetPoints(), source = SourcePoints();
        EXPECT_EQ(solver.Initialize(target, source, CPD::Params{.MaxIterations = 200, .Tolerance = 1e-8}), CPD::Status::Success);
        Reference reference;
        while (solver.Step([&](const CPD::IterationTrace& t) { reference.Trace.push_back(t); })) {}
        reference.Result = solver.Current();
        return reference;
    }
}

TEST(JobFormSpikePlugin, ARunEqualsTheSolversOwnStepsAndAcceptWritesItOnce)
{
    const auto reference = Solve();
    ASSERT_EQ(reference.Result.Stop, CPD::Termination::Converged);
    ASSERT_GT(reference.Trace.size(), 5u);

    Harness harness;
    const auto before = harness.Samples(harness.Source);
    const auto run = harness.Start();
    ASSERT_NE(run, 0u);
    EXPECT_EQ(harness.Observe(run)["phase"], "ready");
    ASSERT_FALSE(harness.Control(run, "step", 3).contains("error"));
    ASSERT_TRUE(harness.Stopped(run));
    EXPECT_EQ(harness.Observe(run)["steps"], 3);
    ASSERT_FALSE(harness.Control(run, "run").contains("error"));
    ASSERT_TRUE(harness.Until(run, [](const Json& o) { return o["end"] != "none" && !o["computing"].get<bool>(); }));
    const auto observed = harness.Observe(run, true);
    EXPECT_EQ(observed["end"], "converged");
    EXPECT_EQ(observed["phase"], "paused") << "it waits for Accept";
    // One step per solver call: its iterations, then the call whose E-step finds convergence (no iteration).
    ASSERT_EQ(observed["metrics"].size(), reference.Trace.size() + 1u);
    for (std::size_t i = 0; i < reference.Trace.size(); ++i)
    {
        const auto& values = observed["metrics"][i]["values"];
        EXPECT_EQ(values["sigma2"].get<double>(), reference.Trace[i].Sigma2) << i;
        EXPECT_EQ(values["objective"].get<double>(), reference.Trace[i].Objective) << i;
        EXPECT_EQ(values["matched"].get<double>(), reference.Trace[i].MatchedWeight) << i;
        EXPECT_EQ(values["nll"].get<double>(), reference.Trace[i].NegativeLogLikelihood) << i;
    }
    const auto& last = observed["metrics"].back()["values"];
    EXPECT_EQ(last["sigma2"].get<double>(), reference.Result.Sigma2);
    EXPECT_EQ(last["nll"].get<double>(), reference.Result.NegativeLogLikelihood);
    EXPECT_EQ(last["matched"].get<double>(), reference.Result.MatchedWeight);
    EXPECT_EQ(harness.Samples(harness.Source), before) << "nothing written before Accept";
    EXPECT_FALSE(harness.History().CanUndo());

    ASSERT_FALSE(harness.Control(run, "accept").contains("error"));
    ASSERT_TRUE(harness.Until(run, [](const Json& o)
                              { return o["phase"] == "committed" || o["phase"] == "failed" || o["phase"] == "discarded"; }));
    ASSERT_EQ(harness.Observe(run)["phase"], "committed") << harness.Observe(run).dump();
    const auto after = harness.Samples(harness.Source);
    ASSERT_EQ(after.size(), reference.Result.TransformedSource.size());
    for (std::size_t i = 0; i < after.size(); ++i) EXPECT_EQ(after[i], glm::vec3(reference.Result.TransformedSource[i])) << i;
    EXPECT_EQ(harness.Samples(harness.Target), TargetPoints()) << "the target stays";
    EXPECT_EQ(harness.Control(run, "accept")["error"], "refused") << "once";
    ASSERT_TRUE(harness.History().CanUndo());
    ASSERT_TRUE(harness.History().Undo().Succeeded());
    EXPECT_EQ(harness.Samples(harness.Source), before) << "one undo entry";
}

TEST(JobFormSpikePlugin, AChangedTargetOrAnotherWorldEndsTheRunWithoutAWrite)
{
    for (const bool switchWorld : {false, true})
    {
        SCOPED_TRACE(switchWorld ? "another world" : "the target changed");
        Harness harness;
        const auto before = harness.Samples(harness.Source);
        const auto run = harness.Start();
        ASSERT_FALSE(harness.Control(run, "run").contains("error"));
        ASSERT_TRUE(harness.Until(run, [](const Json& o) { return o["end"] == "converged"; }));
        auto& scene = harness.Scene();
        if (switchWorld)
        {
            auto& worlds = harness.Engine.Worlds();
            ASSERT_TRUE(worlds.RequestSetActiveWorld(worlds.CreateWorld("other")).has_value());
            (void)worlds.ApplyMaintenance(harness.Engine.Events(), harness.Engine.Jobs());
        }
        else
            scene.Raw().get<GS::Vertices>(harness.Target).Properties.Get<glm::vec3>("samples")[0].x += 1.f;
        ASSERT_TRUE(harness.Until(run, [](const Json& o) { return o["phase"] != "paused"; }));
        const auto ended = harness.Observe(run);
        EXPECT_EQ(ended["phase"], "discarded");
        EXPECT_NE(ended["diagnostic"].get<std::string>().find("stale"), std::string::npos) << ended.dump();
        EXPECT_EQ(harness.Control(run, "accept")["error"], "refused");
        EXPECT_EQ(std::as_const(scene.Raw()).get<GS::Vertices>(harness.Source).Properties.Get<glm::vec3>("samples").Vector(),
                  before);
        EXPECT_FALSE(harness.History().CanUndo());
        if (switchWorld) continue;
        for (int frame = 0; frame < 5; ++frame) harness.Frame(); // its holder job finishes
        EXPECT_NE(harness.Start(), 0u) << "the output is free again";
    }
}

TEST(JobFormSpikePlugin, AnEntityIdBeyond32BitsIsRefusedNotWrapped)
{
    Harness harness;
    const auto out = harness.Call("job_form_spike_start", {{"source", 4294967296ull + Id(harness.Source)},
                                                           {"target", Id(harness.Target)}, {"positions", "samples"}});
    EXPECT_TRUE(out.IsError);
    EXPECT_EQ(out.ErrorCode, "invalid_params") << out.Text;
}

TEST(JobFormSpikePlugin, APausedRunHoldsItsOutputUntilItEnds)
{
    Harness harness;
    const auto first = harness.Start();
    ASSERT_FALSE(harness.Control(first, "step", 2).contains("error"));
    ASSERT_TRUE(harness.Stopped(first));
    EXPECT_EQ(harness.Observe(first)["phase"], "paused");
    for (int frame = 0; frame < 10; ++frame) harness.Frame();
    const auto refused = harness.Call("job_form_spike_start", {{"source", Id(harness.Source)}, {"target", Id(harness.Target)},
                                                               {"positions", "samples"}, {"preview", false}});
    EXPECT_TRUE(refused.IsError);
    EXPECT_NE(refused.Text.find("already running"), std::string::npos) << refused.Text;
    const auto rows = harness.Jobs();
    EXPECT_TRUE(std::ranges::any_of(rows, [](const R::EditorJobRecord& row)
                                    { return row.Name == "Coherent point drift (job form spike)" &&
                                             R::IsActiveEditorJobState(row.State); }))
        << "the paused run is a row of the Jobs window";
    ASSERT_FALSE(harness.Control(first, "discard").contains("error"));
    ASSERT_TRUE(harness.Until(first, [](const Json& o) { return o["phase"] == "discarded"; }));
    for (int frame = 0; frame < 5; ++frame) harness.Frame();
    EXPECT_NE(harness.Start(), 0u) << "free after it ended";
}

TEST(JobFormSpikePlugin, AnUnloadEndsAPausedRunOnce)
{
    Harness harness;
    const auto before = harness.Samples(harness.Source);
    const auto run = harness.Start();
    ASSERT_FALSE(harness.Control(run, "step", 2).contains("error"));
    ASSERT_TRUE(harness.Stopped(run));
    const auto unload = harness.Call("plugin_unload", {{"plugin", "job_form_spike"}});
    ASSERT_FALSE(unload.IsError) << unload.Text;
    const auto ended = harness.Observe(run);
    EXPECT_EQ(ended["phase"], "discarded") << ended.dump();
    EXPECT_EQ(harness.Samples(harness.Source), before);
    EXPECT_FALSE(harness.History().CanUndo());
}

TEST(JobFormSpikePlugin, TheWindowsButtonsControlTheRunStartedLast)
{
    Harness harness{1500}; // steps of milliseconds: Pause lands while it computes
    const auto run = harness.Start({{"tolerance", 1e-14}, {"max_iterations", 5000}});
    ASSERT_EQ(JFS::LastRun(), run);
    auto* ui = harness.Engine.Services().Find<R::EditorUiHost>();
    ASSERT_NE(ui, nullptr);
    ASSERT_TRUE(ui->SetWindowOpen("view.job_form_spike", true));
    TestSupport::ImGuiFrameScope gui;
    ImGui::GetIO().DisplaySize = ImVec2(1280, 1600);
    const auto press = [&](const char* button)
    {
        for (int frame = 0; frame < 3; ++frame)
        {
            if (frame > 0 || ImGui::GetFrameCount() > 1) gui.NextFrame();
            if (auto* window = ImGui::FindWindowByName("Job Form Spike (CPD)###view.job_form_spike"); window && frame == 1)
                ImGui::ActivateItemByID(window->GetID(button));
            (void)ui->Windows().DrawOpenWindows();
            harness.Frame();
        }
    };
    press("Step");
    ASSERT_TRUE(harness.Stopped(run));
    EXPECT_EQ(harness.Observe(run)["steps"], 1);
    press("Run");
    ASSERT_TRUE(harness.Until(run, [](const Json& o) { return o["steps"].get<int>() > 1 || o["end"] != "none"; }));
    press("Pause");
    ASSERT_TRUE(harness.Stopped(run));
    const auto paused = harness.Observe(run)["steps"].get<int>();
    for (int frame = 0; frame < 10; ++frame) harness.Frame();
    EXPECT_EQ(harness.Observe(run)["steps"], paused) << "paused";
    press("Stop");
    EXPECT_EQ(harness.Observe(run)["end"], "stopped");
    press("Accept");
    ASSERT_TRUE(harness.Until(run, [](const Json& o) { return o["phase"] == "committed"; }));
    EXPECT_TRUE(harness.History().CanUndo());
    for (int frame = 0; frame < 5; ++frame) harness.Frame(); // its holder job finishes
    const auto next = harness.Start();
    ASSERT_EQ(JFS::LastRun(), next) << "the window follows the run started last";
    press("Discard");
    ASSERT_TRUE(harness.Until(next, [](const Json& o) { return o["phase"] == "discarded"; }));
}
