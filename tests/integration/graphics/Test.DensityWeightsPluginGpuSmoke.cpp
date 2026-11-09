// ARCH-022 Q90: the density weights plugin's Vulkan run (the editing service's GPU scalar analysis, method 2) in a
// Vulkan engine composed as the Sandbox composes it. Eight domains with deleted slots and NaN rows agree with the CPU
// KD-tree within 1e-5 for every kernel and mode, cold and warm (the cached index reused), with the same neighbour
// contributions, one undo entry each, as the MCP tool runs them (accepting at once), and an automatic radius resolves
// to the CPU's; a run that waits keeps the output until Accept, Discard leaves it, inputs changed before GPU recording
// end a run stale, a Jobs window cancel ends one, and a radius overflow names a smaller radius, all writing nothing; a
// tiny support, a 1e-310 support and one sample match the CPU reference, and subnormal coordinates or a radius beyond
// the LBVH limits are refused; an unload releases a waiting preview without writing. Replaces
// PointLBVHGpuSmoke.DensityWeight* and produces the benchmark JSON of
// geometry.point_lbvh.density_weight_runtime_smoke.
#include <algorithm>
#include <bit>
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
#include "DensityWeightsHarness.hpp"

import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.Runtime.GpuPropertyBinding;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.RHI.BufferManager;
import Geometry.Graph;
import Geometry.HalfedgeMesh;
import Geometry.PointCloud.Kernels;
import Geometry.PointLBVH;

namespace
{
    using namespace Intrinsic::Tests::DensityWeights;
    using D = R::GeometryElementDomain;
    using B = DW::DensityWeightBackend;
    namespace GS = Extrinsic::ECS::Components::GeometrySources;
    namespace Kernels = Geometry::PointCloud::Kernels;
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

