module;
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
module Extrinsic.Graphics.PointLBVH;
import Extrinsic.Core.Filesystem.PathResolver;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Types;

namespace Extrinsic::Graphics
{
    namespace
    {
        struct BuildPush
        {
            std::uint64_t Points{}, Keys{}, Nodes{}, Bounds{};
            std::uint32_t Count{}, Padded{}, Stride{}, J{}, K{}, Reserved{};
            std::uint64_t ObjectIndices{};
        };
        struct QueryPush
        {
            std::uint64_t Nodes{}, Bounds{}, Queries{}, Neighbors{}, Headers{};
            std::uint32_t PointCount{}, QueryCount{}, Stride{}, Capacity{};
            float Radius{};
            std::uint32_t KNearestCount{};
            std::uint64_t ExcludedIndices{};
        };
        static_assert(sizeof(BuildPush) == 64 && sizeof(QueryPush) == 72);
    } // namespace
    struct PointLbvhWorkspace::Impl
    {
        explicit Impl(RHI::IDevice& device) : Device(device)
        {
        }
        ~Impl()
        {
            if (Storage.IsValid())
                Device.DestroyBuffer(Storage);
            for (auto p : Pipelines)
                if (p.IsValid())
                    Device.DestroyPipeline(p);
        }
        bool PipelinesReady()
        {
            constexpr std::array names{"lbvh_bounds", "lbvh_morton", "lbvh_sort", "lbvh_build",
                                       "lbvh_query"};
            for (std::size_t i = 0; i < names.size(); ++i)
            {
                if (Pipelines[i].IsValid())
                    continue;
                const auto path = Core::Filesystem::GetShaderPath(std::string("shaders/") +
                                                                  names[i] + ".comp.spv");
                Pipelines[i] = Device.CreatePipeline(RHI::PipelineDesc{
                    .VertexShaderPath = {},
                    .FragmentShaderPath = {},
                    .ComputeShaderPath = path.c_str(),
                    .PushConstantSize =
                        std::uint32_t(i == 4 ? sizeof(QueryPush) : sizeof(BuildPush)),
                    .DebugName = names[i]});
                if (!Pipelines[i].IsValid())
                    return false;
            }
            return true;
        }
        RHI::IDevice& Device;
        RHI::BufferHandle Storage{};
        std::array<RHI::PipelineHandle, 5> Pipelines{};
        std::uint32_t Capacity{}, Count{}, Padded{};
        std::uint64_t Allocations{}, Builds{}, Keys{}, Nodes{}, Bounds{};
        bool Built{};
    };
    PointLbvhWorkspace::PointLbvhWorkspace(RHI::IDevice& device)
        : m_Impl(std::make_unique<Impl>(device))
    {
    }
    PointLbvhWorkspace::~PointLbvhWorkspace() = default;
    bool PointLbvhWorkspace::Reserve(std::uint32_t count)
    {
        auto& s = *m_Impl;
        if (count > (1u << 20u) || !s.Device.IsOperational())
            return false;
        if (!s.PipelinesReady())
            return false;
        const auto padded = std::bit_ceil(std::max(count, 1u));
        if (s.Capacity >= padded)
            return true;
        const std::uint64_t bytes =
            32u + std::uint64_t(padded) * 8u + (2u * std::uint64_t(padded) - 1u) * 48u;
        auto buffer = s.Device.CreateBuffer(RHI::BufferDesc{.SizeBytes = bytes,
                                                            .Usage = RHI::BufferUsage::Storage |
                                                                     RHI::BufferUsage::TransferSrc |
                                                                     RHI::BufferUsage::TransferDst,
                                                            .DebugName = "PointLBVH.Workspace"});
        if (!buffer.IsValid())
            return false;
        auto bda = s.Device.GetBufferDeviceAddress(buffer);
        if (!bda)
        {
            s.Device.DestroyBuffer(buffer);
            return false;
        }
        if (s.Storage.IsValid())
            s.Device.DestroyBuffer(s.Storage);
        s.Storage = buffer;
        s.Capacity = padded;
        ++s.Allocations;
        s.Built = false;
        s.Bounds = bda;
        s.Keys = bda + 32u;
        s.Nodes = s.Keys + std::uint64_t(padded) * 8u;
        return true;
    }
    bool PointLbvhWorkspace::RecordBuild(RHI::ICommandContext& cmd, PointLbvhInput points,
                                         RHI::BufferHandle objectIndices)
    {
        auto& s = *m_Impl;
        if (points.Count > s.Capacity || points.Stride < 12 || points.Stride % 4 ||
            points.Offset % 4 || !s.Storage.IsValid() || !s.Device.IsOperational())
            return false;
        const auto source = s.Device.GetBufferDeviceAddress(points.Buffer);
        if (points.Count && (!source || points.Offset > ~std::uint64_t{} - source))
            return false;
        BuildPush push{.Points = source + points.Offset,
                       .Keys = s.Keys,
                       .Nodes = s.Nodes,
                       .Bounds = s.Bounds,
                       .Count = points.Count,
                       .Padded = std::bit_ceil(std::max(points.Count, 1u)),
                       .Stride = points.Stride};
        if (objectIndices.IsValid())
        {
            push.ObjectIndices = s.Device.GetBufferDeviceAddress(objectIndices);
            if (!push.ObjectIndices)
                return false;
            cmd.BufferBarrier(objectIndices,
                              RHI::MemoryAccess::TransferWrite | RHI::MemoryAccess::ShaderWrite,
                              RHI::MemoryAccess::ShaderRead);
        }
        if (points.Buffer.IsValid())
            cmd.BufferBarrier(points.Buffer,
                              RHI::MemoryAccess::TransferWrite | RHI::MemoryAccess::ShaderWrite,
                              RHI::MemoryAccess::ShaderRead);
        cmd.BufferBarrier(s.Storage, RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::ShaderWrite,
                          RHI::MemoryAccess::ShaderWrite);
        auto dispatch = [&](int pipeline, std::uint32_t groups) {
            cmd.BindPipeline(s.Pipelines[pipeline]);
            cmd.PushConstants(&push, sizeof(push), 0);
            cmd.Dispatch(groups, 1, 1);
            cmd.BufferBarrier(s.Storage, RHI::MemoryAccess::ShaderWrite,
                              RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::ShaderWrite);
        };
        dispatch(0, 1);
        dispatch(1, (push.Padded + 255) / 256);
        for (std::uint32_t k = 2; k <= push.Padded; k *= 2)
            for (std::uint32_t j = k / 2; j; j /= 2)
            {
                push.K = k;
                push.J = j;
                dispatch(2, (push.Padded + 255) / 256);
            }
        dispatch(3, (std::max(points.Count, 1u) + 255) / 256);
        s.Count = points.Count;
        s.Padded = push.Padded;
        s.Built = true;
        ++s.Builds;
        return true;
    }
    bool PointLbvhWorkspace::RecordQuery(RHI::ICommandContext& cmd, const PointLbvhQuery& query)
    {
        auto& s = *m_Impl;
        if (!s.Built || !s.Device.IsOperational() || query.Queries.Stride < 12 ||
            query.Queries.Stride % 4 || query.Queries.Offset % 4 ||
            query.Queries.Count > (1u << 20u) || query.Capacity > 1024 ||
            !std::isfinite(query.Radius) || (query.Radius < 0 && query.Radius != -1) ||
            query.Radius > 1e18f || (query.KNearestCount ? (query.KNearestCount > 64 ||
                query.Capacity != query.KNearestCount || query.Radius != -1) :
                (query.Radius == -1 && query.Capacity != 1)))
            return false;
        if (!query.Queries.Count)
            return true;
        const auto qb = s.Device.GetBufferDeviceAddress(query.Queries.Buffer),
                   nb = s.Device.GetBufferDeviceAddress(query.Neighbors),
                   hb = s.Device.GetBufferDeviceAddress(query.Headers);
        if (!qb || !nb || !hb || query.Queries.Offset > ~std::uint64_t{} - qb)
            return false;
        const auto excluded = query.ExcludedIndices.IsValid() ?
            s.Device.GetBufferDeviceAddress(query.ExcludedIndices) : 0;
        if (query.ExcludedIndices.IsValid() && !excluded) return false;
        const QueryPush push{.Nodes = s.Nodes,
                             .Bounds = s.Bounds,
                             .Queries = qb + query.Queries.Offset,
                             .Neighbors = nb,
                             .Headers = hb,
                             .PointCount = s.Count,
                             .QueryCount = query.Queries.Count,
                             .Stride = query.Queries.Stride,
                             .Capacity = query.Capacity,
                             .Radius = query.Radius,
                             .KNearestCount = query.KNearestCount,
                             .ExcludedIndices = excluded};
        cmd.BufferBarrier(query.Queries.Buffer,
                          RHI::MemoryAccess::TransferWrite | RHI::MemoryAccess::ShaderWrite,
                          RHI::MemoryAccess::ShaderRead);
        if (query.ExcludedIndices.IsValid())
            cmd.BufferBarrier(query.ExcludedIndices,
                              RHI::MemoryAccess::TransferWrite | RHI::MemoryAccess::ShaderWrite,
                              RHI::MemoryAccess::ShaderRead);
        cmd.BindPipeline(s.Pipelines[4]);
        cmd.PushConstants(&push, sizeof(push), 0);
        cmd.Dispatch((push.QueryCount + 255) / 256, 1, 1);
        for (auto buffer : {query.Neighbors, query.Headers})
            cmd.BufferBarrier(buffer, RHI::MemoryAccess::ShaderWrite,
                              RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::TransferRead);
        return true;
    }
    PointLbvhView PointLbvhWorkspace::View() const noexcept
    {
        const auto& s = *m_Impl;
        return s.Built ? PointLbvhView{s.Nodes, s.Count, s.Storage} : PointLbvhView{};
    }
    std::uint64_t PointLbvhWorkspace::AllocationCount() const noexcept
    {
        return m_Impl->Allocations;
    }
    std::uint64_t PointLbvhWorkspace::BuildCount() const noexcept
    {
        return m_Impl->Builds;
    }
} // namespace Extrinsic::Graphics
