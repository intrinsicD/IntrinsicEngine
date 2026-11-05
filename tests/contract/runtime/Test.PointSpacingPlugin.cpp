// ARCH-022 Q89: the point spacing plugin's contract, in an engine composed as the Sandbox composes it (in-process
// startup plugin, so it runs in static builds too): its section, its MCP tools, its Jobs window rows, every point
// domain on both CPU backends with one undo entry, its stale and cancelled runs, scalar storage with checked
// conversion, the Vulkan backend's admission texts, the latest result it keeps for its window, and its window.
// Replaces the engine's Test.PointSpacingOperations.
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
#include "PointSpacingHarness.hpp"

import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.Runtime.EditorUiHost;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.GeometryProperty.Types;
import Extrinsic.Runtime.SelectionController;

namespace
{
    using namespace Intrinsic::Tests::PointSpacing;
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

    PS::Config Settings(const entt::entity entity, const D domain)
    {
        return {.StableEntityId = R::SelectionController::ToStableEntityId(entity),
                .Positions = {.Domain = domain, .Name = "samples", .ValueKind = K::Vec3},
                .Radii = {.Domain = domain, .Name = "radii", .ValueKind = K::Float},
                .KNeighbors = 2};
    }

    const char* DeletedName(const D d)
    {
        return d == D::MeshFace ? "f:deleted" : (d == D::MeshEdge || d == D::GraphEdge) ? "e:deleted" : "v:deleted";
    }

    bool Usable(const std::string& payload) { return R::ValidateDerivedSection<PS::kSection>(payload, {}, "test").Usable(); }

    const std::string kFloatOnly =
        "Vulkan scalar analysis supports float outputs only; select the CPU backend for other scalar storage.";
    const std::string kParameters = "Vulkan scalar analysis requires k<=63 and normal or zero parameters.";
    const std::string kDevice = "Vulkan scalar analysis requires an operational float64 device and framed spatial cache/jobs.";
}

TEST(PointSpacingPlugin, TheSectionKeepsItsNameBoundsTokensAndStorageKinds)
{
    EXPECT_EQ(PS::kSectionName, "sandbox.point_spacing"); // saved configs of the built-in method still load
    EXPECT_EQ(PS::kSectionSchemaId, "intrinsic.runtime.sandbox.point_spacing");
    for (const auto* payload : {R"({"backend":"vulkan"})", R"({"k_neighbors":-1})", R"({"k_neighbors":4294967296})",
                                R"({"scale_factor":-1})", R"({"gpu_query_batch_size":0})",
                                R"({"gpu_query_batch_size":16385})", R"({"scale_factor":1e100})",
                                R"({"scale_factor":1e-100})", R"({"unknown":1})",
                                R"({"radii":{"domain":"unknown","name":"x","kind":"uint32"}})"})
        EXPECT_FALSE(Usable(payload)) << payload;
    for (const auto* payload : {R"({"k_neighbors":0})", R"({"k_neighbors":4294967295})", R"({"gpu_query_batch_size":1})",
                                R"({"gpu_query_batch_size":16384})", R"({"scale_factor":0})",
                                R"({"scale_factor":3.4028234663852886e+38})"})
        EXPECT_TRUE(Usable(payload)) << payload;
    for (const auto* token : {"cpu_octree", "cpu_lbvh", "vulkan_lbvh"})
        EXPECT_TRUE(Usable(Json{{"backend", token}}.dump())) << token;
    PS::Config mixed{};
    mixed.Radii.Domain = D::MeshFace;
    EXPECT_FALSE(Usable(R::SerializeDerivedSection<PS::kSection>(mixed))) << "inputs on two domains";
    PS::Config same{};
    same.Radii.Name = same.Positions.Name;
    EXPECT_FALSE(Usable(R::SerializeDerivedSection<PS::kSection>(same)));
    const auto registration = R::MakeDerivedSectionRegistration<PS::kSection>();
    for (const auto& [key, value] : Json::parse(registration.DefaultSection.PayloadJson).items())
    {
        const auto result = registration.Validate(Json{{key, value}}.dump(), registration.DefaultSection.PayloadJson, "key");
        EXPECT_TRUE(result.Usable()) << key;
        EXPECT_EQ(result.ParsedFieldCount, 1u) << key;
    }
    for (const auto kind : {K::Bool, K::Int32, K::UInt32, K::UInt64, K::Float, K::Double})
    {
        PS::Config config{.StableEntityId = 7u, .KNeighbors = 3u};
        config.Radii.ValueKind = kind;
        auto document = HeadlessConfig();
        R::SetDerivedSection<PS::kSection>(document, config);
        const auto decoded = R::GetDerivedSection<PS::kSection>(document);
        ASSERT_TRUE(decoded) << int(kind);
        EXPECT_EQ(decoded->Radii, config.Radii);
        EXPECT_EQ(decoded->StableEntityId, 7u);
        EXPECT_EQ(decoded->KNeighbors, 3u);
    }
}

