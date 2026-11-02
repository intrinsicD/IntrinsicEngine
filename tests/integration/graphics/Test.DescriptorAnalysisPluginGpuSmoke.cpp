// ARCH-022: the FPFH descriptor plugin's Vulkan chain (scale, then gated GPU radius pages, then the CPU histograms)
// in a Vulkan engine composed as the Sandbox composes it. Eight domains with deleted slots agree with the CPU k-d
// tree within 1e-5, cold, warm (the cached index reused) and with an automatic radius, one undo entry each; inputs
// changed before GPU recording end the run stale, a cancel between stages ends it cancelled, an uncapped support
// beyond the radius capacity fails with overflow, all writing nothing; a lowest-ID cap serves a dense radius; an
// unload during a chain ends the run once and completes, and a reload runs again. Replaces the built-in's PointLBVHGpuSmoke.Descriptor*
// cases and produces the benchmark JSON of geometry.point_lbvh.descriptor_runtime_smoke.
#include <algorithm>
#include <array>
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

#include "DescriptorAnalysisHarness.hpp"
#include "PluginGpuSmoke.hpp"

import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.Runtime.SelectionController;
import Geometry.Graph;
import Geometry.HalfedgeMesh;

namespace
{
    using namespace Intrinsic::Tests::DescriptorAnalysis;
    using D = R::GeometryElementDomain;
    namespace GS = Extrinsic::ECS::Components::GeometrySources;
    using Gpu = Intrinsic::Tests::Plugins::Gpu<Harness>;
    using Columns = std::array<std::vector<float>, DA::kBins>;

    Geometry::PropertySet& Props(Extrinsic::ECS::Scene::Registry& scene, const entt::entity entity, const D domain)
    {
        return *const_cast<Geometry::PropertySet*>(
            R::ResolveGeometryPropertySet(R::BuildGeometryAvailability(scene.Raw(), entity), domain));
    }

    bool Half(const D domain) { return domain == D::MeshHalfedge || domain == D::GraphHalfedge; }

    // One entity per domain, 66 seeded samples each (row 2 duplicates row 0, row 3 sits just past 2, row 65 is far
    // away), normals tilted per point, a deleted NaN row 4 (a halfedge domain deletes edge 2, rows 4 and 5).
    std::vector<entt::entity> MakeDomains(Extrinsic::ECS::Scene::Registry& scene)
    {
        std::mt19937 random(241);
        std::uniform_real_distribution<float> dist(-1, 1);
        std::vector<glm::vec3> points;
        for (unsigned i = 0; i < 66; ++i)
        {
            const float x = dist(random), y = dist(random);
            points.push_back({x, y, .1f * dist(random)});
        }
        points[0] = {0, 0, 0};
        points[1] = {1.2f, 1.6f, 0};
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
            p.GetOrAdd<float>("keep").Vector().assign(points.size(), 42);
            auto normals = p.GetOrAdd<glm::vec3>("directions");
            for (std::size_t i = 0; i < points.size(); ++i) normals[i] = {.2f * points[i].x, -.1f * points[i].y, 1};
            if (Half(D(d)))
            {
                auto& edges = scene.Raw().get<GS::Edges>(entity).Properties;
                edges.Resize(points.size() / 2);
                edges.GetOrAdd<bool>("e:deleted")[2] = true;
            }
            else
                p.GetOrAdd<bool>(D(d) == D::MeshFace                              ? "f:deleted"
                                 : (D(d) == D::MeshEdge || D(d) == D::GraphEdge) ? "e:deleted"
                                                                                 : "v:deleted")[4] = true;
            p.Get<glm::vec3>("samples")[4] = {std::numeric_limits<float>::quiet_NaN(), 0, 0};
        }
        return entities;
    }

    DA::Config Settings(const entt::entity entity, const D domain, const char* prefix, const bool automatic)
    {
        return {.StableEntityId = R::SelectionController::ToStableEntityId(entity),
                .Backend = DA::DescriptorBackend::VulkanLBVH,
                .Positions = {.Domain = domain, .Name = "samples", .ValueKind = Geometry::PropertyValueKind::Vec3},
                .Normals = {.Domain = domain, .Name = "directions", .ValueKind = Geometry::PropertyValueKind::Vec3},
                .Outputs = DA::MakeOutputs(domain, prefix),
                .MaxNeighbors = automatic ? 0u : 8u,
                .GpuQueryBatchSize = 128,
                .FeatureRadius = automatic ? 0.f : 2.f};
    }

    Columns Read(Geometry::PropertySet& props, const DA::Config& config)
    {
        Columns columns;
        for (std::size_t b = 0; b < DA::kBins; ++b) columns[b] = std::as_const(props).Get<float>(config.Outputs[b].Name).Vector();
        return columns;
    }

    void Fill(Geometry::PropertySet& props, const DA::Config& config)
    {
        for (const auto& output : config.Outputs) props.GetOrAdd<float>(output.Name).Vector().assign(props.Size(), 77);
    }

    bool AllFinal(const std::vector<std::shared_ptr<DA::Run>>& runs)
    {
        return !runs.empty() && std::ranges::all_of(runs, [](const auto& run) { return run->Final.has_value(); });
    }

    double Milliseconds(const std::chrono::steady_clock::time_point started)
    {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    }
}

