// ARCH-022: the bilateral filter plugin's contract, in an engine composed as the Sandbox composes it (in-process
// startup plugin, so it runs in static builds too): its section, its MCP tool, its Jobs window rows, every point
// domain on both CPU backends with one undo entry, its stale and cancelled runs, and its window's Run button.
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>

#include "BilateralFilterHarness.hpp"
#include "PointDomainFixture.hpp"

import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Runtime.EditorUiHost;
import Extrinsic.Runtime.GeometryProperty.Types;
import Extrinsic.Runtime.SelectionController;

namespace
{
    using namespace Intrinsic::Tests::BilateralFilter;
    using D = R::GeometryElementDomain;
    using Intrinsic::Tests::PointDomainProperties;

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

    BF::Config Settings(const entt::entity entity, const D domain)
    {
        return {.StableEntityId = R::SelectionController::ToStableEntityId(entity),
                .Positions = {.Domain = domain, .Name = "samples", .ValueKind = Geometry::PropertyValueKind::Vec3},
                .Normals = {.Domain = domain, .Name = "directions", .ValueKind = Geometry::PropertyValueKind::Vec3},
                .Output = {.Domain = domain, .Name = "filtered", .ValueKind = Geometry::PropertyValueKind::Vec3},
                .KNeighbors = 2,
                .SpatialSigma = 2,
                .Iterations = 3};
    }

    bool Usable(const std::string& payload)
    {
        return R::ValidateDerivedSection<BF::kSection>(payload, {}, "test").Usable();
    }
}

TEST(BilateralFilterPlugin, TheSectionKeepsItsNameAndRejectsInvalidSettings)
{
    EXPECT_EQ(BF::kSectionName, "sandbox.bilateral_filter"); // saved configs of the built-in filter still load
    for (const auto* payload : {R"({"backend":"vulkan"})", R"({"k_neighbors":-1})", R"({"spatial_sigma":-1})",
                                R"({"gpu_query_batch_size":0})", R"({"gpu_query_batch_size":16385})",
                                R"({"spatial_sigma":1e100})", R"({"unknown":1})",
                                R"({"output":{"domain":"unknown","name":"x","kind":"uint32"}})",
                                R"({"iterations":101})", R"({"normal_sigma":-1})"})
        EXPECT_FALSE(Usable(payload)) << payload;
    for (const auto* token : {"cpu_octree", "cpu_lbvh", "vulkan_lbvh"})
        EXPECT_TRUE(Usable(Json{{"backend", token}}.dump())) << token;
    EXPECT_TRUE(Usable(R"({"iterations":100,"gpu_query_batch_size":16384})"));
    BF::Config mixed{};
    mixed.Normals.Domain = D::MeshFace;
    EXPECT_FALSE(Usable(R::SerializeDerivedSection<BF::kSection>(mixed))); // inputs on two domains
    BF::Config overwrite{};
    overwrite.Output.Name = overwrite.Normals.Name;
    EXPECT_FALSE(Usable(R::SerializeDerivedSection<BF::kSection>(overwrite)));
    const auto config = R::GetDerivedSection<BF::kSection>([]
    {
        auto c = HeadlessConfig();
        R::SetDerivedSection<BF::kSection>(c, BF::Config{.StableEntityId = 7u, .Iterations = 4u});
        return c;
    }());
    ASSERT_TRUE(config);
    EXPECT_EQ(config->StableEntityId, 7u);
    EXPECT_EQ(config->Iterations, 4u);
}

TEST(BilateralFilterPlugin, TheToolAnswersWithTheLastLivePassAndTheRunIsAJobsWindowRow)
{
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = Make(scene, D::PointCloudPoint);
    auto& props = PointDomainProperties(scene, entity, D::PointCloudPoint);
    constexpr std::array<glm::vec3, 4> plane{{{0, 0, 0}, {2, 0, 0}, {0, 3, 0}, {2, 3, 0}}};
    std::ranges::copy(plane, props.Get<glm::vec3>("samples").Vector().begin());
    props.Get<glm::vec3>("directions")[0] = {}; // a zero normal: that point stays where it is
    ASSERT_NE(harness.Registry.Find("bilateral_filter"), nullptr);
    auto config = Settings(entity, D::PointCloudPoint);
    for (const auto backend : {BF::FilterBackend::CpuOctree, BF::FilterBackend::CpuLBVH})
        for (const auto iterations : {0u, 3u})
        {
            SCOPED_TRACE(BF::ToString(backend));
            SCOPED_TRACE(iterations);
            config.Backend = backend;
            config.Iterations = iterations;
            ASSERT_TRUE(harness.Apply(config));
            const auto reply = harness.Call("bilateral_filter");
            ASSERT_FALSE(reply.IsError) << reply.ErrorCode << ": " << reply.Text;
            const auto result = Json::parse(reply.Text);
            EXPECT_EQ(result["status"], "applied");
            EXPECT_EQ(result["actual_backend"], BF::ToString(backend));
            EXPECT_EQ(result["slot_count"], 5u);
            EXPECT_EQ(result["live_count"], 4u);
            EXPECT_EQ(result["completed_iterations"], iterations);
            EXPECT_EQ(result["points_filtered"], iterations ? 3u : 0u);
            EXPECT_EQ(result["degenerate_normals"], iterations ? 1u : 0u);
            EXPECT_EQ(result["average_displacement"], 0.0); // a plane with normals along z does not move
            EXPECT_EQ(result["max_displacement"], 0.0);
        }
    const auto jobs = harness.Jobs();
    EXPECT_TRUE(std::ranges::any_of(jobs, [](const auto& job) { return job.Name == "Bilateral point filter"; }))
        << "plugin runs are rows of the shell's Jobs window";
    EXPECT_EQ(harness.Settled(config), ""); // the tool answers what the window shows once validation settled
    const auto ready = Json::parse(harness.Call("bilateral_filter_preview").Text);
    EXPECT_EQ(ready["ready"], true) << ready.dump();
}

