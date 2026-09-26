// Runtime-owned, revision-aware entity point indices shared by geometry consumers.
module;
#include <cstdint>
#include <cstddef>
#include <functional>
#include <entt/entity/entity.hpp>
#include <glm/vec3.hpp>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <span>
#include <vector>
export module Extrinsic.Runtime.SpatialIndexCache;
export import Geometry.PointLBVH;
export import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.ModuleLifecycle;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Core.Error;
import Extrinsic.RHI.Handles;

// The CPU constructor only borrows a reference; matching the owner's C++ language
// linkage keeps this interface out of the WorldRegistry job/RHI import closure.
extern "C++" {
    namespace Extrinsic::Runtime { class WorldRegistry; }
    namespace Extrinsic::RHI { class ICommandContext; }
}

export namespace Extrinsic::Runtime
{
    struct SpatialIndexHandle
    {
        std::uint64_t Value{};
    };
    struct SpatialIndexAcquisition
    {
        SpatialIndexHandle Handle{};
        bool Reused{};
        std::string Diagnostic{};
        [[nodiscard]] bool Ready() const noexcept
        {
            return Handle.Value != 0;
        }
    };
    struct SpatialIndexCacheStats
    {
        std::uint64_t Builds{}, Hits{}, Evictions{}, GpuBuilds{};
    };
    enum class SpatialIndexSpace : std::uint8_t { Property, EntityTransform };
    struct SpatialIndexSnapshot
    {
        Geometry::PointLBVH::Index Index{};
        std::vector<std::uint32_t> Slots{};
    };
    // Ordered source rows and exact numeric positions (+0 == -0); null never matches.
    [[nodiscard]] bool SpatialIndexSnapshotMatches(const SpatialIndexSnapshot*,
        std::span<const std::uint32_t> slots, std::span<const glm::vec3> points) noexcept;
    struct SpatialIndexWorkspace
    {
        SpatialIndexHandle Handle{};
        std::shared_ptr<const SpatialIndexSnapshot> Snapshot{};
        std::string Diagnostic{};
        [[nodiscard]] bool Ready() const noexcept { return Handle.Value && bool(Snapshot); }
    };
    enum class SpatialQueryState : std::uint8_t { Queued, Submitted, Ready, Failed };
    struct SpatialNearestBatch
    {
        SpatialQueryState State{SpatialQueryState::Queued};
        // Query i owns Capacity entries. Radius Counts report all hits, so only
        // min(Counts[i], Capacity) entries are stored; callers must handle overflow.
        std::vector<Geometry::PointLBVH::Neighbor> Neighbors{};
        std::vector<std::uint32_t> Counts{};
        std::uint32_t Capacity{1};
        std::string Diagnostic{};
    };
    struct SpatialGpuIndexView
    {
        std::uint64_t NodesBDA{}, PositionsBDA{}, OriginalSlotsBDA{};
        std::uint32_t Count{};
    };
    struct SpatialGpuResult
    {
        SpatialQueryState State{SpatialQueryState::Queued};
        std::vector<std::byte> Data{};
        std::string Diagnostic{};
    };
    // This concrete service is also its runtime module; there is no forwarding service layer.
    extern "C++" class SpatialIndexCache final : public IRuntimeModule
    {
      public:
        SpatialIndexCache();
        explicit SpatialIndexCache(WorldRegistry& worlds);
        ~SpatialIndexCache() override;
        [[nodiscard]] std::string_view Name() const noexcept override;
        [[nodiscard]] Core::Result OnRegister(EngineSetup& setup) override;
        void OnShutdown(RuntimeModuleShutdownContext& context) override;
        [[nodiscard]] SpatialIndexAcquisition Acquire(WorldHandle world, entt::entity entity,
                                                      const GeometryPropertyRef& positions,
                                                      SpatialIndexSpace space = SpatialIndexSpace::Property);
        // Device-owner thread only. Owns private positions with identity row IDs.
        // Keep the snapshot lease alive while querying; dropping its last caller
        // lease expires the handle. Pending GPU work retains resources until safe.
        [[nodiscard]] SpatialIndexWorkspace CreateWorkspace(std::span<const glm::vec3> positions);
        // Immutable CPU lease survives eviction; indices here are compact, Slots maps to original rows.
        [[nodiscard]] std::shared_ptr<const SpatialIndexSnapshot> Snapshot(SpatialIndexHandle handle) const;
        // Device-owner thread only. Reuse a completed batch to retain its buffer allocation.
        // Results use original property row IDs, matching Nearest.
        [[nodiscard]] std::shared_ptr<SpatialNearestBatch> QueueGpuNearest(
            SpatialIndexHandle handle, std::span<const glm::vec3> queries,
            std::shared_ptr<SpatialNearestBatch> reuse = {});
        // Exact kNN, 1..64; optional exclusions have one original property row ID per query.
        // Reuse requires the same query count and k. No CPU fallback on an unavailable device.
        [[nodiscard]] std::shared_ptr<SpatialNearestBatch> QueueGpuKNearest(
            SpatialIndexHandle handle, std::span<const glm::vec3> queries, std::uint32_t k,
            std::span<const std::uint32_t> excludedSlots = {},
            std::shared_ptr<SpatialNearestBatch> reuse = {});
        // Inclusive radius; counts remain complete when retained hits exceed capacity (1..1024).
        [[nodiscard]] std::shared_ptr<SpatialNearestBatch> QueueGpuRadius(
            SpatialIndexHandle handle, std::span<const glm::vec3> queries, float radius,
            std::uint32_t capacity, std::span<const std::uint32_t> excludedSlots = {},
            std::shared_ptr<SpatialNearestBatch> reuse = {});
        [[nodiscard]] bool GpuQueriesAvailable() const noexcept;
        // Record against the retained, current index on the device-owner thread.
        // Recorder owns its buffers (including a TransferSrc result of readbackBytes)
        // through captured leases. The cache retains it until the final readback is safe.
        [[nodiscard]] std::shared_ptr<SpatialGpuResult> QueueGpuCompute(
            SpatialIndexHandle handle, std::size_t readbackBytes,
            std::function<RHI::BufferHandle(RHI::ICommandContext&,
                                           const SpatialGpuIndexView&)> record);
        // Same framed recording and readback without an index; the view is empty.
        [[nodiscard]] std::shared_ptr<SpatialGpuResult> QueueGpuCompute(
            std::size_t readbackBytes,
            std::function<RHI::BufferHandle(RHI::ICommandContext&,
                                           const SpatialGpuIndexView&)> record);
        // Stale world/entity/property/deletion revisions return nullopt; reacquire to rebuild.
        [[nodiscard]] std::optional<Geometry::PointLBVH::Neighbor> Nearest(
            SpatialIndexHandle handle, glm::vec3 query,
            std::uint32_t excludedSlot = Geometry::PointLBVH::InvalidIndex) const;
        [[nodiscard]] std::optional<Geometry::PointLBVH::RadiusResult> Radius(
            SpatialIndexHandle handle, glm::vec3 query, float radius, std::uint32_t capacity,
            std::uint32_t excludedSlot = Geometry::PointLBVH::InvalidIndex) const;
        [[nodiscard]] std::optional<std::vector<Geometry::PointLBVH::Neighbor>> KNearest(
            SpatialIndexHandle handle, glm::vec3 query, std::uint32_t k,
            std::uint32_t excludedSlot = Geometry::PointLBVH::InvalidIndex) const;
        void Prune();
        [[nodiscard]] SpatialIndexCacheStats Stats() const noexcept;

      private:
        [[nodiscard]] std::shared_ptr<SpatialNearestBatch> QueueGpuBatch(
            SpatialIndexHandle handle, std::span<const glm::vec3> queries, std::uint32_t capacity,
            float radius, std::span<const std::uint32_t> excludedSlots,
            std::shared_ptr<SpatialNearestBatch> reuse);
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace Extrinsic::Runtime
