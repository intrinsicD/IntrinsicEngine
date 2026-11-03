// ARCH-022: the FPFH descriptor analysis plugin's contract, in an engine composed as the Sandbox composes it
// (in-process startup plugin, so it runs in static builds too): its section (including a payload saved by the
// built-in feature it replaced), every point domain on both CPU backends with one undo entry, its stale and
// cancelled runs, its preflight refusals, typed and atomic multi-column publication, its MCP tool and its window.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>

#include "DescriptorAnalysisHarness.hpp"
#include "PointDomainFixture.hpp"

import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Geometry.PointCloud;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.Runtime.EditorUiHost;
import Extrinsic.Runtime.GeometryProperty.Types;
import Extrinsic.Runtime.SelectionController;

namespace
{
    using namespace Intrinsic::Tests::DescriptorAnalysis;
    using D = R::GeometryElementDomain;
    using Intrinsic::Tests::PointDomainProperties;
    namespace GS = Extrinsic::ECS::Components::GeometrySources;

    // The shared sample grid; a point cloud also gets a deleted NaN row 4.
    entt::entity Make(Extrinsic::ECS::Scene::Registry& scene, const D domain)
    {
        const auto entity = Intrinsic::Tests::PointCloudEditing::MakeOrientedSamples(scene, domain);
        if (domain == D::PointCloudPoint)
        {
            auto& props = PointDomainProperties(scene, entity, domain);
            props.GetOrAdd<bool>("v:deleted")[4] = true;
            props.Get<glm::vec3>("samples")[4] = {std::numeric_limits<float>::quiet_NaN(), 0, 0};
        }
        return entity;
    }

    DA::Config Settings(const entt::entity entity, const D domain)
    {
        return {.StableEntityId = R::SelectionController::ToStableEntityId(entity),
                .Positions = {.Domain = domain, .Name = "samples", .ValueKind = Geometry::PropertyValueKind::Vec3},
                .Normals = {.Domain = domain, .Name = "directions", .ValueKind = Geometry::PropertyValueKind::Vec3},
                .Outputs = DA::MakeOutputs(domain, "descriptor"),
                .FeatureRadius = 10};
    }

    bool Usable(const std::string& payload) { return R::ValidateDerivedSection<DA::kSection>(payload, {}, "test").Usable(); }

    std::vector<float> Column(const Geometry::PropertySet& props, const std::string& name)
    {
        return props.Get<float>(name).Vector();
    }
}

TEST(DescriptorAnalysisPlugin, TheSectionKeepsItsNameAndRefusesInvalidSettings)
{
    EXPECT_EQ(DA::kSectionName, "sandbox.descriptor_analysis");
    EXPECT_EQ(DA::kSectionSchemaId, "intrinsic.runtime.sandbox.descriptor_analysis");
    Extrinsic::ECS::Scene::Registry scene;
    const auto config = Settings(Make(scene, D::MeshFace), D::MeshFace);
    const auto payload = R::SerializeDerivedSection<DA::kSection>(config);
    ASSERT_TRUE(Usable(payload)) << payload;
    Extrinsic::Core::Config::EngineConfig document{};
    R::SetDerivedSection<DA::kSection>(document, config);
    const auto read = R::GetDerivedSection<DA::kSection>(document);
    ASSERT_TRUE(read);
    EXPECT_EQ(read->Outputs, config.Outputs);
    EXPECT_EQ(R::SerializeDerivedSection<DA::kSection>(*read), payload);
    for (const auto* bad : {R"({"backend":"vulkan"})", R"({"max_neighbors":-1})", R"({"outputs":[]})",
                            R"({"gpu_query_batch_size":0})", R"({"gpu_radius_capacity":0})",
                            R"({"gpu_radius_capacity":1025})", R"({"feature_radius":1e100})",
                            R"({"feature_radius":1e-100})", R"({"unknown":1})"})
        EXPECT_FALSE(Usable(bad)) << bad;
    auto invalid = config;
    invalid.Outputs[32] = invalid.Outputs[0];
    EXPECT_FALSE(Usable(R::SerializeDerivedSection<DA::kSection>(invalid))) << "duplicate output names";
    invalid = config;
    invalid.Normals.Domain = D::GraphEdge;
    EXPECT_FALSE(Usable(R::SerializeDerivedSection<DA::kSection>(invalid))) << "normals on another domain";
    invalid = config;
    invalid.Outputs[3].Domain = D::MeshVertex;
    EXPECT_FALSE(Usable(R::SerializeDerivedSection<DA::kSection>(invalid))) << "an output on another domain";
    invalid = config;
    invalid.Outputs[7].Name = "directions";
    EXPECT_FALSE(Usable(R::SerializeDerivedSection<DA::kSection>(invalid))) << "an output named like an input";
    invalid = config;
    invalid.Outputs[5].ValueKind = Geometry::PropertyValueKind::Vec3;
    EXPECT_FALSE(Usable(R::SerializeDerivedSection<DA::kSection>(invalid))) << "a vector output";
    Json short_ = Json::parse(payload);
    short_["outputs"].erase(short_["outputs"].begin());
    EXPECT_FALSE(Usable(short_.dump())) << "32 outputs";
    Json long_ = Json::parse(payload);
    long_["outputs"].push_back(long_["outputs"][0]);
    long_["outputs"].back()["name"] = "descriptor.extra";
    EXPECT_FALSE(Usable(long_.dump())) << "34 outputs";
}