TEST(DescriptorAnalysisPluginGpuSmoke, EightDomainsAgreeWithTheCpuKdTreeColdWarmAndAutomatic)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize()) GTEST_SKIP() << "GLFW unavailable";
    Gpu gpu;
    auto& scene = gpu.H->Scene();
    const auto entities = MakeDomains(scene);
    // Per phase (cold, warm, automatic radius): the CPU k-d tree into "reference.*", then Vulkan into
    // "descriptor.*", every domain at once.
    std::vector<std::shared_ptr<DA::Run>> references, vulkan;
    std::vector<double> cpuMs, vulkanMs;
    double maxError = 0;
    unsigned phase = 0;
    auto started = std::chrono::steady_clock::now();
    const auto config = [&](const unsigned d, const char* prefix)
    { return Settings(entities[d - 1], D(d), prefix, phase == 2); };
    ASSERT_TRUE(gpu.Run([&]
    {
        if (references.empty())
        {
            started = std::chrono::steady_clock::now();
            for (unsigned d = 1; d <= 8; ++d)
            {
                auto c = config(d, "reference");
                c.Backend = DA::DescriptorBackend::CpuKDTree;
                Fill(Props(scene, entities[d - 1], D(d)), c); // deleted rows keep 77 in both columns
                references.push_back(DA::Start(c));
            }
            return false;
        }
        if (!AllFinal(references)) return false;
        if (vulkan.empty())
        {
            cpuMs.push_back(Milliseconds(started));
            for (unsigned d = 1; d <= 8; ++d) Fill(Props(scene, entities[d - 1], D(d)), config(d, "descriptor"));
            started = std::chrono::steady_clock::now();
            for (unsigned d = 1; d <= 8; ++d) vulkan.push_back(DA::Start(config(d, "descriptor")));
            return false;
        }
        if (!AllFinal(vulkan)) return false;
        vulkanMs.push_back(Milliseconds(started));
        for (unsigned d = 1; d <= 8; ++d)
        {
            SCOPED_TRACE(d);
            const auto& reference = *references[d - 1]->Final;
            const auto& result = *vulkan[d - 1]->Final;
            EXPECT_EQ(reference.Status, "applied") << reference.Message;
            EXPECT_EQ(result.Status, "applied") << result.Message;
            EXPECT_EQ(result.ActualBackend, "vulkan_lbvh");
            EXPECT_GT(result.GpuQueryBatches, 0u);
            if (phase > 0) EXPECT_TRUE(result.IndexReused);
            EXPECT_EQ(result.WrittenCount, reference.WrittenCount);
            EXPECT_FLOAT_EQ(result.MeanSpacing, reference.MeanSpacing);
            EXPECT_FLOAT_EQ(result.FeatureRadius, reference.FeatureRadius);
            auto& props = Props(scene, entities[d - 1], D(d));
            const auto expected = Read(props, config(d, "reference")), actual = Read(props, config(d, "descriptor"));
            for (std::size_t b = 0; b < DA::kBins; ++b)
            {
                for (std::size_t i = 0; i < actual[b].size(); ++i)
                {
                    const double error = std::abs(double(actual[b][i]) - double(expected[b][i]));
                    maxError = std::isfinite(error) ? std::max(maxError, error) : std::numeric_limits<double>::infinity();
                }
                EXPECT_EQ(actual[b][4], 77) << "a deleted row keeps its value";
                if (Half(D(d))) EXPECT_EQ(actual[b][5], 77);
            }
            EXPECT_EQ(std::as_const(props).Get<float>("keep")[0], 42);
        }
        // The last eight entries are the Vulkan runs': undone, the outputs hold 77 again; then redone.
        for (unsigned i = 0; i < 8; ++i) EXPECT_EQ(gpu.H->History().Undo().Status, R::EditorCommandHistoryStatus::Undone);
        for (unsigned d = 1; d <= 8; ++d)
            for (const auto& output : config(d, "descriptor").Outputs)
                EXPECT_EQ(std::as_const(Props(scene, entities[d - 1], D(d))).Get<float>(output.Name)[0], 77);
        for (unsigned i = 0; i < 8; ++i) EXPECT_EQ(gpu.H->History().Redo().Status, R::EditorCommandHistoryStatus::Redone);
        references.clear();
        vulkan.clear();
        return ++phase == 3;
    })) << "the frames ran out in phase " << phase;
    EXPECT_LE(maxError, 1e-5);
    ASSERT_EQ(vulkanMs.size(), 3u);
    if (const auto* output = std::getenv("INTRINSIC_DESCRIPTOR_BENCHMARK_OUTPUT"))
    {
        const Json json{{"benchmark_id", "geometry.point_lbvh.descriptor_runtime_smoke"},
                        {"method", "geometry.point_lbvh"},
                        {"backend", "gpu_vulkan_compute"},
                        {"dataset", "builtin.descriptor_samples.eight_domains.seed241"},
                        {"commit", "local-dev"},
                        {"metrics", {{"runtime_ms", vulkanMs[1]}, {"quality_error_linf", maxError}}},
                        {"diagnostics",
                         {{"runner", "IntrinsicPointLBVHGpuTests"},
                          {"mode", "smoke"},
                          {"cpu_reference_total_ms", cpuMs[1]},
                          {"vulkan_cold_total_ms", vulkanMs[0]},
                          {"vulkan_warm_total_ms", vulkanMs[1]},
                          {"vulkan_automatic_radius_total_ms", vulkanMs[2]},
                          {"warmup_iterations", 1},
                          {"measured_iterations", 1},
                          {"cpu_scale_spfh_fpfh", true}}},
                        {"status", ::testing::Test::HasFailure() ? "failed" : "passed"}};
        std::ofstream stream(output);
        ASSERT_TRUE(stream.good());
        stream << json.dump(2) << '\n';
        ASSERT_TRUE(stream.good());
    }
}

