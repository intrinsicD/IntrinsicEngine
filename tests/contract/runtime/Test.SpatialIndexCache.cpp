#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <cmath>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <glm/glm.hpp>
#include <gtest/gtest.h>
#include <memory>
#include <optional>
#include <vector>
#include "MockRHI.hpp"
#include "SandboxEditorJobHarness.hpp"
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Graphics.PointLBVH;
import Extrinsic.Runtime.GpuPropertyBinding;
import Extrinsic.Graphics.OutlierAnalysis;
import Extrinsic.Graphics.PointNormals;
import Extrinsic.Graphics.PointScalarAnalysis;
import Extrinsic.RHI.BufferManager;
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
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Geometry.HalfedgeMesh;
import Geometry.Graph;
import Geometry.PointCloud;
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
    std::string why;
    EXPECT_FALSE(cache.PrepareQuerySession(
        first.Handle, {.Capacity = 3u, .PageQueries = 1u, .Queries = std::vector<glm::vec3>{{}}}, why))
        << "no device: no GPU kNN session";
    EXPECT_FALSE(why.empty());
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

TEST(SpatialIndexCache, GpuWorkWithoutDeviceFailsExplicitly)
{
    R::WorldRegistry worlds;
    R::SpatialIndexCache cache(worlds);
    EXPECT_FALSE(cache.GpuQueriesAvailable());
    const auto computation=cache.QueueGpuCompute({},32,{});
    ASSERT_TRUE(computation);
    EXPECT_EQ(computation->State,R::SpatialQueryState::Failed);
    EXPECT_FALSE(computation->Diagnostic.empty());
    // A request without an index fails closed the same way without a device.
    const auto immediate = cache.QueueGpuCompute(32,
        [](auto&, const auto&)
        {
            return Extrinsic::RHI::BufferHandle{};
        });
    ASSERT_TRUE(immediate);
    EXPECT_EQ(immediate->State,R::SpatialQueryState::Failed);
    // A radius session needs no index when its target moves: only the missing device refuses it.
    const std::vector<glm::vec3> queries{{0,0,0}};
    std::string why;
    EXPECT_FALSE(cache.PrepareQuerySession(
        {}, {.Radius = true, .Capacity = 2u, .PageQueries = 1u, .Queries = queries, .Moving = true}, why));
    EXPECT_FALSE(why.empty());
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
    namespace G = Extrinsic::Graphics;
    // The pool parks only the engine's workspace kinds (LeaseGpuWorkspace is instantiated for those alone):
    // these tests lease scalar-analysis workspaces, each recorded once by Lease(), and outlier ones.
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
        R::WorldHandle Scene{};
        R::WorldHandle World()
        {
            if (!Scene.IsValid())
                Scene = Worlds.CreateWorld("lane");
            return Scene;
        }
        void SetUp() override
        {
            Services.BeginRegistration();
            ASSERT_TRUE(Services.Provide<Extrinsic::RHI::IDevice>(Device, "test").has_value());
            R::EngineSetup setup{Commands, Events, Jobs.Jobs(), Worlds, Services, [](R::FramePhase, R::RuntimeFrameHook) {}};
            ASSERT_TRUE(Cache.OnRegister(setup).has_value());
            UseLane();
        }
        void TearDown() override { Shutdown(); }
        void Shutdown()
        {
            if (std::exchange(Stopped, true)) return;
            R::RuntimeModuleShutdownContext shutdown{Commands, Events, Jobs.Jobs(), Worlds, Services};
            Cache.OnShutdown(shutdown);
        }
        // ARCH-023: the device's compute lane, as IDevice::SubmitComputeReadback promises it. The
        // recorder runs on the async compute context (never the frame's); an accepted submission's
        // token stays incomplete and its sink pending until Deliver (the transfer queue's
        // CollectCompleted), which Frame() runs unless Hold. A refusal after recording keeps the
        // sink in Refused, so a test can show a late delivery publishes nothing.
        struct Submission
        {
            Extrinsic::RHI::ReadbackSink Sink{};
            std::uint64_t Bytes{};
            Extrinsic::RHI::ReadbackToken Token{};
        };
        std::vector<Submission> InFlight{}, Refused{};
        bool Hold{}, RefuseAfterRecording{};
        int LaneSubmissions{};
        std::uint64_t Tokens{};
        void UseLane()
        {
            Device.ComputeReadback = [this](auto record, std::uint64_t bytes, Extrinsic::RHI::ReadbackSink sink)
            {
                if (!record(Device.AsyncComputeContext).IsValid())
                    return Extrinsic::RHI::ReadbackToken{};
                if (RefuseAfterRecording)
                {
                    Refused.push_back({std::move(sink), bytes, {}});
                    return Extrinsic::RHI::ReadbackToken{};
                }
                ++LaneSubmissions;
                const Extrinsic::RHI::ReadbackToken token{(1ull << 63u) | ++Tokens};
                Device.TransferQueue.PendingReadbacks.push_back(token);
                InFlight.push_back({std::move(sink), bytes, token});
                return token;
            };
        }
        // Every even word 1: nearest rows {index 1, distance 0} and headers {1 hit, status 0}.
        static std::vector<std::uint32_t> NearestAnswer(std::uint64_t bytes)
        {
            std::vector<std::uint32_t> data(bytes / 4u, 0u);
            for (std::size_t i = 0; i < data.size(); i += 2)
                data[i] = 1u;
            return data;
        }
        void Deliver(std::size_t index)
        {
            auto done = std::move(InFlight[index]);
            InFlight.erase(InFlight.begin() + std::ptrdiff_t(index));
            std::erase(Device.TransferQueue.PendingReadbacks, done.Token);
            const auto answer = NearestAnswer(done.Bytes);
            done.Sink.Deliver(std::as_bytes(std::span(answer)));
        }
        void Frame()
        {
            Jobs.Jobs().RecordGpuQueueFrameCommands(Device.CommandContext);
            while (!Hold && !InFlight.empty())
                Deliver(0);
            (void)Jobs.Jobs().DrainGpuQueueCompletedTransfers();
        }
        // Advances past every frame that may still execute work recorded so far.
        void RetireFrames()
        {
            Device.GlobalFrameNumber += Device.FramesInFlight + 1u;
            Cache.Prune();
        }
        // Times `shader` was bound on `context`.
        std::ptrdiff_t Bound(const Extrinsic::Tests::MockCommandContext& context, std::string_view shader) const
        {
            for (std::size_t i = 0; i < Device.CreatedPipelineDescs.size(); ++i)
                if (Device.CreatedPipelineDescs[i].ComputeShaderPath.ends_with(std::string(shader) + ".comp.spv"))
                    return std::ranges::count(context.BoundPipelines, Device.CreatedPipelineHandles[i]);
            return 0;
        }
        bool FrameRecordedNothing() const
        {
            const auto& frame = Device.CommandContext;
            return frame.DispatchCalls == 0 && frame.CopyBufferRecords.empty() && frame.BoundPipelines.empty();
        }
        // A small scalar run: 768 class bytes of scratch (neighbors 320 -> 512, means 128, stats 112 -> 128).
        static constexpr std::uint64_t kScalarScratchBytes = 768u;
        bool RecordScalar(G::PointScalarWorkspace& workspace)
        {
            const bool float64 = std::exchange(Device.ShaderFloat64, true);
            const G::GpuPropertyView view{.Buffer = {1u, 1u}, .Address = 64u, .Bytes = 32u};
            const auto stats = workspace.Record(Recorded,
                {.Method = 1, .K = 4},
                {.Positions = view, .Output = view, .Nodes = 16u, .LiveSlots = 32u, .LiveCount = 8u});
            Device.ShaderFloat64 = float64;
            return stats.IsValid();
        }
        // A scalar workspace, recorded once: a new object creates its one pipeline, which its
        // destructor destroys, so Live() counts the scalar workspaces in existence.
        std::shared_ptr<G::PointScalarWorkspace> Lease()
        {
            auto lease = Cache.LeaseGpuWorkspace<G::PointScalarWorkspace>();
            if (lease)
                EXPECT_TRUE(RecordScalar(*lease));
            return lease;
        }
        int Live() const
        {
            return Device.CreatePipelineCount - Device.DestroyPipelineCount;
        }
        Extrinsic::Tests::MockCommandContext Recorded;
        // ARCH-023 worker pages: a pulled page completes on its own WaitCompute (no Frame, no CollectCompleted)
        // unless HoldWorker; Take copies the nearest answer once. HoldWorker's first wait models a discard during it
        // (AbortOnHold). Released lists given-up readbacks.
        bool HoldWorker{};
        std::atomic<bool>* AbortOnHold{};
        std::vector<Extrinsic::RHI::ReadbackToken> Released{};
        int Taken{};
        void UseWorkerLane()
        {
            using W = Extrinsic::RHI::ComputeWaitStatus;
            Device.TransferQueue.ComputeWait = [this](Extrinsic::RHI::ReadbackToken token, std::uint64_t)
            {
                if (!token.IsValid() || !HoldWorker)
                    return W::Complete;
                if (AbortOnHold)
                    AbortOnHold->store(true);
                return W::Pending;
            };
            Device.TransferQueue.TakeReadback = [this](Extrinsic::RHI::ReadbackToken token, std::span<std::byte> out)
            {
                const auto found = std::ranges::find_if(InFlight,
                    [&](const auto& f)
                    {
                        return f.Token == token;
                    });
                if (found == InFlight.end() || found->Bytes != out.size())
                    return false;
                const auto answer = NearestAnswer(out.size());
                std::memcpy(out.data(), answer.data(), out.size());
                std::erase(Device.TransferQueue.PendingReadbacks, token);
                InFlight.erase(found);
                ++Taken;
                return true;
            };
            Device.TransferQueue.ReleaseReadbackHook = [this](Extrinsic::RHI::ReadbackToken token)
            {
                Released.push_back(token);
            };
        }
        // One nearest page of `queries` over `target` on a fresh session (UseWorkerLane), then the session dropped and
        // drained: its page completed, so the cache lets it, and its pin on the target, go. False (reported) otherwise.
        bool PageOnce(const R::SpatialIndexHandle target,
            const std::span<const glm::vec3> queries,
            const Extrinsic::Graphics::PointQueryStrategy strategy = Extrinsic::Graphics::PointQueryStrategy::Lbvh)
        {
            std::string why;
            auto session = Cache.PrepareQuerySession(
                target, {.Strategy = strategy, .PageQueries = std::uint32_t(queries.size()), .Queries = queries}, why);
            std::atomic<bool> abort{false};
            R::SpatialNearestBatch page;
            const bool ready = session &&
                session->Query(0u, std::uint32_t(queries.size()), -1.f, nullptr, abort, page) ==
                    R::SpatialPageStatus::Ready;
            if (!ready)
                ADD_FAILURE() << (session ? page.Diagnostic : why);
            session.reset();
            (void)Jobs.Jobs().DrainGpuQueueCompletedTransfers();
            return ready;
        }
        // Index of the last `kind` event on `context` at or after `from`, or -1.
        static std::ptrdiff_t First(const Extrinsic::Tests::MockCommandContext& context,
            Extrinsic::Tests::MockCommandContext::EventKind kind,
            std::size_t from)
        {
            for (std::size_t i = from; i < context.Events.size(); ++i)
                if (context.Events[i] == kind)
                    return std::ptrdiff_t(i);
            return -1;
        }
    };
}

