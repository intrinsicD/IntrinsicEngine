// ARCH-022 Q89: the kernel density plugin's Vulkan run (the editing service's GPU scalar analysis) in a Vulkan engine
// composed as the Sandbox composes it. Eight domains with deleted slots and NaN rows agree with the CPU octree within
// 1e-5, cold, warm (the cached index reused) and with k = 63 and bandwidth 0.2 (an isolated sample keeps an exact
// zero), one undo entry each and one query batch per run, as the MCP tool runs them (accepting at once); a run that
// waits keeps the output until Accept, and Discard leaves it; inputs changed before GPU recording end the run stale, a
// Jobs window cancel ends it, a coincident cluster gives the Gaussian's peak, a tiny bandwidth matches the CPU
// reference, and an unload releases a waiting preview without writing. Replaces PointLBVHGpuSmoke.KernelDensity* and
// the kernel density cases of RUNTIME298PointScalarResidency, and produces the benchmark JSON of
// geometry.point_lbvh.density_runtime_smoke.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include "PluginGpuSmoke.hpp"
#include "KernelDensityHarness.hpp"

import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.Runtime.GpuPropertyBinding;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.SpatialIndexCache;
import Geometry.Graph;
import Geometry.HalfedgeMesh;
import Geometry.PointCloud.Utils;

namespace
{
    using namespace Intrinsic::Tests::KernelDensity;
    using D = R::GeometryElementDomain;
    namespace GS = Extrinsic::ECS::Components::GeometrySources;
    using Gpu = Intrinsic::Tests::Plugins::Gpu<Harness>;

    Geometry::PropertySet& Props(Extrinsic::ECS::Scene::Registry& scene, const entt::entity entity, const D domain)
    {
        return *const_cast<Geometry::PropertySet*>(
            R::ResolveGeometryPropertySet(R::BuildGeometryAvailability(scene.Raw(), entity), domain));
    }

    bool Half(const D domain) { return domain == D::MeshHalfedge || domain == D::GraphHalfedge; }

    const char* DeletedName(const D d)
    {
        return d == D::MeshFace ? "f:deleted" : (d == D::MeshEdge || d == D::GraphEdge) ? "e:deleted" : "v:deleted";
    }

    constexpr std::size_t kIsolatedRow = 75; // far from every other sample: its Gaussian tails underflow at h = 0.2

    // One entity per domain, 82 seeded samples each (row 2 duplicates row 0, row 3 sits just past 0.5, rows 65 and 75
    // are far away), NaN rows 4 and 64 deleted with a deleted run straddling row 64 (a halfedge domain deletes edges 2 and
    // 30..34), so compaction leaves two GPU pages of 64, a partial final one included, on every domain.
    std::vector<entt::entity> MakeDomains(Extrinsic::ECS::Scene::Registry& scene)
    {
        std::mt19937 random(241);
        std::uniform_real_distribution<float> dist(-1, 1);
        std::vector<glm::vec3> points;
        for (unsigned i = 0; i < 82; ++i)
        {
            const float x = dist(random), y = dist(random);
            points.push_back({.1f * x, .1f * y, 0});
        }
        points[0] = {0, 0, 0};
        points[1] = {.3f, .4f, 0};
        points[2] = points[0];
        points[3] = {std::nextafter(.5f, 1.f), 0, 0};
        points[65] = {10, 0, 0};
        points[kIsolatedRow] = {40, 0, 0};
        std::vector<entt::entity> entities;
        for (unsigned d = 1; d <= 8; ++d)
        {
            const auto entity = scene.Create();
            entities.push_back(entity);
            if (d <= unsigned(D::MeshFace))
            {
                Geometry::HalfedgeMesh::Mesh mesh;
                const auto a = mesh.AddVertex({0, 0, 0}), b = mesh.AddVertex({1, 0, 0}), c = mesh.AddVertex({0, 1, 0});
                (void)mesh.AddTriangle(a, b, c);
                GS::PopulateFromMesh(scene.Raw(), entity, mesh);
            }
            else if (d < unsigned(D::PointCloudPoint))
            {
                Geometry::Graph::Graph graph;
                const auto a = graph.AddVertex({0, 0, 0}), b = graph.AddVertex({1, 0, 0});
                (void)graph.AddEdge(a, b);
                GS::PopulateFromGraph(scene.Raw(), entity, graph);
            }
            else scene.Raw().emplace<GS::Vertices>(entity);
            auto& p = Props(scene, entity, D(d));
            p.Resize(points.size());
            p.GetOrAdd<glm::vec3>("samples").Vector() = points;
            p.GetOrAdd<float>("keep").Vector().assign(points.size(), 42.f);
            if (Half(D(d)))
            {
                auto& edges = scene.Raw().get<GS::Edges>(entity).Properties;
                edges.Resize(points.size() / 2);
                auto deleted = edges.GetOrAdd<bool>("e:deleted");
                deleted[2] = true;
                for (unsigned edge = 30; edge < 35; ++edge) deleted[edge] = true;
            }
            else
            {
                auto deleted = p.GetOrAdd<bool>(DeletedName(D(d)));
                deleted[4] = true;
                for (unsigned row = 60; row < 70; ++row) deleted[row] = true;
            }
            p.Get<glm::vec3>("samples")[64] = {std::numeric_limits<float>::quiet_NaN(), 0, 0};
            p.Get<glm::vec3>("samples")[4] = {std::numeric_limits<float>::quiet_NaN(), 0, 0};
        }
        return entities;
    }

