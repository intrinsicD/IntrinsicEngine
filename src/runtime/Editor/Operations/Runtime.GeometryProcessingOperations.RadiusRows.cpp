#include <algorithm>
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
    PointIndexState AcquirePointIndex(
        SpatialIndexCache& cache, WorldHandle world, entt::entity entity,
        const GeometryPropertyRef& positions, std::span<const std::uint32_t> slots,
        std::span<const glm::vec3> points, SpatialIndexHandle& handle,
        std::shared_ptr<const SpatialIndexSnapshot>& snapshot, bool& reused,
        std::string& diagnostic)
    {
        auto acquired = cache.Acquire(world, entity, positions);
        if (!acquired.Ready())
        {
            diagnostic = std::move(acquired.Diagnostic);
            return PointIndexState::Unavailable;
        }
        handle = acquired.Handle;
        snapshot = cache.Snapshot(acquired.Handle);
        reused = acquired.Reused;
        return SpatialIndexSnapshotMatches(snapshot.get(), slots, points)
            ? PointIndexState::Ready : PointIndexState::Mismatched;
    }

    bool AppendPointKnnRows(
        const SpatialIndexSnapshot& source, std::span<const glm::vec3> points,
        std::uint32_t width, std::vector<std::uint32_t>& indices, std::string& diagnostic)
    {
        indices.reserve(indices.size() + points.size() * width);
        for (const auto point : points)
        {
            const auto row = source.Index.KNearest(point, width);
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
    RadiusRowsState AdvancePointRadiusRows(SpatialIndexCache& cache,SpatialIndexHandle handle,
        std::span<const glm::vec3> points,std::span<const std::uint32_t> slots,float radius,
        std::uint32_t batchSize,std::uint32_t capacity,std::uint32_t lowestIdLimit,
        PointRadiusRows& rows,std::string& diagnostic)
    {
        const auto fail=[&](std::string why){diagnostic=std::move(why);rows.Batch.reset();return RadiusRowsState::Failed;};
        if(rows.Finished)return RadiusRowsState::Ready;
        if(points.empty() || points.size()!=slots.size() || batchSize==0 || capacity==0 || rows.NextQuery>=points.size())
            return fail("Invalid radius pagination inputs.");
        if(rows.Started==std::chrono::steady_clock::time_point{})rows.Started=std::chrono::steady_clock::now();
        if(rows.Batch)
        {
            if(rows.Batch->State==SpatialQueryState::Failed)return fail(rows.Batch->Diagnostic);
            if(rows.Batch->State!=SpatialQueryState::Ready)return RadiusRowsState::Pending;
            rows.Queried=true;
            if(rows.Batch->Counts.size()!=std::min<std::size_t>(batchSize,points.size()-rows.NextQuery))
                return fail("Incomplete Vulkan query batch.");
            std::vector<std::uint32_t> row;
            for(std::size_t i=0;i<rows.Batch->Counts.size();++i)
            {
                rows.MaximumNeighbors=std::max(rows.MaximumNeighbors,std::size_t(rows.Batch->Counts[i]));
                const auto required=lowestIdLimit?std::min(lowestIdLimit,rows.Batch->Counts[i]):rows.Batch->Counts[i];
                if(required>rows.Batch->Capacity)
                    return fail("Vulkan radius support overflowed capacity; previous outputs retained.");
                row.clear();
                for(std::uint32_t j=0;j<required;++j)
                {
                    const auto id=rows.Batch->Neighbors[i*rows.Batch->Capacity+j].Index;
                    const auto found=std::lower_bound(slots.begin(),slots.end(),id);
                    if(found==slots.end() || *found!=id || id==slots[rows.NextQuery+i])
                        return fail("Invalid Vulkan radius source row.");
                    row.push_back(std::uint32_t(found-slots.begin()));
                }
                if(!AppendPointRadiusRow(rows,row,diagnostic))return fail(diagnostic);
            }
            rows.NextQuery+=rows.Batch->Counts.size();
            if(rows.NextQuery==points.size())
            {
                rows.Batch.reset();rows.Finished=true;
                rows.Milliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-rows.Started).count();
                return RadiusRowsState::Ready;
            }
        }
        const auto count=std::min<std::size_t>(batchSize,points.size()-rows.NextQuery);
        if(rows.Batch && rows.Batch->Counts.size()!=count)rows.Batch.reset();
        capacity=std::uint32_t(std::max<std::size_t>(1,std::min<std::size_t>(capacity,points.size()-1)));
        if(lowestIdLimit)capacity=std::min(capacity,lowestIdLimit);
        rows.Batch=cache.QueueGpuRadius(handle,points.subspan(rows.NextQuery,count),radius,capacity,
            slots.subspan(rows.NextQuery,count),std::move(rows.Batch));
        ++rows.QueryBatches;
        if(rows.Batch->State==SpatialQueryState::Failed)return fail(rows.Batch->Diagnostic);
        return RadiusRowsState::Pending;
    }
    KnnRowsState AdvancePointKnnRows(
        SpatialIndexCache& cache, SpatialIndexHandle index, std::span<const glm::vec3> points,
        std::span<const std::uint32_t> slots, std::uint32_t width,
        std::uint32_t batchSize, PointKnnRows& rows, std::string& diagnostic)
    {
        const auto fail = [&](std::string why) {
            diagnostic = std::move(why); rows.Batch.reset(); return KnnRowsState::Failed;
        };
        if (rows.Finished) return KnnRowsState::Ready;
        if (rows.Started == std::chrono::steady_clock::time_point{})
            rows.Started = std::chrono::steady_clock::now();
        if (rows.Batch)
        {
            if (rows.Batch->State == SpatialQueryState::Failed) return fail(rows.Batch->Diagnostic);
            if (rows.Batch->State != SpatialQueryState::Ready) return KnnRowsState::Pending;
            for (std::size_t row = 0; row < rows.Batch->Counts.size(); ++row)
            {
                if (rows.Batch->Counts[row] != width) return fail("Incomplete Vulkan kNN neighborhood.");
                for (std::size_t j = 0; j < width; ++j)
                {
                    const auto id = rows.Batch->Neighbors[row * rows.Batch->Capacity + j].Index;
                    const auto found = std::lower_bound(slots.begin(), slots.end(), id);
                    if (found == slots.end() || *found != id) return fail("Invalid Vulkan neighbor source row.");
                    rows.Indices.push_back(std::uint32_t(found - slots.begin()));
                }
            }
            rows.NextQuery += rows.Batch->Counts.size();
            if (rows.NextQuery == points.size())
            {
                rows.Batch.reset(); rows.Finished = true;
                rows.Milliseconds = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - rows.Started).count();
                return KnnRowsState::Ready;
            }
        }
        const auto count = std::min<std::size_t>(batchSize, points.size() - rows.NextQuery);
        if (rows.Batch && rows.Batch->Counts.size() != count) rows.Batch.reset();
        rows.Batch = cache.QueueGpuKNearest(index, points.subspan(rows.NextQuery, count),
                                            width, {}, std::move(rows.Batch));
        ++rows.QueryBatches;
        if (rows.Batch->State == SpatialQueryState::Failed) return fail(rows.Batch->Diagnostic);
        return KnnRowsState::Pending;
    }
}
