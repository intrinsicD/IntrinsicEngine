// Private index admission and neighborhood capture for point-property methods.
// Include after importing the spatial index cache.
#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
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

    // Framed GPU query cursor. Milliseconds span the first advance to completion.
    struct GpuRowPages
    {
        std::shared_ptr<SpatialNearestBatch> Batch{};
        std::size_t NextQuery{}, QueryBatches{};
        bool Finished{};
        std::chrono::steady_clock::time_point Started{};
        double Milliseconds{};
    };
    enum class RowsState { Pending, Ready, Failed };

    // Main-thread pagination over `total` queries; callers guard staleness and cancellation.
    // `consume(batch, diagnostic)` decodes a completed batch or writes the diagnostic and returns
    // false. `queue(first, count, reuse)` submits the next batch; failure drops the batch.
    template <class Consume, class Queue>
    [[nodiscard]] RowsState AdvanceGpuRowPages(GpuRowPages& pages, std::size_t total, std::uint32_t batchSize,
                                               std::string& diagnostic, Consume&& consume, Queue&& queue)
    {
        const auto fail = [&](std::string why) { diagnostic = std::move(why); pages.Batch.reset(); return RowsState::Failed; };
        if (pages.Finished) return RowsState::Ready;
        if (pages.Started == std::chrono::steady_clock::time_point{})
            pages.Started = std::chrono::steady_clock::now();
        if (pages.Batch)
        {
            const auto& batch = *pages.Batch;
            if (batch.State == SpatialQueryState::Failed) return fail(batch.Diagnostic);
            if (batch.State != SpatialQueryState::Ready) return RowsState::Pending;
            if (!consume(batch, diagnostic)) { pages.Batch.reset(); return RowsState::Failed; }
            pages.NextQuery += batch.Counts.size();
            if (pages.NextQuery == total)
            {
                pages.Batch.reset(); pages.Finished = true;
                pages.Milliseconds = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - pages.Started).count();
                return RowsState::Ready;
            }
        }
        const auto count = std::min<std::size_t>(batchSize, total - pages.NextQuery);
        if (pages.Batch && pages.Batch->Counts.size() != count) pages.Batch.reset();
        pages.Batch = queue(pages.NextQuery, count, std::move(pages.Batch));
        ++pages.QueryBatches;
        if (pages.Batch->State == SpatialQueryState::Failed) return fail(pages.Batch->Diagnostic);
        return RowsState::Pending;
    }

    struct PointRadiusRows : GpuRowPages
    {
        std::vector<std::uint32_t> Indices{},Offsets{0};
        std::size_t MaximumNeighbors{};
        bool Queried{};
    };
    [[nodiscard]] bool AppendPointRadiusRow(PointRadiusRows&,std::vector<std::uint32_t>&,std::string& diagnostic);
    // Main-thread stage only. Slots are strictly increasing source IDs aligned
    // with points. Limit=0 requires complete support; a positive limit permits
    // only the exact lowest-ID prefix. The caller owns staleness and job gates.
    [[nodiscard]] RowsState AdvancePointRadiusRows(
        SpatialIndexCache&,SpatialIndexHandle,std::span<const glm::vec3> points,
        std::span<const std::uint32_t> slots,float radius,std::uint32_t batchSize,
        std::uint32_t capacity,std::uint32_t lowestIdLimit,PointRadiusRows&,std::string& diagnostic);

    struct PointKnnRows : GpuRowPages
    {
        std::vector<std::uint32_t> Indices{};
    };
    // Main-thread stage; caller guards staleness/cancellation. Slots are strictly
    // increasing source IDs aligned with points. Width includes self candidates;
    // every row must be complete. Returned indices address compact points.
    [[nodiscard]] RowsState AdvancePointKnnRows(
        SpatialIndexCache&, SpatialIndexHandle, std::span<const glm::vec3> points,
        std::span<const std::uint32_t> slots, std::uint32_t width,
        std::uint32_t batchSize, PointKnnRows&, std::string& diagnostic);
}
}