    KD::Config Settings(const entt::entity entity, const D domain, const KD::DensityBackend backend, const unsigned phase)
    {
        return {.StableEntityId = R::SelectionController::ToStableEntityId(entity),
                .Backend = backend,
                .Positions = {.Domain = domain, .Name = "samples", .ValueKind = Geometry::PropertyValueKind::Vec3},
                .Density = {.Domain = domain, .Name = "density", .ValueKind = Geometry::PropertyValueKind::Float},
                .KNeighbors = phase == 2 ? 63u : 8u,
                .GpuQueryBatchSize = 64, // exercises a final partial chunk
                .Bandwidth = phase == 2 ? .2f : 0.f};
    }

    void Fill(Geometry::PropertySet& props) { props.GetOrAdd<float>("density").Vector().assign(props.Size(), 77.f); }

    // Refreshes the GPU runs (as their tool and window do) and answers whether every one ended.
    bool AllFinal(const std::vector<std::shared_ptr<KD::Run>>& runs)
    {
        for (const auto& run : runs) KD::Refresh(*run);
        return !runs.empty() && std::ranges::all_of(runs, [](const auto& run) { return run->Final.has_value(); });
    }

    double Milliseconds(const std::chrono::steady_clock::time_point started)
    {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    }

    bool HasRing(Harness& harness, const entt::entity entity, const R::GeometryPropertyRef& output)
    {
        auto* cache = harness.Engine.Services().Find<R::SpatialIndexCache>();
        auto* residency = cache != nullptr ? cache->PropertyResidency() : nullptr;
        return residency != nullptr &&
            residency->HasRing(R::MakeGpuPropertyKey(harness.Engine.ActiveWorld(), entity, output));
    }
}

