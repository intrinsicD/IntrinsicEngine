// Private index admission and neighborhood capture for point-property methods.
#pragma once
#include <chrono>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>
extern "C++"
{
namespace Extrinsic::Runtime::GeometryProcessingDetail
{
    enum class PointIndexState { Ready, Unavailable, Mismatched };
    // Main-thread admission after caller preflight. Unavailable leaves lease outputs
    // untouched; Mismatched retains the acquired lease and reuse flag for reporting.
    [[nodiscard]] PointIndexState AcquirePointIndex(
        SpatialIndexCache&, WorldHandle, entt::entity, const GeometryPropertyRef& positions,
        std::span<const std::uint32_t> slots, std::span<const glm::vec3> points,
        SpatialIndexHandle&, std::shared_ptr<const SpatialIndexSnapshot>&,
        bool& reused, std::string& diagnostic);

    // Width includes self candidates; append compact indices in query order.
    // A failed row leaves earlier rows appended; callers must discard the result.
    [[nodiscard]] bool AppendPointKnnRows(
        const SpatialIndexSnapshot&, std::span<const glm::vec3> points,
        std::uint32_t width, std::vector<std::uint32_t>& indices, std::string& diagnostic);

    struct PointRadiusRows
    {
        std::vector<std::uint32_t> Indices{},Offsets{0};
        std::shared_ptr<SpatialNearestBatch> Batch{};
        std::size_t NextQuery{},MaximumNeighbors{},QueryBatches{};
        bool Finished{},Queried{};
        std::chrono::steady_clock::time_point Started{};
        double Milliseconds{};
    };
    enum class RadiusRowsState { Pending, Ready, Failed };
    [[nodiscard]] bool AppendPointRadiusRow(PointRadiusRows&,std::vector<std::uint32_t>&,std::string& diagnostic);
    // Main-thread stage only. Slots are strictly increasing source IDs aligned
    // with points. Limit=0 requires complete support; a positive limit permits
    // only the exact lowest-ID prefix. The caller owns staleness and job gates.
    [[nodiscard]] RadiusRowsState AdvancePointRadiusRows(
        SpatialIndexCache&,SpatialIndexHandle,std::span<const glm::vec3> points,
        std::span<const std::uint32_t> slots,float radius,std::uint32_t batchSize,
        std::uint32_t capacity,std::uint32_t lowestIdLimit,PointRadiusRows&,std::string& diagnostic);

    struct PointKnnRows
    {
        std::vector<std::uint32_t> Indices{};
        std::shared_ptr<SpatialNearestBatch> Batch{};
        std::size_t NextQuery{}, QueryBatches{};
        bool Finished{};
        std::chrono::steady_clock::time_point Started{};
        double Milliseconds{};
    };
    enum class KnnRowsState { Pending, Ready, Failed };
    // Main-thread stage; caller guards staleness/cancellation. Slots are strictly
    // increasing source IDs aligned with points. Width includes self candidates;
    // every row must be complete. Returned indices address compact points.
    [[nodiscard]] KnnRowsState AdvancePointKnnRows(
        SpatialIndexCache&, SpatialIndexHandle, std::span<const glm::vec3> points,
        std::span<const std::uint32_t> slots, std::uint32_t width,
        std::uint32_t batchSize, PointKnnRows&, std::string& diagnostic);
}
}