// The copied statistics describe only the live samples; the run is a Jobs window row whose output is a scalar field
// (so the editor's own scalar runs and this one see each other on the same output).
TEST(PointSpacingPlugin, TheToolAnswersWithTheLiveStatisticsAndTheRunIsAJobsWindowRow)
{
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = Make(scene, D::PointCloudPoint);
    auto& props = PointDomainProperties(scene, entity, D::PointCloudPoint);
    constexpr std::array<glm::vec3, 4> plane{{{0, 0, 0}, {2, 0, 0}, {0, 3, 0}, {2, 3, 0}}};
    std::ranges::copy(plane, props.Get<glm::vec3>("samples").Vector().begin());
    ASSERT_NE(harness.Registry.Find("point_spacing"), nullptr);
    auto config = Settings(entity, D::PointCloudPoint);
    for (const auto backend : {PS::SpacingBackend::CpuOctree, PS::SpacingBackend::CpuLBVH})
    {
        SCOPED_TRACE(PS::ToString(backend));
        config.Backend = backend;
        ASSERT_TRUE(harness.Apply(config));
        const auto reply = harness.Call("point_spacing");
        ASSERT_FALSE(reply.IsError) << reply.ErrorCode << ": " << reply.Text;
        const auto result = Json::parse(reply.Text);
        EXPECT_EQ(result["status"], "applied");
        EXPECT_EQ(result["actual_backend"], PS::ToString(backend));
        EXPECT_EQ(result["slot_count"], 5u);
        EXPECT_EQ(result["live_count"], 4u);
        EXPECT_EQ(result["written_count"], 4u);
        EXPECT_EQ(result["centroid"], Json::array({1.f, 1.5f, 0.f}));
        EXPECT_FLOAT_EQ(result["average_spacing"].get<float>(), 2.f);
        EXPECT_FLOAT_EQ(result["min_spacing"].get<float>(), 2.f);
        EXPECT_FLOAT_EQ(result["max_spacing"].get<float>(), 2.f);
        EXPECT_FLOAT_EQ(result["bounding_box_diagonal"].get<float>(), std::sqrt(13.f));
    }
    const auto jobs = harness.Jobs();
    const auto row = std::ranges::find_if(jobs, [](const auto& job) { return job.Name == "Radii estimation"; });
    ASSERT_NE(row, jobs.end()) << "plugin runs are rows of the shell's Jobs window";
    EXPECT_EQ(row->Identity.OutputSemantic, R::GeometryPresentationSlotSemantic::ScalarField);
    EXPECT_EQ(row->Identity.OutputName, "radii");
    EXPECT_EQ(harness.Settled(config), "");
    const auto ready = Json::parse(harness.Call("point_spacing_preview").Text);
    EXPECT_EQ(ready["ready"], true) << ready.dump();
}

