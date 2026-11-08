// ARCH-023: the executable method template (methods/_template) on the shared run driver and the point-cloud editing
// service: a ready start computes nothing, Step 1 and Run step the same math, Restart begins again from the same
// capture, Accept publishes once as one undo entry, a stale capture discards the run, the brute-force and the cached
// LBVH query give the same rows, a requested live preview is normalized off (no copy, the same result), and the rows and
// the initial state are built by the first worker step of each generation. ARCH-023: its one-shot
// method runs the common lifecycle on its CPU counterpart, and its GPU path answers the existing producer's admission.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include "OneShotLifecycleScript.hpp"
#include "PointCloudEditingHarness.hpp"

import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.PointCloudEditing;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.StepRuns;
import Geometry.PointLBVH;
import Geometry.Properties;

#include "TemplateMethod.hpp"

namespace
{
    namespace R = Extrinsic::Runtime;
    namespace T = Intrinsic::Methods::Template;
    namespace GS = Extrinsic::ECS::Components::GeometrySources;
    using D = R::GeometryElementDomain;
    using Harness = Intrinsic::Tests::PointCloudEditing::Harness;

    // Six live samples on two rows, heights 0.1 i (the template's state starts as the height).
    std::vector<glm::vec3> Samples()
    {
        std::vector<glm::vec3> points;
        for (unsigned i = 0; i < 6u; ++i)
            points.push_back({float(i % 3u), float(i / 3u), 0.1f * float(i)});
        return points;
    }

    entt::entity MakeCloud(Harness& h)
    {
        auto& scene = h.Scene();
        const auto entity = scene.Create();
        auto& props = scene.Raw().emplace<GS::Vertices>(entity).Properties;
        const auto points = Samples();
        props.Resize(points.size());
        props.GetOrAdd<glm::vec3>("samples").Vector() = points;
        props.GetOrAdd<float>("keep").Vector().assign(points.size(), 42.f);
        return entity;
    }

    // The copy's math: the height, then Jacobi averaging over the k neighbours (converged below 1e-7).
    T::TemplateMath Smoothing()
    {
        return {.Init =
                    [](std::span<const glm::vec3> points)
            {
                std::vector<float> state;
                for (const auto& p : points)
                    state.push_back(p.z);
                return state;
            },
            .Step =
                [](std::vector<float>& state, std::span<const std::uint32_t> rows, const std::uint32_t k)
            {
                const std::vector<float> before = state;
                float change = 0.f;
                for (std::size_t i = 0; i < state.size(); ++i)
                {
                    float sum = before[i];
                    for (std::uint32_t j = 0; j < k; ++j)
                        sum += before[rows[i * k + j]];
                    state[i] = sum / float(k + 1u);
                    change = std::max(change, std::abs(state[i] - before[i]));
                }
                return change < 1e-7f;
            }};
    }

    // The CPU reference of `steps` iterations: the exhaustive rows and the same math.
    std::vector<float> Reference(const std::uint32_t k, const unsigned steps)
    {
        const auto points = Samples();
        std::vector<std::uint32_t> rows;
        for (std::uint32_t i = 0; i < points.size(); ++i)
            for (const auto& n : Geometry::PointLBVH::KNearestReference(points, points[i], k, i))
                rows.push_back(n.Index);
        const auto math = Smoothing();
        auto state = math.Init(points);
        for (unsigned s = 0; s < steps; ++s)
            (void)math.Step(state, rows, k);
        return state;
    }

    // The template's run as an editor job lane of the harness engine (a runtime module's EditorStepRunJobs).
    R::StepRunJobs Jobs(Harness& h)
    {
        R::EditorJobCommandSurface surface{};
        surface.Submit = [&h](R::JobDesc desc, R::EditorJobIdentity)
        {
            return h.Engine.Jobs().Submit(std::move(desc));
        };
        surface.Cancel = [&h](const R::JobToken token)
        {
            return h.Engine.Jobs().Cancel(token) ? R::EditorJobCancelStatus::Requested
                                                 : R::EditorJobCancelStatus::NotActive;
        };
        return R::EditorStepRunJobs(surface, {}, std::make_shared<R::JobToken>());
    }

