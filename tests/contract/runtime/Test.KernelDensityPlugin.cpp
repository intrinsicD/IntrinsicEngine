// ARCH-022 Q89: the kernel density plugin's contract, in an engine composed as the Sandbox composes it (in-process
// startup plugin, so it runs in static builds too): its section, its MCP tools, its Jobs window rows, every point
// domain on both CPU backends with one undo entry, its stale and cancelled runs, scalar storage with checked
// conversion, the Vulkan backend's admission texts, the latest result it keeps for its window, and its window.
// Replaces the engine's Test.KernelDensityOperations (its two cases of shared editor machinery moved to
// Test.DensityWeightOperations, and with density weights a plugin since Q90 to Test.OutlierAnalysis).
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
#include "KernelDensityHarness.hpp"
#include "PluginWindowHarness.hpp"

import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.Runtime.EditorUiHost;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.GeometryProperty.Types;
import Extrinsic.Runtime.SelectionController;
import Geometry.PointCloud.Utils;
import Geometry.PointLBVH;

namespace
{
    using namespace Intrinsic::Tests::KernelDensity;
    using D = R::GeometryElementDomain;
    using K = Geometry::PropertyValueKind;
    namespace GS = Extrinsic::ECS::Components::GeometrySources;
    using Intrinsic::Tests::PointDomainProperties;

    // Samples 0.01 apart along x with one far point and an unrelated `keep`; a point cloud also gets a deleted NaN
    // row 4.
    entt::entity Make(Extrinsic::ECS::Scene::Registry& scene, const D domain)
    {
        const auto entity = Intrinsic::Tests::MakePointDomainSource(scene, domain);
        auto& props = PointDomainProperties(scene, entity, domain);
        auto samples = props.GetOrAdd<glm::vec3>("samples");
        for (std::size_t i = 0; i < samples.Size(); ++i) samples[i] = {float(i) * 0.01f, 0, 0};
        samples[domain == D::PointCloudPoint ? 3 : samples.Size() - 1] = {10, 0, 0};
        std::ranges::fill(props.GetOrAdd<float>("keep").Vector(), 42.f);
        if (domain == D::PointCloudPoint)
        {
            props.GetOrAdd<bool>("v:deleted")[4] = true;
            samples[4] = {std::numeric_limits<float>::quiet_NaN(), 0, 0};
        }
        return entity;
    }

    KD::Config Settings(const entt::entity entity, const D domain)
    {
        return {.StableEntityId = R::SelectionController::ToStableEntityId(entity),
            .Positions = {.Domain = domain, .Name = "samples", .ValueKind = K::Vec3},
            .Density = {.Domain = domain, .Name = "density", .ValueKind = K::Float},
            .KNeighbors = 2,
            .Execution = R::kOneShotBatchExecution}; // a batch: computes at once, accepts at its end
    }

    const char* DeletedName(const D d)
    {
        return d == D::MeshFace ? "f:deleted" : (d == D::MeshEdge || d == D::GraphEdge) ? "e:deleted" : "v:deleted";
    }

    bool Usable(const std::string& payload) { return R::ValidateDerivedSection<KD::kSection>(payload, {}, "test").Usable(); }

    const std::string kFloatOnly =
        "Vulkan scalar analysis supports float outputs only; select the CPU backend for other scalar storage.";
    const std::string kParameters = "Vulkan scalar analysis requires k<=63 and normal or zero parameters.";
    const std::string kDevice = "Vulkan scalar analysis requires an operational float64 device and framed spatial cache/jobs.";
}

TEST(KernelDensityPlugin, TheSectionKeepsItsNameBoundsTokensAndStorageKinds)
{
    EXPECT_EQ(KD::kSectionName, "sandbox.kernel_density"); // saved configs of the built-in method still load
    EXPECT_EQ(KD::kSectionSchemaId, "intrinsic.runtime.sandbox.kernel_density");
    for (const auto* payload : {R"({"backend":"vulkan"})", R"({"k_neighbors":-1})", R"({"k_neighbors":4294967296})",
                                R"({"bandwidth":-1})", R"({"gpu_query_batch_size":0})",
                                R"({"gpu_query_batch_size":16385})", R"({"bandwidth":1e100})",
                                R"({"bandwidth":1e-100})", R"({"unknown":1})",
                                R"({"density":{"domain":"unknown","name":"x","kind":"uint32"}})"})
        EXPECT_FALSE(Usable(payload)) << payload;
    for (const auto* payload : {R"({"k_neighbors":0})", R"({"k_neighbors":4294967295})", R"({"gpu_query_batch_size":1})",
                                R"({"gpu_query_batch_size":16384})", R"({"bandwidth":0})",
                                R"({"bandwidth":3.4028234663852886e+38})"})
        EXPECT_TRUE(Usable(payload)) << payload;
    for (const auto* token : {"cpu_octree", "cpu_lbvh", "vulkan_lbvh"})
        EXPECT_TRUE(Usable(Json{{"backend", token}}.dump())) << token;
    KD::Config mixed{};
    mixed.Density.Domain = D::MeshFace;
    EXPECT_FALSE(Usable(R::SerializeDerivedSection<KD::kSection>(mixed))) << "inputs on two domains";
    KD::Config same{};
    same.Density.Name = same.Positions.Name;
    EXPECT_FALSE(Usable(R::SerializeDerivedSection<KD::kSection>(same)));
    const auto registration = R::MakeDerivedSectionRegistration<KD::kSection>();
    for (const auto& [key, value] : Json::parse(registration.DefaultSection.PayloadJson).items())
    {
        const auto result = registration.Validate(Json{{key, value}}.dump(), registration.DefaultSection.PayloadJson, "key");
        EXPECT_TRUE(result.Usable()) << key;
        EXPECT_EQ(result.ParsedFieldCount, 1u) << key;
    }
    for (const auto kind : {K::Bool, K::Int32, K::UInt32, K::UInt64, K::Float, K::Double})
    {
        KD::Config config{.StableEntityId = 7u, .KNeighbors = 3u};
        config.Density.ValueKind = kind;
        auto document = HeadlessConfig();
        R::SetDerivedSection<KD::kSection>(document, config);
        const auto decoded = R::GetDerivedSection<KD::kSection>(document);
        ASSERT_TRUE(decoded) << int(kind);
        EXPECT_EQ(decoded->Density, config.Density);
        EXPECT_EQ(decoded->StableEntityId, 7u);
        EXPECT_EQ(decoded->KNeighbors, 3u);
    }
}