TEST(PointSpacingPlugin, EveryDomainPublishesNamedRadiiKeepsDeletedRowsAndTheBackendsAgree)
{
    for (unsigned d = 1; d <= 8; ++d)
        for (const float scale : {0.f, 2.f})
            for (const unsigned k : {1u, 2u, 63u})
            {
                SCOPED_TRACE(d);
                SCOPED_TRACE(scale);
                SCOPED_TRACE(k);
                Harness harness{};
                auto& scene = harness.Scene();
                const auto entity = Make(scene, D(d));
                auto config = Settings(entity, D(d));
                config.ScaleFactor = scale;
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
                props.GetOrAdd<float>("radii").Vector().assign(size, 77);
                const auto positionRevision = std::as_const(props).Get<glm::vec3>("samples").Revision();
                auto* editing = harness.Engine.Services().Find<R::PointCloudEditingService>();
                ASSERT_NE(editing, nullptr);
                EXPECT_EQ(harness.Settled(config), "");
                const auto catalog = editing->InputCatalog(config.StableEntityId, 2u);
                EXPECT_TRUE(std::ranges::any_of(catalog.Entries, [&](const auto& e) { return e.Ref == config.Positions; }));
                const auto reference = harness.Wait(*PS::Start(config, false));
                ASSERT_EQ(reference.Status, "applied") << reference.Message;
                EXPECT_EQ(reference.ActualBackend, "cpu_octree");
                const auto values = std::as_const(props).Get<float>("radii").Vector();
                EXPECT_EQ(values[2], 77) << "a deleted row keeps its value";
                if (half) EXPECT_EQ(values[3], 77);
                EXPECT_EQ(props.Size(), size);
                EXPECT_EQ(std::as_const(props).Get<glm::vec3>("samples").Revision(), positionRevision);
                props.Get<float>("keep")[0] = 99; // an unrelated edit does not make the entry stale
                ASSERT_TRUE(harness.History().Undo().Succeeded());
                EXPECT_EQ(std::as_const(props).Get<float>("radii")[0], 77);
                ASSERT_TRUE(harness.History().Redo().Succeeded());
                EXPECT_EQ(std::as_const(props).Get<float>("keep")[0], 99);
                config.Backend = PS::SpacingBackend::CpuLBVH;
                const auto indexed = harness.Wait(*PS::Start(config, false));
                ASSERT_EQ(indexed.Status, "applied") << indexed.Message;
                EXPECT_EQ(indexed.ActualBackend, "cpu_lbvh");
                const auto actual = std::as_const(props).Get<float>("radii");
                for (std::size_t i = 0; i < size; ++i) EXPECT_NEAR(actual[i], values[i], 1e-5 * std::max(1.f, values[i]));
                EXPECT_FLOAT_EQ(indexed.AverageSpacing, reference.AverageSpacing);
                EXPECT_FLOAT_EQ(indexed.MinSpacing, reference.MinSpacing);
                EXPECT_FLOAT_EQ(indexed.MaxSpacing, reference.MaxSpacing);
                EXPECT_EQ(indexed.Centroid, reference.Centroid);
                EXPECT_FLOAT_EQ(indexed.BoundingBoxDiagonal, reference.BoundingBoxDiagonal);
                EXPECT_FLOAT_EQ(indexed.MeanRadius, reference.MeanRadius);
                EXPECT_TRUE(harness.Wait(*PS::Start(config, false)).IndexReused);
                props.Get<float>("radii")[0] = 123; // a foreign edit of the output: undo is refused
                EXPECT_FALSE(harness.History().Undo().Succeeded());
                std::string diagnostic; // Show records its own undo entry (the visualization recipe)
                const bool shown = editing->ShowProperty(config.StableEntityId, config.Radii, diagnostic);
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
                    EXPECT_EQ(lane->ScalarFieldName, config.Radii.Name);
                }
            }
}

TEST(PointSpacingPlugin, AnInvalidDeletionMaskRefusesPreviewAndRun)
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
            const auto run = PS::Start(config, false);
            ASSERT_TRUE(run->Final);
            EXPECT_EQ(run->Final->Status, "invalid");
            EXPECT_EQ(run->Final->Message, expected);
            EXPECT_FALSE(input.Exists(config.Radii.Name));
            EXPECT_EQ(input.Revision(), revision);
            EXPECT_FALSE(harness.History().CanUndo());
        }
}

TEST(PointSpacingPlugin, AChangedInputOrOutputOrACancelEndsTheRunWithoutAWrite)
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
        const auto run = PS::Start(Settings(entity, D::MeshVertex), false);
        ASSERT_FALSE(run->Final) << run->Final->Message;
        switch (change)
        {
        case 0: props.Get<float>("keep")[0] = 99; break;
        case 1: props.Get<glm::vec3>("samples")[0].x += 1; break;
        case 2: props.GetOrAdd<bool>("v:deleted")[0] = true; break;
        case 3: props.GetOrAdd<float>("radii")[0] = 77; break;
        case 4: EXPECT_TRUE(PS::Cancel(*run)); break;
        }
        EXPECT_EQ(harness.Wait(*run).Status, expected[change]) << run->Final->Message;
        EXPECT_EQ(harness.History().CanUndo(), change == 0);
        EXPECT_EQ(props.Exists("radii"), change == 0 || change == 3);
    }
}