    T::TemplateRequest Request(const std::uint32_t id, std::string output, const R::StepRunExecution execution)
    {
        return {.StableEntityId = id,
            .Positions = {.Domain = D::PointCloudPoint,
                .Name = "samples",
                .ValueKind = Geometry::PropertyValueKind::Vec3},
            .Output = {.Domain = D::PointCloudPoint,
                .Name = std::move(output),
                .ValueKind = Geometry::PropertyValueKind::Float},
            .K = 2u,
            .Execution = execution};
    }

    struct Fixture
    {
        Harness H{};
        entt::entity Entity{MakeCloud(H)};
        std::uint32_t Id{R::SelectionController::ToStableEntityId(Entity)};

        R::StepRunsService& Runs()
        {
            return *H.Engine.Services().Find<R::StepRunsService>();
        }
        R::StepRunObservation Observe(const std::uint64_t run)
        {
            return *Runs().Observe(run);
        }
        Geometry::PropertySet& Props()
        {
            return H.Scene().Raw().get<GS::Vertices>(Entity).Properties;
        }
        bool Until(const std::function<bool()>& done, const int frames = 2000)
        {
            for (int frame = 0; !done() && frame < frames; ++frame)
                H.Frame();
            return done();
        }
        bool Resting(const std::uint64_t run)
        {
            const auto o = Observe(run);
            return o.Phase == R::StepRunPhase::Paused && !o.SegmentInFlight;
        }
        bool Control(const std::uint64_t run, const R::StepRunAction action, const std::uint32_t count = 1u)
        {
            std::string refusal;
            const bool accepted = Runs().Control(run, action, count, refusal);
            EXPECT_TRUE(accepted) << refusal;
            return accepted;
        }
        std::uint64_t Start(const T::TemplateRequest& request, T::TemplateMath math = Smoothing())
        {
            std::string refusal;
            const auto run = T::StartTemplateCpuRun(H.Editing(), Runs(), Jobs(H), request, std::move(math), refusal);
            EXPECT_NE(run, 0u) << refusal;
            return run;
        }
        std::vector<float> Output(const std::string& name)
        {
            return std::as_const(Props()).Get<float>(name).Vector();
        }
    };

    // The one-shot copy's math: each point's mean distance to its k query neighbours (the point spacing of the GPU
    // producer's method 1 with scale 1).
    T::TemplateOneShotCompute MeanDistance()
    {
        return [](std::span<const glm::vec3> points,
                   std::span<const std::uint32_t> rows,
                   const std::uint32_t k,
                   std::vector<float>& values)
        {
            values.assign(points.size(), 0.f);
            for (std::size_t i = 0; i < points.size(); ++i)
            {
                double sum = 0.0;
                for (std::uint32_t j = 0; j < k; ++j)
                    sum += double(glm::distance(points[i], points[rows[i * k + j]]));
                values[i] = float(sum / double(k));
            }
            return true;
        };
    }

    void ExpectValues(const std::vector<float>& actual, const std::vector<float>& expected)
    {
        ASSERT_EQ(actual.size(), expected.size());
        for (std::size_t i = 0; i < actual.size(); ++i)
            EXPECT_FLOAT_EQ(actual[i], expected[i]) << i;
    }
}

