// ARCH-022: the point-cloud editing service plugins use (capture, CPU index, publication with undo), in an engine
// composed as the Sandbox does, its edits going through the app's workspace.
#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include "MockRHI.hpp"
#include "PointCloudEditingHarness.hpp"

import Extrinsic.Runtime.GpuPropertyBinding;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.WorldHandle;

namespace
{
    namespace R = Extrinsic::Runtime;
    using D = R::GeometryElementDomain;
    using Intrinsic::Tests::PointDomainProperties;
    using Harness = Intrinsic::Tests::PointCloudEditing::Harness;
    constexpr auto Make = Intrinsic::Tests::PointCloudEditing::MakeOrientedSamples;

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
            const auto edit = harness.Editing().Capture(Request(entity, D(d), inPlace), diagnostic);
            ASSERT_TRUE(edit) << diagnostic;
            const auto view = harness.Editing().View(*edit);
            EXPECT_EQ(view.SlotCount, props.Size());
            EXPECT_EQ(view.Points.size(), view.LiveCount);
            std::vector<glm::vec3> moved(view.Points.begin(), view.Points.end());
            for (auto& point : moved) point.z += 1.f;
            ASSERT_EQ(harness.Editing().Publish(*edit, moved, "Lift"), R::PointPublishStatus::Applied);
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

TEST(PointCloudEditing, AReleasedWorkspaceFallsBackToTheServicesOwn)
{
    // As the Sandbox shell does before it detaches: edits go through the service's own workspace again.
    Harness harness{};
    const auto entity = Make(harness.Scene(), D::MeshVertex);
    harness.Editing().ReleaseEditorWorkspace(harness.Shell);
    harness.Shell.Detach();
    std::string diagnostic;
    const auto edit = harness.Editing().Capture(Request(entity, D::MeshVertex), diagnostic);
    ASSERT_TRUE(edit) << diagnostic;
    const std::vector<glm::vec3> points(harness.Editing().View(*edit).Points.begin(),
                                        harness.Editing().View(*edit).Points.end());
    EXPECT_EQ(harness.Editing().Publish(*edit, points, "Copy"), R::PointPublishStatus::Applied);
    EXPECT_TRUE(harness.History().CanUndo());
}

TEST(PointCloudEditing, AChangedInputOrOutputMakesTheCaptureStaleAndNothingIsPublished)
{
    for (unsigned change = 0; change < 4; ++change)
    {
        SCOPED_TRACE(change);
        Harness harness{};
        auto& scene = harness.Scene();
        const auto entity = Make(scene, D::MeshVertex);
        auto& props = PointDomainProperties(scene, entity, D::MeshVertex);
        std::string diagnostic;
        const auto edit = harness.Editing().Capture(Request(entity, D::MeshVertex), diagnostic);
        ASSERT_TRUE(edit) << diagnostic;
        auto scalarRequest = Request(entity, D::MeshVertex);
        scalarRequest.ScalarOutputs = {{.Domain = D::MeshVertex, .Name = "s", .ValueKind = Geometry::PropertyValueKind::Float}};
        const auto scalars = harness.Editing().Capture(scalarRequest, diagnostic);
        ASSERT_TRUE(scalars) << diagnostic;
        std::vector<glm::vec3> points(harness.Editing().View(*edit).Points.begin(), harness.Editing().View(*edit).Points.end());
        switch (change)
        {
        case 0: props.Get<float>("keep")[0] = 99; break; // unrelated: still current
        case 1: props.Get<glm::vec3>("samples")[0].x += 1; break;
        case 2: props.GetOrAdd<glm::vec3>("filtered")[0] = glm::vec3(77); break;
        case 3: // another world becomes active; the captured one is untouched
        {
            auto& worlds = harness.Engine.Worlds();
            ASSERT_TRUE(worlds.RequestSetActiveWorld(worlds.CreateWorld("other")).has_value());
            (void)worlds.ApplyMaintenance(harness.Engine.Events(), harness.Engine.Jobs());
            ASSERT_NE(&harness.Scene(), &scene);
            break;
        }
        }
        EXPECT_EQ(harness.Editing().Current(*edit), change == 0);
        EXPECT_EQ(harness.Editing().Publish(*edit, points, "Copy"),
                  change == 0 ? R::PointPublishStatus::Applied : R::PointPublishStatus::Stale);
        EXPECT_EQ(harness.History().CanUndo(), change == 0);
        if (change == 3)
        {
            const std::vector<std::vector<float>> column{std::vector<float>(points.size(), 1.f)};
            EXPECT_EQ(harness.Editing().PublishScalars(*scalars, column, "Scalars"), R::PointPublishStatus::Stale);
            EXPECT_FALSE(PointDomainProperties(scene, entity, D::MeshVertex).Exists("s"));
        }
    }
}

TEST(PointCloudEditing, AReadinessCaptureCopiesNothingAndOutputsAreChecked)
{
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = Make(scene, D::MeshVertex);
    harness.Frame(60);
    std::string diagnostic;
    auto readiness = Request(entity, D::MeshVertex);
    readiness.CopyValues = false;
    const auto edit = harness.Settled([&] { return harness.Editing().Capture(readiness, diagnostic); });
    ASSERT_TRUE(edit) << diagnostic;
    EXPECT_TRUE(harness.Editing().View(*edit).Points.empty());
    EXPECT_GT(harness.Editing().View(*edit).LiveCount, 0u);
    auto normalsOut = Request(entity, D::MeshVertex);
    normalsOut.Output = normalsOut.Normals;
    EXPECT_FALSE(harness.Editing().Capture(normalsOut, diagnostic));
    // A point cloud without deletions: only the topology rule refuses its absent deletion flag as an output.
    const auto cloud = Make(scene, D::PointCloudPoint);
    auto topology = Request(cloud, D::PointCloudPoint);
    topology.Output.Name = "v:deleted";
    EXPECT_FALSE(harness.Editing().Capture(topology, diagnostic));
    EXPECT_NE(diagnostic.find("topology"), std::string::npos) << diagnostic;
    auto stale = Request(entity, D::MeshVertex);
    stale.StableEntityId = 999999u;
    EXPECT_FALSE(harness.Editing().Capture(stale, diagnostic));
    EXPECT_FALSE(diagnostic.empty());
}

TEST(PointCloudEditing, TheCachedCpuIndexAnswersCompactNeighbours)
{
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = Make(scene, D::PointCloudPoint);
    harness.Frame(60);
    auto request = Request(entity, D::PointCloudPoint);
    request.AcquireIndex = true;
    std::string diagnostic;
    const auto edit = harness.Editing().Capture(request, diagnostic);
    ASSERT_TRUE(edit) << diagnostic;
    const auto* index = harness.Editing().CpuIndex(*edit);
    ASSERT_NE(index, nullptr);
    const auto view = harness.Editing().View(*edit);
    const auto nearest = index->KNearest(view.Points[0], 1u);
    ASSERT_EQ(nearest.size(), 1u);
    EXPECT_EQ(nearest[0].Index, 0u); // compact: the first live point is its own nearest neighbour
    const auto catalog = harness.Settled([&]
    {
        auto entries = harness.Editing().InputCatalog(request.StableEntityId, 2u);
        return entries.Empty() ? std::optional<R::GeometryPropertyCatalogSnapshot>{} : std::optional{std::move(entries)};
    }).value_or(R::GeometryPropertyCatalogSnapshot{});
    std::string names;
    for (const auto& entry : catalog.Entries) names += entry.Ref.Name + " ";
    EXPECT_TRUE(std::ranges::any_of(catalog.Entries, [&](const auto& entry) { return entry.Ref.Name == "directions"; }))
        << names;
}

TEST(PointCloudEditing, ScalarColumnsPublishOnlyWhenEveryOutputGetsOneValuePerPoint)
{
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = Make(scene, D::MeshVertex);
    auto& props = PointDomainProperties(scene, entity, D::MeshVertex);
    auto request = Request(entity, D::MeshVertex);
    request.ScalarOutputs = {{.Domain = D::MeshVertex, .Name = "a", .ValueKind = Geometry::PropertyValueKind::Float},
                             {.Domain = D::MeshVertex, .Name = "b", .ValueKind = Geometry::PropertyValueKind::Float}};
    std::string diagnostic;
    const auto edit = harness.Editing().Capture(request, diagnostic);
    ASSERT_TRUE(edit) << diagnostic;
    const auto live = harness.Editing().View(*edit).LiveCount;
    const std::vector<std::vector<float>> one{std::vector<float>(live, 1.f)};
    EXPECT_EQ(harness.Editing().PublishScalars(*edit, one, "Scalars"), R::PointPublishStatus::Rejected) << "one of two";
    const std::vector<std::vector<float>> shortColumn{std::vector<float>(live, 1.f), std::vector<float>(live - 1, 2.f)};
    EXPECT_EQ(harness.Editing().PublishScalars(*edit, shortColumn, "Scalars"), R::PointPublishStatus::Rejected);
    EXPECT_FALSE(props.Exists("a"));
    EXPECT_FALSE(props.Exists("b"));
    EXPECT_FALSE(harness.History().CanUndo());
    const std::vector<std::vector<float>> both{std::vector<float>(live, 1.f), std::vector<float>(live, 2.f)};
    ASSERT_EQ(harness.Editing().PublishScalars(*edit, both, "Scalars"), R::PointPublishStatus::Applied);
    EXPECT_EQ(std::as_const(props).Get<float>("b")[0], 2.f);
    EXPECT_EQ(harness.History().UndoCount(), 1u);
}

TEST(PointCloudEditing, AnInputCatalogOfOneDomainListsOnlyThatDomain)
{
    // A normals picker offers the positions' domain only: a mesh has vertex and face vectors.
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = Make(scene, D::MeshVertex);
    PointDomainProperties(scene, entity, D::MeshFace).GetOrAdd<glm::vec3>("face_directions").Vector().assign(
        PointDomainProperties(scene, entity, D::MeshFace).Size(), glm::vec3(0, 0, 1));
    const auto stable = R::SelectionController::ToStableEntityId(entity);
    R::GeometryPropertyCatalogSnapshot all{};
    (void)harness.Settled([&] // the catalog comes from the workspace snapshot, which settles over frames
    {
        all = harness.Editing().InputCatalog(stable, 1u);
        return std::ranges::any_of(all.Entries, [](const auto& e) { return e.Ref.Domain == D::MeshFace; });
    });
    EXPECT_TRUE(std::ranges::any_of(all.Entries, [](const auto& e) { return e.Ref.Domain == D::MeshFace; }));
    EXPECT_TRUE(std::ranges::any_of(all.Entries, [](const auto& e) { return e.Ref.Domain == D::MeshVertex; }));
    const auto faces = harness.Editing().InputCatalog(stable, 1u, D::MeshFace);
    ASSERT_FALSE(faces.Entries.empty());
    EXPECT_TRUE(std::ranges::all_of(faces.Entries, [](const auto& e) { return e.Ref.Domain == D::MeshFace; }));
}

// ARCH-022 S3b: a CPU-made front (a step run's position preview). With its upload pending, a slot is neither handed
// out again nor freed by a Discard.
TEST(PointCloudEditing, APreviewFrontWithAPendingUploadIsNeitherReusedNorFreed)
{
    Extrinsic::Tests::MockDevice device;
    device.TransferQueue.AcceptBufferUploads = true;
    device.TransferQueue.AlwaysComplete = false;
    const auto advance = [&] { device.GlobalFrameNumber += device.FramesInFlight + 1u; }; // frame uses complete
    Extrinsic::Graphics::GpuPropertyResidency residency(device);
    const R::GeometryPropertyRef positions{.Domain = D::PointCloudPoint, .Name = "p", .ValueKind = Geometry::PropertyValueKind::Vec3};
    const auto key = R::MakeGpuPropertyKey(R::DefaultWorldHandle, entt::entity{7}, positions);
    const auto layout = R::MakeGpuPropertyLayout(Geometry::PropertyValueKind::Vec3, 4u);
    ASSERT_TRUE(layout);
    const std::vector<std::byte> bytes(layout->Bytes(), std::byte{1});
    const auto upload = [&] { return R::UploadGpuPropertyFront(residency, device, key, *layout, 2u, bytes); };

    ASSERT_TRUE(upload()) << "A";
    ASSERT_TRUE(upload()) << "B, while A's upload is pending";
    EXPECT_EQ(device.TransferQueue.BufferUploads.size(), 2u);
    advance();
    EXPECT_FALSE(upload()) << "A is no longer the front, but its upload is pending";
    device.TransferQueue.AlwaysComplete = true;
    ASSERT_TRUE(upload()) << "A again once its upload completed";

    // Another run's ring, discarded while its upload is pending.
    const auto other = R::MakeGpuPropertyKey(R::DefaultWorldHandle, entt::entity{8}, positions);
    device.TransferQueue.AlwaysComplete = false;
    ASSERT_TRUE(R::UploadGpuPropertyFront(residency, device, other, *layout, 2u, bytes));
    const int destroyed = device.DestroyBufferCount;
    residency.Discard(other, residency.RingGeneration(other));
    advance();
    residency.Tick();
    EXPECT_EQ(device.DestroyBufferCount, destroyed) << "a discarded ring keeps a slot whose upload is pending";
    device.TransferQueue.AlwaysComplete = true;
    residency.Tick();
    EXPECT_EQ(device.DestroyBufferCount, destroyed + 1) << "freed once complete";
}