TEST(PointSpacingPlugin, InvalidOutputsTheGpuWithoutADeviceAndNumericalFailuresKeepTheOutput)
{
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = Make(scene, D::PointCloudPoint);
    auto config = Settings(entity, D::PointCloudPoint);
    auto& props = PointDomainProperties(scene, entity, D::PointCloudPoint);
    props.GetOrAdd<float>("radii").Vector().assign(props.Size(), 77);
    for (const char* name : {"v:deleted", "samples"})
    {
        auto bad = config;
        bad.Radii.Name = name;
        EXPECT_NE(harness.Settled(bad), "") << name;
    }
    auto unrelated = config;
    unrelated.Radii.Name = "h:next";
    EXPECT_EQ(harness.Settled(unrelated), "");
    config.Backend = PS::SpacingBackend::VulkanLBVH;
    EXPECT_EQ(harness.Wait(*PS::Start(config, true)).Status, "invalid");
    config.Backend = PS::SpacingBackend::CpuOctree;
    config.ScaleFactor = std::numeric_limits<float>::max();
    EXPECT_EQ(harness.Wait(*PS::Start(config, false)).Status, "failed");
    EXPECT_EQ(std::as_const(props).Get<float>("radii")[0], 77);
    config.ScaleFactor = 1;
    config.KNeighbors = std::numeric_limits<std::uint32_t>::max();
    EXPECT_EQ(harness.Wait(*PS::Start(config, false)).Status, "applied");
}

// Without a device here, the Vulkan backend answers the GPU admission of the operation runtime through both entry
// points (the readiness tool and the run tool), while the CPU backends take any scalar storage and any k.
TEST(PointSpacingPlugin, TheVulkanBackendAnswersTheGpuAdmissionThroughBothTools)
{
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = Make(scene, D::PointCloudPoint);
    harness.Frame(60);
    const auto check = [&](const PS::Config& config, const std::string& expected)
    {
        ASSERT_TRUE(harness.Apply(config));
        EXPECT_EQ(harness.Settled(config), expected); // the input validation answered, as the window waits for it
        const auto preview = Json::parse(harness.Call("point_spacing_preview").Text);
        EXPECT_EQ(preview["ready"], false);
        EXPECT_EQ(preview["reason"], expected);
        const auto reply = harness.Call("point_spacing");
        ASSERT_TRUE(reply.IsError) << reply.Text;
        const auto result = Json::parse(reply.Text);
        EXPECT_EQ(result["status"], "invalid");
        EXPECT_EQ(result["message"], expected);
    };
    auto wide = Settings(entity, D::PointCloudPoint);
    wide.Backend = PS::SpacingBackend::VulkanLBVH;
    wide.Radii.ValueKind = K::Double;
    check(wide, kFloatOnly);
    auto many = Settings(entity, D::PointCloudPoint);
    many.Backend = PS::SpacingBackend::VulkanLBVH;
    many.KNeighbors = 64;
    check(many, kParameters);
    auto device = Settings(entity, D::PointCloudPoint);
    device.Backend = PS::SpacingBackend::VulkanLBVH;
    check(device, kDevice);
    EXPECT_FALSE(harness.History().CanUndo());
    for (const auto backend : {PS::SpacingBackend::CpuOctree, PS::SpacingBackend::CpuLBVH})
    {
        auto cpu = wide;
        cpu.Backend = backend;
        cpu.KNeighbors = 64;
        EXPECT_EQ(harness.Wait(*PS::Start(cpu, false)).Status, "applied") << PS::ToString(backend);
    }
}

TEST(PointSpacingPlugin, UndoRedoAndAPositionEditRebuildTheIndex)
{
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = Make(scene, D::PointCloudPoint);
    auto config = Settings(entity, D::PointCloudPoint);
    config.Backend = PS::SpacingBackend::CpuLBVH;
    config.KNeighbors = std::numeric_limits<std::uint32_t>::max();
    auto& props = PointDomainProperties(scene, entity, D::PointCloudPoint);
    ASSERT_EQ(harness.Wait(*PS::Start(config, false)).Status, "applied");
    ASSERT_TRUE(harness.History().Undo().Succeeded());
    EXPECT_FALSE(props.Exists("radii"));
    ASSERT_TRUE(harness.History().Redo().Succeeded());
    EXPECT_TRUE(props.Exists("radii"));
    props.Get<glm::vec3>("samples")[0].x += .1f;
    const auto rerun = harness.Wait(*PS::Start(config, false));
    ASSERT_EQ(rerun.Status, "applied") << rerun.Message;
    EXPECT_FALSE(rerun.IndexReused);
}

