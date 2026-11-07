// ARCH-022 Q90: the density weights plugin's contract, in an engine composed as the Sandbox composes it (in-process
// startup plugin, so it runs in static builds too): its section, its MCP tools, its Jobs window rows, every point
// domain, kernel and mode on both CPU backends with one undo entry, the automatic support radius, its stale and
// cancelled runs, scalar storage with checked conversion, the Vulkan backend's admission texts, the latest result it
// keeps for its window, and its window. Replaces the engine's Test.DensityWeightOperations (its cases of shared
// editor machinery moved to Test.OutlierAnalysis, the remaining user of that machinery).
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>

#include "PointDomainFixture.hpp"
#include "DensityWeightsHarness.hpp"
#include "PluginWindowHarness.hpp"

import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.Runtime.EditorUiHost;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.GeometryProperty.Types;
import Extrinsic.Runtime.SelectionController;
import Geometry.PointCloud.Kernels;
import Geometry.PointLBVH;

namespace
{
    using namespace Intrinsic::Tests::DensityWeights;
    using D = R::GeometryElementDomain;
    using K = Geometry::PropertyValueKind;
    using B = DW::DensityWeightBackend;
    namespace GS = Extrinsic::ECS::Components::GeometrySources;
    namespace Kernels = Geometry::PointCloud::Kernels;
    using Intrinsic::Tests::PointDomainProperties;

    // A 3-wide grid with a little depth and an unrelated `keep`; a point cloud also gets a deleted NaN row 4.
    entt::entity Make(Extrinsic::ECS::Scene::Registry& scene, const D domain)
    {
        const auto entity = Intrinsic::Tests::MakePointDomainSource(scene, domain);
        auto& props = PointDomainProperties(scene, entity, domain);
        auto samples = props.GetOrAdd<glm::vec3>("samples");
        for (std::size_t i = 0; i < samples.Size(); ++i)
            samples[i] = {float(i % 3), float(i / 3), .15f * float(int(i % 3) - 1)};
        std::ranges::fill(props.GetOrAdd<float>("keep").Vector(), 42.f);
        if (domain == D::PointCloudPoint)
        {
            props.GetOrAdd<bool>("v:deleted")[4] = true;
            samples[4] = {std::numeric_limits<float>::quiet_NaN(), 0, 0};
        }
        return entity;
    }

    DW::Config Settings(const entt::entity entity, const D domain)
    {
        return {.StableEntityId = R::SelectionController::ToStableEntityId(entity),
                .Positions = {.Domain = domain, .Name = "samples", .ValueKind = K::Vec3},
                .Weights = {.Domain = domain, .Name = "weights", .ValueKind = K::Float},
                .SupportRadius = 2};
    }

    const char* DeletedName(const D d)
    {
        return d == D::MeshFace ? "f:deleted" : (d == D::MeshEdge || d == D::GraphEdge) ? "e:deleted" : "v:deleted";
    }

    bool Usable(const std::string& payload) { return R::ValidateDerivedSection<DW::kSection>(payload, {}, "test").Usable(); }

    // The live samples of a point cloud entity and their slots.
    std::pair<std::vector<glm::vec3>, std::vector<std::size_t>> Live(const Geometry::PropertySet& props)
    {
        const auto samples = props.Get<glm::vec3>("samples").Vector();
        const auto deleted = props.Get<bool>("v:deleted").Vector();
        std::pair<std::vector<glm::vec3>, std::vector<std::size_t>> live;
        for (std::size_t i = 0; i < samples.size(); ++i)
            if (!deleted[i])
            {
                live.first.push_back(samples[i]);
                live.second.push_back(i);
            }
        return live;
    }

    const std::string kFloatOnly =
        "Vulkan scalar analysis supports float outputs only; select the CPU backend for other scalar storage.";
    const std::string kDevice = "Vulkan scalar analysis requires an operational float64 device and framed spatial cache/jobs.";
    const std::string kUnpublished = "Density weights was cancelled or its source became stale; nothing was applied.";
}