TEST(DescriptorAnalysisPlugin, APayloadTheBuiltInFeatureSavedLoadsAtBootUnchanged)
{
    // config_get of the built-in section at 60de92325: names with spaces and slashes, float and double columns,
    // non-default limits.
    std::ifstream file{std::filesystem::path{INTRINSIC_SOURCE_DIR} / "tests/data/descriptor_analysis_saved_section.json"};
    ASSERT_TRUE(file) << "the saved section fixture";
    std::stringstream text;
    text << file.rdbuf();
    const Json saved = Json::parse(text.str());
    auto boot = HeadlessConfig();
    Config::UpsertEngineConfigSection(boot.AppSections, {.Name = std::string{DA::kSectionName},
                                                         .SchemaId = std::string{DA::kSectionSchemaId},
                                                         .SchemaVersion = 1u,
                                                         .PayloadJson = saved.dump()});
    Harness harness{std::move(boot)};
    const auto active = R::ActivePluginSection<DA::kSection>();
    ASSERT_TRUE(active) << "the saved payload is usable";
    EXPECT_EQ(Json::parse(R::SerializeDerivedSection<DA::kSection>(*active)), saved);
    EXPECT_EQ(active->StableEntityId, 12u);
    EXPECT_EQ(active->Backend, DA::DescriptorBackend::CpuLBVH);
    EXPECT_EQ(active->MaxNeighbors, 17u);
    EXPECT_EQ(active->GpuQueryBatchSize, 512u);
    EXPECT_EQ(active->GpuRadiusCapacity, 64u);
    EXPECT_FLOAT_EQ(active->FeatureRadius, .25f);
    EXPECT_EQ(active->Outputs[1].Name, "saved/bin 1");
    EXPECT_EQ(active->Outputs[1].ValueKind, Geometry::PropertyValueKind::Double);
    EXPECT_EQ(active->Outputs[0].Domain, D::PointCloudPoint);
}