TEST(PointSpacingPlugin, AReplacedStorageMakesUndoStale)
{
    for (const bool replaceOutput : {false, true})
    {
        SCOPED_TRACE(replaceOutput);
        Harness harness{};
        auto& scene = harness.Scene();
        const auto entity = Make(scene, D::PointCloudPoint);
        const auto config = Settings(entity, D::PointCloudPoint);
        auto& props = PointDomainProperties(scene, entity, D::PointCloudPoint);
        ASSERT_EQ(harness.Wait(*PS::Start(config, false)).Status, "applied");
        const auto computed = std::as_const(props).Get<float>("radii").Vector();
        if (replaceOutput)
        {
            auto old = props.Get<float>("radii");
            props.Remove(old);
            props.GetOrAdd<glm::vec3>("radii").Vector().assign(props.Size(), glm::vec3(17));
        }
        else
        {
            const auto samples = std::as_const(props).Get<glm::vec3>("samples").Vector();
            auto old = props.Get<glm::vec3>("samples");
            props.Remove(old);
            props.GetOrAdd<glm::vec3>("samples").Vector() = samples;
        }
        EXPECT_FALSE(harness.History().Undo().Succeeded());
        if (replaceOutput) EXPECT_EQ(std::as_const(props).Get<glm::vec3>("radii")[0], glm::vec3(17));
        else EXPECT_EQ(std::as_const(props).Get<float>("radii").Vector(), computed);
    }
}

TEST(PointSpacingPlugin, ScalarStoragePublishesWithCheckedConversionAndExactUndo)
{
    for (const auto kind : {K::Bool, K::Int32, K::UInt32, K::UInt64, K::Float, K::Double})
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
        config.Radii.ValueKind = kind;
        const auto result = harness.Wait(*PS::Start(config, false));
        ASSERT_EQ(result.Status, "applied") << result.Message;
        EXPECT_EQ(R::DetectGeometryPropertyValueKind(props, "radii"), kind);
        EXPECT_EQ(harness.History().Undo().Status, R::EditorCommandHistoryStatus::Undone);
        EXPECT_FALSE(props.Exists("radii"));
        EXPECT_EQ(harness.History().Redo().Status, R::EditorCommandHistoryStatus::Redone);
    }
    {
        Harness harness{};
        auto& scene = harness.Scene();
        const auto entity = Make(scene, D::PointCloudPoint);
        auto config = Settings(entity, D::PointCloudPoint);
        config.Radii.ValueKind = K::Int32; // 0.01-spaced radii are not integers
        const auto result = harness.Wait(*PS::Start(config, false));
        EXPECT_EQ(result.Status, "invalid");
        EXPECT_EQ(result.Message, "Output values are not exactly representable in the selected scalar storage.");
        EXPECT_FALSE(PointDomainProperties(scene, entity, D::PointCloudPoint).Exists("radii"));
        EXPECT_EQ(harness.History().Undo().Status, R::EditorCommandHistoryStatus::EmptyUndoStack);
    }
    {
        Harness harness{};
        auto& scene = harness.Scene();
        const auto entity = Make(scene, D::PointCloudPoint);
        auto& props = PointDomainProperties(scene, entity, D::PointCloudPoint);
        props.GetOrAdd<double>("radii").Vector().assign(props.Size(), 77.0);
        props.Get<double>("radii")[4] = std::numeric_limits<double>::quiet_NaN();
        auto config = Settings(entity, D::PointCloudPoint);
        config.Radii.ValueKind = K::Double;
        ASSERT_EQ(harness.Wait(*PS::Start(config, false)).Status, "applied");
        EXPECT_TRUE(std::isnan(std::as_const(props).Get<double>("radii")[4])) << "the deleted row keeps its NaN";
        props.Get<float>("keep")[0] = 99;
        ASSERT_TRUE(harness.History().Undo().Succeeded());
        EXPECT_EQ(std::as_const(props).Get<double>("radii")[0], 77);
        EXPECT_TRUE(std::isnan(std::as_const(props).Get<double>("radii")[4]));
        ASSERT_TRUE(harness.History().Redo().Succeeded());
        EXPECT_EQ(std::as_const(props).Get<float>("keep")[0], 99);
    }
}

