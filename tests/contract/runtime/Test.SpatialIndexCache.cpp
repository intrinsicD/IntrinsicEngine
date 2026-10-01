#include <array>
#include <cmath>
#include <limits>
#include <span>
#include <utility>
#include <glm/glm.hpp>
#include <gtest/gtest.h>
#include <memory>
#include <optional>
#include <vector>
#include "MockRHI.hpp"
#include "SandboxEditorJobHarness.hpp"
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.JobService;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.TransferQueue;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.CommandContext;
import Extrinsic.Runtime.WorldRegistry;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Geometry.HalfedgeMesh;
import Geometry.Graph;
namespace R = Extrinsic::Runtime;
namespace GS = Extrinsic::ECS::Components::GeometrySources;
using D = R::GeometryElementDomain;
namespace
{
    R::GeometryPropertyRef Ref(D domain, const char* name = "samples")
    {
        return {.Domain = domain, .Name = name, .ValueKind = Geometry::PropertyValueKind::Vec3};
    }
} // namespace
TEST(SpatialIndexCache, SnapshotComparisonPreservesOrderedRowsAndNumericEquality)
{
    EXPECT_FALSE(R::SpatialIndexSnapshotMatches(nullptr, {}, {}));
    R::SpatialIndexSnapshot empty;
    EXPECT_TRUE(R::SpatialIndexSnapshotMatches(&empty, {}, {}));

    const std::array<glm::vec3, 3> points{{{0, 0, 0}, {1, 2, 3}, {2, 3, 4}}};
    R::SpatialIndexSnapshot snapshot;
    ASSERT_TRUE(snapshot.Index.Build(points));
    snapshot.Slots = {2, 4, 8};
    EXPECT_TRUE(R::SpatialIndexSnapshotMatches(&snapshot, snapshot.Slots, points));
    EXPECT_FALSE(R::SpatialIndexSnapshotMatches(&snapshot,
        std::span(snapshot.Slots).first(2), points));
    EXPECT_FALSE(R::SpatialIndexSnapshotMatches(&snapshot,
        snapshot.Slots, std::span(points).first(2)));
    auto slots = snapshot.Slots;
    slots[1] = 5;
    EXPECT_FALSE(R::SpatialIndexSnapshotMatches(&snapshot, slots, points));
    slots = snapshot.Slots;
    std::swap(slots[0], slots[1]);
    EXPECT_FALSE(R::SpatialIndexSnapshotMatches(&snapshot, slots, points));

    auto changed = points;
    changed[1].x = std::nextafter(changed[1].x, 2.f);
    EXPECT_FALSE(R::SpatialIndexSnapshotMatches(&snapshot, snapshot.Slots, changed));
    changed = points;
    std::swap(changed[0], changed[1]);
    EXPECT_FALSE(R::SpatialIndexSnapshotMatches(&snapshot, snapshot.Slots, changed));
    changed = points;
    changed[0].x = -0.f;
    EXPECT_TRUE(R::SpatialIndexSnapshotMatches(&snapshot, snapshot.Slots, changed));
    changed[0].x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(R::SpatialIndexSnapshotMatches(&snapshot, snapshot.Slots, changed));
}