// The copied statistics describe only the live samples; the run is a Jobs window row whose output is a scalar field
// (so the editor's own scalar runs and this one see each other on the same output).
TEST(KernelDensityPlugin, TheToolAnswersWithTheLiveStatisticsAndTheRunIsAJobsWindowRow)
{
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = Make(scene, D::PointCloudPoint);
    auto& props = PointDomainProperties(scene, entity, D::PointCloudPoint);
    constexpr std::array<glm::vec3, 4> plane{{{0, 0, 0}, {2, 0, 0}, {0, 3, 0}, {2, 3, 0}}};
    std::ranges::copy(plane, props.Get<glm::vec3>("samples").Vector().begin());
    ASSERT_NE(harness.Registry.Find("kernel_density"), nullptr);
    auto config = Settings(entity, D::PointCloudPoint);
    for (const auto backend : {KD::DensityBackend::CpuOctree, KD::DensityBackend::CpuLBVH})
    {
        SCOPED_TRACE(KD::ToString(backend));
        config.Backend = backend;
        ASSERT_TRUE(harness.Apply(config));
        const auto reply = harness.Call("kernel_density");
        ASSERT_FALSE(reply.IsError) << reply.ErrorCode << ": " << reply.Text;
        const auto result = Json::parse(reply.Text);
        EXPECT_EQ(result["status"], "applied");
        EXPECT_EQ(result["actual_backend"], KD::ToString(backend));
        EXPECT_EQ(result["slot_count"], 5u);
        EXPECT_EQ(result["live_count"], 4u);
        EXPECT_EQ(result["written_count"], 4u);
        EXPECT_GT(result["used_bandwidth"].get<float>(), 0.f) << "automatic: from the nearest-other spacing";
        EXPECT_GT(result["mean_density"].get<float>(), 0.f);
        EXPECT_LE(result["min_density"].get<float>(), result["mean_density"].get<float>());
        EXPECT_GE(result["max_density"].get<float>(), result["mean_density"].get<float>());
    }
    const auto jobs = harness.Jobs();
    const auto row = std::ranges::find_if(jobs, [](const auto& job) { return job.Name == "Density estimation"; });
    ASSERT_NE(row, jobs.end()) << "plugin runs are rows of the shell's Jobs window";
    EXPECT_EQ(row->Identity.OutputSemantic, R::GeometryPresentationSlotSemantic::ScalarField);
    EXPECT_EQ(row->Identity.OutputName, "density");
    EXPECT_EQ(harness.Settled(config), "");
    const auto ready = Json::parse(harness.Call("kernel_density_preview").Text);
    EXPECT_EQ(ready["ready"], true) << ready.dump();
}