TEST(BilateralFilterPlugin, EveryDomainFiltersLiveRowsWithOneUndoEntryAndTheBackendsAgree)
{
    for (unsigned d = 1; d <= 8; ++d)
        for (const bool inPlace : {false, true})
        {
            SCOPED_TRACE(d);
            SCOPED_TRACE(inPlace);
            Harness harness{};
            auto& scene = harness.Scene();
            const auto entity = Make(scene, D(d));
            auto& props = PointDomainProperties(scene, entity, D(d));
            const auto size = props.Size();
            const bool half = D(d) == D::MeshHalfedge || D(d) == D::GraphHalfedge;
            if (half)
                scene.Raw().get<Extrinsic::ECS::Components::GeometrySources::Edges>(entity)
                    .Properties.GetOrAdd<bool>("e:deleted")[1] = true;
            else if (D(d) != D::PointCloudPoint)
                props.GetOrAdd<bool>(D(d) == D::MeshFace ? "f:deleted"
                                     : (D(d) == D::MeshEdge || D(d) == D::GraphEdge) ? "e:deleted"
                                                                                     : "v:deleted")[2] = true;
            const auto original = std::as_const(props).Get<glm::vec3>("samples").Vector();
            props.GetOrAdd<glm::vec3>("filtered").Vector().assign(size, glm::vec3(77));
            auto config = Settings(entity, D(d));
            if (inPlace) config.Output = config.Positions;

            const auto octree = harness.Wait(*BF::Start(config));
            ASSERT_EQ(octree.Status, "applied") << octree.Message;
            EXPECT_EQ(octree.CompletedIterations, 3u);
            const auto values = std::as_const(props).Get<glm::vec3>(config.Output.Name).Vector();
            ASSERT_EQ(values.size(), size);
            if (D(d) != D::PointCloudPoint) // a deleted row keeps what it had (row 2 is deleted in every domain)
                EXPECT_EQ(values[2], inPlace ? original[2] : glm::vec3(77));
            if (!inPlace) EXPECT_NE(values[0], glm::vec3(77)) << "a live row was written";
            props.Get<float>("keep")[0] = 99; // an unrelated edit does not make the entry stale
            ASSERT_TRUE(harness.History().Undo().Succeeded());
            EXPECT_EQ(std::as_const(props).Get<glm::vec3>(config.Output.Name)[0], inPlace ? original[0] : glm::vec3(77));
            ASSERT_TRUE(harness.History().Redo().Succeeded());
            EXPECT_EQ(std::as_const(props).Get<float>("keep")[0], 99);
            ASSERT_TRUE(harness.History().Undo().Succeeded());

            config.Backend = BF::FilterBackend::CpuLBVH;
            for (int frame = 0; frame < 20; ++frame) harness.Frame(); // the spatial cache builds its index
            const auto lbvh = harness.Wait(*BF::Start(config));
            ASSERT_EQ(lbvh.Status, "applied") << lbvh.Message;
            EXPECT_EQ(lbvh.ActualBackend, "cpu_lbvh");
            EXPECT_EQ(lbvh.WorkspaceBuilds, 2u) << "the cached index serves the first pass, rebuilt ones the others";
            const auto actual = std::as_const(props).Get<glm::vec3>(config.Output.Name).Vector();
            for (std::size_t i = 0; i < size; ++i)
                if (i != 4 || D(d) != D::PointCloudPoint) EXPECT_LE(glm::length(actual[i] - values[i]), 1e-5) << i;
            EXPECT_FLOAT_EQ(lbvh.AverageDisplacement, octree.AverageDisplacement);
            EXPECT_FLOAT_EQ(lbvh.MaxDisplacement, octree.MaxDisplacement);
            EXPECT_EQ(lbvh.PointsFiltered, octree.PointsFiltered);
            EXPECT_EQ(lbvh.DegenerateNormals, octree.DegenerateNormals);
            props.Get<glm::vec3>("directions")[0].x = 1; // an input changed: the entry is stale
            EXPECT_EQ(harness.History().Undo().Status, R::EditorCommandHistoryStatus::StaleEntity);
        }
}