TEST(DescriptorAnalysisPlugin, EveryDomainWritesLiveRowsWithOneUndoEntryAndBothCpuBackendsAgree)
{
    for (unsigned d = 1; d <= 8; ++d)
    {
        SCOPED_TRACE(d);
        Harness harness{};
        auto& scene = harness.Scene();
        const auto entity = Make(scene, D(d));
        const auto config = Settings(entity, D(d));
        auto& props = PointDomainProperties(scene, entity, D(d));
        const auto size = props.Size();
        const bool half = D(d) == D::MeshHalfedge || D(d) == D::GraphHalfedge;
        if (half) scene.Raw().get<GS::Edges>(entity).Properties.GetOrAdd<bool>("e:deleted")[1] = true;
        else
            props.GetOrAdd<bool>(D(d) == D::MeshFace ? "f:deleted"
                                 : (D(d) == D::MeshEdge || D(d) == D::GraphEdge) ? "e:deleted"
                                                                                 : "v:deleted")[2] = true;
        for (const auto& output : config.Outputs) props.GetOrAdd<float>(output.Name).Vector().assign(size, 77);
        const auto positions = std::as_const(props).Get<glm::vec3>("samples").Revision();
        const auto normals = std::as_const(props).Get<glm::vec3>("directions").Revision();
        EXPECT_EQ(harness.Settled(config), "");

        const auto kdtree = harness.Wait(*DA::Start(config));
        ASSERT_EQ(kdtree.Status, "applied") << kdtree.Message;
        EXPECT_EQ(kdtree.ActualBackend, "cpu_kdtree");
        EXPECT_GT(kdtree.MeanSpacing, 0.f);
        EXPECT_FLOAT_EQ(kdtree.FeatureRadius, config.FeatureRadius);
        std::array<std::vector<float>, DA::kBins> expected;
        for (std::size_t b = 0; b < DA::kBins; ++b)
        {
            expected[b] = Column(props, config.Outputs[b].Name);
            ASSERT_EQ(expected[b].size(), size);
            EXPECT_EQ(expected[b][2], 77) << "a deleted row keeps its value";
            if (half) EXPECT_EQ(expected[b][3], 77);
        }
        EXPECT_NE(expected[0][0], 77) << "a live row was written";
        EXPECT_EQ(std::as_const(props).Get<glm::vec3>("samples").Revision(), positions);
        EXPECT_EQ(std::as_const(props).Get<glm::vec3>("directions").Revision(), normals);
        props.Get<float>("keep")[0] = 99; // an unrelated edit does not make the entry stale
        ASSERT_TRUE(harness.History().Undo().Succeeded());
        for (const auto& output : config.Outputs) EXPECT_EQ(Column(props, output.Name), std::vector<float>(size, 77));
        ASSERT_TRUE(harness.History().Redo().Succeeded());
        EXPECT_EQ(std::as_const(props).Get<float>("keep")[0], 99);

        auto indexed = config;
        indexed.Backend = DA::DescriptorBackend::CpuLBVH;
        for (int frame = 0; frame < 20; ++frame) harness.Frame(); // the spatial cache builds its index
        const auto lbvh = harness.Wait(*DA::Start(indexed));
        ASSERT_EQ(lbvh.Status, "applied") << lbvh.Message;
        EXPECT_EQ(lbvh.ActualBackend, "cpu_lbvh");
        EXPECT_EQ(lbvh.WrittenCount, kdtree.WrittenCount);
        for (std::size_t b = 0; b < DA::kBins; ++b) EXPECT_EQ(Column(props, config.Outputs[b].Name), expected[b]) << b;
        EXPECT_FLOAT_EQ(lbvh.MeanSpacing, kdtree.MeanSpacing);
        EXPECT_FLOAT_EQ(lbvh.FeatureRadius, kdtree.FeatureRadius);
        EXPECT_TRUE(harness.Wait(*DA::Start(indexed)).IndexReused);

        // "Show histogram bin": the bin as the entity's scalar colours, where the domain has a lane.
        std::string diagnostic;
        const bool shown = harness.Editing().ShowProperty(config.StableEntityId, config.Outputs[32], diagnostic);
        EXPECT_EQ(shown, !half) << diagnostic;
        if (half) continue;
        const auto* overrides = scene.Raw().try_get<Extrinsic::Graphics::Components::VisualizationLaneOverrides>(entity);
        ASSERT_NE(overrides, nullptr);
        const auto& lane = D(d) == D::MeshVertex || D(d) == D::MeshFace ? overrides->Surface
            : D(d) == D::PointCloudPoint                                ? overrides->Points
                                                                        : overrides->Edges;
        ASSERT_TRUE(lane);
        EXPECT_EQ(lane->Source, Extrinsic::Graphics::Components::VisualizationConfig::ColorSource::ScalarField);
        EXPECT_EQ(lane->ScalarFieldName, config.Outputs[32].Name);
    }
}

