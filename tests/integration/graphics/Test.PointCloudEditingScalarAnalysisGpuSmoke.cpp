// ARCH-022 Q89: the point-cloud editing service's GPU scalar analysis (a point scalar method's Vulkan run with a
// resident preview) on a real device, in an engine composed as the Sandbox composes it: a run waits for Accept with
// its preview resident and publishes point spacing equal to the CPU reference within the operation smoke's
// tolerance as one undo entry; an auto-accept run publishes without any caller. ARCH-023: a ready run follows the
// common one-shot lifecycle on the device (OneShotLifecycleScript) and Step 1 publishes what Run publishes; a restart
// records into the workspace its first generation leased; a detach before run_control's Accept publishes nothing; the
// method template's one-shot runs this producer as its GPU path beside its CPU counterpart.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include "OneShotLifecycleScript.hpp"
#include "PluginGpuSmoke.hpp"

import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.SpatialIndexCache;
import Geometry.PointCloud.Utils;

#include "TemplateMethod.hpp"

namespace
{
    namespace R = Extrinsic::Runtime;
    using D = R::GeometryElementDomain;
    using P = R::ScalarAnalysisPhase;
    using Gpu = Intrinsic::Tests::Plugins::Gpu<Intrinsic::Tests::PointCloudEditing::Harness>;

    std::vector<glm::vec3> Cloud()
    {
        std::mt19937 random(241);
        std::uniform_real_distribution<float> dist(-1, 1);
        std::vector<glm::vec3> points;
        for (unsigned i = 0; i < 66; ++i) points.push_back({dist(random), dist(random), .1f * dist(random)});
        points[65] = {10, 0, 0};
        return points;
    }

    Geometry::PropertySet& Points(Extrinsic::ECS::Scene::Registry& scene, const entt::entity entity)
    {
        return scene.Raw().get<Extrinsic::ECS::Components::GeometrySources::Vertices>(entity).Properties;
    }

    entt::entity MakeCloud(Extrinsic::ECS::Scene::Registry& scene)
    {
        const auto entity = scene.Create();
        auto& props = scene.Raw().emplace<Extrinsic::ECS::Components::GeometrySources::Vertices>(entity).Properties;
        const auto points = Cloud();
        props.Resize(points.size());
        props.GetOrAdd<glm::vec3>("samples").Vector() = points;
        return entity;
    }

    // Each named output equals the CPU point-spacing reference within the operation smoke's tolerance.
    void ExpectCpuReference(Geometry::PropertySet& props, const std::initializer_list<const char*> names)
    {
        const auto reference = Geometry::PointCloud::EstimateRadii(Cloud(), {.KNeighbors = 8, .ScaleFactor = 1.f});
        ASSERT_TRUE(reference);
        for (const char* name : names)
        {
            SCOPED_TRACE(name);
            const auto values = std::as_const(props).Get<float>(name).Vector();
            ASSERT_EQ(values.size(), reference->Radii.size());
            double error = 0;
            for (std::size_t i = 0; i < values.size(); ++i)
                error = std::max(error, double(std::abs(values[i] - reference->Radii[i])));
            EXPECT_LE(error, 1e-5);
        }
    }

    R::ScalarAnalysisRequest Spacing(const entt::entity entity, std::string output)
    {
        return {.StableEntityId = R::SelectionController::ToStableEntityId(entity),
                .Positions = {.Domain = D::PointCloudPoint, .Name = "samples", .ValueKind = Geometry::PropertyValueKind::Vec3},
                .Output = {.Domain = D::PointCloudPoint, .Name = std::move(output), .ValueKind = Geometry::PropertyValueKind::Float},
                .Params = {.Method = 1, .K = 8, .Scale = 1.f},
                .Noun = "Radii",
                .Method = "Point spacing",
                .History = "Estimate point spacing and radii"};
    }

    std::uint64_t WorkspaceLeases(Intrinsic::Tests::PointCloudEditing::Harness& h)
    {
        return h.Engine.Services().Find<R::SpatialIndexCache>()->Stats().WorkspaceLeases;
    }
}