// The section keeps the built-in method's name, schema id and default payload (saved configs load), its bounds and
// tokens, and the texts of its hand-written codec where a single field is wrong.
TEST(DensityWeightsPlugin, TheSectionKeepsItsNamePayloadBoundsAndTexts)
{
    EXPECT_EQ(DW::kSectionName, "sandbox.density_weights");
    EXPECT_EQ(DW::kSectionSchemaId, "intrinsic.runtime.sandbox.density_weights");
    EXPECT_EQ(Json::parse(R::SerializeDerivedSection<DW::kSection>({})),
              Json::parse(R"({"backend":"cpu_kdtree","entity":0,"gpu_query_batch_size":4096,"gpu_radius_capacity":256,
                  "kernel":"theta_lop","mode":"direct","positions":{"domain":"Unknown","kind":"vec3","name":"v:position"},
                  "support_radius":0.0,"weights":{"domain":"Unknown","kind":"float","name":"density_weight"}})"))
        << "the payload the removed engine codec wrote";
    for (const auto* payload : {R"({"backend":"vulkan"})", R"({"support_radius":-1})", R"({"support_radius":1e100})",
                                R"({"kernel":"unknown"})", R"({"mode":"unknown"})", R"({"gpu_query_batch_size":0})",
                                R"({"gpu_radius_capacity":0})", R"({"gpu_radius_capacity":1025})", R"({"unknown":1})"})
        EXPECT_FALSE(Usable(payload)) << payload;
    for (const auto* payload : {R"({"support_radius":0})", R"({"support_radius":1e-310})",
                                R"({"gpu_radius_capacity":1024})", R"({"gpu_query_batch_size":16384})"})
        EXPECT_TRUE(Usable(payload)) << payload;
    const auto refusal = [](const std::string& payload)
    {
        const auto result = R::ValidateDerivedSection<DW::kSection>(payload, {}, "test");
        return result.Diagnostics.empty() ? std::string{} : result.Diagnostics.front().Message;
    };
    EXPECT_EQ(refusal(R"({"backend":"vulkan"})"), "Unknown density weight backend.");
    EXPECT_EQ(refusal(R"({"kernel":"unknown"})"), "Unknown radial kernel.");
    EXPECT_EQ(refusal(R"({"mode":"unknown"})"), "Unknown density weight mode.");
    EXPECT_EQ(refusal(R"({"gpu_radius_capacity":0})"), DW::kCapacityRefusal);
    EXPECT_EQ(refusal(R"({"gpu_query_batch_size":0})"), DW::kCapacityRefusal);
    DW::Config same{};
    same.Weights.Name = same.Positions.Name;
    EXPECT_EQ(refusal(R::SerializeDerivedSection<DW::kSection>(same)),
              "Position and weight bindings need distinct canonical vec3/float properties on the same domain.");
    DW::Config mixed{};
    mixed.Weights.Domain = D::GraphEdge;
    EXPECT_FALSE(Usable(R::SerializeDerivedSection<DW::kSection>(mixed)));
    EXPECT_EQ(DW::Config{}.SupportRadius, 0) << "RUNTIME-291: automatic by default";
    for (const auto kind : {K::Bool, K::Int32, K::UInt32, K::UInt64, K::Float, K::Double})
    {
        DW::Config config{.StableEntityId = 7u, .SupportRadius = .75, .Kernel = Kernels::KernelType::Gaussian,
                          .Mode = Kernels::DensityWeightMode::Reciprocal};
        config.Weights.ValueKind = kind;
        auto document = HeadlessConfig();
        R::SetDerivedSection<DW::kSection>(document, config);
        const auto decoded = R::GetDerivedSection<DW::kSection>(document);
        ASSERT_TRUE(decoded) << int(kind);
        EXPECT_EQ(R::SerializeDerivedSection<DW::kSection>(*decoded), R::SerializeDerivedSection<DW::kSection>(config));
    }
}

// The tool answers with the radius and the weight range; the run is a Jobs window row whose output is a scalar field.
TEST(DensityWeightsPlugin, TheToolAnswersWithTheRadiusAndTheRunIsAJobsWindowRow)
{
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = Make(scene, D::PointCloudPoint);
    ASSERT_NE(harness.Registry.Find("density_weights"), nullptr);
    auto config = Settings(entity, D::PointCloudPoint);
    for (const auto backend : {B::CpuKDTree, B::CpuLBVH})
    {
        SCOPED_TRACE(DW::ToString(backend));
        config.Backend = backend;
        ASSERT_TRUE(harness.Apply(config));
        const auto reply = harness.Call("density_weights");
        ASSERT_FALSE(reply.IsError) << reply.ErrorCode << ": " << reply.Text;
        const auto result = Json::parse(reply.Text);
        EXPECT_EQ(result["status"], "applied");
        EXPECT_EQ(result["actual_backend"], DW::ToString(backend));
        EXPECT_EQ(result["live_count"], result["slot_count"].get<std::size_t>() - 1);
        EXPECT_EQ(result["written_count"], result["live_count"]);
        EXPECT_EQ(result["support_radius"], 2.0);
        EXPECT_GE(result["query_radius"].get<double>(), 2.0);
        EXPECT_GE(result["min_weight"].get<float>(), 1.f);
        EXPECT_GT(result["max_weight"].get<float>(), 1.f);
    }
    const auto jobs = harness.Jobs();
    const auto row = std::ranges::find_if(jobs, [](const auto& job) { return job.Name == "Density weights"; });
    ASSERT_NE(row, jobs.end()) << "plugin runs are rows of the shell's Jobs window";
    EXPECT_EQ(row->Identity.OutputSemantic, R::GeometryPresentationSlotSemantic::ScalarField);
    EXPECT_EQ(row->Identity.OutputName, "weights");
    EXPECT_EQ(harness.Settled(config), "");
    const auto ready = Json::parse(harness.Call("density_weights_preview").Text);
    EXPECT_EQ(ready["ready"], true) << ready.dump();
}