TEST(DescriptorAnalysisPlugin, AChangedInputOrOutputOrACancelEndsTheRunWithoutAWrite)
{
    // 0 unrelated edit (applies), 1 positions, 2 a deletion, 3 the first output written, 4 cancel, 5 normals, 6 the
    // last output written, 7 positions changed and then cancelled (the inputs changed: stale, not cancelled).
    const char* expected[]{"applied", "stale", "stale", "stale", "cancelled", "stale", "stale", "stale"};
    for (unsigned change = 0; change < 8; ++change)
    {
        SCOPED_TRACE(change);
        Harness harness{};
        auto& scene = harness.Scene();
        const auto entity = Make(scene, D::MeshVertex);
        const auto config = Settings(entity, D::MeshVertex);
        auto& props = PointDomainProperties(scene, entity, D::MeshVertex);
        const auto run = DA::Start(config);
        ASSERT_FALSE(run->Final) << run->Final->Message;
        switch (change)
        {
        case 0: props.Get<float>("keep")[0] = 99; break;
        case 1: props.Get<glm::vec3>("samples")[0].x += 1; break;
        case 2: props.GetOrAdd<bool>("v:deleted")[0] = true; break;
        case 3: props.GetOrAdd<float>(config.Outputs[0].Name)[0] = 77; break;
        case 4: EXPECT_TRUE(R::CancelPluginRun(*run)); break;
        case 5: props.Get<glm::vec3>("directions")[0].x += 1; break;
        case 6: props.GetOrAdd<float>(config.Outputs[32].Name)[0] = 77; break;
        case 7:
            props.Get<glm::vec3>("samples")[0].x += 1;
            EXPECT_TRUE(R::CancelPluginRun(*run));
            break;
        }
        const auto result = harness.Wait(*run);
        EXPECT_EQ(result.Status, expected[change]) << result.Message;
        EXPECT_EQ(harness.History().CanUndo(), change == 0);
        for (std::size_t b = 0; b < DA::kBins; ++b)
            EXPECT_EQ(props.Exists(config.Outputs[b].Name), change == 0 || (change == 3 && b == 0) || (change == 6 && b == 32));
    }
}

TEST(DescriptorAnalysisPlugin, InvalidOutputsNormalsAndScaleAreRefusedAndKeepTheData)
{
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = Make(scene, D::MeshVertex);
    const auto config = Settings(entity, D::MeshVertex);
    auto& props = PointDomainProperties(scene, entity, D::MeshVertex);
    for (const auto* name : {"samples", "directions", "v:deleted", "v:connectivity"})
    {
        auto bad = config;
        bad.Outputs[32].Name = name;
        EXPECT_NE(harness.Settled(bad), "") << name;
    }
    auto unrelated = config;
    unrelated.Outputs[32].Name = "h:connectivity"; // a halfedge name, not a vertex topology property
    EXPECT_EQ(harness.Settled(unrelated), "");
    auto gpu = config;
    gpu.Backend = DA::DescriptorBackend::VulkanLBVH;
    EXPECT_NE(harness.Settled(gpu), "") << "no GPU queries without a device";
    props.GetOrAdd<float>(config.Outputs[0].Name).Vector().assign(props.Size(), 77);
    props.Get<glm::vec3>("directions")[0] = {0, 0, 0};
    EXPECT_NE(harness.Settled(config).find("nonzero"), std::string::npos);
    EXPECT_EQ(harness.Wait(*DA::Start(config)).Status, "invalid");
    props.Get<glm::vec3>("directions")[0] = {0, 0, 1};
    props.Get<glm::vec3>("samples").Vector().assign(props.Size(), glm::vec3(0)); // no spacing
    const auto degenerate = harness.Wait(*DA::Start(config));
    EXPECT_EQ(degenerate.Status, "failed") << degenerate.Message;
    EXPECT_EQ(Column(props, config.Outputs[0].Name), std::vector<float>(props.Size(), 77));
    for (std::size_t b = 1; b < DA::kBins; ++b) EXPECT_FALSE(props.Exists(config.Outputs[b].Name));
    EXPECT_FALSE(harness.History().CanUndo());
}