TEST(KernelDensityPlugin, EveryDomainPublishesNamedDensityKeepsDeletedRowsAndTheBackendsAgree)
{
    for (unsigned d = 1; d <= 8; ++d)
        for (const float bandwidth : {0.f, .2f, 2.f})
            for (const unsigned k : {1u, 2u, 63u})
            {
                SCOPED_TRACE(d);
                SCOPED_TRACE(bandwidth);
                SCOPED_TRACE(k);
                Harness harness{};
                auto& scene = harness.Scene();
                const auto entity = Make(scene, D(d));
                auto config = Settings(entity, D(d));
                config.Bandwidth = bandwidth;
                config.KNeighbors = k;
                auto& props = PointDomainProperties(scene, entity, D(d));
                if (D(d) == D::PointCloudPoint)
                {
                    props.Resize(70);
                    auto samples = props.Get<glm::vec3>("samples");
                    for (std::size_t i = 0; i < samples.Size(); ++i) samples[i] = {float(i * i) * 0.01f, 0, 0};
                    samples[1] = samples[0];
                }
                const auto size = props.Size();
                const bool half = D(d) == D::MeshHalfedge || D(d) == D::GraphHalfedge;
                if (half) scene.Raw().get<GS::Edges>(entity).Properties.GetOrAdd<bool>("e:deleted")[1] = true;
                else props.GetOrAdd<bool>(DeletedName(D(d)))[2] = true;
                props.Get<glm::vec3>("samples")[2] = {std::numeric_limits<float>::quiet_NaN(), 0, 0};
                props.GetOrAdd<float>("density").Vector().assign(size, 77);
                const auto positionRevision = std::as_const(props).Get<glm::vec3>("samples").Revision();
                auto* editing = harness.Engine.Services().Find<R::PointCloudEditingService>();
                ASSERT_NE(editing, nullptr);
                EXPECT_EQ(harness.Settled(config), "");
                const auto catalog = editing->InputCatalog(config.StableEntityId, 2u);
                EXPECT_TRUE(std::ranges::any_of(catalog.Entries, [&](const auto& e) { return e.Ref == config.Positions; }));
                const auto reference = harness.Wait(*KD::Start(config));
                ASSERT_EQ(reference.Status, "applied") << reference.Message;
                EXPECT_EQ(reference.ActualBackend, "cpu_octree");
                const auto values = std::as_const(props).Get<float>("density").Vector();
                EXPECT_EQ(values[2], 77) << "a deleted row keeps its value";
                if (half) EXPECT_EQ(values[3], 77);
                EXPECT_EQ(props.Size(), size);
                EXPECT_EQ(std::as_const(props).Get<glm::vec3>("samples").Revision(), positionRevision);
                props.Get<float>("keep")[0] = 99; // an unrelated edit does not make the entry stale
                ASSERT_TRUE(harness.History().Undo().Succeeded());
                EXPECT_EQ(std::as_const(props).Get<float>("density")[0], 77);
                ASSERT_TRUE(harness.History().Redo().Succeeded());
                EXPECT_EQ(std::as_const(props).Get<float>("keep")[0], 99);
                config.Backend = KD::DensityBackend::CpuLBVH;
                const auto indexed = harness.Wait(*KD::Start(config));
                ASSERT_EQ(indexed.Status, "applied") << indexed.Message;
                EXPECT_EQ(indexed.ActualBackend, "cpu_lbvh");
                const auto actual = std::as_const(props).Get<float>("density");
                for (std::size_t i = 0; i < size; ++i) EXPECT_NEAR(actual[i], values[i], 1e-5 * std::max(1.f, values[i]));
                EXPECT_FLOAT_EQ(indexed.UsedBandwidth, reference.UsedBandwidth);
                EXPECT_TRUE(harness.Wait(*KD::Start(config)).IndexReused);
                props.Get<float>("density")[0] = 123; // a foreign edit of the output: undo is refused
                EXPECT_FALSE(harness.History().Undo().Succeeded());
                std::string diagnostic; // Show records its own undo entry (the visualization recipe)
                const bool shown = editing->ShowProperty(config.StableEntityId, config.Density, diagnostic);
                EXPECT_EQ(shown, !half) << diagnostic;
                if (!half)
                {
                    const auto* overrides =
                        scene.Raw().try_get<Extrinsic::Graphics::Components::VisualizationLaneOverrides>(entity);
                    ASSERT_NE(overrides, nullptr);
                    const auto& lane = D(d) == D::MeshVertex || D(d) == D::MeshFace ? overrides->Surface
                        : D(d) == D::PointCloudPoint                               ? overrides->Points
                                                                                   : overrides->Edges;
                    ASSERT_TRUE(lane);
                    EXPECT_EQ(lane->ScalarFieldName, config.Density.Name);
                }
            }
}

TEST(KernelDensityPlugin, AnInvalidDeletionMaskRefusesPreviewAndRun)
{
    for (unsigned domain = 1; domain <= unsigned(D::PointCloudPoint); ++domain)
        for (unsigned fault = 0; fault < 5; ++fault)
        {
            SCOPED_TRACE(domain);
            SCOPED_TRACE(fault);
            const bool halfedge = D(domain) == D::MeshHalfedge || D(domain) == D::GraphHalfedge;
            if (fault >= 3 && !halfedge) continue;
            Harness harness{};
            auto& scene = harness.Scene();
            const auto entity = Make(scene, D(domain));
            const auto config = Settings(entity, D(domain));
            auto& input = PointDomainProperties(scene, entity, D(domain));
            auto& masks = halfedge ? scene.Raw().get<GS::Edges>(entity).Properties : input;
            const char* name = halfedge ? "e:deleted" : DeletedName(D(domain));
            if (auto old = masks.Get<bool>(name)) masks.Remove(old);
            if (fault == 0) (void)masks.GetOrAdd<float>(name);
            if (fault == 1) masks.GetOrAdd<bool>(name).Vector().resize(masks.Size() - 1);
            if (fault == 2) masks.GetOrAdd<bool>(name).Vector().resize(masks.Size() + 1);
            if (fault == 3) masks.Resize(masks.Size() + 1);
            if (fault == 4) input.Resize(input.Size() - 1);
            const auto revision = input.Revision();
            const char* expected =
                fault < 3 ? "Deletion mask must be a count-matched bool property." : "Invalid deletion domain/cardinality.";
            EXPECT_EQ(harness.Settled(config), expected);
            const auto run = KD::Start(config);
            ASSERT_TRUE(run->Final);
            EXPECT_EQ(run->Final->Status, "invalid");
            EXPECT_EQ(run->Final->Message, expected);
            EXPECT_FALSE(input.Exists(config.Density.Name));
            EXPECT_EQ(input.Revision(), revision);
            EXPECT_FALSE(harness.History().CanUndo());
        }
}

