// ARCH-022: the bilateral filter plugin's Vulkan chain (bandwidth, then per pass gated GPU kNN pages and a CPU
// update) in a Vulkan engine composed as the Sandbox composes it: three passes agree with the CPU LBVH run and page
// their queries; inputs changed before GPU recording end the run stale; a cancel between stages ends it cancelled;
// in both nothing is written. Replaces the built-in filter's PointLBVHGpuSmoke.Bilateral* cases.
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include "BilateralFilterHarness.hpp"
#include "PluginGpuSmoke.hpp"

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
                .Positions = {.Domain = D::PointCloudPoint, .Name = "samples", .ValueKind = Geometry::PropertyValueKind::Vec3},
                .Normals = {.Domain = D::PointCloudPoint, .Name = "directions", .ValueKind = Geometry::PropertyValueKind::Vec3},
                .Output = {.Domain = D::PointCloudPoint, .Name = std::move(output), .ValueKind = Geometry::PropertyValueKind::Vec3},
                .KNeighbors = 8,
                .GpuQueryBatchSize = 16,
                .SpatialSigma = 2,
                .Iterations = 3};
    }

    using Gpu = Intrinsic::Tests::Plugins::Gpu<Harness>;
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
    const auto a = std::as_const(props).Get<glm::vec3>("cpu").Vector(), b = std::as_const(props).Get<glm::vec3>("gpu").Vector();
    ASSERT_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i) EXPECT_LE(glm::length(a[i] - b[i]), 1e-5f) << i;
    EXPECT_GT(glm::length(b[0] - std::as_const(props).Get<glm::vec3>("samples")[0]), 0.f) << "the passes moved points";
}

TEST(BilateralFilterPluginGpuSmoke, StaleInputsAndACancelBetweenStagesWriteNothing)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize()) GTEST_SKIP() << "GLFW unavailable";
    Gpu gpu;
    const auto cloud = MakeCloud(gpu.H->Scene());
    auto& props = Points(gpu.H->Scene(), cloud);
    std::shared_ptr<BF::Run> stale, cancelled;
    bool cancelRequested = false;
    ASSERT_TRUE(gpu.Run([&]
    {
        if (!stale)
        {
            stale = BF::Start(Settings(cloud, BF::FilterBackend::VulkanLBVH, "stale_output"));
            props.Get<glm::vec3>("samples")[0].x += .1f; // before any GPU recording
        }
        if (stale->Final && !cancelled) cancelled = BF::Start(Settings(cloud, BF::FilterBackend::VulkanLBVH, "cancel_output"));
        if (cancelled && !cancelRequested)
        {
            // Between stages: once this run's GPU neighbourhood stage is active, its bandwidth stage has completed.
            // (The stale run's finished stages stay listed until the frame reaps them.)
            const auto jobs = gpu.H->Jobs();
            if (std::ranges::any_of(jobs, [&](const auto& job)
                {
                    return job.Name == "Bilateral neighborhoods (Vulkan)" && job.Identity.Run == cancelled->Handle.Token &&
                        R::IsActiveEditorJobState(job.State);
                }))
                cancelRequested = R::CancelPluginRun(*cancelled);
        }
        return cancelled && cancelled->Final;
    })) << "the frames ran out";
    EXPECT_EQ(stale->Final->Status, "stale") << stale->Final->Message;
    EXPECT_TRUE(cancelRequested);
    EXPECT_EQ(cancelled->Final->Status, "cancelled") << cancelled->Final->Message;
    EXPECT_FALSE(props.Exists("stale_output"));
    EXPECT_FALSE(props.Exists("cancel_output"));
    EXPECT_FALSE(gpu.H->History().CanUndo());
}