TEST(DescriptorAnalysisPlugin, TheResolvedScaleKeepsAutomaticAndExplicitRadii)
{
    constexpr std::array<glm::vec3, 4> plane{{{0, 0, 0}, {2, 0, 0}, {0, 3, 0}, {2, 3, 0}}};
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = Make(scene, D::PointCloudPoint);
    auto positions = PointDomainProperties(scene, entity, D::PointCloudPoint).Get<glm::vec3>("samples");
    for (std::size_t i = 0; i < plane.size(); ++i) positions[i] = plane[i];
    for (int frame = 0; frame < 20; ++frame) harness.Frame();
    for (const auto backend : {DA::DescriptorBackend::CpuKDTree, DA::DescriptorBackend::CpuLBVH})
        for (const bool automatic : {false, true})
        {
            auto config = Settings(entity, D::PointCloudPoint);
            config.Backend = backend;
            config.FeatureRadius = automatic ? 0.f : 7.f;
            const auto result = harness.Wait(*DA::Start(config));
            ASSERT_EQ(result.Status, "applied") << result.Message;
            EXPECT_EQ(result.LiveCount, 4u);
            EXPECT_FLOAT_EQ(result.MeanSpacing, 2.f);
            EXPECT_FLOAT_EQ(result.FeatureRadius, automatic ? 10.f : 7.f);
        }
}

TEST(DescriptorAnalysisPlugin, DoubleColumnsPublishAsOneHistoryEntry)
{
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = Make(scene, D::MeshFace);
    auto config = Settings(entity, D::MeshFace);
    for (auto& output : config.Outputs) output.ValueKind = Geometry::PropertyValueKind::Double;
    const auto result = harness.Wait(*DA::Start(config));
    ASSERT_EQ(result.Status, "applied") << result.Message;
    auto& props = PointDomainProperties(scene, entity, D::MeshFace);
    for (const auto& output : config.Outputs) EXPECT_TRUE(std::as_const(props).Get<double>(output.Name)) << output.Name;
    EXPECT_EQ(harness.History().UndoCount(), 1u);
    ASSERT_TRUE(harness.History().Undo().Succeeded());
    for (const auto& output : config.Outputs) EXPECT_FALSE(props.Exists(output.Name));
    ASSERT_TRUE(harness.History().Redo().Succeeded());
    for (const auto& output : config.Outputs) EXPECT_TRUE(std::as_const(props).Get<double>(output.Name));
}

// The 33 columns publish all or none: a column whose storage cannot hold its values exactly refuses the whole
// publication before history runs, and an undo after any one column changed is refused without restoring the others.
TEST(DescriptorAnalysisPlugin, MultiColumnPublicationIsAtomicForPreparationAndUndoGuards)
{
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = Make(scene, D::MeshVertex);
    auto& props = PointDomainProperties(scene, entity, D::MeshVertex);
    const auto config = Settings(entity, D::MeshVertex);
    ASSERT_EQ(harness.Wait(*DA::Start(config)).Status, "applied");
    EXPECT_EQ(harness.History().UndoCount(), 1u);
    std::array<std::vector<float>, DA::kBins> published;
    for (std::size_t b = 0; b < DA::kBins; ++b) published[b] = Column(props, config.Outputs[b].Name);
    std::size_t fractional = DA::kBins; // the last column holding a fractional value cannot be stored as Int32
    for (std::size_t b = 0; b < DA::kBins; ++b)
        if (std::ranges::any_of(published[b], [](const float v) { return v != std::trunc(v); })) fractional = b;
    ASSERT_LT(fractional, DA::kBins);
    ASSERT_TRUE(harness.History().Undo().Succeeded());
    auto inexact = config;
    inexact.Outputs[fractional].ValueKind = Geometry::PropertyValueKind::Int32;
    const auto refused = harness.Wait(*DA::Start(inexact));
    EXPECT_EQ(refused.Status, "invalid");
    EXPECT_NE(refused.Message.find("represented exactly"), std::string::npos) << refused.Message;
    EXPECT_FALSE(harness.History().CanUndo());
    for (const auto& output : inexact.Outputs) EXPECT_FALSE(props.Exists(output.Name)) << output.Name;

    ASSERT_TRUE(harness.History().Redo().Succeeded());
    props.Get<float>(config.Outputs[32].Name)[0] = -5.f;
    EXPECT_FALSE(harness.History().Undo().Succeeded());
    for (std::size_t b = 0; b < 32; ++b) EXPECT_EQ(Column(props, config.Outputs[b].Name), published[b]) << b;
    EXPECT_EQ(std::as_const(props).Get<float>(config.Outputs[32].Name)[0], -5.f);
}