TEST(DescriptorAnalysisPluginGpuSmoke, StaleInputsACancelBetweenStagesAndRadiusOverflowWriteNothing)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize()) GTEST_SKIP() << "GLFW unavailable";
    Gpu gpu;
    auto& scene = gpu.H->Scene();
    const auto entity = MakeDomains(scene).back();
    auto& props = Props(scene, entity, D::PointCloudPoint);
    const auto settings = [&](const char* prefix) { return Settings(entity, D::PointCloudPoint, prefix, false); };
    std::shared_ptr<DA::Run> stale, cancelled, overflow;
    bool cancelRequested = false;
    ASSERT_TRUE(gpu.Run([&]
    {
        if (!stale)
        {
            stale = DA::Start(settings("stale"));
            props.Get<glm::vec3>("directions")[0] += .1f; // before any GPU recording
        }
        if (stale->Final && !cancelled) cancelled = DA::Start(settings("cancelled"));
        if (cancelled && !cancelRequested)
        {
            // Between stages: once this run's GPU stage is active, its scale stage has completed.
            const auto jobs = gpu.H->Jobs();
            if (std::ranges::any_of(jobs, [&](const auto& job)
                {
                    return job.Name == "Descriptor radius support (Vulkan)" &&
                        job.Identity.Run == cancelled->Handle.Token && R::IsActiveEditorJobState(job.State);
                }))
                cancelRequested = DA::Cancel(*cancelled);
        }
        if (!cancelled || !cancelled->Final) return false;
        if (!overflow)
        {
            // 1029 coincident samples and one far one: an uncapped support of 1028 exceeds the capacity of 1024.
            props.Resize(1030);
            props.Get<glm::vec3>("samples").Vector().assign(1030, glm::vec3(0));
            props.Get<glm::vec3>("samples")[1029] = {10, 0, 0};
            props.Get<glm::vec3>("directions").Vector().assign(1030, glm::vec3(0, 0, 1));
            auto c = settings("overflow");
            c.GpuRadiusCapacity = 1024;
            c.MaxNeighbors = 0;
            overflow = DA::Start(c);
        }
        return overflow->Final.has_value();
    })) << "the frames ran out";
    EXPECT_EQ(stale->Final->Status, "stale") << stale->Final->Message;
    EXPECT_TRUE(cancelRequested);
    EXPECT_EQ(cancelled->Final->Status, "cancelled") << cancelled->Final->Message;
    EXPECT_EQ(overflow->Final->Status, "failed") << overflow->Final->Message;
    EXPECT_NE(overflow->Final->Message.find("overflow"), std::string::npos) << overflow->Final->Message;
    EXPECT_GT(overflow->Final->MaximumNeighbors, 1024u);
    EXPECT_GT(overflow->Final->GpuQueryBatches, 0u);
    for (const char* prefix : {"stale", "cancelled", "overflow"})
        for (const auto& output : settings(prefix).Outputs) EXPECT_FALSE(props.Exists(output.Name)) << output.Name;
    EXPECT_FALSE(gpu.H->History().CanUndo());
}