TEST(DensityWeightsPlugin, EveryDomainKernelAndModeKeepsDeletedRowsAndTheBackendsAgree)
{
    for (unsigned d = 1; d <= 8; ++d)
        for (unsigned kernel = 0; kernel < 3; ++kernel)
            for (unsigned mode = 0; mode < 2; ++mode)
            {
                SCOPED_TRACE(d);
                SCOPED_TRACE(kernel);
                SCOPED_TRACE(mode);
                Harness harness{};
                auto& scene = harness.Scene();
                const auto entity = Make(scene, D(d));
                auto config = Settings(entity, D(d));
                config.Kernel = Kernels::KernelType(kernel);
                config.Mode = Kernels::DensityWeightMode(mode);
                auto& props = PointDomainProperties(scene, entity, D(d));
                const auto size = props.Size();
                const bool half = D(d) == D::MeshHalfedge || D(d) == D::GraphHalfedge;
                if (half) scene.Raw().get<GS::Edges>(entity).Properties.GetOrAdd<bool>("e:deleted")[1] = true;
                else props.GetOrAdd<bool>(DeletedName(D(d)))[2] = true;
                props.GetOrAdd<float>("weights").Vector().assign(size, 77);
                const auto positionRevision = std::as_const(props).Get<glm::vec3>("samples").Revision();
                EXPECT_EQ(harness.Settled(config), "");
                const auto catalog = harness.Editing().InputCatalog(config.StableEntityId, 1u);
                EXPECT_TRUE(std::ranges::any_of(catalog.Entries, [&](const auto& e) { return e.Ref == config.Positions; }));
                const auto reference = harness.Wait(*DW::Start(config, false));
                ASSERT_EQ(reference.Status, "applied") << reference.Message;
                EXPECT_EQ(reference.ActualBackend, "cpu_kdtree");
                const auto expected = std::as_const(props).Get<float>("weights").Vector();
                EXPECT_EQ(expected[2], 77) << "a deleted row keeps its value";
                if (half) EXPECT_EQ(expected[3], 77);
                EXPECT_EQ(props.Size(), size);
                EXPECT_EQ(std::as_const(props).Get<glm::vec3>("samples").Revision(), positionRevision);
                props.Get<float>("keep")[0] = 99; // an unrelated edit does not make the entry stale
                ASSERT_TRUE(harness.History().Undo().Succeeded());
                EXPECT_EQ(std::as_const(props).Get<float>("weights").Vector(), std::vector<float>(size, 77));
                ASSERT_TRUE(harness.History().Redo().Succeeded());
                EXPECT_EQ(std::as_const(props).Get<float>("keep")[0], 99);
                config.Backend = B::CpuLBVH;
                const auto indexed = harness.Wait(*DW::Start(config, false));
                ASSERT_EQ(indexed.Status, "applied") << indexed.Message;
                EXPECT_EQ(indexed.ActualBackend, "cpu_lbvh");
                EXPECT_EQ(indexed.WrittenCount, reference.WrittenCount);
                EXPECT_EQ(indexed.Diagnostics.NeighborContributionCount, reference.Diagnostics.NeighborContributionCount);
                EXPECT_EQ(std::as_const(props).Get<float>("weights").Vector(), expected);
                EXPECT_TRUE(harness.Wait(*DW::Start(config, false)).IndexReused);
            }
}

