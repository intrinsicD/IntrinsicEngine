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
    RowsState AdvancePointRadiusRows(SpatialIndexCache& cache,SpatialIndexHandle handle,
        std::span<const glm::vec3> points,std::span<const std::uint32_t> slots,float radius,
        std::uint32_t batchSize,std::uint32_t capacity,std::uint32_t lowestIdLimit,
        PointRadiusRows& rows,std::string& diagnostic)
    {
        if(rows.Finished)return RowsState::Ready;
        if(points.empty() || points.size()!=slots.size() || batchSize==0 || capacity==0 || rows.NextQuery>=points.size())
        {diagnostic="Invalid radius pagination inputs.";rows.Batch.reset();return RowsState::Failed;}
        capacity=std::uint32_t(std::max<std::size_t>(1,std::min<std::size_t>(capacity,points.size()-1)));
        if(lowestIdLimit)capacity=std::min(capacity,lowestIdLimit);
        return AdvanceGpuRowPages(rows,points.size(),batchSize,diagnostic,
            [&](const SpatialNearestBatch& batch,std::string& why)
            {
                rows.Queried=true;
                if(batch.Counts.size()!=std::min<std::size_t>(batchSize,points.size()-rows.NextQuery))
                {why="Incomplete Vulkan query batch.";return false;}
                std::vector<std::uint32_t> row;
                for(std::size_t i=0;i<batch.Counts.size();++i)
                {
                    rows.MaximumNeighbors=std::max(rows.MaximumNeighbors,std::size_t(batch.Counts[i]));
                    const auto required=lowestIdLimit?std::min(lowestIdLimit,batch.Counts[i]):batch.Counts[i];
                    if(required>batch.Capacity)
                    {why="Vulkan radius support overflowed capacity; previous outputs retained.";return false;}
                    row.clear();
                    for(std::uint32_t j=0;j<required;++j)
                    {
                        const auto id=batch.Neighbors[i*batch.Capacity+j].Index;
                        const auto found=std::lower_bound(slots.begin(),slots.end(),id);
                        if(found==slots.end() || *found!=id || id==slots[rows.NextQuery+i])
                        {why="Invalid Vulkan radius source row.";return false;}
                        row.push_back(std::uint32_t(found-slots.begin()));
                    }
                    if(!AppendPointRadiusRow(rows,row,why))return false;
                }
                return true;
            },
            [&](std::size_t first,std::size_t count,std::shared_ptr<SpatialNearestBatch> reuse)
            {
                return cache.QueueGpuRadius(handle,points.subspan(first,count),radius,capacity,
                    slots.subspan(first,count),std::move(reuse));
            });
    }
    RowsState AdvancePointKnnRows(
        SpatialIndexCache& cache, SpatialIndexHandle index, std::span<const glm::vec3> points,
        std::span<const std::uint32_t> slots, std::uint32_t width,
        std::uint32_t batchSize, PointKnnRows& rows, std::string& diagnostic)
    {
        return AdvanceGpuRowPages(rows, points.size(), batchSize, diagnostic,
            [&](const SpatialNearestBatch& batch, std::string& why)
            {
                for (std::size_t row = 0; row < batch.Counts.size(); ++row)
                {
                    if (batch.Counts[row] != width) { why = "Incomplete Vulkan kNN neighborhood."; return false; }
                    for (std::size_t j = 0; j < width; ++j)
                    {
                        const auto id = batch.Neighbors[row * batch.Capacity + j].Index;
                        const auto found = std::lower_bound(slots.begin(), slots.end(), id);
                        if (found == slots.end() || *found != id)
                        { why = "Invalid Vulkan neighbor source row."; return false; }
                        rows.Indices.push_back(std::uint32_t(found - slots.begin()));
                    }
                }
                return true;
            },
            [&](std::size_t first, std::size_t count, std::shared_ptr<SpatialNearestBatch> reuse)
            {
                return cache.QueueGpuKNearest(index, points.subspan(first, count), width, {}, std::move(reuse));
            });
    }
}
