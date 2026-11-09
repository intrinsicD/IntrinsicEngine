// ARCH-022: the point-cloud editing service plugins use (capture, CPU index, publication with undo), in an engine
// composed as the Sandbox does, its edits going through the app's workspace.
#include <algorithm>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include "MockRHI.hpp"
#include "PointCloudEditingHarness.hpp"

import Extrinsic.ECS.Component.Hierarchy;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.Graphics.GpuPropertyResidency;
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

    // Point spacing's request (method 1) into a float `spacing` beside the samples.
    R::ScalarAnalysisRequest Analysis(const entt::entity entity, const D domain, std::string output = "spacing")
    {
        return {.StableEntityId = R::SelectionController::ToStableEntityId(entity),
                .Positions = {.Domain = domain, .Name = "samples", .ValueKind = Geometry::PropertyValueKind::Vec3},
                .Output = {.Domain = domain, .Name = std::move(output), .ValueKind = Geometry::PropertyValueKind::Float},
                .Params = {.Method = 1, .K = 2, .Scale = 1.f},
                .Noun = "Radii",
                .Method = "Point spacing",
                .History = "Estimate point spacing and radii"};
    }

    // The readiness once the asynchronous input validation answered.
    std::string Ready(Harness& harness, const R::ScalarAnalysisRequest& request, const R::ScalarAnalysisBackend backend)
    {
        std::string reason;
        (void)harness.Settled(
            [&]
            {
                reason = harness.Editing().ScalarAnalysisReadiness(request, backend);
                return reason.find("Wait for input validation") == std::string::npos;
            });
        return reason;
    }

    // A test residency (no device work): runs over given values, captured as a CPU run is.
    struct ScalarRuns
    {
        Extrinsic::Tests::MockDevice Device{};
        Extrinsic::Graphics::GpuPropertyResidency Residency{Device};
        ScalarRuns() { Device.TransferQueue.AcceptBufferUploads = true; }
        R::ScalarAnalysis Start(Harness& harness, const entt::entity entity, const D domain, const float value,
                                R::ScalarAnalysisRefusal& refusal, std::string output = "spacing")
        {
            std::vector<float> values(PointDomainProperties(harness.Scene(), entity, domain).Size(), value);
            return harness.Editing().StartScalarAnalysisForTest(Analysis(entity, domain, std::move(output)),
                                                                std::move(values), Residency, refusal);
        }
        bool HasRing(Harness& harness, const entt::entity entity, const D domain, std::string output = "spacing")
        {
            return Residency.HasRing(R::MakeGpuPropertyKey(harness.Engine.ActiveWorld(), entity,
                                                           Analysis(entity, domain, std::move(output)).Output));
        }
    };
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

