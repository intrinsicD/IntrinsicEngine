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
        static_assert(sizeof(Push)==128 && offsetof(Push,CountBandwidthFactor)==112);
        static_assert(sizeof(PointScalarGpuStats)==112 && offsetof(PointScalarGpuStats,Bandwidth)==16);
    }
    struct PointScalarWorkspace::Impl
    {
        RHI::IDevice& Device;
        RHI::PipelineHandle Pipeline{};
        RHI::BufferHandle Neighbors{}, Means{}, Stats{};
        explicit Impl(RHI::IDevice& device):Device(device){}
        ~Impl()
        {
            for(auto b:{Neighbors,Means,Stats})if(b.IsValid())Device.DestroyBuffer(b);
            if(Pipeline.IsValid())Device.DestroyPipeline(Pipeline);
        }
    };
    std::uint32_t PointScalarNeighborWidth(const PointScalarGpuParams& p, std::uint32_t count)
    {
        if (!count || p.Method > 2) return 0;
        if (p.Method == 2) return std::min(std::max(1u,p.Capacity),count);
        return std::uint32_t(std::min(std::uint64_t(count), std::uint64_t(std::max(p.K,p.Method==0?2u:1u))+1));
    }
    PointScalarWorkspace::PointScalarWorkspace(RHI::IDevice& device):m_Impl(std::make_unique<Impl>(device)){}
    PointScalarWorkspace::~PointScalarWorkspace()=default;
    RHI::BufferHandle PointScalarWorkspace::Record(RHI::ICommandContext& cmd,const PointScalarGpuParams& p,const PointScalarResidentIo& io)
    {
        auto& s=*m_Impl;
        const auto count=io.LiveCount;
        const auto width=PointScalarNeighborWidth(p,count);
        if(!width||!count||count>(1u<<20)||width>4096||std::uint64_t(count)*width>(1u<<24)||
           !io.Positions.Address||!io.Nodes||!io.LiveSlots||!io.Output.Address||
           !s.Device.IsOperational()||!s.Device.SupportsShaderFloat64()||s.Pipeline.IsValid())return {};
        s.Pipeline=CreateComputePipeline(s.Device,"shaders/point_scalar_analysis.comp.spv",sizeof(Push),"PointScalarAnalysis");
        const auto allocate=[&](std::uint64_t bytes){return s.Device.CreateBuffer({.SizeBytes=bytes,
            .Usage=RHI::BufferUsage::Storage|RHI::BufferUsage::TransferSrc|RHI::BufferUsage::TransferDst,.DebugName="PointScalarAnalysis.Scratch"});};
        s.Neighbors=allocate(std::uint64_t(count)*width*8);s.Means=allocate(std::uint64_t(count)*16);s.Stats=allocate(sizeof(PointScalarGpuStats));
        if(!s.Pipeline.IsValid()||!s.Neighbors.IsValid()||!s.Means.IsValid()||!s.Stats.IsValid())return {};
        const auto shader=RHI::MemoryAccess::ShaderRead|RHI::MemoryAccess::ShaderWrite;
        cmd.BufferBarrier(io.Positions.Buffer,RHI::MemoryAccess::TransferWrite|shader,RHI::MemoryAccess::ShaderRead);
        cmd.BufferBarrier(io.Output.Buffer,shader|RHI::MemoryAccess::TransferRead,RHI::MemoryAccess::TransferWrite);
        if(io.Base.Address){cmd.BufferBarrier(io.Base.Buffer,RHI::MemoryAccess::TransferWrite|shader,RHI::MemoryAccess::TransferRead);
            cmd.CopyBuffer(io.Base.Buffer,io.Output.Buffer,0,0,io.Output.Bytes);}
        else cmd.FillBuffer(io.Output.Buffer,0,io.Output.Bytes,0);
        cmd.BufferBarrier(io.Output.Buffer,RHI::MemoryAccess::TransferWrite,shader);
        cmd.FillBuffer(s.Stats,0,sizeof(PointScalarGpuStats),0);
        cmd.BufferBarrier(s.Stats,RHI::MemoryAccess::TransferWrite,shader);
        Push push{.Positions=io.Positions.Address,.Nodes=io.Nodes,.Slots=io.LiveSlots,
            .Neighbors=s.Device.GetBufferDeviceAddress(s.Neighbors),.Nearest=s.Device.GetBufferDeviceAddress(s.Means),
            // Distinct float coordinates cannot lie inside a double-subnormal support.
            // The shader handles coincident points before testing this zero support.
            .Output=io.Output.Address,.Stats=s.Device.GetBufferDeviceAddress(s.Stats),.SupportRadius=p.SupportRadius<std::numeric_limits<double>::min()?0.0:p.SupportRadius,
            .Count=count,.Width=width,.Method=p.Method,.Kernel=p.Kernel,.Inverse=p.Inverse,
            .Bandwidth=p.Bandwidth,.Scale=p.Scale,.QueryRadius=p.QueryRadius,
            .CountBandwidthFactor=std::pow(double(count),-0.2),
            .GaussianNormConstant=std::pow(2.0*std::numbers::pi_v<double>,1.5)};
        cmd.BindPipeline(s.Pipeline);
        const auto dispatch=[&](std::uint32_t mode,std::uint32_t threads){
            push.Mode=mode;
            for(std::uint32_t first=0;first<threads;first+=65535u*64u){
                push.First=first;push.Threads=std::min(threads-first,65535u*64u);
                cmd.PushConstants(&push,sizeof(push),0);cmd.Dispatch((push.Threads+63)/64,1,1);}
        };
        dispatch(0,count);
        for(auto b:{s.Neighbors,s.Means,io.Output.Buffer,s.Stats})cmd.BufferBarrier(b,shader,shader);
        dispatch(1,1);
        for(auto b:{s.Stats,io.Output.Buffer})cmd.BufferBarrier(b,shader,shader);
        if(p.Method==0){dispatch(2,count);
            for(auto b:{s.Means,io.Output.Buffer,s.Stats})cmd.BufferBarrier(b,shader,shader);}
        dispatch(3,1);
        for(auto b:{io.Output.Buffer,s.Stats})cmd.BufferBarrier(b,shader,RHI::MemoryAccess::ShaderRead|RHI::MemoryAccess::TransferRead);
        return s.Stats;
    }
}
