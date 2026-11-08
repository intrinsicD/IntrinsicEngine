module;
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
module Extrinsic.Graphics.PointNormals;
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
            std::uint64_t Positions{}, Nodes{}, Slots{}, Neighbors{}, Output{}, Stats{};
            double Epsilon{}, CollinearRatio{};
            std::uint32_t Count{}, Width{}, MinimumNeighbors{}, RadiusSearch{};
            float Radius{}, FallbackX{}, FallbackY{}, FallbackZ{};
            std::uint32_t First{}, Threads{};
        };
        static_assert(sizeof(Push) == 104 && offsetof(Push, First) == 96);
    }
    struct PointNormalsWorkspace::Impl
    {
        RHI::IDevice& Device;
        RHI::BufferManager& Scratch;
        RHI::PipelineHandle Pipeline{};
        RHI::BufferManager::BufferLease Neighbors{}, Stats{};
        std::uint64_t PageBytes{}; // the run's page scratch; 0: no run started
        // ARCH-023: the scratch addresses, resolved with it (device-owner thread). Prepared: Prepare checked the device
        // and sized the scratch, so every page of that run (page 0 included) makes no device call.
        std::uint64_t NeighborsAddress{}, StatsAddress{};
        bool Prepared{};
        Impl(RHI::IDevice& device, RHI::BufferManager& scratch) : Device(device), Scratch(scratch) {}
        // Page 0's scratch for `pageBytes` (pipeline kept, only undersized scratch replaced), addresses resolved.
        bool Begin(const std::uint64_t pageBytes)
        {
            const auto desc = [](std::uint64_t bytes)
            {
                return RHI::BufferDesc{.SizeBytes = bytes,
                    .Usage = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferSrc | RHI::BufferUsage::TransferDst,
                    .DebugName = "PointNormals.Scratch"};
            };
            PageBytes = 0;
            if (!Pipeline.IsValid())
                Pipeline =
                    CreateComputePipeline(Device, "shaders/point_normals.comp.spv", sizeof(Push), "PointNormals");
            if (!Pipeline.IsValid() || !Scratch.EnsureScratch(Neighbors, desc(pageBytes)) ||
                !Scratch.EnsureScratch(Stats, desc(sizeof(PointNormalsGpuStats))))
                return false;
            NeighborsAddress = Device.GetBufferDeviceAddress(Neighbors.GetHandle());
            StatsAddress = Device.GetBufferDeviceAddress(Stats.GetHandle());
            PageBytes = pageBytes;
            return true;
        }
        ~Impl()
        {
            if (Pipeline.IsValid()) Device.DestroyPipeline(Pipeline);
        }
    };
    PointNormalsWorkspace::PointNormalsWorkspace(RHI::IDevice& device, RHI::BufferManager& scratch)
        : m_Impl(std::make_unique<Impl>(device, scratch))
    {
    }
    PointNormalsWorkspace::~PointNormalsWorkspace() = default;
    void PointNormalsWorkspace::ReleaseScratch()
    {
        m_Impl->Neighbors = {};
        m_Impl->Stats = {};
        m_Impl->PageBytes = 0;
        m_Impl->Prepared = false;
    }
    namespace
    {
        std::uint32_t NormalsWidth(const PointNormalsGpuParams& p, const std::uint32_t count)
        {
            return p.RadiusSearch ? std::min(count, 1024u)
                                  : std::uint32_t(std::min(std::uint64_t(count), std::uint64_t(p.K) + 1));
        }
    }
    bool PointNormalsWorkspace::Prepare(const PointNormalsGpuParams& p, const std::uint32_t liveCount)
    {
        auto& s = *m_Impl;
        s.Prepared = false;
        const auto width = NormalsWidth(p, liveCount);
        if (!liveCount || liveCount > (1u << 20) || (!p.RadiusSearch && width > 64) || !s.Device.IsOperational() ||
            !s.Device.SupportsShaderFloat64())
            return false;
        const std::uint64_t pageBytes =
            std::uint64_t(std::min(liveCount, RowsPerSubmission(p.RadiusSearch, p.BatchSize))) * width * 8;
        s.Prepared = s.Begin(pageBytes);
        return s.Prepared;
    }
    std::uint32_t PointNormalsWorkspace::RowsPerSubmission(bool radiusSearch, std::uint32_t batchSize)
    {
        (void)radiusSearch; // radius rows exit early on overflow, so both modes page alike
        return std::clamp(batchSize, 64u, 4096u);
    }
    RHI::BufferHandle PointNormalsWorkspace::Record(RHI::ICommandContext& cmd, const PointNormalsGpuParams& p,
                                                   const PointNormalsResidentIo& io, std::uint32_t first)
    {
        auto& s = *m_Impl;
        const auto count = io.LiveCount;
        const auto width = NormalsWidth(p, count);
        if (!count || first >= count || count > (1u << 20) || (!p.RadiusSearch && width > 64) ||
            !io.Positions.Address || !io.Nodes || !io.LiveSlots || !io.Output.Address ||
            (!s.Prepared && (!s.Device.IsOperational() || !s.Device.SupportsShaderFloat64())))
            return {};
        const std::uint32_t batch = RowsPerSubmission(p.RadiusSearch, p.BatchSize);
        const std::uint64_t pageBytes = std::uint64_t(std::min(count, batch)) * width * 8;
        // A run starts at page 0: the pipeline is kept and only an undersized scratch buffer is
        // replaced. Later pages continue that run, so they must fit what page 0 prepared.
        // A prepared run's pages (page 0 included) only check the prepared scratch.
        if (first == 0 && !s.Prepared)
        {
            if (!s.Begin(pageBytes))
                return {};
        }
        else if (!s.PageBytes || pageBytes > s.PageBytes) return {};
        const auto neighbors = s.Neighbors.GetHandle(), stats = s.Stats.GetHandle();
        const auto shader = RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::ShaderWrite;
        cmd.BufferBarrier(io.Positions.Buffer, RHI::MemoryAccess::TransferWrite | shader, RHI::MemoryAccess::ShaderRead);
        if (first == 0)
        {
            cmd.BufferBarrier(io.Output.Buffer, shader | RHI::MemoryAccess::TransferRead, RHI::MemoryAccess::TransferWrite);
            if (io.Base.Address)
            {
                cmd.BufferBarrier(io.Base.Buffer, RHI::MemoryAccess::TransferWrite | shader, RHI::MemoryAccess::TransferRead);
                cmd.CopyBuffer(io.Base.Buffer, io.Output.Buffer, 0, 0, io.Output.Bytes);
            }
            else cmd.FillBuffer(io.Output.Buffer, 0, io.Output.Bytes, 0);
            cmd.BufferBarrier(io.Output.Buffer, RHI::MemoryAccess::TransferWrite, shader);
            // A reused workspace: the previous run's stats readback precedes the reset.
            cmd.BufferBarrier(stats, shader | RHI::MemoryAccess::TransferRead, RHI::MemoryAccess::TransferWrite);
            cmd.FillBuffer(stats, 0, sizeof(PointNormalsGpuStats), 0);
            cmd.BufferBarrier(stats, RHI::MemoryAccess::TransferWrite, shader);
        }
        else
        {
            cmd.BufferBarrier(io.Output.Buffer, shader, shader);
            cmd.BufferBarrier(stats, shader | RHI::MemoryAccess::TransferRead, shader);
        }
        Push push{.Positions = io.Positions.Address,
            .Nodes = io.Nodes,
            .Slots = io.LiveSlots,
            .Neighbors = s.NeighborsAddress,
            .Output = io.Output.Address,
            .Stats = s.StatsAddress,
            .Epsilon = p.Epsilon,
            .CollinearRatio = p.CollinearRatio,
            .Count = count,
            .Width = width,
            .MinimumNeighbors = p.MinimumNeighbors,
            .RadiusSearch = p.RadiusSearch,
            .Radius = p.Radius,
            .FallbackX = p.Fallback[0],
            .FallbackY = p.Fallback[1],
            .FallbackZ = p.Fallback[2]};
        cmd.BindPipeline(s.Pipeline);
        push.First = first;
        push.Threads = std::min(count - first, batch);
        cmd.PushConstants(&push, sizeof(push), 0);
        cmd.Dispatch((push.Threads + 63) / 64, 1, 1);
        cmd.BufferBarrier(neighbors, shader, shader);
        for (auto b : {io.Output.Buffer, stats})
            cmd.BufferBarrier(b, shader, RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::TransferRead);
        return stats;
    }
}