TEST(SpatialIndexCache, ReusesOnlyUnchangedPropertyAndLiveElementIdentity)
{
    R::WorldRegistry worlds;
    const auto world = worlds.CreateWorld("cache-test");
    auto& scene = *worlds.Get(world);
    R::SpatialIndexCache cache(worlds);
    const auto entity = scene.Create();
    auto& props = scene.Raw().emplace<GS::Vertices>(entity).Properties;
    props.Resize(3);
    auto positions = props.GetOrAdd<glm::vec3>("samples");
    positions.Vector() = {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}};
    auto deleted = props.GetOrAdd<bool>("v:deleted");
    deleted[0] = true;
    auto first = cache.Acquire(world, entity, Ref(D::PointCloudPoint));
    ASSERT_TRUE(first.Ready()) << first.Diagnostic;
    auto nearest = cache.Nearest(first.Handle, {0, 0, 0});
    ASSERT_TRUE(nearest);
    EXPECT_EQ(nearest->Index, 1);
    auto knn = cache.KNearest(first.Handle, {}, 8, 1);
    ASSERT_TRUE(knn);
    ASSERT_EQ(knn->size(), 1);
    EXPECT_EQ(knn->front().Index, 2);
    EXPECT_EQ(cache.Nearest(first.Handle, {}, 1)->Index, 2);
    EXPECT_EQ(cache.Radius(first.Handle, {}, 3, 0, 1)->TotalCount, 1);
    EXPECT_EQ(cache.KNearest(first.Handle, {}, 8, 0)->size(), 2); // Deleted slot excludes nothing.
    EXPECT_TRUE(cache.KNearest(first.Handle, {}, 0)->empty());
    EXPECT_EQ(cache.QueueGpuKNearest(first.Handle, std::vector<glm::vec3>{{}}, 3)->State,
              R::SpatialQueryState::Failed);
    auto second = cache.Acquire(world, entity, Ref(D::PointCloudPoint));
    EXPECT_TRUE(second.Reused);
    EXPECT_EQ(first.Handle.Value, second.Handle.Value);
    auto color = props.GetOrAdd<glm::vec3>("unrelated");
    color[0] = {1, 0, 0};
    EXPECT_TRUE(cache.Acquire(world, entity, Ref(D::PointCloudPoint)).Reused);
    positions[1] = {10, 0, 0};
    EXPECT_FALSE(cache.Nearest(first.Handle, {}));
    EXPECT_FALSE(cache.KNearest(first.Handle, {}, 3));
    auto changed = cache.Acquire(world, entity, Ref(D::PointCloudPoint));
    ASSERT_TRUE(changed.Ready());
    EXPECT_FALSE(changed.Reused);
    EXPECT_EQ(cache.Nearest(changed.Handle, {})->Index, 2);
    deleted[0] = false;
    EXPECT_FALSE(cache.Nearest(changed.Handle, {}));
    changed = cache.Acquire(world, entity, Ref(D::PointCloudPoint));
    EXPECT_EQ(cache.Nearest(changed.Handle, {})->Index, 0);
    const auto radius = cache.Radius(changed.Handle, {}, 3, 1);
    ASSERT_TRUE(radius);
    EXPECT_EQ(radius->TotalCount, 2);
    EXPECT_TRUE(radius->Overflowed());
    worlds.Clear();
    cache.Prune();
    EXPECT_FALSE(cache.Nearest(changed.Handle, {}));
    EXPECT_EQ(cache.Stats().Builds, 3);
    EXPECT_EQ(cache.Stats().Hits, 2);
    EXPECT_EQ(cache.Stats().Evictions, 3);
}
TEST(SpatialIndexCache, CanonicalDomainsAndWorldEntityIdentity)
{
    R::WorldRegistry worlds;
    const auto world = worlds.CreateWorld("cache-test");
    auto& scene = *worlds.Get(world);
    R::SpatialIndexCache cache(worlds);
    Geometry::HalfedgeMesh::Mesh mesh;
    auto a = mesh.AddVertex({0, 0, 0}), b = mesh.AddVertex({1, 0, 0}),
         c = mesh.AddVertex({0, 1, 0});
    ASSERT_TRUE(mesh.AddTriangle(a, b, c));
    auto entity = scene.Create();
    GS::PopulateFromMesh(scene.Raw(), entity, mesh);
    auto check = [&](D domain, Geometry::PropertySet& props) {
        auto samples = props.GetOrAdd<glm::vec3>("samples");
        for (std::uint32_t i = 0; i < props.Size(); ++i)
            samples[i] = {float(i), 0, 0};
        auto value = cache.Acquire(world, entity, Ref(domain));
        ASSERT_TRUE(value.Ready()) << value.Diagnostic;
        auto n = cache.Nearest(value.Handle, {});
        ASSERT_TRUE(n);
        EXPECT_EQ(n->Index, 0);
        auto neighbors = cache.KNearest(value.Handle, {}, 64, 0);
        ASSERT_TRUE(neighbors);
        ASSERT_EQ(neighbors->size(), props.Size()-1);
        for (std::size_t i=0; i<neighbors->size(); ++i) EXPECT_EQ((*neighbors)[i].Index, i+1);
    };
    check(D::MeshVertex, scene.Raw().get<GS::Vertices>(entity).Properties);
    check(D::MeshFace, scene.Raw().get<GS::Faces>(entity).Properties);
    check(D::MeshEdge, scene.Raw().get<GS::Edges>(entity).Properties);
    check(D::MeshHalfedge, scene.Raw().get<GS::Halfedges>(entity).Properties);
    auto wrong = Ref(D::MeshFace);
    wrong.ValueKind = Geometry::PropertyValueKind::Float;
    EXPECT_FALSE(cache.Acquire(world, entity, wrong).Ready());
    const auto other = worlds.CreateWorld("other");
    auto& scene2 = *worlds.Get(other);
    auto entity2 = scene2.Create();
    auto& p = scene2.Raw().emplace<GS::Vertices>(entity2).Properties;
    p.Resize(1);
    p.GetOrAdd<glm::vec3>("samples")[0] = {9, 0, 0};
    auto one = cache.Acquire(world, entity, Ref(D::MeshVertex)),
         two = cache.Acquire(other, entity2, Ref(D::PointCloudPoint));
    ASSERT_TRUE(one.Ready());
    ASSERT_TRUE(two.Ready());
    EXPECT_NE(one.Handle.Value, two.Handle.Value);
    EXPECT_FLOAT_EQ(cache.Nearest(two.Handle, {})->SquaredDistance, 81);
    scene2.Destroy(entity2);
    cache.Prune();
    EXPECT_FALSE(cache.Nearest(two.Handle, {}));
    EXPECT_TRUE(cache.Nearest(one.Handle, {}));
    Geometry::Graph::Graph graph;
    const auto ga = graph.AddVertex({0, 0, 0}), gb = graph.AddVertex({1, 0, 0});
    (void)graph.AddEdge(ga, gb);
    GS::PopulateFromGraph(scene.Raw(), entity, graph);
    check(D::GraphNode, scene.Raw().get<GS::Vertices>(entity).Properties);
    check(D::GraphEdge, scene.Raw().get<GS::Edges>(entity).Properties);
    check(D::GraphHalfedge, scene.Raw().get<GS::Halfedges>(entity).Properties);
    EXPECT_FALSE(cache.Nearest(one.Handle, {}));
}

