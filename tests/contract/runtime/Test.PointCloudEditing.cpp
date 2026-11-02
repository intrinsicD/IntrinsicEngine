// ARCH-022: the point-cloud editing service plugins use (capture, CPU index, publication with undo), in an engine
// composed as the Sandbox does, its edits going through the app's workspace.
#include <algorithm>
#include <chrono>
#include <thread>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include "PointDomainFixture.hpp"
#include "RuntimeTestModule.hpp"

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.Window;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.AsyncWorkModule;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EditorUiModule;
import Extrinsic.Runtime.EditorWorkspaceAttachment;
import Extrinsic.Runtime.EditorWorkspaceSnapshots;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.PointCloudEditing;
import Extrinsic.Runtime.SceneDocumentModule;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.SpatialIndexCache;

namespace
{
    namespace Config = Extrinsic::Core::Config;
    namespace R = Extrinsic::Runtime;
    using D = R::GeometryElementDomain;
    using Intrinsic::Tests::PointDomainProperties;

    Config::EngineConfig HeadlessConfig()
    {
        Config::EngineConfig config{};
        config.Simulation.WorkerThreadCount = 1u;
        config.ReferenceScene.Enabled = false;
        config.Camera.Enabled = false;
        config.Window.Backend = Config::WindowBackend::Null;
        return config;
    }

    struct Harness
    {
        Intrinsic::Tests::RuntimeTestKernel Engine;
        R::EditorWorkspaceAttachment Shell{};
        R::PointCloudEditingService* Service{nullptr};

        Harness() : Engine(HeadlessConfig(), std::make_unique<Intrinsic::Tests::OneFrameApplication>())
        {
            Engine.EmplaceModule<R::AsyncWorkModule>();
            Engine.EmplaceModule<R::EngineConfigControl>();
            Engine.EmplaceModule<R::EditorUiModule>();
            Engine.EmplaceModule<R::SceneDocumentModule>();
            Engine.EmplaceModule<R::SpatialIndexCache>();
            Engine.EmplaceModule<R::PointCloudEditingModule>();
            Engine.Initialize();
            Service = Engine.Services().Find<R::PointCloudEditingService>();
            Shell.Attach(Engine.Worlds(), Engine.Services());
            (void)R::PrepareEditorWorkspaceSnapshotFrame(Shell); // the shell prepares one every frame
            Service->UseEditorWorkspace(Shell);
        }
        ~Harness() { Engine.Shutdown(); }

        Extrinsic::ECS::Scene::Registry& Scene() { return *Engine.Worlds().Get(Engine.ActiveWorld()); }
        // Asks again each frame until the asynchronous validation answers, as a window does.
        template <class Ask>
        auto Settled(Ask ask)
        {
            auto answer = ask();
            for (int attempt = 0; !answer && attempt < 20; ++attempt)
            {
                // The queued check runs on the command bus; a new workspace frame in between would supersede it.
                Engine.Commands().Drain(Scene());
                (void)Engine.Jobs().DrainCompletions(Engine.Events());
                answer = ask();
            }
            return answer;
        }
        // Frames as the Sandbox runs them: the shell prepares its workspace and job completions are applied, so the
        // asynchronous input validation behind readiness and catalogs settles.
        void Frame(const int frames = 60)
        {
            for (int frame = 0; frame < frames; ++frame)
            {
                (void)R::PrepareEditorWorkspaceSnapshotFrame(Shell);
                Engine.Commands().Drain(Scene()); // the input validation runs as commands
                (void)Engine.Jobs().DrainCompletions(Engine.Events());
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        }
        R::EditorCommandHistory& History() { return *Engine.Services().Find<R::EditorCommandHistory>(); }
    };

    // Samples on a 3-wide grid with fixed normals and an unrelated float property; row 2 deleted.
    entt::entity Make(Extrinsic::ECS::Scene::Registry& scene, const D domain)
    {
        const auto entity = Intrinsic::Tests::MakePointDomainSource(scene, domain);
        auto& props = PointDomainProperties(scene, entity, domain);
        auto samples = props.GetOrAdd<glm::vec3>("samples");
        for (std::size_t i = 0; i < samples.Size(); ++i)
            samples[i] = {float(i % 3), float(i / 3), .15f * float(int(i % 3) - 1)};
        props.GetOrAdd<glm::vec3>("directions").Vector().assign(props.Size(), glm::vec3(0, 0, 1));
        std::ranges::fill(props.GetOrAdd<float>("keep").Vector(), 42.f);
        return entity;
    }

