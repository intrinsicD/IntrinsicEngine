module;
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
module Extrinsic.Graphics.PointKeypoints;

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
            std::uint64_t Nodes{}, Points{}, Slots{}, Scratch{}, Output{};
            std::uint32_t Count{}, First{}, QueryCount{}, MinimumNeighbors{};
            float SalientRadius{}, NonMaxRadius{};
            double Gamma21{}, Gamma32{};
            std::uint32_t Reserved{}, Mode{};
            std::uint64_t States{}, Scores{}, Masks{};
            std::uint32_t Visits{}, Resume{};
        };
        static_assert(sizeof(Push) == 120 && offsetof(Push, Gamma21) == 64);
        static_assert(sizeof(PointKeypointHeader) == 32);
    }
    struct PointKeypointWorkspace::Impl
    {
        RHI::IDevice& Device;
        RHI::PipelineHandle Pipeline{};
        RHI::BufferHandle Scratch{}, Output{}, States{};
        std::uint32_t Capacity{};
        explicit Impl(RHI::IDevice& device) : Device(device) {}
        ~Impl()
        {
            for (auto buffer : {Scratch, Output, States})
                if (buffer.IsValid()) Device.DestroyBuffer(buffer);
            if (Pipeline.IsValid()) Device.DestroyPipeline(Pipeline);
        }
        bool Reserve(std::uint32_t count)
        {
            if (!Device.IsOperational() || !Device.SupportsShaderFloat64()) return false;
            if (!Pipeline.IsValid())
            {
                Pipeline = CreateComputePipeline(Device, "shaders/point_keypoints.comp.spv", sizeof(Push), "PointKeypoints");
                if (!Pipeline.IsValid()) return false;
            }
            if (count <= Capacity) return true;
            auto allocate = [&](std::uint64_t bytes) {
                return Device.CreateBuffer({.SizeBytes = bytes,
                    .Usage = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferSrc | RHI::BufferUsage::TransferDst,
                    .HostVisible = true, .DebugName = "PointKeypoints.Workspace"});
            };
            const auto scratch = allocate((std::uint64_t(count) + (count+4095)/4096) * 8);
            const auto states = allocate(std::uint64_t(std::min(count,16384u)) * 384);
            const auto output = allocate(sizeof(PointKeypointHeader) + std::uint64_t(count) * 8);
            if (!scratch.IsValid() || !output.IsValid() || !states.IsValid())
            {
                if (scratch.IsValid()) Device.DestroyBuffer(scratch);
                if (states.IsValid()) Device.DestroyBuffer(states);
                if (output.IsValid()) Device.DestroyBuffer(output);
                return false;
            }
            for (auto buffer : {Scratch, Output, States})
                if (buffer.IsValid()) Device.DestroyBuffer(buffer);
            Scratch = scratch; Output = output; States = states; Capacity = count;
            return true;
        }
    };
    PointKeypointWorkspace::PointKeypointWorkspace(RHI::IDevice& device) : m_Impl(std::make_unique<Impl>(device)) {}
    PointKeypointWorkspace::~PointKeypointWorkspace() = default;
    RHI::BufferHandle PointKeypointWorkspace::RecordPage(RHI::ICommandContext& commands,
        std::uint64_t nodes, const GpuPropertyView& positions, std::uint64_t slots,
        std::uint32_t count, const PointKeypointParams& params, const PointKeypointPage& page,
        const GpuPropertyView& score, const GpuPropertyView& mask)
    {
        auto& s = *m_Impl;
        if (!nodes || !positions.Address || positions.Layout.ElementBytes()!=12 || !slots || count<2 || params.MinimumNeighbors>=count ||
            !page.Rows || page.First>=count || page.Rows>count-page.First || !page.Visits || page.Visits>1024 ||
            ((page.Mode==0 || page.Mode==2 || page.Mode==3) &&
                (page.Rows>16384 || std::uint64_t(page.Rows)*page.Visits>(1u<<24u))) ||
            !std::isfinite(params.Gamma21) || params.Gamma21<0 || params.Gamma21>1 ||
            !std::isfinite(params.Gamma32) || params.Gamma32<0 || params.Gamma32>1 ||
            !std::isfinite(params.SalientRadius) || params.SalientRadius<0 || params.SalientRadius>1e18f ||
            !std::isfinite(params.NonMaxRadius) || params.NonMaxRadius<0 || params.NonMaxRadius>1e18f ||
            (page.Mode==4 && (!score.Address || !mask.Address)) ||
            page.Mode>5 || !s.Reserve(count)) return {};
        // Only reset the page's unfinished count. All writes occur after the previous
        // completion; scale/error/count survive across pages and stages.
        if (page.Mode==0 && page.First==0 && !page.Resume) {
            const PointKeypointHeader header{};
            s.Device.WriteBuffer(s.Output,&header,sizeof(header));
        } else {
            const std::uint32_t zero{};
            s.Device.WriteBuffer(s.Output,&zero,sizeof(zero),offsetof(PointKeypointHeader,Reserved));
            if(page.Mode==2 && page.First==0 && !page.Resume)s.Device.WriteBuffer(s.Output,&zero,sizeof(zero));
        }
        Push push{nodes,positions.Address,slots,s.Device.GetBufferDeviceAddress(s.Scratch),
            s.Device.GetBufferDeviceAddress(s.Output),count,page.First,page.Rows,params.MinimumNeighbors,
            params.SalientRadius,params.NonMaxRadius,params.Gamma21,params.Gamma32,0,page.Mode,
            s.Device.GetBufferDeviceAddress(s.States),score.Address,mask.Address,page.Visits,page.Resume};
        if (!push.Scratch || !push.Output || !push.States) return {};
        for(auto buffer:{s.Output,s.Scratch,s.States})
            commands.BufferBarrier(buffer,RHI::MemoryAccess::TransferWrite|RHI::MemoryAccess::ShaderWrite,
                RHI::MemoryAccess::ShaderRead|RHI::MemoryAccess::ShaderWrite);
        commands.BufferBarrier(positions.Buffer,RHI::MemoryAccess::TransferWrite,RHI::MemoryAccess::ShaderRead);
        commands.BindPipeline(s.Pipeline);
        commands.PushConstants(&push,sizeof(push),0);
        commands.Dispatch(page.Mode==1?1:page.Mode==5?(page.Rows+4095)/4096:(page.Rows+63)/64,1,1);
        for(auto buffer:{s.Output,s.Scratch,s.States})
            commands.BufferBarrier(buffer,RHI::MemoryAccess::ShaderWrite,
                RHI::MemoryAccess::ShaderRead|RHI::MemoryAccess::TransferRead);
        if(page.Mode==4)for(auto buffer:{score.Buffer,mask.Buffer})
            commands.BufferBarrier(buffer,RHI::MemoryAccess::ShaderWrite,RHI::MemoryAccess::ShaderRead|RHI::MemoryAccess::TransferRead);
        return s.Output;
    }
}