TEST(SpatialIndexCache, FramedRadiusWithoutDeviceFailsExplicitly)
{
    R::WorldRegistry worlds;
    R::SpatialIndexCache cache(worlds);
    EXPECT_FALSE(cache.GpuQueriesAvailable());
    const auto computation=cache.QueueGpuCompute({},32,{});
    ASSERT_TRUE(computation);
    EXPECT_EQ(computation->State,R::SpatialQueryState::Failed);
    EXPECT_FALSE(computation->Diagnostic.empty());
    // GRAPHICS-150: an immediate request fails closed the same way without a device.
    const auto immediate=cache.QueueGpuCompute(32,[](auto&,const auto&){return Extrinsic::RHI::BufferHandle{};},
                                               R::SpatialGpuLatency::Immediate);
    ASSERT_TRUE(immediate);
    EXPECT_EQ(immediate->State,R::SpatialQueryState::Failed);
    const std::vector<glm::vec3> queries{{0,0,0}};
    for (float radius : {-1.f, 0.f, 1.f})
    {
        const auto batch = cache.QueueGpuRadius({}, queries, radius, 2);
        ASSERT_TRUE(batch);
        EXPECT_EQ(batch->State, R::SpatialQueryState::Failed);
        EXPECT_FALSE(batch->Diagnostic.empty());
    }
}