TEST(DescriptorAnalysisPlugin, TheToolAnswersWhenTheRunEndsAndTheRunIsAJobsWindowRow)
{
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = Make(scene, D::MeshVertex);
    const auto config = Settings(entity, D::MeshVertex);
    ASSERT_TRUE(harness.Apply(config));
    const auto preview = harness.Call("descriptor_analysis_preview");
    ASSERT_FALSE(preview.IsError) << preview.Text;
    const auto reply = harness.Call("descriptor_analysis");
    ASSERT_FALSE(reply.IsError) << reply.Text;
    const Json result = Json::parse(reply.Text);
    EXPECT_EQ(result["status"], "applied");
    EXPECT_EQ(result["outputs"].size(), DA::kBins);
    EXPECT_EQ(result["outputs"][0], config.Outputs[0].Name);
    EXPECT_EQ(result["written_count"], PointDomainProperties(scene, entity, D::MeshVertex).Size());
    const auto jobs = harness.Jobs();
    ASSERT_EQ(jobs.size(), 1u);
    EXPECT_EQ(jobs.front().Identity.EntityId, config.StableEntityId);
    EXPECT_EQ(jobs.front().Identity.OutputName, config.Outputs[0].Name);
    EXPECT_EQ(jobs.front().State, R::JobState::Published);
}

TEST(DescriptorAnalysisPlugin, TheWindowRunsTheAppliedSettingsAndTheBinFollowsOnlyAfterAShow)
{
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = Make(scene, D::MeshVertex);
    auto& props = PointDomainProperties(scene, entity, D::MeshVertex);
    const auto config = Settings(entity, D::MeshVertex);
    ASSERT_TRUE(harness.Apply(config));
    ASSERT_EQ(harness.Settled(config), "");
    auto* ui = harness.Engine.Services().Find<R::EditorUiHost>();
    ASSERT_NE(ui, nullptr);
    ASSERT_TRUE(ui->SetWindowOpen("view.descriptor_analysis", true));
    const auto shownBin = [&]() -> std::string
    {
        const auto* overrides = scene.Raw().try_get<Extrinsic::Graphics::Components::VisualizationLaneOverrides>(entity);
        return overrides != nullptr && overrides->Surface ? overrides->Surface->ScalarFieldName : std::string{};
    };

    IMGUI_CHECKVERSION();
    ImGuiContext* context = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280, 1600);
    io.IniFilename = nullptr;
    unsigned char* pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    std::string beforeShow{}, afterShow{};
    const auto editBin = [&](const int frame, const int start, const char* value, ImGuiWindow* window)
    {
        if (frame == start)
        {
            ImGui::ActivateItemByID(window->GetID("Display histogram bin"));
            ImGui::GetCurrentContext()->NavNextActivateFlags = ImGuiActivateFlags_PreferInput;
        }
        if (frame == start + 2) io.AddInputCharactersUTF8(value);
        if (frame == start + 4) io.AddKeyEvent(ImGuiKey_Enter, true);
        if (frame == start + 5) io.AddKeyEvent(ImGuiKey_Enter, false);
    };
    for (int frame = 0; frame < 90; ++frame)
    {
        io.DeltaTime = 1.f / 60.f;
        ImGui::NewFrame();
        if (auto* window = ImGui::FindWindowByName("FPFH Descriptor Analysis###view.descriptor_analysis"))
        {
            ImGui::FocusWindow(window);
            if (frame == 2) ImGui::ActivateItemByID(window->GetID("Run"));
            editBin(frame, 30, "7", window); // before any Show: the viewport does not follow
            if (frame == 45) ImGui::ActivateItemByID(window->GetID("Show histogram bin"));
            editBin(frame, 55, "19", window); // after a successful Show it does
        }
        (void)ui->Windows().DrawOpenWindows();
        ImGui::Render();
        harness.Frame();
        if (frame == 40) beforeShow = shownBin();
        if (frame == 52) afterShow = shownBin();
    }
    ImGui::DestroyContext(context);
    EXPECT_TRUE(props.Exists(config.Outputs[0].Name)) << "the Run button started a run that published";
    EXPECT_EQ(harness.History().UndoCount(), 3u) << "the run, then the two displays (each display is undoable)";
    EXPECT_EQ(beforeShow, "") << "editing the bin before a Show shows nothing";
    EXPECT_EQ(afterShow, config.Outputs[7].Name) << "Show displays the selected bin";
    EXPECT_EQ(shownBin(), config.Outputs[19].Name) << "after a Show the viewport follows the slider";
}