TEST_F(SpatialWorkspaceLeases, OverlappingLeasesNeverAliasAndOneRetiredWorkspacePerKindIsReused)
{
    auto first = Lease(), overlap = Lease();
    ASSERT_TRUE(first && overlap);
    EXPECT_NE(first.get(), overlap.get());
    EXPECT_EQ(Live(), 2);
    first.reset();
    overlap.reset();
    EXPECT_EQ(Live(), 2) << "returned, not destroyed";
    // Another kind is never served by them; and nothing is leased again before its frames retire.
    EXPECT_TRUE(Cache.LeaseGpuWorkspace<G::OutlierWorkspace>());
    auto early = Lease();
    EXPECT_EQ(Live(), 3);
    EXPECT_EQ(Cache.Stats().WorkspaceReuses, 0u);
    early.reset();
    RetireFrames(); // first/overlap mature: only one idle workspace is kept
    EXPECT_EQ(Live(), 2);
    RetireFrames(); // early matures and replaces it
    EXPECT_EQ(Live(), 1);
    auto warm = Lease();
    EXPECT_EQ(Live(), 1);
    EXPECT_EQ(Cache.Stats().WorkspaceReuses, 1u);
    auto concurrent = Lease();
    EXPECT_NE(concurrent.get(), warm.get());
    EXPECT_EQ(Live(), 2);
    EXPECT_EQ(Cache.Stats().WorkspaceLeases, 6u);
    EXPECT_EQ(Cache.Stats().WorkspaceReuses, 1u);
}

// ARCH-023: frames retiring do not prove that compute-lane work finished; a workspace returned while
// such work was pending is leased again only once the transfer queue's compute fence completed.
TEST_F(SpatialWorkspaceLeases, ReturnedWorkspaceWaitsForComputeFenceBeyondFrameRetirement)
{
    Device.TransferQueue.ComputeFenceToken = Extrinsic::RHI::ReadbackToken{(1ull << 63u) | 7u};
    Device.TransferQueue.ReadbacksComplete = false;
    Lease().reset();
    RetireFrames();
    RetireFrames();
    auto cold = Lease();
    EXPECT_EQ(Cache.Stats().WorkspaceReuses, 0u) << "its frames retired, but the compute lane has not";
    EXPECT_EQ(Live(), 2);
    cold.reset();
    Device.TransferQueue.ReadbacksComplete = true;
    RetireFrames();
    auto warm = Lease();
    EXPECT_EQ(Cache.Stats().WorkspaceReuses, 1u);
}

TEST_F(SpatialWorkspaceLeases, ShutdownReleasesRetainedWorkspacesAndDetachesHeldLeases)
{
    auto held = Lease();
    Lease().reset();
    EXPECT_EQ(Live(), 2);
    EXPECT_TRUE(Jobs.Jobs().HasGpuQueueWork()) << "a pooled workspace keeps the device-idle shutdown";
    Shutdown();
    EXPECT_EQ(Live(), 1);
    EXPECT_FALSE(Lease());
    held.reset();
    EXPECT_EQ(Live(), 0);
}

// A detached workspace still owns its scratch manager while the device outlives both.
TEST_F(SpatialWorkspaceLeases, HeldWorkspaceKeepsItsScratchOwnerAliveAfterCacheDestruction)
{
    Shutdown();
    auto cache = std::make_unique<R::SpatialIndexCache>();
    R::EngineSetup setup{Commands, Events, Jobs.Jobs(), Worlds, Services, [](R::FramePhase, R::RuntimeFrameHook) {}};
    ASSERT_TRUE(cache->OnRegister(setup));
    auto held = cache->LeaseGpuWorkspace<G::PointScalarWorkspace>();
    ASSERT_TRUE(held);
    ASSERT_TRUE(RecordScalar(*held));
    const auto destroyed = Device.DestroyBufferCount;
    R::RuntimeModuleShutdownContext shutdown{Commands, Events, Jobs.Jobs(), Worlds, Services};
    cache->OnShutdown(shutdown);
    cache.reset();
    EXPECT_EQ(Device.DestroyBufferCount, destroyed);
    held.reset();
    EXPECT_EQ(Device.DestroyBufferCount - destroyed, 3);
    EXPECT_EQ(Live(), 0);
}

// A refused recorder submits nothing and returns its workspace without an in-flight token.
TEST_F(SpatialWorkspaceLeases, RecorderFailureSubmitsNothingAndReturnsItsWorkspace)
{
    std::weak_ptr<G::PointScalarWorkspace> watched;
    std::shared_ptr<R::SpatialGpuResult> result;
    {
        auto lease = Lease();
        watched = lease;
        result = Cache.QueueGpuCompute(4u, [lease](auto& commands, const auto&) {
            commands.FillBuffer({}, 0, 4, 0); // recorded before the recorder fails
            return Extrinsic::RHI::BufferHandle{};
        });
    }
    EXPECT_EQ(result->State, R::SpatialQueryState::Failed);
    EXPECT_EQ(LaneSubmissions, 0);
    EXPECT_TRUE(watched.expired()) << "nothing was submitted, so nothing retains the recorder";
    EXPECT_EQ(Device.AsyncComputeContext.FillBufferCalls, 1);
    EXPECT_TRUE(FrameRecordedNothing());
    Cache.Prune(); // stamps the returned workspace, which matures once its frames retired
    RetireFrames();
    auto warm = Lease();
    EXPECT_EQ(Cache.Stats().WorkspaceReuses, 1u);
    EXPECT_EQ(Live(), 1);
    EXPECT_EQ(Cache.Stats().GpuComputeSubmissions, 0u);
}