// RUNTIME-291: support_radius 0 derives the radius from the selected positions with one rule for mesh vertices, graph
// nodes and point clouds; scaling the model by 1000 scales the radius and keeps the weights.
TEST(DensityWeightsPlugin, TheAutomaticSupportRadiusIsScaleInvariantOnMeshGraphAndCloud)
{
    // Irregular dyadic positions: x1000 is exact in float and no pair sits on the support boundary.
    constexpr std::array<glm::vec3, 4> base{{{0, 0, 0}, {1.25f, .5f, 0}, {.25f, 2.75f, .5f}, {2.5f, 3, -.75f}}};
    for (unsigned kernel = 0; kernel < 3; ++kernel)
    {
        SCOPED_TRACE(kernel);
        double firstRadius = 0;
        std::vector<float> firstWeights;
        for (const auto domain : {D::MeshVertex, D::GraphNode, D::PointCloudPoint})
            for (const auto backend : {B::CpuKDTree, B::CpuLBVH})
            {
                SCOPED_TRACE(int(domain));
                SCOPED_TRACE(int(backend));
                double radius[2]{};
                std::vector<float> weights[2];
                for (const int scaled : {0, 1})
                {
                    Harness harness{};
                    auto& scene = harness.Scene();
                    const auto entity = Make(scene, domain);
                    auto& props = PointDomainProperties(scene, entity, domain);
                    for (std::size_t i = 0; i < base.size(); ++i)
                        props.Get<glm::vec3>("samples")[i] = base[i] * (scaled ? 1000.f : 1.f);
                    props.GetOrAdd<float>("weights").Vector().assign(props.Size(), 77);
                    auto config = Settings(entity, domain);
                    config.SupportRadius = 0;
                    config.Backend = backend;
                    config.Kernel = Kernels::KernelType(kernel);
                    EXPECT_EQ(harness.Settled(config), "");
                    const auto result = harness.Wait(*DW::Start(config, false));
                    ASSERT_EQ(result.Status, "applied") << result.Message;
                    ASSERT_GT(result.SupportRadius, 0);
                    EXPECT_GE(double(result.QueryRadius), result.SupportRadius);
                    EXPECT_GT(result.MaxWeight, 1.f);
                    radius[scaled] = result.SupportRadius;
                    const auto all = std::as_const(props).Get<float>("weights").Vector();
                    weights[scaled].assign(all.begin(), all.begin() + base.size());
                    if (domain == D::PointCloudPoint) EXPECT_EQ(all[4], 77); // the deleted row stays untouched
                    // The reported radius is the one the kernel used.
                    auto manual = config;
                    manual.SupportRadius = result.SupportRadius;
                    manual.Weights.Name = "manual";
                    ASSERT_EQ(harness.Wait(*DW::Start(manual, false)).Status, "applied");
                    const auto again = std::as_const(props).Get<float>("manual").Vector();
                    EXPECT_EQ(std::vector<float>(again.begin(), again.begin() + base.size()), weights[scaled]);
                    // An automatic radius that exceeds the candidate budget fails instead of guessing.
                    auto tight = config;
                    tight.GpuRadiusCapacity = 1;
                    const auto refused = harness.Wait(*DW::Start(tight, false));
                    EXPECT_EQ(refused.Status, "invalid"); // as on Vulkan
                    EXPECT_NE(refused.Message.find("Automatic support radius"), std::string::npos) << refused.Message;
                    EXPECT_EQ(std::as_const(props).Get<float>("weights").Vector(), all);
                }
                EXPECT_NEAR(radius[1] / radius[0], 1000.0, 1e-9);
                for (std::size_t i = 0; i < base.size(); ++i) EXPECT_FLOAT_EQ(weights[1][i], weights[0][i]) << i;
                if (firstWeights.empty())
                {
                    firstRadius = radius[0];
                    firstWeights = weights[0];
                }
                EXPECT_EQ(radius[0], firstRadius);
                EXPECT_EQ(weights[0], firstWeights);
            }
    }
}

TEST(DensityWeightsPlugin, AChangedInputOrOutputOrACancelEndsTheRunWithoutAWrite)
{
    // 0 unrelated edit (applies), 1 positions, 2 a deletion, 3 the output written, 4 cancel.
    const char* expected[]{"applied", "stale", "stale", "stale", "cancelled"};
    for (unsigned change = 0; change < 5; ++change)
    {
        SCOPED_TRACE(change);
        Harness harness{};
        auto& scene = harness.Scene();
        const auto entity = Make(scene, D::MeshVertex);
        auto& props = PointDomainProperties(scene, entity, D::MeshVertex);
        const auto run = DW::Start(Settings(entity, D::MeshVertex), false);
        ASSERT_FALSE(run->Final) << run->Final->Message;
        EXPECT_EQ(run->Pending.Message, "Density weights queued.") << "the removed operation's text";
        switch (change)
        {
        case 0: props.Get<float>("keep")[0] = 99; break;
        case 1: props.Get<glm::vec3>("samples")[0].x += 1; break;
        case 2: props.GetOrAdd<bool>("v:deleted")[0] = true; break;
        case 3: props.GetOrAdd<float>("weights")[0] = 77; break;
        case 4: EXPECT_TRUE(DW::Cancel(*run)); break;
        }
        EXPECT_EQ(harness.Wait(*run).Status, expected[change]) << run->Final->Message;
        if (change != 0) EXPECT_EQ(run->Final->Message, kUnpublished) << "the removed operation's text";
        EXPECT_EQ(harness.History().CanUndo(), change == 0);
        EXPECT_EQ(props.Exists("weights"), change == 0 || change == 3);
        if (change == 3) EXPECT_EQ(std::as_const(props).Get<float>("weights")[0], 77);
    }
}