TEST(DescriptorAnalysisPluginGpuSmoke, ALowestIdCapServesADenseRadius)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize()) GTEST_SKIP() << "GLFW unavailable";
    Gpu gpu;
    auto& scene = gpu.H->Scene();
    const auto entity = MakeDomains(scene).back();
    auto& props = Props(scene, entity, D::PointCloudPoint);
    props.Resize(1030);
    auto positions = props.Get<glm::vec3>("samples"), normals = props.Get<glm::vec3>("directions");
    for (unsigned i = 0; i < 1030; ++i)
    {
        positions[i] = {.001f * float(i % 31), .001f * float(i / 31), .0001f * float(i % 7)};
        normals[i] = {.01f * float(i % 13), .01f * float(i % 17), 1};
    }
    positions[1029] = {10, 0, 0};
    auto c = Settings(entity, D::PointCloudPoint, "dense", false);
    c.MaxNeighbors = 1;
    c.GpuRadiusCapacity = 1;
    auto referenceConfig = c;
    referenceConfig.Backend = DA::DescriptorBackend::CpuKDTree;
    referenceConfig.Outputs = DA::MakeOutputs(D::PointCloudPoint, "dense_reference");
    std::shared_ptr<DA::Run> reference, vulkan;
    ASSERT_TRUE(gpu.Run([&]
    {
        if (!reference)
        {
            Fill(props, referenceConfig); // deleted rows keep 77 in both columns
            reference = DA::Start(referenceConfig);
        }
        if (!reference->Final) return false;
        if (!vulkan)
        {
            Fill(props, c);
            vulkan = DA::Start(c);
        }
        return vulkan->Final.has_value();
    })) << "the frames ran out";
    ASSERT_EQ(reference->Final->Status, "applied") << reference->Final->Message;
    ASSERT_EQ(vulkan->Final->Status, "applied") << vulkan->Final->Message;
    EXPECT_EQ(vulkan->Final->ActualBackend, "vulkan_lbvh");
    EXPECT_GT(vulkan->Final->MaximumNeighbors, 1024u);
    const auto expected = Read(props, referenceConfig), actual = Read(props, c);
    EXPECT_GT(expected[5][0], 0);
    for (std::size_t b = 0; b < DA::kBins; ++b)
        for (std::size_t i = 0; i < actual[b].size(); ++i) EXPECT_NEAR(actual[b][i], expected[b][i], 1e-5) << b << " " << i;
    EXPECT_EQ(gpu.H->History().UndoCount(), 2u) << "the reference's entry, then the Vulkan run's";
    EXPECT_TRUE(gpu.H->History().Undo().Succeeded());
    for (const auto& output : c.Outputs) EXPECT_EQ(std::as_const(props).Get<float>(output.Name)[0], 77);
    EXPECT_TRUE(gpu.H->History().Redo().Succeeded());
}