// Ready computes nothing; Step 1 computes exactly one iteration; Run continues with the same steps to the cap; Restart
// begins generation 1 from the same capture (ready again); Run reaches the same result; Accept writes it once as one
// undo entry and leaves the unrelated property alone.
TEST(MethodTemplate, ReadyStepRunRestartAndOneAcceptOnTheSharedDriver)
{
    Fixture f;
    const auto run = f.Start(Request(f.Id,
        "smoothed",
        {.IterationCap = 3u,
            .PreviewEvery = 0u,
            .AutoAccept = R::StepRunAutoAccept::Never,
            .Start = R::StepRunStart::Ready}));
    ASSERT_NE(run, 0u);
    f.H.Frame(5);
    EXPECT_EQ(f.Observe(run).Phase, R::StepRunPhase::Ready);
    EXPECT_EQ(f.Observe(run).Steps, 0u) << "Init runs no iteration";
    std::string refusal;
    EXPECT_FALSE(f.Runs().Control(run, R::StepRunAction::Accept, 1u, refusal)) << "nothing to accept before a step";

    ASSERT_TRUE(f.Control(run, R::StepRunAction::Step));
    ASSERT_TRUE(f.Until(
        [&]
        {
            return f.Resting(run);
        }));
    EXPECT_EQ(f.Observe(run).Steps, 1u) << "Step 1 is exactly one iteration";
    EXPECT_EQ(f.Observe(run).End, R::StepRunEnd::None);
    EXPECT_FALSE(f.Props().Exists("smoothed")) << "nothing is published before Accept";

    ASSERT_TRUE(f.Control(run, R::StepRunAction::Run));
    ASSERT_TRUE(f.Until(
        [&]
        {
            return f.Resting(run) && f.Observe(run).End != R::StepRunEnd::None;
        }));
    EXPECT_EQ(f.Observe(run).Steps, 3u);
    EXPECT_EQ(f.Observe(run).End, R::StepRunEnd::IterationCap);

    ASSERT_TRUE(f.Control(run, R::StepRunAction::Restart));
    ASSERT_TRUE(f.Until(
        [&]
        {
            return f.Observe(run).Generation == 1u && f.Observe(run).Phase == R::StepRunPhase::Ready;
        }));
    EXPECT_EQ(f.Observe(run).Steps, 0u);
    ASSERT_TRUE(f.Control(run, R::StepRunAction::Run));
    ASSERT_TRUE(f.Until(
        [&]
        {
            return f.Resting(run) && f.Observe(run).End != R::StepRunEnd::None;
        }));
    EXPECT_EQ(f.Observe(run).Steps, 3u) << "Run repeats the same steps from the captured input";
    EXPECT_EQ(f.Observe(run).PreviewCopies, 0u) << "preview off: no copy";

    ASSERT_TRUE(f.Control(run, R::StepRunAction::Accept));
    ASSERT_TRUE(f.Until(
        [&]
        {
            return R::IsTerminal(f.Observe(run).Phase);
        }));
    EXPECT_EQ(f.Observe(run).Phase, R::StepRunPhase::Committed) << f.Observe(run).Diagnostic;
    ExpectValues(f.Output("smoothed"), Reference(2u, 3u));
    EXPECT_EQ(std::as_const(f.Props()).Get<float>("keep").Vector(), std::vector<float>(6u, 42.f));
    EXPECT_EQ(f.H.History().UndoCount(), 1u) << "one Accept, one undo entry";
}