// One live sample has weight 1 on both CPU backends; invalid outputs, a radius beyond the LBVH limits, subnormal
// coordinates on Vulkan and a non-finite position refuse without touching the output.
TEST(DensityWeightsPlugin, OneSampleAndTheBackendPreflightPreserveOutputs)
{
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = Make(scene, D::PointCloudPoint);
    auto config = Settings(entity, D::PointCloudPoint);
    auto& props = PointDomainProperties(scene, entity, D::PointCloudPoint);
    props.Resize(1);
    props.Get<glm::vec3>("samples")[0] = {0, 0, 0};
    for (const auto backend : {B::CpuKDTree, B::CpuLBVH})
    {
        config.Backend = backend;
        ASSERT_EQ(harness.Wait(*DW::Start(config, false)).Status, "applied") << DW::ToString(backend);
        EXPECT_EQ(std::as_const(props).Get<float>("weights")[0], 1);
        ASSERT_TRUE(harness.History().Undo().Succeeded());
        EXPECT_FALSE(props.Exists("weights"));
    }
    props.GetOrAdd<float>("weights")[0] = 77;
    for (const auto* name : {"samples", "v:deleted"})
    {
        auto bad = config;
        bad.Weights.Name = name;
        EXPECT_NE(harness.Settled(bad), "") << name;
    }
    auto unrelated = config;
    unrelated.Weights.Name = "h:connectivity";
    EXPECT_EQ(harness.Settled(unrelated), "");
    config.Backend = B::CpuLBVH;
    config.SupportRadius = double(Geometry::PointLBVH::CoordinateLimit);
    EXPECT_EQ(harness.Settled(config),
              "LBVH requires the spatial cache, at most 2^24 points, and coordinates/expanded radius within 1e18.");
    config.SupportRadius = 1;
    config.Backend = B::VulkanLBVH;
    for (const float value : {std::numeric_limits<float>::denorm_min(), -std::numeric_limits<float>::denorm_min()})
    {
        props.Get<glm::vec3>("samples")[0] = {value, 0, 0};
        EXPECT_NE(harness.Settled(config).find("subnormal"), std::string::npos);
    }
    config.Backend = B::CpuKDTree;
    props.Get<glm::vec3>("samples")[0] = {std::numeric_limits<float>::quiet_NaN(), 0, 0};
    EXPECT_NE(harness.Wait(*DW::Start(config, false)).Status, "applied");
    EXPECT_EQ(std::as_const(props).Get<float>("weights")[0], 77);
    props.Get<bool>("v:deleted")[0] = true;
    EXPECT_EQ(harness.Settled(config), "Density weights require at least one live sample.");
}

// Without a device here, the Vulkan backend answers the GPU admission through both tools; the CPU backends take any
// scalar storage.
TEST(DensityWeightsPlugin, TheVulkanBackendAnswersTheGpuAdmissionThroughBothTools)
{
    for (const double radius : {2.0, 0.0})
    {
        SCOPED_TRACE(radius);
        Harness harness{};
        auto& scene = harness.Scene();
        const auto entity = Make(scene, D::PointCloudPoint);
        harness.Frame(60);
        const auto check = [&](const DW::Config& config, const std::string& expected)
        {
            ASSERT_TRUE(harness.Apply(config));
            EXPECT_EQ(harness.Settled(config), expected);
            const auto preview = Json::parse(harness.Call("density_weights_preview").Text);
            EXPECT_EQ(preview["ready"], false);
            EXPECT_EQ(preview["reason"], expected);
            const auto reply = harness.Call("density_weights");
            ASSERT_TRUE(reply.IsError) << reply.Text;
            const auto result = Json::parse(reply.Text);
            EXPECT_EQ(result["status"], "invalid");
            EXPECT_EQ(result["message"], expected);
        };
        auto wide = Settings(entity, D::PointCloudPoint);
        wide.SupportRadius = radius;
        wide.Backend = B::VulkanLBVH;
        wide.Weights.ValueKind = K::Double;
        check(wide, kFloatOnly);
        auto device = Settings(entity, D::PointCloudPoint);
        device.SupportRadius = radius;
        device.Backend = B::VulkanLBVH;
        check(device, kDevice);
        EXPECT_FALSE(harness.History().CanUndo());
        for (const auto backend : {B::CpuKDTree, B::CpuLBVH})
        {
            auto cpu = wide;
            cpu.Backend = backend;
            EXPECT_EQ(harness.Wait(*DW::Start(cpu, false)).Status, "applied") << DW::ToString(backend);
        }
    }
}

// RUNTIME-291: the radius-independent Vulkan refusals come before the automatic analysis, which would refuse these
// coincident samples first; the CPU backends report the analysis.
TEST(DensityWeightsPlugin, VulkanRefusesBeforeTheAutomaticRadiusIsDerived)
{
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = Make(scene, D::PointCloudPoint);
    auto& props = PointDomainProperties(scene, entity, D::PointCloudPoint);
    props.Get<glm::vec3>("samples").Vector().assign(props.Size(), glm::vec3(1, 2, 3));
    auto config = Settings(entity, D::PointCloudPoint);
    config.SupportRadius = 0;
    config.Backend = B::VulkanLBVH;
    EXPECT_EQ(harness.Settled(config), kDevice) << "the readiness never derives the radius";
    const auto gpu = DW::Start(config, true);
    ASSERT_TRUE(gpu->Final);
    EXPECT_EQ(gpu->Final->Message, kDevice);
    config.Backend = B::CpuKDTree;
    const auto cpu = harness.Wait(*DW::Start(config, false));
    EXPECT_EQ(cpu.Status, "invalid");
    EXPECT_NE(cpu.Message.find("Automatic support radius"), std::string::npos) << cpu.Message;
}