TEST_F(SpatialWorkspaceLeases, RefusedSubmissionReturnsWorkspaceForReuse)
{
    Device.ComputeReadback = [this](auto record, auto, auto) {
        (void)record(Device.CommandContext);
        return Extrinsic::RHI::ReadbackToken{}; // refused after recording
    };
    std::weak_ptr<G::PointScalarWorkspace> watched;
    {
        auto lease = Lease();
        watched = lease;
        const auto output = Device.CreateBuffer({.SizeBytes = 16});
        const auto result = Cache.QueueGpuCompute(4u,
            [lease, output](auto&, const auto&)
            {
                return output;
            });
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
    EXPECT_EQ(Live(), 1);
    EXPECT_FALSE(Jobs.Jobs().HasGpuQueueWork());
    Shutdown();
    EXPECT_TRUE(watched.expired());
    EXPECT_EQ(Live(), 1);
}

TEST_F(SpatialWorkspaceLeases, CancelledRunDoesNotRecycleWorkspaceBeforeItsSubmissionCompletes)
{
    std::optional<Extrinsic::RHI::ReadbackSink> held;
    Device.ComputeReadback = [&](auto record, auto, auto sink) {
        EXPECT_TRUE(record(Device.CommandContext).IsValid());
        held = std::move(sink);
        return Extrinsic::RHI::ReadbackToken{7};
    };
    std::weak_ptr<G::PointScalarWorkspace> watched;
    {
        auto lease = Lease();
        watched = lease;
        const auto output = Device.CreateBuffer({.SizeBytes = 16});
        // The caller drops both its lease and the result, as a cancelled run does.
        (void)Cache.QueueGpuCompute(0u,
            [lease, output](auto&, const auto&)
            {
                return output;
            });
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

// ARCH-023: a returned workspace keeps its pipeline but holds no budgeted bytes once the owner
// thread parked it; its buffers wait for the shared retirement stamp and then serve the next lease.
TEST_F(SpatialWorkspaceLeases, ParkedWorkspaceGivesBackItsScratchButKeepsItsPipeline)
{
    EXPECT_EQ(Cache.Stats().ScratchBudgetBytes, std::uint64_t{1024} << 20u) << "the default budget without config";
    Lease().reset();
    EXPECT_EQ(Cache.Stats().ScratchActiveBytes, kScalarScratchBytes) << "returned, not yet parked by the owner";
    Cache.Prune();
    auto stats = Cache.Stats();
    EXPECT_EQ(stats.ScratchActiveBytes, 0u);
    EXPECT_EQ(stats.ScratchPendingBytes, kScalarScratchBytes);
    RetireFrames();
    stats = Cache.Stats();
    EXPECT_EQ(stats.ScratchPendingBytes, 0u);
    EXPECT_EQ(stats.ScratchIdleBytes, kScalarScratchBytes);
    const auto created = Device.CreateBufferCount;
    auto warm = Lease();
    stats = Cache.Stats();
    EXPECT_EQ(stats.WorkspaceReuses, 1u);
    EXPECT_EQ(Device.CreatePipelineCount, 1) << "the parked workspace kept its pipeline";
    EXPECT_EQ(Device.CreateBufferCount, created) << "its buffers came back from the cache";
    EXPECT_EQ(stats.ScratchHits, 3u);
    EXPECT_EQ(stats.ScratchActiveBytes, kScalarScratchBytes);
    EXPECT_EQ(stats.ScratchIdleBytes, 0u);
}

// Two workspace kinds share retired buffers of the same size class and memory kind.
TEST_F(SpatialWorkspaceLeases, ReturnedBuffersServeAnotherWorkspaceKind)
{
    Lease().reset();
    Cache.Prune();
    RetireFrames();
    const auto before = Cache.Stats();
    const auto created = Device.CreateBufferCount;
    auto normals = Cache.LeaseGpuWorkspace<G::PointNormalsWorkspace>();
    ASSERT_TRUE(normals);
    Device.ShaderFloat64 = true;
    const G::GpuPropertyView view{.Buffer = {1u, 1u}, .Address = 64u, .Bytes = 32u};
    ASSERT_TRUE(normals
            ->Record(Recorded,
                {.K = 4},
                {.Positions = view, .Output = view, .Nodes = 16u, .LiveSlots = 32u, .LiveCount = 8u})
            .IsValid());
    const auto after = Cache.Stats();
    // Neighbors (8 rows x 5 x 8 = 320 -> 512) reuse the scalar run's; the 32-byte stats (class 64) are new.
    EXPECT_EQ(after.ScratchHits - before.ScratchHits, 1u);
    EXPECT_EQ(after.ScratchMisses - before.ScratchMisses, 1u);
    EXPECT_EQ(Device.CreateBufferCount - created, 1);
}

// A run whose scratch would exceed the method budget fails with the byte counts and allocates nothing.
TEST_F(SpatialWorkspaceLeases, BudgetRefusalFailsTheComputationWithItsByteCounts)
{
    Cache.MethodScratch()->SetScratchBudget(512u); // below the scalar run's 768 bytes
    auto workspace = Cache.LeaseGpuWorkspace<G::PointScalarWorkspace>();
    ASSERT_TRUE(workspace);
    const auto created = Device.CreateBufferCount;
    const auto result = Cache.QueueGpuCompute(sizeof(G::PointScalarGpuStats),
        [this, workspace](auto&, const auto&)
        {
            return RecordScalar(*workspace) ? Extrinsic::RHI::BufferHandle{1u, 1u} : Extrinsic::RHI::BufferHandle{};
        });
    Frame();
    ASSERT_EQ(result->State, R::SpatialQueryState::Failed);
    EXPECT_NE(result->Diagnostic.find("budget exceeded"), std::string::npos) << result->Diagnostic;
    EXPECT_NE(result->Diagnostic.find("render.gpu_method_buffer_budget_megabytes"), std::string::npos);
    EXPECT_NE(result->Diagnostic.find("512-byte budget"), std::string::npos) << result->Diagnostic;
    EXPECT_EQ(Cache.Stats().ScratchRefusals, 1u);
    // The 512-byte neighbor class fits alone; the 128-byte means then does not: only that one allocation ran.
    EXPECT_EQ(Device.CreateBufferCount - created, 1);
    EXPECT_EQ(Cache.Stats().GpuComputeSubmissions, 0u);
}

// Shutdown drains every parked buffer after device idle; a lease held past it destroys its buffer directly.
TEST_F(SpatialWorkspaceLeases, ShutdownDrainsParkedScratchAndLaterReleasesDestroyDirectly)
{
    auto held = Lease();
    Lease().reset();
    Cache.Prune(); // the returned workspace's 768 bytes are pending
    const auto destroyed = Device.DestroyBufferCount;
    Shutdown();
    EXPECT_EQ(Device.DestroyBufferCount - destroyed, 3) << "the parked workspace's three buffers";
    EXPECT_EQ(Cache.MethodScratch(), nullptr);
    held.reset();
    EXPECT_EQ(Device.DestroyBufferCount - destroyed, 6);
}

// Without shader float64 a transformed session is refused before it allocates or writes anything; a non-finite pose
// fails its page before submission, so only the rows written at prepare count as uploaded.
TEST_F(SpatialWorkspaceLeases, TransformedQueriesRefuseUnsupportedAndNonfiniteTransforms)
{
    const std::vector<glm::vec3> points{{0,0,0}, {1,0,0}};
    const auto target = Cache.CreateWorkspace(points);
    const R::SpatialQuerySessionDesc desc{.Capacity = 1u, .PageQueries = 2u, .Queries = points, .Transformed = true};
    std::string why;
    EXPECT_FALSE(Cache.PrepareQuerySession(target.Handle, desc, why));
    EXPECT_NE(why.find("float64"), std::string::npos) << why;
    EXPECT_EQ(Cache.Stats().GpuQueryUploadBytes, 0u);
    Device.ShaderFloat64 = true;
    const auto session = Cache.PrepareQuerySession(target.Handle, desc, why);
    ASSERT_TRUE(session) << why;
    glm::dmat4 pose{1.0};
    pose[3].x = std::numeric_limits<double>::quiet_NaN();
    std::atomic<bool> abort{false};
    R::SpatialNearestBatch page;
    EXPECT_EQ(session->Query(0u, 2u, -1.f, &pose, abort, page), R::SpatialPageStatus::Failed);
    EXPECT_NE(page.Diagnostic.find("finite"), std::string::npos) << page.Diagnostic;
    EXPECT_EQ(session->Pages(), 0u);
    EXPECT_EQ(Cache.Stats().GpuQueryUploadBytes, points.size() * 12u) << "the rows only: no pose was uploaded";
}

// The one brute-force work budget shared by the recorder, the cache, ICP and the bilateral filter: each limit
// admits exactly its boundary and refuses one unit beyond it.
TEST(SpatialIndexCache, BruteForceAdmissionSharesOneEngineeringBudget)
{
    namespace G = Extrinsic::Graphics;
    static_assert(G::PointBruteForceDispatchWork == (1u << 24u) && G::PointBruteForceInvocationWork == (1u << 20u));
    EXPECT_TRUE(G::PointBruteForceRefusal(1u << 12u, 1u << 11u, 1u).empty()); // 2^12 x 2^11 x 2 = 2^24
    EXPECT_NE(G::PointBruteForceRefusal((1u << 12u) + 1u, 1u << 11u, 1u).find("2^24"), std::string_view::npos);
    EXPECT_TRUE(G::PointBruteForceRefusal(1u, 1u << 19u, 1u).empty()); // serial 2^20
    EXPECT_NE(G::PointBruteForceRefusal(1u, (1u << 19u) + 1u, 1u).find("2^20"), std::string_view::npos);
    // Retained radius slots dominate the serial scan: 1024 slots admit at most 1023 targets.
    EXPECT_TRUE(G::PointBruteForceRefusal(1u, 1024u, 1023u).empty());
    EXPECT_FALSE(G::PointBruteForceRefusal(1u, 1024u, 1024u).empty());
    EXPECT_TRUE(G::PointBruteForceRefusal(1u, 1023u, 1024u).empty());
    EXPECT_TRUE(G::PointBruteForceRefusal(1u << 4u, 1u << 20u, 0u).empty()); // capacity 0 counts only
    EXPECT_FALSE(G::PointBruteForceRefusal(~0u, ~0u, ~0u).empty()); // no 64-bit overflow
    EXPECT_TRUE(G::PointBruteForceRefusal(0u, 1u << 19u, 1u).empty());
}

namespace
{
    // Writes of exactly these points' bytes (query inputs differ in size).
    std::ptrdiff_t WritesOf(const Extrinsic::Tests::MockDevice& device, std::span<const glm::vec3> points)
    {
        const auto bytes = std::as_bytes(points);
        return std::ranges::count_if(device.BufferWrites,
            [&](const auto& w)
            {
                return std::ranges::equal(w.Data, bytes);
            });
    }
}

// A brute-force session's admission takes its page against the target; LBVH admits the same request. The pages at
// both boundaries record and complete without a hierarchy.
TEST_F(SpatialWorkspaceLeases, BruteForceAdmissionRefusesOverBudgetSessionsAndRecordsAtTheLimit)
{
    using S = Extrinsic::Graphics::PointQueryStrategy;
    UseWorkerLane();
    std::vector<glm::vec3> targets(1024);
    for (std::size_t i = 0; i < targets.size(); ++i)
        targets[i] = {float(i), 0, 0};
    const auto target = Cache.CreateWorkspace(targets);
    ASSERT_TRUE(target.Ready());
    const std::vector<glm::vec3> one{{0.5f, 0, 0}};
    std::string why;
    const auto radius = [&](const S strategy, const std::uint32_t capacity)
    {
        return Cache.PrepareQuerySession(target.Handle,
            {.Strategy = strategy, .Radius = true, .Capacity = capacity, .PageQueries = 1u, .Queries = one},
            why);
    };
    // Radius with 1024 retained slots: serial 1024 x 1025 > 2^20 is refused; the same request is admitted by LBVH.
    EXPECT_FALSE(radius(S::BruteForce, 1024u));
    EXPECT_NE(why.find("2^20"), std::string::npos) << why;
    EXPECT_TRUE(radius(S::Lbvh, 1024u)) << why;
    // Dispatch work: a page of 8192 queries x 1024 targets x (1 + 1) = 2^24 is the boundary.
    std::vector<glm::vec3> queries(8192, glm::vec3(0.5f, 0, 0));
    const auto nearest = [&]
    {
        return Cache.PrepareQuerySession(target.Handle,
            {.Strategy = S::BruteForce, .PageQueries = std::uint32_t(queries.size()), .Queries = queries},
            why);
    };
    const auto atLimit = nearest();
    ASSERT_TRUE(atLimit) << why;
    queries.push_back({0.5f, 0, 0});
    EXPECT_FALSE(nearest());
    EXPECT_NE(why.find("2^24"), std::string::npos) << why;
    const auto lastRadius = radius(S::BruteForce, 1023u);
    ASSERT_TRUE(lastRadius) << "1024 x 1024 = 2^20 serial: " << why;
    std::atomic<bool> abort{false};
    R::SpatialNearestBatch page;
    EXPECT_EQ(lastRadius->Query(0u, 1u, -1.f, nullptr, abort, page), R::SpatialPageStatus::Failed);
    EXPECT_NE(page.Diagnostic.find("radius"), std::string::npos) << page.Diagnostic;
    EXPECT_EQ(lastRadius->Pages(), 0u) << "a radius outside [0, 1e18] is refused before it submits";
    // The recorder accepts both boundary pages without a hierarchy.
    EXPECT_EQ(atLimit->Query(0u, 8192u, -1.f, nullptr, abort, page), R::SpatialPageStatus::Ready) << page.Diagnostic;
    EXPECT_EQ(lastRadius->Query(0u, 1u, 1.f, nullptr, abort, page), R::SpatialPageStatus::Ready) << page.Diagnostic;
    EXPECT_EQ(Cache.Stats().GpuBuilds, 1u) << "only the LBVH radius session's preparation built";
    EXPECT_EQ(Bound(Device.AsyncComputeContext, "lbvh_build"), 1);
}

TEST_F(SpatialWorkspaceLeases, BruteForceRecorderSharesTheAdmissionBoundary)
{
    namespace G = Extrinsic::Graphics;
    G::PointLbvhWorkspace workspace(Device);
    const auto points = Device.CreateBuffer({.SizeBytes = 1024u * 12u});
    const auto query = Device.CreateBuffer({.SizeBytes = 12u}), neighbors = Device.CreateBuffer({.SizeBytes = 8192u}),
               headers = Device.CreateBuffer({.SizeBytes = 8u});
    G::PointLbvhQuery radius{.Queries = {.Buffer = query, .Count = 1},
        .Neighbors = neighbors,
        .Headers = headers,
        .Capacity = 1023,
        .Radius = 1};
    EXPECT_TRUE(workspace.RecordBruteForceQuery(Device.CommandContext, {.Buffer = points, .Count = 1024}, {}, radius));
    radius.Capacity = 1024;
    EXPECT_FALSE(workspace.RecordBruteForceQuery(Device.CommandContext, {.Buffer = points, .Count = 1024}, {}, radius));
    EXPECT_EQ(Device.CommandContext.DispatchCalls, 1);
    EXPECT_EQ(workspace.BuildCount(), 0u);
    EXPECT_EQ(workspace.AllocationCount(), 0u);
}

// LBVH build -> UpdateWorkspace -> brute force (points rewritten once, no build) -> LBVH (rebuilt over the new
// points, no second rewrite) -> repeated queries on the same revision reuse both without writes or builds.
TEST_F(SpatialWorkspaceLeases, MixedStrategiesRebuildOnlyTheLbvhAfterAnUpdate)
{
    using S = Extrinsic::Graphics::PointQueryStrategy;
    UseWorkerLane();
    const std::vector<glm::vec3> before{{0, 0, 0}, {1, 0, 0}, {2, 0, 0}}, after{{5, 0, 0}, {6, 0, 0}, {7, 0, 0}};
    const std::vector<glm::vec3> queries{{0.5f, 0, 0}};
    auto workspace = Cache.CreateWorkspace(before);
    ASSERT_TRUE(workspace.Ready());
    ASSERT_TRUE(PageOnce(workspace.Handle, queries, S::Lbvh));
    EXPECT_EQ(Cache.Stats().GpuBuilds, 1u);
    EXPECT_EQ(WritesOf(Device, before), 1);
    ASSERT_TRUE(Cache.UpdateWorkspace(workspace, after));
    ASSERT_TRUE(PageOnce(workspace.Handle, queries, S::BruteForce));
    EXPECT_EQ(Cache.Stats().GpuBuilds, 1u) << "brute force reads the rewritten points without a build";
    EXPECT_EQ(WritesOf(Device, after), 1) << "the update reached the device points";
    EXPECT_EQ(Bound(Device.AsyncComputeContext, "point_brute_force_query"), 1);
    ASSERT_TRUE(PageOnce(workspace.Handle, queries, S::Lbvh));
    EXPECT_EQ(Cache.Stats().GpuBuilds, 2u) << "the stale hierarchy is rebuilt after the brute-force query";
    EXPECT_EQ(WritesOf(Device, after), 1) << "the rebuild reads the points brute force already wrote";
    ASSERT_TRUE(PageOnce(workspace.Handle, queries, S::BruteForce));
    ASSERT_TRUE(PageOnce(workspace.Handle, queries, S::Lbvh));
    EXPECT_EQ(Cache.Stats().GpuBuilds, 2u) << "same revision: no rebuild";
    EXPECT_EQ(WritesOf(Device, before), 1) << "same revision: no rewrite";
    EXPECT_EQ(WritesOf(Device, after), 1) << "same revision: no rewrite";
    EXPECT_EQ(Bound(Device.AsyncComputeContext, "lbvh_build"), 2);
    EXPECT_TRUE(FrameRecordedNothing());
}

// A computation never records against a hierarchy UpdateWorkspace made stale (even after a brute-force query
// rewrote its points): its own submission rebuilds first, before the method recorder runs.
TEST_F(SpatialWorkspaceLeases, ComputeRebuildsAStaleHierarchyInItsOwnSubmission)
{
    using S = Extrinsic::Graphics::PointQueryStrategy;
    UseWorkerLane();
    const std::vector<glm::vec3> before{{0, 0, 0}, {1, 0, 0}, {2, 0, 0}}, after{{5, 0, 0}, {6, 0, 0}, {7, 0, 0}};
    const std::vector<glm::vec3> queries{{0.5f, 0, 0}};
    auto workspace = Cache.CreateWorkspace(before);
    ASSERT_TRUE(workspace.Ready());
    ASSERT_TRUE(PageOnce(workspace.Handle, queries, S::Lbvh));
    ASSERT_TRUE(Cache.UpdateWorkspace(workspace, after));
    ASSERT_TRUE(PageOnce(workspace.Handle, queries, S::BruteForce));
    ASSERT_EQ(Cache.Stats().GpuBuilds, 1u);
    std::ptrdiff_t buildsRecorded = 0;
    const auto output = Device.CreateBuffer({.SizeBytes = 16});
    const auto result = Cache.QueueGpuCompute(workspace.Handle,
        4u,
        [&](auto&, const R::SpatialGpuIndexView& view)
        {
            buildsRecorded = Bound(Device.AsyncComputeContext, "lbvh_build");
            EXPECT_NE(view.NodesBDA, 0u);
            return output;
        });
    EXPECT_EQ(result->State, R::SpatialQueryState::Submitted) << result->Diagnostic;
    EXPECT_EQ(buildsRecorded, 2) << "the rebuild precedes the method recorder in the same submission";
    EXPECT_EQ(Cache.Stats().GpuBuilds, 2u);
    EXPECT_TRUE(FrameRecordedNothing());
}

namespace
{
    // `total` hits per query in the packed readback: queries x capacity rows {index 1, distance 0}, then headers.
    std::vector<std::uint32_t> Packed(std::size_t queries, std::uint32_t capacity, std::uint32_t total)
    {
        std::vector<std::uint32_t> words(queries * capacity * 2u + queries * 2u, 0u);
        for (std::size_t i = 0; i < queries * capacity; ++i)
            words[2u * i] = 1u;
        for (std::size_t i = 0; i < queries; ++i)
            words[queries * capacity * 2u + 2u * i] = total;
        return words;
    }
}

// ARCH-023 B4: a computation on an index that was never built, without any latency choice (formerly framed by
// default), and every untransformed session shape on both strategies (a preparation and a page each) submit at once
// through IDevice::SubmitComputeReadback; the frame's command context records nothing. The missing build is recorded
// into the computation's own submission before its recorder, and LBVH builds once for all later work.
TEST_F(SpatialWorkspaceLeases, FirstUnbuiltTargetAndEveryStrategySubmitThroughTheComputeLane)
{
    using S = Extrinsic::Graphics::PointQueryStrategy;
    UseWorkerLane();
    std::vector<std::uint32_t> answer; // the next page's packed words, served after the fixture's bookkeeping
    Device.TransferQueue.TakeReadback = [&answer, take = Device.TransferQueue.TakeReadback](
                                            const Extrinsic::RHI::ReadbackToken token, const std::span<std::byte> out)
    {
        if (!take(token, out) || out.size() != answer.size() * 4u)
            return false;
        std::memcpy(out.data(), answer.data(), out.size());
        return true;
    };
    const auto target = Cache.CreateWorkspace(std::vector<glm::vec3>{{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {3, 0, 0}});
    ASSERT_TRUE(target.Ready());
    std::uint64_t nodes = 0;
    std::ptrdiff_t buildsBeforeRecorder = -1;
    const auto output = Device.CreateBuffer({.SizeBytes = 16});
    const auto compute = Cache.QueueGpuCompute(target.Handle,
        4u,
        [&](auto&, const R::SpatialGpuIndexView& view)
        {
            nodes = view.NodesBDA;
            buildsBeforeRecorder = Bound(Device.AsyncComputeContext, "lbvh_build");
            return output;
        });
    EXPECT_EQ(compute->State, R::SpatialQueryState::Submitted) << compute->Diagnostic;
    EXPECT_EQ(LaneSubmissions, 1);
    EXPECT_NE(nodes, 0u);
    EXPECT_EQ(buildsBeforeRecorder, 1);
    EXPECT_EQ(Cache.Stats().GpuBuilds, 1u);

    const std::vector<glm::vec3> queries{{0.4f, 0, 0}, {2.6f, 0, 0}};
    const std::vector<std::uint32_t> excluded{0u, Geometry::PointLBVH::InvalidIndex};
    struct Shape
    {
        bool Radius;
        std::uint32_t Capacity, Total;
        std::span<const std::uint32_t> Excluded;
    };
    const Shape shapes[]{{false, 1u, 1u, {}}, {false, 2u, 2u, excluded}, {true, 3u, 2u, excluded}};
    std::atomic<bool> abort{false};
    for (const S strategy : {S::Lbvh, S::BruteForce})
        for (const auto& shape : shapes)
        {
            SCOPED_TRACE(::testing::Message() << "brute force " << (strategy == S::BruteForce) << " radius "
                                              << shape.Radius << " capacity " << shape.Capacity);
            std::string why;
            const auto session = Cache.PrepareQuerySession(target.Handle,
                {.Strategy = strategy,
                    .Radius = shape.Radius,
                    .Capacity = shape.Capacity,
                    .PageQueries = 2u,
                    .Queries = queries,
                    .Excluded = shape.Excluded},
                why);
            ASSERT_TRUE(session) << why;
            answer = Packed(queries.size(), shape.Capacity, shape.Total);
            R::SpatialNearestBatch page;
            ASSERT_EQ(
                session->Query(0u, 2u, shape.Radius ? 1.f : -1.f, nullptr, abort, page), R::SpatialPageStatus::Ready)
                << page.Diagnostic;
            EXPECT_EQ(session->ReadbackBytes(), answer.size() * 4u) << "one packed readback: rows, then headers";
            EXPECT_EQ(page.Counts, (std::vector<std::uint32_t>{shape.Total, shape.Total}));
        }
    EXPECT_EQ(LaneSubmissions, 13) << "the computation, then a preparation and a page per shape";
    EXPECT_EQ(Cache.Stats().GpuQuerySubmissions, 6u);
    EXPECT_EQ(Cache.Stats().GpuBuilds, 1u) << "LBVH sessions reuse the accepted build; brute force builds none";
    EXPECT_EQ(Bound(Device.AsyncComputeContext, "lbvh_query"), 3);
    EXPECT_EQ(Bound(Device.AsyncComputeContext, "point_brute_force_query"), 3);
    Frame(); // delivers the computation (the pages were pulled)
    EXPECT_EQ(compute->State, R::SpatialQueryState::Ready);
    EXPECT_TRUE(FrameRecordedNothing());
}

// A submission refused after recording (or whose recorder failed after the gather and build were recorded)
// leaves the entry's producer state unset and publishes nothing, even if its sink were delivered late; the next
// new request records the gather and build again, and only its accepted submission counts the build. A query
// session's preparation is such a submission: refused, it leaves no session.
TEST_F(SpatialWorkspaceLeases, RefusedRecordingLeavesTheProducerStaleAndNeverPublishes)
{
    UseWorkerLane();
    Device.TransferQueue.AcceptBufferUploads = true;
    const auto entity = Worlds.Get(World())->Create();
    auto& props = Worlds.Get(World())->Raw().emplace<GS::Vertices>(entity).Properties;
    props.Resize(4);
    props.GetOrAdd<glm::vec3>("samples").Vector() = {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {3, 0, 0}};
    props.GetOrAdd<bool>("v:deleted")[1] = true; // compacted: the gather path
    const auto index = Cache.Acquire(World(), entity, Ref(D::PointCloudPoint));
    ASSERT_TRUE(index.Ready()) << index.Diagnostic;
    const auto& lane = Device.AsyncComputeContext;
    const auto output = Device.CreateBuffer({.SizeBytes = 16});
    int recorded = 0;
    const auto recorder = [&](auto&, const R::SpatialGpuIndexView& view)
    {
        ++recorded;
        EXPECT_NE(view.NodesBDA, 0u);
        return output;
    };

    // Partial record failure: gather and build recorded, then the consumer fails.
    const auto partial = Cache.QueueGpuCompute(index.Handle,
        4u,
        [&](auto&, const auto&)
        {
            return Extrinsic::RHI::BufferHandle{};
        });
    EXPECT_EQ(partial->State, R::SpatialQueryState::Failed);
    EXPECT_EQ(lane.CopyBufferRecords.size(), 2u) << "two contiguous runs of live rows were gathered";
    EXPECT_EQ(Bound(lane, "lbvh_build"), 1);
    // Refused after recording.
    RefuseAfterRecording = true;
    const auto refused = Cache.QueueGpuCompute(index.Handle, 4u, recorder);
    EXPECT_EQ(refused->State, R::SpatialQueryState::Failed);
    EXPECT_NE(refused->Diagnostic.find("after recording"), std::string::npos) << refused->Diagnostic;
    const std::vector<glm::vec3> queries{{0.5f, 0, 0}};
    std::string why;
    EXPECT_FALSE(Cache.PrepareQuerySession(index.Handle, {.PageQueries = 1u, .Queries = queries}, why));
    EXPECT_NE(why.find("after recording"), std::string::npos) << why;
    EXPECT_EQ(recorded, 1) << "a refused recorder is never replayed";
    EXPECT_EQ(lane.CopyBufferRecords.size(), 6u) << "each refused attempt re-recorded the gather";
    EXPECT_EQ(Bound(lane, "lbvh_build"), 3);
    EXPECT_EQ(Cache.Stats().GpuBuilds, 0u);
    EXPECT_EQ(Cache.Stats().GpuComputeSubmissions + Cache.Stats().GpuQuerySubmissions, 0u);
    ASSERT_EQ(Refused.size(), 2u);
    for (auto& late : Refused)
    {
        const auto answer = NearestAnswer(late.Bytes);
        late.Sink.Deliver(std::as_bytes(std::span(answer)));
    }
    EXPECT_EQ(refused->State, R::SpatialQueryState::Failed);
    EXPECT_EQ(refused->Data, std::vector<std::byte>(4u)) << "a refused submission publishes no bytes";

    // A later new request records gather and build again; once accepted they are reused.
    RefuseAfterRecording = false;
    const auto accepted = Cache.QueueGpuCompute(index.Handle, 4u, recorder);
    EXPECT_EQ(accepted->State, R::SpatialQueryState::Submitted) << accepted->Diagnostic;
    EXPECT_EQ(lane.CopyBufferRecords.size(), 8u);
    EXPECT_EQ(Bound(lane, "lbvh_build"), 4);
    EXPECT_EQ(Cache.Stats().GpuBuilds, 1u);
    EXPECT_TRUE(PageOnce(index.Handle, queries));
    EXPECT_EQ(lane.CopyBufferRecords.size(), 8u) << "the accepted gather is not repeated";
    EXPECT_EQ(Bound(lane, "lbvh_build"), 4) << "nor the accepted build";
    Frame();
    EXPECT_EQ(accepted->State, R::SpatialQueryState::Ready);
    EXPECT_TRUE(FrameRecordedNothing());
}

// The canonical slot an index reads and a ring slot a recorder acquired stay protected by the submission's own
// compute token: replacing the canonical revision, many retired graphics frames and preview pressure neither
// release nor reuse them. Another submission completing does not either; only that token does.
TEST_F(SpatialWorkspaceLeases, ComputeTokenGuardsCanonicalAndRingSlotsBeyondGraphicsFrames)
{
    namespace G = Extrinsic::Graphics;
    Device.TransferQueue.AcceptBufferUploads = true;
    Hold = true;
    const auto entity = Worlds.Get(World())->Create();
    auto& props = Worlds.Get(World())->Raw().emplace<GS::Vertices>(entity).Properties;
    props.Resize(3);
    auto positions = props.GetOrAdd<glm::vec3>("samples");
    positions.Vector() = {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}};
    auto* residency = Cache.PropertyResidency();
    ASSERT_NE(residency, nullptr);
    // A key of the live entity, so the cache's Prune keeps the ring.
    const auto ring = R::MakeGpuPropertyKey(World(), entity, Ref(D::PointCloudPoint, "preview"));
    const G::GpuPropertyLayout layout{.Count = 3};
    const auto output = Device.CreateBuffer({.SizeBytes = 16});
    Extrinsic::RHI::BufferHandle written{};
    auto first = Cache.QueueGpuCompute(Cache.Acquire(World(), entity, Ref(D::PointCloudPoint)).Handle,
        4u,
        [&](auto&, const auto&)
        {
            written = residency->AcquireBack(ring, layout, 2u)->Buffer; // acquired while recording
            return output;
        });
    ASSERT_EQ(first->State, R::SpatialQueryState::Submitted) << first->Diagnostic;
    ASSERT_TRUE(written.IsValid());
    ASSERT_TRUE(residency->Publish(ring));
    const auto releases = residency->Stats().Releases;

    // A new revision: the next index uploads it and replaces the canonical slot the first submission reads.
    positions[0] = {9, 0, 0};
    Cache.Prune();
    auto second = Cache.QueueGpuCompute(Cache.Acquire(World(), entity, Ref(D::PointCloudPoint)).Handle,
        4u,
        [&](auto&, const auto&)
        {
            return output;
        });
    ASSERT_EQ(second->State, R::SpatialQueryState::Submitted) << second->Diagnostic;
    ASSERT_EQ(InFlight.size(), 2u);
    Deliver(1); // the second submission finished; the first has not
    for (int i = 0; i < 8; ++i)
    {
        RetireFrames();
        Frame();
    }
    EXPECT_EQ(residency->Stats().Releases, releases) << "the replaced canonical slot is still read on the device";
    // Preview pressure: the other ring slot becomes front; the first one stays unavailable, so the preview drops.
    ASSERT_TRUE(residency->AcquireBack(ring, layout, 2u));
    ASSERT_TRUE(residency->Publish(ring));
    const auto dropped = residency->Stats().DroppedPreviews;
    EXPECT_FALSE(residency->AcquireBack(ring, layout, 2u)) << "its writer has not completed";
    EXPECT_EQ(residency->Stats().DroppedPreviews, dropped + 1u);

    Deliver(0);
    Frame();
    Cache.Prune();
    EXPECT_EQ(residency->Stats().Releases, releases + 1u);
    const auto reused = residency->AcquireBack(ring, layout, 2u);
    ASSERT_TRUE(reused);
    EXPECT_EQ(reused->Buffer, written);
    EXPECT_EQ(residency->Stats().Readbacks, 0u) << "a compute token is a safety completion, not a transfer";
}

// A dropped (cancelled) session whose abandoned page is still on the device stays owned by the cache until that page
// completed: the workspace refuses updates meanwhile and the queue keeps reporting work. Shutdown after device idle
// fails a computation still pending; its late sink publishes nothing. (Held sessions at shutdown:
// ShutdownStopsHeldSessionsAndALaterDropReleasesNoDeviceObject.)
TEST_F(SpatialWorkspaceLeases, CancelledAndShutdownWorkWaitForTheDevice)
{
    UseWorkerLane();
    const std::vector<glm::vec3> points{{0, 0, 0}, {1, 0, 0}};
    auto workspace = Cache.CreateWorkspace(points);
    std::string why;
    auto session = Cache.PrepareQuerySession(workspace.Handle, {.PageQueries = 2u, .Queries = points}, why);
    ASSERT_TRUE(session) << why;
    std::atomic<bool> abort{false};
    HoldWorker = true;
    AbortOnHold = &abort; // the run is discarded during the page's wait
    R::SpatialNearestBatch page;
    ASSERT_EQ(session->Query(0u, 2u, -1.f, nullptr, abort, page), R::SpatialPageStatus::Abandoned);
    session.reset(); // the run drops it at once
    (void)Jobs.Jobs().DrainGpuQueueCompletedTransfers();
    RetireFrames();
    EXPECT_TRUE(Jobs.Jobs().HasGpuQueueWork());
    EXPECT_FALSE(Cache.UpdateWorkspace(workspace, points)) << "the device may still read its points";
    Device.TransferQueue.PendingReadbacks.clear(); // the device finished the page
    (void)Jobs.Jobs().DrainGpuQueueCompletedTransfers();
    EXPECT_FALSE(Jobs.Jobs().HasGpuQueueWork());
    EXPECT_TRUE(Cache.UpdateWorkspace(workspace, points));

    Hold = true;
    const auto computation = Cache.QueueGpuCompute(workspace.Handle,
        4u,
        [output = Device.CreateBuffer({.SizeBytes = 16})](auto&, const auto&)
        {
            return output;
        });
    ASSERT_EQ(computation->State, R::SpatialQueryState::Submitted) << computation->Diagnostic;
    Shutdown();
    EXPECT_EQ(computation->State, R::SpatialQueryState::Failed);
    EXPECT_NE(computation->Diagnostic.find("stopped"), std::string::npos);
    Deliver(InFlight.size() - 1u); // the computation's sink, arriving after shutdown, publishes nothing
    EXPECT_EQ(computation->State, R::SpatialQueryState::Failed);
}

TEST(SpatialIndexCache, PopulatedCloudExcludesDeletedRowsFromSpatialQueries)
{
    R::WorldRegistry worlds;
    const auto world = worlds.CreateWorld("populated-cloud");
    auto& scene = *worlds.Get(world);
    const auto entity = scene.Create();
    Geometry::PointCloud::Cloud cloud;
    (void)cloud.AddPoint({0, 0, 0});
    const auto deleted = cloud.AddPoint({1, 0, 0});
    (void)cloud.AddPoint({4, 0, 0});
    cloud.DeletePoint(deleted);
    GS::PopulateFromCloud(scene.Raw(), entity, cloud);
    R::SpatialIndexCache cache(worlds);

    const auto acquired = cache.Acquire(world, entity, Ref(D::PointCloudPoint, "v:position"));
    ASSERT_TRUE(acquired.Ready()) << acquired.Diagnostic;
    const auto nearest = cache.Nearest(acquired.Handle, {1, 0, 0});
    ASSERT_TRUE(nearest);
    EXPECT_EQ(nearest->Index, 0u);
    const auto neighbors = cache.KNearest(acquired.Handle, {1, 0, 0}, 8);
    ASSERT_TRUE(neighbors);
    ASSERT_EQ(neighbors->size(), 2u);
    EXPECT_EQ((*neighbors)[0].Index, 0u);
    EXPECT_EQ((*neighbors)[1].Index, 2u);
}

// ARCH-023 worker query sessions (root worker-query review). Predictions written before any run: each assertion below.
// A moving session pages three passes of changed rows with no Frame() and no collection: each pass is one mapped
// write into the prepared staging and one compute-lane copy recorded before that page's hierarchy build, no transfer
// queue upload, no buffer created after prepare. Mutations: UploadBuffer for the rows (BufferUploads grows, the
// MappedWrites count stays 0); the copy recorded after the build (copy index > dispatch index); a staging lease per
// upload (CreateBufferCount grows); waiting on CollectCompleted (Query never Ready: the fixture never collects).
TEST_F(SpatialWorkspaceLeases, AMovingQuerySessionPagesChangedRowsOnTheComputeLaneWithoutFrames)
{
    using K = Extrinsic::Tests::MockCommandContext::EventKind;
    UseWorkerLane();
    std::vector<glm::vec3> rows{{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {3, 0, 0}};
    std::string why;
    const auto session = Cache.PrepareQuerySession({},
        {.Strategy = Extrinsic::Graphics::PointQueryStrategy::Lbvh,
            .Capacity = 1u,
            .PageQueries = 2u,
            .Queries = rows,
            .Moving = true},
        why);
    ASSERT_TRUE(session) << why;
    EXPECT_EQ(LaneSubmissions, 0) << "a moving target needs no owner preparation submission";
    const auto buffers = Device.CreateBufferCount;
    const auto uploads = Device.TransferQueue.BufferUploads.size();
    std::atomic<bool> abort{false};
    R::SpatialNearestBatch page;
    for (int pass = 0; pass < 3; ++pass)
    {
        for (auto& point : rows)
            point.y += 0.25f;
        ASSERT_TRUE(session->UploadQueries(rows, why)) << why;
        ASSERT_FALSE(Device.MappedWrites.empty());
        EXPECT_EQ(Device.MappedWrites.back().Data.size(), rows.size() * 12u);
        EXPECT_EQ(std::memcmp(Device.MappedWrites.back().Data.data(), rows.data(), rows.size() * 12u), 0) << pass;
        const auto copies = Device.AsyncComputeContext.CopyBufferRecords.size();
        const auto events = Device.AsyncComputeContext.Events.size();
        const auto builds = Bound(Device.AsyncComputeContext, "lbvh_build");
        ASSERT_EQ(session->Query(0u, 2u, -1.f, nullptr, abort, page), R::SpatialPageStatus::Ready) << page.Diagnostic;
        EXPECT_EQ(Device.AsyncComputeContext.CopyBufferRecords.size(), copies + 1u) << "the rows' copy, once a pass";
        const auto copy = First(Device.AsyncComputeContext, K::CopyBuffer, events);
        const auto dispatch = First(Device.AsyncComputeContext, K::Dispatch, events);
        EXPECT_TRUE(copy >= 0 && dispatch > copy) << "the page copies its rows before the build reads them";
        EXPECT_EQ(Bound(Device.AsyncComputeContext, "lbvh_build"), builds + 1) << "rebuilt over the new rows";
        EXPECT_EQ(page.Counts, (std::vector<std::uint32_t>{1u, 1u}));
        EXPECT_EQ(page.Neighbors.front().Index, 1u) << "identity row IDs";
        ASSERT_EQ(session->Query(2u, 2u, -1.f, nullptr, abort, page), R::SpatialPageStatus::Ready) << page.Diagnostic;
        EXPECT_EQ(Device.AsyncComputeContext.CopyBufferRecords.size(), copies + 1u) << "a later page copies nothing";
        EXPECT_EQ(Bound(Device.AsyncComputeContext, "lbvh_build"), builds + 1) << "and builds nothing";
    }
    EXPECT_EQ(session->Pages(), 6u);
    EXPECT_EQ(Taken, 6);
    EXPECT_EQ(Device.MappedWrites.size(), 3u);
    EXPECT_EQ(Device.TransferQueue.BufferUploads.size(), uploads) << "no graphics-lane upload";
    EXPECT_EQ(Device.CreateBufferCount, buffers) << "every buffer was prepared once";
    EXPECT_EQ(session->UploadBytes(), 4u * rows.size() * 12u) << "the first rows and three passes";
    EXPECT_EQ(session->ReadbackBytes(), 6u * 2u * 16u);
    EXPECT_TRUE(Released.empty());
}

// The ICP shape: a fixed private target, one source uploaded at prepare, a pose per page written in-command (32
// FillBuffer words) with the hierarchy built once by the owner's preparation; while the session lives the target
// refuses UpdateWorkspace. The cache's query traffic counts the rows once and one pose per page, every page one
// submission and one packed readback. Mutations: a host write of the pose (MappedWrites grows); a build per page
// (lbvh_build bound more than once); no session guard in UpdateWorkspace (it succeeds while pages read the hierarchy).
TEST_F(SpatialWorkspaceLeases, AFixedTargetSessionTransformsEachPageInCommandAndPinsItsTarget)
{
    UseWorkerLane();
    Device.ShaderFloat64 = true;
    auto target = Cache.CreateWorkspace(std::vector<glm::vec3>{{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {3, 0, 0}});
    ASSERT_TRUE(target.Ready());
    const std::vector<glm::vec3> source{{0.1f, 0, 0}, {2.9f, 0, 0}};
    std::string why;
    auto session = Cache.PrepareQuerySession(
        target.Handle, {.Capacity = 1u, .PageQueries = 2u, .Queries = source, .Transformed = true}, why);
    ASSERT_TRUE(session) << why;
    EXPECT_EQ(LaneSubmissions, 1) << "the owner's preparation: resident samples and the hierarchy";
    EXPECT_EQ(Bound(Device.AsyncComputeContext, "lbvh_build"), 1);
    EXPECT_EQ(Cache.Stats().GpuQueryUploadBytes, source.size() * 12u) << "the rows, once";
    const auto fills = Device.AsyncComputeContext.FillBufferCalls;
    std::atomic<bool> abort{false};
    R::SpatialNearestBatch page;
    for (int step = 0; step < 3; ++step)
    {
        glm::dmat4 pose(1.0);
        pose[3][0] = 0.1 * step;
        ASSERT_EQ(session->Query(0u, 2u, -1.f, &pose, abort, page), R::SpatialPageStatus::Ready) << page.Diagnostic;
        EXPECT_EQ(page.Counts, (std::vector<std::uint32_t>{1u, 1u}));
        const auto pages = std::uint64_t(step) + 1u;
        EXPECT_EQ(Cache.Stats().GpuQueryUploadBytes, source.size() * 12u + pages * sizeof(glm::dmat4))
            << "and then only each page's pose";
        EXPECT_EQ(Cache.Stats().GpuQueryTransformUploads, pages);
        EXPECT_EQ(Cache.Stats().GpuQuerySubmissions, pages) << "every page, including the first, used the lane";
        EXPECT_EQ(session->ReadbackBytes(), pages * source.size() * 16u) << "one packed readback: neighbors, headers";
    }
    EXPECT_EQ(Device.AsyncComputeContext.FillBufferCalls, fills + 3 * 32);
    EXPECT_TRUE(Device.MappedWrites.empty()) << "no host write per page";
    EXPECT_EQ(Bound(Device.AsyncComputeContext, "lbvh_build"), 1) << "built once";
    EXPECT_EQ(Bound(Device.AsyncComputeContext, "lbvh_transform_queries"), 3);
    EXPECT_EQ(session->Query(0u, 2u, -1.f, nullptr, abort, page), R::SpatialPageStatus::Failed) << "a pose is required";
    EXPECT_FALSE(Cache.UpdateWorkspace(target, std::vector<glm::vec3>(4u, glm::vec3{5.f})))
        << "pages read the target's hierarchy";
    session.reset();
    (void)Jobs.Jobs().DrainGpuQueueCompletedTransfers(); // its pages completed: the cache lets it go
    EXPECT_TRUE(Cache.UpdateWorkspace(target, std::vector<glm::vec3>(4u, glm::vec3{5.f})));
}

// Cancellation and retirement: a discard during a page's wait abandons it (released, never taken); the staging is not
// rewritten while that page may still run; a dropped session keeps its storage until its last page completed, then
// returns it. Bad inputs fail without device work. Mutations: Upload without the completion check (succeeds while
// held); retiring on use_count alone (scratch freed while the page is pending).
TEST_F(SpatialWorkspaceLeases, AnAbandonedPageIsReleasedAndItsStorageWaitsForTheDevice)
{
    UseWorkerLane();
    const std::vector<glm::vec3> rows{{0, 0, 0}, {1, 0, 0}, {2, 0, 0}};
    std::string why;
    EXPECT_FALSE(Cache.PrepareQuerySession(
        {}, {.Capacity = 1u, .PageQueries = 2u, .Queries = rows, .Transformed = true, .Moving = true}, why))
        << "a pose needs a fixed target";
    EXPECT_FALSE(
        Cache.PrepareQuerySession({}, {.Capacity = 65u, .PageQueries = 2u, .Queries = rows, .Moving = true}, why));
    EXPECT_FALSE(
        Cache.PrepareQuerySession({}, {.Capacity = 1u, .PageQueries = 0u, .Queries = rows, .Moving = true}, why));
    EXPECT_FALSE(Cache.PrepareQuerySession({12345u}, {.Capacity = 1u, .PageQueries = 2u, .Queries = rows}, why))
        << "no such index";
    const auto active = Cache.Stats().ScratchActiveBytes;
    auto session = Cache.PrepareQuerySession({},
        {.Strategy = Extrinsic::Graphics::PointQueryStrategy::BruteForce,
            .Capacity = 1u,
            .PageQueries = 3u,
            .Queries = rows,
            .Moving = true},
        why);
    ASSERT_TRUE(session) << why;
    EXPECT_GT(Cache.Stats().ScratchActiveBytes, active);
    std::atomic<bool> abort{false};
    R::SpatialNearestBatch page;
    EXPECT_EQ(session->Query(2u, 2u, -1.f, nullptr, abort, page), R::SpatialPageStatus::Failed) << "past the rows";
    EXPECT_EQ(session->Query(0u, 4u, -1.f, nullptr, abort, page), R::SpatialPageStatus::Failed) << "past the page";
    EXPECT_FALSE(session->UploadQueries(std::vector<glm::vec3>(2u), why)) << "another row count";
    EXPECT_EQ(session->Pages(), 0u);
    Device.AcceptMappedWrites = false;
    EXPECT_FALSE(session->UploadQueries(rows, why)) << "a backend without a mapping reports it";
    Device.AcceptMappedWrites = true;

    HoldWorker = true;
    AbortOnHold = &abort;
    EXPECT_EQ(session->Query(0u, 3u, -1.f, nullptr, abort, page), R::SpatialPageStatus::Abandoned);
    ASSERT_EQ(Released.size(), 1u) << "the abandoned page's bytes are given up";
    EXPECT_EQ(Taken, 0);
    EXPECT_FALSE(session->UploadQueries(rows, why)) << "the staging is not rewritten while the page may still run";
    EXPECT_EQ(Device.MappedWrites.size(), 0u);
    EXPECT_EQ(session->Query(0u, 3u, -1.f, nullptr, abort, page), R::SpatialPageStatus::Abandoned)
        << "an aborted run submits nothing more";
    EXPECT_EQ(session->Pages(), 1u);

    session.reset();
    (void)Jobs.Jobs().DrainGpuQueueCompletedTransfers();
    const auto held = Cache.Stats().ScratchActiveBytes;
    EXPECT_GT(held, active) << "kept while its abandoned page is on the device";
    Device.TransferQueue.PendingReadbacks.clear(); // the device finished it
    (void)Jobs.Jobs().DrainGpuQueueCompletedTransfers();
    EXPECT_EQ(Cache.Stats().ScratchActiveBytes, active) << "returned to the method scratch once complete";
    EXPECT_EQ(Cache.Stats().GpuQuerySubmissions, 1u) << "its page folded once";
}

// ARCH-023 (pending integration review item 1): a fixed target only GPU queries read builds no CPU hierarchy, on its
// first capture. A fresh GPU-only entry (a deleted row, so its rows are a gather of the canonical slot) serves both
// worker strategies with zero CPU builds; a later CPU acquisition of the same current entry builds once, keeps the
// handle, rows and the earlier GPU-only snapshot lease, and answers CPU queries in original row IDs; acquiring again
// builds nothing. Predictions, before any run: Builds 0, 0, then 1, 1; GpuBuilds 1 (LBVH only); Nearest nullopt then
// row 2. Mutations: an unconditional Index.Build in Acquire (Builds 1 after the GPU-only capture); an upgrade that
// rebuilds on every CPU acquisition (Builds 2); an upgrade mutating the shared snapshot in place (the held GPU-only
// lease reports Cpu); Matches against Index.Points() alone (a GPU-only capture mismatches its own rows).
TEST_F(SpatialWorkspaceLeases, AGpuOnlyFixedTargetBuildsNoCpuHierarchyUntilACpuCaptureAsksOnce)
{
    using S = Extrinsic::Graphics::PointQueryStrategy;
    UseWorkerLane();
    Device.TransferQueue.AcceptBufferUploads = true; // the fixed property enters canonical residency
    const auto world = World();
    auto& scene = *Worlds.Get(world);
    const auto entity = scene.Create();
    auto& props = scene.Raw().emplace<GS::Vertices>(entity).Properties;
    props.Resize(4);
    props.GetOrAdd<glm::vec3>("samples").Vector() = {{9, 9, 9}, {1, 0, 0}, {2, 0, 0}, {3, 0, 0}};
    props.GetOrAdd<bool>("v:deleted")[0] = true;
    const auto ref = Ref(D::PointCloudPoint);

    const auto gpu = Cache.Acquire(world, entity, ref, R::SpatialIndexSpace::Property, false);
    ASSERT_TRUE(gpu.Ready()) << gpu.Diagnostic;
    EXPECT_FALSE(gpu.Reused);
    EXPECT_EQ(Cache.Stats().Builds, 0u) << "a GPU-only capture builds no CPU hierarchy";
    const auto gpuOnly = Cache.Snapshot(gpu.Handle);
    ASSERT_TRUE(gpuOnly);
    EXPECT_FALSE(gpuOnly->Cpu);
    EXPECT_EQ(gpuOnly->Slots, (std::vector<std::uint32_t>{1u, 2u, 3u}));
    const std::vector<glm::vec3> live{{1, 0, 0}, {2, 0, 0}, {3, 0, 0}};
    EXPECT_TRUE(R::SpatialIndexSnapshotMatches(gpuOnly.get(), gpuOnly->Slots, live))
        << "rows match without a hierarchy";
    EXPECT_FALSE(Cache.Nearest(gpu.Handle, {2.1f, 0, 0})) << "no CPU query on a GPU-only entry";

    const std::vector<glm::vec3> queries{{0.9f, 0, 0}, {2.1f, 0, 0}};
    std::atomic<bool> abort{false};
    for (const S strategy : {S::BruteForce, S::Lbvh})
    {
        std::string why;
        const auto session = Cache.PrepareQuerySession(
            gpu.Handle, {.Strategy = strategy, .Capacity = 1u, .PageQueries = 2u, .Queries = queries}, why);
        ASSERT_TRUE(session) << why;
        R::SpatialNearestBatch page;
        ASSERT_EQ(session->Query(0u, 2u, -1.f, nullptr, abort, page), R::SpatialPageStatus::Ready) << page.Diagnostic;
        EXPECT_EQ(page.Counts, (std::vector<std::uint32_t>{1u, 1u}));
        EXPECT_EQ(page.Neighbors.front().Index, 1u) << "original row IDs";
    }
    EXPECT_EQ(Cache.Stats().Builds, 0u) << "neither strategy built a CPU hierarchy";
    EXPECT_EQ(Cache.Stats().GpuBuilds, 1u) << "the LBVH session's device build only";

    const auto cpu = Cache.Acquire(world, entity, ref, R::SpatialIndexSpace::Property, true);
    ASSERT_TRUE(cpu.Ready()) << cpu.Diagnostic;
    EXPECT_TRUE(cpu.Reused);
    EXPECT_EQ(cpu.Handle.Value, gpu.Handle.Value) << "the same entry, rows and revision key";
    EXPECT_EQ(Cache.Stats().Builds, 1u) << "the first CPU user builds once";
    const auto cpuSnapshot = Cache.Snapshot(cpu.Handle);
    ASSERT_TRUE(cpuSnapshot);
    EXPECT_TRUE(cpuSnapshot->Cpu);
    EXPECT_EQ(cpuSnapshot->Slots, gpuOnly->Slots);
    EXPECT_TRUE(std::ranges::equal(cpuSnapshot->Points(), live));
    const auto nearest = Cache.Nearest(cpu.Handle, {2.1f, 0, 0});
    ASSERT_TRUE(nearest);
    EXPECT_EQ(nearest->Index, 2u);
    EXPECT_FALSE(gpuOnly->Cpu) << "the earlier lease is unchanged";
    EXPECT_TRUE(std::ranges::equal(gpuOnly->Points(), live));
    EXPECT_TRUE(Cache.Acquire(world, entity, ref, R::SpatialIndexSpace::Property, true).Reused);
    EXPECT_TRUE(Cache.Acquire(world, entity, ref, R::SpatialIndexSpace::Property, false).Reused);
    EXPECT_EQ(Cache.Stats().Builds, 1u) << "built once";
}

// ARCH-023 query-review fixes. Predictions written before any run: each assertion below.
namespace
{
    // A fresh property-space entry over four points with row 0 deleted: its first GPU use uploads the canonical slot
    // (a graphics-lane UploadBuffer) and gathers the live rows.
    auto FreshPropertyEntity(R::WorldRegistry& worlds, const R::WorldHandle world)
    {
        auto& scene = *worlds.Get(world);
        const auto entity = scene.Create();
        auto& props = scene.Raw().emplace<GS::Vertices>(entity).Properties;
        props.Resize(4);
        props.GetOrAdd<glm::vec3>("samples").Vector() = {{9, 9, 9}, {1, 0, 0}, {2, 0, 0}, {3, 0, 0}};
        props.GetOrAdd<bool>("v:deleted")[0] = true;
        return entity;
    }
}

// B1: host preparation (the canonical slot's upload, buffers, hierarchy storage and pipelines) runs before
// IDevice::SubmitComputeReadback on every cache path, never inside the recorder (where a graphics-lane recorder holds
// the lane lock UploadBuffer takes). Mutation: PrepareHost (or Reserve) called from the recorder (violations > 0).
TEST_F(SpatialWorkspaceLeases, HostPreparationPrecedesRecordingOnEveryCachePath)
{
    using S = Extrinsic::Graphics::PointQueryStrategy;
    UseWorkerLane();
    Device.TransferQueue.AcceptBufferUploads = true;
    int violations = 0, recorded = 0;
    Device.ComputeReadback = [this, &violations, &recorded, lane = Device.ComputeReadback](
                                 auto record, std::uint64_t bytes, Extrinsic::RHI::ReadbackSink sink)
    {
        return lane(
            [&, record](Extrinsic::RHI::ICommandContext& commands)
            {
                const auto uploads = Device.TransferQueue.BufferUploads.size();
                const auto buffers = Device.CreateBufferCount;
                const auto pipelines = Device.CreatePipelineCount;
                const auto output = record(commands);
                ++recorded;
                violations += uploads != Device.TransferQueue.BufferUploads.size() ||
                    buffers != Device.CreateBufferCount || pipelines != Device.CreatePipelineCount;
                return output;
            },
            bytes,
            std::move(sink));
    };
    const std::vector<glm::vec3> queries{{0.9f, 0, 0}, {2.1f, 0, 0}};
    const auto fresh = [&]
    {
        const auto entity = FreshPropertyEntity(Worlds, World());
        const auto acquired = Cache.Acquire(World(), entity, Ref(D::PointCloudPoint));
        EXPECT_TRUE(acquired.Ready()) << acquired.Diagnostic;
        return acquired.Handle;
    };
    const auto uploaded = [&](const std::size_t before)
    {
        return Device.TransferQueue.BufferUploads.size() > before;
    };
    std::size_t uploads = Device.TransferQueue.BufferUploads.size();
    const auto output = Device.CreateBuffer({.SizeBytes = 16});
    const auto computation = Cache.QueueGpuCompute(fresh(),
        4u,
        [output](auto&, const auto&)
        {
            return output;
        });
    EXPECT_EQ(computation->State, R::SpatialQueryState::Submitted) << computation->Diagnostic;
    EXPECT_TRUE(uploaded(uploads)) << "the computation's canonical slot was uploaded";
    uploads = Device.TransferQueue.BufferUploads.size();
    for (const S strategy : {S::Lbvh, S::BruteForce})
    {
        std::string why;
        const auto session = Cache.PrepareQuerySession(
            fresh(), {.Strategy = strategy, .Capacity = 1u, .PageQueries = 2u, .Queries = queries}, why);
        ASSERT_TRUE(session) << why;
        EXPECT_TRUE(uploaded(uploads)) << "the session preparation's canonical slot was uploaded";
        uploads = Device.TransferQueue.BufferUploads.size();
        std::atomic<bool> abort{false};
        R::SpatialNearestBatch page;
        EXPECT_EQ(session->Query(0u, 2u, -1.f, nullptr, abort, page), R::SpatialPageStatus::Ready) << page.Diagnostic;
    }
    EXPECT_EQ(recorded, 5) << "a computation, two preparations and two pages";
    EXPECT_EQ(violations, 0) << "nothing uploaded, created or compiled while recording";
}

// B2: owner submissions (computations, a session's preparation) wait for every transfer issued before them; a
// prepared session's pages follow the compute lane alone. A fixed target's preparation submits even when the entry
// needs nothing recorded, so its pages always follow an AfterIssuedTransfers submission; a moving target (host rows
// only) needs none. Mutations: Query without LaneOrderOnly (a page reports AfterIssuedTransfers); preparation skipped
// for a built entry (LaneSubmissions unchanged, the first page then unordered after the canonical upload).
TEST_F(SpatialWorkspaceLeases, OwnerSubmissionsWaitForIssuedTransfersAndPreparedPagesFollowTheLane)
{
    using O = Extrinsic::RHI::ComputeInputOrder;
    UseWorkerLane();
    Device.TransferQueue.AcceptBufferUploads = true;
    const auto entity = FreshPropertyEntity(Worlds, World());
    const auto handle = Cache.Acquire(World(), entity, Ref(D::PointCloudPoint)).Handle;
    const std::vector<glm::vec3> queries{{0.9f, 0, 0}, {2.1f, 0, 0}};
    const auto output = Device.CreateBuffer({.SizeBytes = 16});
    const auto recorder = [output](auto&, const auto&)
    {
        return output;
    };
    const auto build = Cache.QueueGpuCompute(handle, 4u, recorder); // gathers and builds the entry
    ASSERT_EQ(build->State, R::SpatialQueryState::Submitted) << build->Diagnostic;
    Frame();
    ASSERT_EQ(build->State, R::SpatialQueryState::Ready) << build->Diagnostic;
    const int built = LaneSubmissions;
    std::string why;
    const auto fixed = Cache.PrepareQuerySession(handle, {.Capacity = 1u, .PageQueries = 1u, .Queries = queries}, why);
    ASSERT_TRUE(fixed) << why;
    EXPECT_EQ(LaneSubmissions, built + 1) << "the built entry's preparation still submits once";
    std::atomic<bool> abort{false};
    R::SpatialNearestBatch page;
    ASSERT_EQ(fixed->Query(0u, 1u, -1.f, nullptr, abort, page), R::SpatialPageStatus::Ready) << page.Diagnostic;
    ASSERT_EQ(fixed->Query(1u, 1u, -1.f, nullptr, abort, page), R::SpatialPageStatus::Ready) << page.Diagnostic;
    const auto moving =
        Cache.PrepareQuerySession({}, {.Capacity = 1u, .PageQueries = 2u, .Queries = queries, .Moving = true}, why);
    ASSERT_TRUE(moving) << why;
    ASSERT_EQ(moving->Query(0u, 2u, -1.f, nullptr, abort, page), R::SpatialPageStatus::Ready) << page.Diagnostic;
    EXPECT_EQ(Cache.QueueGpuCompute(4u, recorder)->State, R::SpatialQueryState::Submitted);
    EXPECT_EQ(Device.ComputeInputOrders,
        (std::vector<O>{O::AfterIssuedTransfers,
            O::AfterIssuedTransfers,
            O::LaneOrderOnly,
            O::LaneOrderOnly,
            O::LaneOrderOnly,
            O::AfterIssuedTransfers}))
        << "a computation, a preparation, two fixed pages, a moving page, a computation";
}

// N5: a run still holding sessions at cache shutdown (after device idle). Shutdown releases their device objects on
// the owner (the fixed target's entry and device workspace, the moving target's private hierarchy, every scratch
// lease); the run's later drop destroys nothing, and pages and uploads after shutdown fail as stopped. Mutation:
// ShutdownBatches only clearing Sessions (dropping them later destroys buffers and pipelines on the dropping thread).
TEST_F(SpatialWorkspaceLeases, ShutdownStopsHeldSessionsAndALaterDropReleasesNoDeviceObject)
{
    UseWorkerLane();
    Device.TransferQueue.AcceptBufferUploads = true;
    const auto entity = FreshPropertyEntity(Worlds, World());
    const auto handle = Cache.Acquire(World(), entity, Ref(D::PointCloudPoint)).Handle;
    std::vector<glm::vec3> queries{{0.9f, 0, 0}, {2.1f, 0, 0}};
    std::string why;
    auto fixed = Cache.PrepareQuerySession(handle, {.Capacity = 1u, .PageQueries = 2u, .Queries = queries}, why);
    ASSERT_TRUE(fixed) << why;
    auto moving =
        Cache.PrepareQuerySession({}, {.Capacity = 1u, .PageQueries = 2u, .Queries = queries, .Moving = true}, why);
    ASSERT_TRUE(moving) << why;
    std::atomic<bool> abort{false};
    R::SpatialNearestBatch page;
    ASSERT_EQ(fixed->Query(0u, 2u, -1.f, nullptr, abort, page), R::SpatialPageStatus::Ready) << page.Diagnostic;
    ASSERT_EQ(moving->Query(0u, 2u, -1.f, nullptr, abort, page), R::SpatialPageStatus::Ready) << page.Diagnostic;
    const auto buffersBefore = Device.DestroyBufferCount;
    Shutdown();
    const auto buffers = Device.DestroyBufferCount;
    const auto pipelines = Device.DestroyPipelineCount;
    EXPECT_GT(buffers, buffersBefore) << "the sessions' device objects went at shutdown";
    EXPECT_EQ(fixed->Query(0u, 2u, -1.f, nullptr, abort, page), R::SpatialPageStatus::Failed);
    EXPECT_NE(page.Diagnostic.find("stopped"), std::string::npos) << page.Diagnostic;
    EXPECT_FALSE(moving->UploadQueries(queries, why));
    EXPECT_NE(why.find("stopped"), std::string::npos) << why;
    fixed.reset();
    moving.reset();
    EXPECT_EQ(Device.DestroyBufferCount, buffers) << "the run's late drop destroys no buffer";
    EXPECT_EQ(Device.DestroyPipelineCount, pipelines) << "nor a pipeline";
}

// N4: IndexDeviceBytes reports device bytes only sessions or index leases hold: a moving session's private hierarchy,
// and a fixed target's entry after the cache evicted it while a session or lease still reads it (once, however many
// holders). Mutations: counting cache entries only (the moving bytes are 0; the evicted entry's bytes vanish while
// still allocated); not counting leases (the lease-only bytes read 0); no deduplication (double counts).
TEST_F(SpatialWorkspaceLeases, IndexBytesCountSessionHeldHierarchiesAndEvictedTargets)
{
    UseWorkerLane();
    Device.TransferQueue.AcceptBufferUploads = true;
    EXPECT_EQ(Cache.Stats().IndexDeviceBytes, 0u);
    const std::vector<glm::vec3> rows{{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {3, 0, 0}};
    std::string why;
    auto moving =
        Cache.PrepareQuerySession({}, {.Capacity = 1u, .PageQueries = 2u, .Queries = rows, .Moving = true}, why);
    ASSERT_TRUE(moving) << why;
    const auto own = Cache.Stats().IndexDeviceBytes;
    EXPECT_GT(own, 0u) << "the moving session's private hierarchy";
    const auto entity = FreshPropertyEntity(Worlds, World());
    const auto handle = Cache.Acquire(World(), entity, Ref(D::PointCloudPoint)).Handle;
    auto first = Cache.PrepareQuerySession(handle, {.Capacity = 1u, .PageQueries = 2u, .Queries = rows}, why);
    auto second = Cache.PrepareQuerySession(handle, {.Capacity = 1u, .PageQueries = 2u, .Queries = rows}, why);
    ASSERT_TRUE(first && second) << why;
    const auto both = Cache.Stats().IndexDeviceBytes;
    EXPECT_GT(both, own) << "the cached entry";
    auto index = Cache.PrepareGpuIndex(handle, why);
    ASSERT_TRUE(index) << why;
    EXPECT_EQ(Cache.Stats().IndexDeviceBytes, both) << "a lease on the cached entry adds nothing";
    const auto evictions = Cache.Stats().Evictions;
    auto positions = Worlds.Get(World())->Raw().get<GS::Vertices>(entity).Properties.GetOrAdd<glm::vec3>("samples");
    positions[1] = {5, 0, 0};
    Cache.Prune();
    ASSERT_EQ(Cache.Stats().Evictions, evictions + 1u) << "the revision change evicts the entry";
    EXPECT_EQ(Cache.Stats().IndexDeviceBytes, both) << "its buffers stay allocated for the holders, counted once";
    first.reset();
    second.reset();
    moving.reset();
    (void)Jobs.Jobs().DrainGpuQueueCompletedTransfers();
    EXPECT_EQ(Cache.Stats().IndexDeviceBytes, both - own) << "the evicted entry, held by the index lease alone";
    index.reset();
    (void)Jobs.Jobs().DrainGpuQueueCompletedTransfers();
    EXPECT_EQ(Cache.Stats().IndexDeviceBytes, 0u) << "released once the sessions and the lease went";
}

// ARCH-023: a run still holding a GPU index lease whose entry the cache evicted (the lease alone holds it). The
// participant's shutdown (BeginShutdown: run workers may still read the view until CancelAndDrain) keeps the lease
// intact; the module's final OnShutdown releases the entry's device objects on the owner and empties the view and
// barriers; the run's later drop destroys nothing. Mutations: ShutdownBatches clearing IndexLeases with no release
// (the late drop destroys the entry's buffers); releasing at participant shutdown (the view is empty while a worker
// may still read it).
TEST_F(SpatialWorkspaceLeases, ShutdownReleasesHeldIndexLeasesAndALaterDropReleasesNoDeviceObject)
{
    UseWorkerLane();
    Device.TransferQueue.AcceptBufferUploads = true;
    const auto entity = FreshPropertyEntity(Worlds, World());
    const auto handle = Cache.Acquire(World(), entity, Ref(D::PointCloudPoint)).Handle;
    std::string why;
    auto index = Cache.PrepareGpuIndex(handle, why);
    ASSERT_TRUE(index) << why;
    const std::vector<Extrinsic::RHI::BufferHandle> held(index->Barriers().begin(), index->Barriers().end());
    ASSERT_FALSE(held.empty());
    const auto destroyed = [&]
    {
        return std::ranges::count_if(held,
            [&](const auto buffer)
            {
                return std::ranges::find(Device.DestroyedBuffers, buffer) != Device.DestroyedBuffers.end();
            });
    };
    ASSERT_NE(index->View().PositionsBDA, 0u);
    const auto evictions = Cache.Stats().Evictions;
    auto positions = Worlds.Get(World())->Raw().get<GS::Vertices>(entity).Properties.GetOrAdd<glm::vec3>("samples");
    positions[1] = {5, 0, 0};
    Cache.Prune();
    ASSERT_EQ(Cache.Stats().Evictions, evictions + 1u) << "the revision change evicts the entry";
    EXPECT_EQ(destroyed(), 0) << "eviction keeps the leased entry's buffers";
    (void)Jobs.Jobs().ShutdownGpuQueueParticipants();
    EXPECT_EQ(destroyed(), 0) << "participant shutdown frees no leased buffer";
    EXPECT_EQ(index->Barriers().size(), held.size()) << "nor empties a view a worker may still read";
    EXPECT_NE(index->View().PositionsBDA, 0u);
    Shutdown();
    const auto buffers = Device.DestroyBufferCount;
    const auto pipelines = Device.DestroyPipelineCount;
    EXPECT_GT(destroyed(), 0) << "the leased entry's device buffers went at module shutdown, on this thread";
    EXPECT_TRUE(index->Barriers().empty());
    EXPECT_EQ(index->View().PositionsBDA, 0u);
    EXPECT_EQ(index->View().NodesBDA, 0u);
    EXPECT_EQ(Cache.Stats().IndexDeviceBytes, 0u);
    index.reset();
    EXPECT_EQ(Device.DestroyBufferCount, buffers) << "the run's late drop destroys no buffer";
    EXPECT_EQ(Device.DestroyPipelineCount, pipelines) << "nor a pipeline";
}