namespace
{
    // FNV-1a over the bytes, as the fixture's columns_fnv1a64.
    std::string Fnv1a64(const std::vector<float>& values)
    {
        std::uint64_t hash = 0xcbf29ce484222325ull;
        for (const auto byte : std::as_bytes(std::span{values}))
            hash = (hash ^ std::uint64_t(byte)) * 0x100000001b3ull;
        char text[17]{};
        std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(hash));
        return text;
    }
}

TEST(DescriptorAnalysisPlugin, BothCpuBackendsReproduceTheBuiltInFeaturesGoldenColumns)
{
    std::ifstream file{std::filesystem::path{INTRINSIC_SOURCE_DIR} / "tests/data/descriptor_golden_cloud.json"};
    ASSERT_TRUE(file) << "the golden fixture";
    const Json golden = Json::parse(file);
    Geometry::PointCloud::Cloud cloud;
    cloud.EnableNormals();
    for (const auto& point : golden["points"])
        (void)cloud.AddPoint({point[0].get<float>(), point[1].get<float>(), point[2].get<float>()});
    const auto normals = cloud.Normals();
    for (std::size_t i = 0; i < normals.size(); ++i)
    {
        const auto& normal = golden["normals"][i];
        normals[i] = {normal[0].get<float>(), normal[1].get<float>(), normal[2].get<float>()};
    }
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = scene.Raw().create();
    GS::PopulateFromCloud(scene.Raw(), entity, cloud);
    for (int frame = 0; frame < 20; ++frame) harness.Frame(); // the spatial cache builds its index
    for (const auto backend : {DA::DescriptorBackend::CpuKDTree, DA::DescriptorBackend::CpuLBVH})
    {
        SCOPED_TRACE(DA::ToString(backend));
        auto config = Settings(entity, D::PointCloudPoint);
        config.Positions.Name = "v:position";
        config.Normals.Name = "v:normal";
        config.FeatureRadius = 0; // automatic, as the golden run
        config.Backend = backend;
        config.Outputs = DA::MakeOutputs(D::PointCloudPoint, std::string{"g_"} + DA::ToString(backend));
        const auto result = harness.Wait(*DA::Start(config));
        ASSERT_EQ(result.Status, "applied") << result.Message;
        const auto& props = PointDomainProperties(scene, entity, D::PointCloudPoint);
        std::vector<float> rows(golden["points"].size() * DA::kBins);
        for (std::size_t b = 0; b < DA::kBins; ++b)
        {
            const auto column = Column(props, config.Outputs[b].Name);
            ASSERT_EQ(column.size(), golden["points"].size());
            for (std::size_t i = 0; i < column.size(); ++i) rows[i * DA::kBins + b] = column[i];
        }
        for (std::size_t b = 0; b < DA::kBins; ++b) EXPECT_EQ(rows[b], golden["first_row"][b].get<float>()) << b;
        EXPECT_EQ(Fnv1a64(rows), golden["columns_fnv1a64"].get<std::string>());
    }
}

TEST(DescriptorAnalysisPlugin, AnUnloadAndAReloadKeepTheHistoryAndTheToolRunsAgain)
{
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = Make(scene, D::MeshVertex);
    auto& props = PointDomainProperties(scene, entity, D::MeshVertex);
    const auto config = Settings(entity, D::MeshVertex);
    ASSERT_TRUE(harness.Apply(config));
    ASSERT_FALSE(harness.Call("descriptor_analysis").IsError);
    const auto written = Column(props, config.Outputs[7].Name);
    const auto unloaded = harness.Call("plugin_unload", {{"plugin", "descriptor_analysis"}});
    ASSERT_FALSE(unloaded.IsError) << unloaded.Text;
    EXPECT_EQ(harness.Registry.Find("descriptor_analysis"), nullptr);
    // The undo entry holds host data: it works while the plugin is gone.
    ASSERT_TRUE(harness.History().Undo().Succeeded());
    EXPECT_FALSE(props.Exists(config.Outputs[7].Name));
    ASSERT_TRUE(harness.History().Redo().Succeeded());
    EXPECT_EQ(Column(props, config.Outputs[7].Name), written);
    const auto loaded = harness.Call("plugin_load", {{"path", "descriptor_analysis"}});
    ASSERT_FALSE(loaded.IsError) << loaded.Text;
    // The section kept its applied payload, and the tool runs again on it.
    const auto active = R::ActivePluginSection<DA::kSection>();
    ASSERT_TRUE(active);
    EXPECT_EQ(active->Outputs, config.Outputs);
    ASSERT_TRUE(harness.History().Undo().Succeeded());
    const auto again = harness.Call("descriptor_analysis");
    ASSERT_FALSE(again.IsError) << again.Text;
    EXPECT_EQ(Column(props, config.Outputs[7].Name), written);
}