    // One entity per domain, 66 seeded samples each (row 2 duplicates row 0, row 3 sits just past the support of row 1,
    // row 65 is far away), NaN row 4 deleted (a halfedge domain deletes edge 2).
    std::vector<entt::entity> MakeDomains(Extrinsic::ECS::Scene::Registry& scene)
    {
        std::mt19937 random(251);
        std::uniform_real_distribution<float> dist(-1, 1);
        std::vector<glm::vec3> points;
        for (unsigned i = 0; i < 66; ++i)
        {
            const float x = dist(random), y = dist(random);
            points.push_back({x, y, .1f * dist(random)});
        }
        points[0] = {0, 0, 0};
        points[1] = {2, 0, 0};
        points[2] = points[0];
        points[3] = {std::nextafter(2.f, 3.f), 0, 0};
        points[65] = {10, 0, 0};
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
                edges.GetOrAdd<bool>("e:deleted")[2] = true;
            }
            else p.GetOrAdd<bool>(DeletedName(D(d)))[4] = true;
            p.Get<glm::vec3>("samples")[4] = {std::numeric_limits<float>::quiet_NaN(), 0, 0};
        }
        return entities;
    }

    // Phases 0 (cold) and 1 (warm): LOP theta, direct; 2-5: Gaussian and Wendland C2, direct and reciprocal; 6: LOP
    // theta reciprocal; 7: an automatic radius.
    // A run that computes at once and accepts at its end (`accept`, a batch) or waits for Accept or Discard (the
    // section's auto_accept and start rows).
    DW::Config Running(DW::Config config, const bool accept)
    {
        config.Execution =
            R::OneShotExecution({.AutoAccept = accept ? R::StepRunAutoAccept::End : R::StepRunAutoAccept::Never,
                .Start = R::StepRunStart::Run});
        return config;
    }

    DW::Config Settings(const entt::entity entity, const D domain, const B backend, const unsigned phase)
    {
        DW::Config c{.StableEntityId = R::SelectionController::ToStableEntityId(entity),
                     .Backend = backend,
                     .Positions = {.Domain = domain, .Name = "samples", .ValueKind = Geometry::PropertyValueKind::Vec3},
                     .Weights = {.Domain = domain, .Name = "weights", .ValueKind = Geometry::PropertyValueKind::Float},
                     .SupportRadius = phase == 7 ? 0.0 : 2.0,
                     .GpuQueryBatchSize = 128};
        if (phase == 2 || phase == 3) c.Kernel = Kernels::KernelType::Gaussian;
        if (phase == 4 || phase == 5) c.Kernel = Kernels::KernelType::WendlandC2;
        if (phase == 3 || phase == 5 || phase == 6) c.Mode = Kernels::DensityWeightMode::Reciprocal;
        return c;
    }

    void Fill(Geometry::PropertySet& props) { props.GetOrAdd<float>("weights").Vector().assign(props.Size(), 77.f); }

    // Refreshes the GPU runs (as their tool and window do) and answers whether every one ended.
    bool AllFinal(const std::vector<std::shared_ptr<DW::Run>>& runs)
    {
        for (const auto& run : runs) DW::Refresh(*run);
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

TEST(DensityWeightsPluginGpuSmoke, EightDomainsEveryKernelAndModeAndAnAutomaticRadiusAgreeWithTheCpuKdTree)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize()) GTEST_SKIP() << "GLFW unavailable";
    Gpu gpu;
    auto& scene = gpu.H->Scene();
    const auto entities = MakeDomains(scene);
    // Per phase: the CPU KD-tree for every domain, its values kept as the reference and the output reset to 77, then
    // Vulkan for every domain at once, accepting as the tool does.
    std::vector<std::shared_ptr<DW::Run>> references, vulkan;
    std::vector<std::vector<float>> expected;
    std::vector<double> cpuMs, vulkanMs;
    double maxError = 0;
    unsigned phase = 0;
    auto started = std::chrono::steady_clock::now();
    ASSERT_TRUE(gpu.Run([&]
    {
        if (references.empty())
        {
            gpu.H->History().ClearHistory();
            started = std::chrono::steady_clock::now();
            for (unsigned d = 1; d <= 8; ++d)
            {
                Fill(Props(scene, entities[d - 1], D(d)));
                references.push_back(DW::Start(Running(Settings(entities[d - 1], D(d), B::CpuKDTree, phase), true)));
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
                expected.push_back(std::as_const(props).Get<float>("weights").Vector());
                Fill(props);
            }
            started = std::chrono::steady_clock::now();
            for (unsigned d = 1; d <= 8; ++d)
                vulkan.push_back(DW::Start(Running(Settings(entities[d - 1], D(d), B::VulkanLBVH, phase), true)));
            return false;
        }
        if (!AllFinal(vulkan)) return false;
        vulkanMs.push_back(Milliseconds(started));
        for (unsigned d = 1; d <= 8; ++d)
        {
            SCOPED_TRACE(d);
            SCOPED_TRACE(phase);
            const auto& reference = *references[d - 1]->Final;
            const auto& result = *vulkan[d - 1]->Final;
            EXPECT_EQ(reference.Status, "applied") << reference.Message;
            EXPECT_EQ(result.Status, "applied") << result.Message;
            EXPECT_EQ(result.ActualBackend, "vulkan_lbvh");
            EXPECT_GT(result.GpuQueryBatches, 0u);
            EXPECT_EQ(result.Weights.Domain, D(d));
            if (phase > 0) EXPECT_TRUE(result.IndexReused);
            EXPECT_EQ(result.WrittenCount, reference.WrittenCount);
            EXPECT_EQ(result.Diagnostics.NeighborContributionCount, reference.Diagnostics.NeighborContributionCount);
            EXPECT_EQ(result.SupportRadius, reference.SupportRadius) << "the automatic radius resolves as on the CPU";
            EXPECT_EQ(result.QueryRadius, reference.QueryRadius);
            const auto& props = Props(scene, entities[d - 1], D(d));
            const auto values = std::as_const(props).Get<float>("weights").Vector();
            EXPECT_EQ(values.size(), expected[d - 1].size());
            for (std::size_t i = 0; i < std::min(values.size(), expected[d - 1].size()); ++i)
            {
                const double error = std::abs(double(values[i]) - double(expected[d - 1][i]));
                maxError = std::isfinite(error) ? std::max(maxError, error) : std::numeric_limits<double>::infinity();
            }
            EXPECT_EQ(values[4], 77) << "a deleted row keeps its value";
            if (Half(D(d))) EXPECT_EQ(values[5], 77);
            EXPECT_EQ(std::as_const(props).Get<float>("keep")[0], 42.f);
        }
        // The last eight entries are the Vulkan runs': undone, the outputs hold 77 again; then redone.
        for (unsigned i = 0; i < 8; ++i) EXPECT_EQ(gpu.H->History().Undo().Status, R::EditorCommandHistoryStatus::Undone);
        for (unsigned d = 1; d <= 8; ++d)
            EXPECT_EQ(std::as_const(Props(scene, entities[d - 1], D(d))).Get<float>("weights")[0], 77);
        for (unsigned i = 0; i < 8; ++i) EXPECT_EQ(gpu.H->History().Redo().Status, R::EditorCommandHistoryStatus::Redone);
        references.clear();
        vulkan.clear();
        return ++phase == 8;
    })) << "the frames ran out in phase " << phase;
    EXPECT_LE(maxError, 1e-5);
    ASSERT_EQ(vulkanMs.size(), 8u);
    if (const auto* output = std::getenv("INTRINSIC_DENSITY_WEIGHT_BENCHMARK_OUTPUT"))
    {
        const Json json{{"benchmark_id", "geometry.point_lbvh.density_weight_runtime_smoke"},
                        {"method", "geometry.point_lbvh"},
                        {"backend", "gpu_vulkan_compute"},
                        {"dataset", "builtin.density_weight_samples.eight_domains.seed251"},
                        {"commit", "local-dev"},
                        {"metrics", {{"runtime_ms", vulkanMs[1]}, {"quality_error_linf", maxError}}},
                        {"diagnostics",
                         {{"runner", "IntrinsicPointLBVHGpuTests"},
                          {"mode", "smoke"},
                          {"cpu_reference_total_ms", cpuMs[1]},
                          {"vulkan_cold_total_ms", vulkanMs[0]},
                          {"vulkan_warm_total_ms", vulkanMs[1]},
                          {"warmup_iterations", 1},
                          {"measured_iterations", 1},
                          {"cpu_kernel_reduction", false}}},
                        {"status", ::testing::Test::HasFailure() ? "failed" : "passed"}};
        std::ofstream stream(output);
        ASSERT_TRUE(stream.good());
        stream << json.dump(2) << '\n';
        ASSERT_TRUE(stream.good());
    }
}

