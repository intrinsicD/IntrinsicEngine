module;
#include <algorithm>
#include <cstdint>
#include <memory>
module Extrinsic.Graphics.OutlierAnalysis;
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
            std::uint64_t Positions{}, Nodes{}, Slots{}, Neighbors{}, Means{}, Score{}, Mask{}, Presentation{}, Stats{};
            std::uint32_t Count{}, Width{}, Method{}, Minimum{};
            float Radius{}, Multiplier{}, ScoreThreshold{};
            std::uint32_t Mode{}, First{}, Threads{};
        };
        static_assert(sizeof(Push) == 112);
        static_assert(sizeof(OutlierGpuStats) == 32);
    }
    struct OutlierWorkspace::Impl
    {
        RHI::IDevice& Device;
        RHI::BufferManager& Scratch;
        RHI::PipelineHandle Pipeline{};
        RHI::BufferManager::BufferLease Neighbors{}, Means{}, Stats{};
        // ARCH-023: the scratch addresses, resolved with the scratch (device-owner thread). Prepared: Prepare checked
        // the device and sized the scratch for its shape, so Record of that shape makes no device call.
        std::uint64_t NeighborsAddress{}, MeansAddress{}, StatsAddress{};
        bool Prepared{};
        std::uint32_t PreparedCount{}, PreparedWidth{};
        std::uint64_t PreparedScoreBytes{};
        Impl(RHI::IDevice& device, RHI::BufferManager& scratch) : Device(device), Scratch(scratch) {}
        ~Impl()
        {
            if (Pipeline.IsValid())
                Device.DestroyPipeline(Pipeline);
        }
        // Repeated runs keep the pipeline and grow only an undersized scratch buffer. The
        // reduction pass assigns every stats field, and scratch rows are written before read.
        bool Ensure(std::uint64_t neighborBytes, std::uint64_t meanBytes)
        {
            if (!Pipeline.IsValid())
                Pipeline =
                    CreateComputePipeline(Device, "shaders/outlier_analysis.comp.spv", sizeof(Push), "OutlierAnalysis");
            const auto desc = [](std::uint64_t bytes)
            {
                return RHI::BufferDesc{.SizeBytes = bytes,
                    .Usage = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferSrc,
                    .DebugName = "OutlierAnalysis.Scratch"};
            };
            if (!Pipeline.IsValid() || !Scratch.EnsureScratch(Neighbors, desc(neighborBytes)) ||
                !Scratch.EnsureScratch(Means, desc(meanBytes)) ||
                !Scratch.EnsureScratch(Stats, desc(sizeof(OutlierGpuStats))))
                return false;
            NeighborsAddress = Device.GetBufferDeviceAddress(Neighbors.GetHandle());
            MeansAddress = Device.GetBufferDeviceAddress(Means.GetHandle());
            StatsAddress = Device.GetBufferDeviceAddress(Stats.GetHandle());
            return true;
        }
        bool Ready(const std::uint32_t count, const std::uint32_t width, const std::uint64_t scoreBytes)
        {
            if (Prepared)
                return count == PreparedCount && width == PreparedWidth && scoreBytes == PreparedScoreBytes;
            return Device.IsOperational() && Device.SupportsShaderFloat64() &&
                Ensure(std::uint64_t(count) * width * 8, scoreBytes * 4);
        }
    };
    std::uint32_t OutlierNeighborWidth(std::uint32_t method, std::uint32_t k, std::uint32_t count)
    {
        if (!count || method > 2)
            return 0;
        if (method == 1)
            return 1;
        const auto neighbors = method == 0 ? k : std::max(k, 2u);
        return std::uint32_t(std::min(std::uint64_t(count), std::uint64_t(neighbors) + 1));
    }
    OutlierWorkspace::OutlierWorkspace(RHI::IDevice& device, RHI::BufferManager& scratch)
        : m_Impl(std::make_unique<Impl>(device, scratch))
    {
    }
    OutlierWorkspace::~OutlierWorkspace() = default;
    void OutlierWorkspace::ReleaseScratch()
    {
        m_Impl->Neighbors = {};
        m_Impl->Means = {};
        m_Impl->Stats = {};
        m_Impl->Prepared = false;
    }
    bool OutlierWorkspace::Prepare(
        const OutlierGpuParams& p, const std::uint32_t liveCount, const std::uint64_t scoreBytes)
    {
        auto& s = *m_Impl;
        s.Prepared = false;
        const auto width = OutlierNeighborWidth(p.Method, p.K, liveCount);
        if (!width || !liveCount || liveCount > (1u << 20) || width > 65 ||
            std::uint64_t(liveCount) * width > (1u << 24) || !s.Ready(liveCount, width, scoreBytes))
            return false;
        s.PreparedCount = liveCount;
        s.PreparedWidth = width;
        s.PreparedScoreBytes = scoreBytes;
        s.Prepared = true;
        return true;
    }
    RHI::BufferHandle OutlierWorkspace::Record(
        RHI::ICommandContext& cmd, const OutlierGpuParams& p, const OutlierResidentIo& io)
    {
        auto& s = *m_Impl;
        const auto count = io.LiveCount;
        const auto width = OutlierNeighborWidth(p.Method, p.K, count);
        if (!width || !count || count > (1u << 20) || width > 65 || std::uint64_t(count) * width > (1u << 24) ||
            !io.Positions.Address || !io.Nodes || !io.LiveSlots || !io.Score.Address || !io.Mask.Address ||
            !io.Presentation.Address || !s.Ready(count, width, io.Score.Bytes))
            return {};
        const auto neighbors = s.Neighbors.GetHandle(), means = s.Means.GetHandle(), stats = s.Stats.GetHandle();
        const auto shader = RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::ShaderWrite;
        cmd.BufferBarrier(
            io.Positions.Buffer, RHI::MemoryAccess::TransferWrite | shader, RHI::MemoryAccess::ShaderRead);
        const auto initialize = [&](const GpuPropertyView& output, const GpuPropertyView& base)
        {
            cmd.BufferBarrier(
                output.Buffer, shader | RHI::MemoryAccess::TransferRead, RHI::MemoryAccess::TransferWrite);
            if (base.Address)
            {
                cmd.BufferBarrier(
                    base.Buffer, RHI::MemoryAccess::TransferWrite | shader, RHI::MemoryAccess::TransferRead);
                cmd.CopyBuffer(base.Buffer, output.Buffer, 0, 0, output.Bytes);
            }
            else
                cmd.FillBuffer(output.Buffer, 0, output.Bytes, 0);
            cmd.BufferBarrier(output.Buffer, RHI::MemoryAccess::TransferWrite, shader);
        };
        initialize(io.Score, io.ScoreBase);
        initialize(io.Mask, io.MaskBase);
        initialize(io.Presentation, {});
        // A reused workspace: the previous run's scratch reads and stats readback precede these writes.
        for (auto b : {neighbors, means, stats})
            cmd.BufferBarrier(b, shader | RHI::MemoryAccess::TransferRead, RHI::MemoryAccess::ShaderWrite);
        Push push{.Positions = io.Positions.Address,
            .Nodes = io.Nodes,
            .Slots = io.LiveSlots,
            .Neighbors = s.NeighborsAddress,
            .Means = s.MeansAddress,
            .Score = io.Score.Address,
            .Mask = io.Mask.Address,
            .Presentation = io.Presentation.Address,
            .Stats = s.StatsAddress,
            .Count = count,
            .Width = width,
            .Method = p.Method,
            .Minimum = p.MinimumNeighbors,
            .Radius = p.Radius,
            .Multiplier = p.Multiplier,
            .ScoreThreshold = p.ScoreThreshold};
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
        dispatch(3, io.Mask.Layout.Count);
        cmd.BufferBarrier(io.Presentation.Buffer, RHI::MemoryAccess::ShaderWrite, RHI::MemoryAccess::ShaderWrite);
        dispatch(0, count);
        for (auto b : {neighbors, means, io.Score.Buffer})
            cmd.BufferBarrier(b, RHI::MemoryAccess::ShaderWrite, shader);
        if (p.Method == 2)
        {
            dispatch(1, count);
            for (auto b : {means, io.Score.Buffer})
                cmd.BufferBarrier(b, RHI::MemoryAccess::ShaderWrite, RHI::MemoryAccess::ShaderRead);
        }
        dispatch(2, 1);
        for (auto b : {io.Score.Buffer, io.Mask.Buffer, io.Presentation.Buffer, stats})
            cmd.BufferBarrier(b, shader, RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::TransferRead);
        return stats;
    }
}
