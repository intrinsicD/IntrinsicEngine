// Private index admission and neighborhood capture for point-property methods.
// Include after importing the spatial index cache; the including unit supplies <atomic> in its global fragment.
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
    // untouched; Mismatched retains the acquired lease and reuse flag for reporting and writes a
    // diagnostic (callers with their own wording ignore it). `cpuIndex` false: a capture only GPU queries read, whose
    // snapshot has no CPU hierarchy (SpatialIndexCache::Acquire); the rows and points are still matched.
    [[nodiscard]] PointIndexState AcquirePointIndex(SpatialIndexCache&,
        WorldHandle,
        entt::entity,
        const GeometryPropertyRef& positions,
        std::span<const std::uint32_t> slots,
        std::span<const glm::vec3> points,
        SpatialIndexHandle&,
        std::shared_ptr<const SpatialIndexSnapshot>&,
        bool& reused,
        std::string& diagnostic,
        bool cpuIndex = true);

    // Width includes self candidates; append compact indices in query order.
    // A failed row leaves earlier rows appended; callers must discard the result.
    [[nodiscard]] bool AppendPointKnnRows(
        const SpatialIndexSnapshot&, std::span<const glm::vec3> points,
        std::uint32_t width, std::vector<std::uint32_t>& indices, std::string& diagnostic);

    // ComputePointRadiusRows' outcome. (ARCH-023: the frame-paced GpuRowPages/AdvanceGpuRowPages cursor is gone; GPU
    // rows run in a worker query session, SpatialIndexCache::PrepareQuerySession.)
    enum class RowsState { Pending, Ready, Failed };

    // Worker radius rows (ComputePointRadiusRows): CSR rows and the last pass's time (the session counts its pages).
    struct PointRadiusRows
    {
        std::vector<std::uint32_t> Indices{},Offsets{0};
        std::size_t MaximumNeighbors{};
        double Milliseconds{};
        bool Queried{};
    };
    [[nodiscard]] bool AppendPointRadiusRow(PointRadiusRows&,std::vector<std::uint32_t>&,std::string& diagnostic);
    // The radius candidates one query retains: `capacity` within 1..points-1, at most a positive lowest-ID limit.
    [[nodiscard]] std::uint32_t PointRadiusCapacity(
        std::uint32_t capacity, std::size_t points, std::uint32_t lowestIdLimit);
    // One completed radius page (queries first.. of `slots`) appended as compact rows, self excluded; false with
    // the reason when a required support overflowed the capacity or names an invalid row.
    [[nodiscard]] bool AppendPointRadiusPage(PointRadiusRows&,
        const SpatialNearestBatch&,
        std::span<const std::uint32_t> slots,
        std::size_t first,
        std::uint32_t lowestIdLimit,
        std::string& why);
    // ARCH-023, a run's worker step: every radius row of a prepared session (radius rows over the captured points,
    // `slots` their source IDs, excluding each query's own row, capacity PointRadiusCapacity), page by page without
    // frames, each page decoded by AppendPointRadiusPage. Ready; Failed with the reason; Pending when `abort`
    // won (the run ended). Replaces the rows.
    [[nodiscard]] RowsState ComputePointRadiusRows(SpatialQuerySession&,
        std::span<const std::uint32_t> slots,
        float radius,
        std::uint32_t lowestIdLimit,
        const std::atomic<bool>& abort,
        PointRadiusRows&,
        std::string& diagnostic);
}
}