// The publication changes the output's revision and marks no geometry dirty; undo and redo do the same, and a
// replaced storage of an input or the output makes them stale without a notification.
TEST(DensityWeightsPlugin, PublicationUndoAndRedoKeepTheGeometryAndAReplacedStorageMakesThemStale)
{
    namespace Dirty = Extrinsic::ECS::Components::DirtyTags;
    for (unsigned replacement = 0; replacement < 4; ++replacement)
    {
        SCOPED_TRACE(replacement);
        Harness harness{};
        auto& scene = harness.Scene();
        const auto entity = Make(scene, D::PointCloudPoint);
        auto& props = PointDomainProperties(scene, entity, D::PointCloudPoint);
        props.GetOrAdd<float>("weights").Vector().assign(props.Size(), 77);
        const auto clear = [&] { scene.Raw().remove<Dirty::GpuDirty, Dirty::DirtyVertexAttributes>(entity); };
        const auto clean = [&] { return !scene.Raw().any_of<Dirty::GpuDirty, Dirty::DirtyVertexAttributes>(entity); };
        auto revision = props.FindPropertyRevision("weights");
        const auto advanced = [&]
        {
            const auto current = props.FindPropertyRevision("weights");
            const bool changed = current != revision;
            revision = current;
            return changed;
        };
        clear();
        ASSERT_EQ(harness.Wait(*DW::Start(Settings(entity, D::PointCloudPoint), false)).Status, "applied");
        EXPECT_TRUE(advanced());
        EXPECT_TRUE(clean());
        const auto computed = std::as_const(props).Get<float>("weights").Vector();
        ASSERT_TRUE(harness.History().Undo().Succeeded());
        EXPECT_TRUE(advanced());
        EXPECT_TRUE(clean());
        EXPECT_EQ(std::as_const(props).Get<float>("weights").Vector(), std::vector<float>(props.Size(), 77));
        ASSERT_TRUE(harness.History().Redo().Succeeded());
        EXPECT_TRUE(advanced());
        EXPECT_TRUE(clean());
        EXPECT_EQ(std::as_const(props).Get<float>("weights").Vector(), computed);
        const auto replace = [&]<class T>(const char* name)
        {
            const auto values = std::as_const(props).Get<T>(name).Vector();
            auto old = props.Get<T>(name);
            props.Remove(old);
            props.GetOrAdd<T>(name).Vector() = values;
        };
        if (replacement == 0) replace.operator()<glm::vec3>("samples");
        else if (replacement == 1) replace.operator()<bool>("v:deleted");
        else if (replacement == 2) replace.operator()<float>("weights");
        else
        {
            auto old = props.Get<float>("weights");
            props.Remove(old);
            props.GetOrAdd<glm::vec3>("weights").Vector().assign(props.Size(), glm::vec3(17));
        }
        EXPECT_EQ(harness.History().Undo().Status, R::EditorCommandHistoryStatus::StaleEntity);
        EXPECT_TRUE(clean());
        if (replacement == 3) EXPECT_EQ(std::as_const(props).Get<glm::vec3>("weights")[0], glm::vec3(17));
        else EXPECT_EQ(std::as_const(props).Get<float>("weights").Vector(), computed);
    }
}

TEST(DensityWeightsPlugin, ScalarStoragePublishesWithCheckedConversionAndExactUndo)
{
    for (const auto kind : {K::Double, K::Int32, K::UInt32, K::UInt64, K::Bool})
    {
        SCOPED_TRACE(int(kind));
        Harness harness{};
        auto& scene = harness.Scene();
        const auto entity = Make(scene, D::PointCloudPoint);
        auto& props = PointDomainProperties(scene, entity, D::PointCloudPoint);
        auto config = Settings(entity, D::PointCloudPoint);
        config.Weights.ValueKind = kind;
        // Samples farther apart than the support each weigh exactly 1, which every storage holds; neighbours do not.
        config.SupportRadius = .5;
        const auto isolated = harness.Wait(*DW::Start(config, false));
        ASSERT_EQ(isolated.Status, "applied") << isolated.Message;
        EXPECT_EQ(R::DetectGeometryPropertyValueKind(props, "weights"), kind);
        EXPECT_EQ(harness.History().Undo().Status, R::EditorCommandHistoryStatus::Undone);
        EXPECT_FALSE(props.Exists("weights"));
        config.SupportRadius = 2;
        const auto close = harness.Wait(*DW::Start(config, false));
        EXPECT_EQ(close.Status, kind == K::Double ? "applied" : "invalid") << close.Message;
        if (kind != K::Double)
        {
            EXPECT_EQ(close.Message, "Output values are not exactly representable in the selected scalar storage.");
            EXPECT_FALSE(props.Exists("weights"));
        }
    }
}