TEST(PointCloudEditingScalarAnalysisGpuSmoke, AcceptPublishesTheCpuReferenceAndAnAutoAcceptRunNeedsNoCaller)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize()) GTEST_SKIP() << "GLFW unavailable";
    Gpu gpu;
    const auto cloud = MakeCloud(gpu.H->Scene());
    auto& editing = gpu.H->Editing();
    R::ScalarAnalysis manual, automatic;
    std::string refusal;
    bool sawPreview = false, accepted = false;
    std::size_t undoAfterManual = 0;
    ASSERT_TRUE(gpu.Run([&]
    {
        if (!manual)
        {
            // Readiness settles over frames (the input validation); the start waits for it.
            refusal = editing.ScalarAnalysisReadiness(Spacing(cloud, "manual"), R::ScalarAnalysisBackend::Gpu);
            if (!refusal.empty()) return refusal.find("Wait for input validation") == std::string::npos;
            R::ScalarAnalysisRefusal why;
            auto interactive = R::kOneShotStepRunExecution;
            interactive.Start = R::StepRunStart::Run;
            manual = editing.StartScalarAnalysis(Spacing(cloud, "manual"), interactive, why);
            refusal = why.Message;
            return !manual;
        }
        const auto state = manual.Poll();
        if (state.Phase == P::ReadyToAccept && !accepted)
        {
            sawPreview = state.CanAccept && !Points(gpu.H->Scene(), cloud).Exists("manual");
            accepted = manual.Accept().Phase == P::Accepting;
            return !accepted;
        }
        if (state.Phase != P::Applied) return state.Phase == P::Discarded || state.Phase == P::Failed;
        if (!automatic)
        {
            undoAfterManual = gpu.H->History().UndoCount();
            R::ScalarAnalysisRefusal why;
            auto execution = R::kOneShotBatchExecution;
            execution.AutoAccept = R::StepRunAutoAccept::Converged;
            automatic = editing.StartScalarAnalysis(Spacing(cloud, "automatic"), execution, why);
            if (automatic)
            {
                const auto observed = automatic.StepRuns()->Observe(automatic.Poll().StepRun);
                EXPECT_TRUE(observed);
                if (observed)
                    EXPECT_EQ(observed->Execution.AutoAccept, R::StepRunAutoAccept::Converged);
            }
            refusal = why.Message;
            return !automatic;
        }
        const auto phase = automatic.Poll().Phase;
        return phase == P::Applied || phase == P::Discarded || phase == P::Failed;
    })) << "the frames ran out";
    EXPECT_EQ(gpu.H->Engine.Services().Find<R::SpatialIndexCache>()->Stats().Builds, 0u)
        << "GPU-only scalar runs build no CPU hierarchy";
    ASSERT_TRUE(manual) << refusal;
    ASSERT_TRUE(automatic) << refusal;
    EXPECT_TRUE(sawPreview) << "the run waited for Accept with the output unchanged";
    const auto state = manual.Poll();
    ASSERT_EQ(state.Phase, P::Applied) << state.Message;
    EXPECT_EQ(state.LiveCount, 66u);
    EXPECT_GT(state.GpuQueryBatches, 0u);
    ASSERT_EQ(automatic.Poll().Phase, P::Applied) << automatic.Poll().Message;
    EXPECT_EQ(undoAfterManual, 1u) << "Accept recorded one undo entry";
    EXPECT_EQ(gpu.H->History().UndoCount(), 2u) << "the automatic run recorded one more";
    ExpectCpuReference(Points(gpu.H->Scene(), cloud), {"manual", "automatic"});
}