// On the point cloud: a run that waits shows its preview with the output unchanged until Accept, and Discard leaves
// the output and the history; inputs changed before GPU recording end a run stale, a Jobs window cancel ends one, and
// 1030 coincident points overflow a candidate capacity of 1024 with a suggested smaller radius, all writing nothing.
TEST(DensityWeightsPluginGpuSmoke, AcceptDiscardStaleCancelAndARadiusOverflow)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize()) GTEST_SKIP() << "GLFW unavailable";
    Gpu gpu;
    auto& scene = gpu.H->Scene();
    const auto cloud = MakeDomains(scene).back();
    auto& props = Props(scene, cloud, D::PointCloudPoint);
    Fill(props);
    const auto config = Settings(cloud, D::PointCloudPoint, B::VulkanLBVH, 0);
    std::shared_ptr<DW::Run> discarded, accepted, stale, cancelled, overflow;
    bool previewSeen = false, cancelRequested = false, staleKept = false, cancelKept = false;
    auto& cache = *gpu.H->Engine.Services().Find<R::SpatialIndexCache>();
    std::uint64_t submittedBefore = 0;
    std::size_t undoBefore = 0;
    ASSERT_TRUE(gpu.Run(
        [&]
        {
            if (!discarded)
                discarded = DW::Start(Running(config, false));
            DW::Refresh(*discarded);
            if (!discarded->Final)
            {
                const auto state = discarded->Analysis.Poll();
                if (state.Phase != R::ScalarAnalysisPhase::ReadyToAccept)
                    return false;
                const auto observed = gpu.H->Engine.Services().Find<R::StepRunsService>()->Observe(state.StepRun);
                EXPECT_TRUE(observed);
                if (observed)
                    EXPECT_EQ(observed->Owner, "density_weights");
                previewSeen = state.CanAccept && HasRing(*gpu.H, cloud, config.Weights) &&
                    std::as_const(props).Get<float>("weights")[0] == 77.f;
                undoBefore = gpu.H->History().UndoCount();
                (void)DW::Cancel(*discarded); // the window's Discard
                return false;
            }
            if (!accepted)
                accepted = DW::Start(Running(config, false));
            DW::Refresh(*accepted);
            if (!accepted->Final)
            {
                if (accepted->Analysis.Poll().Phase == R::ScalarAnalysisPhase::ReadyToAccept)
                    DW::Accept(*accepted);
                return false;
            }
            if (!stale)
            {
                Fill(props);
                stale = DW::Start(Running(config, true));
                props.Get<glm::vec3>("samples")[0].x += .1f; // before any GPU recording
            }
            DW::Refresh(*stale);
            if (!stale->Final)
                return false;
            if (!cancelled)
            {
                staleKept = std::as_const(props).Get<float>("weights")[0] == 77.f;
                props.Get<glm::vec3>("samples")[0].x -= .1f;
                submittedBefore = cache.Stats().GpuComputeSubmissions;
                cancelled = DW::Start(Running(config, true));
            }
            if (!cancelRequested)
            {
                // Only after this run's compute reached the device and before its result is read.
                DW::Refresh(*cancelled);
                if (cancelled->Final)
                    return true; // it ended before its cancel: reported below
                if (cache.Stats().GpuComputeSubmissions != submittedBefore + 1u ||
                    cancelled->Analysis.Poll().Phase != R::ScalarAnalysisPhase::Running)
                    return false;
                const auto jobs = gpu.H->Jobs();
                const auto row = std::ranges::find_if(jobs,
                    [&](const auto& job)
                    {
                        return job.Identity.OutputName == "weights" && job.State == R::JobState::AwaitingApply;
                    });
                if (row == jobs.end())
                    return false;
                const auto commands = R::PrepareEditorProcessingCommands(gpu.H->Shell);
                cancelRequested = R::CancelEditorJobRun(commands, *row) == R::EditorJobCancelStatus::Requested;
                return false;
            }
            DW::Refresh(*cancelled);
            if (!cancelled->Final)
                return false;
            if (!overflow)
            {
                cancelKept = std::as_const(props).Get<float>("weights")[0] == 77.f;
                props.Resize(1030);
                props.Get<glm::vec3>("samples").Vector().assign(1030, glm::vec3(0));
                props.Get<bool>("v:deleted").Vector().assign(1030, false);
                Fill(props);
                auto c = config;
                c.GpuRadiusCapacity = 1024;
                overflow = DW::Start(Running(c, true));
            }
            DW::Refresh(*overflow);
            return overflow->Final.has_value();
        }))
        << "the frames ran out";
    EXPECT_TRUE(previewSeen) << "the preview was resident and the output unchanged";
    ASSERT_TRUE(discarded->Final);
    EXPECT_EQ(discarded->Final->Status, "discarded") << discarded->Final->Message;
    ASSERT_TRUE(accepted->Final);
    EXPECT_EQ(accepted->Final->Status, "applied") << accepted->Final->Message;
    EXPECT_EQ(accepted->Final->WrittenCount, accepted->Final->LiveCount);
    EXPECT_EQ(accepted->Final->GpuInputUploadBytes, 0u) << "a second run over unchanged inputs uploads nothing";
    EXPECT_EQ(stale->Final->Status, "stale") << stale->Final->Message;
    EXPECT_TRUE(staleKept) << "the stale run wrote nothing";
    ASSERT_TRUE(cancelRequested) << "the run ended before its GPU compute was submitted and parked for its cancel";
    EXPECT_EQ(cancelled->Final->Status, "stale") << cancelled->Final->Message;
    EXPECT_TRUE(cancelKept) << "the cancelled run wrote nothing";
    ASSERT_EQ(overflow->Final->Status, "failed") << overflow->Final->Message;
    // RUNTIME-291: the overflow names the radius as too large and suggests a value (2 * 1024 / 1029).
    EXPECT_NE(overflow->Final->Message.find("overflow"), std::string::npos) << overflow->Final->Message;
    EXPECT_NE(overflow->Final->Message.find("too large"), std::string::npos) << overflow->Final->Message;
    EXPECT_NE(overflow->Final->Message.find("Use about 1.99,"), std::string::npos) << overflow->Final->Message;
    EXPECT_GT(overflow->Final->MaximumNeighbors, 1024u);
    EXPECT_EQ(std::as_const(props).Get<float>("weights")[0], 77.f);
    EXPECT_EQ(gpu.H->History().UndoCount(), undoBefore + 1u) << "the accepted run only";
}

