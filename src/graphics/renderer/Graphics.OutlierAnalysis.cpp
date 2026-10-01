module;
#include <algorithm>
#include <cstdint>
#include <memory>
module Extrinsic.Graphics.OutlierAnalysis;
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
            std::uint64_t Positions{}, Nodes{}, Slots{}, Neighbors{}, Means{}, Score{}, Mask{}, Presentation{}, Stats{};
            std::uint32_t Count{}, Width{}, Method{}, Minimum{};
            float Radius{}, Multiplier{}, ScoreThreshold{};
            std::uint32_t Mode{}, First{}, Threads{};
        };
        static_assert(sizeof(Push)==112);
        static_assert(sizeof(OutlierGpuStats)==32);
    }
    struct OutlierWorkspace::Impl
    {
        RHI::IDevice& Device;
        RHI::PipelineHandle Pipeline{};
        RHI::BufferHandle Neighbors{}, Means{}, Stats{};
        std::uint64_t NeighborBytes{}, MeanBytes{};
        explicit Impl(RHI::IDevice& device):Device(device){}
        ~Impl()
        {
            for(auto b:{Neighbors,Means,Stats})if(b.IsValid())Device.DestroyBuffer(b);
            if(Pipeline.IsValid())Device.DestroyPipeline(Pipeline);
        }
        // Repeated runs keep the pipeline and grow only an undersized scratch buffer. The
        // reduction pass assigns every stats field, and scratch rows are written before read.
        bool Ensure(std::uint64_t neighborBytes,std::uint64_t meanBytes)
        {
            if(!Pipeline.IsValid())Pipeline=CreateComputePipeline(Device,"shaders/outlier_analysis.comp.spv",sizeof(Push),"OutlierAnalysis");
            const auto allocate=[&](std::uint64_t bytes){return Device.CreateBuffer({.SizeBytes=bytes,
                .Usage=RHI::BufferUsage::Storage|RHI::BufferUsage::TransferSrc,.DebugName="OutlierAnalysis.Scratch"});};
            const auto grow=[&](RHI::BufferHandle& buffer,std::uint64_t& capacity,std::uint64_t bytes){
                if(buffer.IsValid()&&capacity>=bytes)return;
                if(buffer.IsValid())Device.DestroyBuffer(buffer);
                buffer=allocate(bytes);capacity=buffer.IsValid()?bytes:0;};
            grow(Neighbors,NeighborBytes,neighborBytes);grow(Means,MeanBytes,meanBytes);
            if(!Stats.IsValid())Stats=allocate(sizeof(OutlierGpuStats));
            return Pipeline.IsValid()&&Neighbors.IsValid()&&Means.IsValid()&&Stats.IsValid();
        }
    };
    std::uint32_t OutlierNeighborWidth(std::uint32_t method, std::uint32_t k, std::uint32_t count)
    {
        if (!count || method > 2) return 0;
        if (method == 1) return 1;
        const auto neighbors = method == 0 ? k : std::max(k, 2u);
        return std::uint32_t(std::min(std::uint64_t(count), std::uint64_t(neighbors) + 1));
    }
    OutlierWorkspace::OutlierWorkspace(RHI::IDevice& device):m_Impl(std::make_unique<Impl>(device)){}
    OutlierWorkspace::~OutlierWorkspace()=default;
    RHI::BufferHandle OutlierWorkspace::Record(RHI::ICommandContext& cmd,const OutlierGpuParams& p,const OutlierResidentIo& io)
    {
        auto& s=*m_Impl;
        const auto count=io.LiveCount;
        const auto width=OutlierNeighborWidth(p.Method,p.K,count);
        if(!width||!count||count>(1u<<20)||width>65||std::uint64_t(count)*width>(1u<<24)||
           !io.Positions.Address||!io.Nodes||!io.LiveSlots||!io.Score.Address||!io.Mask.Address||!io.Presentation.Address||
           !s.Device.IsOperational()||!s.Device.SupportsShaderFloat64()||
           !s.Ensure(std::uint64_t(count)*width*8,io.Score.Bytes*4))return {};
        const auto shader=RHI::MemoryAccess::ShaderRead|RHI::MemoryAccess::ShaderWrite;
        cmd.BufferBarrier(io.Positions.Buffer,RHI::MemoryAccess::TransferWrite|shader,RHI::MemoryAccess::ShaderRead);
        const auto initialize=[&](const GpuPropertyView& output,const GpuPropertyView& base){
            cmd.BufferBarrier(output.Buffer,shader|RHI::MemoryAccess::TransferRead,RHI::MemoryAccess::TransferWrite);
            if(base.Address){cmd.BufferBarrier(base.Buffer,RHI::MemoryAccess::TransferWrite|shader,RHI::MemoryAccess::TransferRead);
                cmd.CopyBuffer(base.Buffer,output.Buffer,0,0,output.Bytes);}
            else cmd.FillBuffer(output.Buffer,0,output.Bytes,0);
            cmd.BufferBarrier(output.Buffer,RHI::MemoryAccess::TransferWrite,shader);
        };
        initialize(io.Score,io.ScoreBase);initialize(io.Mask,io.MaskBase);initialize(io.Presentation,{});
        // A reused workspace: the previous run's scratch reads and stats readback precede these writes.
        for(auto b:{s.Neighbors,s.Means,s.Stats})cmd.BufferBarrier(b,shader|RHI::MemoryAccess::TransferRead,RHI::MemoryAccess::ShaderWrite);
        Push push{.Positions=io.Positions.Address,.Nodes=io.Nodes,.Slots=io.LiveSlots,
            .Neighbors=s.Device.GetBufferDeviceAddress(s.Neighbors),.Means=s.Device.GetBufferDeviceAddress(s.Means),
            .Score=io.Score.Address,.Mask=io.Mask.Address,.Presentation=io.Presentation.Address,.Stats=s.Device.GetBufferDeviceAddress(s.Stats),
            .Count=count,.Width=width,.Method=p.Method,.Minimum=p.MinimumNeighbors,.Radius=p.Radius,
            .Multiplier=p.Multiplier,.ScoreThreshold=p.ScoreThreshold};
        cmd.BindPipeline(s.Pipeline);
        const auto dispatch=[&](std::uint32_t mode,std::uint32_t threads){
            push.Mode=mode;
            for(std::uint32_t first=0;first<threads;first+=65535u*64u){
                push.First=first;push.Threads=std::min(threads-first,65535u*64u);
                cmd.PushConstants(&push,sizeof(push),0);cmd.Dispatch((push.Threads+63)/64,1,1);}
        };
        dispatch(3,io.Mask.Layout.Count);
        cmd.BufferBarrier(io.Presentation.Buffer,RHI::MemoryAccess::ShaderWrite,RHI::MemoryAccess::ShaderWrite);
        dispatch(0,count);
        for(auto b:{s.Neighbors,s.Means,io.Score.Buffer})cmd.BufferBarrier(b,RHI::MemoryAccess::ShaderWrite,shader);
        if(p.Method==2){dispatch(1,count);for(auto b:{s.Means,io.Score.Buffer})cmd.BufferBarrier(b,RHI::MemoryAccess::ShaderWrite,RHI::MemoryAccess::ShaderRead);}
        dispatch(2,1);
        for(auto b:{io.Score.Buffer,io.Mask.Buffer,io.Presentation.Buffer,s.Stats})
            cmd.BufferBarrier(b,shader,RHI::MemoryAccess::ShaderRead|RHI::MemoryAccess::TransferRead);
        return s.Stats;
    }
}