TEST(KernelDensityPlugin, AChangedInputOrOutputOrACancelEndsTheRunWithoutAWrite)
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
        const auto run = KD::Start(Settings(entity, D::MeshVertex));
        ASSERT_FALSE(run->Final) << run->Final->Message;
        switch (change)
        {
        case 0: props.Get<float>("keep")[0] = 99; break;
        case 1: props.Get<glm::vec3>("samples")[0].x += 1; break;
        case 2: props.GetOrAdd<bool>("v:deleted")[0] = true; break;
        case 3: props.GetOrAdd<float>("density")[0] = 77; break;
        case 4: EXPECT_TRUE(KD::Cancel(*run)); break;
        }
        EXPECT_EQ(harness.Wait(*run).Status, expected[change]) << run->Final->Message;
        if (change != 0)
            EXPECT_EQ(run->Final->Message,
                      "Density estimation was cancelled or its source became stale; nothing was applied.")
                << "the removed operation's text";
        EXPECT_EQ(harness.History().CanUndo(), change == 0);
        EXPECT_EQ(props.Exists("density"), change == 0 || change == 3);
    }
}

TEST(KernelDensityPlugin, InvalidOutputsTheGpuWithoutADeviceAndNumericalFailuresKeepTheOutput)
{
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = Make(scene, D::PointCloudPoint);
    auto config = Settings(entity, D::PointCloudPoint);
    auto& props = PointDomainProperties(scene, entity, D::PointCloudPoint);
    props.GetOrAdd<float>("density").Vector().assign(props.Size(), 77);
    for (const char* name : {"v:deleted", "samples"})
    {
        auto bad = config;
        bad.Density.Name = name;
        EXPECT_NE(harness.Settled(bad), "") << name;
    }
    auto unrelated = config;
    unrelated.Density.Name = "h:next";
    EXPECT_EQ(harness.Settled(unrelated), "");
    config.Backend = KD::DensityBackend::VulkanLBVH;
    EXPECT_EQ(harness.Wait(*KD::Start(config)).Status, "invalid");
    // A coincident peer keeps a nonzero kernel: h^-3 genuinely overflows the float output.
    props.Get<glm::vec3>("samples")[1] = std::as_const(props).Get<glm::vec3>("samples")[0];
    config.Backend = KD::DensityBackend::CpuOctree;
    config.Bandwidth = 1e-30f;
    EXPECT_EQ(harness.Wait(*KD::Start(config)).Status, "failed");
    EXPECT_EQ(std::as_const(props).Get<float>("density")[0], 77);
    config.Bandwidth = 1;
    config.KNeighbors = std::numeric_limits<std::uint32_t>::max();
    EXPECT_EQ(harness.Wait(*KD::Start(config)).Status, "applied");
}

// Without a device here, the Vulkan backend answers the GPU admission of the operation runtime through both entry
// points (the readiness tool and the run tool), while the CPU backends take any scalar storage and any k.
TEST(KernelDensityPlugin, TheVulkanBackendAnswersTheGpuAdmissionThroughBothTools)
{
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = Make(scene, D::PointCloudPoint);
    harness.Frame(60);
    const auto check = [&](const KD::Config& config, const std::string& expected)
    {
        ASSERT_TRUE(harness.Apply(config));
        EXPECT_EQ(harness.Settled(config), expected); // the input validation answered, as the window waits for it
        const auto preview = Json::parse(harness.Call("kernel_density_preview").Text);
        EXPECT_EQ(preview["ready"], false);
        EXPECT_EQ(preview["reason"], expected);
        const auto reply = harness.Call("kernel_density");
        ASSERT_TRUE(reply.IsError) << reply.Text;
        const auto result = Json::parse(reply.Text);
        EXPECT_EQ(result["status"], "invalid");
        EXPECT_EQ(result["message"], expected);
    };
    auto wide = Settings(entity, D::PointCloudPoint);
    wide.Backend = KD::DensityBackend::VulkanLBVH;
    wide.Density.ValueKind = K::Double;
    check(wide, kFloatOnly);
    auto many = Settings(entity, D::PointCloudPoint);
    many.Backend = KD::DensityBackend::VulkanLBVH;
    many.KNeighbors = 64;
    check(many, kParameters);
    auto device = Settings(entity, D::PointCloudPoint);
    device.Backend = KD::DensityBackend::VulkanLBVH;
    check(device, kDevice);
    EXPECT_FALSE(harness.History().CanUndo());
    for (const auto backend : {KD::DensityBackend::CpuOctree, KD::DensityBackend::CpuLBVH})
    {
        auto cpu = wide;
        cpu.Backend = backend;
        cpu.KNeighbors = 64;
        EXPECT_EQ(harness.Wait(*KD::Start(cpu)).Status, "applied") << KD::ToString(backend);
    }
}