// ARCH-023: the real scalar transaction's one-shot step run through the common script: started ready it records no
// device work (no query batch) and shows nothing; Step 1 computes its one step; Restart drops that result (the counters
// start over) and Run computes it again; Accept publishes once. A second ready run accepted after Step 1 publishes the
// same CPU reference, so Step 1 and Run compute the same output.
TEST(PointCloudEditingScalarAnalysisGpuSmoke, AReadyRunFollowsTheCommonOneShotLifecycleOnTheDevice)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize())
        GTEST_SKIP() << "GLFW unavailable";
    Gpu gpu;
    const auto cloud = MakeCloud(gpu.H->Scene());
    auto& editing = gpu.H->Editing();
    R::ScalarAnalysis scripted, stepped;
    std::string refusal;
    std::uint64_t leases = 0u;
    Intrinsic::Tests::OneShotLifecycleScript script{.OnReadyHeld =
                                                        [&]
        {
            const auto s = scripted.Poll();
            EXPECT_EQ(s.Phase, P::Running);
            EXPECT_EQ(s.GpuQueryBatches, 0u) << "a ready run queues no device work";
            EXPECT_FALSE(Points(gpu.H->Scene(), cloud).Exists("scripted"));
        },
        .OnFirstResult =
            [&]
        {
            EXPECT_EQ(scripted.Poll().Phase, P::ReadyToAccept);
            leases = WorkspaceLeases(*gpu.H);
        },
        .OnRestartedReady =
            [&]
        {
            const auto s = scripted.Poll();
            EXPECT_EQ(s.Phase, P::Running);
            EXPECT_EQ(s.GpuQueryBatches, 0u) << "the new generation starts from the queued state";
        },
        .OnRestartedResult =
            [&]
        {
            EXPECT_EQ(scripted.Poll().Phase, P::ReadyToAccept);
            EXPECT_EQ(WorkspaceLeases(*gpu.H), leases) << "the restart recorded into the workspace it already held";
        }};
    bool steppedOnce = false, steppedAccepted = false;
    ASSERT_TRUE(gpu.Run(
        [&]
        {
            if (!scripted)
            {
                refusal = editing.ScalarAnalysisReadiness(Spacing(cloud, "scripted"), R::ScalarAnalysisBackend::Gpu);
                if (!refusal.empty())
                    return refusal.find("Wait for input validation") == std::string::npos;
                R::ScalarAnalysisRefusal why;
                scripted = editing.StartScalarAnalysis(Spacing(cloud, "scripted"), R::kOneShotStepRunExecution, why);
                refusal = why.Message;
                return !scripted;
            }
            if (!script.Advance(*scripted.StepRuns(), scripted.Poll().StepRun))
                return false;
            if (scripted.Poll().Phase != P::Applied)
                return scripted.Poll().Phase == P::Discarded || scripted.Poll().Phase == P::Failed;
            auto* runs = scripted.StepRuns();
            std::string ignored;
            if (!stepped)
            {
                R::ScalarAnalysisRefusal why;
                stepped = editing.StartScalarAnalysis(Spacing(cloud, "stepped"), R::kOneShotStepRunExecution, why);
                refusal = why.Message;
                return !stepped;
            }
            const auto s = stepped.Poll();
            const auto o = runs->Observe(s.StepRun);
            if (!steppedOnce)
            {
                steppedOnce = runs->Control(s.StepRun, R::StepRunAction::Step, 1u, ignored);
                return !steppedOnce;
            }
            if (s.Phase == P::ReadyToAccept && o && o->Phase == R::StepRunPhase::Paused && !steppedAccepted)
            {
                steppedAccepted = runs->Control(s.StepRun, R::StepRunAction::Accept, 1u, ignored);
                return !steppedAccepted;
            }
            return s.Phase == P::Applied || s.Phase == P::Discarded || s.Phase == P::Failed;
        }))
        << "the frames ran out";
    ASSERT_TRUE(scripted) << refusal;
    ASSERT_TRUE(stepped) << refusal;
    EXPECT_EQ(script.Current, Intrinsic::Tests::OneShotLifecycleScript::Stage::Done);
    ASSERT_EQ(scripted.Poll().Phase, P::Applied) << scripted.Poll().Message;
    ASSERT_EQ(stepped.Poll().Phase, P::Applied) << stepped.Poll().Message;
    EXPECT_EQ(gpu.H->History().UndoCount(), 2u) << "one undo entry per Accept";
    ExpectCpuReference(Points(gpu.H->Scene(), cloud), {"scripted", "stepped"});
}

