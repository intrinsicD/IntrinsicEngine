// Every startup method supplies a real tool/config lifecycle scenario. The generated startup inventory makes
// adding a plugin without such a scenario fail, while algorithm-specific inputs keep the contract meaningful.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include "PluginHarness.hpp"
#include "OneShotLifecycleScript.hpp"
#include "StartupPluginRegisters.hpp"

import Extrinsic.Runtime.SelectionController;

namespace
{
    namespace R = Extrinsic::Runtime;
    namespace P = Intrinsic::Plugins;
    using Harness = Intrinsic::Tests::Plugins::Harness;
    using Json = nlohmann::json;
    using D = R::GeometryElementDomain;
    using K = Geometry::PropertyValueKind;
    constexpr D kDomain = D::PointCloudPoint;

    enum class Method
    {
        Spacing,
        Density,
        Weights,
        Descriptors,
        Bilateral,
        Cpd
    };
    struct Scenario
    {
        R::PluginRegisterFn Register;
        R::PluginShutdownFn Shutdown;
        const char* Name;
        const char* Tool;
        Method Kind;
        bool OneShot;
        const char* ConfigException{};
    };
    constexpr std::array kScenarios{Scenario{&P::PointSpacing::Register,
                                        &P::PointSpacing::Shutdown,
                                        "point_spacing",
                                        "point_spacing",
                                        Method::Spacing,
                                        true},
        Scenario{&P::KernelDensity::Register,
            &P::KernelDensity::Shutdown,
            "kernel_density",
            "kernel_density",
            Method::Density,
            true},
        Scenario{&P::DensityWeights::Register,
            &P::DensityWeights::Shutdown,
            "density_weights",
            "density_weights",
            Method::Weights,
            true},
        Scenario{&P::DescriptorAnalysis::Register,
            &P::DescriptorAnalysis::Shutdown,
            "descriptor_analysis",
            "descriptor_analysis_start",
            Method::Descriptors,
            true},
        Scenario{&P::BilateralFilter::Register,
            &P::BilateralFilter::Shutdown,
            "bilateral_filter",
            "bilateral_filter_start",
            Method::Bilateral,
            false},
        // Config-only exception: RUNTIME-361 owns replacing the request-only configuration with a persistent section.
        // The common lifecycle below still runs on two clouds; existing CPD solver/parity tests remain independent.
        Scenario{&P::CoherentPointDrift::Register,
            &P::CoherentPointDrift::Shutdown,
            "coherent_point_drift",
            "coherent_point_drift_start",
            Method::Cpd,
            false,
            "RUNTIME-361: persistent section"}};

    R::GeometryPropertyRef Property(std::string name, const K kind = K::Float)
    {
        return {.Domain = kDomain, .Name = std::move(name), .ValueKind = kind};
    }

    template <class Predicate> bool Until(Harness& harness, Predicate done)
    {
        for (unsigned frame = 0; frame < 5000; ++frame)
        {
            if (done())
                return true;
            harness.Frame();
        }
        return false;
    }

    // The section apply path validates the same serializable values used by UI and config tools.
    void Configure(Harness& harness,
        const Scenario& scenario,
        const entt::entity entity,
        std::vector<R::GeometryPropertyRef>& outputs)
    {
        const auto id = R::SelectionController::ToStableEntityId(entity);
        const auto positions = Property("samples", K::Vec3), normals = Property("directions", K::Vec3);
        const auto ready = R::kOneShotStepRunExecution;
        switch (scenario.Kind)
        {
        case Method::Spacing:
        {
            P::PointSpacing::Config c{.StableEntityId = id,
                .Positions = positions,
                .Radii = Property("radii"),
                .KNeighbors = 2,
                .Execution = ready};
            ASSERT_TRUE(harness.Apply<P::PointSpacing::kSection>(c));
            ASSERT_TRUE(harness
                    .Settled(
                        [&]
                        {
                            return P::PointSpacing::Preview(c);
                        })
                    .empty());
            outputs = {c.Radii};
            break;
        }
        case Method::Density:
        {
            P::KernelDensity::Config c{.StableEntityId = id,
                .Positions = positions,
                .Density = Property("density"),
                .KNeighbors = 2,
                .Execution = ready};
            ASSERT_TRUE(harness.Apply<P::KernelDensity::kSection>(c));
            ASSERT_TRUE(harness
                    .Settled(
                        [&]
                        {
                            return P::KernelDensity::Preview(c);
                        })
                    .empty());
            outputs = {c.Density};
            break;
        }
        case Method::Weights:
        {
            P::DensityWeights::Config c{.StableEntityId = id,
                .Positions = positions,
                .Weights = Property("weights"),
                .SupportRadius = 2.f,
                .Execution = ready};
            ASSERT_TRUE(harness.Apply<P::DensityWeights::kSection>(c));
            ASSERT_TRUE(harness
                    .Settled(
                        [&]
                        {
                            return P::DensityWeights::Preview(c);
                        })
                    .empty());
            outputs = {c.Weights};
            break;
        }
        case Method::Descriptors:
        {
            P::DescriptorAnalysis::Config c{.StableEntityId = id,
                .Positions = positions,
                .Normals = normals,
                .Outputs = P::DescriptorAnalysis::MakeOutputs(kDomain, "descriptor"),
                .FeatureRadius = 10.f,
                .Execution = ready};
            ASSERT_TRUE(harness.Apply<P::DescriptorAnalysis::kSection>(c));
            ASSERT_TRUE(harness
                    .Settled(
                        [&]
                        {
                            return P::DescriptorAnalysis::Preview(c);
                        })
                    .empty());
            outputs.assign(c.Outputs.begin(), c.Outputs.end());
            break;
        }
        case Method::Bilateral:
        {
            P::BilateralFilter::Config c{.StableEntityId = id,
                .Positions = positions,
                .Normals = normals,
                .Output = Property("filtered", K::Vec3),
                .KNeighbors = 2,
                .SpatialSigma = 2.f,
                .Execution = {.IterationCap = 3,
                    .PreviewEvery = 0,
                    .PreviewIntervalMs = 0,
                    .AutoAccept = R::StepRunAutoAccept::Never,
                    .Start = R::StepRunStart::Ready}};
            ASSERT_TRUE(harness.Apply<P::BilateralFilter::kSection>(c));
            ASSERT_TRUE(harness
                    .Settled(
                        [&]
                        {
                            return P::BilateralFilter::Preview(c);
                        })
                    .empty());
            outputs = {c.Output};
            break;
        }
        case Method::Cpd:
            ASSERT_NE(scenario.ConfigException, nullptr);
            outputs = {positions};
            break;
        }
    }
}

