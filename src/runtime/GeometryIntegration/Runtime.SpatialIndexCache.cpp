module;
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <format>
#include <functional>
#include <glm/gtc/quaternion.hpp>
#include <cstdint>
#include <entt/entity/entity.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
module Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.WorldRegistry;
import Extrinsic.Graphics.PointLBVH;
import Extrinsic.Graphics.GpuPropertyResidency;
import Extrinsic.Runtime.GpuPropertyBinding;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.RHI.CommandContext;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Types;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.JobService;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.RHI.TransferQueue;
import Extrinsic.RHI.BufferManager;
import Extrinsic.Core.Config.Render;

#include "Services/Runtime.ComputePageWait.hpp"

namespace Extrinsic::Runtime
{
    bool SpatialIndexSnapshotMatches(const SpatialIndexSnapshot* snapshot,
        std::span<const std::uint32_t> slots, std::span<const glm::vec3> points) noexcept
    {
        return snapshot && std::ranges::equal(snapshot->Slots, slots) && std::ranges::equal(points, snapshot->Points());
    }

    namespace
    {
        static_assert(sizeof(Geometry::PointLBVH::Neighbor) == 8);
        glm::mat4 EntityMatrix(WorldRegistry* worlds, WorldHandle world, entt::entity entity)
        {
            glm::mat4 matrix(1.f);
            const auto* scene = worlds ? worlds->Get(world) : nullptr;
            if (scene && scene->IsValid(entity))
                if (const auto* transform = scene->Raw().try_get<ECS::Components::Transform::Component>(entity))
                    matrix = ECS::Components::Transform::GetMatrix(*transform);
            return matrix;
        }
        std::uint32_t CompactSlot(std::span<const std::uint32_t> slots, std::uint32_t slot)
        {
            const auto it = std::ranges::lower_bound(slots, slot);
            return it != slots.end() && *it == slot ? std::uint32_t(it - slots.begin())
                                                    : Geometry::PointLBVH::InvalidIndex;
        }
        std::uint32_t CompactSlot(const SpatialIndexSnapshot& snapshot, std::uint32_t slot)
        {
            return CompactSlot(std::span<const std::uint32_t>(snapshot.Slots), slot);
        }
        // The packed result of one session page (neighbors, then a {count, status} header per query) into
        // `state` (Neighbors and Counts sized), validated against the target's ordered original row IDs: nearest
        // and kNN rows hold min(capacity, available) hits, radius rows at most the available (all hits counted),
        // status 0 everywhere. `excluded`: empty or one original row ID per query.
        bool DecodePacked(std::span<const std::byte> data,
            std::span<const std::uint32_t> slots,
            std::span<const std::uint32_t> excluded,
            std::uint32_t capacity,
            float radius,
            std::vector<std::uint32_t>& headers,
            SpatialNearestBatch& state)
        {
            const auto neighborBytes = state.Neighbors.size() * sizeof(Geometry::PointLBVH::Neighbor);
            headers.resize(state.Counts.size() * 2u);
            if (data.size() != neighborBytes + headers.size() * sizeof(std::uint32_t))
            {
                state.State = SpatialQueryState::Failed;
                state.Diagnostic = "GPU spatial query returned an incomplete result.";
                return false;
            }
            std::memcpy(state.Neighbors.data(), data.data(), neighborBytes);
            std::memcpy(headers.data(), data.data() + neighborBytes, data.size() - neighborBytes);
            bool valid = true;
            for (std::size_t i = 0; i < state.Counts.size(); ++i)
            {
                const auto available = slots.size() -
                    (!excluded.empty() && CompactSlot(slots, excluded[i]) != Geometry::PointLBVH::InvalidIndex);
                const auto expected = std::min(std::size_t(capacity), available);
                valid &=
                    headers[2 * i + 1] == 0 && (radius < 0 ? headers[2 * i] == expected : headers[2 * i] <= available);
                state.Counts[i] = headers[2 * i];
            }
            state.State = valid ? SpatialQueryState::Ready : SpatialQueryState::Failed;
            if (!valid)
                state.Diagnostic = "GPU spatial query reported invalid input.";
            return valid;
        }
        struct Source
        {
            Geometry::ConstProperty<glm::vec3> Points{};
            Geometry::ConstProperty<bool> Deleted{};
            std::uint32_t DeletionDivisor{1};
        };
        std::optional<Source> Resolve(WorldRegistry* worlds, WorldHandle world, entt::entity entity,
                                      const GeometryPropertyRef& ref)
        {
            const auto* scene = worlds ? worlds->Get(world) : nullptr;
            if (!scene || !scene->IsValid(entity) ||
                ref.ValueKind != Geometry::PropertyValueKind::Vec3)
                return {};
            const auto available = BuildGeometryAvailability(scene->Raw(), entity);
            const auto* properties = ResolveGeometryPropertySet(available, ref.Domain);
            if (!properties ||
                !ResolveGeometryProperty(available, ref, properties->Size(), false).Resolved())
                return {};
            Source result{.Points = properties->Get<glm::vec3>(ref.Name)};
            std::string_view deleted = "v:deleted";
            if (ref.Domain == GeometryElementDomain::MeshFace)
                deleted = "f:deleted";
            if (ref.Domain == GeometryElementDomain::MeshEdge ||
                ref.Domain == GeometryElementDomain::GraphEdge)
                deleted = "e:deleted";
            if (ref.Domain == GeometryElementDomain::MeshHalfedge ||
                ref.Domain == GeometryElementDomain::GraphHalfedge)
            {
                if (!available.SourceView.EdgeSource)
                    return {};
                properties = &available.SourceView.EdgeSource->Properties;
                deleted = "e:deleted";
                result.DeletionDivisor = 2;
            }
            result.Deleted = properties->Get<bool>(deleted);
            if (result.Deleted &&
                result.Deleted.Size() * result.DeletionDivisor != result.Points.Size())
                return {};
            return result;
        }
        // LeaseGpuWorkspace storage. Returned objects arrive from any thread; the owner thread
        // releases their scratch and stamps them, and they become leasable once that stamp retired.
        struct WorkspacePool
        {
            struct Slot
            {
                const void* Kind;
                std::shared_ptr<void> Object{};
                void (*ReleaseScratch)(void*){};
                RHI::RetirementStamp Stamp{};
            };
            std::mutex Mutex{};
            std::vector<Slot> Returned{}; // guarded by Mutex
            std::vector<Slot> Waiting{}, Idle{};
        };
        // The control block behind one lease: the last copy returns the object to its pool.
        struct WorkspaceLease
        {
            // A detached workspace may outlive the cache; destroy its buffers before their manager.
            std::shared_ptr<RHI::BufferManager> Scratch;
            WorkspacePool::Slot Slot;
            std::weak_ptr<WorkspacePool> Pool{};
            WorkspaceLease(WorkspacePool::Slot slot,
                std::weak_ptr<WorkspacePool> pool,
                std::shared_ptr<RHI::BufferManager> scratch)
                : Scratch(std::move(scratch)), Slot(std::move(slot)), Pool(std::move(pool))
            {
            }
            WorkspaceLease(const WorkspaceLease&) = delete;
            WorkspaceLease& operator=(const WorkspaceLease&) = delete;
            ~WorkspaceLease()
            {
                if (const auto pool = Pool.lock(); pool && Slot.Object)
                {
                    std::scoped_lock lock{pool->Mutex};
                    pool->Returned.push_back(std::move(Slot));
                }
            }
        };
    } // namespace
    extern "C++"
    {
        // A prepared worker query session (ARCH-023). Written by PrepareQuerySession on the owner before the run's worker
        // can see it; the worker then reads the immutable part and writes only NeedsBuild, the scratch vectors and the
        // atomics. The owner reads TargetKey, Resident, Stamped and the atomics, and Stop()s it at shutdown.
        struct SpatialQuerySession::Impl
        {
            // Held by each page and upload (the one worker); Stop takes it. Stopped is guarded by it.
            std::mutex Paging{};
            bool Stopped{};
            RHI::IDevice* Device{};
            RHI::ITransferQueue* Transfer{};
            std::shared_ptr<RHI::BufferManager> Scratch{}; // outlives the leases below
            std::shared_ptr<const void> Target{}; // a fixed target's cache entry, with its device buffers
            const void* TargetKey{};
            std::unique_ptr<Graphics::PointLbvhWorkspace> Own{}; // a moving target's (brute force: its pipeline only)
            Graphics::PointLbvhWorkspace* Workspace{}; // the entry's, or Own
            std::shared_ptr<const SpatialIndexSnapshot> Snapshot{}; // a fixed target's original row IDs
            std::vector<std::uint32_t> Identity{}; // a moving target's
            Graphics::PointQueryStrategy Strategy{};
            bool Radius{}, Transformed{}, Moving{};
            std::uint32_t Capacity{1}, PageQueries{}, RowCount{}, TargetCount{};
            // Staging: an uploading session's host-visible copy source, reused by every upload (Queries is then
            // device-local and written only by the page's own compute-lane copy).
            RHI::BufferManager::BufferLease Queries{}, Staging{}, Exclusions{}, Packed{}, Pose{}, Posed{};
            std::uint64_t QueriesAddress{}, ExclusionsAddress{}, PackedAddress{}, PoseAddress{}, PosedAddress{};
            RHI::BufferHandle TargetPoints{}, TargetMapping{};
            std::uint64_t TargetPointsAddress{}, TargetMappingAddress{};
            std::vector<std::uint32_t> Excluded{}; // original row IDs, one per query row (empty: none)
            // Worker only: the next page copies Staging into Queries first; a moving Lbvh target's hierarchy is rebuilt
            // by the next page; page scratch.
            bool PendingCopy{}, NeedsBuild{};
            std::vector<std::byte> Bytes{};
            std::vector<std::uint32_t> Headers{};
            std::atomic<std::uint64_t> LastToken{}, Pages{}, Poses{}, UploadBytes{}, ReadbackBytes{};
            // Owner: residency slots the pages read, stamped with the compute fence once the run dropped the session.
            std::vector<RHI::BufferHandle> Resident{};
            bool Stamped{};
            [[nodiscard]] std::span<const std::uint32_t> Slots() const
            {
                return Moving ? std::span<const std::uint32_t>(Identity)
                              : std::span<const std::uint32_t>(Snapshot->Slots);
            }
            // Waits in bounded slices for the worker's own page; false (released) on abort or a device that cannot
            // finish it.
            [[nodiscard]] SpatialPageStatus Await(const RHI::ReadbackToken token, const std::atomic<bool>& abort) const
            {
                const auto status = WaitForComputePage(*Transfer,
                    token,
                    [&abort]
                    {
                        return abort.load(std::memory_order_acquire);
                    });
                if (status == ComputePageWait::Complete)
                    return SpatialPageStatus::Ready;
                Transfer->ReleaseReadback(token);
                return status == ComputePageWait::Abandoned ? SpatialPageStatus::Abandoned : SpatialPageStatus::Failed;
            }
            // Owner, cache shutdown after device idle (N5): no page runs (the lock), an abandoned last page is waited
            // for, and every device object goes now, on the owner, while the device lives; later pages and uploads fail,
            // and the run's last copy (any thread, any time) releases only host memory.
            void Stop()
            {
                std::scoped_lock paging{Paging};
                if (std::exchange(Stopped, true))
                    return;
                if (Transfer)
                    (void)WaitForComputePage(*Transfer,
                        RHI::ReadbackToken{LastToken.load(std::memory_order_acquire)},
                        []
                        {
                            return false;
                        });
                Workspace = nullptr;
                Own.reset();
                Target.reset();
                for (auto* lease : {&Queries, &Staging, &Exclusions, &Packed, &Pose, &Posed})
                    *lease = {};
                Scratch.reset(); // after its leases
                Resident.clear();
            }
        };
        SpatialQuerySession::SpatialQuerySession(std::unique_ptr<Impl> impl) noexcept : m_Impl(std::move(impl)) {}
        SpatialQuerySession::~SpatialQuerySession() = default;
        std::uint32_t SpatialQuerySession::Rows() const noexcept
        {
            return m_Impl->RowCount;
        }
        std::uint32_t SpatialQuerySession::PageQueries() const noexcept
        {
            return m_Impl->PageQueries;
        }
        std::uint32_t SpatialQuerySession::Capacity() const noexcept
        {
            return m_Impl->Capacity;
        }
        std::uint64_t SpatialQuerySession::Pages() const noexcept
        {
            return m_Impl->Pages.load(std::memory_order_acquire);
        }
        std::uint64_t SpatialQuerySession::UploadBytes() const noexcept
        {
            return m_Impl->UploadBytes.load(std::memory_order_acquire);
        }
        std::uint64_t SpatialQuerySession::ReadbackBytes() const noexcept
        {
            return m_Impl->ReadbackBytes.load(std::memory_order_acquire);
        }
        bool SpatialQuerySession::UploadQueries(const std::span<const glm::vec3> rows, std::string& diagnostic)
        {
            auto& s = *m_Impl;
            std::scoped_lock paging{s.Paging};
            if (s.Stopped)
            {
                diagnostic = "Spatial query service stopped.";
                return false;
            }
            if (rows.size() != s.RowCount || !std::ranges::all_of(rows, Geometry::PointLBVH::ValidPoint))
            {
                diagnostic = "GPU query rows must keep the session's count and be finite within +/-1e18.";
                return false;
            }
            if (!s.Staging.IsValid())
            {
                diagnostic = "This GPU query session was prepared without query uploads.";
                return false;
            }
            // The staging is reused: no submitted page may still copy out of it (an abandoned one may still run). A
            // zero-timeout wait on the session's own last token, no frame.
            if (s.Transfer->WaitCompute(RHI::ReadbackToken{s.LastToken.load(std::memory_order_acquire)}, 0u) !=
                RHI::ComputeWaitStatus::Complete)
            {
                diagnostic = "A GPU query page still reads the session's rows.";
                return false;
            }
            // Mapped host write (no queue, no frame, no staging allocation); the next page copies it on the compute lane.
            if (!s.Device->WriteMappedBuffer(s.Staging.GetHandle(), rows.data(), rows.size() * 12u))
            {
                diagnostic = "The device refused the GPU query rows' mapped write (no persistent mapping, or a lost "
                             "device).";
                return false;
            }
            s.UploadBytes.fetch_add(rows.size() * 12u, std::memory_order_relaxed);
            s.PendingCopy = true;
            s.NeedsBuild = s.Moving && s.Strategy == Graphics::PointQueryStrategy::Lbvh;
            return true;
        }
        SpatialPageStatus SpatialQuerySession::Query(const std::uint32_t first,
            const std::uint32_t count,
            const float radius,
            const glm::dmat4* transform,
            const std::atomic<bool>& abort,
            SpatialNearestBatch& out)
        {
            auto& s = *m_Impl;
            const auto fail = [&out](std::string why)
            {
                out.State = SpatialQueryState::Failed;
                out.Diagnostic = std::move(why);
                return SpatialPageStatus::Failed;
            };
            std::scoped_lock paging{s.Paging};
            if (s.Stopped)
                return fail("Spatial query service stopped.");
            if (count == 0u || count > s.PageQueries || first > s.RowCount || count > s.RowCount - first)
                return fail("GPU query page outside the session's rows.");
            if (s.Radius && (!std::isfinite(radius) || radius < 0 || radius > Geometry::PointLBVH::CoordinateLimit))
                return fail("GPU radius must be finite and in [0, 1e18].");
            if (s.Transformed != (transform != nullptr))
                return fail("A transformed GPU query session needs one pose per page, others none.");
            if (transform)
                for (unsigned column = 0; column < 4; ++column)
                    for (unsigned row = 0; row < 4; ++row)
                        if (!std::isfinite((*transform)[column][row]))
                            return fail("Query transform must be finite.");
            if (abort.load(std::memory_order_acquire))
                return SpatialPageStatus::Abandoned;
            const std::uint32_t capacity = s.Capacity;
            const std::uint64_t neighborBytes = std::uint64_t(count) * capacity * 8u;
            const std::uint64_t bytes = neighborBytes + std::uint64_t(count) * 8u;
            const auto packed = s.Packed.GetHandle();
            const auto token = s.Device->SubmitComputeReadback(
                [&](RHI::ICommandContext& cmd) -> RHI::BufferHandle
                {
                    if (transform)
                    {
                        // ponytail: the pose as 32 in-command FillBuffer words (the RHI has no UpdateBuffer); no host
                        // write races the owner and nothing waits for the graphics lane. The previous page completed.
                        std::array<std::uint32_t, sizeof(glm::dmat4) / 4u> words{};
                        std::memcpy(words.data(), &(*transform)[0][0], sizeof(glm::dmat4));
                        cmd.BufferBarrier(
                            s.Pose.GetHandle(), RHI::MemoryAccess::ShaderRead, RHI::MemoryAccess::TransferWrite);
                        for (std::size_t i = 0; i < words.size(); ++i)
                            cmd.FillBuffer(s.Pose.GetHandle(), i * 4u, 4u, words[i]);
                    }
                    if (s.PendingCopy)
                    {
                        // The uploaded rows, staging -> device-local queries, ordered before this page's reads (the
                        // build's and query's barriers cover TransferWrite -> ShaderRead).
                        cmd.BufferBarrier(
                            s.Queries.GetHandle(), RHI::MemoryAccess::ShaderRead, RHI::MemoryAccess::TransferWrite);
                        cmd.CopyBuffer(
                            s.Staging.GetHandle(), s.Queries.GetHandle(), 0u, 0u, std::uint64_t(s.RowCount) * 12u);
                    }
                    if (s.NeedsBuild &&
                        !s.Own->RecordBuild(
                            cmd, {.Buffer = s.Queries.GetHandle(), .Count = s.RowCount, .Address = s.QueriesAddress}))
                        return {};
                    const Graphics::PointLbvhQuery query{.Queries = {.Buffer = s.Queries.GetHandle(),
                                                             .Offset = std::uint64_t(first) * 12u,
                                                             .Count = count,
                                                             .Address = s.QueriesAddress},
                        .Neighbors = packed,
                        .Headers = packed,
                        .Capacity = capacity,
                        .Radius = s.Radius ? radius : -1.f,
                        .KNearestCount = s.Radius ? 0u : capacity,
                        .ExcludedIndices = s.Exclusions.GetHandle(),
                        .Transform = transform ? s.Pose.GetHandle() : RHI::BufferHandle{},
                        .TransformedQueries = transform ? s.Posed.GetHandle() : RHI::BufferHandle{},
                        .HeaderOffset = neighborBytes,
                        .Resolved = {.Neighbors = s.PackedAddress,
                            .Headers = s.PackedAddress,
                            .ExcludedIndices =
                                s.ExclusionsAddress ? s.ExclusionsAddress + std::uint64_t(first) * 4u : 0u,
                            .Transform = s.PoseAddress,
                            .TransformedQueries = s.PosedAddress,
                            .TargetIndices = s.TargetMappingAddress}};
                    const bool recorded = s.Strategy == Graphics::PointQueryStrategy::BruteForce
                        ? s.Workspace->RecordBruteForceQuery(cmd,
                              {.Buffer = s.TargetPoints, .Count = s.TargetCount, .Address = s.TargetPointsAddress},
                              s.TargetMapping,
                              query)
                        : s.Workspace->RecordQuery(cmd, query);
                    return recorded ? packed : RHI::BufferHandle{};
                },
                bytes,
                RHI::ReadbackSink::Pull(),
                // A fixed target's inputs precede the session's preparation submission (which waited for every transfer
                // issued before it), a moving target's are host writes and earlier compute-lane copies: later
                // graphics-queue uploads (a preview, the renderer) are not this page's inputs and are not waited for.
                RHI::ComputeInputOrder::LaneOrderOnly);
            if (!token.IsValid())
                return fail("The GPU compute lane refused the query page.");
            s.LastToken.store(token.Value, std::memory_order_release);
            s.Pages.fetch_add(1u, std::memory_order_relaxed);
            if (transform)
            {
                s.UploadBytes.fetch_add(sizeof(glm::dmat4), std::memory_order_relaxed);
                s.Poses.fetch_add(1u, std::memory_order_relaxed);
            }
            s.PendingCopy = s.NeedsBuild = false; // the accepted page copied the rows and rebuilt the moving hierarchy
            if (const auto waited = s.Await(token, abort); waited != SpatialPageStatus::Ready)
                return waited == SpatialPageStatus::Abandoned
                    ? waited
                    : fail("GPU query page did not complete: the device was lost.");
            s.Bytes.resize(bytes);
            if (!s.Transfer->TakeCompletedReadback(token, s.Bytes))
            {
                s.Transfer->ReleaseReadback(token);
                return fail("GPU query page readback unavailable.");
            }
            s.ReadbackBytes.fetch_add(bytes, std::memory_order_relaxed);
            out.Capacity = capacity;
            out.Diagnostic.clear();
            out.Neighbors.resize(std::size_t(count) * capacity);
            out.Counts.assign(count, 0u);
            const auto excluded = s.Excluded.empty() ? std::span<const std::uint32_t>{}
                                                     : std::span<const std::uint32_t>(s.Excluded).subspan(first, count);
            return DecodePacked(s.Bytes, s.Slots(), excluded, capacity, s.Radius ? radius : -1.f, s.Headers, out)
                ? SpatialPageStatus::Ready
                : SpatialPageStatus::Failed;
        }
        // ARCH-023: a retained device index view (PrepareGpuIndex). The worker reads View, Buffers and Snapshot only;
        // the owner reads Target, Resident, Stamped and Fence.
        struct SpatialGpuIndexLease::Impl
        {
            std::shared_ptr<const void> Target{}; // the cache entry, with its device buffers
            const void* TargetKey{};
            SpatialGpuIndexView View{};
            std::vector<RHI::BufferHandle> Buffers{};
            std::shared_ptr<const SpatialIndexSnapshot> Snapshot{};
            std::vector<RHI::BufferHandle> Resident{}; // borrowed residency slots, stamped once the lease is dropped
            bool Stamped{};
            RHI::ReadbackToken Fence{};
        };
        SpatialGpuIndexLease::SpatialGpuIndexLease(std::unique_ptr<Impl> impl) noexcept : m_Impl(std::move(impl)) {}
        SpatialGpuIndexLease::~SpatialGpuIndexLease() = default;
        const SpatialGpuIndexView& SpatialGpuIndexLease::View() const noexcept
        {
            return m_Impl->View;
        }
        std::span<const RHI::BufferHandle> SpatialGpuIndexLease::Barriers() const noexcept
        {
            return m_Impl->Buffers;
        }
        const std::shared_ptr<const SpatialIndexSnapshot>& SpatialGpuIndexLease::Snapshot() const noexcept
        {
            return m_Impl->Snapshot;
        }
    struct SpatialIndexCache::Impl
    {
        struct Entry
        {
            std::uint64_t Id{};
            bool Transient{};
            WorldHandle World{};
            entt::entity Entity{};
            GeometryPropertyRef Ref{};
            std::uint64_t Revision{}, DeletedRevision{};
            std::size_t Size{};
            SpatialIndexSpace Space{};
            glm::mat4 Matrix{1.f};
            std::shared_ptr<SpatialIndexSnapshot> Snapshot{std::make_shared<SpatialIndexSnapshot>()};
            RHI::IDevice* Device{};
            RHI::BufferHandle Points{}, Mapping{};
            std::optional<Graphics::GpuPropertyView> GatherSource{};
            // GRAPHICS-154: Points borrowed from the property's canonical residency slot (shared
            // with every other GPU user of that revision); the lease keeps it resident.
            std::shared_ptr<const void> ResidentPoints{};
            std::unique_ptr<Graphics::PointLbvhWorkspace> Gpu{};
            // Device producer state, set only once a submission that recorded it was accepted; a refused
            // one leaves it unset, so the next submission records it again (Gpu's own recorded-build flag
            // is not an execution receipt). Gathered: the canonical-slot gather into Points. Built: the
            // hierarchy over the current Points. PointsStale: UpdateWorkspace replaced the points, which
            // the next submission rewrites on the host (a brute-force query needs only that, no Built).
            bool Gathered{}, Built{}, PointsStale{};
            bool CpuIndex{true};
            [[nodiscard]] std::span<const glm::vec3> HostPoints() const
            {
                return Snapshot->Points();
            }
            // Device bytes this entry owns (a borrowed canonical slot excluded).
            [[nodiscard]] std::uint64_t DeviceBytes() const
            {
                const std::uint64_t rows = Snapshot->Slots.size();
                return (Points.IsValid() && !ResidentPoints ? std::max<std::uint64_t>(16u, rows * 12u) : 0u) +
                    (Mapping.IsValid() ? std::max<std::uint64_t>(16u, rows * 4u) : 0u) +
                    (Gpu ? Gpu->DeviceBytes() : 0u);
            }
            ~Entry()
            {
                if (Device)
                {
                    if (Points.IsValid() && !ResidentPoints)
                        Device->DestroyBuffer(Points);
                    if (Mapping.IsValid())
                        Device->DestroyBuffer(Mapping);
                }
            }
        };
        WorldRegistry* Worlds{};
        RHI::IDevice* Device{};
        // Method scratch (MethodScratch): declared before every lease holder below, so destroyed after them.
        std::shared_ptr<RHI::BufferManager> Scratch{};
        std::unique_ptr<Graphics::GpuPropertyResidency> Residency{};
        Graphics::GpuPropertyResidencyConfig ResidencyConfig{};
        std::vector<std::shared_ptr<Entry>> Entries{};
        JobService* Jobs{};
        GpuQueueParticipantHandle Participant{};
        struct Computation
        {
            std::shared_ptr<SpatialGpuResult> Result{std::make_shared<SpatialGpuResult>()};
            std::shared_ptr<Entry> Target{};
            // Retained with its captured leases until the sink ran (the device finished).
            std::function<RHI::BufferHandle(RHI::ICommandContext&, const SpatialGpuIndexView&)> Record{};
        };
        // What one submission recorded into its target entry, committed only when it is accepted.
        struct Producer
        {
            Entry* Target{};
            bool Gathered{}, Built{};
        };
        std::vector<std::shared_ptr<Computation>> Computations{};
        // ARCH-023 worker query sessions, kept until the run dropped them and their last page completed.
        std::vector<std::shared_ptr<SpatialQuerySession>> Sessions{};
        // ARCH-023 retained device index views, kept until the run dropped them and the compute fence at the drop
        // completed.
        std::vector<std::shared_ptr<SpatialGpuIndexLease>> IndexLeases{};
        std::shared_ptr<WorkspacePool> Pool{std::make_shared<WorkspacePool>()};
        // Owner thread: stamps returned workspaces and moves those whose stamp frame has retired
        // to Idle, keeping the most recently matured one per kind.
        void MatureWorkspaces()
        {
            std::vector<WorkspacePool::Slot> returned;
            {
                std::scoped_lock lock{Pool->Mutex};
                returned.swap(Pool->Returned);
            }
            if (!Device)
            {
                Pool->Waiting.clear();
                Pool->Idle.clear();
                return;
            }
            // One stamp for the returned workspaces and the scratch their release just returned.
            const auto stamp = RHI::StampRetirement(*Device);
            for (auto& slot : returned)
            {
                slot.ReleaseScratch(slot.Object.get());
                slot.Stamp = stamp;
                Pool->Waiting.push_back(std::move(slot));
            }
            Scratch->CollectScratch(stamp);
            for (auto it = Pool->Waiting.begin(); it != Pool->Waiting.end();)
            {
                if (!RHI::IsRetired(*Device, it->Stamp))
                {
                    ++it;
                    continue;
                }
                std::erase_if(Pool->Idle, [&](const auto& idle) { return idle.Kind == it->Kind; });
                Pool->Idle.push_back(std::move(*it));
                it = Pool->Waiting.erase(it);
            }
        }
        bool HoldsWorkspaces()
        {
            std::scoped_lock lock{Pool->Mutex};
            return !Pool->Returned.empty() || !Pool->Waiting.empty() || !Pool->Idle.empty();
        }
        void ReleaseWorkspacesAfterDeviceIdle()
        {
            std::vector<WorkspacePool::Slot> returned;
            {
                std::scoped_lock lock{Pool->Mutex};
                returned.swap(Pool->Returned);
            }
            returned.clear();
            Pool->Waiting.clear();
            Pool->Idle.clear();
            // Leases still held past shutdown destroy their workspace instead of returning it.
            Pool = std::make_shared<WorkspacePool>();
            // Every pooled buffer is unused once the device is idle; later releases destroy directly.
            if (Scratch)
                Scratch->DrainScratch();
        }
        std::shared_ptr<void> LeaseWorkspace(const void* kind,
            std::shared_ptr<void> (*make)(RHI::IDevice&, RHI::BufferManager&),
            void (*releaseScratch)(void*))
        {
            if (!Device || !Scratch)
                return {};
            MatureWorkspaces();
            std::shared_ptr<void> object;
            const auto idle = std::ranges::find_if(Pool->Idle,
                [&](const auto& slot)
                {
                    return slot.Kind == kind;
                });
            const bool reused = idle != Pool->Idle.end();
            if (reused)
            {
                object = std::move(idle->Object);
                Pool->Idle.erase(idle);
            }
            else
                object = make(*Device, *Scratch);
            if (!object)
                return {};
            ++Stats.WorkspaceLeases;
            if (reused)
                ++Stats.WorkspaceReuses;
            void* const raw = object.get();
            auto lease = std::make_shared<WorkspaceLease>(
                WorkspacePool::Slot{.Kind = kind, .Object = std::move(object), .ReleaseScratch = releaseScratch},
                Pool,
                Scratch);
            return std::shared_ptr<void>(std::move(lease), raw);
        }
        // The method budget's refusal since `refusals` (a ScratchRefusals() value), else `fallback`.
        std::uint64_t ScratchRefusals() const
        {
            return Scratch ? Scratch->GetScratchStats().Refusals : 0u;
        }
        std::string ScratchDiagnostic(std::uint64_t refusals, std::string_view fallback) const
        {
            const auto stats = Scratch ? Scratch->GetScratchStats() : RHI::ScratchStats{};
            if (stats.Refusals == refusals)
                return std::string(fallback);
            if (!stats.LastRefusedBytes)
                return "GPU method scratch request exceeds the largest buffer size.";
            return std::format(
                "GPU method buffer budget exceeded: a {}-byte scratch buffer does not fit beside {} bytes "
                "in use or awaiting GPU completion within the {}-byte budget; raise "
                "render.gpu_method_buffer_budget_megabytes or reduce the input.",
                stats.LastRefusedBytes,
                stats.LastRefusalCommittedBytes,
                stats.BudgetBytes);
        }
        // Delivers a computation's readback bytes once the device finished it. A refused submission
        // (Failed) never publishes, even if a sink were delivered.
        static RHI::ReadbackSink ComputationSink(const std::shared_ptr<Computation>& work)
        {
            return RHI::ReadbackSink::Invoke(
                [work](std::span<const std::byte> data)
                {
                    if (work->Result->State == SpatialQueryState::Failed)
                        return;
                    if (data.size() != work->Result->Data.size())
                    {
                        work->Result->Diagnostic = "GPU computation returned an incomplete result.";
                        work->Result->State = SpatialQueryState::Failed;
                        return;
                    }
                    if (!data.empty())
                        std::memcpy(work->Result->Data.data(), data.data(), data.size());
                    work->Result->State = SpatialQueryState::Ready;
                });
        }
        void Commit(const Producer& producer)
        {
            if (!producer.Target)
                return;
            producer.Target->Gathered |= producer.Gathered;
            if (!producer.Built)
                return;
            producer.Target->Built = true;
            ++Stats.GpuBuilds;
        }
        // The one owner submission path (computations, session/index preparations): IDevice::SubmitComputeReadback,
        // at once, on the compute lane (graphics where forced or single-queue; the transfer queue's
        // ComputeLane diagnostic reports which). Nothing records into a graphics frame. On acceptance
        // the target's producer state is committed and every residency slot the recorder used carries
        // the returned token besides its frame; an invalid token means nothing was submitted. `host` runs first,
        // inside the residency bracket and before the device call (buffers, residency acquisition and its uploads,
        // host writes); false submits nothing. `record` only records. Owner submissions wait for every
        // transfer issued before them (ComputeInputOrder::AfterIssuedTransfers, the default).
        RHI::ReadbackToken Submit(const std::function<bool()>& host,
            const std::function<RHI::BufferHandle(RHI::ICommandContext&, Producer&)>& record,
            std::uint64_t bytes,
            RHI::ReadbackSink sink)
        {
            Producer producer;
            auto* residency = EnsureResidency();
            if (residency)
                residency->BeginSubmission();
            const auto token = host() ? Device->SubmitComputeReadback(
                                            [&](RHI::ICommandContext& commands)
                                            {
                                                return record(commands, producer);
                                            },
                                            bytes,
                                            std::move(sink))
                                      : RHI::ReadbackToken{};
            if (residency)
                residency->EndSubmission(token);
            if (token.IsValid())
                Commit(producer);
            return token;
        }
        // Submits a validated computation; Failed (never replayed: a recorder may advance a cursor) when
        // the device refuses it.
        void SubmitComputation(const std::shared_ptr<Computation>& work)
        {
            const auto refusals = ScratchRefusals();
            std::string_view why = "The GPU compute lane refused the submission before recording.";
            Entry* e = nullptr;
            const auto token = Submit(
                [&]
                {
                    if (!work->Target)
                        return true;
                    e = PrepareHost({work->Target->Id}, true, &work->Result->CpuStageUploadBytes);
                    if (!e)
                        why = "GPU computation could not record against a current index.";
                    return e != nullptr;
                },
                [&](RHI::ICommandContext& commands, Producer& producer) -> RHI::BufferHandle
                {
                    why = "GPU compute submission refused after recording.";
                    if (!e)
                        return work->Record(commands, {});
                    if (!RecordBuild(*e, commands, producer))
                    {
                        why = "GPU computation could not record against a current index.";
                        return {};
                    }
                    return work->Record(commands,
                        {e->Gpu->View().NodesBDA,
                            Device->GetBufferDeviceAddress(e->Points),
                            Device->GetBufferDeviceAddress(e->Mapping),
                            std::uint32_t(e->Snapshot->Slots.size())});
                },
                work->Result->Data.size(),
                ComputationSink(work));
            if (!token.IsValid())
            {
                work->Result->State = SpatialQueryState::Failed;
                work->Result->Diagnostic = ScratchDiagnostic(refusals, why);
                return;
            }
            if (work->Result->State == SpatialQueryState::Queued)
                work->Result->State = SpatialQueryState::Submitted;
            ++Stats.GpuComputeSubmissions;
            Computations.push_back(work);
        }
        void ShutdownBatches()
        {
            // Called after the participant's device-idle fence. Deliver pending sinks
            // before releasing their device-owned source buffers.
            if (Device && !Computations.empty())
                Device->GetTransferQueue().CollectCompleted();
            for (auto& work : Computations)
            {
                work->Result->State = SpatialQueryState::Failed;
                work->Result->Diagnostic = "Spatial compute service stopped.";
            }
            Computations.clear();
            StopSessions(); // before the scratch drain: their leases return to it
            // IndexLeases stay: at participant shutdown run workers may still read their views (CancelAndDrain comes
            // later); the module's OnShutdown releases them (ReleaseIndexLeases).
            ReleaseWorkspacesAfterDeviceIdle();
        }
        // ARCH-023: final module shutdown, workers drained and the device idle. A lease a run or caller still holds
        // gives up its entry now, on this thread (the entry's device buffers go here if the lease held it alone);
        // its view and barriers go empty, so its later drop releases host memory only.
        void ReleaseIndexLeases()
        {
            for (const auto& lease : IndexLeases)
            {
                auto& i = lease->Internals();
                i.Target.reset();
                i.TargetKey = nullptr;
                i.View = {};
                i.Buffers.clear();
                i.Resident.clear();
                i.Stamped = true; // the residency goes with the module
            }
            IndexLeases.clear();
        }
        // Completion arrives through each submission's sink, never by counting frames; this only
        // releases finished work that neither a caller nor a pending sink still references.
        void Drain()
        {
            std::erase_if(Computations,
                [](const auto& work)
                {
                    const auto state = work->Result->State;
                    return work.use_count() == 1 &&
                        (state == SpatialQueryState::Ready || state == SpatialQueryState::Failed);
                });
            // A dropped session: its residency slots first carry every compute submission so far (its pages
            // included), its traffic is folded, then it goes once its last page completed.
            std::erase_if(Sessions,
                [this](const auto& session)
                {
                    if (session.use_count() != 1)
                        return false;
                    auto& i = session->Internals();
                    if (!std::exchange(i.Stamped, true))
                    {
                        if (auto* residency = Residency.get(); residency && !i.Resident.empty())
                        {
                            const auto frame = Device->GetGlobalFrameNumber();
                            residency->BeginSubmission();
                            for (const auto buffer : i.Resident)
                                residency->NoteUse(buffer, frame);
                            residency->EndSubmission(Device->GetTransferQueue().ComputeFence());
                        }
                        Stats.GpuQuerySubmissions += i.Pages.load(std::memory_order_acquire);
                        Stats.GpuQueryTransformUploads += i.Poses.load(std::memory_order_acquire);
                        Stats.GpuQueryUploadBytes += i.UploadBytes.load(std::memory_order_acquire);
                    }
                    return Device->GetTransferQueue().IsComplete(
                        RHI::ReadbackToken{i.LastToken.load(std::memory_order_acquire)});
                });
            // A dropped index view: its borrowed residency slots carry every compute submission so far, then the entry
            // (if the cache let it go meanwhile) is freed once that fence completed.
            std::erase_if(IndexLeases,
                [this](const auto& lease)
                {
                    if (lease.use_count() != 1)
                        return false;
                    auto& i = lease->Internals();
                    if (!std::exchange(i.Stamped, true))
                    {
                        i.Fence = Device->GetTransferQueue().ComputeFence();
                        if (auto* residency = Residency.get(); residency && !i.Resident.empty())
                        {
                            const auto frame = Device->GetGlobalFrameNumber();
                            residency->BeginSubmission();
                            for (const auto buffer : i.Resident)
                                residency->NoteUse(buffer, frame);
                            residency->EndSubmission(i.Fence);
                        }
                    }
                    return Device->GetTransferQueue().IsComplete(i.Fence);
                });
        }
        std::uint64_t Next{1};
        SpatialIndexCacheStats Stats{};
        RenderExtractionCache* Extraction{}; // observes rings through our residency while registered
        Graphics::GpuPropertyResidency* EnsureResidency()
        {
            if (!Residency && Device) Residency = std::make_unique<Graphics::GpuPropertyResidency>(*Device, ResidencyConfig);
            return Residency.get();
        }
        bool Current(const Entry& e) const
        {
            if (e.Transient) return e.Snapshot.use_count() > 1;
            auto source = Resolve(Worlds, e.World, e.Entity, e.Ref);
            return source && source->Points.Revision() == e.Revision &&
                   source->Points.Size() == e.Size &&
                   (source->Deleted ? source->Deleted.Revision() : 0) == e.DeletedRevision &&
                   (e.Space == SpatialIndexSpace::Property || EntityMatrix(Worlds, e.World, e.Entity) == e.Matrix);
        }
        Entry* Find(SpatialIndexHandle handle) const
        {
            const auto it =
                std::ranges::find_if(Entries, [&](const auto& e) { return e->Id == handle.Value; });
            return it != Entries.end() && Current(**it) ? it->get() : nullptr;
        }
        // Host side of a submission, before IDevice::SubmitComputeReadback (Submit's `host`): the entry's float3
        // samples and original-row mapping for either query strategy (buffers created, host writes at once, the
        // canonical slot acquired, whose upload goes on the graphics lane now, before the submission that waits for
        // it), and with `lbvh` the hierarchy's storage and pipelines. Records nothing: a recorder may not create
        // resources or submit (the RHI contract), and on the graphics lane an upload from inside it would take the
        // lane lock its recording holds. Null when the entry is not current or cannot be made resident; never
        // silently runs CPU.
        Entry* PrepareHost(SpatialIndexHandle handle, const bool lbvh, std::uint64_t* hostBytes = nullptr)
        {
            auto& s = *this;
            auto* e = s.Find(handle);
            if (!e || !s.Device || !s.Device->IsOperational())
                return nullptr;
            e->Device = s.Device;
            if (!e->Gpu)
            {
                e->Gpu = std::make_unique<Graphics::PointLbvhWorkspace>(*s.Device);
                auto allocate = [&](std::size_t bytes) {
                    return s.Device->CreateBuffer(RHI::BufferDesc{
                        .SizeBytes = std::max(std::size_t(16), bytes),
                        .Usage = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferSrc |
                                 RHI::BufferUsage::TransferDst,
                        .HostVisible = true,
                        .DebugName = "SpatialIndex.Source"});
                };
                // Compact live rows on the device from the same canonical property slot
                // used by method kernels. The CPU snapshot supplies only the row mapping.
                const bool canonical = !e->Transient && e->Space == SpatialIndexSpace::Property && e->Size;
                if (canonical)
                {
                    const auto source = Resolve(s.Worlds, e->World, e->Entity, e->Ref);
                    if (!source)
                    {
                        e->Gpu.reset();
                        return nullptr;
                    }
                    const auto view = s.EnsureResidency()->AcquireInput(
                        MakeGpuPropertyKey(e->World, e->Entity, e->Ref), e->Revision,
                        *MakeGpuPropertyLayout(Geometry::PropertyValueKind::Vec3, std::uint32_t(e->Size)),
                        [&](std::span<std::byte> out) { std::memcpy(out.data(), source->Points.Span().data(), out.size()); });
                    if (!view)
                    {
                        e->Gpu.reset();
                        return nullptr;
                    }
                    if (e->Snapshot->Slots.size() == e->Size)
                    {
                        e->Points = view->Buffer;
                        e->ResidentPoints = view->Lease;
                    }
                    else e->GatherSource = view;
                }
                if (!e->Points.IsValid()) e->Points = allocate(e->Snapshot->Slots.size() * 12);
                e->Mapping = allocate(e->Snapshot->Slots.size() * 4);
                if (!e->Points.IsValid() || !e->Mapping.IsValid())
                {
                    if (e->Points.IsValid() && !e->ResidentPoints)
                        s.Device->DestroyBuffer(e->Points);
                    if (e->Mapping.IsValid())
                        s.Device->DestroyBuffer(e->Mapping);
                    e->Points = {};
                    e->ResidentPoints.reset();
                    e->Mapping = {};
                    e->Gpu.reset();
                    return nullptr;
                }
                if (!e->Snapshot->Slots.empty())
                {
                    if (!e->ResidentPoints && !e->GatherSource)
                        s.Device->WriteBuffer(e->Points, e->HostPoints().data(), e->Snapshot->Slots.size() * 12);
                    s.Device->WriteBuffer(e->Mapping, e->Snapshot->Slots.data(), e->Snapshot->Slots.size() * 4);
                    if(hostBytes)*hostBytes+=e->Snapshot->Slots.size()*(4+(!e->ResidentPoints&&!e->GatherSource?12:0));
                }
                e->PointsStale = false;
                e->Gathered = !e->GatherSource;
            }
            else if (e->PointsStale)
            {
                // Same count and identity slots: only the points change.
                s.Device->WriteBuffer(e->Points, e->HostPoints().data(), e->Snapshot->Slots.size() * 12);
                e->PointsStale = false;
            }
            if (lbvh && !e->Built && !e->Gpu->Reserve(std::uint32_t(e->Snapshot->Slots.size())))
                return nullptr;
            return e;
        }
        // Recording side (inside Submit's recorder, after PrepareHost): the canonical-slot gather until a submission
        // carrying it is accepted; the slots it reads are noted for this submission's token. Builds no hierarchy.
        void RecordGather(Entry& e, RHI::ICommandContext& commands, Producer& producer)
        {
            // The build and every query read the canonical slot in this submission.
            producer.Target = &e;
            if (e.ResidentPoints && Residency)
                Residency->NoteUse(e.Points, Device->GetGlobalFrameNumber());
            if (e.Gathered)
                return;
            const auto& source = *e.GatherSource;
            Residency->NoteUse(source.Buffer, Device->GetGlobalFrameNumber());
            commands.BufferBarrier(source.Buffer,
                RHI::MemoryAccess::TransferWrite | RHI::MemoryAccess::ShaderWrite,
                RHI::MemoryAccess::TransferRead);
            commands.BufferBarrier(e.Points, RHI::MemoryAccess::ShaderRead, RHI::MemoryAccess::TransferWrite);
            const auto& slots = e.Snapshot->Slots;
            for (std::size_t first = 0; first < slots.size();)
            {
                std::size_t end = first + 1;
                while (end < slots.size() && slots[end] == slots[first] + end - first)
                    ++end;
                commands.CopyBuffer(
                    source.Buffer, e.Points, std::uint64_t(slots[first]) * 12, first * 12, (end - first) * 12);
                first = end;
            }
            producer.Gathered = true;
        }
        // LBVH strategy: the gather plus a lazy hierarchy build into the storage PrepareHost reserved, recorded into
        // the same submission as its consumer until one is accepted (and again after an update).
        bool RecordBuild(Entry& e, RHI::ICommandContext& commands, Producer& producer)
        {
            RecordGather(e, commands, producer);
            if (e.Built)
                return true;
            if (!e.Gpu->RecordBuild(
                    commands, {.Buffer = e.Points, .Count = std::uint32_t(e.Snapshot->Slots.size())}, e.Mapping))
                return false;
            producer.Built = true;
            return true;
        }
        // ARCH-023 N5: device shutdown with sessions a run still holds. The device is idle (after the
        // participant's fence); each session's device objects (its target entry, private hierarchy, scratch
        // leases) go now, on this thread, and its later pages fail, so the run's last copy releases nothing.
        void StopSessions()
        {
            for (const auto& session : Sessions)
                session->Internals().Stop();
            Sessions.clear();
        }
    };
    SpatialIndexCache::SpatialIndexCache() : m_Impl(std::make_unique<Impl>())
    {
    }
    SpatialIndexCache::SpatialIndexCache(WorldRegistry& worlds) : SpatialIndexCache()
    {
        m_Impl->Worlds = &worlds;
    }
    SpatialIndexCache::~SpatialIndexCache() = default;
    std::string_view SpatialIndexCache::Name() const noexcept
    {
        return "Runtime.SpatialIndexCache";
    }
    Core::Result SpatialIndexCache::OnRegister(EngineSetup& setup)
    {
        m_Impl->Worlds = &setup.Worlds();
        m_Impl->Device = setup.Services().Find<RHI::IDevice>();
        if (m_Impl->Device && !m_Impl->Scratch)
        {
            m_Impl->Scratch = std::make_shared<RHI::BufferManager>(*m_Impl->Device);
            // OnResolve applies the configured budget; this default covers a cache without config control.
            m_Impl->Scratch->SetScratchBudget(
                std::uint64_t(Core::Config::RenderConfig{}.GpuMethodBufferBudgetMegabytes) << 20u);
        }
        auto result = setup.Services().Provide<SpatialIndexCache>(*this, Name());
        if (!result)
            return result;
        m_Impl->Jobs = &setup.Jobs();
        if (m_Impl->Jobs && m_Impl->Device)
            m_Impl->Participant =
                m_Impl->Jobs->RegisterGpuQueueParticipant({.DebugName = "Runtime.SpatialIndex.Queries",
                    // Records nothing into frames: every submission goes through the compute lane.
                    .DrainCompletedTransfers =
                        [this]()
                    {
                        m_Impl->Drain();
                    },
                    .HasInFlightWork =
                        [this]()
                    {
                        return !m_Impl->Computations.empty() || !m_Impl->Sessions.empty() ||
                            !m_Impl->IndexLeases.empty() || m_Impl->HoldsWorkspaces();
                    },
                    .ShutdownAfterDeviceIdle =
                        [this]()
                    {
                        m_Impl->ShutdownBatches();
                        m_Impl->Participant = {};
                    }});
        return setup.RegisterFrameHook(FramePhase::Maintenance,
                                       [this](RuntimeFrameHookContext&) { Prune(); });
    }
    Core::Result SpatialIndexCache::OnResolve(EngineSetup& setup)
    {
        if (const auto* control = setup.Services().Find<EngineConfigControl>())
        {
            const auto& render = control->GetEngineConfigControlState().ActiveConfig.Render;
            m_Impl->ResidencyConfig.IdleEvictSeconds = double(render.GpuPropertyIdleEvictSeconds);
            m_Impl->ResidencyConfig.BudgetBytes = std::uint64_t(render.GpuPropertyBudgetMegabytes) << 20u;
            if (m_Impl->Scratch)
                m_Impl->Scratch->SetScratchBudget(std::uint64_t(render.GpuMethodBufferBudgetMegabytes) << 20u);
        }
        // The renderer observes method output rings through extraction (ADR 0030 decision 5).
        if (auto* extraction = setup.Services().Find<RenderExtractionCache>(); extraction && m_Impl->Device)
        {
            m_Impl->Extraction = extraction;
            extraction->SetGpuPropertyObserver(
                [this](const WorldHandle world, const entt::entity entity, const GeometryPropertyRef& ref)
                    -> std::optional<RenderExtractionCache::GpuPropertyFront> {
                    auto& s = *m_Impl;
                    auto* scene = s.Worlds ? s.Worlds->Get(world) : nullptr;
                    if (!scene || !s.Residency || s.Residency->Stats().Rings == 0u) return std::nullopt;
                    const auto front = ObserveGpuPropertyFront(*s.Residency, *scene, world, entity, ref);
                    if (!front) return std::nullopt;
                    return RenderExtractionCache::GpuPropertyFront{.Buffer = front->Buffer, .Address = front->Address,
                                                                   .Bytes = front->Bytes, .Count = front->Count,
                                                                   .Stamp = front->Stamp, .ScalarRange = front->ScalarRange};
                });
        }
        return Core::Ok();
    }
    void SpatialIndexCache::OnShutdown(RuntimeModuleShutdownContext& context)
    {
        if (m_Impl->Extraction) m_Impl->Extraction->SetGpuPropertyObserver({});
        m_Impl->Extraction = nullptr;
        if (m_Impl->Jobs && m_Impl->Participant.IsValid())
            m_Impl->Jobs->UnregisterGpuQueueParticipant(m_Impl->Participant, [this]() { m_Impl->Device->WaitIdle(); });
        else if (m_Impl->Device && (m_Impl->HoldsWorkspaces() || !m_Impl->IndexLeases.empty()))
            m_Impl->Device->WaitIdle(); // drained workers' last pages may still read leased entries
        m_Impl->ShutdownBatches();
        m_Impl->ReleaseIndexLeases();
        m_Impl->Jobs = nullptr;
        m_Impl->Entries.clear();
        m_Impl->Residency.reset(); // after the entries release their leases
        m_Impl->Device = nullptr;
        m_Impl->Worlds = nullptr;
        (void)context.Services.Withdraw<SpatialIndexCache>(*this);
    }
    SpatialIndexAcquisition SpatialIndexCache::Acquire(WorldHandle world,
        entt::entity entity,
        const GeometryPropertyRef& ref,
        SpatialIndexSpace space,
        const bool cpuIndex)
    {
        auto& s = *m_Impl;
        auto source = Resolve(s.Worlds, world, entity, ref);
        if (!source || source->Points.Size() > (1u << 24u))
            return {.Diagnostic =
                        "A live entity and compatible canonical float3 property are required."};
        auto old = std::ranges::find_if(s.Entries, [&](const auto& e) {
            return !e->Transient && e->World == world && e->Entity == entity && e->Ref == ref && e->Space == space;
        });
        if (old != s.Entries.end())
        {
            if (s.Current(**old))
            {
                auto& e = **old;
                if (cpuIndex && !e.CpuIndex)
                {
                    // The first CPU user of a GPU-only entry: its hierarchy once, into a new snapshot (holders of
                    // the GPU-only one, and the sessions reading the entry's device buffers, keep theirs).
                    auto snapshot = std::make_shared<SpatialIndexSnapshot>();
                    if (!snapshot->Index.Build(e.Snapshot->Positions))
                        return {.Diagnostic = "LBVH requires finite coordinates within +/-1e18 and at most "
                                              "2^24 live points."};
                    snapshot->Slots = e.Snapshot->Slots;
                    e.Snapshot = std::move(snapshot);
                    e.CpuIndex = true;
                    ++s.Stats.Builds;
                }
                ++s.Stats.Hits;
                return {{e.Id}, true, {}};
            }
            s.Entries.erase(old);
            ++s.Stats.Evictions;
        }
        auto e = std::make_shared<Impl::Entry>();
        e->World = world;
        e->Entity = entity;
        e->Ref = ref;
        e->Space = space;
        if (space == SpatialIndexSpace::EntityTransform) e->Matrix = EntityMatrix(s.Worlds, world, entity);
        e->Revision = source->Points.Revision();
        e->DeletedRevision = source->Deleted ? source->Deleted.Revision() : 0;
        e->Size = source->Points.Size();
        e->Device = s.Device;
        std::vector<glm::vec3> points;
        for (std::uint32_t i = 0; i < source->Points.Size(); ++i)
            if (!source->Deleted || !source->Deleted[i / source->DeletionDivisor])
            {
                points.push_back(glm::vec3(e->Matrix * glm::vec4(source->Points[i], 1.f)));
                e->Snapshot->Slots.push_back(i);
            }
        // GPU-only: the points Index::Build would accept, kept without the hierarchy.
        const bool valid =
            cpuIndex ? e->Snapshot->Index.Build(points) : std::ranges::all_of(points, Geometry::PointLBVH::ValidPoint);
        if (!valid)
            return {.Diagnostic = "LBVH requires finite coordinates within +/-1e18 and at most "
                                  "2^24 live points."};
        e->CpuIndex = e->Snapshot->Cpu = cpuIndex;
        if (!cpuIndex)
            e->Snapshot->Positions = std::move(points);
        e->Id = s.Next++;
        const auto id = e->Id;
        s.Entries.push_back(std::move(e));
        s.Stats.Builds += cpuIndex ? 1u : 0u; // CPU hierarchy builds only
        return {{id}, false, {}};
    }
    SpatialIndexWorkspace SpatialIndexCache::CreateWorkspace(std::span<const glm::vec3> positions)
    {
        auto e = std::make_shared<Impl::Entry>();
        if (positions.empty() || !e->Snapshot->Index.Build(positions))
            return {.Diagnostic = "A private LBVH workspace requires 1..2^24 finite points within +/-1e18."};
        e->Transient = true;
        e->Device = m_Impl->Device;
        e->Id = m_Impl->Next++;
        e->Snapshot->Slots.resize(positions.size());
        for (std::uint32_t i = 0; i < positions.size(); ++i) e->Snapshot->Slots[i] = i;
        SpatialIndexWorkspace result{{e->Id}, e->Snapshot, {}};
        m_Impl->Entries.push_back(std::move(e));
        ++m_Impl->Stats.Builds;
        return result;
    }
    bool SpatialIndexCache::UpdateWorkspace(SpatialIndexWorkspace& workspace, std::span<const glm::vec3> positions)
    {
        auto& s = *m_Impl;
        const auto found = std::ranges::find_if(s.Entries, [&](const auto& e) { return e->Id == workspace.Handle.Value; });
        if (found == s.Entries.end() || !(*found)->Transient || (*found)->Snapshot != workspace.Snapshot ||
            (*found)->Snapshot->Slots.size() != positions.size())
            return false;
        auto& e = *found;
        // The device buffers are host visible and rewritten on the next record: nothing that
        // reads them may still be queued or in flight.
        const auto busy = [](SpatialQueryState state) {
            return state == SpatialQueryState::Queued || state == SpatialQueryState::Submitted;
        };
        if (std::ranges::any_of(s.Computations,
                [&](const auto& c)
                {
                    return c->Target == e && busy(c->Result->State);
                }) ||
            std::ranges::any_of(s.IndexLeases,
                [&](const auto& l)
                {
                    return l->Internals().TargetKey == e.get();
                }) ||
            std::ranges::any_of(s.Sessions,
                [&](const auto& q)
                {
                    return q->Internals().TargetKey == e.get();
                }))
            return false; // a worker session reads its hierarchy until dropped and complete
        auto snapshot = std::make_shared<SpatialIndexSnapshot>();
        if (!snapshot->Index.Build(positions))
            return false;
        snapshot->Slots = e->Snapshot->Slots;
        e->Snapshot = snapshot;
        workspace.Snapshot = std::move(snapshot);
        e->Built = false;
        e->PointsStale = e->Gpu != nullptr;
        ++s.Stats.WorkspaceUpdates;
        return true;
    }
    std::shared_ptr<const SpatialIndexSnapshot> SpatialIndexCache::Snapshot(SpatialIndexHandle handle) const
    {
        const auto* entry = m_Impl->Find(handle);
        return entry ? entry->Snapshot : nullptr;
    }
    bool SpatialIndexCache::GpuQueriesAvailable() const noexcept
    {
        return m_Impl->Device && m_Impl->Device->IsOperational() && m_Impl->Participant.IsValid();
    }
    std::shared_ptr<SpatialQuerySession> SpatialIndexCache::PrepareQuerySession(
        const SpatialIndexHandle target, const SpatialQuerySessionDesc& d, std::string& diagnostic)
    {
        using S = Graphics::PointQueryStrategy;
        auto& s = *m_Impl;
        const auto fail = [&](std::string why) -> std::shared_ptr<SpatialQuerySession>
        {
            diagnostic = std::move(why);
            return {};
        };
        const std::size_t rows = d.Queries.size();
        const auto found = d.Moving ? s.Entries.end()
                                    : std::ranges::find_if(s.Entries,
                                          [&](const auto& e)
                                          {
                                              return e->Id == target.Value;
                                          });
        Impl::Entry* const entry = found != s.Entries.end() && s.Current(**found) ? found->get() : nullptr;
        const std::size_t targets = d.Moving ? rows : entry ? entry->Snapshot->Slots.size() : 0u;
        const auto page = std::uint32_t(std::min<std::size_t>(d.PageQueries, rows));
        if (!GpuQueriesAvailable() || (!d.Moving && !entry) || targets == 0u || targets > (1u << 20) || rows == 0u ||
            rows > (1u << 20) || page == 0u || d.Capacity == 0u || d.Capacity > (d.Radius ? 1024u : 64u) ||
            (!d.Excluded.empty() && d.Excluded.size() != rows) ||
            !std::ranges::all_of(d.Queries, Geometry::PointLBVH::ValidPoint) ||
            (d.Strategy != S::Lbvh && d.Strategy != S::BruteForce) ||
            (d.Transformed && (d.Radius || d.Capacity != 1u || d.Moving)))
            return fail("Vulkan query sessions require an operational device, a current target (1..2^20 rows) or "
                        "moving rows, finite queries (1..2^20) and a page of at least one, k in 1..64 or radius "
                        "capacity in 1..1024, zero or query-count exclusions, and a pose only for nearest queries "
                        "on a fixed target.");
        if (d.Strategy == S::BruteForce)
            if (const auto refusal = Graphics::PointBruteForceRefusal(page, std::uint32_t(targets), d.Capacity);
                !refusal.empty())
                return fail(std::string(refusal));
        if (d.Transformed && !s.Device->SupportsShaderFloat64())
            return fail("Transformed queries require shader float64.");
        auto i = std::make_unique<SpatialQuerySession::Impl>();
        i->Device = s.Device;
        i->Transfer = &s.Device->GetTransferQueue();
        i->Scratch = s.Scratch;
        i->Strategy = d.Strategy;
        i->Radius = d.Radius;
        i->Transformed = d.Transformed;
        i->Moving = d.Moving;
        i->Capacity = d.Capacity;
        i->PageQueries = page;
        i->RowCount = std::uint32_t(rows);
        i->TargetCount = std::uint32_t(targets);
        i->Excluded.assign(d.Excluded.begin(), d.Excluded.end());
        // Every buffer the pages use, once: the pages allocate nothing.
        const auto refusals = s.ScratchRefusals();
        const auto lease = [&](RHI::BufferManager::BufferLease& out, std::size_t bytes, bool host, const char* name)
        {
            auto created = s.Scratch->CreateScratch({.SizeBytes = bytes,
                .Usage = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferSrc | RHI::BufferUsage::TransferDst,
                .HostVisible = host,
                .DebugName = name});
            if (created)
                out = std::move(*created);
            return bool(created);
        };
        const bool uploads = d.Uploads || d.Moving;
        if (!lease(i->Queries, rows * 12u, !uploads, "SpatialIndex.QuerySession") ||
            (uploads && !lease(i->Staging, rows * 12u, true, "SpatialIndex.QueryStaging")) ||
            !lease(i->Packed, std::size_t(page) * (d.Capacity + 1u) * 8u, false, "SpatialIndex.QuerySession") ||
            (!d.Excluded.empty() && !lease(i->Exclusions, rows * 4u, true, "SpatialIndex.QuerySession")) ||
            (d.Transformed &&
                (!lease(i->Pose, sizeof(glm::dmat4), false, "SpatialIndex.QueryTransform") ||
                    !lease(i->Posed, std::size_t(page) * 12u, false, "SpatialIndex.QueryTransform"))))
            return fail(s.ScratchDiagnostic(refusals, "GPU query session allocation failed."));
        // The first rows: host-visible queries directly, or the staging an uploading session's first page copies.
        s.Device->WriteBuffer((uploads ? i->Staging : i->Queries).GetHandle(), d.Queries.data(), rows * 12u);
        i->PendingCopy = uploads;
        if (!d.Excluded.empty())
            s.Device->WriteBuffer(i->Exclusions.GetHandle(), d.Excluded.data(), rows * 4u);
        i->UploadBytes.store(rows * 12u + d.Excluded.size() * 4u, std::memory_order_relaxed);
        if (d.Moving)
        {
            // A private target over the query rows: its hierarchy storage (Lbvh) and pipelines now, rebuilt by the
            // worker's first page and after each upload. Its device bytes count in Stats().IndexDeviceBytes.
            // ponytail: a fresh workspace (storage and pipelines) per moving session, created at a run's Init; pool it
            // through LeaseGpuWorkspace if run starts show pipeline creation.
            i->Own = std::make_unique<Graphics::PointLbvhWorkspace>(*s.Device);
            if ((d.Strategy == S::Lbvh && !i->Own->Reserve(std::uint32_t(rows))) ||
                !i->Own->PreparePipelines(d.Strategy == S::BruteForce, false))
                return fail("GPU query session pipelines or hierarchy storage unavailable.");
            i->Workspace = i->Own.get();
            i->Identity.resize(rows);
            for (std::uint32_t row = 0; row < rows; ++row)
                i->Identity[row] = row;
            i->TargetPoints = i->Queries.GetHandle();
            i->NeedsBuild = d.Strategy == S::Lbvh;
        }
        else
        {
            // Always one ordinary owner submission (AfterIssuedTransfers), recording the resident samples' gather and
            // the (Lbvh) hierarchy only when missing: it waits for every graphics-lane producer issued so far (the
            // canonical slot's upload, wherever it came from), so the worker's pages follow it in lane order alone
            // (ComputeInputOrder::LaneOrderOnly). A refused one leaves no session.
            const bool lbvh = d.Strategy == S::Lbvh;
            std::string_view why = "The GPU compute lane refused the query session's preparation before recording.";
            const auto token = s.Submit(
                [&]
                {
                    if (s.PrepareHost(target, lbvh) != entry)
                        why = "GPU query session preparation could not record against a current index.";
                    else if (!entry->Gpu->PreparePipelines(d.Strategy == S::BruteForce, d.Transformed))
                        why = "GPU query session pipelines unavailable.";
                    else
                        return true;
                    return false;
                },
                [&](RHI::ICommandContext& commands, Impl::Producer& producer) -> RHI::BufferHandle
                {
                    if (!lbvh)
                        s.RecordGather(*entry, commands, producer);
                    else if (!s.RecordBuild(*entry, commands, producer))
                    {
                        why = "GPU query session preparation could not record against a current index.";
                        return {};
                    }
                    why = "GPU query session preparation refused after recording.";
                    return entry->Points;
                },
                0u,
                RHI::ReadbackSink::Invoke([](std::span<const std::byte>) {}));
            if (!token.IsValid())
                return fail(s.ScratchDiagnostic(refusals, why));
            ++s.Stats.GpuComputeSubmissions;
            i->Target = *found;
            i->TargetKey = entry;
            i->Workspace = entry->Gpu.get();
            i->Snapshot = entry->Snapshot;
            i->TargetPoints = entry->Points;
            i->TargetMapping = entry->Mapping;
            if (entry->ResidentPoints)
                i->Resident.push_back(entry->Points);
            if (entry->GatherSource)
                i->Resident.push_back(entry->GatherSource->Buffer);
        }
        const auto address = [&](const RHI::BufferHandle buffer)
        {
            return buffer.IsValid() ? s.Device->GetBufferDeviceAddress(buffer) : std::uint64_t{};
        };
        i->QueriesAddress = address(i->Queries.GetHandle());
        i->ExclusionsAddress = address(i->Exclusions.GetHandle());
        i->PackedAddress = address(i->Packed.GetHandle());
        i->PoseAddress = address(i->Pose.GetHandle());
        i->PosedAddress = address(i->Posed.GetHandle());
        i->TargetPointsAddress = address(i->TargetPoints);
        i->TargetMappingAddress = address(i->TargetMapping);
        if (!i->QueriesAddress || !i->PackedAddress || !i->TargetPointsAddress ||
            (i->Exclusions.IsValid() && !i->ExclusionsAddress) ||
            (d.Transformed && (!i->PoseAddress || !i->PosedAddress)) ||
            (i->TargetMapping.IsValid() && !i->TargetMappingAddress))
            return fail("GPU query session buffers have no device address.");
        auto session = std::make_shared<SpatialQuerySession>(std::move(i));
        s.Sessions.push_back(session);
        return session;
    }
    std::shared_ptr<SpatialGpuIndexLease> SpatialIndexCache::PrepareGpuIndex(
        const SpatialIndexHandle handle, std::string& diagnostic)
    {
        auto& s = *m_Impl;
        const auto found = std::ranges::find_if(s.Entries,
            [&](const auto& e)
            {
                return e->Id == handle.Value;
            });
        Impl::Entry* const entry = found != s.Entries.end() && s.Current(**found) ? found->get() : nullptr;
        if (!entry || !GpuQueriesAvailable() || entry->Snapshot->Slots.empty() ||
            entry->Snapshot->Slots.size() > (1u << 20))
        {
            diagnostic = "GPU computation requires a current index, operational device and bounded result.";
            return {};
        }
        // The samples, row map and hierarchy through one ordinary owner submission, recorded only when missing (the
        // build QueueGpuCompute(handle, ...) records); a worker's pages follow it on the same lane.
        if (!entry->Gpu || !entry->Gathered || entry->PointsStale || !entry->Built)
        {
            const auto refusals = s.ScratchRefusals();
            std::string_view why = "The GPU compute lane refused the index preparation before recording.";
            const auto token = s.Submit(
                [&]
                {
                    return s.PrepareHost(handle, true) == entry;
                },
                [&](RHI::ICommandContext& commands, Impl::Producer& producer) -> RHI::BufferHandle
                {
                    why = "GPU index preparation could not record against a current index.";
                    if (!s.RecordBuild(*entry, commands, producer))
                        return {};
                    why = "GPU index preparation refused after recording.";
                    return entry->Points;
                },
                0u,
                RHI::ReadbackSink::Invoke([](std::span<const std::byte>) {}));
            if (!token.IsValid())
            {
                diagnostic = s.ScratchDiagnostic(refusals, why);
                return {};
            }
            ++s.Stats.GpuComputeSubmissions;
        }
        auto i = std::make_unique<SpatialGpuIndexLease::Impl>();
        const auto lbvh = entry->Gpu->View();
        i->View = {.NodesBDA = lbvh.NodesBDA,
            .PositionsBDA = entry->Points.IsValid() ? s.Device->GetBufferDeviceAddress(entry->Points) : 0u,
            .OriginalSlotsBDA = entry->Mapping.IsValid() ? s.Device->GetBufferDeviceAddress(entry->Mapping) : 0u,
            .Count = std::uint32_t(entry->Snapshot->Slots.size())};
        if (!i->View.NodesBDA || !i->View.PositionsBDA || !i->View.OriginalSlotsBDA)
        {
            diagnostic = "The GPU index has no device address.";
            return {};
        }
        for (const auto buffer : {entry->Points, entry->Mapping, lbvh.Storage})
            if (buffer.IsValid())
                i->Buffers.push_back(buffer);
        i->Target = *found;
        i->TargetKey = entry;
        i->Snapshot = entry->Snapshot;
        if (entry->ResidentPoints)
            i->Resident.push_back(entry->Points);
        if (entry->GatherSource)
            i->Resident.push_back(entry->GatherSource->Buffer);
        auto lease = std::make_shared<SpatialGpuIndexLease>(std::move(i));
        s.IndexLeases.push_back(lease);
        return lease;
    }
    std::shared_ptr<SpatialGpuResult> SpatialIndexCache::QueueGpuCompute(SpatialIndexHandle handle,
        std::size_t readbackBytes,
        std::function<RHI::BufferHandle(RHI::ICommandContext&, const SpatialGpuIndexView&)> record)
    {
        auto work = std::make_shared<Impl::Computation>();
        auto& s = *m_Impl;
        const auto* entry = s.Find(handle);
        if (!entry || !GpuQueriesAvailable() || entry->Snapshot->Slots.empty() ||
            entry->Snapshot->Slots.size() > (1u << 20) || !record ||
            readbackBytes > (1u << 28))
        {
            work->Result->State = SpatialQueryState::Failed;
            work->Result->Diagnostic =
                "GPU computation requires a current index, operational device and bounded result.";
            return work->Result;
        }
        work->Target = *std::ranges::find_if(s.Entries, [&](const auto& e) { return e->Id == handle.Value; });
        work->Record = std::move(record);
        work->Result->Data.resize(readbackBytes);
        s.SubmitComputation(work);
        return work->Result;
    }
    std::shared_ptr<SpatialGpuResult> SpatialIndexCache::QueueGpuCompute(std::size_t readbackBytes,
        std::function<RHI::BufferHandle(RHI::ICommandContext&, const SpatialGpuIndexView&)> record)
    {
        auto work = std::make_shared<Impl::Computation>();
        if (!GpuQueriesAvailable() || !record || readbackBytes > (1u << 28))
        {
            work->Result->State = SpatialQueryState::Failed;
            work->Result->Diagnostic = "GPU computation requires an operational device and bounded result.";
            return work->Result;
        }
        work->Record = std::move(record);
        work->Result->Data.resize(readbackBytes);
        m_Impl->SubmitComputation(work);
        return work->Result;
    }
    std::optional<Geometry::PointLBVH::Neighbor> SpatialIndexCache::Nearest(
        SpatialIndexHandle handle, glm::vec3 query, std::uint32_t excludedSlot) const
    {
        const auto* e = m_Impl->Find(handle);
        if (!e || !e->CpuIndex || !Geometry::PointLBVH::ValidPoint(query))
            return {};
        auto result = e->Snapshot->Index.Nearest(query, CompactSlot(*e->Snapshot, excludedSlot));
        if (result.Index != Geometry::PointLBVH::InvalidIndex)
            result.Index = e->Snapshot->Slots[result.Index];
        return result;
    }
    std::optional<Geometry::PointLBVH::RadiusResult> SpatialIndexCache::Radius(
        SpatialIndexHandle handle, glm::vec3 query, float radius, std::uint32_t capacity,
        std::uint32_t excludedSlot) const
    {
        const auto* e = m_Impl->Find(handle);
        if (!e || !e->CpuIndex || !Geometry::PointLBVH::ValidPoint(query) || !std::isfinite(radius) || radius < 0 ||
            radius > Geometry::PointLBVH::CoordinateLimit)
            return {};
        auto result = e->Snapshot->Index.Radius(query, radius, capacity, CompactSlot(*e->Snapshot, excludedSlot));
        for (auto& n : result.Neighbors)
            n.Index = e->Snapshot->Slots[n.Index];
        return result;
    }
    std::optional<std::vector<Geometry::PointLBVH::Neighbor>> SpatialIndexCache::KNearest(
        SpatialIndexHandle handle, glm::vec3 query, std::uint32_t k, std::uint32_t excludedSlot) const
    {
        const auto* e = m_Impl->Find(handle);
        if (!e || !e->CpuIndex || !Geometry::PointLBVH::ValidPoint(query))
            return {};
        auto result = e->Snapshot->Index.KNearest(query, k, CompactSlot(*e->Snapshot, excludedSlot));
        for (auto& neighbor : result) neighbor.Index = e->Snapshot->Slots[neighbor.Index];
        return result;
    }
    void SpatialIndexCache::Prune()
    {
        auto& s = *m_Impl;
        s.Stats.Evictions +=
            std::erase_if(s.Entries, [&](const auto& e) { return !s.Current(*e); });
        // Slots of entities that no longer exist go; a live entity's canonical slot stays for
        // its next GPU user until the residency's idle timeout or byte budget evicts it.
        if (s.Residency)
        {
            s.Residency->Prune([&](const Graphics::GpuPropertyKey& key) {
                const auto* scene = s.Worlds ? s.Worlds->Get(WorldHandle{std::uint32_t(key.Scope & 0xffffffffu), std::uint32_t(key.Scope >> 32u)}) : nullptr;
                return scene && scene->IsValid(entt::entity(std::uint32_t(key.Owner)));
            });
            s.Residency->Tick();
        }
        s.MatureWorkspaces();
    }
    std::shared_ptr<void> SpatialIndexCache::LeaseErasedGpuWorkspace(const void* kind,
        std::shared_ptr<void> (*make)(RHI::IDevice&, RHI::BufferManager&),
        void (*releaseScratch)(void*))
    {
        return m_Impl->LeaseWorkspace(kind, make, releaseScratch);
    }
    RHI::BufferManager* SpatialIndexCache::MethodScratch() noexcept
    {
        return m_Impl->Device ? m_Impl->Scratch.get() : nullptr;
    }
    std::string SpatialIndexCache::MethodScratchDiagnostic(std::uint64_t refusals, std::string_view fallback) const
    {
        return m_Impl->ScratchDiagnostic(refusals, fallback);
    }
    Graphics::GpuPropertyResidency* SpatialIndexCache::PropertyResidency() noexcept
    {
        return m_Impl->EnsureResidency();
    }
    SpatialIndexCache::GpuPositionCommitStatus SpatialIndexCache::CommitGpuPositions(const GpuPositionCommit& c)
    {
        if (!m_Impl->Extraction) return GpuPositionCommitStatus::NotAcknowledged;
        using Status = RenderExtractionCache::PositionCommitStatus;
        switch (m_Impl->Extraction->CommitAcceptedPositions(
            {.StableEntityId = c.StableEntityId, .PositionBytes = c.PositionBytes, .RowCount = c.RowCount,
             .Revision = c.Revision,
             .Front = {.Buffer = c.Front, .Address = c.FrontAddress, .Bytes = c.FrontBytes, .Count = c.FrontCount,
                       .Stamp = c.FrontStamp},
             .FrontLease = c.FrontLease}))
        {
        case Status::Acknowledged: return GpuPositionCommitStatus::Acknowledged;
        case Status::AcknowledgedCopyPending: return GpuPositionCommitStatus::AcknowledgedCopyPending;
        case Status::NotOneToOne:
        case Status::Rejected: break;
        }
        return GpuPositionCommitStatus::NotAcknowledged;
    }
    const Graphics::GpuPropertyResidency* SpatialIndexCache::PropertyResidency() const noexcept
    {
        return m_Impl->Residency.get();
    }
    SpatialIndexCacheStats SpatialIndexCache::Stats() const noexcept
    {
        auto stats = m_Impl->Stats;
        if (m_Impl->Scratch)
        {
            const auto scratch = m_Impl->Scratch->GetScratchStats();
            stats.ScratchActiveBytes = scratch.ActiveBytes;
            stats.ScratchPendingBytes = scratch.PendingBytes;
            stats.ScratchIdleBytes = scratch.IdleBytes;
            stats.ScratchBudgetBytes = scratch.BudgetBytes;
            stats.ScratchHits = scratch.Hits;
            stats.ScratchMisses = scratch.Misses;
            stats.ScratchRefusals = scratch.Refusals;
            stats.ScratchEvictions = scratch.Evictions;
        }
        // Every entry still allocated counts once: the cache's, and one it let go that a session or index lease still
        // holds. Live sessions' traffic (Drain folds a dropped one once) and a moving target's private hierarchy too.
        std::vector<const void*> counted;
        const auto count = [&](const void* key, const bool held)
        {
            if (!key || !held || std::ranges::find(counted, key) != counted.end())
                return;
            counted.push_back(key);
            stats.IndexDeviceBytes += static_cast<const Impl::Entry*>(key)->DeviceBytes();
        };
        for (const auto& entry : m_Impl->Entries)
            count(entry.get(), true);
        for (const auto& lease : m_Impl->IndexLeases)
            count(lease->Internals().TargetKey, lease->Internals().Target != nullptr);
        for (const auto& session : m_Impl->Sessions)
        {
            auto& i = session->Internals();
            if (i.Own)
                stats.IndexDeviceBytes += i.Own->DeviceBytes();
            count(i.TargetKey, i.Target != nullptr);
            if (!i.Stamped)
            {
                stats.GpuQuerySubmissions += i.Pages.load(std::memory_order_acquire);
                stats.GpuQueryTransformUploads += i.Poses.load(std::memory_order_acquire);
                stats.GpuQueryUploadBytes += i.UploadBytes.load(std::memory_order_acquire);
            }
        }
        return stats;
    }
    } // extern "C++"
} // namespace Extrinsic::Runtime