TEST(BilateralFilterPlugin, AChangedInputOrOutputOrACancelEndsTheRunWithoutAWrite)
{
    // 0 unrelated edit (applies), 1 positions, 2 a deletion, 3 the output written, 4 cancel, 5 normals.
    const char* expected[]{"applied", "stale", "stale", "stale", "cancelled", "stale"};
    for (unsigned change = 0; change < 6; ++change)
    {
        SCOPED_TRACE(change);
        Harness harness{};
        auto& scene = harness.Scene();
        const auto entity = Make(scene, D::MeshVertex);
        auto& props = PointDomainProperties(scene, entity, D::MeshVertex);
        const auto run = BF::Start(Settings(entity, D::MeshVertex));
        ASSERT_FALSE(run->Final) << run->Final->Message;
        EXPECT_FALSE(props.Exists("filtered"));
        switch (change)
        {
        case 0: props.Get<float>("keep")[0] = 99; break;
        case 1: props.Get<glm::vec3>("samples")[0].x += 1; break;
        case 2: props.GetOrAdd<bool>("v:deleted")[0] = true; break;
        case 3: props.GetOrAdd<glm::vec3>("filtered")[0] = glm::vec3(77); break;
        case 4: EXPECT_TRUE(BF::Cancel(*run)); break;
        case 5: props.Get<glm::vec3>("directions")[0].x = 1; break;
        }
        const auto result = harness.Wait(*run);
        EXPECT_EQ(result.Status, expected[change]) << result.Message;
        EXPECT_EQ(harness.History().CanUndo(), change == 0);
        EXPECT_EQ(props.Exists("filtered"), change == 0 || change == 3);
        if (change == 3) EXPECT_EQ(std::as_const(props).Get<glm::vec3>("filtered")[0], glm::vec3(77));
    }
}

TEST(BilateralFilterPlugin, RunsOnDifferentOutputsAreConcurrentAndTheSameOutputIsRefused)
{
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = Make(scene, D::MeshVertex);
    auto& props = PointDomainProperties(scene, entity, D::MeshVertex);
    auto first = Settings(entity, D::MeshVertex), second = first;
    second.Output.Name = "filtered_too";
    const auto a = BF::Start(first), b = BF::Start(second), same = BF::Start(first);
    ASSERT_FALSE(a->Final);
    ASSERT_FALSE(b->Final) << b->Final->Message;
    ASSERT_TRUE(same->Final);
    EXPECT_EQ(same->Final->Status, "failed");
    EXPECT_NE(same->Final->Message.find("already running"), std::string::npos) << same->Final->Message;
    EXPECT_EQ(harness.Wait(*a).Status, "applied");
    EXPECT_EQ(harness.Wait(*b).Status, "applied");
    EXPECT_TRUE(props.Exists("filtered"));
    EXPECT_TRUE(props.Exists("filtered_too"));
}

TEST(BilateralFilterPlugin, SuccessiveInPlaceRunsUndoInTurn)
{
    // Both entries guard the positions through the history slot they share: undoing the second stamps it, so the
    // first still applies (the guard a GPU position accept shares, too).
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = Make(scene, D::MeshVertex);
    auto& props = PointDomainProperties(scene, entity, D::MeshVertex);
    const auto original = std::as_const(props).Get<glm::vec3>("samples").Vector();
    auto config = Settings(entity, D::MeshVertex);
    config.Output = config.Positions;
    config.Iterations = 1;
    ASSERT_EQ(harness.Wait(*BF::Start(config)).Status, "applied");
    const auto once = std::as_const(props).Get<glm::vec3>("samples").Vector();
    ASSERT_NE(once, original);
    ASSERT_EQ(harness.Wait(*BF::Start(config)).Status, "applied");
    ASSERT_TRUE(harness.History().Undo().Succeeded());
    EXPECT_EQ(std::as_const(props).Get<glm::vec3>("samples").Vector(), once);
    ASSERT_TRUE(harness.History().Undo().Succeeded()) << "the first run's entry is not stale";
    EXPECT_EQ(std::as_const(props).Get<glm::vec3>("samples").Vector(), original);
    ASSERT_TRUE(harness.History().Redo().Succeeded());
    ASSERT_TRUE(harness.History().Redo().Succeeded());
}