TEST(KernelDensityPlugin, UndoRedoAndAPositionEditRebuildTheIndex)
{
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = Make(scene, D::PointCloudPoint);
    auto config = Settings(entity, D::PointCloudPoint);
    config.Backend = KD::DensityBackend::CpuLBVH;
    config.KNeighbors = std::numeric_limits<std::uint32_t>::max();
    auto& props = PointDomainProperties(scene, entity, D::PointCloudPoint);
    ASSERT_EQ(harness.Wait(*KD::Start(config)).Status, "applied");
    ASSERT_TRUE(harness.History().Undo().Succeeded());
    EXPECT_FALSE(props.Exists("density"));
    ASSERT_TRUE(harness.History().Redo().Succeeded());
    EXPECT_TRUE(props.Exists("density"));
    props.Get<glm::vec3>("samples")[0].x += .1f;
    const auto rerun = harness.Wait(*KD::Start(config));
    ASSERT_EQ(rerun.Status, "applied") << rerun.Message;
    EXPECT_FALSE(rerun.IndexReused);
}

TEST(KernelDensityPlugin, AReplacedStorageMakesUndoStale)
{
    for (const bool replaceOutput : {false, true})
    {
        SCOPED_TRACE(replaceOutput);
        Harness harness{};
        auto& scene = harness.Scene();
        const auto entity = Make(scene, D::PointCloudPoint);
        const auto config = Settings(entity, D::PointCloudPoint);
        auto& props = PointDomainProperties(scene, entity, D::PointCloudPoint);
        ASSERT_EQ(harness.Wait(*KD::Start(config)).Status, "applied");
        const auto computed = std::as_const(props).Get<float>("density").Vector();
        if (replaceOutput)
        {
            auto old = props.Get<float>("density");
            props.Remove(old);
            props.GetOrAdd<glm::vec3>("density").Vector().assign(props.Size(), glm::vec3(17));
        }
        else
        {
            const auto samples = std::as_const(props).Get<glm::vec3>("samples").Vector();
            auto old = props.Get<glm::vec3>("samples");
            props.Remove(old);
            props.GetOrAdd<glm::vec3>("samples").Vector() = samples;
        }
        EXPECT_FALSE(harness.History().Undo().Succeeded());
        if (replaceOutput) EXPECT_EQ(std::as_const(props).Get<glm::vec3>("density")[0], glm::vec3(17));
        else EXPECT_EQ(std::as_const(props).Get<float>("density").Vector(), computed);
    }
}

TEST(KernelDensityPlugin, ScalarStoragePublishesWithCheckedConversionAndExactUndo)
{
    // Densities are not integers: the integer kinds are refused below (checked conversion).
    for (const auto kind : {K::Float, K::Double})
    {
        SCOPED_TRACE(int(kind));
        Harness harness{};
        auto& scene = harness.Scene();
        const auto entity = Make(scene, D::PointCloudPoint);
        auto& props = PointDomainProperties(scene, entity, D::PointCloudPoint);
        auto samples = props.Get<glm::vec3>("samples");
        for (unsigned i = 0; i < 4; ++i) samples[i] = {float(i), 0, 0};
        auto config = Settings(entity, D::PointCloudPoint);
        config.KNeighbors = 1;
        config.Density.ValueKind = kind;
        const auto result = harness.Wait(*KD::Start(config));
        ASSERT_EQ(result.Status, "applied") << result.Message;
        EXPECT_EQ(R::DetectGeometryPropertyValueKind(props, "density"), kind);
        EXPECT_EQ(harness.History().Undo().Status, R::EditorCommandHistoryStatus::Undone);
        EXPECT_FALSE(props.Exists("density"));
        EXPECT_EQ(harness.History().Redo().Status, R::EditorCommandHistoryStatus::Redone);
    }
    {
        Harness harness{};
        auto& scene = harness.Scene();
        const auto entity = Make(scene, D::PointCloudPoint);
        auto config = Settings(entity, D::PointCloudPoint);
        config.Density.ValueKind = K::Int32; // densities of 0.01-spaced samples are not integers
        const auto result = harness.Wait(*KD::Start(config));
        EXPECT_EQ(result.Status, "invalid");
        EXPECT_EQ(result.Message, "Output values are not exactly representable in the selected scalar storage.");
        EXPECT_FALSE(PointDomainProperties(scene, entity, D::PointCloudPoint).Exists("density"));
        EXPECT_EQ(harness.History().Undo().Status, R::EditorCommandHistoryStatus::EmptyUndoStack);
    }
    {
        Harness harness{};
        auto& scene = harness.Scene();
        const auto entity = Make(scene, D::PointCloudPoint);
        auto& props = PointDomainProperties(scene, entity, D::PointCloudPoint);
        props.GetOrAdd<double>("density").Vector().assign(props.Size(), 77.0);
        props.Get<double>("density")[4] = std::numeric_limits<double>::quiet_NaN();
        auto config = Settings(entity, D::PointCloudPoint);
        config.Density.ValueKind = K::Double;
        ASSERT_EQ(harness.Wait(*KD::Start(config)).Status, "applied");
        EXPECT_TRUE(std::isnan(std::as_const(props).Get<double>("density")[4])) << "the deleted row keeps its NaN";
        props.Get<float>("keep")[0] = 99;
        ASSERT_TRUE(harness.History().Undo().Succeeded());
        EXPECT_EQ(std::as_const(props).Get<double>("density")[0], 77);
        EXPECT_TRUE(std::isnan(std::as_const(props).Get<double>("density")[4]));
        ASSERT_TRUE(harness.History().Redo().Succeeded());
        EXPECT_EQ(std::as_const(props).Get<float>("keep")[0], 99);
    }
}