// RUNTIME-348's guarantees for the plugin's own runs: the newest run either the window or the tool started is the
// latest whoever started it (also once it ended without an open window), a start refused because another run owns
// the output does not replace it, a dismiss clears it, and a result of an earlier editor session is gone (also
// after an in-place detach and reattach of the same workspace), while that session's run publishes nothing.
TEST(PointSpacingPlugin, TheLatestRunIsKeptWhoeverStartedItAndEndsWithItsSession)
{
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = Make(scene, D::MeshVertex);
    auto& props = PointDomainProperties(scene, entity, D::MeshVertex);
    const auto config = Settings(entity, D::MeshVertex);
    const auto older = PS::Start(config, false);
    ASSERT_FALSE(older->Final);
    auto elsewhere = config;
    elsewhere.StableEntityId = 0xFFFFFFF0u;
    const auto newer = PS::Start(elsewhere, false); // refused at once: a newer result while the older run goes on
    ASSERT_TRUE(newer->Final);
    EXPECT_EQ(harness.Wait(*older).Status, "applied");
    EXPECT_EQ(PS::Latest(), newer) << "the older run's completion never replaces a newer result";
    const auto first = PS::Start(config, false);
    ASSERT_FALSE(first->Final);
    EXPECT_EQ(PS::Latest(), first) << "a started run is the latest at once";
    ASSERT_EQ(PS::Active().size(), 1u);
    const auto busy = PS::Start(config, false);
    ASSERT_TRUE(busy->Final);
    EXPECT_EQ(busy->Final->Status, "busy") << busy->Final->Message;
    EXPECT_EQ(PS::Latest(), first) << "a busy refusal answers for the other run";
    EXPECT_EQ(harness.Wait(*first).Status, "applied");
    EXPECT_EQ(PS::Latest(), first);
    EXPECT_TRUE(PS::Active().empty());
    auto missing = config;
    missing.StableEntityId = 0xFFFFFFF0u;
    const auto refused = PS::Start(missing, false);
    ASSERT_TRUE(refused->Final);
    EXPECT_EQ(refused->Final->Status, "invalid");
    EXPECT_EQ(PS::Latest(), refused) << "a refusal of this run is its result";
    PS::Dismiss(first);
    EXPECT_EQ(PS::Latest(), refused) << "a dismiss of another run changes nothing";
    PS::Dismiss(refused);
    EXPECT_EQ(PS::Latest(), nullptr);

    auto other = config;
    other.Radii.Name = "radii_of_the_old_session";
    const auto outlived = PS::Start(other, false);
    ASSERT_FALSE(outlived->Final);
    EXPECT_EQ(PS::Latest(), outlived);
    const auto undo = harness.History().UndoCount();
    harness.Shell.Detach();
    harness.Shell.Attach(harness.Engine.Worlds(), harness.Engine.Services());
    (void)R::PrepareEditorWorkspaceSnapshotFrame(harness.Shell);
    EXPECT_EQ(PS::Latest(), nullptr) << "the same workspace, a new session";
    (void)harness.Wait(*outlived);
    EXPECT_EQ(PS::Latest(), nullptr) << "an old session's delivery does not come back";
    EXPECT_FALSE(props.Exists("radii_of_the_old_session"));
    EXPECT_EQ(harness.History().UndoCount(), undo);
    const auto fresh = PS::Start(config, false);
    EXPECT_EQ(harness.Wait(*fresh).Status, "applied");
    EXPECT_EQ(PS::Latest(), fresh);
}

// The service stopping to use the run's workspace (the shell releasing it, still attached) ends the run's session: it
// publishes nothing there, nor in the session the service uses now.
TEST(PointSpacingPlugin, ARunOfAReleasedWorkspaceWritesNothing)
{
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = Make(scene, D::MeshVertex);
    auto& props = PointDomainProperties(scene, entity, D::MeshVertex);
    const auto run = PS::Start(Settings(entity, D::MeshVertex), false);
    ASSERT_FALSE(run->Final);
    harness.Editing().ReleaseEditorWorkspace(harness.Shell);
    EXPECT_EQ(harness.Wait(*run).Status, "cancelled") << run->Final->Message;
    EXPECT_FALSE(props.Exists("radii"));
    EXPECT_FALSE(harness.History().CanUndo());
    EXPECT_EQ(PS::Latest(), nullptr);
}