    R::PointEditRequest Request(const entt::entity entity, const D domain, const bool inPlace = false)
    {
        R::PointEditRequest request{
            .StableEntityId = R::SelectionController::ToStableEntityId(entity),
            .Positions = {.Domain = domain, .Name = "samples", .ValueKind = Geometry::PropertyValueKind::Vec3},
            .Normals = {.Domain = domain, .Name = "directions", .ValueKind = Geometry::PropertyValueKind::Vec3},
            .Output = {.Domain = domain, .Name = "filtered", .ValueKind = Geometry::PropertyValueKind::Vec3}};
        if (inPlace) request.Output = request.Positions;
        return request;
    }
}

TEST(PointCloudEditing, EveryDomainPublishesLiveRowsWithUndoCopyAndInPlace)
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
            props.GetOrAdd<glm::vec3>("filtered").Vector().assign(props.Size(), glm::vec3(77));
            const auto original = std::as_const(props).Get<glm::vec3>("samples").Vector();
            std::string diagnostic;
            const auto edit = harness.Service->Capture(Request(entity, D(d), inPlace), diagnostic);
            ASSERT_TRUE(edit) << diagnostic;
            const auto view = harness.Service->View(*edit);
            EXPECT_EQ(view.SlotCount, props.Size());
            EXPECT_EQ(view.Points.size(), view.LiveCount);
            std::vector<glm::vec3> moved(view.Points.begin(), view.Points.end());
            for (auto& point : moved) point.z += 1.f;
            ASSERT_EQ(harness.Service->Publish(*edit, moved, "Lift"), R::PointPublishStatus::Applied);
            const auto written = std::as_const(props).Get<glm::vec3>(Request(entity, D(d), inPlace).Output.Name).Vector();
            EXPECT_EQ(written[0], original[0] + glm::vec3(0, 0, 1));
            props.Get<float>("keep")[0] = 99; // an unrelated edit does not make the entry stale
            ASSERT_TRUE(harness.History().Undo().Succeeded());
            EXPECT_EQ(std::as_const(props).Get<glm::vec3>(Request(entity, D(d), inPlace).Output.Name)[0],
                      inPlace ? original[0] : glm::vec3(77));
            ASSERT_TRUE(harness.History().Redo().Succeeded());
            EXPECT_EQ(std::as_const(props).Get<float>("keep")[0], 99);
            props.Get<glm::vec3>("directions")[0].x = 1; // an input changed: the entry is stale
            EXPECT_EQ(harness.History().Undo().Status, R::EditorCommandHistoryStatus::StaleEntity);
        }
}

TEST(PointCloudEditing, AChangedInputOrOutputMakesTheCaptureStaleAndNothingIsPublished)
{
    for (unsigned change = 0; change < 3; ++change)
    {
        SCOPED_TRACE(change);
        Harness harness{};
        auto& scene = harness.Scene();
        const auto entity = Make(scene, D::MeshVertex);
        auto& props = PointDomainProperties(scene, entity, D::MeshVertex);
        std::string diagnostic;
        const auto edit = harness.Service->Capture(Request(entity, D::MeshVertex), diagnostic);
        ASSERT_TRUE(edit) << diagnostic;
        std::vector<glm::vec3> points(harness.Service->View(*edit).Points.begin(), harness.Service->View(*edit).Points.end());
        switch (change)
        {
        case 0: props.Get<float>("keep")[0] = 99; break; // unrelated: still current
        case 1: props.Get<glm::vec3>("samples")[0].x += 1; break;
        case 2: props.GetOrAdd<glm::vec3>("filtered")[0] = glm::vec3(77); break;
        }
        EXPECT_EQ(harness.Service->Current(*edit), change == 0);
        EXPECT_EQ(harness.Service->Publish(*edit, points, "Copy"),
                  change == 0 ? R::PointPublishStatus::Applied : R::PointPublishStatus::Stale);
        EXPECT_EQ(harness.History().CanUndo(), change == 0);
    }
}

TEST(PointCloudEditing, AReadinessCaptureCopiesNothingAndOutputsAreChecked)
{
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = Make(scene, D::MeshVertex);
    harness.Frame();
    std::string diagnostic;
    auto readiness = Request(entity, D::MeshVertex);
    readiness.CopyValues = false;
    const auto edit = harness.Settled([&] { return harness.Service->Capture(readiness, diagnostic); });
    ASSERT_TRUE(edit) << diagnostic;
    EXPECT_TRUE(harness.Service->View(*edit).Points.empty());
    EXPECT_GT(harness.Service->View(*edit).LiveCount, 0u);
    auto normalsOut = Request(entity, D::MeshVertex);
    normalsOut.Output = normalsOut.Normals;
    EXPECT_FALSE(harness.Service->Capture(normalsOut, diagnostic));
    // A point cloud without deletions: only the topology rule refuses its absent deletion flag as an output.
    const auto cloud = Make(scene, D::PointCloudPoint);
    auto topology = Request(cloud, D::PointCloudPoint);
    topology.Output.Name = "v:deleted";
    EXPECT_FALSE(harness.Service->Capture(topology, diagnostic));
    EXPECT_NE(diagnostic.find("topology"), std::string::npos) << diagnostic;
    auto stale = Request(entity, D::MeshVertex);
    stale.StableEntityId = 999999u;
    EXPECT_FALSE(harness.Service->Capture(stale, diagnostic));
    EXPECT_FALSE(diagnostic.empty());
}

TEST(PointCloudEditing, TheCachedCpuIndexAnswersCompactNeighbours)
{
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = Make(scene, D::PointCloudPoint);
    harness.Frame();
    auto request = Request(entity, D::PointCloudPoint);
    request.AcquireIndex = true;
    std::string diagnostic;
    const auto edit = harness.Service->Capture(request, diagnostic);
    ASSERT_TRUE(edit) << diagnostic;
    const auto* index = harness.Service->CpuIndex(*edit);
    ASSERT_NE(index, nullptr);
    const auto view = harness.Service->View(*edit);
    const auto nearest = index->KNearest(view.Points[0], 1u);
    ASSERT_EQ(nearest.size(), 1u);
    EXPECT_EQ(nearest[0].Index, 0u); // compact: the first live point is its own nearest neighbour
    const auto catalog = harness.Settled([&]
    {
        auto entries = harness.Service->InputCatalog(request.StableEntityId, 2u);
        return entries.Empty() ? std::optional<R::GeometryPropertyCatalogSnapshot>{} : std::optional{std::move(entries)};
    }).value_or(R::GeometryPropertyCatalogSnapshot{});
    std::string names;
    for (const auto& entry : catalog.Entries) names += entry.Ref.Name + " ";
    EXPECT_TRUE(std::ranges::any_of(catalog.Entries, [&](const auto& entry) { return entry.Ref.Name == "directions"; }))
        << names;
}
