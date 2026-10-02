module;
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <glm/gtc/quaternion.hpp>
#include <cstdint>
#include <entt/entity/entity.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
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

namespace Extrinsic::Runtime
{
    bool SpatialIndexSnapshotMatches(const SpatialIndexSnapshot* snapshot,
        std::span<const std::uint32_t> slots, std::span<const glm::vec3> points) noexcept
    {
        return snapshot && std::ranges::equal(snapshot->Slots, slots) &&
               std::ranges::equal(points, snapshot->Index.Points());
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
                {
                    matrix = glm::mat4_cast(transform->Rotation);
                    matrix[0] *= transform->Scale.x;
                    matrix[1] *= transform->Scale.y;
                    matrix[2] *= transform->Scale.z;
                    matrix[3] = glm::vec4(transform->Position, 1.f);
                }
            return matrix;
        }
        std::uint32_t CompactSlot(const SpatialIndexSnapshot& snapshot, std::uint32_t slot)
        {
            const auto it = std::ranges::lower_bound(snapshot.Slots, slot);
            return it != snapshot.Slots.end() && *it == slot ?
                std::uint32_t(it - snapshot.Slots.begin()) : Geometry::PointLBVH::InvalidIndex;
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
        // stamps them, and they become leasable once that frame's submissions have retired.
        struct WorkspacePool
        {
            struct Slot
            {
                const void* Kind;
                std::shared_ptr<void> Object{};
                std::uint64_t SafeFrame{};
            };
            std::mutex Mutex{};
            std::vector<Slot> Returned{}; // guarded by Mutex
            std::vector<Slot> Waiting{}, Idle{};
        };
        // The control block behind one lease: the last copy returns the object to its pool.
        struct WorkspaceLease
        {
            const void* Kind;
            std::shared_ptr<void> Object{};
            std::weak_ptr<WorkspacePool> Pool{};
            WorkspaceLease(const void* kind, std::shared_ptr<void> object, std::weak_ptr<WorkspacePool> pool)
                : Kind(kind), Object(std::move(object)), Pool(std::move(pool)) {}
            WorkspaceLease(const WorkspaceLease&) = delete;
            WorkspaceLease& operator=(const WorkspaceLease&) = delete;
            ~WorkspaceLease()
            {
                if (const auto pool = Pool.lock(); pool && Object)
                {
                    std::scoped_lock lock{pool->Mutex};
                    pool->Returned.push_back({Kind, std::move(Object)});
                }
            }
        };
    } // namespace
    extern "C++"
    {
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
            bool GpuStale{}; // UpdateWorkspace replaced the points; rebuild into the same buffers
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
        std::unique_ptr<Graphics::GpuPropertyResidency> Residency{};
        Graphics::GpuPropertyResidencyConfig ResidencyConfig{};
        std::vector<std::shared_ptr<Entry>> Entries{};
        JobService* Jobs{};
        GpuQueueParticipantHandle Participant{};
        struct Batch
        {
            explicit Batch(RHI::IDevice& device) : Device(&device) {}
            std::shared_ptr<SpatialNearestBatch> State{std::make_shared<SpatialNearestBatch>()};
            std::shared_ptr<Entry> Target{};
            std::vector<glm::vec3> Queries{};
            std::vector<std::uint32_t> Headers{}, Excluded{};
            std::uint32_t Capacity{1};
            float Radius{-1.f};
            RHI::IDevice* Device{};
            RHI::BufferHandle Input{}, Output{}, Header{}, Exclusions{}, TransformBuffer{}, TransformedQueries{}, PackedResult{};
            std::optional<glm::dmat4> Transform{};
            std::size_t TransformedRows{}, PackedRows{};
            bool InputDirty{true};
            // Allocated rows: queries, and neighbor entries (queries x capacity). A reused batch
            // may serve any request that fits (GRAPHICS-153), e.g. a shorter last page.
            std::size_t QueryRows{}, NeighborRows{};
            std::uint64_t SubmittedFrame{};
            unsigned Downloads{};
            bool DownloadQueued{};
            ~Batch()
            {
                if (Device)
                    for (auto buffer : {Input, Output, Header, Exclusions, TransformBuffer, TransformedQueries, PackedResult})
                        if (buffer.IsValid()) Device->DestroyBuffer(buffer);
            }
        };
        std::vector<std::shared_ptr<Batch>> Batches{};
        struct Computation
        {
            std::shared_ptr<SpatialGpuResult> Result{std::make_shared<SpatialGpuResult>()};
            std::shared_ptr<Entry> Target{};
            std::function<RHI::BufferHandle(RHI::ICommandContext&, const SpatialGpuIndexView&)> Record{};
            RHI::BufferHandle Output{};
            std::uint64_t SubmittedFrame{};
            bool DownloadQueued{};
            // The recorder ran (it may have recorded commands even when it then failed), into a
            // frame's command buffer, and whether the device is known to be done with them.
            bool Recorded{}, Framed{}, Completed{};
        };
        std::vector<std::shared_ptr<Computation>> Computations{};
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
            const auto frame = Device->GetGlobalFrameNumber();
            for (auto& slot : returned)
            {
                slot.SafeFrame = frame + Device->GetFramesInFlight() + 1u;
                Pool->Waiting.push_back(std::move(slot));
            }
            for (auto it = Pool->Waiting.begin(); it != Pool->Waiting.end();)
            {
                if (frame < it->SafeFrame) { ++it; continue; }
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
        }
        // Delivers a computation's readback bytes (framed download or immediate submit).
        static RHI::ReadbackSink ComputationSink(const std::shared_ptr<Computation>& work)
        {
            return RHI::ReadbackSink::Invoke([work](std::span<const std::byte> data) {
                work->Completed = true;
                if (data.size() != work->Result->Data.size())
                {
                    work->Result->Diagnostic = "GPU computation returned an incomplete result.";
                    work->Result->State = SpatialQueryState::Failed;
                    return;
                }
                if (!data.empty()) std::memcpy(work->Result->Data.data(), data.data(), data.size());
                work->Result->State = SpatialQueryState::Ready;
            });
        }
        // GRAPHICS-150: submits the computation on its own command buffer at once. Only an index
        // that is already built qualifies (a build recorded here would be lost if the submit
        // were refused); false leaves the work for the frame.
        bool SubmitImmediate(const std::shared_ptr<Computation>& work)
        {
            if (work->Target && !(work->Target->Gpu && work->Target->Gpu->View().NodesBDA)) return false;
            bool recorded = false;
            const auto record = [this, work, &recorded](RHI::ICommandContext& commands) {
                recorded = true;
                if (!work->Target) return work->Record(commands, {});
                const auto& e = *work->Target;
                return work->Record(commands, {e.Gpu->View().NodesBDA, Device->GetBufferDeviceAddress(e.Points),
                                               Device->GetBufferDeviceAddress(e.Mapping),
                                               std::uint32_t(e.Snapshot->Slots.size())});
            };
            const bool submitted = Device->SubmitComputeReadback(record, work->Result->Data.size(), ComputationSink(work)).IsValid();
            // An invalid token guarantees that no commands were submitted.
            work->Recorded = submitted;
            if (!submitted)
            {
                // A recorder can advance a chunk cursor. Replaying it after submission refusal
                // would skip work that never reached the device. Only unsupported calls fall back.
                if (recorded)
                {
                    work->Result->State = SpatialQueryState::Failed;
                    work->Result->Diagnostic = "GPU compute submission refused after recording.";
                }
                return false;
            }
            work->DownloadQueued = true;
            work->SubmittedFrame = Device->GetGlobalFrameNumber();
            work->Result->State = SpatialQueryState::Submitted;
            return true;
        }
        void ShutdownBatches()
        {
            // Called after the participant's device-idle fence. Deliver pending sinks
            // before releasing their device-owned source buffers.
            if (Device && (!Batches.empty() || !Computations.empty())) Device->GetTransferQueue().CollectCompleted();
            for (auto& batch : Batches)
            {
                batch->State->State = SpatialQueryState::Failed;
                batch->State->Diagnostic = "Spatial query service stopped.";
            }
            Batches.clear();
            for (auto& work : Computations)
            {
                work->Result->State = SpatialQueryState::Failed;
                work->Result->Diagnostic = "Spatial compute service stopped.";
            }
            Computations.clear();
            ReleaseWorkspacesAfterDeviceIdle();
        }
        void Drain()
        {
            for (auto& work : Computations)
            {
                // A recorder that failed in a frame: done once that frame has retired.
                if (work->Result->State == SpatialQueryState::Failed && work->Framed && !work->Completed &&
                    Device->GetGlobalFrameNumber() >= work->SubmittedFrame + Device->GetFramesInFlight() + 1u)
                    work->Completed = true;
                if (work->Result->State != SpatialQueryState::Submitted || work->DownloadQueued ||
                    Device->GetGlobalFrameNumber() < work->SubmittedFrame + Device->GetFramesInFlight() +
                        (work->Result->Data.empty() ? 1u : 0u)) continue;
                work->DownloadQueued = true;
                work->Completed = true; // its frame has retired
                if (work->Result->Data.empty())
                {
                    work->Result->State = SpatialQueryState::Ready;
                    continue;
                }
                const auto ticket = Device->GetTransferQueue().DownloadBuffer(
                    work->Output, work->Result->Data.size(), 0, ComputationSink(work));
                if (!ticket.IsValid())
                {
                    work->Result->State = SpatialQueryState::Failed;
                    work->Result->Diagnostic = "GPU computation readback submission failed.";
                }
            }
            std::erase_if(Computations, [this](const auto& work) {
                const auto state = work->Result->State;
                if ((state != SpatialQueryState::Ready && state != SpatialQueryState::Failed) || work.use_count() != 1)
                    return false;
                if (state == SpatialQueryState::Failed && work->Recorded && !work->Completed)
                    return false; // a failed frame recorder may still have commands in flight
                return true;
            });
            for (auto& batch : Batches)
            {
                if (batch->State->State != SpatialQueryState::Submitted) continue;
                if (batch->DownloadQueued)
                {
                    if (batch->Downloads == 2)
                    {
                        bool valid = true;
                        for (std::size_t i = 0; i < batch->Queries.size(); ++i)
                        {
                            const auto available = batch->Target->Snapshot->Slots.size() -
                                (CompactSlot(*batch->Target->Snapshot, batch->Excluded[i]) != Geometry::PointLBVH::InvalidIndex);
                            const auto expected = std::min(std::size_t(batch->Capacity), available);
                            valid &= batch->Headers[2*i+1] == 0 && (batch->Radius < 0
                                ? batch->Headers[2*i] == expected : batch->Headers[2*i] <= available);
                            batch->State->Counts[i] = batch->Headers[2*i];
                        }
                        batch->State->State = valid ? SpatialQueryState::Ready : SpatialQueryState::Failed;
                        if (!valid) batch->State->Diagnostic = "GPU spatial query reported invalid input.";
                    }
                    continue;
                }
                if (Device->GetGlobalFrameNumber() < batch->SubmittedFrame + Device->GetFramesInFlight())
                    continue;
                batch->DownloadQueued = true;
                auto download = [&](RHI::BufferHandle buffer, std::size_t bytes, bool headers) {
                    return Device->GetTransferQueue().DownloadBuffer(buffer, bytes, 0,
                        RHI::ReadbackSink::Invoke([batch, headers](std::span<const std::byte> data) {
                            const auto expected = headers ? batch->Headers.size() * sizeof(std::uint32_t)
                                : batch->State->Neighbors.size() * sizeof(Geometry::PointLBVH::Neighbor);
                            if (data.size() != expected)
                            {
                                batch->State->State = SpatialQueryState::Failed;
                                batch->State->Diagnostic = "GPU spatial query returned an incomplete result.";
                                return;
                            }
                            void* destination = headers ? static_cast<void*>(batch->Headers.data())
                                                       : static_cast<void*>(batch->State->Neighbors.data());
                            std::memcpy(destination, data.data(), data.size());
                            ++batch->Downloads;
                        })).IsValid();
                };
                const bool neighbors = download(batch->Output, batch->Queries.size()*batch->Capacity*8, false);
                const bool headers = download(batch->Header, batch->Queries.size()*8, true);
                if (!neighbors || !headers)
                {
                    batch->State->State = SpatialQueryState::Failed;
                    batch->State->Diagnostic = "GPU spatial readback submission failed.";
                }
            }
            std::erase_if(Batches, [](const auto& b) {
                const bool finished = b->State.use_count() == 1 && b->State->State != SpatialQueryState::Queued &&
                    b->State->State != SpatialQueryState::Submitted && b.use_count() == 1;
                if (finished) b->Target.reset(); // the pooled scratch does not retain a scene index
                return finished;
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
        // Private recording path: the queue participant is the only caller. Records a
        // lazy GPU build then queries into batch-owned buffers; never silently runs CPU.
        bool RecordBuild(SpatialIndexHandle handle, RHI::ICommandContext& commands, std::uint64_t* hostBytes = nullptr)
        {
            auto& s = *this;
            auto* e = s.Find(handle);
            if (!e || !s.Device || !s.Device->IsOperational())
                return false;
            e->Device = s.Device;
            if (!e->Gpu)
            {
                e->Gpu = std::make_unique<Graphics::PointLbvhWorkspace>(*s.Device);
                if (!e->Gpu->Reserve(e->Snapshot->Slots.size()))
                {
                    e->Gpu.reset();
                    return false;
                }
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
                    if (!source) { e->Gpu.reset(); return false; }
                    const auto view = s.EnsureResidency()->AcquireInput(
                        MakeGpuPropertyKey(e->World, e->Entity, e->Ref), e->Revision,
                        *MakeGpuPropertyLayout(Geometry::PropertyValueKind::Vec3, std::uint32_t(e->Size)),
                        [&](std::span<std::byte> out) { std::memcpy(out.data(), source->Points.Span().data(), out.size()); });
                    if (!view) { e->Gpu.reset(); return false; }
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
                    return false;
                }
                if (!e->Snapshot->Slots.empty())
                {
                    if (!e->ResidentPoints && !e->GatherSource)
                        s.Device->WriteBuffer(e->Points, e->Snapshot->Index.Points().data(), e->Snapshot->Slots.size() * 12);
                    s.Device->WriteBuffer(e->Mapping, e->Snapshot->Slots.data(), e->Snapshot->Slots.size() * 4);
                    if(hostBytes)*hostBytes+=e->Snapshot->Slots.size()*(4+(!e->ResidentPoints&&!e->GatherSource?12:0));
                }
            }
            else if (e->GpuStale)
            {
                // Same count and identity slots: only the points change.
                s.Device->WriteBuffer(e->Points, e->Snapshot->Index.Points().data(), e->Snapshot->Slots.size() * 12);
            }
            // The build and every query read the canonical slot in this frame.
            if (e->ResidentPoints && s.Residency) s.Residency->NoteUse(e->Points, s.Device->GetGlobalFrameNumber());
            if (!e->Gpu->View().NodesBDA || e->GpuStale)
            {
                if (e->GatherSource)
                {
                    const auto& source = *e->GatherSource;
                    s.Residency->NoteUse(source.Buffer, s.Device->GetGlobalFrameNumber());
                    commands.BufferBarrier(source.Buffer, RHI::MemoryAccess::TransferWrite | RHI::MemoryAccess::ShaderWrite,
                                           RHI::MemoryAccess::TransferRead);
                    commands.BufferBarrier(e->Points, RHI::MemoryAccess::ShaderRead, RHI::MemoryAccess::TransferWrite);
                    const auto& slots = e->Snapshot->Slots;
                    for (std::size_t first = 0; first < slots.size();)
                    {
                        std::size_t end = first + 1;
                        while (end < slots.size() && slots[end] == slots[first] + end - first) ++end;
                        commands.CopyBuffer(source.Buffer, e->Points, std::uint64_t(slots[first]) * 12,
                                            first * 12, (end - first) * 12);
                        first = end;
                    }
                }
                e->GpuStale = false;
                if (!e->Gpu->RecordBuild(commands,
                                         {.Buffer = e->Points, .Count = std::uint32_t(e->Snapshot->Slots.size())},
                                         e->Mapping))
                    return false;
                ++s.Stats.GpuBuilds;
            }
            return true;
        }
        bool RecordBatch(const std::shared_ptr<Batch>& batch, RHI::ICommandContext& commands, bool packed)
        {
            if (batch->InputDirty)
            {
                Device->WriteBuffer(batch->Input, batch->Queries.data(), batch->Queries.size()*12);
                Stats.GpuQueryUploadBytes += batch->Queries.size()*12;
                batch->InputDirty = false;
            }
            if (batch->Transform)
            {
                Device->WriteBuffer(batch->TransformBuffer, &*batch->Transform, sizeof(glm::dmat4));
                Stats.GpuQueryUploadBytes += sizeof(glm::dmat4);
                ++Stats.GpuQueryTransformUploads;
            }
            else
            {
                Device->WriteBuffer(batch->Exclusions, batch->Excluded.data(), batch->Excluded.size()*4);
                Stats.GpuQueryUploadBytes += batch->Excluded.size()*4;
            }
            return RecordQueries({batch->Target->Id}, commands,
                {.Queries = {.Buffer = batch->Input, .Count = std::uint32_t(batch->Queries.size())},
                 .Neighbors = packed ? batch->PackedResult : batch->Output,
                 .Headers = packed ? batch->PackedResult : batch->Header,
                 .Capacity = batch->Capacity, .Radius = batch->Radius,
                 .KNearestCount = batch->Radius < 0 ? batch->Capacity : 0u,
                 .ExcludedIndices = batch->Transform ? RHI::BufferHandle{} : batch->Exclusions,
                 .Transform = batch->Transform ? batch->TransformBuffer : RHI::BufferHandle{},
                 .TransformedQueries = batch->Transform ? batch->TransformedQueries : RHI::BufferHandle{},
                 .HeaderOffset = packed ? batch->Queries.size()*8u : 0u});
        }
        void SubmitBatchImmediate(const std::shared_ptr<Batch>& batch)
        {
            // Never build the target inside a submission that may be refused: its workspace
            // otherwise could report a build that was recorded but never executed.
            if (!batch->Transform || !batch->Target->Gpu || !batch->Target->Gpu->View().NodesBDA ||
                batch->Target->GpuStale) return;
            bool recorded = false;
            const auto token = Device->SubmitComputeReadback(
                [this, batch, &recorded](RHI::ICommandContext& commands) {
                    recorded = true;
                    return RecordBatch(batch, commands, true) ? batch->PackedResult : RHI::BufferHandle{};
                }, batch->Queries.size()*16u,
                RHI::ReadbackSink::Invoke([batch](std::span<const std::byte> data) {
                    const auto bytes = batch->Queries.size()*8u;
                    if (data.size() != bytes*2u)
                    {
                        batch->State->State = SpatialQueryState::Failed;
                        batch->State->Diagnostic = "GPU spatial query returned an incomplete result.";
                        return;
                    }
                    std::memcpy(batch->State->Neighbors.data(), data.data(), bytes);
                    std::memcpy(batch->Headers.data(), data.data()+bytes, bytes);
                    batch->Downloads = 2u;
                }));
            if (!token.IsValid())
            {
                if (recorded)
                {
                    batch->State->State = SpatialQueryState::Failed;
                    batch->State->Diagnostic = "GPU spatial submission refused after recording.";
                }
                return; // unsupported immediate submission stays queued for the frame
            }
            batch->DownloadQueued = true;
            batch->SubmittedFrame = Device->GetGlobalFrameNumber();
            if (batch->State->State != SpatialQueryState::Failed)
                batch->State->State = SpatialQueryState::Submitted;
            ++Stats.GpuQueryImmediateSubmissions;
        }
        bool RecordQueries(SpatialIndexHandle handle, RHI::ICommandContext& commands,
                           const Graphics::PointLbvhQuery& query)
        {
            if (!RecordBuild(handle, commands))
                return false;
            return Find(handle)->Gpu->RecordQuery(commands, query);
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
        auto result = setup.Services().Provide<SpatialIndexCache>(*this, Name());
        if (!result)
            return result;
        m_Impl->Jobs = &setup.Jobs();
        if (m_Impl->Jobs && m_Impl->Device)
            m_Impl->Participant = m_Impl->Jobs->RegisterGpuQueueParticipant({
                .DebugName = "Runtime.SpatialIndex.Queries",
                .RecordFrameCommands = [this](RHI::ICommandContext& commands) {
                    for (auto& work : m_Impl->Computations)
                    {
                        if (work->Result->State != SpatialQueryState::Queued) continue;
                        work->Framed = true;
                        work->SubmittedFrame = m_Impl->Device->GetGlobalFrameNumber();
                        if (!work->Target)
                        {
                            work->Recorded = true;
                            work->Output = work->Record(commands, {});
                        }
                        else if (auto& e = *work->Target; m_Impl->RecordBuild({e.Id}, commands, &work->Result->CpuStageUploadBytes))
                        {
                            work->Recorded = true;
                            work->Output = work->Record(commands,
                                {e.Gpu->View().NodesBDA,
                                 m_Impl->Device->GetBufferDeviceAddress(e.Points),
                                 m_Impl->Device->GetBufferDeviceAddress(e.Mapping),
                                 std::uint32_t(e.Snapshot->Slots.size())});
                        }
                        if (!work->Output.IsValid())
                        {
                            work->Result->State = SpatialQueryState::Failed;
                            work->Result->Diagnostic = "GPU computation could not record against a current index.";
                            continue;
                        }
                        work->SubmittedFrame = m_Impl->Device->GetGlobalFrameNumber();
                        work->Result->State = SpatialQueryState::Submitted;
                    }
                    for (auto& batch : m_Impl->Batches)
                    {
                        if (batch->State->State != SpatialQueryState::Queued) continue;
                        if (!m_Impl->RecordBatch(batch, commands, false))
                        {
                            batch->State->State = SpatialQueryState::Failed;
                            batch->State->Diagnostic = "GPU spatial query could not record against a current index.";
                            continue;
                        }
                        batch->SubmittedFrame = m_Impl->Device->GetGlobalFrameNumber();
                        batch->State->State = SpatialQueryState::Submitted;
                    }
                },
                .DrainCompletedTransfers = [this]() { m_Impl->Drain(); },
                .HasInFlightWork = [this]() {
                    return !m_Impl->Batches.empty() || !m_Impl->Computations.empty() ||
                           m_Impl->HoldsWorkspaces();
                },
                .ShutdownAfterDeviceIdle = [this]() { m_Impl->ShutdownBatches(); m_Impl->Participant = {}; }
            });
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
        else if (m_Impl->Device && m_Impl->HoldsWorkspaces())
            m_Impl->Device->WaitIdle();
        m_Impl->ShutdownBatches();
        m_Impl->Jobs = nullptr;
        m_Impl->Entries.clear();
        m_Impl->Residency.reset(); // after the entries release their leases
        m_Impl->Device = nullptr;
        m_Impl->Worlds = nullptr;
        (void)context.Services.Withdraw<SpatialIndexCache>(*this);
    }
    SpatialIndexAcquisition SpatialIndexCache::Acquire(WorldHandle world, entt::entity entity,
                                                       const GeometryPropertyRef& ref, SpatialIndexSpace space)
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
                ++s.Stats.Hits;
                return {{(*old)->Id}, true, {}};
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
        if (!e->Snapshot->Index.Build(points))
            return {.Diagnostic = "LBVH requires finite coordinates within +/-1e18 and at most "
                                  "2^24 live points."};
        e->Id = s.Next++;
        const auto id = e->Id;
        s.Entries.push_back(std::move(e));
        ++s.Stats.Builds;
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
        if (std::ranges::any_of(s.Batches, [&](const auto& b) { return b->Target == e && busy(b->State->State); }) ||
            std::ranges::any_of(s.Computations, [&](const auto& c) { return c->Target == e && busy(c->Result->State); }))
            return false;
        auto snapshot = std::make_shared<SpatialIndexSnapshot>();
        if (!snapshot->Index.Build(positions)) return false;
        snapshot->Slots = e->Snapshot->Slots; // identity
        e->Snapshot = snapshot;
        workspace.Snapshot = std::move(snapshot);
        e->GpuStale = e->Gpu != nullptr;
        ++s.Stats.WorkspaceUpdates;
        return true;
    }
    std::shared_ptr<const SpatialIndexSnapshot> SpatialIndexCache::Snapshot(SpatialIndexHandle handle) const
    {
        const auto* entry = m_Impl->Find(handle);
        return entry ? entry->Snapshot : nullptr;
    }
    std::shared_ptr<SpatialNearestBatch> SpatialIndexCache::QueueGpuNearest(
        SpatialIndexHandle handle, std::span<const glm::vec3> queries,
        std::shared_ptr<SpatialNearestBatch> reuse)
    {
        return QueueGpuKNearest(handle, queries, 1, {}, std::move(reuse));
    }
    std::shared_ptr<SpatialNearestBatch> SpatialIndexCache::QueueGpuNearestTransformed(
        SpatialIndexHandle handle, std::span<const glm::vec3> source, const glm::dmat4& transform,
        std::shared_ptr<SpatialNearestBatch> reuse)
    {
        return QueueGpuBatch(handle, source, 1, -1.f, {}, std::move(reuse), &transform);
    }
    std::shared_ptr<SpatialNearestBatch> SpatialIndexCache::QueueGpuKNearest(
        SpatialIndexHandle handle, std::span<const glm::vec3> queries, std::uint32_t k,
        std::span<const std::uint32_t> excludedSlots, std::shared_ptr<SpatialNearestBatch> reuse)
    {
        return QueueGpuBatch(handle, queries, k, -1.f, excludedSlots, std::move(reuse));
    }
    std::shared_ptr<SpatialNearestBatch> SpatialIndexCache::QueueGpuRadius(
        SpatialIndexHandle handle, std::span<const glm::vec3> queries, float radius,
        std::uint32_t capacity, std::span<const std::uint32_t> excludedSlots,
        std::shared_ptr<SpatialNearestBatch> reuse)
    {
        if (!std::isfinite(radius) || radius < 0 || radius > Geometry::PointLBVH::CoordinateLimit)
        {
            auto result = std::make_shared<SpatialNearestBatch>();
            result->State = SpatialQueryState::Failed;
            result->Diagnostic = "GPU radius must be finite and in [0, 1e18].";
            return result;
        }
        return QueueGpuBatch(handle, queries, capacity, radius, excludedSlots, std::move(reuse));
    }
    bool SpatialIndexCache::GpuQueriesAvailable() const noexcept
    {
        return m_Impl->Device && m_Impl->Device->IsOperational() && m_Impl->Participant.IsValid();
    }
    std::shared_ptr<SpatialNearestBatch> SpatialIndexCache::QueueGpuBatch(
        SpatialIndexHandle handle, std::span<const glm::vec3> queries, std::uint32_t k, float radius,
        std::span<const std::uint32_t> excludedSlots, std::shared_ptr<SpatialNearestBatch> reuse,
        const glm::dmat4* transform)
    {
        auto& s = *m_Impl;
        auto fail = [](std::string diagnostic) {
            auto result = std::make_shared<SpatialNearestBatch>();
            result->State = SpatialQueryState::Failed;
            result->Diagnostic = std::move(diagnostic);
            return result;
        };
        const auto* entry = s.Find(handle);
        if (!entry || !s.Device || !s.Device->IsOperational() || !s.Participant.IsValid() ||
            entry->Snapshot->Slots.empty() || entry->Snapshot->Slots.size() > (1u << 20) ||
            queries.empty() || queries.size() > (1u << 20) || k == 0 || k > (radius < 0 ? 64u : 1024u) ||
            (!excludedSlots.empty() && excludedSlots.size() != queries.size()) ||
            !std::ranges::all_of(queries, Geometry::PointLBVH::ValidPoint))
            return fail("Vulkan queries require an operational framed device, a current target (1..2^20 rows), finite queries (1..2^20), k in 1..64 or radius capacity in 1..1024, and zero or query-count exclusions.");
        if (transform)
        {
            if (!s.Device->SupportsShaderFloat64()) return fail("Transformed queries require shader float64.");
            for (unsigned column = 0; column < 4; ++column)
                for (unsigned row = 0; row < 4; ++row)
                    if (!std::isfinite((*transform)[column][row])) return fail("Query transform must be finite.");
        }
        std::shared_ptr<Impl::Batch> batch;
        bool completedSameTarget = false;
        if (reuse)
        {
            auto found = std::ranges::find_if(s.Batches, [&](const auto& b) { return b->State == reuse; });
            if (found == s.Batches.end() || reuse->State != SpatialQueryState::Ready)
                return fail("Only a completed batch can be reused.");
            batch = *found;
            completedSameTarget = batch->Target && batch->Target->Id == handle.Value;
            // Keep pooled ICP scratch bounded to nearest-query storage. A radius/kNN request
            // starts its own non-pooled batch rather than growing this lease to k*N neighbors.
            if (!transform && batch->Transform) batch.reset();
        }
        if (!batch)
        {
            batch = transform ? LeaseGpuWorkspace<Impl::Batch>() : std::make_shared<Impl::Batch>(*s.Device);
            if (!batch) return fail("GPU query workspace unavailable.");
            s.Batches.push_back(batch);
        }
        const bool allocateInput = !batch->Input.IsValid() || batch->QueryRows < queries.size();
        const bool allocateOutput = !batch->Output.IsValid() || batch->NeighborRows < queries.size()*k;
        if (allocateInput || allocateOutput || !batch->Header.IsValid() || !batch->Exclusions.IsValid())
        {
            auto ensure = [&](RHI::BufferHandle& buffer, std::size_t bytes, bool host, bool grow) {
                if (buffer.IsValid() && !grow) return true;
                const auto replacement = s.Device->CreateBuffer({.SizeBytes = bytes,
                    .Usage = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferSrc | RHI::BufferUsage::TransferDst,
                    .HostVisible = host, .DebugName = "SpatialIndex.QueryBatch"});
                if (!replacement.IsValid()) return false;
                if (buffer.IsValid()) s.Device->DestroyBuffer(buffer);
                buffer = replacement;
                return true;
            };
            batch->InputDirty = batch->InputDirty || allocateInput;
            if (!ensure(batch->Input, queries.size()*12, true, allocateInput) ||
                !ensure(batch->Output, queries.size()*k*8, false, allocateOutput) ||
                !ensure(batch->Header, queries.size()*8, false, allocateInput) ||
                !ensure(batch->Exclusions, queries.size()*4, true, allocateInput))
            {
                batch->State->State = SpatialQueryState::Failed;
                return fail("GPU spatial batch allocation failed.");
            }
            batch->QueryRows = std::max(batch->QueryRows, queries.size());
            batch->NeighborRows = std::max(batch->NeighborRows, queries.size()*k);
            ++s.Stats.GpuBatchAllocations;
        }
        if (transform)
        {
            auto allocate = [&](std::size_t bytes, bool host) {
                return s.Device->CreateBuffer({.SizeBytes = bytes,
                    .Usage = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferDst,
                    .HostVisible = host, .DebugName = "SpatialIndex.QueryTransform"});
            };
            if (!batch->TransformBuffer.IsValid()) batch->TransformBuffer = allocate(sizeof(glm::dmat4), true);
            if (batch->TransformedRows < queries.size())
            {
                const auto replacement = allocate(queries.size()*12, false);
                if (!replacement.IsValid())
                {
                    batch->State->State = SpatialQueryState::Failed;
                    return fail("GPU transformed query allocation failed.");
                }
                if (batch->TransformedQueries.IsValid()) s.Device->DestroyBuffer(batch->TransformedQueries);
                batch->TransformedQueries = replacement;
                batch->TransformedRows = queries.size();
            }
            if (batch->PackedRows < queries.size())
            {
                const auto replacement = s.Device->CreateBuffer({.SizeBytes = queries.size()*16u,
                    .Usage = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferSrc,
                    .DebugName = "SpatialIndex.PackedCorrespondences"});
                if (!replacement.IsValid())
                {
                    batch->State->State = SpatialQueryState::Failed;
                    return fail("GPU packed query allocation failed.");
                }
                if (batch->PackedResult.IsValid()) s.Device->DestroyBuffer(batch->PackedResult);
                batch->PackedResult = replacement;
                batch->PackedRows = queries.size();
            }
            if (!batch->TransformBuffer.IsValid())
            {
                batch->State->State = SpatialQueryState::Failed;
                return fail("GPU query transform allocation failed.");
            }
        }
        batch->InputDirty = batch->InputDirty || allocateInput || !transform || batch->Queries.size() != queries.size() ||
            !std::equal(batch->Queries.begin(), batch->Queries.end(), queries.begin(), queries.end());
        batch->Transform = transform ? std::optional{*transform} : std::nullopt;
        batch->Target = *std::ranges::find_if(s.Entries, [&](const auto& e) { return e->Id == handle.Value; });
        batch->Queries.assign(queries.begin(), queries.end());
        batch->Capacity = k;
        batch->Radius = radius;
        batch->Excluded.assign(queries.size(), Geometry::PointLBVH::InvalidIndex);
        if (!excludedSlots.empty()) std::ranges::copy(excludedSlots, batch->Excluded.begin());
        batch->Headers.assign(queries.size()*2, 0);
        batch->State->Counts.assign(queries.size(), 0);
        batch->State->Capacity = k;
        batch->State->Diagnostic.clear();
        batch->State->Neighbors.resize(queries.size()*k);
        batch->State->State = SpatialQueryState::Queued;
        batch->Downloads = 0;
        batch->DownloadQueued = false;
        if (completedSameTarget) s.SubmitBatchImmediate(batch);
        return batch->State;
    }
    std::shared_ptr<SpatialGpuResult> SpatialIndexCache::QueueGpuCompute(
        SpatialIndexHandle handle, std::size_t readbackBytes,
        std::function<RHI::BufferHandle(RHI::ICommandContext&, const SpatialGpuIndexView&)> record,
        const SpatialGpuLatency latency)
    {
        auto work = std::make_shared<Impl::Computation>();
        auto& s = *m_Impl;
        const auto* entry = s.Find(handle);
        if (!entry || !GpuQueriesAvailable() || entry->Snapshot->Slots.empty() ||
            entry->Snapshot->Slots.size() > (1u << 20) || !record ||
            readbackBytes > (1u << 28))
        {
            work->Result->State = SpatialQueryState::Failed;
            work->Result->Diagnostic = "GPU computation requires a current index, framed device and bounded result.";
            return work->Result;
        }
        work->Target = *std::ranges::find_if(s.Entries, [&](const auto& e) { return e->Id == handle.Value; });
        work->Record = std::move(record);
        work->Result->Data.resize(readbackBytes);
        if (latency == SpatialGpuLatency::Immediate) (void)s.SubmitImmediate(work);
        s.Computations.push_back(work);
        return work->Result;
    }
    std::shared_ptr<SpatialGpuResult> SpatialIndexCache::QueueGpuCompute(
        std::size_t readbackBytes,
        std::function<RHI::BufferHandle(RHI::ICommandContext&, const SpatialGpuIndexView&)> record,
        const SpatialGpuLatency latency)
    {
        auto work = std::make_shared<Impl::Computation>();
        if (!GpuQueriesAvailable() || !record || readbackBytes > (1u << 28))
        {
            work->Result->State = SpatialQueryState::Failed;
            work->Result->Diagnostic = "GPU computation requires a framed device and bounded result.";
            return work->Result;
        }
        work->Record = std::move(record);
        work->Result->Data.resize(readbackBytes);
        if (latency == SpatialGpuLatency::Immediate) (void)m_Impl->SubmitImmediate(work);
        m_Impl->Computations.push_back(work);
        return work->Result;
    }
    std::optional<Geometry::PointLBVH::Neighbor> SpatialIndexCache::Nearest(
        SpatialIndexHandle handle, glm::vec3 query, std::uint32_t excludedSlot) const
    {
        const auto* e = m_Impl->Find(handle);
        if (!e || !Geometry::PointLBVH::ValidPoint(query))
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
        if (!e || !Geometry::PointLBVH::ValidPoint(query) || !std::isfinite(radius) || radius < 0 ||
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
        if (!e || !Geometry::PointLBVH::ValidPoint(query)) return {};
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
                                                                    std::shared_ptr<void> (*make)(RHI::IDevice&))
    {
        auto& s = *m_Impl;
        if (!s.Device || !make) return {};
        s.MatureWorkspaces();
        std::shared_ptr<void> object;
        const auto idle = std::ranges::find_if(s.Pool->Idle, [&](const auto& slot) { return slot.Kind == kind; });
        const bool reused = idle != s.Pool->Idle.end();
        if (reused)
        {
            object = std::move(idle->Object);
            s.Pool->Idle.erase(idle);
        }
        else object = make(*s.Device);
        if (!object) return {};
        ++s.Stats.WorkspaceLeases;
        if (reused) ++s.Stats.WorkspaceReuses;
        auto lease = std::make_shared<WorkspaceLease>(kind, std::move(object), s.Pool);
        void* const raw = lease->Object.get();
        return std::shared_ptr<void>(std::move(lease), raw);
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
        return m_Impl->Stats;
    }
    } // extern "C++"
} // namespace Extrinsic::Runtime