TEST(KernelDensityPluginGpuSmoke, EightDomainsAgreeWithTheCpuOctreeColdWarmAndScaled)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize()) GTEST_SKIP() << "GLFW unavailable";
    Gpu gpu;
    auto& scene = gpu.H->Scene();
    const auto entities = MakeDomains(scene);
    // Per phase (cold, warm, k = 63 with bandwidth 0.2): the CPU octree for every domain, its values kept as the reference
    // and the output reset to 77, then Vulkan for every domain at once, accepting as the tool does.
    std::vector<std::shared_ptr<KD::Run>> references, vulkan;
    std::vector<std::vector<float>> expected;
    std::vector<double> cpuMs, vulkanMs;
    std::vector<std::size_t> batchCounts;
    double maxError = 0;
    unsigned phase = 0;
    std::string unsupported;
    auto started = std::chrono::steady_clock::now();
    ASSERT_TRUE(gpu.Run([&]
    {
        // Asked every frame until the input validation answered (it settles over frames).
        if (unsupported.empty() || unsupported.starts_with("Checking"))
        {
            auto refused = Settings(entities[7], D::PointCloudPoint, KD::DensityBackend::VulkanLBVH, 0);
            refused.KNeighbors = 64;
            unsupported = KD::Preview(refused);
        }
        if (references.empty())
        {
            gpu.H->History().ClearHistory();
            started = std::chrono::steady_clock::now();
            for (unsigned d = 1; d <= 8; ++d)
            {
                Fill(Props(scene, entities[d - 1], D(d)));
                references.push_back(KD::Start(Settings(entities[d - 1], D(d), KD::DensityBackend::CpuOctree, phase), false));
            }
            return false;
        }
        if (!AllFinal(references)) return false;
        if (vulkan.empty())
        {
            cpuMs.push_back(Milliseconds(started));
            expected.clear();
            for (unsigned d = 1; d <= 8; ++d)
            {
                auto& props = Props(scene, entities[d - 1], D(d));
                expected.push_back(std::as_const(props).Get<float>("density").Vector());
                Fill(props);
            }
            started = std::chrono::steady_clock::now();
            for (unsigned d = 1; d <= 8; ++d)
                vulkan.push_back(KD::Start(Settings(entities[d - 1], D(d), KD::DensityBackend::VulkanLBVH, phase), true));
            return false;
        }
        if (!AllFinal(vulkan)) return false;
        vulkanMs.push_back(Milliseconds(started));
        std::size_t batches = 0;
        for (unsigned d = 1; d <= 8; ++d)
        {
            SCOPED_TRACE(d);
            const auto& reference = *references[d - 1]->Final;
            const auto& result = *vulkan[d - 1]->Final;
            EXPECT_EQ(reference.Status, "applied") << reference.Message;
            EXPECT_EQ(result.Status, "applied") << result.Message;
            EXPECT_EQ(result.ActualBackend, "vulkan_lbvh");
            EXPECT_EQ(result.GpuQueryBatches, 1u) << "one resident kernel submission";
            EXPECT_EQ(result.CpuComputeMilliseconds, 0);
            EXPECT_EQ(result.CpuStageReadbackBytes, Props(scene, entities[d - 1], D(d)).Size() * sizeof(float));
            EXPECT_EQ(result.SlotCount, reference.SlotCount);
            EXPECT_EQ(result.SlotCount, Props(scene, entities[d - 1], D(d)).Size());
            EXPECT_EQ(result.Density.Domain, D(d));
            if (phase > 0) EXPECT_TRUE(result.IndexReused);
            EXPECT_EQ(result.LiveCount, reference.LiveCount);
            batches += result.GpuQueryBatches;
            for (const auto [actual, wanted] : {std::pair{result.UsedBandwidth, reference.UsedBandwidth},
                                                std::pair{result.MeanDensity, reference.MeanDensity},
                                                std::pair{result.MinDensity, reference.MinDensity},
                                                std::pair{result.MaxDensity, reference.MaxDensity}})
            {
                EXPECT_NEAR(actual, wanted, 1e-5);
                maxError = std::max(maxError, double(std::abs(actual - wanted)));
            }
            const auto& props = Props(scene, entities[d - 1], D(d));
            const auto values = std::as_const(props).Get<float>("density").Vector();
            EXPECT_EQ(values.size(), expected[d - 1].size());
            for (std::size_t i = 0; i < std::min(values.size(), expected[d - 1].size()); ++i)
            {
                EXPECT_TRUE(std::isfinite(values[i])) << i;
                const double error = std::abs(double(values[i]) - double(expected[d - 1][i]));
                maxError = std::isfinite(error) ? std::max(maxError, error) : std::numeric_limits<double>::infinity();
            }
            EXPECT_EQ(values[Half(D(d)) ? 5 : 4], 77) << "a deleted row keeps its value";
            if (phase == 2 && kIsolatedRow < values.size())
            {
                EXPECT_EQ(values[kIsolatedRow], 0.f) << "the isolated sample keeps an exact zero";
                EXPECT_EQ(expected[d - 1][kIsolatedRow], 0.f);
            }
            EXPECT_EQ(std::as_const(props).Get<float>("keep")[0], 42.f);
        }
        batchCounts.push_back(batches);
        // The last eight entries are the Vulkan runs': undone, the outputs hold 77 again; then redone.
        for (unsigned i = 0; i < 8; ++i) EXPECT_EQ(gpu.H->History().Undo().Status, R::EditorCommandHistoryStatus::Undone);
        for (unsigned d = 1; d <= 8; ++d)
            EXPECT_EQ(std::as_const(Props(scene, entities[d - 1], D(d))).Get<float>("density")[0], 77);
        for (unsigned i = 0; i < 8; ++i) EXPECT_EQ(gpu.H->History().Redo().Status, R::EditorCommandHistoryStatus::Redone);
        references.clear();
        vulkan.clear();
        return ++phase == 3;
    })) << "the frames ran out in phase " << phase;
    EXPECT_EQ(unsupported, "Vulkan scalar analysis requires k<=63 and normal or zero parameters.");
    EXPECT_LE(maxError, 1e-5);
    ASSERT_EQ(vulkanMs.size(), 3u);
    if (const auto* output = std::getenv("INTRINSIC_DENSITY_BENCHMARK_OUTPUT"))
    {
        const Json json{{"benchmark_id", "geometry.point_lbvh.density_runtime_smoke"},
                        {"method", "geometry.point_lbvh"},
                        {"backend", "gpu_vulkan_compute"},
                        {"dataset", "builtin.density_clusters.eight_domains.seed241"},
                        {"commit", "local-dev"},
                        {"metrics", {{"runtime_ms", vulkanMs[1]}, {"quality_error_linf", maxError}}},
                        {"diagnostics",
                         {{"runner", "IntrinsicPointLBVHGpuTests"},
                          {"mode", "smoke"},
                          {"cpu_reference_total_ms", cpuMs[1]},
                          {"vulkan_cold_total_ms", vulkanMs[0]},
                          {"vulkan_warm_total_ms", vulkanMs[1]},
                          {"vulkan_manual_bandwidth_k63_total_ms", vulkanMs[2]},
                          {"cpu_manual_bandwidth_k63_total_ms", cpuMs[2]},
                          {"warmup_iterations", 1},
                          {"measured_iterations", 1},
                          {"cpu_bandwidth_and_gaussian", false},
                          {"gpu_query_batch_counts", batchCounts}}},
                        {"status", ::testing::Test::HasFailure() ? "failed" : "passed"}};
        std::ofstream stream(output);
        ASSERT_TRUE(stream.good());
        stream << json.dump(2) << '\n';
        ASSERT_TRUE(stream.good());
    }
}