// Tiny supports keep their contributions (the GPU's double expressions round only at publication), a 1e-310 support
// and one sample give weight 1, and the readiness refuses subnormal coordinates and a radius beyond the LBVH limits.
TEST(DensityWeightsPluginGpuSmoke, TinySupportsAndOneSampleMatchTheCpuReference)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize()) GTEST_SKIP() << "GLFW unavailable";
    Gpu gpu;
    auto& scene = gpu.H->Scene();
    const auto entity = scene.Create();
    auto& props = scene.Raw().emplace<GS::Vertices>(entity).Properties;
    struct Case
    {
        std::size_t Size;
        double Support;
    };
    const std::vector<Case> cases{{2, 4.6766236639043417e-23}, {33, 4.6766236639043417e-23}, {1, 1e-310}};
    std::size_t index = 0;
    std::shared_ptr<DW::Run> run;
    std::vector<std::string> refusals;
    std::optional<glm::vec3> subnormal;
    std::vector<std::vector<float>> values, references;
    auto config = Settings(entity, D::PointCloudPoint, B::VulkanLBVH, 2); // Gaussian
    const auto setUp = [&](const Case& c)
    {
        props.Resize(c.Size);
        const float v = std::bit_cast<float>(0x1a01460fu);
        props.GetOrAdd<glm::vec3>("samples").Vector().assign(c.Size, glm::vec3(v));
        props.Get<glm::vec3>("samples")[0] = c.Size == 1 ? glm::vec3{0, -0.f, std::numeric_limits<float>::min()}
                                                         : glm::vec3{0, 0, 0};
        props.GetOrAdd<bool>("v:deleted").Vector().assign(c.Size, false);
        Fill(props);
        config.SupportRadius = c.Support;
    };
    setUp(cases[0]);
    ASSERT_TRUE(gpu.Run([&]
    {
        if (index == 0 && refusals.size() < 2)
        {
            // Asked until the input validation answered (it settles over frames): a subnormal coordinate, then a
            // radius beyond the LBVH limits (refused before the readiness).
            if (refusals.empty() && !subnormal)
            {
                subnormal = std::as_const(props).Get<glm::vec3>("samples")[0];
                props.Get<glm::vec3>("samples")[0] = {std::numeric_limits<float>::denorm_min(), 0, 0};
            }
            if (refusals.empty())
            {
                const auto reason = DW::Preview(config);
                if (reason.starts_with("Checking")) return false;
                refusals.push_back(reason);
                props.Get<glm::vec3>("samples")[0] = *subnormal;
            }
            auto far = config;
            far.SupportRadius = double(Geometry::PointLBVH::CoordinateLimit);
            refusals.push_back(DW::Preview(far));
            return false;
        }
        if (!run)
            run = DW::Start(Running(config, true));
        DW::Refresh(*run);
        if (!run->Final) return false;
        EXPECT_EQ(run->Final->Status, "applied") << run->Final->Message;
        values.push_back(std::as_const(props).Get<float>("weights").Vector());
        const auto samples = std::as_const(props).Get<glm::vec3>("samples").Vector();
        const auto reference = Kernels::ComputeDensityWeights(samples, config.SupportRadius, config.Kernel, config.Mode);
        EXPECT_TRUE(reference.Succeeded());
        references.push_back(reference.Weights);
        run.reset();
        if (++index == cases.size()) return true;
        setUp(cases[index]);
        return false;
    })) << "the frames ran out in case " << index;
    ASSERT_EQ(values.size(), cases.size());
    for (std::size_t c = 0; c < cases.size(); ++c)
    {
        SCOPED_TRACE(c);
        ASSERT_EQ(values[c].size(), references[c].size());
        for (std::size_t i = 0; i < values[c].size(); ++i)
            EXPECT_NEAR(values[c][i], references[c][i], 1e-5) << i;
    }
    EXPECT_GT(values[0][0], 1.0003f) << "the tiny support keeps the neighbour's contribution";
    EXPECT_GT(values[1][0], 1.0003f);
    EXPECT_EQ(values[2][0], 1.f) << "one sample weighs 1";
    ASSERT_EQ(refusals.size(), 2u);
    EXPECT_NE(refusals[0].find("subnormal"), std::string::npos) << refusals[0];
    EXPECT_EQ(refusals[1], "LBVH requires the spatial cache, at most 2^24 points, and coordinates/expanded radius within 1e18.");
}

