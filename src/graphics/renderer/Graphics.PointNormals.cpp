module;
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
module Extrinsic.Graphics.PointNormals;
import Extrinsic.Graphics.ComputeParallelPrimitives;
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
        RHI::PipelineHandle Pipeline{};
        RHI::BufferHandle Neighbors{}, Stats{};
        explicit Impl(RHI::IDevice& device) : Device(device) {}
        ~Impl()
        {
            for (auto b : {Neighbors, Stats}) if (b.IsValid()) Device.DestroyBuffer(b);
            if (Pipeline.IsValid()) Device.DestroyPipeline(Pipeline);
        }
    };
    PointNormalsWorkspace::PointNormalsWorkspace(RHI::IDevice& device) : m_Impl(std::make_unique<Impl>(device)) {}
    PointNormalsWorkspace::~PointNormalsWorkspace() = default;
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
        const auto width = p.RadiusSearch ? std::min(count, 1024u)
            : std::uint32_t(std::min(std::uint64_t(count), std::uint64_t(p.K) + 1));
        if (!count || first >= count || count > (1u << 20) || (!p.RadiusSearch && width > 64) ||
            !io.Positions.Address || !io.Nodes || !io.LiveSlots || !io.Output.Address ||
            !s.Device.IsOperational() || !s.Device.SupportsShaderFloat64()) return {};
        if (first == 0 && s.Pipeline.IsValid()) return {};
        if (first == 0)
            s.Pipeline = CreateComputePipeline(s.Device, "shaders/point_normals.comp.spv", sizeof(Push), "PointNormals");
        const std::uint32_t batch = RowsPerSubmission(p.RadiusSearch, p.BatchSize);
        const auto allocate = [&](std::uint64_t bytes) { return s.Device.CreateBuffer({.SizeBytes = bytes,
            .Usage = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferSrc | RHI::BufferUsage::TransferDst,
            .DebugName = "PointNormals.Scratch"}); };
        if (first == 0)
        {
            s.Neighbors = allocate(std::uint64_t(std::min(count, batch)) * width * 8);
            s.Stats = allocate(sizeof(PointNormalsGpuStats));
        }
        if (!s.Pipeline.IsValid() || !s.Neighbors.IsValid() || !s.Stats.IsValid()) return {};
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
            cmd.FillBuffer(s.Stats, 0, sizeof(PointNormalsGpuStats), 0);
            cmd.BufferBarrier(s.Stats, RHI::MemoryAccess::TransferWrite, shader);
        }
        else
        {
            cmd.BufferBarrier(io.Output.Buffer, shader, shader);
            cmd.BufferBarrier(s.Stats, shader | RHI::MemoryAccess::TransferRead, shader);
        }
        Push push{.Positions = io.Positions.Address, .Nodes = io.Nodes, .Slots = io.LiveSlots,
            .Neighbors = s.Device.GetBufferDeviceAddress(s.Neighbors), .Output = io.Output.Address,
            .Stats = s.Device.GetBufferDeviceAddress(s.Stats), .Epsilon = p.Epsilon, .CollinearRatio = p.CollinearRatio,
            .Count = count, .Width = width, .MinimumNeighbors = p.MinimumNeighbors, .RadiusSearch = p.RadiusSearch,
            .Radius = p.Radius, .FallbackX = p.Fallback[0], .FallbackY = p.Fallback[1], .FallbackZ = p.Fallback[2]};
        cmd.BindPipeline(s.Pipeline);
        push.First = first;
        push.Threads = std::min(count - first, batch);
        cmd.PushConstants(&push, sizeof(push), 0);
        cmd.Dispatch((push.Threads + 63) / 64, 1, 1);
        cmd.BufferBarrier(s.Neighbors, shader, shader);
        for (auto b : {io.Output.Buffer, s.Stats})
            cmd.BufferBarrier(b, shader, RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::TransferRead);
        return s.Stats;
    }
}
