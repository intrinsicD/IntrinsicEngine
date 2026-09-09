// Reusable device LBVH workspace; callers own submission ordering and input/result buffers.
module;
#include <cstdint>
#include <memory>
export module Extrinsic.Graphics.PointLBVH;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Handles;

export namespace Extrinsic::Graphics
{
    struct PointLbvhInput
    {
        RHI::BufferHandle Buffer{};
        std::uint64_t Offset{};
        std::uint32_t Count{}, Stride{12};
    };
    struct PointLbvhView
    {
        std::uint64_t NodesBDA{};
        std::uint32_t PointCount{};
        RHI::BufferHandle Storage{};
    };
    struct PointLbvhNeighbor
    {
        std::uint32_t Index{~0u};
        float SquaredDistance{};
    };
    // Status=1 rejects nonfinite/out-of-range inputs; TotalCount>capacity reports truncation.
    struct PointLbvhQueryHeader
    {
        std::uint32_t TotalCount{}, Status{};
    };
    struct PointLbvhQuery
    {
        PointLbvhInput Queries{};
        RHI::BufferHandle Neighbors{}, Headers{};
        std::uint32_t Capacity{1};
        float Radius{-1}; // -1 selects nearest; otherwise inclusive radius in input units.
        // Nonzero selects kNN: 1..64, Capacity must equal KNearestCount and Radius must be -1.
        std::uint32_t KNearestCount{};
        // Optional queryCount uint32 original source IDs. ~0u excludes nothing; equal positions remain eligible.
        RHI::BufferHandle ExcludedIndices{};
    };
    class PointLbvhWorkspace
    {
      public:
        explicit PointLbvhWorkspace(RHI::IDevice& device);
        ~PointLbvhWorkspace();
        PointLbvhWorkspace(const PointLbvhWorkspace&) = delete;
        PointLbvhWorkspace& operator=(const PointLbvhWorkspace&) = delete;
        // Reserve before recording; resizing invalidates borrowed views. At most 2^20 points.
        [[nodiscard]] bool Reserve(std::uint32_t count);
        // Input float3 coordinates must be finite, |coordinate|<=1e18. No CPU readback/build.
        // Optional uint32 object indices map compact live inputs back to source slots.
        [[nodiscard]] bool RecordBuild(RHI::ICommandContext& cmd, PointLbvhInput points,
                                       RHI::BufferHandle objectIndices = {});
        // Neighbors needs queryCount*max(capacity,1)*8 bytes, Headers queryCount*8 bytes.
        [[nodiscard]] bool RecordQuery(RHI::ICommandContext& cmd, const PointLbvhQuery& query);
        // Borrow valid until resize/destruction, contents until next build. Device must outlive
        // owner.
        [[nodiscard]] PointLbvhView View() const noexcept;
        [[nodiscard]] std::uint64_t AllocationCount() const noexcept;
        [[nodiscard]] std::uint64_t BuildCount() const noexcept;

      private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace Extrinsic::Graphics