// A Vulkan start whose job submission the host rejects (here: submissions closed by CancelAndDrain) ends "failed" with
// the rejection's text, not "invalid", and writes nothing (the editing service's refusal and the plugin's mapping).
TEST(DensityWeightsPluginGpuSmoke, ARejectedSubmissionFailsAndWritesNothing)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize()) GTEST_SKIP() << "GLFW unavailable";
    Gpu gpu;
    auto& scene = gpu.H->Scene();
    const auto cloud = MakeDomains(scene).back();
    auto& props = Props(scene, cloud, D::PointCloudPoint);
    Fill(props);
    const auto config = Settings(cloud, D::PointCloudPoint, B::VulkanLBVH, 0);
    std::shared_ptr<DW::Run> run;
    ASSERT_TRUE(gpu.Run([&]
    {
        if (DW::Preview(config).starts_with("Checking")) return false; // the input validation settles first
        gpu.H->Engine.Jobs().CancelAndDrain();
        run = DW::Start(Running(config, true));
        gpu.H->Engine.Jobs().ResumeSubmissions();
        return true;
    })) << "the frames ran out";
    ASSERT_TRUE(run && run->Final);
    EXPECT_EQ(run->Final->Status, "failed") << run->Final->Message;
    EXPECT_NE(run->Final->Message.find("submission was rejected"), std::string::npos) << run->Final->Message;
    EXPECT_EQ(std::as_const(props).Get<float>("weights")[0], 77.f);
    EXPECT_FALSE(HasRing(*gpu.H, cloud, config.Weights));
    EXPECT_FALSE(gpu.H->History().CanUndo());
}

