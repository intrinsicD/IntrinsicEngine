module;
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
module Extrinsic.Graphics.PointLBVH;

import Extrinsic.Graphics.ComputeParallelPrimitives;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.CommandContext;
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
        struct TransformPush
        {
            std::uint64_t Source{}, Pose{}, Destination{};
            std::uint32_t Count{}, Stride{};
        };
        static_assert(sizeof(TransformPush) == 32);
        struct QueryPush
        {
            std::uint64_t Nodes{}, Bounds{}, Queries{}, Neighbors{}, Headers{};
            std::uint32_t PointCount{}, QueryCount{}, Stride{}, Capacity{};
            float Radius{};
            std::uint32_t KNearestCount{};
            std::uint64_t ExcludedIndices{};
        };
        // point_brute_force_query.comp: QueryPush with target rows in place of the hierarchy.
        struct BruteForcePush
        {
            std::uint64_t Targets{}, TargetIndices{}, Queries{}, Neighbors{}, Headers{};
            std::uint32_t TargetCount{}, QueryCount{}, Stride{}, Capacity{};
            float Radius{};
            std::uint32_t KNearestCount{};
            std::uint64_t ExcludedIndices{};
            std::uint32_t TargetStride{}, Reserved{};
        };
        static_assert(sizeof(BuildPush) == 64 && sizeof(QueryPush) == 72 && sizeof(BruteForcePush) == 80);
        constexpr std::uint32_t kMortonBits = 30u; // lbvh_morton.comp: 10 bits per axis
        constexpr ParallelPrimitiveKind kSort[] = {ParallelPrimitiveKind::RadixSort};
    } // namespace
    struct PointLbvhWorkspace::Impl
    {
        explicit Impl(RHI::IDevice& device) : Device(device)
        {
        }
        ~Impl()
        {
            for (auto buffer : {Storage, SortScratch})
                if (buffer.IsValid())
                    Device.DestroyBuffer(buffer);
            DestroyParallelPrimitivePipelines(Device, Sort);
            for (auto p : Pipelines)
                if (p.IsValid())
                    Device.DestroyPipeline(p);
        }
        bool PipelinesReady()
        {
            constexpr std::array names{"lbvh_bounds", "lbvh_morton", "lbvh_build", "lbvh_query"};
            for (std::size_t i = 0; i < names.size(); ++i)
            {
                if (Pipelines[i].IsValid())
                    continue;
                Pipelines[i] = CreateComputePipeline(Device, (std::string("shaders/") + names[i] + ".comp.spv").c_str(),
                    std::uint32_t(i == 3 ? sizeof(QueryPush) : sizeof(BuildPush)), names[i]);
                if (!Pipelines[i].IsValid())
                    return false;
            }
            return CreateParallelPrimitivePipelines(Device, Sort, kSort);
        }
        // 4: the query transform, 5: brute force; created on first use (owner) or by PreparePipelines.
        bool QueryPipeline(std::size_t i)
        {
            if (!Pipelines[i].IsValid())
                Pipelines[i] = i == 4 ? CreateComputePipeline(Device,
                                            "shaders/lbvh_transform_queries.comp.spv",
                                            sizeof(TransformPush),
                                            "LBVH query transform")
                                      : CreateComputePipeline(Device,
                                            "shaders/point_brute_force_query.comp.spv",
                                            sizeof(BruteForcePush),
                                            "Point brute-force query");
            return Pipelines[i].IsValid();
        }
        RHI::IDevice& Device;
        RHI::BufferHandle Storage{}, SortScratch{};
        std::uint64_t SortScratchAddress{}; // resolved with the storage, for a resolved build's sort
        std::array<RHI::PipelineHandle, 6> Pipelines{}; // 4: query transform, 5: brute force
        ParallelPrimitivePipelineSet Sort{}; // radix sort of the (Morton code, index) keys
        std::uint32_t Capacity{}, Count{}, Padded{};
        std::uint64_t Allocations{}, Builds{}, Keys{}, Nodes{}, Bounds{}, Bytes{};
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
        // Scratch for the largest count this capacity holds covers every smaller build.
        const auto sortPlan = ComputeRadixSortDispatchPlan(padded, 1u, kMortonBits);
        auto scratch = sortPlan.ScratchBytes
            ? s.Device.CreateBuffer(BuildParallelPrimitiveScratchBufferDesc(sortPlan, "PointLBVH.SortScratch"))
            : RHI::BufferHandle{};
        auto bda = buffer.IsValid() ? s.Device.GetBufferDeviceAddress(buffer) : 0u;
        const auto scratchBda = scratch.IsValid() ? s.Device.GetBufferDeviceAddress(scratch) : 0u;
        if (!bda || (sortPlan.ScratchBytes && !scratch.IsValid()))
        {
            for (auto b : {buffer, scratch})
                if (b.IsValid())
                    s.Device.DestroyBuffer(b);
            return false;
        }
        for (auto b : {s.Storage, s.SortScratch})
            if (b.IsValid())
                s.Device.DestroyBuffer(b);
        s.Storage = buffer;
        s.SortScratch = scratch;
        s.SortScratchAddress = scratchBda;
        s.Capacity = padded;
        s.Bytes = bytes + sortPlan.ScratchBytes;
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
        // Resolved (ARCH-023): the caller's address, no readiness query; Reserve created the pipelines.
        const bool resolved = points.Address != 0u;
        if (points.Count > s.Capacity || points.Stride < 12 || points.Stride % 4 || points.Offset % 4 ||
            !s.Storage.IsValid() || (resolved ? objectIndices.IsValid() : !s.Device.IsOperational()))
            return false;
        const auto source = resolved ? points.Address : s.Device.GetBufferDeviceAddress(points.Buffer);
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
        // Stable by Morton code over records in index order: the (code, index) order the build
        // expects; the invalid padding records past Count stay in place.
        if (!RecordGpuRadixSort({.Device = &s.Device,
                                    .CommandContext = &cmd,
                                    .Pipelines = s.Sort,
                                    .Elements = s.Storage,
                                    .ElementsOffsetBytes = s.Keys - s.Bounds,
                                    .Scratch = s.SortScratch,
                                    .ElementCount = points.Count,
                                    .KeyWords = 1u,
                                    .KeyBits = kMortonBits,
                                    .ElementsAddress = resolved ? s.Bounds : 0u,
                                    .ScratchAddress = resolved ? s.SortScratchAddress : 0u})
                .Succeeded())
            return false;
        dispatch(2, (std::max(points.Count, 1u) + 255) / 256);
        s.Count = points.Count;
        s.Padded = push.Padded;
        s.Built = true;
        ++s.Builds;
        return true;
    }
    bool PointLbvhWorkspace::PreparePipelines(const bool bruteForce, const bool transform)
    {
        return (!bruteForce || m_Impl->QueryPipeline(5)) && (!transform || m_Impl->QueryPipeline(4));
    }
    bool PointLbvhWorkspace::RecordQuery(RHI::ICommandContext& cmd, const PointLbvhQuery& query)
    {
        return RecordQueries(cmd, query, nullptr, {});
    }
    bool PointLbvhWorkspace::RecordBruteForceQuery(
        RHI::ICommandContext& cmd, PointLbvhInput targets, RHI::BufferHandle targetIndices, const PointLbvhQuery& query)
    {
        return RecordQueries(cmd, query, &targets, targetIndices);
    }
    bool PointLbvhWorkspace::RecordQueries(RHI::ICommandContext& cmd,
        const PointLbvhQuery& query,
        const PointLbvhInput* targets,
        RHI::BufferHandle targetIndices)
    {
        auto& s = *m_Impl;
        if (targets
                ? (targets->Count > (1u << 20u) || targets->Stride < 12 || targets->Stride % 4 || targets->Offset % 4 ||
                      !PointBruteForceRefusal(query.Queries.Count, targets->Count, query.Capacity).empty())
                : !s.Built)
            return false;
        // Resolved (ARCH-023): every address is the caller's and no pipeline is created; no device call at all.
        const bool resolved = query.Queries.Address != 0u;
        const auto address = [&](RHI::BufferHandle buffer, std::uint64_t given) -> std::uint64_t
        {
            return resolved ? given : s.Device.GetBufferDeviceAddress(buffer);
        };
        if ((!resolved && !s.Device.IsOperational()) || query.Queries.Stride < 12 || query.Queries.Stride % 4 ||
            query.Queries.Offset % 4 || query.Queries.Count > (1u << 20u) || query.Capacity > 1024 ||
            !std::isfinite(query.Radius) || (query.Radius < 0 && query.Radius != -1) || query.Radius > 1e18f ||
            (query.KNearestCount
                    ? (query.KNearestCount > 64 || query.Capacity != query.KNearestCount || query.Radius != -1)
                    : (query.Radius == -1 && query.Capacity != 1)))
            return false;
        if (!query.Queries.Count)
            return true;
        const auto qb = resolved ? query.Queries.Address : s.Device.GetBufferDeviceAddress(query.Queries.Buffer),
                   nb = address(query.Neighbors, query.Resolved.Neighbors),
                   hb = address(query.Headers, query.Resolved.Headers);
        if (!qb || !nb || !hb || query.Queries.Offset > ~std::uint64_t{} - qb ||
            query.HeaderOffset % 8u || query.HeaderOffset > ~std::uint64_t{} - hb)
            return false;
        const auto excluded =
            query.ExcludedIndices.IsValid() ? address(query.ExcludedIndices, query.Resolved.ExcludedIndices) : 0;
        if (query.ExcludedIndices.IsValid() && !excluded) return false;
        std::uint64_t targetAddress = 0, indexAddress = 0;
        if (targets)
        {
            targetAddress =
                targets->Count ? (resolved ? targets->Address : s.Device.GetBufferDeviceAddress(targets->Buffer)) : 0;
            indexAddress = targetIndices.IsValid() ? address(targetIndices, query.Resolved.TargetIndices) : 0;
            if ((targets->Count && (!targetAddress || targets->Offset > ~std::uint64_t{} - targetAddress)) ||
                (targetIndices.IsValid() && !indexAddress))
                return false;
            if (resolved ? !s.Pipelines[5].IsValid() : !s.QueryPipeline(5))
                return false;
            if (targetAddress)
                targetAddress += targets->Offset;
        }
        auto queryAddress = qb + query.Queries.Offset;
        auto queryStride = query.Queries.Stride;
        auto queryBuffer = query.Queries.Buffer;
        if (query.Transform.IsValid() != query.TransformedQueries.IsValid()) return false;
        if (query.Transform.IsValid())
        {
            // A resolved transform's float64 support was checked when its pipeline was prepared.
            if ((!resolved && !s.Device.SupportsShaderFloat64()) || query.TransformedQueries == query.Queries.Buffer)
                return false;
            const auto matrix = address(query.Transform, query.Resolved.Transform);
            const auto output = address(query.TransformedQueries, query.Resolved.TransformedQueries);
            if (!matrix || !output) return false;
            if (resolved ? !s.Pipelines[4].IsValid() : !s.QueryPipeline(4))
                return false;
            for (auto input : {query.Queries.Buffer, query.Transform})
                cmd.BufferBarrier(input, RHI::MemoryAccess::TransferWrite | RHI::MemoryAccess::ShaderWrite,
                                  RHI::MemoryAccess::ShaderRead);
            cmd.BufferBarrier(query.TransformedQueries, RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::ShaderWrite,
                              RHI::MemoryAccess::ShaderWrite);
            const TransformPush transform{queryAddress, matrix, output, query.Queries.Count, queryStride};
            cmd.BindPipeline(s.Pipelines[4]);
            cmd.PushConstants(&transform, sizeof(transform), 0);
            cmd.Dispatch((query.Queries.Count + 255u) / 256u, 1, 1);
            queryAddress = output;
            queryStride = 12;
            queryBuffer = query.TransformedQueries;
        }
        if (targets)
        {
            for (auto input : {targets->Count ? targets->Buffer : RHI::BufferHandle{}, targetIndices})
                if (input.IsValid())
                    cmd.BufferBarrier(input,
                        RHI::MemoryAccess::TransferWrite | RHI::MemoryAccess::ShaderWrite,
                        RHI::MemoryAccess::ShaderRead);
        }
        const auto headers = hb + query.HeaderOffset;
        cmd.BufferBarrier(queryBuffer,
                          RHI::MemoryAccess::TransferWrite | RHI::MemoryAccess::ShaderWrite,
                          RHI::MemoryAccess::ShaderRead);
        if (query.ExcludedIndices.IsValid())
            cmd.BufferBarrier(query.ExcludedIndices,
                              RHI::MemoryAccess::TransferWrite | RHI::MemoryAccess::ShaderWrite,
                              RHI::MemoryAccess::ShaderRead);
        if (targets)
        {
            // The hierarchy fields stay unread: the owner may Reserve or build this workspace's hierarchy while a
            // worker records brute-force pages against it.
            const BruteForcePush brute{.Targets = targetAddress,
                .TargetIndices = indexAddress,
                .Queries = queryAddress,
                .Neighbors = nb,
                .Headers = headers,
                .TargetCount = targets->Count,
                .QueryCount = query.Queries.Count,
                .Stride = queryStride,
                .Capacity = query.Capacity,
                .Radius = query.Radius,
                .KNearestCount = query.KNearestCount,
                .ExcludedIndices = excluded,
                .TargetStride = targets->Stride};
            cmd.BindPipeline(s.Pipelines[5]);
            cmd.PushConstants(&brute, sizeof(brute), 0);
        }
        else
        {
            const QueryPush push{.Nodes = s.Nodes,
                .Bounds = s.Bounds,
                .Queries = queryAddress,
                .Neighbors = nb,
                .Headers = headers,
                .PointCount = s.Count,
                .QueryCount = query.Queries.Count,
                .Stride = queryStride,
                .Capacity = query.Capacity,
                .Radius = query.Radius,
                .KNearestCount = query.KNearestCount,
                .ExcludedIndices = excluded};
            cmd.BindPipeline(s.Pipelines[3]);
            cmd.PushConstants(&push, sizeof(push), 0);
        }
        cmd.Dispatch((query.Queries.Count + 255) / 256, 1, 1);
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
    std::uint64_t PointLbvhWorkspace::DeviceBytes() const noexcept
    {
        return m_Impl->Bytes;
    }
} // namespace Extrinsic::Graphics
