module;
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
module Extrinsic.Graphics.PointKeypoints;

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
        RHI::BufferManager& Scratch;
        RHI::PipelineHandle Pipeline{};
        RHI::BufferManager::BufferLease Scratches{}, Output{}, States{};
        std::uint32_t Capacity{};
        // ARCH-023: addresses resolved with the reservation (device-owner thread); Prepared: Prepare reserved, so a
        // page within Capacity makes no device call (a method worker records it).
        std::uint64_t ScratchesAddress{}, OutputAddress{}, StatesAddress{};
        bool Prepared{};
        Impl(RHI::IDevice& device, RHI::BufferManager& scratch) : Device(device), Scratch(scratch) {}
        ~Impl()
        {
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
            const auto desc = [](std::uint64_t bytes)
            {
                return RHI::BufferDesc{.SizeBytes = bytes,
                    .Usage = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferSrc | RHI::BufferUsage::TransferDst,
                    .HostVisible = true,
                    .DebugName = "PointKeypoints.Workspace"};
            };
            // All or nothing: a partial reservation keeps no capacity.
            Capacity = 0;
            if (!Scratch.EnsureScratch(Scratches, desc((std::uint64_t(count) + (count + 4095) / 4096) * 8)) ||
                !Scratch.EnsureScratch(States, desc(std::uint64_t(std::min(count, 16384u)) * 384)) ||
                !Scratch.EnsureScratch(Output, desc(sizeof(PointKeypointHeader) + std::uint64_t(count) * 8)))
                return false;
            ScratchesAddress = Device.GetBufferDeviceAddress(Scratches.GetHandle());
            OutputAddress = Device.GetBufferDeviceAddress(Output.GetHandle());
            StatesAddress = Device.GetBufferDeviceAddress(States.GetHandle());
            Capacity = count;
            return true;
        }
    };
    PointKeypointWorkspace::PointKeypointWorkspace(RHI::IDevice& device, RHI::BufferManager& scratch)
        : m_Impl(std::make_unique<Impl>(device, scratch))
    {
    }
    PointKeypointWorkspace::~PointKeypointWorkspace() = default;
    void PointKeypointWorkspace::ReleaseScratch()
    {
        m_Impl->Scratches = {};
        m_Impl->Output = {};
        m_Impl->States = {};
        m_Impl->Capacity = 0;
        m_Impl->Prepared = false;
    }
    bool PointKeypointWorkspace::Prepare(const std::uint32_t count)
    {
        auto& s = *m_Impl;
        s.Prepared = count >= 2u && s.Reserve(count);
        return s.Prepared;
    }
    RHI::BufferHandle PointKeypointWorkspace::RecordPage(RHI::ICommandContext& commands,
        std::uint64_t nodes, const GpuPropertyView& positions, std::uint64_t slots,
        std::uint32_t count, const PointKeypointParams& params, const PointKeypointPage& page,
        const GpuPropertyView& score, const GpuPropertyView& mask)
    {
        auto& s = *m_Impl;
        if (!nodes || !positions.Address || positions.Layout.ElementBytes() != 12 || !slots || count < 2 ||
            params.MinimumNeighbors >= count || !page.Rows || page.First >= count || page.Rows > count - page.First ||
            !page.Visits || page.Visits > 1024 ||
            ((page.Mode == 0 || page.Mode == 2 || page.Mode == 3) &&
                (page.Rows > 16384 || std::uint64_t(page.Rows) * page.Visits > (1u << 24u))) ||
            !std::isfinite(params.Gamma21) || params.Gamma21 < 0 || params.Gamma21 > 1 ||
            !std::isfinite(params.Gamma32) || params.Gamma32 < 0 || params.Gamma32 > 1 ||
            !std::isfinite(params.SalientRadius) || params.SalientRadius < 0 || params.SalientRadius > 1e18f ||
            !std::isfinite(params.NonMaxRadius) || params.NonMaxRadius < 0 || params.NonMaxRadius > 1e18f ||
            (page.Mode == 4 && (!score.Address || !mask.Address)) || page.Mode > 5 ||
            (s.Prepared ? count > s.Capacity : !s.Reserve(count)))
            return {};
        const auto scratch = s.Scratches.GetHandle(), output = s.Output.GetHandle(), states = s.States.GetHandle();
        // Only reset the page's unfinished count. All writes occur after the previous
        // completion; scale/error/count survive across pages and stages.
        // ARCH-023: recorded fills, not host writes, so a worker records the page; ordered after the previous page's
        // reads of the header by the barrier below.
        commands.BufferBarrier(output,
            RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::ShaderWrite | RHI::MemoryAccess::TransferRead,
            RHI::MemoryAccess::TransferWrite);
        if (page.Mode==0 && page.First==0 && !page.Resume) {
            // A value-initialized header is all zero bits (integers and +0.0f).
            commands.FillBuffer(output, 0u, sizeof(PointKeypointHeader), 0u);
        } else {
            commands.FillBuffer(output, offsetof(PointKeypointHeader, Reserved), sizeof(std::uint32_t), 0u);
            if (page.Mode == 2 && page.First == 0 && !page.Resume)
                commands.FillBuffer(output, 0u, sizeof(std::uint32_t), 0u);
        }
        Push push{nodes,
            positions.Address,
            slots,
            s.ScratchesAddress,
            s.OutputAddress,
            count,
            page.First,
            page.Rows,
            params.MinimumNeighbors,
            params.SalientRadius,
            params.NonMaxRadius,
            params.Gamma21,
            params.Gamma32,
            0,
            page.Mode,
            s.StatesAddress,
            score.Address,
            mask.Address,
            page.Visits,
            page.Resume};
        if (!push.Scratch || !push.Output || !push.States) return {};
        for (auto buffer : {output, scratch, states})
            commands.BufferBarrier(buffer,RHI::MemoryAccess::TransferWrite|RHI::MemoryAccess::ShaderWrite,
                RHI::MemoryAccess::ShaderRead|RHI::MemoryAccess::ShaderWrite);
        commands.BufferBarrier(positions.Buffer,RHI::MemoryAccess::TransferWrite,RHI::MemoryAccess::ShaderRead);
        commands.BindPipeline(s.Pipeline);
        commands.PushConstants(&push,sizeof(push),0);
        commands.Dispatch(page.Mode==1?1:page.Mode==5?(page.Rows+4095)/4096:(page.Rows+63)/64,1,1);
        for (auto buffer : {output, scratch, states})
            commands.BufferBarrier(buffer,RHI::MemoryAccess::ShaderWrite,
                RHI::MemoryAccess::ShaderRead|RHI::MemoryAccess::TransferRead);
        if(page.Mode==4)for(auto buffer:{score.Buffer,mask.Buffer})
            commands.BufferBarrier(buffer,RHI::MemoryAccess::ShaderWrite,RHI::MemoryAccess::ShaderRead|RHI::MemoryAccess::TransferRead);
        return output;
    }
}