// An unload while a run waits for Accept: the plugin's runs go with it, which discards the preview (its ring is
// released) without writing the output or the history.
TEST(DensityWeightsPluginGpuSmoke, AnUnloadReleasesAWaitingPreviewWithoutWriting)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize()) GTEST_SKIP() << "GLFW unavailable";
    Gpu gpu;
    auto& scene = gpu.H->Scene();
    const auto cloud = MakeDomains(scene).back();
    auto& props = Props(scene, cloud, D::PointCloudPoint);
    const auto config = Settings(cloud, D::PointCloudPoint, B::VulkanLBVH, 0);
    std::weak_ptr<DW::Run> waiting; // the plugin holds its runs; the test does not keep it alive
    std::optional<R::AgentOperationOutcome> unload;
    R::AgentOperationOutcome unloaded{};
    bool ready = false, ringBefore = false, finished = false;
    ASSERT_TRUE(gpu.Run([&]
    {
        if (!ready)
        {
            if (waiting.expired())
                waiting = DW::Start(Running(config, false));
            const auto run = waiting.lock();
            if (!run || run->Final) return true; // reported below
            ready = run->Analysis.Poll().Phase == R::ScalarAnalysisPhase::ReadyToAccept;
            ringBefore = ready && HasRing(*gpu.H, cloud, config.Weights);
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
    EXPECT_FALSE(HasRing(*gpu.H, cloud, config.Weights)) << "the preview was released";
    EXPECT_FALSE(props.Exists("weights"));
    EXPECT_FALSE(gpu.H->History().CanUndo());
}

// ARCH-023: method scratch on the real Vulkan path. A warm run, started once the cold run's buffers retired, takes
// its scratch from the cache (no device allocation) and publishes bitwise the cold run's weights; a plugin unload
// leaves only the host's parked raw buffers (nothing active), and the reloaded plugin's run is served from them.
TEST(DensityWeightsPluginGpuSmoke, ScratchCacheWarmAndReloadedRunsMatchTheColdRunBitwise)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize())
        GTEST_SKIP() << "GLFW unavailable";
    Gpu gpu;
    auto& scene = gpu.H->Scene();
    const auto cloud = MakeDomains(scene).back();
    auto& props = Props(scene, cloud, D::PointCloudPoint);
    const auto config = Settings(cloud, D::PointCloudPoint, B::VulkanLBVH, 0);
    auto& cache = *gpu.H->Engine.Services().Find<R::SpatialIndexCache>();
    const auto poll = [&](std::optional<R::AgentOperationOutcome>& call, R::AgentOperationOutcome& result)
    {
        if (!call->Continuation)
        {
            result = *call;
            return true;
        }
        return call->Continuation(gpu.H->Context, result);
    };
    // Every buffer of the previous run is reusable: nothing leased or awaiting completion, something idle.
    const auto retired = [&]
    {
        const auto stats = cache.Stats();
        return stats.ScratchActiveBytes == 0u && stats.ScratchPendingBytes == 0u && stats.ScratchIdleBytes > 0u;
    };
    std::vector<std::vector<float>> weights;
    std::vector<R::SpatialIndexCacheStats> before, after;
    std::optional<R::SpatialIndexCacheStats> afterUnload;
    std::shared_ptr<DW::Run> run;
    std::optional<R::AgentOperationOutcome> unload, load;
    R::AgentOperationOutcome unloaded{}, loaded{};
    unsigned stage = 0; // runs: 0 cold, 1 warm, 4 after the reload; 2 unloads, 3 loads
    ASSERT_TRUE(gpu.Run(
        [&]
        {
            if (stage == 2)
            {
                if (!retired())
                    return false;
                if (!unload)
                    unload = R::InvokeAgentOperation(gpu.H->Registry, "plugin_unload", gpu.H->Context, "{}", false);
                if (!poll(unload, unloaded))
                    return false;
                unload->Continuation = {}; // answered: released, as the agent server does
                afterUnload = cache.Stats();
                stage = 3;
                return false;
            }
            if (stage == 3)
            {
                if (!load)
                    load = R::InvokeAgentOperation(
                        gpu.H->Registry, "plugin_load", gpu.H->Context, R"({"path":"density_weights"})", false);
                if (!poll(load, loaded))
                    return false;
                load->Continuation = {};
                stage = 4;
                return false;
            }
            if (!run)
            {
                if (stage > 0 && !retired())
                    return false;
                Fill(props);
                before.push_back(cache.Stats());
                run = DW::Start(Running(config, true));
                return false;
            }
            DW::Refresh(*run);
            if (!run->Final)
                return false;
            EXPECT_EQ(run->Final->Status, "applied") << run->Final->Message;
            after.push_back(cache.Stats());
            weights.push_back(std::as_const(props).Get<float>("weights").Vector());
            run.reset();
            if (stage == 4)
                return true;
            ++stage;
            return false;
        }))
        << "the frames ran out in stage " << stage;
    ASSERT_EQ(weights.size(), 3u);
    ASSERT_EQ(after.size(), 3u);
    EXPECT_GT(after[0].ScratchMisses, before[0].ScratchMisses) << "the cold run allocates its scratch";
    for (const std::size_t warm : {std::size_t{1}, std::size_t{2}})
    {
        SCOPED_TRACE(warm);
        EXPECT_EQ(weights[warm], weights[0]) << "bitwise the cold run's weights";
        EXPECT_GE(after[warm].ScratchHits - before[warm].ScratchHits, 3u) << "neighbors, means and stats";
        EXPECT_EQ(after[warm].ScratchMisses, before[warm].ScratchMisses) << "no device allocation";
    }
    EXPECT_FALSE(unloaded.IsError) << unloaded.Text;
    EXPECT_FALSE(loaded.IsError) << loaded.Text;
    ASSERT_TRUE(afterUnload);
    EXPECT_EQ(afterUnload->ScratchActiveBytes, 0u) << "nothing of the unloaded plugin holds a lease";
    EXPECT_GT(afterUnload->ScratchIdleBytes, 0u) << "the host's parked raw buffers survive the unload";
}

// ARCH-023: a run whose scratch the method budget refuses fails with the byte counts and the config key, allocates
// nothing beyond the budget and publishes nothing (no CPU fallback).
TEST(DensityWeightsPluginGpuSmoke, AMethodBudgetRefusalFailsAndWritesNothing)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize())
        GTEST_SKIP() << "GLFW unavailable";
    Gpu gpu;
    auto& scene = gpu.H->Scene();
    const auto cloud = MakeDomains(scene).back();
    auto& props = Props(scene, cloud, D::PointCloudPoint);
    Fill(props);
    const auto config = Settings(cloud, D::PointCloudPoint, B::VulkanLBVH, 0);
    auto& cache = *gpu.H->Engine.Services().Find<R::SpatialIndexCache>();
    ASSERT_NE(cache.MethodScratch(), nullptr);
    cache.MethodScratch()->SetScratchBudget(1024u); // below the 66-row neighbor scratch
    std::shared_ptr<DW::Run> run;
    ASSERT_TRUE(gpu.Run(
        [&]
        {
            if (!run)
                run = DW::Start(Running(config, true));
            DW::Refresh(*run);
            return run->Final.has_value();
        }))
        << "the frames ran out";
    ASSERT_TRUE(run->Final);
    EXPECT_EQ(run->Final->Status, "failed") << run->Final->Message;
    EXPECT_NE(run->Final->Message.find("budget exceeded"), std::string::npos) << run->Final->Message;
    EXPECT_NE(run->Final->Message.find("render.gpu_method_buffer_budget_megabytes"), std::string::npos)
        << run->Final->Message;
    EXPECT_NE(run->Final->ActualBackend, "cpu_kdtree") << "no CPU fallback";
    EXPECT_GT(cache.Stats().ScratchRefusals, 0u);
    EXPECT_LE(
        cache.Stats().ScratchActiveBytes + cache.Stats().ScratchPendingBytes + cache.Stats().ScratchIdleBytes, 1024u);
    EXPECT_EQ(std::as_const(props).Get<float>("weights")[0], 77.f);
    EXPECT_FALSE(HasRing(*gpu.H, cloud, config.Weights));
    EXPECT_FALSE(gpu.H->History().CanUndo());
}
