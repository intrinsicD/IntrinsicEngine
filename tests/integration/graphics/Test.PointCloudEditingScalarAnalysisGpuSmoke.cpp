// ARCH-022 Q89: the point-cloud editing service's GPU scalar analysis (a point scalar method's Vulkan run with a
// resident preview) on a real device, in an engine composed as the Sandbox composes it: a run waits for Accept with
// its preview resident and publishes point spacing equal to the CPU reference within the operation smoke's
// tolerance as one undo entry; an auto-accept run publishes without any caller.
#include <algorithm>
#include <cmath>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include "PluginGpuSmoke.hpp"

import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.Runtime.SelectionController;
import Geometry.PointCloud.Utils;

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
            manual = editing.StartScalarAnalysis(Spacing(cloud, "manual"), false, why);
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
            automatic = editing.StartScalarAnalysis(Spacing(cloud, "automatic"), true, why);
            refusal = why.Message;
            return !automatic;
        }
        const auto phase = automatic.Poll().Phase;
        return phase == P::Applied || phase == P::Discarded || phase == P::Failed;
    })) << "the frames ran out";
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
    const auto reference = Geometry::PointCloud::EstimateRadii(Cloud(), {.KNeighbors = 8, .ScaleFactor = 1.f});
    ASSERT_TRUE(reference);
    auto& props = Points(gpu.H->Scene(), cloud);
    for (const char* name : {"manual", "automatic"})
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