// RUNTIME-348's guarantees for the plugin's own runs: the newest run either the window or the tool started is the
// latest whoever started it (also once it ended without an open window), a start refused because another run owns
// the output does not replace it, a dismiss clears it, and a result of an earlier editor session is gone (also
// after an in-place detach and reattach of the same workspace), while that session's run publishes nothing.
TEST(KernelDensityPlugin, TheLatestRunIsKeptWhoeverStartedItAndEndsWithItsSession)
{
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = Make(scene, D::MeshVertex);
    auto& props = PointDomainProperties(scene, entity, D::MeshVertex);
    const auto config = Settings(entity, D::MeshVertex);
    const auto older = KD::Start(config);
    ASSERT_FALSE(older->Final);
    auto elsewhere = config;
    elsewhere.StableEntityId = 0xFFFFFFF0u;
    const auto newer = KD::Start(elsewhere); // refused at once: a newer result while the older run goes on
    ASSERT_TRUE(newer->Final);
    EXPECT_EQ(harness.Wait(*older).Status, "applied");
    EXPECT_EQ(KD::Latest(), newer) << "the older run's completion never replaces a newer result";
    const auto first = KD::Start(config);
    ASSERT_FALSE(first->Final);
    EXPECT_EQ(KD::Latest(), first) << "a started run is the latest at once";
    ASSERT_EQ(KD::Active().size(), 1u);
    const auto busy = KD::Start(config);
    ASSERT_TRUE(busy->Final);
    EXPECT_EQ(busy->Final->Status, "busy") << busy->Final->Message;
    EXPECT_EQ(KD::Latest(), first) << "a busy refusal answers for the other run";
    EXPECT_EQ(harness.Wait(*first).Status, "applied");
    EXPECT_EQ(KD::Latest(), first);
    EXPECT_TRUE(KD::Active().empty());
    auto missing = config;
    missing.StableEntityId = 0xFFFFFFF0u;
    const auto refused = KD::Start(missing);
    ASSERT_TRUE(refused->Final);
    EXPECT_EQ(refused->Final->Status, "invalid");
    EXPECT_EQ(refused->Final->Message, "Density target entity is stale or missing.") << "the removed operation's text";
    EXPECT_EQ(KD::Latest(), refused) << "a refusal of this run is its result";
    KD::Dismiss(first);
    EXPECT_EQ(KD::Latest(), refused) << "a dismiss of another run changes nothing";
    KD::Dismiss(refused);
    EXPECT_EQ(KD::Latest(), nullptr);

    auto other = config;
    other.Density.Name = "density_of_the_old_session";
    const auto outlived = KD::Start(other);
    ASSERT_FALSE(outlived->Final);
    EXPECT_EQ(KD::Latest(), outlived);
    const auto undo = harness.History().UndoCount();
    harness.Shell.Detach();
    harness.Shell.Attach(harness.Engine.Worlds(), harness.Engine.Services());
    (void)R::PrepareEditorWorkspaceSnapshotFrame(harness.Shell);
    EXPECT_EQ(KD::Latest(), nullptr) << "the same workspace, a new session";
    (void)harness.Wait(*outlived);
    EXPECT_EQ(KD::Latest(), nullptr) << "an old session's delivery does not come back";
    EXPECT_FALSE(props.Exists("density_of_the_old_session"));
    EXPECT_EQ(harness.History().UndoCount(), undo);
    const auto fresh = KD::Start(config);
    EXPECT_EQ(harness.Wait(*fresh).Status, "applied");
    EXPECT_EQ(KD::Latest(), fresh);
}