// On the point cloud: a run that waits shows its preview with the output unchanged until Accept, and Discard leaves
// the output and the history; inputs changed before GPU recording end a run stale and a Jobs window cancel ends one,
// both writing nothing; a coincident cluster of 1029 points gives the Gaussian's peak (2 pi)^-1.5 at bandwidth 1.
TEST(KernelDensityPluginGpuSmoke, AcceptDiscardStaleCancelAndACoincidentCluster)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize()) GTEST_SKIP() << "GLFW unavailable";
    Gpu gpu;
    auto& scene = gpu.H->Scene();
    const auto cloud = MakeDomains(scene).back();
    auto& props = Props(scene, cloud, D::PointCloudPoint);
    Fill(props);
    const auto config = Settings(cloud, D::PointCloudPoint, KD::DensityBackend::VulkanLBVH, 0);
    std::shared_ptr<KD::Run> discarded, accepted, stale, cancelled, coincident;
    bool previewSeen = false, cancelRequested = false, staleKept = false, cancelKept = false;
    std::size_t undoBefore = 0;
    ASSERT_TRUE(gpu.Run([&]
    {
        if (!discarded) discarded = KD::Start(config, false);
        KD::Refresh(*discarded);
        if (!discarded->Final)
        {
            const auto state = discarded->Analysis.Poll();
            if (state.Phase != R::ScalarAnalysisPhase::ReadyToAccept) return false;
            previewSeen = state.CanAccept && HasRing(*gpu.H, cloud, config.Density) &&
                std::as_const(props).Get<float>("density")[0] == 77.f;
            undoBefore = gpu.H->History().UndoCount();
            (void)KD::Cancel(*discarded); // the window's Discard
            return false;
        }
        if (!accepted)
        {
            auto unknown = config; // the domains resolve to the entity's primary point domain
            unknown.Positions.Domain = unknown.Density.Domain = D::Unknown;
            accepted = KD::Start(unknown, false);
        }
        KD::Refresh(*accepted);
        if (!accepted->Final)
        {
            if (accepted->Analysis.Poll().Phase == R::ScalarAnalysisPhase::ReadyToAccept) KD::Accept(*accepted);
            return false;
        }
        if (!stale)
        {
            Fill(props);
            stale = KD::Start(config, true);
            props.Get<glm::vec3>("samples")[0].x += .1f; // before any GPU recording
        }
        KD::Refresh(*stale);
        if (!stale->Final) return false;
        if (!cancelled)
        {
            staleKept = std::as_const(props).Get<float>("density")[0] == 77.f;
            props.Get<glm::vec3>("samples")[0].x -= .1f;
            cancelled = KD::Start(config, true);
        }
        if (!cancelRequested)
        {
            // While the run's job is parked in AwaitingApply (waiting for residency or for the device result; this does
            // not prove the GPU submission already happened).
            KD::Refresh(*cancelled);
            if (cancelled->Final) return true; // it ended before its cancel: reported below
            const auto jobs = gpu.H->Jobs();
            const auto row = std::ranges::find_if(jobs, [&](const auto& job)
            { return job.Identity.OutputName == "density" && job.State == R::JobState::AwaitingApply; });
            if (row == jobs.end()) return false;
            const auto commands = R::PrepareEditorProcessingCommands(gpu.H->Shell);
            cancelRequested = R::CancelEditorJobRun(commands, *row) == R::EditorJobCancelStatus::Requested;
            return false;
        }
        KD::Refresh(*cancelled);
        if (!cancelled->Final) return false;
        if (!coincident) cancelKept = std::as_const(props).Get<float>("density")[0] == 77.f;
        if (!coincident)
        {
            props.Resize(1030);
            props.Get<glm::vec3>("samples").Vector().assign(1030, glm::vec3(0));
            props.Get<glm::vec3>("samples")[1029] = {10, 0, 0};
            props.Get<bool>("v:deleted").Vector().assign(1030, false);
            auto c = config;
            c.GpuQueryBatchSize = 4096;
            c.Bandwidth = 1;
            coincident = KD::Start(c, true);
        }
        KD::Refresh(*coincident);
        return coincident->Final.has_value();
    })) << "the frames ran out";
    EXPECT_TRUE(previewSeen) << "the preview was resident and the output unchanged";
    ASSERT_TRUE(discarded->Final);
    EXPECT_EQ(discarded->Final->Status, "discarded") << discarded->Final->Message;
    ASSERT_TRUE(accepted->Final);
    EXPECT_EQ(accepted->Final->Status, "applied") << accepted->Final->Message;
    EXPECT_EQ(accepted->Final->WrittenCount, accepted->Final->LiveCount);
    EXPECT_EQ(accepted->Final->SlotCount, 82u);
    EXPECT_EQ(accepted->Final->Density.Domain, D::PointCloudPoint) << "the resolved domain";
    EXPECT_EQ(accepted->Final->GpuInputUploadBytes, 0u) << "a second run over unchanged inputs uploads nothing";
    EXPECT_EQ(stale->Final->Status, "stale") << stale->Final->Message;
    EXPECT_TRUE(staleKept) << "the stale run wrote nothing";
    ASSERT_TRUE(cancelRequested) << "the run ended before it was parked for its cancel";
    EXPECT_EQ(cancelled->Final->Status, "stale") << cancelled->Final->Message;
    EXPECT_NE(cancelled->Final->Message.find("cancelled"), std::string::npos) << cancelled->Final->Message;
    EXPECT_TRUE(cancelKept) << "the cancelled run wrote nothing";
    EXPECT_EQ(gpu.H->History().UndoCount(), undoBefore + 2u) << "the accepted and the coincident runs only";
    ASSERT_EQ(coincident->Final->Status, "applied") << coincident->Final->Message;
    EXPECT_NEAR(std::as_const(props).Get<float>("density")[0], std::pow(2 * std::acos(-1.), -1.5), 1e-7);
}

