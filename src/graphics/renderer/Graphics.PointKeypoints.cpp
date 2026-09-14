module;
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
module Extrinsic.Graphics.PointKeypoints;
import Extrinsic.Core.Filesystem.PathResolver;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Types;

namespace Extrinsic::Graphics
{
    namespace
    {
        struct Push
        {
            std::uint64_t Nodes{}, Points{}, Slots{}, Scratch{}, Output{};
            std::uint32_t Count{}, First{}, QueryCount{}, MinimumNeighbors{};
            float SalientRadius{}, NonMaxRadius{};
            double Gamma21{}, Gamma32{};
            std::uint32_t Capacity{}, Mode{};
        };
        static_assert(sizeof(Push) == 88 && offsetof(Push, Gamma21) == 64);
        static_assert(sizeof(PointKeypointHeader) == 32 && sizeof(PointKeypointValue) == 8);
    }
    struct PointKeypointWorkspace::Impl
    {
        RHI::IDevice& Device;
        RHI::PipelineHandle Pipeline{};
        RHI::BufferHandle Scratch{}, Output{};
        std::uint32_t Capacity{};
        explicit Impl(RHI::IDevice& device) : Device(device) {}
        ~Impl()
        {
            for (auto buffer : {Scratch, Output})
                if (buffer.IsValid()) Device.DestroyBuffer(buffer);
            if (Pipeline.IsValid()) Device.DestroyPipeline(Pipeline);
        }
        bool Reserve(std::uint32_t count)
        {
            if (!Device.IsOperational() || !Device.SupportsShaderFloat64()) return false;
            if (!Pipeline.IsValid())
            {
                const auto path = Core::Filesystem::GetShaderPath("shaders/point_keypoints.comp.spv");
                Pipeline = Device.CreatePipeline({.VertexShaderPath = {}, .FragmentShaderPath = {},
                    .ComputeShaderPath = path.c_str(), .PushConstantSize = sizeof(Push),
                    .DebugName = "PointKeypoints"});
                if (!Pipeline.IsValid()) return false;
            }
            if (count <= Capacity) return true;
            auto allocate = [&](std::uint64_t bytes) {
                return Device.CreateBuffer({.SizeBytes = bytes,
                    .Usage = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferSrc | RHI::BufferUsage::TransferDst,
                    .HostVisible = true, .DebugName = "PointKeypoints.Workspace"});
            };
            const auto scratch = allocate(std::uint64_t(count) * 8);
            const auto output = allocate(sizeof(PointKeypointHeader) + std::uint64_t(count) * sizeof(PointKeypointValue));
            if (!scratch.IsValid() || !output.IsValid())
            {
                if (scratch.IsValid()) Device.DestroyBuffer(scratch);
                if (output.IsValid()) Device.DestroyBuffer(output);
                return false;
            }
            for (auto buffer : {Scratch, Output})
                if (buffer.IsValid()) Device.DestroyBuffer(buffer);
            Scratch = scratch; Output = output; Capacity = count;
            return true;
        }
    };
    PointKeypointWorkspace::PointKeypointWorkspace(RHI::IDevice& device) : m_Impl(std::make_unique<Impl>(device)) {}
    PointKeypointWorkspace::~PointKeypointWorkspace() = default;
    RHI::BufferHandle PointKeypointWorkspace::Record(RHI::ICommandContext& commands,
        std::uint64_t nodes, std::uint64_t points, std::uint64_t slots,
        std::uint32_t count, const PointKeypointParams& params)
    {
        auto& s = *m_Impl;
        if (!nodes || !points || !slots || count < 2 || count > (1u << 20) ||
            params.MinimumNeighbors >= count || !params.RadiusCapacity || params.RadiusCapacity > 1024 ||
            !params.BatchSize || params.BatchSize > 16384 ||
            !std::isfinite(params.Gamma21) || !std::isfinite(params.Gamma32) ||
            params.Gamma21 < 0 || params.Gamma21 > 1 || params.Gamma32 < 0 || params.Gamma32 > 1 ||
            !std::isfinite(params.SalientRadius) || !std::isfinite(params.NonMaxRadius) ||
            params.SalientRadius < 0 || params.NonMaxRadius < 0 ||
            params.SalientRadius > 1e18f || params.NonMaxRadius > 1e18f || !s.Reserve(count)) return {};
        const PointKeypointHeader header{};
        s.Device.WriteBuffer(s.Output, &header, sizeof(header));
        Push push{nodes, points, slots, s.Device.GetBufferDeviceAddress(s.Scratch),
            s.Device.GetBufferDeviceAddress(s.Output), count, 0, count, params.MinimumNeighbors,
            params.SalientRadius, params.NonMaxRadius, params.Gamma21, params.Gamma32, params.RadiusCapacity};
        if (!push.Scratch || !push.Output) return {};
        commands.BufferBarrier(s.Output, RHI::MemoryAccess::TransferWrite | RHI::MemoryAccess::ShaderRead,
                               RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::ShaderWrite);
        commands.BindPipeline(s.Pipeline);
        for (push.Mode = 0; push.Mode != 4; ++push.Mode)
        {
            for (push.First = 0; push.First < count; push.First += params.BatchSize)
            {
                push.QueryCount = std::min(params.BatchSize, count - push.First);
                commands.PushConstants(&push, sizeof(push), 0);
                commands.Dispatch(push.Mode == 1 ? 1 : (push.QueryCount + 63) / 64, 1, 1);
                if (push.Mode == 1) break;
            }
            for (auto buffer : {s.Scratch, s.Output})
                commands.BufferBarrier(buffer, RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::ShaderWrite,
                    RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::ShaderWrite | RHI::MemoryAccess::TransferRead);
        }
        return s.Output;
    }
}
