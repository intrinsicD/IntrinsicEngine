#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>
#include <glm/glm.hpp>
#include <entt/entity/entity.hpp>
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.WorldHandle;
#include "Editor/Operations/Runtime.GeometryProcessingOperations.RadiusRows.hpp"
namespace Extrinsic::Runtime::GeometryProcessingDetail
{
    PointIndexState AcquirePointIndex(SpatialIndexCache& cache,
        WorldHandle world,
        entt::entity entity,
        const GeometryPropertyRef& positions,
        std::span<const std::uint32_t> slots,
        std::span<const glm::vec3> points,
        SpatialIndexHandle& handle,
        std::shared_ptr<const SpatialIndexSnapshot>& snapshot,
        bool& reused,
        std::string& diagnostic,
        bool cpuIndex)
    {
        auto acquired = cache.Acquire(world, entity, positions, SpatialIndexSpace::Property, cpuIndex);
        if (!acquired.Ready())
        {
            diagnostic = std::move(acquired.Diagnostic);
            return PointIndexState::Unavailable;
        }
        handle = acquired.Handle;
        snapshot = cache.Snapshot(acquired.Handle);
        reused = acquired.Reused;
        if (SpatialIndexSnapshotMatches(snapshot.get(), slots, points))
            return PointIndexState::Ready;
        diagnostic = "Spatial index snapshot does not match the selected samples.";
        return PointIndexState::Mismatched;
    }

    bool AppendPointKnnRows(
        const SpatialIndexSnapshot& source, std::span<const glm::vec3> points,
        std::uint32_t width, std::vector<std::uint32_t>& indices, std::string& diagnostic)
    {
        indices.reserve(indices.size() + points.size() * width);
        for (const auto point : points)
        {
            const auto row = source.Index.KNearest(point, width, Geometry::PointLBVH::InvalidIndex, true);
            if (row.size() != width)
            {
                diagnostic = "Incomplete CPU kNN neighborhood.";
                return false;
            }
            for (const auto& neighbor : row) indices.push_back(neighbor.Index);
        }
        return true;
    }

    bool AppendPointRadiusRow(PointRadiusRows& rows,std::vector<std::uint32_t>& row,std::string& diagnostic)
    {
        if(row.size()>std::numeric_limits<std::uint32_t>::max()-rows.Indices.size())
        {diagnostic="Required radius support exceeds the packed neighborhood range.";return false;}
        std::sort(row.begin(),row.end());
        rows.MaximumNeighbors=std::max(rows.MaximumNeighbors,row.size());
        rows.Indices.insert(rows.Indices.end(),row.begin(),row.end());
        rows.Offsets.push_back(std::uint32_t(rows.Indices.size()));return true;
    }
    std::uint32_t PointRadiusCapacity(std::uint32_t capacity, std::size_t points, std::uint32_t lowestIdLimit)
    {
        capacity = std::uint32_t(std::max<std::size_t>(1, std::min<std::size_t>(capacity, points - 1)));
        return lowestIdLimit ? std::min(capacity, lowestIdLimit) : capacity;
    }
    bool AppendPointRadiusPage(PointRadiusRows& rows,
        const SpatialNearestBatch& batch,
        std::span<const std::uint32_t> slots,
        std::size_t first,
        std::uint32_t lowestIdLimit,
        std::string& why)
    {
        rows.Queried = true;
        std::vector<std::uint32_t> row;
        for (std::size_t i = 0; i < batch.Counts.size(); ++i)
        {
            rows.MaximumNeighbors = std::max(rows.MaximumNeighbors, std::size_t(batch.Counts[i]));
            const auto required = lowestIdLimit ? std::min(lowestIdLimit, batch.Counts[i]) : batch.Counts[i];
            if (required > batch.Capacity)
            {
                why = "Vulkan radius support overflowed capacity; previous outputs retained.";
                return false;
            }
            row.clear();
            for (std::uint32_t j = 0; j < required; ++j)
            {
                const auto id = batch.Neighbors[i * batch.Capacity + j].Index;
                const auto found = std::lower_bound(slots.begin(), slots.end(), id);
                if (found == slots.end() || *found != id || id == slots[first + i])
                {
                    why = "Invalid Vulkan radius source row.";
                    return false;
                }
                row.push_back(std::uint32_t(found - slots.begin()));
            }
            if (!AppendPointRadiusRow(rows, row, why))
                return false;
        }
        return true;
    }
    RowsState ComputePointRadiusRows(SpatialQuerySession& session,
        std::span<const std::uint32_t> slots,
        float radius,
        std::uint32_t lowestIdLimit,
        const std::atomic<bool>& abort,
        PointRadiusRows& rows,
        std::string& diagnostic)
    {
        const auto started = std::chrono::steady_clock::now();
        rows.Indices.clear();
        rows.Offsets.assign(1u, 0u);
        rows.MaximumNeighbors = 0u;
        if (slots.size() != session.Rows())
        {
            diagnostic = "Invalid radius pagination inputs.";
            return RowsState::Failed;
        }
        SpatialNearestBatch batch;
        for (std::uint32_t first = 0; first < session.Rows();)
        {
            const auto count = std::min(session.PageQueries(), session.Rows() - first);
            const auto status = session.Query(first, count, radius, nullptr, abort, batch);
            if (status == SpatialPageStatus::Abandoned)
                return RowsState::Pending;
            if (status == SpatialPageStatus::Failed)
            {
                diagnostic = batch.Diagnostic;
                return RowsState::Failed;
            }
            if (!AppendPointRadiusPage(rows, batch, slots, first, lowestIdLimit, diagnostic))
                return RowsState::Failed;
            first += count;
        }
        rows.Milliseconds =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        return RowsState::Ready;
    }
}