// CPU LBVH checks the method's limits before it acquires the index, so a finite coordinate beyond them answers the
// readiness's text at the start too.
TEST(PointSpacingPlugin, AnOutOfRangeCoordinateIsRefusedWithTheMethodsLbvhText)
{
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = Make(scene, D::MeshVertex);
    PointDomainProperties(scene, entity, D::MeshVertex).Get<glm::vec3>("samples")[0].x = 2e18f;
    auto config = Settings(entity, D::MeshVertex);
    config.Backend = PS::SpacingBackend::CpuLBVH;
    const std::string limits = "LBVH requires the spatial cache, at most 2^24 samples and coordinates within 1e18.";
    EXPECT_EQ(harness.Settled(config), limits);
    const auto run = PS::Start(config, false);
    ASSERT_TRUE(run->Final);
    EXPECT_EQ(run->Final->Status, "invalid");
    EXPECT_EQ(run->Final->Message, limits);
}

TEST(PointSpacingPlugin, TheJobsWindowCancelsARun)
{
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = Make(scene, D::MeshVertex);
    auto& props = PointDomainProperties(scene, entity, D::MeshVertex);
    const auto run = PS::Start(Settings(entity, D::MeshVertex), false);
    ASSERT_FALSE(run->Final);
    const auto jobs = harness.Jobs();
    const auto row = std::ranges::find_if(jobs, [](const auto& job) { return job.Name == "Radii estimation"; });
    ASSERT_NE(row, jobs.end());
    const auto commands = R::PrepareEditorProcessingCommands(harness.Shell);
    ASSERT_TRUE(R::ResolveEditorJobCancelReadiness(commands, *row).Enabled);
    EXPECT_EQ(R::CancelEditorJobRun(commands, *row), R::EditorJobCancelStatus::Requested);
    EXPECT_EQ(harness.Wait(*run).Status, "cancelled") << run->Final->Message;
    EXPECT_FALSE(props.Exists("radii"));
    EXPECT_FALSE(harness.History().CanUndo());
}

// The window runs the applied settings ("Estimate radii"); "Show radii" then shows the published output without
// another run.
TEST(PointSpacingPlugin, TheWindowRunsTheAppliedSettingsAndShowsWithoutRecomputing)
{
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = Make(scene, D::PointCloudPoint);
    auto& props = PointDomainProperties(scene, entity, D::PointCloudPoint);
    auto config = Settings(entity, D::PointCloudPoint);
    config.Radii.Name = "window_radii";
    ASSERT_TRUE(harness.Apply(config));
    auto* ui = harness.Engine.Services().Find<R::EditorUiHost>();
    ASSERT_NE(ui, nullptr);
    ASSERT_TRUE(ui->SetWindowOpen("view.point_spacing", true));
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
        auto* window = ImGui::FindWindowByName("Point Spacing and Radii###view.point_spacing");
        if (window != nullptr)
        {
            ImGui::SetWindowSize(window, {700, 1300});
            ImGui::SetWindowPos(window, {0, 0});
        }
        if (frame == 2 && window != nullptr) ImGui::ActivateItemByID(window->GetID("Estimate radii"));
        if (showFrame == 0 && props.Exists("window_radii") && window != nullptr)
        {
            submittedBeforeShow = harness.Engine.Jobs().Stats().SubmittedJobs;
            undoBeforeShow = harness.History().UndoCount();
            ImGui::ActivateItemByID(window->GetID("Show radii"));
            showFrame = frame;
        }
        (void)ui->Windows().DrawOpenWindows();
        ImGui::Render();
        harness.Frame();
    }
    ImGui::DestroyContext(context);
    ASSERT_GT(showFrame, 0) << "the Estimate radii button started a run that published";
    EXPECT_EQ(harness.Engine.Jobs().Stats().SubmittedJobs, submittedBeforeShow) << "Show ran nothing";
    EXPECT_EQ(undoBeforeShow, 1u) << "one run, one undo entry";
    const auto* overrides = scene.Raw().try_get<Extrinsic::Graphics::Components::VisualizationLaneOverrides>(entity);
    ASSERT_NE(overrides, nullptr);
    ASSERT_TRUE(overrides->Points);
    EXPECT_EQ(overrides->Points->ScalarFieldName, "window_radii");
    ASSERT_NE(PS::Latest(), nullptr);
    ASSERT_TRUE(PS::Latest()->Final);
    EXPECT_EQ(PS::Latest()->Final->Status, "applied");
}