TEST(DescriptorAnalysisPluginGpuSmoke, AnUnloadDuringAChainEndsTheRunOnceAndAReloadRunsAgain)
{
    // The drain cancels the run's stages (a later stage the host refused while draining would end it as failed):
    // the run ends once, writes nothing, and the unload completes; after a reload the same settings run again.
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize()) GTEST_SKIP() << "GLFW unavailable";
    Gpu gpu;
    auto& scene = gpu.H->Scene();
    const auto entity = MakeDomains(scene).back();
    auto& props = Props(scene, entity, D::PointCloudPoint);
    const auto c = Settings(entity, D::PointCloudPoint, "unloaded", false);
    std::shared_ptr<DA::Run> run, again;
    std::optional<R::AgentOperationOutcome> unload, load;
    R::AgentOperationOutcome unloaded{}, loaded{};
    bool unloadFinished = false, loadFinished = false, toolsGone = false;
    const auto poll = [&](std::optional<R::AgentOperationOutcome>& call, R::AgentOperationOutcome& result)
    {
        if (!call->Continuation)
        {
            result = *call;
            return true;
        }
        return call->Continuation(gpu.H->Context, result);
    };
    ASSERT_TRUE(gpu.Run([&]
    {
        if (!run) run = DA::Start(c);
        if (!unload)
        {
            const auto jobs = gpu.H->Jobs();
            if (std::ranges::any_of(jobs, [&](const auto& job)
                {
                    return job.Name == "Descriptor radius support (Vulkan)" && job.Identity.Run == run->Handle.Token &&
                        R::IsActiveEditorJobState(job.State);
                }))
                unload = R::InvokeAgentOperation(gpu.H->Registry, "plugin_unload", gpu.H->Context, "{}", false);
            return false;
        }
        if (!unloadFinished)
        {
            unloadFinished = poll(unload, unloaded);
            toolsGone = unloadFinished && gpu.H->Registry.Find("descriptor_analysis") == nullptr;
            if (unloadFinished) unload->Continuation = {}; // answered: released, as the agent server does
            return false;
        }
        if (!run->Final) return false;
        // Then a reload, and the same settings run again on Vulkan.
        if (!load)
            load = R::InvokeAgentOperation(gpu.H->Registry, "plugin_load", gpu.H->Context,
                                           R"({"path":"descriptor_analysis"})", false);
        if (!loadFinished)
        {
            loadFinished = poll(load, loaded);
            if (loadFinished) load->Continuation = {};
            return false;
        }
        if (!again) again = DA::Start(c);
        return again->Final.has_value();
    })) << "the frames ran out";
    EXPECT_FALSE(unloaded.IsError) << unloaded.Text;
    EXPECT_TRUE(toolsGone) << "the plugin's tools went with it";
    ASSERT_TRUE(run->Final);
    EXPECT_TRUE(run->Final->Status == "cancelled" || run->Final->Status == "failed") << run->Final->Status;
    EXPECT_FALSE(loaded.IsError) << loaded.Text;
    ASSERT_TRUE(again && again->Final);
    EXPECT_EQ(again->Final->Status, "applied") << again->Final->Message;
    EXPECT_EQ(again->Final->ActualBackend, "vulkan_lbvh");
    EXPECT_EQ(gpu.H->History().UndoCount(), 1u) << "only the run after the reload wrote";
    for (const auto& output : c.Outputs) EXPECT_TRUE(props.Exists(output.Name)) << output.Name;
}