namespace
{
    // The combo popup open in the last frame (ImGui names them "##Combo_NN").
    ImGuiWindow* OpenComboPopup()
    {
        for (ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
            if (window->WasActive && std::string_view{window->Name}.starts_with("##Combo_")) return window;
        return nullptr;
    }
}

TEST(DescriptorAnalysisPlugin, ThePositionsPickerMovesTheOutputsToItsDomainAndKeepsAnUnlistedChoice)
{
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = Make(scene, D::MeshVertex);
    auto& faces = PointDomainProperties(scene, entity, D::MeshFace);
    faces.GetOrAdd<glm::vec3>("face_samples").Vector().assign(faces.Size(), glm::vec3(1, 2, 3));
    auto config = Settings(entity, D::MeshVertex);
    config.Positions.Name = "unlisted"; // not in the entity's catalog
    ASSERT_TRUE(harness.Apply(config));
    auto* ui = harness.Engine.Services().Find<R::EditorUiHost>();
    ASSERT_NE(ui, nullptr);
    ASSERT_TRUE(ui->SetWindowOpen("view.descriptor_analysis", true));
    const std::string face = "face_samples (" + std::string{R::ToString(D::MeshFace)} + ")";

    IMGUI_CHECKVERSION();
    ImGuiContext* context = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280, 1600);
    io.IniFilename = nullptr;
    unsigned char* pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    std::optional<DA::Config> afterOpenAndClose{}, afterChoice{};
    for (int frame = 0; frame < 40; ++frame)
    {
        io.DeltaTime = 1.f / 60.f;
        ImGui::NewFrame();
        if (auto* window = ImGui::FindWindowByName("FPFH Descriptor Analysis###view.descriptor_analysis"))
        {
            if (frame == 1) ImGui::FocusWindow(window); // once: focusing closes open popups
            // Open the picker and close it without a choice, then apply: the unlisted positions stay.
            if (frame == 2 || frame == 4 || frame == 12) ImGui::ActivateItemByID(window->GetID("Positions")); // 4 closes
            if (frame == 8 || frame == 18) ImGui::ActivateItemByID(window->GetID("Apply settings"));
            // Open it again and choose the face property, then apply.
            if (frame == 14)
                if (auto* popup = OpenComboPopup()) ImGui::ActivateItemByID(popup->GetID(face.c_str()));
        }
        (void)ui->Windows().DrawOpenWindows();
        ImGui::Render();
        harness.Frame();
        if (frame == 10) afterOpenAndClose = R::ActivePluginSection<DA::kSection>();
        if (frame == 20) afterChoice = R::ActivePluginSection<DA::kSection>();
    }
    ImGui::DestroyContext(context);
    ASSERT_TRUE(afterOpenAndClose);
    EXPECT_EQ(afterOpenAndClose->Positions.Name, "unlisted") << "a choice the catalog does not list is kept";
    ASSERT_TRUE(afterChoice);
    EXPECT_EQ(afterChoice->Positions.Name, "face_samples");
    EXPECT_EQ(afterChoice->Positions.Domain, D::MeshFace);
    for (const auto& output : afterChoice->Outputs) EXPECT_EQ(output.Domain, D::MeshFace) << output.Name;
    EXPECT_EQ(afterChoice->Normals.Domain, D::MeshFace) << "the normals follow the positions' domain";
}