// The service stopping to use the run's workspace (the shell releasing it, still attached) ends the run's session: it
// publishes nothing there, nor in the session the service uses now.
TEST(KernelDensityPlugin, ARunOfAReleasedWorkspaceWritesNothing)
{
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = Make(scene, D::MeshVertex);
    auto& props = PointDomainProperties(scene, entity, D::MeshVertex);
    const auto run = KD::Start(Settings(entity, D::MeshVertex));
    ASSERT_FALSE(run->Final);
    harness.Editing().ReleaseEditorWorkspace(harness.Shell);
    EXPECT_EQ(harness.Wait(*run).Status, "cancelled") << run->Final->Message;
    EXPECT_FALSE(props.Exists("density"));
    EXPECT_FALSE(harness.History().CanUndo());
    EXPECT_EQ(KD::Latest(), nullptr);
}

// CPU LBVH checks the method's limits before it acquires the index, so a finite coordinate beyond them answers the
// readiness's text at the start too.
TEST(KernelDensityPlugin, AnOutOfRangeCoordinateIsRefusedWithTheMethodsLbvhText)
{
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = Make(scene, D::MeshVertex);
    PointDomainProperties(scene, entity, D::MeshVertex).Get<glm::vec3>("samples")[0].x = 2e18f;
    auto config = Settings(entity, D::MeshVertex);
    config.Backend = KD::DensityBackend::CpuLBVH;
    const std::string limits = "LBVH requires the spatial cache, at most 2^24 samples and coordinates within 1e18.";
    EXPECT_EQ(harness.Settled(config), limits);
    const auto run = KD::Start(config);
    ASSERT_TRUE(run->Final);
    EXPECT_EQ(run->Final->Status, "invalid");
    EXPECT_EQ(run->Final->Message, limits);
}

// The window's position picker lists a property only with at least two live samples, as the method needs.
TEST(KernelDensityPlugin, TheInputCatalogNeedsTwoLiveSamples)
{
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = Make(scene, D::PointCloudPoint);
    auto deleted = PointDomainProperties(scene, entity, D::PointCloudPoint).Get<bool>("v:deleted");
    for (std::size_t i = 1; i < deleted.Size(); ++i) deleted[i] = true; // one live sample
    const auto id = R::SelectionController::ToStableEntityId(entity);
    auto* editing = harness.Engine.Services().Find<R::PointCloudEditingService>();
    ASSERT_NE(editing, nullptr);
    // The catalog fills once the input validation answered (it settles over frames).
    const auto settle = [&](const std::size_t minimum)
    {
        for (int frame = 0; frame < 50 && editing->InputCatalog(id, minimum).Empty(); ++frame) harness.Frame();
    };
    settle(1u);
    EXPECT_FALSE(editing->InputCatalog(id, 1u).Empty()) << "one live sample is listed for a one-sample method";
    EXPECT_TRUE(editing->InputCatalog(id, 2u).Empty());
    deleted[1] = false;
    settle(2u);
    EXPECT_FALSE(editing->InputCatalog(id, 2u).Empty());
    EXPECT_EQ(harness.Settled(Settings(entity, D::PointCloudPoint)), "");
}

TEST(KernelDensityPlugin, TheJobsWindowCancelsARun)
{
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = Make(scene, D::MeshVertex);
    auto& props = PointDomainProperties(scene, entity, D::MeshVertex);
    const auto run = KD::Start(Settings(entity, D::MeshVertex));
    ASSERT_FALSE(run->Final);
    const auto jobs = harness.Jobs();
    const auto row = std::ranges::find_if(jobs, [](const auto& job) { return job.Name == "Density estimation"; });
    ASSERT_NE(row, jobs.end());
    const auto commands = R::PrepareEditorProcessingCommands(harness.Shell);
    ASSERT_TRUE(R::ResolveEditorJobCancelReadiness(commands, *row).Enabled);
    EXPECT_EQ(R::CancelEditorJobRun(commands, *row), R::EditorJobCancelStatus::Requested);
    EXPECT_EQ(harness.Wait(*run).Status, "cancelled") << run->Final->Message;
    EXPECT_FALSE(props.Exists("density"));
    EXPECT_FALSE(harness.History().CanUndo());
}