// Undo and redo of a scalar publication refuse an output changed through any mutable path that keeps the property
// contract (Geometry.Properties.cppm: a borrow marks the property, a retained borrow is marked by MarkModified); this is
// what lets the apply check revisions instead of recapturing the columns (ARCH-022 Q90).
TEST(PointCloudEditing, AScalarPublicationRefusesAnOutputChangedThroughAnyMutablePath)
{
    // 0 Vector(), 1 operator[], 2 a vector borrow retained from before the capture and marked after the write, 3 removed
    // and added again with the same values, 4 Span(), 5 Data(), 6 a span retained from before the capture and marked
    // after the write.
    for (unsigned path = 0; path < 7; ++path)
        for (const bool redo : {false, true})
        {
            SCOPED_TRACE(path);
            SCOPED_TRACE(redo);
            Harness harness{};
            auto& scene = harness.Scene();
            const auto entity = Make(scene, D::MeshVertex);
            auto& props = PointDomainProperties(scene, entity, D::MeshVertex);
            props.GetOrAdd<float>("a", 5.f);
            auto request = Request(entity, D::MeshVertex);
            request.ScalarOutputs = {{.Domain = D::MeshVertex, .Name = "a", .ValueKind = Geometry::PropertyValueKind::Float}};
            auto retained = props.Get<float>("a");
            auto& borrowed = retained.Vector(); // the borrows mark now, before the capture
            const auto retainedSpan = retained.Span();
            std::string diagnostic;
            const auto edit = harness.Editing().Capture(request, diagnostic);
            ASSERT_TRUE(edit) << diagnostic;
            const std::vector<std::vector<float>> column{std::vector<float>(harness.Editing().View(*edit).LiveCount, 1.f)};
            ASSERT_EQ(harness.Editing().PublishScalars(*edit, column, "Scalars"), R::PointPublishStatus::Applied);
            if (redo) ASSERT_TRUE(harness.History().Undo().Succeeded());
            const float value = std::as_const(props).Get<float>("a")[1];
            switch (path)
            {
            case 0: props.Get<float>("a").Vector()[1] = value + 1.f; break;
            case 1: props.Get<float>("a")[1] = value + 1.f; break;
            case 2:
                borrowed[1] = value + 1.f;
                retained.MarkModified();
                break;
            case 3:
            {
                const auto values = std::as_const(props).Get<float>("a").Vector();
                auto removed = props.Get<float>("a");
                props.Remove(removed);
                props.GetOrAdd<float>("a").Vector() = values;
                break;
            }
            case 4: props.Get<float>("a").Span()[1] = value + 1.f; break;
            case 5: props.Get<float>("a").Data()[1] = value + 1.f; break;
            case 6:
                retainedSpan[1] = value + 1.f;
                retained.MarkModified();
                break;
            }
            const auto status = redo ? harness.History().Redo().Status : harness.History().Undo().Status;
            EXPECT_EQ(status, R::EditorCommandHistoryStatus::StaleEntity);
            EXPECT_EQ(std::as_const(props).Get<float>("a")[1], path == 3 ? value : value + 1.f) << "nothing was written";
        }
}