// GRAPHICS-153: an iterative caller updates its private workspace in place; old snapshot leases
// stay valid, and a wrong count, a foreign handle or invalid points leave the workspace alone.
TEST(SpatialIndexCache, PrivateWorkspaceUpdatesInPlaceAndKeepsOldLeases)
{
    R::SpatialIndexCache cache;
    std::vector<glm::vec3> points{{0,0,0},{2,0,0},{4,0,0}};
    auto moving=cache.CreateWorkspace(points);ASSERT_TRUE(moving.Ready());
    const auto handle=moving.Handle;const auto before=moving.Snapshot;
    EXPECT_EQ(cache.Nearest(handle,{3.9f,0,0})->Index,2u);
    std::vector<glm::vec3> moved{{10,0,0},{12,0,0},{3.8f,0,0}};
    ASSERT_TRUE(cache.UpdateWorkspace(moving,moved));
    EXPECT_EQ(moving.Handle.Value,handle.Value) << "same workspace, no new build";
    EXPECT_NE(moving.Snapshot,before);
    EXPECT_EQ(before->Index.Points()[0],glm::vec3(0,0,0)) << "an old lease keeps its points";
    EXPECT_EQ(cache.Nearest(handle,{0,0,0})->Index,2u);
    EXPECT_EQ(cache.Stats().Builds,1u);EXPECT_EQ(cache.Stats().WorkspaceUpdates,1u);
    EXPECT_FALSE(cache.UpdateWorkspace(moving,std::vector<glm::vec3>{{0,0,0}})) << "count changed";
    moved[1].x=1e30f;EXPECT_FALSE(cache.UpdateWorkspace(moving,moved)) << "invalid points";
    EXPECT_EQ(cache.Nearest(handle,{0,0,0})->Index,2u) << "a refused update leaves the workspace";
    R::SpatialIndexWorkspace foreign{.Handle={9999},.Snapshot=moving.Snapshot};
    EXPECT_FALSE(cache.UpdateWorkspace(foreign,points));
    EXPECT_EQ(cache.Stats().WorkspaceUpdates,1u);
}

TEST(SpatialIndexCache, PrivateWorkspaceLeaseOwnsImmutableIndependentPositions)
{
    R::SpatialIndexCache cache;
    std::vector<glm::vec3> points{{0,0,0},{2,0,0}};
    auto a=cache.CreateWorkspace(points);ASSERT_TRUE(a.Ready())<<a.Diagnostic;
    points[0]={5,0,0};auto b=cache.CreateWorkspace(points);ASSERT_TRUE(b.Ready());
    EXPECT_NE(a.Handle.Value,b.Handle.Value);EXPECT_EQ(a.Snapshot->Slots,(std::vector<std::uint32_t>{0,1}));
    EXPECT_EQ(cache.Nearest(a.Handle,{})->Index,0);EXPECT_EQ(cache.Nearest(b.Handle,{})->Index,1);
    auto lease=a.Snapshot;const auto handle=a.Handle;a={};cache.Prune();EXPECT_TRUE(cache.Nearest(handle,{}));
    lease.reset();EXPECT_FALSE(cache.Nearest(handle,{}));cache.Prune();EXPECT_EQ(cache.Stats().Evictions,1);
    EXPECT_TRUE(cache.Nearest(b.Handle,{}));b={};cache.Prune();EXPECT_EQ(cache.Stats().Evictions,2);
    EXPECT_FALSE(cache.CreateWorkspace({}).Ready());points[0].x=1e30f;EXPECT_FALSE(cache.CreateWorkspace(points).Ready());
}

namespace
{
    // A leasable workspace kind that counts its live objects.
    struct CountingWorkspace
    {
        static inline int Live = 0;
        explicit CountingWorkspace(Extrinsic::RHI::IDevice&) { ++Live; }
        ~CountingWorkspace() { --Live; }
    };
    struct OtherWorkspace
    {
        explicit OtherWorkspace(Extrinsic::RHI::IDevice&) {}
    };
    class SpatialWorkspaceLeases : public ::testing::Test
    {
    protected:
        Extrinsic::Tests::MockDevice Device;
        Extrinsic::Tests::EditorJobHarness Jobs;
        R::WorldRegistry Worlds;
        R::CommandBus Commands;
        R::KernelEventBus Events;
        R::ServiceRegistry Services;
        R::SpatialIndexCache Cache;
        bool Stopped{};
        void SetUp() override
        {
            CountingWorkspace::Live = 0;
            Services.BeginRegistration();
            ASSERT_TRUE(Services.Provide<Extrinsic::RHI::IDevice>(Device, "test").has_value());
            R::EngineSetup setup{Commands, Events, Jobs.Jobs(), Worlds, Services, [](R::FramePhase, R::RuntimeFrameHook) {}};
            ASSERT_TRUE(Cache.OnRegister(setup).has_value());
        }
        void TearDown() override { Shutdown(); }
        void Shutdown()
        {
            if (std::exchange(Stopped, true)) return;
            R::RuntimeModuleShutdownContext shutdown{Commands, Events, Jobs.Jobs(), Worlds, Services};
            Cache.OnShutdown(shutdown);
        }
        // Records the participant's frame commands and drains completed work.
        void Frame()
        {
            Jobs.Jobs().RecordGpuQueueFrameCommands(Device.CommandContext);
            (void)Jobs.Jobs().DrainGpuQueueCompletedTransfers();
        }
        // Advances past every frame that may still execute work recorded so far.
        void RetireFrames()
        {
            Device.GlobalFrameNumber += Device.FramesInFlight + 1u;
            Cache.Prune();
        }
        auto Lease() { return Cache.LeaseGpuWorkspace<CountingWorkspace>(); }
    };
}

