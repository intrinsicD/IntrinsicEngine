module;
#include <algorithm>
#include <cmath>
#include <cstring>
#include <glm/gtc/quaternion.hpp>
#include <cstdint>
#include <entt/entity/entity.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>
module Extrinsic.Runtime.SpatialIndexCache;
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
    namespace
    {
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
    } // namespace
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
            std::unique_ptr<Graphics::PointLbvhWorkspace> Gpu{};
            ~Entry()
            {
                if (Device)
                {
                    if (Points.IsValid())
                        Device->DestroyBuffer(Points);
                    if (Mapping.IsValid())
                        Device->DestroyBuffer(Mapping);
                }
            }
        };
        WorldRegistry* Worlds{};
        RHI::IDevice* Device{};
        std::vector<std::shared_ptr<Entry>> Entries{};
        JobService* Jobs{};
        GpuQueueParticipantHandle Participant{};
        struct Batch
        {
            std::shared_ptr<SpatialNearestBatch> State{std::make_shared<SpatialNearestBatch>()};
            std::shared_ptr<Entry> Target{};
            std::vector<glm::vec3> Queries{};
            std::vector<std::uint32_t> Headers{}, Excluded{};
            std::uint32_t Capacity{1};
            float Radius{-1.f};
            RHI::IDevice* Device{};
            RHI::BufferHandle Input{}, Output{}, Header{}, Exclusions{};
            std::uint64_t SubmittedFrame{};
            unsigned Downloads{};
            bool DownloadQueued{};
            ~Batch()
            {
                if (Device)
                    for (auto buffer : {Input, Output, Header, Exclusions})
                        if (buffer.IsValid()) Device->DestroyBuffer(buffer);
            }
        };
        std::vector<std::shared_ptr<Batch>> Batches{};
        void ShutdownBatches()
        {
            // Called after the participant's device-idle fence. Deliver pending sinks
            // before releasing their device-owned source buffers.
            if (Device && !Batches.empty()) Device->GetTransferQueue().CollectCompleted();
            for (auto& batch : Batches)
            {
                batch->State->State = SpatialQueryState::Failed;
                batch->State->Diagnostic = "Spatial query service stopped.";
            }
            Batches.clear();
        }
        void Drain()
        {
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
                return b->State.use_count() == 1 && b->State->State != SpatialQueryState::Queued &&
                    b->State->State != SpatialQueryState::Submitted && b.use_count() == 1;
            });
        }
        std::uint64_t Next{1};
        SpatialIndexCacheStats Stats{};
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
                    for (auto& batch : m_Impl->Batches)
                    {
                        if (batch->State->State != SpatialQueryState::Queued) continue;
                        m_Impl->Device->WriteBuffer(batch->Input, batch->Queries.data(), batch->Queries.size()*12);
                        m_Impl->Device->WriteBuffer(batch->Exclusions, batch->Excluded.data(), batch->Excluded.size()*4);
                        if (!RecordGpuQueries({batch->Target->Id}, commands,
                            {.Queries = {.Buffer = batch->Input, .Count = std::uint32_t(batch->Queries.size())},
                             .Neighbors = batch->Output, .Headers = batch->Header,
                             .Capacity = batch->Capacity, .Radius = batch->Radius,
                             .KNearestCount = batch->Radius < 0 ? batch->Capacity : 0u,
                             .ExcludedIndices = batch->Exclusions}))
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
                .HasInFlightWork = [this]() { return !m_Impl->Batches.empty(); },
                .ShutdownAfterDeviceIdle = [this]() { m_Impl->ShutdownBatches(); m_Impl->Participant = {}; }
            });
        return setup.RegisterFrameHook(FramePhase::Maintenance,
                                       [this](RuntimeFrameHookContext&) { Prune(); });
    }
    void SpatialIndexCache::OnShutdown(RuntimeModuleShutdownContext& context)
    {
        if (m_Impl->Jobs && m_Impl->Participant.IsValid())
            m_Impl->Jobs->UnregisterGpuQueueParticipant(m_Impl->Participant, [this]() { m_Impl->Device->WaitIdle(); });
        m_Impl->ShutdownBatches();
        m_Impl->Jobs = nullptr;
        m_Impl->Entries.clear();
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
        std::span<const std::uint32_t> excludedSlots, std::shared_ptr<SpatialNearestBatch> reuse)
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
        std::shared_ptr<Impl::Batch> batch;
        if (reuse)
        {
            auto found = std::ranges::find_if(s.Batches, [&](const auto& b) { return b->State == reuse; });
            if (found == s.Batches.end() || reuse->State != SpatialQueryState::Ready ||
                ((*found)->Queries.size() != queries.size() || (*found)->Capacity != k))
                return fail("Only a completed batch with the same query count and k can be reused.");
            batch = *found;
        }
        else
        {
            batch = std::make_shared<Impl::Batch>();
            batch->Device = s.Device;
            auto allocate = [&](std::size_t bytes, bool host) {
                return s.Device->CreateBuffer({.SizeBytes = bytes,
                    .Usage = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferSrc | RHI::BufferUsage::TransferDst,
                    .HostVisible = host, .DebugName = "SpatialIndex.QueryBatch"});
            };
            batch->Input = allocate(queries.size()*12, true);
            batch->Output = allocate(queries.size()*k*8, false);
            batch->Header = allocate(queries.size()*8, false);
            batch->Exclusions = allocate(queries.size()*4, true);
            if (!batch->Input.IsValid() || !batch->Output.IsValid() || !batch->Header.IsValid() || !batch->Exclusions.IsValid())
                return fail("GPU spatial batch allocation failed.");
            s.Batches.push_back(batch);
        }
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
        return batch->State;
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
    bool SpatialIndexCache::RecordGpuBuild(SpatialIndexHandle handle,
                                           RHI::ICommandContext& commands)
    {
        auto& s = *m_Impl;
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
            e->Points = allocate(e->Snapshot->Slots.size() * 12);
            e->Mapping = allocate(e->Snapshot->Slots.size() * 4);
            if (!e->Points.IsValid() || !e->Mapping.IsValid())
            {
                if (e->Points.IsValid())
                    s.Device->DestroyBuffer(e->Points);
                if (e->Mapping.IsValid())
                    s.Device->DestroyBuffer(e->Mapping);
                e->Points = {};
                e->Mapping = {};
                e->Gpu.reset();
                return false;
            }
            if (!e->Snapshot->Slots.empty())
            {
                s.Device->WriteBuffer(e->Points, e->Snapshot->Index.Points().data(), e->Snapshot->Slots.size() * 12);
                s.Device->WriteBuffer(e->Mapping, e->Snapshot->Slots.data(), e->Snapshot->Slots.size() * 4);
            }
        }
        if (!e->Gpu->View().NodesBDA)
        {
            if (!e->Gpu->RecordBuild(commands,
                                     {.Buffer = e->Points, .Count = std::uint32_t(e->Snapshot->Slots.size())},
                                     e->Mapping))
                return false;
            ++s.Stats.GpuBuilds;
        }
        return true;
    }
    bool SpatialIndexCache::RecordGpuQueries(SpatialIndexHandle handle,
                                             RHI::ICommandContext& commands,
                                             const Graphics::PointLbvhQuery& query)
    {
        if (!RecordGpuBuild(handle, commands))
            return false;
        return m_Impl->Find(handle)->Gpu->RecordQuery(commands, query);
    }
    Graphics::PointLbvhView SpatialIndexCache::GpuView(SpatialIndexHandle handle) const
    {
        const auto* e = m_Impl->Find(handle);
        return e && e->Gpu ? e->Gpu->View() : Graphics::PointLbvhView{};
    }
    void SpatialIndexCache::Prune()
    {
        auto& s = *m_Impl;
        s.Stats.Evictions +=
            std::erase_if(s.Entries, [&](const auto& e) { return !s.Current(*e); });
    }
    SpatialIndexCacheStats SpatialIndexCache::Stats() const noexcept
    {
        return m_Impl->Stats;
    }
} // namespace Extrinsic::Runtime