TEST(BilateralFilterPlugin, TheJobsWindowCancelsARun)
{
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = Make(scene, D::MeshVertex);
    auto& props = PointDomainProperties(scene, entity, D::MeshVertex);
    const auto run = BF::Start(Settings(entity, D::MeshVertex));
    ASSERT_FALSE(run->Final);
    const auto jobs = harness.Jobs();
    const auto row = std::ranges::find_if(jobs, [](const auto& job) { return job.Name == "Bilateral point filter"; });
    ASSERT_NE(row, jobs.end());
    // As the Jobs window's Cancel button does, on the shell's session.
    const auto commands = R::PrepareEditorProcessingCommands(harness.Shell);
    ASSERT_TRUE(R::ResolveEditorJobCancelReadiness(commands, *row).Enabled);
    EXPECT_EQ(R::CancelEditorJobRun(commands, *row), R::EditorJobCancelStatus::Requested);
    EXPECT_EQ(harness.Wait(*run).Status, "cancelled") << run->Final->Message;
    EXPECT_FALSE(props.Exists("filtered"));
    EXPECT_FALSE(harness.History().CanUndo());
}

TEST(BilateralFilterPlugin, ZeroPassesCopyThePositionsAndPreviewChecksTheOutput)
{
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = Make(scene, D::MeshVertex);
    auto& props = PointDomainProperties(scene, entity, D::MeshVertex);
    auto config = Settings(entity, D::MeshVertex);
    config.Iterations = 0;
    const auto copied = harness.Wait(*BF::Start(config));
    ASSERT_EQ(copied.Status, "applied") << copied.Message;
    EXPECT_EQ(copied.CompletedIterations, 0u);
    EXPECT_EQ(std::as_const(props).Get<glm::vec3>("filtered").Vector(),
              std::as_const(props).Get<glm::vec3>("samples").Vector());
    auto refused = config;
    refused.Output = refused.Normals;
    EXPECT_FALSE(BF::Preview(refused).empty());
    refused.Output = refused.Positions;
    refused.Output.Name = "v:deleted";
    EXPECT_NE(BF::Preview(refused).find("topology"), std::string::npos) << BF::Preview(refused);
    refused = config;
    refused.Backend = BF::FilterBackend::VulkanLBVH; // no GPU here: refused before anything runs
    EXPECT_NE(harness.Settled(refused).find("Vulkan"), std::string::npos) << harness.Settled(refused);
    refused.Iterations = 1;
    const auto run = BF::Start(refused);
    ASSERT_TRUE(run->Final);
    EXPECT_EQ(run->Final->Status, "invalid");
    EXPECT_EQ(harness.History().UndoCount(), 1u);
}

TEST(BilateralFilterPlugin, TheWindowRunsTheAppliedSettings)
{
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = Make(scene, D::MeshVertex);
    auto& props = PointDomainProperties(scene, entity, D::MeshVertex);
    ASSERT_TRUE(harness.Apply(Settings(entity, D::MeshVertex)));
    auto* ui = harness.Engine.Services().Find<R::EditorUiHost>();
    ASSERT_NE(ui, nullptr);
    ASSERT_TRUE(ui->SetWindowOpen("view.bilateral_filter", true));

    IMGUI_CHECKVERSION();
    ImGuiContext* context = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280, 900);
    io.IniFilename = nullptr;
    unsigned char* pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    auto second = Settings(entity, D::MeshVertex);
    second.Output.Name = "filtered_too";
    ASSERT_EQ(harness.Settled(second), ""); // input validation answered, as after a few frames in the Sandbox
    for (int frame = 0; frame < 2000 && !(props.Exists("filtered") && props.Exists("filtered_too")); ++frame)
    {
        io.DeltaTime = 1.f / 60.f;
        ImGui::NewFrame();
        // Run, apply settings for another output while the first run is still pending (no completion is applied
        // before frame 10) and run again: Run stays enabled.
        if (frame == 6) ASSERT_TRUE(harness.Apply(second)); // an activation lands the frame after it is requested
        if (frame == 2 || frame == 8)
            if (auto* window = ImGui::FindWindowByName("Bilateral Point Filter###view.bilateral_filter"))
                ImGui::ActivateItemByID(window->GetID("Run"));
        (void)ui->Windows().DrawOpenWindows();
        ImGui::Render();
        if (frame < 10) harness.Engine.Commands().Drain(harness.Scene());
        else harness.Frame();
    }
    ImGui::DestroyContext(context);
    EXPECT_TRUE(props.Exists("filtered")) << "the Run button started a run that published";
    EXPECT_TRUE(props.Exists("filtered_too")) << "a second Run with other applied settings";
    EXPECT_EQ(harness.History().UndoCount(), 2u);
}