// RUNTIME-348's guarantees for the plugin's own runs (the shared run book): the newest started run is the latest, a
// busy refusal answers for the other run, a dismiss clears it, and a result of an earlier editor session is gone while
// that session's run publishes nothing.
TEST(DensityWeightsPlugin, TheLatestRunIsKeptWhoeverStartedItAndEndsWithItsSession)
{
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = Make(scene, D::MeshVertex);
    auto& props = PointDomainProperties(scene, entity, D::MeshVertex);
    const auto config = Settings(entity, D::MeshVertex);
    const auto first = DW::Start(config, false);
    ASSERT_FALSE(first->Final);
    EXPECT_EQ(DW::Latest(), first) << "a started run is the latest at once";
    ASSERT_EQ(DW::Active().size(), 1u);
    const auto busy = DW::Start(config, false);
    ASSERT_TRUE(busy->Final);
    EXPECT_EQ(busy->Final->Status, "busy") << busy->Final->Message;
    EXPECT_EQ(DW::Latest(), first) << "a busy refusal answers for the other run";
    EXPECT_EQ(harness.Wait(*first).Status, "applied");
    EXPECT_TRUE(DW::Active().empty());
    auto missing = config;
    missing.StableEntityId = 0xFFFFFFF0u;
    const auto refused = DW::Start(missing, false);
    ASSERT_TRUE(refused->Final);
    EXPECT_EQ(refused->Final->Status, "invalid");
    EXPECT_EQ(refused->Final->Message, "Density-weight target entity is stale or missing.");
    EXPECT_EQ(DW::Latest(), refused);
    DW::Dismiss(first);
    EXPECT_EQ(DW::Latest(), refused) << "a dismiss of another run changes nothing";
    DW::Dismiss(refused);
    EXPECT_EQ(DW::Latest(), nullptr);

    auto other = config;
    other.Weights.Name = "weights_of_the_old_session";
    const auto outlived = DW::Start(other, false);
    ASSERT_FALSE(outlived->Final);
    const auto undo = harness.History().UndoCount();
    harness.Shell.Detach();
    harness.Shell.Attach(harness.Engine.Worlds(), harness.Engine.Services());
    (void)R::PrepareEditorWorkspaceSnapshotFrame(harness.Shell);
    EXPECT_EQ(DW::Latest(), nullptr) << "the same workspace, a new session";
    (void)harness.Wait(*outlived);
    EXPECT_EQ(DW::Latest(), nullptr) << "an old session's delivery does not come back";
    EXPECT_FALSE(props.Exists("weights_of_the_old_session"));
    EXPECT_EQ(harness.History().UndoCount(), undo);
}

TEST(DensityWeightsPlugin, TheJobsWindowCancelsARun)
{
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = Make(scene, D::MeshVertex);
    auto& props = PointDomainProperties(scene, entity, D::MeshVertex);
    const auto run = DW::Start(Settings(entity, D::MeshVertex), false);
    ASSERT_FALSE(run->Final);
    const auto jobs = harness.Jobs();
    const auto row = std::ranges::find_if(jobs, [](const auto& job) { return job.Name == "Density weights"; });
    ASSERT_NE(row, jobs.end());
    const auto commands = R::PrepareEditorProcessingCommands(harness.Shell);
    ASSERT_TRUE(R::ResolveEditorJobCancelReadiness(commands, *row).Enabled);
    EXPECT_EQ(R::CancelEditorJobRun(commands, *row), R::EditorJobCancelStatus::Requested);
    EXPECT_EQ(harness.Wait(*run).Status, "cancelled") << run->Final->Message;
    EXPECT_FALSE(props.Exists("weights"));
    EXPECT_FALSE(harness.History().CanUndo());
}

// A CPU run publishes exactly what the geometry functions return for the live samples (KD-tree:
// ComputeDensityWeights; LBVH: ComputeDensityWeightsFromNeighbors over the complete radius rows, self excluded, at the
// conservative query radius), with a given and the automatic radius.
TEST(DensityWeightsPlugin, CpuOutputsAreBitwiseTheGeometryFunctions)
{
    for (const auto backend : {B::CpuKDTree, B::CpuLBVH})
        for (const double radius : {0.0, 1.5})
            for (unsigned kernel = 0; kernel < 3; ++kernel)
            {
                SCOPED_TRACE(DW::ToString(backend));
                SCOPED_TRACE(radius);
                SCOPED_TRACE(kernel);
                Harness harness{};
                auto& scene = harness.Scene();
                const auto entity = Make(scene, D::PointCloudPoint);
                auto config = Settings(entity, D::PointCloudPoint);
                config.Backend = backend;
                config.SupportRadius = radius;
                config.Kernel = Kernels::KernelType(kernel);
                config.Mode = Kernels::DensityWeightMode(kernel % 2);
                const auto result = harness.Wait(*DW::Start(config, false));
                ASSERT_EQ(result.Status, "applied") << result.Message;
                const auto& props = PointDomainProperties(scene, entity, D::PointCloudPoint);
                const auto [live, slots] = Live(props);
                const double support = result.SupportRadius;
                ASSERT_GT(support, 0);
                if (radius > 0) EXPECT_EQ(support, radius);
                Kernels::DensityWeightResult expected;
                if (backend == B::CpuKDTree)
                    expected = Kernels::ComputeDensityWeights(live, support, config.Kernel, config.Mode);
                else
                {
                    const auto query = Kernels::ConservativeQueryRadius(support);
                    ASSERT_TRUE(query);
                    EXPECT_EQ(result.QueryRadius, *query);
                    Geometry::PointLBVH::Index index;
                    ASSERT_TRUE(index.Build(live));
                    std::vector<std::uint32_t> offsets{0u}, indices;
                    for (std::uint32_t i = 0; i < live.size(); ++i)
                    {
                        const auto found = index.Radius(live[i], *query, std::uint32_t(live.size() - 1), i, true);
                        ASSERT_EQ(found.Neighbors.size(), found.TotalCount);
                        std::vector<std::uint32_t> row;
                        for (const auto& neighbor : found.Neighbors) row.push_back(neighbor.Index);
                        std::ranges::sort(row);
                        indices.insert(indices.end(), row.begin(), row.end());
                        offsets.push_back(std::uint32_t(indices.size()));
                    }
                    expected = Kernels::ComputeDensityWeightsFromNeighbors(live, {offsets, indices}, support,
                                                                          config.Kernel, config.Mode);
                }
                ASSERT_TRUE(expected.Succeeded());
                const auto values = std::as_const(props).Get<float>("weights").Vector();
                for (std::size_t i = 0; i < live.size(); ++i) EXPECT_EQ(values[slots[i]], expected.Weights[i]) << slots[i];
            }
}