// Two live points 1.34e-11 apart with bandwidth 1e-12 (the GPU's double expressions round only at publication)
// match the CPU reference within a relative 2e-6, which rejects a flushed-to-zero result.
TEST(KernelDensityPluginGpuSmoke, TinyBandwidthsMatchTheCpuReference)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize()) GTEST_SKIP() << "GLFW unavailable";
    Gpu gpu;
    auto& scene = gpu.H->Scene();
    const auto entity = scene.Create();
    auto& props = scene.Raw().emplace<GS::Vertices>(entity).Properties;
    props.Resize(67);
    const std::vector<glm::vec3> points{{0, 0, 0}, {1.34e-11f, 0, 0}};
    auto positions = props.GetOrAdd<glm::vec3>("samples");
    positions[0] = points[0];
    positions[1] = points[1];
    auto deleted = props.GetOrAdd<bool>("v:deleted");
    for (unsigned j = 2; j < 67; ++j) deleted[j] = true;
    std::shared_ptr<KD::Run> run;
    ASSERT_TRUE(gpu.Run([&]
    {
        if (!run)
        {
            auto c = Settings(entity, D::PointCloudPoint, KD::DensityBackend::VulkanLBVH, 0);
            c.Bandwidth = 1e-12f;
            run = KD::Start(c, true);
        }
        KD::Refresh(*run);
        return run->Final.has_value();
    })) << "the frames ran out";
    ASSERT_EQ(run->Final->Status, "applied") << run->Final->Message;
    const auto reference = Geometry::PointCloud::EstimateKernelDensity(points, {.KNeighbors = 8, .Bandwidth = 1e-12f});
    ASSERT_TRUE(reference);
    const auto values = std::as_const(props).Get<float>("density").Vector();
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        EXPECT_TRUE(std::isfinite(values[i]));
        EXPECT_GT(reference->Densities[i], 0.f);
        EXPECT_NEAR(values[i], reference->Densities[i], std::abs(double(reference->Densities[i])) * 2e-6);
    }
}