TEST(StartupPluginExecution, EveryStartupPluginHasExactlyOneScenario)
{
    const std::vector<R::PluginRegisterFn> startup{INTRINSIC_STARTUP_PLUGIN_REGISTERS};
    for (const auto registration : startup)
        EXPECT_EQ(std::ranges::count(kScenarios, registration, &Scenario::Register), 1)
            << "A new startup plugin needs a real lifecycle/config scenario or a bounded exception.";
    for (const auto& scenario : kScenarios)
    {
        EXPECT_EQ(std::ranges::count(startup, scenario.Register), 1)
            << scenario.Name << ": stale/duplicate registration";
        EXPECT_EQ(std::ranges::count(kScenarios, scenario.Register, &Scenario::Register), 1) << scenario.Name;
        EXPECT_EQ(scenario.ConfigException != nullptr, scenario.Kind == Method::Cpd) << scenario.Name;
    }
}

TEST(StartupPluginExecution, ToolsShareReadyStepRunAcceptUndoAndDiscard)
{
    for (const auto& scenario : kScenarios)
    {
        SCOPED_TRACE(scenario.Name);
        Harness harness(scenario.Register, scenario.Shutdown);
        const auto entity = Intrinsic::Tests::PointCloudEditing::MakeOrientedSamples(harness.Scene(), kDomain);
        Json args{{"start", "ready"}, {"auto_accept", "never"}};
        if (scenario.Kind == Method::Cpd)
        {
            const auto target = Intrinsic::Tests::PointCloudEditing::MakeOrientedSamples(harness.Scene(), kDomain);
            auto points =
                Intrinsic::Tests::PointDomainProperties(harness.Scene(), target, kDomain).Get<glm::vec3>("samples");
            points[3].z += .5f;
            Intrinsic::Tests::PointDomainProperties(harness.Scene(), entity, kDomain).Get<glm::vec3>("samples")[3].z +=
                .5f;
            for (auto& point : points.Vector())
                point += glm::vec3(.3f, -.2f, .1f);
            args = {{"source", R::SelectionController::ToStableEntityId(entity)},
                {"target", R::SelectionController::ToStableEntityId(target)},
                {"positions", "samples"},
                {"max_iterations", 3},
                {"preview_every", 0},
                {"start", "ready"},
                {"auto_accept", "never"}};
        }
        auto& props = Intrinsic::Tests::PointDomainProperties(harness.Scene(), entity, kDomain);
        const auto samples = std::as_const(props).Get<glm::vec3>("samples").Vector();
        const auto normals = std::as_const(props).Get<glm::vec3>("directions").Vector();
        const auto keep = std::as_const(props).Get<float>("keep").Vector();
        std::vector<R::GeometryPropertyRef> outputs;
        Configure(harness, scenario, entity, outputs);
        ASSERT_FALSE(HasFatalFailure());
        harness.Frame(5);
        auto* runs = harness.Engine.Services().Find<R::StepRunsService>();
        ASSERT_NE(runs, nullptr);
        const auto unchanged = [&]
        {
            EXPECT_EQ(std::as_const(props).Get<glm::vec3>("samples").Vector(), samples);
            EXPECT_EQ(std::as_const(props).Get<glm::vec3>("directions").Vector(), normals);
            EXPECT_EQ(std::as_const(props).Get<float>("keep").Vector(), keep);
            for (const auto& output : outputs)
                if (output.Name != "samples")
                    EXPECT_FALSE(props.Exists(output.Name)) << output.Name;
            EXPECT_EQ(harness.History().UndoCount(), 0u);
        };
        const auto start = [&]() -> std::uint64_t
        {
            const auto result = harness.Call(scenario.Tool, args);
            EXPECT_FALSE(result.IsError) << result.Text;
            if (result.IsError)
                return 0;
            return Json::parse(result.Text).at("run").get<std::uint64_t>();
        };
        const auto run = start();
        ASSERT_NE(run, 0u);
        ASSERT_TRUE(runs->Observe(run));
        EXPECT_EQ(runs->Observe(run)->Owner, scenario.Name);
        EXPECT_EQ(runs->Observe(run)->Phase, R::StepRunPhase::Ready);
        EXPECT_EQ(runs->Observe(run)->Steps, 0u);
        harness.Frame(5);
        EXPECT_EQ(runs->Observe(run)->Steps, 0u);
        unchanged();
        std::string refusal;
        if (scenario.OneShot)
        {
            Intrinsic::Tests::OneShotLifecycleScript script{.OnReadyHeld = unchanged,
                .OnFirstResult = unchanged,
                .OnRestartedReady = unchanged,
                .OnRestartedResult = unchanged};
            ASSERT_TRUE(Until(harness,
                [&]
                {
                    return script.Advance(*runs, run);
                }));
        }
        else
        {
            ASSERT_TRUE(runs->Control(run, R::StepRunAction::Step, 1, refusal)) << refusal;
            ASSERT_TRUE(Until(harness,
                [&]
                {
                    const auto o = runs->Observe(run);
                    return o && o->Phase == R::StepRunPhase::Paused && !o->SegmentInFlight;
                }));
            EXPECT_EQ(runs->Observe(run)->Steps, 1u);
            unchanged();
            if (runs->Observe(run)->End == R::StepRunEnd::None)
            {
                ASSERT_TRUE(runs->Control(run, R::StepRunAction::Run, 1, refusal)) << refusal;
                ASSERT_TRUE(Until(harness,
                    [&]
                    {
                        const auto o = runs->Observe(run);
                        return o && o->Phase == R::StepRunPhase::Paused && !o->SegmentInFlight;
                    }));
                EXPECT_GT(runs->Observe(run)->Steps, 1u);
            }
            unchanged();
            ASSERT_TRUE(runs->Control(run, R::StepRunAction::Accept, 1, refusal)) << refusal;
        }
        ASSERT_TRUE(Until(harness,
            [&]
            {
                return R::IsTerminal(runs->Observe(run)->Phase);
            }));
        ASSERT_EQ(runs->Observe(run)->Phase, R::StepRunPhase::Committed) << runs->Observe(run)->Diagnostic;
        for (const auto& output : outputs)
        {
            ASSERT_TRUE(props.Exists(output.Name)) << output.Name;
            if (output.ValueKind == K::Vec3)
            {
                const auto& values = std::as_const(props).Get<glm::vec3>(output.Name).Vector();
                EXPECT_EQ(values.size(), samples.size());
                for (const auto value : values)
                    EXPECT_TRUE(std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z));
            }
            else
            {
                const auto& values = std::as_const(props).Get<float>(output.Name).Vector();
                EXPECT_EQ(values.size(), samples.size());
                for (const auto value : values)
                    EXPECT_TRUE(std::isfinite(value));
            }
        }
        if (scenario.Kind == Method::Cpd)
            EXPECT_NE(std::as_const(props).Get<glm::vec3>("samples").Vector(), samples);
        EXPECT_EQ(harness.History().UndoCount(), 1u);
        EXPECT_FALSE(runs->Control(run, R::StepRunAction::Accept, 1, refusal));
        harness.Frame(3);
        EXPECT_EQ(harness.History().UndoCount(), 1u);
        ASSERT_TRUE(harness.History().Undo().Succeeded());
        unchanged();
        const auto discarded = start();
        ASSERT_NE(discarded, 0u);
        ASSERT_TRUE(runs->Control(discarded, R::StepRunAction::Step, 1, refusal)) << refusal;
        ASSERT_TRUE(Until(harness,
            [&]
            {
                const auto o = runs->Observe(discarded);
                return o && o->Phase == R::StepRunPhase::Paused && !o->SegmentInFlight;
            }));
        unchanged();
        ASSERT_TRUE(runs->Control(discarded, R::StepRunAction::Discard, 1, refusal)) << refusal;
        ASSERT_TRUE(Until(harness,
            [&]
            {
                return R::IsTerminal(runs->Observe(discarded)->Phase);
            }));
        EXPECT_EQ(runs->Observe(discarded)->Phase, R::StepRunPhase::Discarded);
        harness.Frame(3);
        unchanged();
    }
}