// The explicit query choice: the exhaustive scan and the cached LBVH give the same rows, so the same result. A requested
// live preview (preview_every 1) has no consumer: it is normalized to 0, reported by the observation, makes no copy and
// leaves the result unchanged. Batch runs accept at their end.
TEST(MethodTemplate, BruteForceAndLbvhRowsAgreeAndARequestedPreviewIsNormalizedOff)
{
    Fixture f;
    const R::StepRunExecution batch{.IterationCap = 4u,
        .PreviewEvery = 0u,
        .PreviewIntervalMs = 0u,
        .AutoAccept = R::StepRunAutoAccept::End,
        .Start = R::StepRunStart::Run};
    auto previewed = batch;
    previewed.PreviewEvery = 1u;
    auto brute = Request(f.Id, "brute", batch);
    brute.Query = T::TemplateQuery::BruteForce;
    auto lbvh = Request(f.Id, "lbvh", previewed);
    lbvh.Query = T::TemplateQuery::CachedLbvh;
    const auto a = f.Start(brute);
    ASSERT_TRUE(f.Until(
        [&]
        {
            return R::IsTerminal(f.Observe(a).Phase);
        }));
    const auto b = f.Start(lbvh);
    ASSERT_TRUE(f.Until(
        [&]
        {
            return R::IsTerminal(f.Observe(b).Phase);
        }));
    EXPECT_EQ(f.Observe(a).Phase, R::StepRunPhase::Committed) << f.Observe(a).Diagnostic;
    EXPECT_EQ(f.Observe(b).Phase, R::StepRunPhase::Committed) << f.Observe(b).Diagnostic;
    EXPECT_EQ(f.Observe(a).PreviewCopies, 0u);
    EXPECT_EQ(f.Observe(b).Execution.PreviewEvery, 0u) << "the requested preview is reported off";
    EXPECT_EQ(f.Observe(b).PreviewCopies, 0u) << "no state copy without a consumer";
    ExpectValues(f.Output("brute"), Reference(2u, 4u));
    ExpectValues(f.Output("lbvh"), f.Output("brute"));
    EXPECT_EQ(f.H.History().UndoCount(), 2u);
}

// The rows (here the O(n^2) brute-force scan) and the copy's Init run in the first step of a generation, on the worker:
// not at the start, not while ready, not on later steps, not in Restart (main thread), again on the next generation's
// first step.
TEST(MethodTemplate, TheFirstWorkerStepOfAGenerationPreparesRowsAndState)
{
    Fixture f;
    struct Probe
    {
        std::atomic<int> Inits{0};
        std::atomic<std::thread::id> Thread{};
    };
    const auto probe = std::make_shared<Probe>();
    auto math = Smoothing();
    math.Init = [probe, init = math.Init](std::span<const glm::vec3> points)
    {
        probe->Thread.store(std::this_thread::get_id());
        probe->Inits.fetch_add(1);
        return init(points);
    };
    auto request = Request(f.Id,
        "prepared",
        {.IterationCap = 3u,
            .PreviewEvery = 0u,
            .AutoAccept = R::StepRunAutoAccept::Never,
            .Start = R::StepRunStart::Ready});
    request.Query = T::TemplateQuery::BruteForce;
    const auto main = std::this_thread::get_id();
    const auto run = f.Start(request, std::move(math));
    ASSERT_NE(run, 0u);
    EXPECT_EQ(probe->Inits.load(), 0) << "the start only captures";
    f.H.Frame(5);
    EXPECT_EQ(probe->Inits.load(), 0) << "a ready run prepares nothing";

    ASSERT_TRUE(f.Control(run, R::StepRunAction::Step));
    ASSERT_TRUE(f.Until(
        [&]
        {
            return f.Resting(run);
        }));
    EXPECT_EQ(probe->Inits.load(), 1) << "step 1 prepares";
    EXPECT_NE(probe->Thread.load(), main) << "on the worker";
    ASSERT_TRUE(f.Control(run, R::StepRunAction::Step));
    ASSERT_TRUE(f.Until(
        [&]
        {
            return f.Resting(run) && f.Observe(run).Steps == 2u;
        }));
    EXPECT_EQ(probe->Inits.load(), 1) << "a later step does not prepare again";

    ASSERT_TRUE(f.Control(run, R::StepRunAction::Restart));
    ASSERT_TRUE(f.Until(
        [&]
        {
            return f.Observe(run).Generation == 1u && f.Observe(run).Phase == R::StepRunPhase::Ready;
        }));
    EXPECT_EQ(probe->Inits.load(), 1) << "Restart (main thread) prepares nothing";
    probe->Thread.store({});
    ASSERT_TRUE(f.Control(run, R::StepRunAction::Run));
    ASSERT_TRUE(f.Until(
        [&]
        {
            return f.Resting(run) && f.Observe(run).End != R::StepRunEnd::None;
        }));
    EXPECT_EQ(probe->Inits.load(), 2) << "the new generation's first step prepares again";
    EXPECT_NE(probe->Thread.load(), main);
    ASSERT_TRUE(f.Control(run, R::StepRunAction::Accept));
    ASSERT_TRUE(f.Until(
        [&]
        {
            return R::IsTerminal(f.Observe(run).Phase);
        }));
    EXPECT_EQ(f.Observe(run).Phase, R::StepRunPhase::Committed) << f.Observe(run).Diagnostic;
    ExpectValues(f.Output("prepared"), Reference(2u, 3u));
}