// An unload while a run waits for Accept: the plugin's runs go with it, which discards the preview (its ring is
// released) without writing the output or the history.
TEST(KernelDensityPluginGpuSmoke, AnUnloadReleasesAWaitingPreviewWithoutWriting)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize()) GTEST_SKIP() << "GLFW unavailable";
    Gpu gpu;
    auto& scene = gpu.H->Scene();
    const auto cloud = MakeDomains(scene).back();
    auto& props = Props(scene, cloud, D::PointCloudPoint);
    const auto config = Settings(cloud, D::PointCloudPoint, KD::DensityBackend::VulkanLBVH, 0);
    std::weak_ptr<KD::Run> waiting; // the plugin holds its runs; the test does not keep it alive
    std::optional<R::AgentOperationOutcome> unload;
    R::AgentOperationOutcome unloaded{};
    bool ready = false, ringBefore = false, finished = false;
    ASSERT_TRUE(gpu.Run([&]
    {
        if (!ready)
        {
            if (waiting.expired()) waiting = KD::Start(config, false);
            const auto run = waiting.lock();
            if (!run || run->Final) return true; // reported below
            ready = run->Analysis.Poll().Phase == R::ScalarAnalysisPhase::ReadyToAccept;
            ringBefore = ready && HasRing(*gpu.H, cloud, config.Density);
            return false;
        }
        if (!unload) unload = R::InvokeAgentOperation(gpu.H->Registry, "plugin_unload", gpu.H->Context, "{}", false);
        if (!unload->Continuation) unloaded = *unload;
        else if (!unload->Continuation(gpu.H->Context, unloaded)) return false;
        unload->Continuation = {};
        finished = true;
        return true;
    })) << "the frames ran out";
    EXPECT_TRUE(ready);
    EXPECT_TRUE(ringBefore) << "the preview was resident before the unload";
    EXPECT_TRUE(finished);
    EXPECT_FALSE(unloaded.IsError) << unloaded.Text;
    EXPECT_TRUE(waiting.expired()) << "the plugin's runs went with it";
    EXPECT_FALSE(HasRing(*gpu.H, cloud, config.Density)) << "the preview was released";
    EXPECT_FALSE(props.Exists("density"));
    EXPECT_FALSE(gpu.H->History().CanUndo());
}