TEST_F(SpatialWorkspaceLeases, OverlappingLeasesNeverAliasAndOneRetiredWorkspacePerKindIsReused)
{
    auto first = Lease(), overlap = Lease();
    ASSERT_TRUE(first && overlap);
    EXPECT_NE(first.get(), overlap.get());
    EXPECT_EQ(CountingWorkspace::Live, 2);
    first.reset();
    overlap.reset();
    EXPECT_EQ(CountingWorkspace::Live, 2) << "returned, not destroyed";
    // Another kind is never served by them; and nothing is leased again before its frames retire.
    EXPECT_TRUE(Cache.LeaseGpuWorkspace<OtherWorkspace>());
    auto early = Lease();
    EXPECT_EQ(CountingWorkspace::Live, 3);
    EXPECT_EQ(Cache.Stats().WorkspaceReuses, 0u);
    early.reset();
    RetireFrames(); // first/overlap mature: only one idle workspace is kept
    EXPECT_EQ(CountingWorkspace::Live, 2);
    RetireFrames(); // early matures and replaces it
    EXPECT_EQ(CountingWorkspace::Live, 1);
    auto warm = Lease();
    EXPECT_EQ(CountingWorkspace::Live, 1);
    EXPECT_EQ(Cache.Stats().WorkspaceReuses, 1u);
    auto concurrent = Lease();
    EXPECT_NE(concurrent.get(), warm.get());
    EXPECT_EQ(CountingWorkspace::Live, 2);
    EXPECT_EQ(Cache.Stats().WorkspaceLeases, 6u);
    EXPECT_EQ(Cache.Stats().WorkspaceReuses, 1u);
}

TEST_F(SpatialWorkspaceLeases, ShutdownReleasesRetainedWorkspacesAndDetachesHeldLeases)
{
    auto held = Lease();
    Lease().reset();
    EXPECT_EQ(CountingWorkspace::Live, 2);
    EXPECT_TRUE(Jobs.Jobs().HasGpuQueueWork()) << "a pooled workspace keeps the device-idle shutdown";
    Shutdown();
    EXPECT_EQ(CountingWorkspace::Live, 1);
    EXPECT_FALSE(Lease());
    held.reset();
    EXPECT_EQ(CountingWorkspace::Live, 0);
}

TEST_F(SpatialWorkspaceLeases, FramedRecorderFailureKeepsItsWorkspaceUntilThatFrameRetires)
{
    std::weak_ptr<CountingWorkspace> watched;
    std::shared_ptr<R::SpatialGpuResult> result;
    {
        auto lease = Lease();
        watched = lease;
        result = Cache.QueueGpuCompute(4u, [lease](auto& commands, const auto&) {
            commands.FillBuffer({}, 0, 4, 0); // recorded before the recorder fails
            return Extrinsic::RHI::BufferHandle{};
        });
    }
    Frame();
    EXPECT_EQ(result->State, R::SpatialQueryState::Failed);
    result.reset();
    Device.GlobalFrameNumber += Device.FramesInFlight;
    Frame();
    EXPECT_FALSE(watched.expired()) << "its frame may still execute";
    Device.GlobalFrameNumber += 1u;
    Frame();
    EXPECT_TRUE(watched.expired());
    Cache.Prune();
    RetireFrames();
    auto warm = Lease();
    EXPECT_EQ(Cache.Stats().WorkspaceReuses, 1u);
    EXPECT_EQ(CountingWorkspace::Live, 1);
}