// A zero iteration cap is refused at the start: the template's Commit publishes the state the first step prepares, so a
// cap-0 run would have nothing to publish. No run starts and the copy's Init never runs.
TEST(MethodTemplate, AZeroIterationCapIsRefusedBeforeAnyRunStarts)
{
    Fixture f;
    const auto inits = std::make_shared<std::atomic<int>>(0);
    auto math = Smoothing();
    math.Init = [inits, init = math.Init](std::span<const glm::vec3> points)
    {
        inits->fetch_add(1);
        return init(points);
    };
    const auto request = Request(f.Id,
        "zero",
        {.IterationCap = 0u,
            .PreviewEvery = 0u,
            .AutoAccept = R::StepRunAutoAccept::End,
            .Start = R::StepRunStart::Run});
    std::string refusal;
    EXPECT_EQ(T::StartTemplateCpuRun(f.H.Editing(), f.Runs(), Jobs(f.H), request, std::move(math), refusal), 0u);
    EXPECT_NE(refusal.find("max_iterations"), std::string::npos) << refusal;
    EXPECT_TRUE(f.Runs().Runs().empty()) << "no run starts";
    f.H.Frame(5);
    EXPECT_EQ(inits->load(), 0) << "the copy's Init never runs";
    EXPECT_FALSE(f.Props().Exists("zero"));
    EXPECT_EQ(f.H.History().UndoCount(), 0u);
}

// Generation safety: an input edit after the capture discards the run (nothing is published); a Discard publishes
// nothing either.
TEST(MethodTemplate, AStaleCaptureDiscardsTheRunAndDiscardPublishesNothing)
{
    Fixture f;
    const R::StepRunExecution held{.IterationCap = 2u,
        .PreviewEvery = 0u,
        .AutoAccept = R::StepRunAutoAccept::Never,
        .Start = R::StepRunStart::Run};
    const auto stale = f.Start(Request(f.Id, "stale", held));
    ASSERT_TRUE(f.Until(
        [&]
        {
            return f.Resting(stale);
        }));
    f.Props().Get<glm::vec3>("samples")[0].z += 1.f; // the captured input changes
    ASSERT_TRUE(f.Until(
        [&]
        {
            return R::IsTerminal(f.Observe(stale).Phase);
        }));
    EXPECT_EQ(f.Observe(stale).Phase, R::StepRunPhase::Discarded) << f.Observe(stale).Diagnostic;
    EXPECT_FALSE(f.Props().Exists("stale"));

    const auto dropped = f.Start(Request(f.Id, "dropped", held));
    ASSERT_TRUE(f.Until(
        [&]
        {
            return f.Resting(dropped);
        }));
    ASSERT_TRUE(f.Control(dropped, R::StepRunAction::Discard));
    ASSERT_TRUE(f.Until(
        [&]
        {
            return R::IsTerminal(f.Observe(dropped).Phase);
        }));
    EXPECT_EQ(f.Observe(dropped).Phase, R::StepRunPhase::Discarded);
    EXPECT_FALSE(f.Props().Exists("dropped"));
    EXPECT_EQ(f.H.History().UndoCount(), 0u);
}