// The apply compares revisions, not values (no recapture of the outputs): a write that bypasses the property contract
// (a retained span written without MarkModified) is not seen, and the undo restores over it (ARCH-022 Q90).
TEST(PointCloudEditing, AScalarPublicationChecksOutputRevisionsNotValues)
{
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = Make(scene, D::MeshVertex);
    auto& props = PointDomainProperties(scene, entity, D::MeshVertex);
    props.GetOrAdd<float>("a", 5.f);
    auto request = Request(entity, D::MeshVertex);
    request.ScalarOutputs = {{.Domain = D::MeshVertex, .Name = "a", .ValueKind = Geometry::PropertyValueKind::Float}};
    const auto unmarked = props.Get<float>("a").Span(); // marks now, before the capture
    std::string diagnostic;
    const auto edit = harness.Editing().Capture(request, diagnostic);
    ASSERT_TRUE(edit) << diagnostic;
    const std::vector<std::vector<float>> column{std::vector<float>(harness.Editing().View(*edit).LiveCount, 1.f)};
    ASSERT_EQ(harness.Editing().PublishScalars(*edit, column, "Scalars"), R::PointPublishStatus::Applied);
    unmarked[1] = 99.f; // outside the contract: no revision changes
    EXPECT_EQ(harness.History().Undo().Status, R::EditorCommandHistoryStatus::Undone);
    EXPECT_EQ(std::as_const(props).Get<float>("a")[1], 5.f) << "the undo restored the value before the publication";
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

// ARCH-022 block A: an empty normal name takes the positions alone, with and without copying values; a later normal
// edit does not stale such a capture, while a given normal input stays watched and an invalid one still fails.
TEST(PointCloudEditing, AnEmptyNormalNameTakesThePositionsAlone)
{
    Harness harness{};
    auto& scene = harness.Scene();
    const auto oriented = Make(scene, D::PointCloudPoint);
    const auto bare = Intrinsic::Tests::MakePointDomainSource(scene, D::PointCloudPoint);
    PointDomainProperties(scene, bare, D::PointCloudPoint).GetOrAdd<glm::vec3>("samples", glm::vec3{1.f});
    harness.Frame(60);
    std::string diagnostic;
    for (const auto entity : {oriented, bare})
    {
        auto request = Request(entity, D::PointCloudPoint, true);
        request.Normals = {};
        const auto edit = harness.Editing().Capture(request, diagnostic);
        ASSERT_TRUE(edit) << diagnostic;
        EXPECT_TRUE(harness.Editing().View(*edit).PointNormals.empty());
        EXPECT_FALSE(harness.Editing().View(*edit).Points.empty());
        request.CopyValues = false; // readiness: the asynchronous input check
        const auto ready = harness.Settled([&] { return harness.Editing().Capture(request, diagnostic); });
        ASSERT_TRUE(ready) << diagnostic;
        EXPECT_GT(harness.Editing().View(*ready).LiveCount, 0u);
    }
    auto positionsOnly = Request(oriented, D::PointCloudPoint, true);
    positionsOnly.Normals = {};
    const auto withoutNormals = harness.Editing().Capture(positionsOnly, diagnostic);
    const auto withNormals = harness.Editing().Capture(Request(oriented, D::PointCloudPoint, true), diagnostic);
    ASSERT_TRUE(withoutNormals && withNormals) << diagnostic;
    PointDomainProperties(scene, oriented, D::PointCloudPoint).Get<glm::vec3>("directions")[0] = glm::vec3(1, 0, 0);
    EXPECT_TRUE(harness.Editing().Current(*withoutNormals)) << "no normal input, so a normal edit changes nothing";
    EXPECT_FALSE(harness.Editing().Current(*withNormals)) << "a given normal input stays watched";
    auto missing = Request(oriented, D::PointCloudPoint);
    missing.Normals.Name = "no_such_normals";
    EXPECT_FALSE(harness.Editing().Capture(missing, diagnostic)) << "a given but invalid normal input still fails";
}

// ARCH-022 block A: CaptureTransform takes the local Transform's matrix of an unparented entity; the capture is stale,
// and a direct publication writes nothing, once the entity moves, gets a parent, or its Transform appears or goes.
TEST(PointCloudEditing, ACapturedTransformMustStayAsCapturedUntilPublication)
{
    namespace ECSC = Extrinsic::ECS::Components;
    for (int change = 0; change < 5; ++change)
    {
        SCOPED_TRACE(change);
        Harness harness{};
        auto& scene = harness.Scene();
        auto& raw = scene.Raw();
        const auto entity = Make(scene, D::PointCloudPoint);
        if (change != 4)
        {
            auto& transform = raw.emplace<ECSC::Transform::Component>(entity);
            transform.Position = {1.f, 2.f, 3.f};
            transform.Scale = {2.f, 1.f, 0.5f};
        }
        auto request = Request(entity, D::PointCloudPoint, true);
        request.Normals = {};
        request.CaptureTransform = true;
        std::string diagnostic;
        const auto edit = harness.Editing().Capture(request, diagnostic);
        ASSERT_TRUE(edit) << diagnostic;
        auto scalarRequest = request;
        scalarRequest.ScalarOutputs = {{.Domain = D::PointCloudPoint, .Name = "s", .ValueKind = Geometry::PropertyValueKind::Float}};
        const auto scalars = harness.Editing().Capture(scalarRequest, diagnostic);
        ASSERT_TRUE(scalars) << diagnostic;
        const auto expected = change != 4 ? ECSC::Transform::GetMatrixDouble(raw.get<ECSC::Transform::Component>(entity))
                                          : glm::dmat4(1.0);
        EXPECT_EQ(harness.Editing().View(*edit).Model, expected);
        EXPECT_TRUE(harness.Editing().Current(*edit));
        const auto before = std::as_const(PointDomainProperties(scene, entity, D::PointCloudPoint)).Get<glm::vec3>("samples").Vector();
        switch (change)
        {
        case 0: raw.get<ECSC::Transform::Component>(entity).Position.x += 1.f; break; // moved
        case 1: // given a parent; its local Transform is unchanged
        {
            const auto parent = scene.Create();
            raw.emplace<ECSC::Hierarchy::Component>(entity).Parent = parent;
            break;
        }
        case 2: raw.remove<ECSC::Transform::Component>(entity); break;
        case 3: raw.get<ECSC::Transform::Component>(entity).Scale.y = 3.f; break;
        case 4: raw.emplace<ECSC::Transform::Component>(entity); break; // a Transform appears
        }
        EXPECT_FALSE(harness.Editing().Current(*edit));
        std::vector<glm::vec3> moved(harness.Editing().View(*edit).Points.begin(), harness.Editing().View(*edit).Points.end());
        for (auto& p : moved) p += glm::vec3(1.f);
        EXPECT_EQ(harness.Editing().Publish(*edit, moved, "Moved"), R::PointPublishStatus::Stale);
        const std::vector<std::vector<float>> column{std::vector<float>(moved.size(), 1.f)};
        EXPECT_EQ(harness.Editing().PublishScalars(*scalars, column, "Scalars"), R::PointPublishStatus::Stale);
        EXPECT_FALSE(PointDomainProperties(scene, entity, D::PointCloudPoint).Exists("s"));
        EXPECT_EQ(std::as_const(PointDomainProperties(scene, entity, D::PointCloudPoint)).Get<glm::vec3>("samples").Vector(), before);
        EXPECT_FALSE(harness.History().CanUndo());
    }
    // A parented entity is refused at capture.
    Harness harness{};
    auto& scene = harness.Scene();
    const auto child = Make(scene, D::PointCloudPoint);
    scene.Raw().emplace<Extrinsic::ECS::Components::Hierarchy::Component>(child).Parent = scene.Create();
    auto request = Request(child, D::PointCloudPoint, true);
    request.CaptureTransform = true;
    std::string diagnostic;
    EXPECT_FALSE(harness.Editing().Capture(request, diagnostic));
    EXPECT_NE(diagnostic.find("unparented"), std::string::npos) << diagnostic;
}

TEST(PointCloudEditing, ANonFinitePositionIsRefusedAndNothingIsWritten)
{
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = Make(scene, D::PointCloudPoint);
    std::string diagnostic;
    const auto edit = harness.Editing().Capture(Request(entity, D::PointCloudPoint, true), diagnostic);
    ASSERT_TRUE(edit) << diagnostic;
    std::vector<glm::vec3> live(harness.Editing().View(*edit).Points.begin(), harness.Editing().View(*edit).Points.end());
    const auto before = live;
    live.back().y = std::numeric_limits<float>::infinity();
    EXPECT_EQ(harness.Editing().Publish(*edit, live, "Infinite"), R::PointPublishStatus::Rejected);
    EXPECT_EQ(std::as_const(PointDomainProperties(scene, entity, D::PointCloudPoint)).Get<glm::vec3>("samples").Vector()[0], before[0]);
    EXPECT_FALSE(harness.History().CanUndo());
}

// Codex block A finding 1: a publication checks the world before it reads the captured scene, so a capture whose world
// was destroyed is stale (and reads nothing freed).
TEST(PointCloudEditing, ACaptureOfADestroyedWorldIsStaleWithoutReadingIt)
{
    Harness harness{};
    auto& scene = harness.Scene();
    const auto entity = Make(scene, D::PointCloudPoint);
    auto request = Request(entity, D::PointCloudPoint, true);
    request.Normals = {};
    request.CaptureTransform = true;
    request.ScalarOutputs = {{.Domain = D::PointCloudPoint, .Name = "s", .ValueKind = Geometry::PropertyValueKind::Float}};
    std::string diagnostic;
    const auto scalars = harness.Editing().Capture(request, diagnostic);
    ASSERT_TRUE(scalars) << diagnostic;
    auto& worlds = harness.Engine.Worlds();
    const auto captured = harness.Engine.ActiveWorld();
    ASSERT_TRUE(worlds.RequestSetActiveWorld(worlds.CreateWorld("other")).has_value());
    (void)worlds.ApplyMaintenance(harness.Engine.Events(), harness.Engine.Jobs());
    ASSERT_TRUE(worlds.RequestDestroyWorld(captured).has_value());
    for (int i = 0; i < 3; ++i) (void)worlds.ApplyMaintenance(harness.Engine.Events(), harness.Engine.Jobs());
    ASSERT_FALSE(worlds.Contains(captured)) << "the captured world is gone";
    const std::vector<std::vector<float>> column{std::vector<float>(harness.Editing().View(*scalars).LiveCount, 1.f)};
    EXPECT_EQ(harness.Editing().PublishScalars(*scalars, column, "Scalars"), R::PointPublishStatus::Stale);
    EXPECT_FALSE(harness.Editing().Current(*scalars));
}

// ARCH-022 Q89: a point scalar method's readiness and GPU start run the capture stage and the GPU admission of the
// operation runtime, with its texts: the CPU takes any scalar output and any k; the GPU refuses a non-float output,
// k > 63 and a subnormal parameter, and without a float64 device; readiness and a refused start leave nothing behind.
TEST(PointCloudEditing, AScalarAnalysisIsAdmittedAsTheOperationRuntimeAdmitsIt)
{
    using B = R::ScalarAnalysisBackend;
    Harness harness{};
    const auto entity = Make(harness.Scene(), D::PointCloudPoint);
    harness.Frame(60);
    const auto refused = [&](const R::ScalarAnalysisRequest& request)
    {
        R::ScalarAnalysisRefusal refusal;
        EXPECT_FALSE(harness.Editing().StartScalarAnalysis(request, R::kOneShotBatchExecution, refusal));
        EXPECT_FALSE(refusal.Busy);
        return refusal.Message;
    };
    auto wide = Analysis(entity, D::PointCloudPoint);
    wide.Output.ValueKind = Geometry::PropertyValueKind::Double;
    EXPECT_EQ(Ready(harness, wide, B::CpuReference), "");
    EXPECT_EQ(Ready(harness, wide, B::CpuIndex), "");
    const std::string floatOnly =
        "Vulkan scalar analysis supports float outputs only; select the CPU backend for other scalar storage.";
    EXPECT_EQ(Ready(harness, wide, B::Gpu), floatOnly);
    EXPECT_EQ(refused(wide), floatOnly);
    auto many = Analysis(entity, D::PointCloudPoint);
    many.Params.K = 64;
    const std::string parameters = "Vulkan scalar analysis requires k<=63 and normal or zero parameters.";
    EXPECT_EQ(Ready(harness, many, B::CpuIndex), "");
    EXPECT_EQ(Ready(harness, many, B::Gpu), parameters);
    EXPECT_EQ(refused(many), parameters);
    auto tiny = Analysis(entity, D::PointCloudPoint);
    tiny.Params.Scale = std::numeric_limits<float>::denorm_min();
    EXPECT_EQ(Ready(harness, tiny, B::Gpu), parameters);
    const std::string device =
        "Vulkan scalar analysis requires an operational float64 device and framed spatial cache/jobs.";
    EXPECT_EQ(Ready(harness, Analysis(entity, D::PointCloudPoint), B::Gpu), device) << "no device here";
    EXPECT_EQ(refused(Analysis(entity, D::PointCloudPoint)), device);
    auto stale = Analysis(entity, D::PointCloudPoint);
    stale.StableEntityId = 999999u;
    EXPECT_EQ(Ready(harness, stale, B::CpuReference), "Radii target entity is stale or missing.");
    EXPECT_EQ(refused(stale), "Radii target entity is stale or missing.");
    EXPECT_FALSE(PointDomainProperties(harness.Scene(), entity, D::PointCloudPoint).Exists("spacing"));
    EXPECT_FALSE(harness.History().CanUndo());
    for (const auto& job : harness.Engine.Jobs().SnapshotAll())
        EXPECT_EQ(job.DebugName.find("scalar"), std::string::npos) << "queued: " << job.DebugName;
}

// Accept publishes the preview once, as one undo entry; Discard (also while Accept waits for its stage) leaves the
// output and the history as they were; a second start on the output is busy until the first ends.
TEST(PointCloudEditing, AScalarAnalysisAcceptsOnceAndDiscardsWithoutATrace)
{
    using P = R::ScalarAnalysisPhase;
    Harness harness{};
    ScalarRuns runs;
    const auto entity = Make(harness.Scene(), D::PointCloudPoint);
    harness.Frame(60);
    auto& props = PointDomainProperties(harness.Scene(), entity, D::PointCloudPoint);
    R::ScalarAnalysisRefusal refusal;
    auto run = runs.Start(harness, entity, D::PointCloudPoint, 3.f, refusal);
    ASSERT_TRUE(run) << refusal.Message;
    EXPECT_EQ(run.Poll().Phase, P::ReadyToAccept);
    EXPECT_TRUE(run.Poll().CanAccept);
    EXPECT_EQ(run.Poll().SlotCount, props.Size());
    EXPECT_EQ(run.Poll().Output.Domain, D::PointCloudPoint);
    EXPECT_TRUE(runs.HasRing(harness, entity, D::PointCloudPoint)) << "the preview is resident";
    auto second = runs.Start(harness, entity, D::PointCloudPoint, 5.f, refusal);
    EXPECT_FALSE(second);
    EXPECT_TRUE(refusal.Busy) << refusal.Message;
    EXPECT_EQ(run.Accept().Phase, P::Accepting);
    harness.Frame(3);
    EXPECT_EQ(run.Poll().Phase, P::Applied);
    ASSERT_TRUE(props.Exists("spacing"));
    EXPECT_EQ(std::as_const(props).Get<float>("spacing").Vector(), std::vector<float>(props.Size(), 3.f));
    EXPECT_EQ(harness.History().UndoCount(), 1u);
    EXPECT_NE(run.Accept().Phase, P::Accepting) << "an applied run is not accepted again";
    harness.Frame(3);
    EXPECT_EQ(harness.History().UndoCount(), 1u);

    auto discarded = runs.Start(harness, entity, D::PointCloudPoint, 7.f, refusal);
    ASSERT_TRUE(discarded) << refusal.Message;
    discarded.Discard();
    EXPECT_EQ(discarded.Poll().Phase, P::Discarded);
    EXPECT_FALSE(runs.HasRing(harness, entity, D::PointCloudPoint));
    auto accepting = runs.Start(harness, entity, D::PointCloudPoint, 9.f, refusal);
    ASSERT_TRUE(accepting) << refusal.Message;
    ASSERT_EQ(accepting.Accept().Phase, P::Accepting);
    accepting.Discard(); // the plugin's cancel, before the Accept stage ran
    harness.Frame(3);
    EXPECT_EQ(accepting.Poll().Phase, P::Discarded);
    EXPECT_EQ(std::as_const(props).Get<float>("spacing").Vector(), std::vector<float>(props.Size(), 3.f));
    EXPECT_EQ(harness.History().UndoCount(), 1u);
}

// The owner's lifetime bounds the preview: the last handle going discards the run; a run whose workspace detached is
// discarded at its next poll and publishes nothing.
TEST(PointCloudEditing, AScalarAnalysisEndsWithItsHandleAndWithItsWorkspace)
{
    using P = R::ScalarAnalysisPhase;
    Harness harness{};
    ScalarRuns runs;
    const auto entity = Make(harness.Scene(), D::PointCloudPoint);
    harness.Frame(60);
    R::ScalarAnalysisRefusal refusal;
    {
        auto run = runs.Start(harness, entity, D::PointCloudPoint, 3.f, refusal);
        ASSERT_TRUE(run) << refusal.Message;
        auto moved = std::move(run);
        EXPECT_TRUE(runs.HasRing(harness, entity, D::PointCloudPoint));
        auto other = runs.Start(harness, entity, D::PointCloudPoint, 4.f, refusal, "other");
        ASSERT_TRUE(other) << refusal.Message;
        moved = std::move(other); // the run `moved` held goes; the incoming one stays usable
        EXPECT_FALSE(runs.HasRing(harness, entity, D::PointCloudPoint));
        EXPECT_TRUE(runs.HasRing(harness, entity, D::PointCloudPoint, "other"));
        EXPECT_EQ(moved.Poll().Phase, P::ReadyToAccept);
    }
    EXPECT_FALSE(runs.HasRing(harness, entity, D::PointCloudPoint, "other")) << "the handle went: the preview with it";
    auto accepting = runs.Start(harness, entity, D::PointCloudPoint, 5.f, refusal);
    ASSERT_TRUE(accepting) << refusal.Message;
    ASSERT_EQ(accepting.Accept().Phase, P::Accepting);
    harness.Editing().ReleaseEditorWorkspace(harness.Shell);
    harness.Shell.Detach();
    const auto state = accepting.Poll();
    EXPECT_TRUE(state.Detached);
    EXPECT_EQ(state.Phase, P::Discarded);
    EXPECT_FALSE(state.CanAccept);
    harness.Frame(3);
    EXPECT_FALSE(PointDomainProperties(harness.Scene(), entity, D::PointCloudPoint).Exists("spacing"));
    EXPECT_FALSE(runs.HasRing(harness, entity, D::PointCloudPoint));
}

// The service stopping to use a workspace (the Sandbox shell releasing it, still attached) ends that workspace's
// runs at once, also one whose Accept waits for its stage: nothing is published, the preview goes.
TEST(PointCloudEditing, AScalarAnalysisEndsWhenTheServiceReleasesItsWorkspace)
{
    using P = R::ScalarAnalysisPhase;
    Harness harness{};
    ScalarRuns runs;
    const auto entity = Make(harness.Scene(), D::PointCloudPoint);
    harness.Frame(60);
    R::ScalarAnalysisRefusal refusal;
    auto ready = runs.Start(harness, entity, D::PointCloudPoint, 3.f, refusal);
    ASSERT_TRUE(ready) << refusal.Message;
    auto accepting = runs.Start(harness, entity, D::PointCloudPoint, 5.f, refusal, "other");
    ASSERT_TRUE(accepting) << refusal.Message;
    ASSERT_EQ(accepting.Accept().Phase, P::Accepting);
    harness.Editing().ReleaseEditorWorkspace(harness.Shell); // the shell stays attached
    harness.Frame(3);
    for (auto* run : {&ready, &accepting})
    {
        const auto state = run->Poll();
        EXPECT_TRUE(state.Detached);
        EXPECT_EQ(state.Phase, P::Discarded);
        EXPECT_FALSE(state.CanAccept);
    }
    EXPECT_NE(ready.Accept().Phase, P::Accepting);
    harness.Frame(3);
    const auto& props = PointDomainProperties(harness.Scene(), entity, D::PointCloudPoint);
    EXPECT_FALSE(props.Exists("spacing"));
    EXPECT_FALSE(props.Exists("other"));
    EXPECT_FALSE(harness.History().CanUndo());
    EXPECT_FALSE(runs.HasRing(harness, entity, D::PointCloudPoint));
    EXPECT_FALSE(runs.HasRing(harness, entity, D::PointCloudPoint, "other"));
}

// A session ends when the workspace's attachment does, also when the same workspace detaches and attaches again in
// place without another UseEditorWorkspace, and when the service releases the workspace.
TEST(PointCloudEditing, ASessionEndsWithItsAttachmentAndItsWorkspace)
{
    Harness harness{};
    const auto before = harness.Editing().CurrentSession();
    EXPECT_TRUE(harness.Editing().SessionCurrent(before));
    harness.Shell.Detach();
    EXPECT_FALSE(harness.Editing().SessionCurrent(before));
    harness.Shell.Attach(harness.Engine.Worlds(), harness.Engine.Services());
    (void)R::PrepareEditorWorkspaceSnapshotFrame(harness.Shell);
    EXPECT_FALSE(harness.Editing().SessionCurrent(before)) << "the same workspace, a new attachment";
    const auto after = harness.Editing().CurrentSession();
    EXPECT_TRUE(harness.Editing().SessionCurrent(after));
    harness.Editing().ReleaseEditorWorkspace(harness.Shell);
    EXPECT_FALSE(harness.Editing().SessionCurrent(after)) << "the service uses its own workspace again";
    EXPECT_TRUE(harness.Editing().SessionCurrent(harness.Editing().CurrentSession()));
}