TEST_F(SpatialWorkspaceLeases, ImmediateSubmissionRefusalReturnsWorkspaceForReuse)
{
    Device.ComputeReadback = [this](auto record, auto, auto) {
        (void)record(Device.CommandContext);
        return Extrinsic::RHI::ReadbackToken{}; // refused after recording
    };
    std::weak_ptr<CountingWorkspace> watched;
    {
        auto lease = Lease();
        watched = lease;
        const auto output = Device.CreateBuffer({.SizeBytes = 16});
        const auto result = Cache.QueueGpuCompute(4u, [lease, output](auto&, const auto&) { return output; },
                                                  R::SpatialGpuLatency::Immediate);
        EXPECT_EQ(result->State, R::SpatialQueryState::Failed);
    }
    for (int i = 0; i < 3; ++i)
    {
        Device.GlobalFrameNumber += Device.FramesInFlight + 1u;
        Frame();
        Cache.Prune();
    }
    EXPECT_TRUE(watched.expired());
    auto fresh = Lease();
    EXPECT_EQ(Cache.Stats().WorkspaceReuses, 1u);
    EXPECT_EQ(CountingWorkspace::Live, 1);
    EXPECT_FALSE(Jobs.Jobs().HasGpuQueueWork());
    Shutdown();
    EXPECT_TRUE(watched.expired());
    EXPECT_EQ(CountingWorkspace::Live, 1);
}

TEST_F(SpatialWorkspaceLeases, CancelledRunDoesNotRecycleWorkspaceBeforeItsSubmissionCompletes)
{
    std::optional<Extrinsic::RHI::ReadbackSink> held;
    Device.ComputeReadback = [&](auto record, auto, auto sink) {
        EXPECT_TRUE(record(Device.CommandContext).IsValid());
        held = std::move(sink);
        return Extrinsic::RHI::ReadbackToken{7};
    };
    std::weak_ptr<CountingWorkspace> watched;
    {
        auto lease = Lease();
        watched = lease;
        const auto output = Device.CreateBuffer({.SizeBytes = 16});
        // The caller drops both its lease and the result, as a cancelled run does.
        (void)Cache.QueueGpuCompute(0u, [lease, output](auto&, const auto&) { return output; },
                                    R::SpatialGpuLatency::Immediate);
    }
    ASSERT_TRUE(held);
    for (int i = 0; i < 3; ++i)
    {
        Device.GlobalFrameNumber += Device.FramesInFlight + 1u;
        Frame();
        Cache.Prune();
    }
    EXPECT_FALSE(watched.expired());
    auto during = Lease();
    EXPECT_EQ(Cache.Stats().WorkspaceReuses, 0u);
    during.reset();
    held->Deliver({}); // the GPU finished
    held.reset();
    Frame();
    EXPECT_TRUE(watched.expired());
}