// The window runs the applied settings ("Compute compact weights"); "Show weights" then shows the published output
// without another run.
TEST(DensityWeightsPlugin, TheWindowRunsTheAppliedSettingsAndShowsWithoutRecomputing)
{
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = Make(scene, D::PointCloudPoint);
    auto& props = PointDomainProperties(scene, entity, D::PointCloudPoint);
    auto config = Settings(entity, D::PointCloudPoint);
    config.Weights.Name = "window_weights";
    ASSERT_TRUE(harness.Apply(config));
    auto* ui = harness.Engine.Services().Find<R::EditorUiHost>();
    ASSERT_NE(ui, nullptr);
    ASSERT_TRUE(ui->SetWindowOpen("view.density_weights", true));
    ASSERT_EQ(harness.Settled(config), "");

    IMGUI_CHECKVERSION();
    ImGuiContext* context = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280, 1400);
    io.IniFilename = nullptr;
    unsigned char* pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    std::uint64_t submittedBeforeShow = 0;
    std::size_t undoBeforeShow = 0;
    int showFrame = 0;
    for (int frame = 0; frame < 2000 && (showFrame == 0 || frame < showFrame + 4); ++frame)
    {
        io.DeltaTime = 1.f / 60.f;
        ImGui::NewFrame();
        auto* window = ImGui::FindWindowByName("Compact Density Weights###view.density_weights");
        if (window != nullptr)
        {
            ImGui::SetWindowSize(window, {700, 1300});
            ImGui::SetWindowPos(window, {0, 0});
        }
        if (frame == 2 && window != nullptr) ImGui::ActivateItemByID(window->GetID("Compute compact weights"));
        if (showFrame == 0 && props.Exists("window_weights") && window != nullptr)
        {
            submittedBeforeShow = harness.Engine.Jobs().Stats().SubmittedJobs;
            undoBeforeShow = harness.History().UndoCount();
            ImGui::ActivateItemByID(window->GetID("Show weights"));
            showFrame = frame;
        }
        (void)ui->Windows().DrawOpenWindows();
        ImGui::Render();
        harness.Frame();
    }
    ImGui::DestroyContext(context);
    ASSERT_GT(showFrame, 0) << "the Compute compact weights button started a run that published";
    EXPECT_EQ(harness.Engine.Jobs().Stats().SubmittedJobs, submittedBeforeShow) << "Show ran nothing";
    EXPECT_EQ(undoBeforeShow, 1u) << "one run, one undo entry";
    const auto* overrides = scene.Raw().try_get<Extrinsic::Graphics::Components::VisualizationLaneOverrides>(entity);
    ASSERT_NE(overrides, nullptr);
    ASSERT_TRUE(overrides->Points);
    EXPECT_EQ(overrides->Points->ScalarFieldName, "window_weights");
    ASSERT_NE(DW::Latest(), nullptr);
    ASSERT_TRUE(DW::Latest()->Final);
    EXPECT_EQ(DW::Latest()->Final->Status, "applied");
}

// The window's draft follows the section when it changes elsewhere (config_apply, an agent): its Apply then writes
// those settings, never a stale draft.
TEST(DensityWeightsPlugin, TheWindowFollowsSettingsAppliedElsewhere)
{
    Harness harness{};
    const auto entity = Make(harness.Scene(), D::PointCloudPoint);
    auto first = Settings(entity, D::PointCloudPoint);
    first.Weights.Name = "window_first";
    auto elsewhere = first;
    elsewhere.Weights.Name = "window_elsewhere";
    const auto active = Intrinsic::Tests::Plugins::ApplyElsewhereThenPressApply<DW::kSection>(
        harness, "view.density_weights", "Compact Density Weights###view.density_weights", first, elsewhere);
    ASSERT_TRUE(active);
    EXPECT_EQ(active->Weights.Name, "window_elsewhere") << "the window applied a stale draft";
}
