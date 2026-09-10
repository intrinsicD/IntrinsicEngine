// Private framed radius pagination shared by property-based point methods.
#pragma once
#include <chrono>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>
namespace Extrinsic::Runtime::GeometryProcessingDetail
{
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
}