TEST_F(SpatialWorkspaceLeases, TransformedQueriesUploadOnlyChangedInputsAndReuseRetiredScratch)
{
    Device.ShaderFloat64 = true;
    Device.TransferQueue.BufferDownload = [](auto, std::uint64_t bytes, auto, auto sink) {
        std::vector<std::uint32_t> data(bytes / 4u, 0u);
        for (std::size_t i = 0; i < data.size(); i += 2) data[i] = 1u;
        sink.Deliver(std::as_bytes(std::span(data)));
        return Extrinsic::RHI::ReadbackToken{1};
    };
    std::vector<glm::vec3> source{{0,0,0}, {1,0,0}, {2,0,0}};
    const auto target = Cache.CreateWorkspace(source);
    ASSERT_TRUE(target.Ready());
    glm::dmat4 pose{1.0};
    auto batch = Cache.QueueGpuNearestTransformed(target.Handle, source, pose);
    auto complete = [&] {
        Frame();
        Device.GlobalFrameNumber += Device.FramesInFlight + 1u;
        Frame();
        Frame();
        EXPECT_EQ(batch->State, R::SpatialQueryState::Ready) << batch->Diagnostic;
    };
    complete();
    EXPECT_EQ(Cache.Stats().GpuQueryUploadBytes, 3u * 12u + sizeof(pose));
    auto uploaded = Cache.Stats().GpuQueryUploadBytes;
    auto allocations = Cache.Stats().GpuBatchAllocations;
    pose[3].x = 0.125;
    batch = Cache.QueueGpuNearestTransformed(target.Handle, source, pose, batch);
    complete();
    EXPECT_EQ(Cache.Stats().GpuQueryUploadBytes - uploaded, sizeof(pose));
    EXPECT_EQ(Cache.Stats().GpuBatchAllocations, allocations);
    std::optional<Extrinsic::RHI::ReadbackSink> pending;
    Device.ComputeReadback = [&](auto record, auto bytes, auto sink) {
        EXPECT_TRUE(record(Device.CommandContext).IsValid());
        EXPECT_EQ(bytes, source.size()*16u);
        pending = std::move(sink);
        return Extrinsic::RHI::ReadbackToken{2};
    };
    batch = Cache.QueueGpuNearestTransformed(target.Handle, source, pose, batch);
    EXPECT_EQ(batch->State, R::SpatialQueryState::Submitted);
    EXPECT_EQ(Cache.Stats().GpuQueryImmediateSubmissions, 1u);
    ASSERT_TRUE(pending);
    std::vector<std::uint32_t> packed(source.size()*4u, 0u);
    for (std::size_t i = 0; i < packed.size(); i += 2) packed[i] = 1u;
    pending->Deliver(std::as_bytes(std::span(packed)));
    pending.reset();
    Frame(); // no frame retirement delay for the completed immediate readback
    EXPECT_EQ(batch->State, R::SpatialQueryState::Ready);
    Device.ComputeReadback = {};
    source[0].y = 1.f;
    uploaded = Cache.Stats().GpuQueryUploadBytes;
    batch = Cache.QueueGpuNearestTransformed(target.Handle, source, pose, batch);
    complete();
    EXPECT_EQ(Cache.Stats().GpuQueryUploadBytes - uploaded, source.size()*12u + sizeof(pose));
    source.resize(5, {1,1,1});
    batch = Cache.QueueGpuNearestTransformed(target.Handle, source, pose, batch);
    complete();
    EXPECT_EQ(Cache.Stats().GpuBatchAllocations, allocations + 1u);
    source.resize(2);
    batch = Cache.QueueGpuNearestTransformed(target.Handle, source, pose, batch);
    complete();
    EXPECT_EQ(batch->Neighbors.size(), 2u);
    EXPECT_EQ(Cache.Stats().GpuBatchAllocations, allocations + 1u);
    batch.reset();
    Frame();
    Cache.Prune();
    RetireFrames();
    uploaded = Cache.Stats().GpuQueryUploadBytes;
    batch = Cache.QueueGpuNearestTransformed(target.Handle, source, pose);
    complete();
    EXPECT_GT(Cache.Stats().WorkspaceReuses, 0u);
    EXPECT_EQ(Cache.Stats().GpuBatchAllocations, allocations + 1u);
    EXPECT_EQ(Cache.Stats().GpuQueryUploadBytes - uploaded, sizeof(pose));
}

TEST_F(SpatialWorkspaceLeases, TransformedQueriesRefuseUnsupportedAndNonfiniteTransforms)
{
    const std::vector<glm::vec3> points{{0,0,0}, {1,0,0}};
    const auto target = Cache.CreateWorkspace(points);
    glm::dmat4 pose{1.0};
    EXPECT_EQ(Cache.QueueGpuNearestTransformed(target.Handle, points, pose)->State, R::SpatialQueryState::Failed);
    Device.ShaderFloat64 = true;
    pose[3].x = std::numeric_limits<double>::quiet_NaN();
    EXPECT_EQ(Cache.QueueGpuNearestTransformed(target.Handle, points, pose)->State, R::SpatialQueryState::Failed);
    EXPECT_EQ(Cache.Stats().GpuQueryUploadBytes, 0u);
}