// The window runs the applied settings ("Estimate density"); "Show density" then shows the published output without
// another run.
TEST(KernelDensityPlugin, TheWindowRunsTheAppliedSettingsAndShowsWithoutRecomputing)
{
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = Make(scene, D::PointCloudPoint);
    auto& props = PointDomainProperties(scene, entity, D::PointCloudPoint);
    auto config = Settings(entity, D::PointCloudPoint);
    config.Density.Name = "window_density";
    ASSERT_TRUE(harness.Apply(config));
    auto* ui = harness.Engine.Services().Find<R::EditorUiHost>();
    ASSERT_NE(ui, nullptr);
    ASSERT_TRUE(ui->SetWindowOpen("view.kernel_density", true));
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
        auto* window = ImGui::FindWindowByName("Kernel Density###view.kernel_density");
        if (window != nullptr)
        {
            ImGui::SetWindowSize(window, {700, 1300});
            ImGui::SetWindowPos(window, {0, 0});
        }
        if (frame == 2 && window != nullptr) ImGui::ActivateItemByID(window->GetID("Estimate density"));
        if (showFrame == 0 && props.Exists("window_density") && window != nullptr)
        {
            submittedBeforeShow = harness.Engine.Jobs().Stats().SubmittedJobs;
            undoBeforeShow = harness.History().UndoCount();
            ImGui::ActivateItemByID(window->GetID("Show density"));
            showFrame = frame;
        }
        (void)ui->Windows().DrawOpenWindows();
        ImGui::Render();
        harness.Frame();
    }
    ImGui::DestroyContext(context);
    ASSERT_GT(showFrame, 0) << "the Estimate density button started a run that published";
    EXPECT_EQ(harness.Engine.Jobs().Stats().SubmittedJobs, submittedBeforeShow) << "Show ran nothing";
    EXPECT_EQ(undoBeforeShow, 1u) << "one run, one undo entry";
    const auto* overrides = scene.Raw().try_get<Extrinsic::Graphics::Components::VisualizationLaneOverrides>(entity);
    ASSERT_NE(overrides, nullptr);
    ASSERT_TRUE(overrides->Points);
    EXPECT_EQ(overrides->Points->ScalarFieldName, "window_density");
    ASSERT_NE(KD::Latest(), nullptr);
    ASSERT_TRUE(KD::Latest()->Final);
    EXPECT_EQ(KD::Latest()->Final->Status, "applied");
}

// The plan's acceptance: a CPU run publishes exactly what the geometry function returns for the live samples (octree:
// EstimateKernelDensity; LBVH: EstimateKernelDensityFromNeighbors over the max(k,2)+1 nearest rows, self included), with
// the automatic and an explicit bandwidth.
TEST(KernelDensityPlugin, CpuOutputsAreBitwiseTheGeometryFunctions)
{
    for (const auto backend : {KD::DensityBackend::CpuOctree, KD::DensityBackend::CpuLBVH})
        for (const auto [bandwidth, k] : {std::pair{0.f, 2u}, std::pair{.5f, 6u}})
        {
            SCOPED_TRACE(KD::ToString(backend));
            SCOPED_TRACE(k);
            Harness harness{};
            auto& scene = harness.Scene();
            const auto entity = Make(scene, D::PointCloudPoint);
            auto config = Settings(entity, D::PointCloudPoint);
            config.Backend = backend;
            config.Bandwidth = bandwidth;
            config.KNeighbors = k;
            ASSERT_EQ(harness.Wait(*KD::Start(config)).Status, "applied");
            const auto& props = PointDomainProperties(scene, entity, D::PointCloudPoint);
            const auto samples = std::as_const(props).Get<glm::vec3>("samples").Vector();
            const auto deleted = std::as_const(props).Get<bool>("v:deleted").Vector();
            std::vector<glm::vec3> live;
            std::vector<std::size_t> slots;
            for (std::size_t i = 0; i < samples.size(); ++i)
                if (!deleted[i])
                {
                    live.push_back(samples[i]);
                    slots.push_back(i);
                }
            const Geometry::PointCloud::KDEParams params{.KNeighbors = k, .Bandwidth = bandwidth};
            std::optional<Geometry::PointCloud::KDEResult> expected;
            if (backend == KD::DensityBackend::CpuOctree) expected = Geometry::PointCloud::EstimateKernelDensity(live, params);
            else
            {
                Geometry::PointLBVH::Index index;
                ASSERT_TRUE(index.Build(live));
                const auto width = std::uint32_t(std::min<std::size_t>(live.size(), std::max<std::size_t>(k, 2) + 1));
                std::vector<std::uint32_t> rows;
                for (const auto& point : live)
                    for (const auto& neighbor : index.KNearest(point, width, Geometry::PointLBVH::InvalidIndex, true))
                        rows.push_back(neighbor.Index);
                expected = Geometry::PointCloud::EstimateKernelDensityFromNeighbors(live, rows, params);
            }
            ASSERT_TRUE(expected);
            const auto values = std::as_const(props).Get<float>("density").Vector();
            for (std::size_t i = 0; i < live.size(); ++i) EXPECT_EQ(values[slots[i]], expected->Densities[i]) << slots[i];
        }
}

// The window's draft follows the section when it changes elsewhere (config_apply, an agent): its Apply then writes
// those settings, never a stale draft.
TEST(KernelDensityPlugin, TheWindowFollowsSettingsAppliedElsewhere)
{
    Harness harness{};
    const auto entity = Make(harness.Scene(), D::PointCloudPoint);
    auto first = Settings(entity, D::PointCloudPoint);
    first.Density.Name = "window_first";
    auto elsewhere = first;
    elsewhere.Density.Name = "window_elsewhere";
    const auto active = Intrinsic::Tests::Plugins::ApplyElsewhereThenPressApply<KD::kSection>(
        harness, "view.kernel_density", "Kernel Density###view.kernel_density", first, elsewhere);
    ASSERT_TRUE(active);
    EXPECT_EQ(active->Density.Name, "window_elsewhere") << "the window applied a stale draft";
}
