// Reusable device point queries (LBVH traversal or exhaustive scan) under one neighbor/header ABI;
// callers own submission ordering and input/result buffers.
module;
#include <cstdint>
#include <memory>
export module Extrinsic.Graphics.PointLBVH;
export import Extrinsic.Graphics.PointQuery;
import Extrinsic.RHI.Handles;

extern "C++" { namespace Extrinsic::RHI { class IDevice; class ICommandContext; } }

export namespace Extrinsic::Graphics
{
    struct PointLbvhInput
    {
        RHI::BufferHandle Buffer{};
        std::uint64_t Offset{};
        std::uint32_t Count{}, Stride{12};
        // ARCH-023: the buffer's base device address, resolved by the owner (Offset still applies). Nonzero selects
        // a resolved recording: every buffer of the call carries its address (a query's in Resolved), pipelines come
        // from PreparePipelines, and the call makes no device call, so a method worker records it while the owner
        // renders, resizes or is minimized. A resolved build takes no object indices.
        std::uint64_t Address{};
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
        // Optional column-major dmat4 applied in double precision before querying. Both buffers
        // must be supplied: Transform holds 128 bytes; TransformedQueries holds Count float3s.
        // The scratch must not alias Queries and must remain alive through command completion.
        RHI::BufferHandle Transform{}, TransformedQueries{};
        // Byte offset of queryCount headers; supports one packed correspondence readback.
        std::uint64_t HeaderOffset{};
        // ARCH-023, a resolved recording (Queries.Address): the owner-resolved device addresses of the buffers above
        // and of RecordBruteForceQuery's targetIndices, offsets included (HeaderOffset still applies to Headers); a
        // given buffer needs its address. Handles are still required: barriers name them.
        struct Addresses
        {
            std::uint64_t Neighbors{}, Headers{}, ExcludedIndices{}, Transform{}, TransformedQueries{}, TargetIndices{};
        } Resolved{};
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
        // ARCH-023, owner thread: creates the brute-force and query-transform pipelines a resolved recording needs
        // (Reserve creates the hierarchy's), so a worker's resolved recording creates nothing. False when one fails.
        [[nodiscard]] bool PreparePipelines(bool bruteForce, bool transform);
        // Input float3 coordinates must be finite, |coordinate|<=1e18. No CPU readback/build.
        // Optional uint32 object indices map compact live inputs back to source slots.
        [[nodiscard]] bool RecordBuild(RHI::ICommandContext& cmd, PointLbvhInput points,
                                       RHI::BufferHandle objectIndices = {});
        // Neighbors needs queryCount*max(capacity,1)*8 bytes, Headers queryCount*8 bytes.
        [[nodiscard]] bool RecordQuery(RHI::ICommandContext& cmd, const PointLbvhQuery& query);
        // BruteForce strategy: RecordQuery's validation, ranking and output ABI over `targets` (at most
        // 2^20 float3 rows, admitted by PointBruteForceRefusal) without Reserve, RecordBuild or any
        // hierarchy; optional uint32 targetIndices
        // name each row's original ID. Any invalid target sets Status=1 for every query, as in Lbvh.
        [[nodiscard]] bool RecordBruteForceQuery(RHI::ICommandContext& cmd,
            PointLbvhInput targets,
            RHI::BufferHandle targetIndices,
            const PointLbvhQuery& query);
        // Borrow valid until resize/destruction, contents until next build. Device must outlive
        // owner.
        [[nodiscard]] PointLbvhView View() const noexcept;
        [[nodiscard]] std::uint64_t AllocationCount() const noexcept;
        [[nodiscard]] std::uint64_t BuildCount() const noexcept;
        // Bytes of the hierarchy storage and its sort scratch currently allocated.
        [[nodiscard]] std::uint64_t DeviceBytes() const noexcept;

    private:
        struct Impl;
        [[nodiscard]] bool RecordQueries(RHI::ICommandContext& cmd,
            const PointLbvhQuery& query,
            const PointLbvhInput* targets,
            RHI::BufferHandle targetIndices);
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace Extrinsic::Graphics