// ARCH-023 (the scalar run_control bypass): a detach after the result is ready refuses run_control's Accept at the
// transaction's one current check (GpuTransactionCurrent), without any plugin poll: nothing is published, no undo.
TEST(PointCloudEditingScalarAnalysisGpuSmoke, ARunControlAcceptAfterADetachPublishesNothing)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize())
        GTEST_SKIP() << "GLFW unavailable";
    Gpu gpu;
    const auto cloud = MakeCloud(gpu.H->Scene());
    auto& editing = gpu.H->Editing();
    R::ScalarAnalysis run;
    std::string refusal;
    bool accepted = false;
    int after = 0;
    ASSERT_TRUE(gpu.Run(
        [&]
        {
            if (!run)
            {
                refusal = editing.ScalarAnalysisReadiness(Spacing(cloud, "detached"), R::ScalarAnalysisBackend::Gpu);
                if (!refusal.empty())
                    return refusal.find("Wait for input validation") == std::string::npos;
                R::ScalarAnalysisRefusal why;
                run = editing.StartScalarAnalysis(Spacing(cloud, "detached"),
                    R::OneShotExecution({.AutoAccept = R::StepRunAutoAccept::Never, .Start = R::StepRunStart::Run}),
                    why);
                refusal = why.Message;
                return !run;
            }
            auto* runs = run.StepRuns();
            const auto id = run.Poll().StepRun;
            if (!accepted)
            {
                const auto o = runs->Observe(id);
                if (!o || o->End != R::StepRunEnd::Converged || o->SegmentInFlight)
                    return false;
                gpu.H->Shell.Detach(); // no Poll in between: run_control alone sees the detach
                std::string ignored;
                accepted = runs->Control(id, R::StepRunAction::Accept, 1u, ignored);
                gpu.H->Shell.Attach(gpu.H->Engine.Worlds(), gpu.H->Engine.Services());
                return !accepted;
            }
            return ++after > 30;
        }))
        << "the frames ran out";
    ASSERT_TRUE(accepted) << refusal;
    EXPECT_FALSE(Points(gpu.H->Scene(), cloud).Exists("detached"));
    EXPECT_FALSE(gpu.H->History().CanUndo());
    EXPECT_NE(run.Poll().Phase, P::Applied);
}

namespace
{
    // The template's CPU run as a runtime module submits it: editor jobs of the harness engine.
    R::StepRunJobs EngineJobs(Intrinsic::Tests::PointCloudEditing::Harness& h)
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
}

// ARCH-023: the method template's one-shot on both backends. The GPU path is the existing scalar producer (method 1,
// the point spacing kernel) and follows the common script on the device; the CPU counterpart (the copy's math over the
// cached LBVH rows) runs as a batch; both publish the CPU reference within the operation smoke's tolerance, each with
// one undo entry.
TEST(PointCloudEditingScalarAnalysisGpuSmoke, TheTemplatesOneShotRunsTheResidentProducerAndItsCpuCounterpart)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize())
        GTEST_SKIP() << "GLFW unavailable";
    namespace T = Intrinsic::Methods::Template;
    Gpu gpu;
    const auto cloud = MakeCloud(gpu.H->Scene());
    const auto request = [&](std::string output, const R::StepRunExecution execution)
    {
        const auto spacing = Spacing(cloud, std::move(output));
        return T::TemplateOneShotRequest{.StableEntityId = spacing.StableEntityId,
            .Positions = spacing.Positions,
            .Output = spacing.Output,
            .Execution = execution,
            .Params = spacing.Params};
    };
    const auto meanDistance = [](std::span<const glm::vec3> points,
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
    R::ScalarAnalysis device;
    std::string refusal, cpuEnded;
    bool cpuStarted = false;
    Intrinsic::Tests::OneShotLifecycleScript script{};
    ASSERT_TRUE(gpu.Run(
        [&]
        {
            if (!device)
            {
                refusal = gpu.H->Editing().ScalarAnalysisReadiness(
                    Spacing(cloud, "template_gpu"), R::ScalarAnalysisBackend::Gpu);
                if (!refusal.empty())
                    return refusal.find("Wait for input validation") == std::string::npos;
                device = T::StartTemplateOneShotGpuRun(
                    gpu.H->Editing(), request("template_gpu", R::kOneShotStepRunExecution), refusal);
                return !device;
            }
            if (!script.Advance(*device.StepRuns(), device.Poll().StepRun))
                return false;
            if (!cpuStarted)
            {
                cpuStarted = true;
                return T::StartTemplateOneShotCpuRun(
                           gpu.H->Editing(),
                           EngineJobs(*gpu.H),
                           request("template_cpu", R::kOneShotBatchExecution),
                           meanDistance,
                           [&](const std::string_view status, std::string, bool)
                           {
                               cpuEnded = std::string{status};
                           },
                           refusal) == 0u;
            }
            return !cpuEnded.empty() && device.Poll().Phase != P::Accepting;
        }))
        << "the frames ran out";
    ASSERT_TRUE(device) << refusal;
    EXPECT_EQ(script.Current, Intrinsic::Tests::OneShotLifecycleScript::Stage::Done);
    ASSERT_EQ(device.Poll().Phase, P::Applied) << device.Poll().Message;
    ASSERT_EQ(cpuEnded, "applied") << refusal;
    EXPECT_EQ(gpu.H->History().UndoCount(), 2u) << "one undo entry per Accept";
    ExpectCpuReference(Points(gpu.H->Scene(), cloud), {"template_gpu", "template_cpu"});
}