// ARCH-023: the template's one-shot method. Its CPU counterpart follows the common script on the shared driver (ready
// computes nothing, Step 1 one step, Restart from the same capture, Run, one Accept), writes nothing before Accept and
// gives the same column with either query; its GPU path is the existing scalar producer, whose admission answers here
// without a device (the device run is PointCloudEditingScalarAnalysisGpuSmoke's).
TEST(MethodTemplate, AOneShotRunsTheCommonLifecycleOnTheCpuAndAdmitsTheGpuProducer)
{
    Fixture f;
    const auto request = [&](std::string output, const T::TemplateQuery query, const R::StepRunStart start)
    {
        return T::TemplateOneShotRequest{.StableEntityId = f.Id,
            .Positions = {.Domain = D::PointCloudPoint,
                .Name = "samples",
                .ValueKind = Geometry::PropertyValueKind::Vec3},
            .Output = {.Domain = D::PointCloudPoint,
                .Name = std::move(output),
                .ValueKind = Geometry::PropertyValueKind::Float},
            .Query = query,
            .Execution = R::OneShotExecution({.AutoAccept = R::StepRunAutoAccept::Never, .Start = start}),
            .Params = {.Method = 1, .K = 2u, .Scale = 1.f}};
    };
    std::string ended, refusal;
    const auto run = T::StartTemplateOneShotCpuRun(
        f.H.Editing(),
        Jobs(f.H),
        request("spacing", T::TemplateQuery::CachedLbvh, R::StepRunStart::Ready),
        MeanDistance(),
        [&](const std::string_view status, std::string, bool)
        {
            ended = std::string{status};
        },
        refusal);
    ASSERT_NE(run, 0u) << refusal;
    const auto nothing = [&]
    {
        EXPECT_FALSE(f.Props().Exists("spacing")) << "nothing is written before Accept";
    };
    Intrinsic::Tests::OneShotLifecycleScript script{
        .OnReadyHeld = nothing, .OnFirstResult = nothing, .OnRestartedReady = nothing, .OnRestartedResult = nothing};
    ASSERT_TRUE(f.Until(
        [&]
        {
            return script.Advance(f.Runs(), run);
        }));
    EXPECT_EQ(script.Current, Intrinsic::Tests::OneShotLifecycleScript::Stage::Done);
    ASSERT_TRUE(f.Until(
        [&]
        {
            return !ended.empty();
        }));
    EXPECT_EQ(ended, "applied");

    std::string bruteEnded;
    auto brute = request("brute_spacing", T::TemplateQuery::BruteForce, R::StepRunStart::Run);
    brute.Execution.AutoAccept = R::StepRunAutoAccept::End; // a batch
    ASSERT_NE(T::StartTemplateOneShotCpuRun(
                  f.H.Editing(),
                  Jobs(f.H),
                  brute,
                  MeanDistance(),
                  [&](const std::string_view status, std::string, bool)
                  {
                      bruteEnded = std::string{status};
                  },
                  refusal),
        0u)
        << refusal;
    ASSERT_TRUE(f.Until(
        [&]
        {
            return !bruteEnded.empty();
        }));
    EXPECT_EQ(bruteEnded, "applied");
    ExpectValues(f.Output("spacing"), f.Output("brute_spacing"));
    EXPECT_EQ(std::as_const(f.Props()).Get<float>("keep").Vector(), std::vector<float>(6u, 42.f));
    EXPECT_EQ(f.H.History().UndoCount(), 2u) << "one undo entry per Accept";

    const auto gpu = T::StartTemplateOneShotGpuRun(
        f.H.Editing(), request("gpu_spacing", T::TemplateQuery::CachedLbvh, R::StepRunStart::Ready), refusal);
    EXPECT_FALSE(gpu) << "no device here";
    EXPECT_EQ(refusal, "Vulkan scalar analysis requires an operational float64 device and framed spatial cache/jobs.");
    EXPECT_FALSE(f.Props().Exists("gpu_spacing"));
}
