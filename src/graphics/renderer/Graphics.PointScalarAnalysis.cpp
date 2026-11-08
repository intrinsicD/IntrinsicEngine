module;
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <cmath>
#include <numbers>
#include <limits>
#include <memory>
module Extrinsic.Graphics.PointScalarAnalysis;
import Extrinsic.Graphics.ComputeParallelPrimitives;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Types;
namespace Extrinsic::Graphics
{
    namespace
    {
        struct Push
        {
            std::uint64_t Positions{}, Nodes{}, Slots{}, Neighbors{}, Nearest{}, Output{}, Stats{};
            double SupportRadius{};
            std::uint32_t Count{}, Width{}, Method{}, Kernel{}, Inverse{}, Mode{};
            float Bandwidth{}, Scale{}, QueryRadius{};
            std::uint32_t First{}, Threads{};
            std::uint32_t Padding{};
            double CountBandwidthFactor{}, GaussianNormConstant{};
        };
        static_assert(sizeof(Push) == 128 && offsetof(Push, CountBandwidthFactor) == 112);
        static_assert(sizeof(PointScalarGpuStats) == 112 && offsetof(PointScalarGpuStats, Bandwidth) == 16);
    }
    struct PointScalarWorkspace::Impl
    {
        RHI::IDevice& Device;
        RHI::BufferManager& Scratch;
        RHI::PipelineHandle Pipeline{};
        RHI::BufferManager::BufferLease Neighbors{}, Means{}, Stats{};
        // ARCH-023: the scratch addresses, resolved with the scratch (device-owner thread). Prepared: Prepare checked
        // the device and sized the scratch for PreparedCount x PreparedWidth, so Record of that shape makes no device
        // call (a method worker records it).
        std::uint64_t NeighborsAddress{}, MeansAddress{}, StatsAddress{};
        bool Prepared{};
        std::uint32_t PreparedCount{}, PreparedWidth{};
        Impl(RHI::IDevice& device, RHI::BufferManager& scratch) : Device(device), Scratch(scratch) {}
        ~Impl()
        {
            if (Pipeline.IsValid())
                Device.DestroyPipeline(Pipeline);
        }
        // Repeated runs keep the pipeline and grow only an undersized scratch buffer; every
        // run zeroes the stats and indexes scratch rows by its own count and width.
        bool Ensure(std::uint64_t neighborBytes, std::uint64_t meanBytes)
        {
            if (!Pipeline.IsValid())
                Pipeline = CreateComputePipeline(
                    Device, "shaders/point_scalar_analysis.comp.spv", sizeof(Push), "PointScalarAnalysis");
            const auto desc = [](std::uint64_t bytes)
            {
                return RHI::BufferDesc{.SizeBytes = bytes,
                    .Usage = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferSrc | RHI::BufferUsage::TransferDst,
                    .DebugName = "PointScalarAnalysis.Scratch"};
            };
            if (!Pipeline.IsValid() || !Scratch.EnsureScratch(Neighbors, desc(neighborBytes)) ||
                !Scratch.EnsureScratch(Means, desc(meanBytes)) ||
                !Scratch.EnsureScratch(Stats, desc(sizeof(PointScalarGpuStats))))
                return false;
            NeighborsAddress = Device.GetBufferDeviceAddress(Neighbors.GetHandle());
            MeansAddress = Device.GetBufferDeviceAddress(Means.GetHandle());
            StatsAddress = Device.GetBufferDeviceAddress(Stats.GetHandle());
            return true;
        }
        // A run of `count` rows and `width` neighbours may record: prepared for that shape (no device call), or the
        // device checked and the scratch ensured now (device-owner thread).
        bool Ready(const std::uint32_t count, const std::uint32_t width)
        {
            if (Prepared)
                return count == PreparedCount && width == PreparedWidth;
            return Device.IsOperational() && Device.SupportsShaderFloat64() &&
                Ensure(std::uint64_t(count) * width * 8, std::uint64_t(count) * 16);
        }
    };
    std::uint32_t PointScalarNeighborWidth(const PointScalarGpuParams& p, std::uint32_t count)
    {
        if (!count || p.Method > 2)
            return 0;
        if (p.Method == 2)
            return std::min(std::max(1u, p.Capacity), count);
        return std::uint32_t(std::min(std::uint64_t(count), std::uint64_t(std::max(p.K, p.Method == 0 ? 2u : 1u)) + 1));
    }
    PointScalarWorkspace::PointScalarWorkspace(RHI::IDevice& device, RHI::BufferManager& scratch)
        : m_Impl(std::make_unique<Impl>(device, scratch))
    {
    }
    PointScalarWorkspace::~PointScalarWorkspace() = default;
    void PointScalarWorkspace::ReleaseScratch()
    {
        m_Impl->Neighbors = {};
        m_Impl->Means = {};
        m_Impl->Stats = {};
        m_Impl->Prepared = false;
    }
    bool PointScalarWorkspace::Prepare(const PointScalarGpuParams& p, const std::uint32_t liveCount)
    {
        auto& s = *m_Impl;
        s.Prepared = false;
        const auto width = PointScalarNeighborWidth(p, liveCount);
        if (!width || !liveCount || liveCount > (1u << 20) || width > 4096 ||
            std::uint64_t(liveCount) * width > (1u << 24) || !s.Ready(liveCount, width))
            return false;
        s.PreparedCount = liveCount;
        s.PreparedWidth = width;
        s.Prepared = true;
        return true;
    }
    RHI::BufferHandle PointScalarWorkspace::Record(
        RHI::ICommandContext& cmd, const PointScalarGpuParams& p, const PointScalarResidentIo& io)
    {
        auto& s = *m_Impl;
        const auto count = io.LiveCount;
        const auto width = PointScalarNeighborWidth(p, count);
        if (!width || !count || count > (1u << 20) || width > 4096 || std::uint64_t(count) * width > (1u << 24) ||
            !io.Positions.Address || !io.Nodes || !io.LiveSlots || !io.Output.Address || !s.Ready(count, width))
            return {};
        const auto neighbors = s.Neighbors.GetHandle(), means = s.Means.GetHandle(), stats = s.Stats.GetHandle();
        const auto shader = RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::ShaderWrite;
        cmd.BufferBarrier(
            io.Positions.Buffer, RHI::MemoryAccess::TransferWrite | shader, RHI::MemoryAccess::ShaderRead);
        cmd.BufferBarrier(io.Output.Buffer, shader | RHI::MemoryAccess::TransferRead, RHI::MemoryAccess::TransferWrite);
        if (io.Base.Address)
        {
            cmd.BufferBarrier(
                io.Base.Buffer, RHI::MemoryAccess::TransferWrite | shader, RHI::MemoryAccess::TransferRead);
            cmd.CopyBuffer(io.Base.Buffer, io.Output.Buffer, 0, 0, io.Output.Bytes);
        }
        else
            cmd.FillBuffer(io.Output.Buffer, 0, io.Output.Bytes, 0);
        cmd.BufferBarrier(io.Output.Buffer, RHI::MemoryAccess::TransferWrite, shader);
        cmd.BufferBarrier(stats, shader | RHI::MemoryAccess::TransferRead, RHI::MemoryAccess::TransferWrite);
        cmd.FillBuffer(stats, 0, sizeof(PointScalarGpuStats), 0);
        cmd.BufferBarrier(stats, RHI::MemoryAccess::TransferWrite, shader);
        // A reused workspace: the previous run's scratch reads precede these writes.
        for (auto b : {neighbors, means})
            cmd.BufferBarrier(b, shader, RHI::MemoryAccess::ShaderWrite);
        Push push{.Positions = io.Positions.Address,
            .Nodes = io.Nodes,
            .Slots = io.LiveSlots,
            .Neighbors = s.NeighborsAddress,
            .Nearest = s.MeansAddress,
            // Distinct float coordinates cannot lie inside a double-subnormal support.
            // The shader handles coincident points before testing this zero support.
            .Output = io.Output.Address,
            .Stats = s.StatsAddress,
            .SupportRadius = p.SupportRadius < std::numeric_limits<double>::min() ? 0.0 : p.SupportRadius,
            .Count = count,
            .Width = width,
            .Method = p.Method,
            .Kernel = p.Kernel,
            .Inverse = p.Inverse,
            .Bandwidth = p.Bandwidth,
            .Scale = p.Scale,
            .QueryRadius = p.QueryRadius,
            .CountBandwidthFactor = std::pow(double(count), -0.2),
            .GaussianNormConstant = std::pow(2.0 * std::numbers::pi_v<double>, 1.5)};
        cmd.BindPipeline(s.Pipeline);
        const auto dispatch = [&](std::uint32_t mode, std::uint32_t threads)
        {
            push.Mode = mode;
            for (std::uint32_t first = 0; first < threads; first += 65535u * 64u)
            {
                push.First = first;
                push.Threads = std::min(threads - first, 65535u * 64u);
                cmd.PushConstants(&push, sizeof(push), 0);
                cmd.Dispatch((push.Threads + 63) / 64, 1, 1);
            }
        };
        dispatch(0, count);
        for (auto b : {neighbors, means, io.Output.Buffer, stats})
            cmd.BufferBarrier(b, shader, shader);
        dispatch(1, 1);
        for (auto b : {stats, io.Output.Buffer})
            cmd.BufferBarrier(b, shader, shader);
        if (p.Method == 0)
        {
            dispatch(2, count);
            for (auto b : {means, io.Output.Buffer, stats})
                cmd.BufferBarrier(b, shader, shader);
        }
        dispatch(3, 1);
        for (auto b : {io.Output.Buffer, stats})
            cmd.BufferBarrier(b, shader, RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::TransferRead);
        return stats;
    }
}
