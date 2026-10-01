module;
#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <glm/vec3.hpp>
module Extrinsic.Runtime.ClusteringModule;
import :GpuBackend;
import Extrinsic.Graphics.ComputeParallelPrimitives;
import Extrinsic.Graphics.GpuTransfer;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Types;
namespace Extrinsic::Runtime
{
    namespace
    {
        // Scalar layout shared with include/kmeans_state.glsl.
        struct Push
        {
            std::uint64_t Input{}, Positions{}, Slots{}, Centroids{}, Labels{}, Previous{},
                Distances{}, Sums{}, Counts{}, Stats{}, Output{}, Presentation{};
            std::uint32_t Points{}, Clusters{}, Phase{}, First{}, Count{}, InnerFirst{}, InnerCount{};
        };
        static_assert(sizeof(Push) == 128);
    }
    struct KMeansGpuWorkspace::Impl
    {
        RHI::IDevice& Device;
        std::array<RHI::BufferHandle, 9> Buffers{};
        std::array<std::uint64_t, 9> Capacity{}, Addresses{};
        std::array<RHI::PipelineHandle, 3> Pipelines{};
        std::uint32_t Count{}, Clusters{}, BuffersCreated{}, PipelinesCreated{};
        std::uint64_t Uploaded{};
        bool Slotted{};
        explicit Impl(RHI::IDevice& d):Device(d){}
        ~Impl(){for(auto b:Buffers)if(b.IsValid())Device.DestroyBuffer(b);
            for(auto p:Pipelines)if(p.IsValid())Device.DestroyPipeline(p);}
    };
    KMeansGpuWorkspace::KMeansGpuWorkspace(RHI::IDevice& d):m_Impl(std::make_unique<Impl>(d)){}
    KMeansGpuWorkspace::~KMeansGpuWorkspace()=default;
    bool KMeansGpuWorkspace::Prepare(std::uint32_t count, std::span<const glm::vec3> seeds,
        std::span<const std::uint32_t> slots)
    {
        auto& s=*m_Impl;s.Count=count;s.Clusters=std::uint32_t(seeds.size());s.Slotted=!slots.empty();
        s.BuffersCreated=s.PipelinesCreated=0;s.Uploaded=0;
        if(!count||seeds.empty()||!s.Device.IsOperational())return false;
        const std::array<std::uint64_t,9> sizes{std::uint64_t(count)*12, slots.size_bytes(), seeds.size_bytes(),
            std::uint64_t(count)*4,std::uint64_t(count)*4,std::uint64_t(count)*4,
            seeds.size()*24,seeds.size()*4,sizeof(KMeansGpuDiagnostics)};
        // Shaders bound every access by Count/Clusters, so larger retained
        // buffers serve smaller runs; only an undersized buffer is replaced.
        for(std::size_t i=0;i<sizes.size();++i){
            if(!sizes[i]||(s.Buffers[i].IsValid()&&s.Capacity[i]>=sizes[i]))continue;
            if(s.Buffers[i].IsValid())s.Device.DestroyBuffer(s.Buffers[i]);
            s.Capacity[i]=0;s.Addresses[i]=0;
            s.Buffers[i]=s.Device.CreateBuffer({.SizeBytes=sizes[i],
                .Usage=RHI::BufferUsage::Storage|RHI::BufferUsage::TransferSrc|RHI::BufferUsage::TransferDst,
                .DebugName="KMeans.ResidentWorkspace"});
            if(!s.Buffers[i].IsValid())return false;
            s.Capacity[i]=sizes[i];s.Addresses[i]=s.Device.GetBufferDeviceAddress(s.Buffers[i]);++s.BuffersCreated;}
        constexpr std::array<const char*,3> shaders{"shaders/kmeans_reset.comp.spv","shaders/kmeans_assign.comp.spv","shaders/kmeans_update.comp.spv"};
        constexpr std::array<const char*,3> names{"KMeans.InitializeReducePreview","KMeans.Assign","KMeans.Update"};
        for(std::size_t i=0;i<s.Pipelines.size();++i){if(s.Pipelines[i].IsValid())continue;
            s.Pipelines[i]=Graphics::CreateComputePipeline(s.Device,shaders[i],sizeof(Push),names[i]);
            if(!s.Pipelines[i].IsValid())return false;
            ++s.PipelinesCreated;}
        if(!Graphics::SubmitBufferUpload(s.Device,s.Buffers[2],seeds.data(),seeds.size_bytes(),0).Accepted())return false;
        s.Uploaded=seeds.size_bytes();
        if(!slots.empty()){
            if(!Graphics::SubmitBufferUpload(s.Device,s.Buffers[1],slots.data(),slots.size_bytes(),0).Accepted())return false;
            s.Uploaded+=slots.size_bytes();}
        return true;
    }
    RHI::BufferHandle KMeansGpuWorkspace::Record(RHI::ICommandContext& cmd,const Graphics::GpuPropertyView& input,
        const KMeansGpuPage& page,const Graphics::GpuPropertyView& labels,const Graphics::GpuPropertyView& presentation)
    {
        auto& s=*m_Impl;
        const auto shader=RHI::MemoryAccess::ShaderRead|RHI::MemoryAccess::ShaderWrite;
        for(auto b:s.Buffers)if(b.IsValid())cmd.BufferBarrier(b,shader|RHI::MemoryAccess::TransferWrite|RHI::MemoryAccess::TransferRead,shader);
        cmd.BufferBarrier(input.Buffer,RHI::MemoryAccess::TransferWrite|shader,RHI::MemoryAccess::ShaderRead);
        // A retained slot map from an earlier run stays allocated but unbound.
        const auto address=[&](std::size_t i){return i==1&&!s.Slotted?0:s.Addresses[i];};
        Push p{input.Address,address(0),address(1),address(2),address(3),address(4),address(5),address(6),address(7),address(8),
            labels.Address,presentation.Address,s.Count,s.Clusters,std::uint32_t(page.Phase),page.First,page.Count,page.InnerFirst,page.InnerCount};
        if(page.Phase==KMeansGpuPhase::Assign&&page.First==0&&page.InnerFirst==0){
            cmd.FillBuffer(s.Buffers[8],0,sizeof(KMeansGpuDiagnostics),0);
            cmd.BufferBarrier(s.Buffers[8],RHI::MemoryAccess::TransferWrite,shader);}
        for(auto view:{labels,presentation})if(view.Valid())cmd.BufferBarrier(view.Buffer,shader|RHI::MemoryAccess::TransferRead,shader);
        const auto pipeline=page.Phase==KMeansGpuPhase::Assign?1:page.Phase==KMeansGpuPhase::Update?2:0;
        cmd.BindPipeline(s.Pipelines[pipeline]);cmd.PushConstants(&p,sizeof(p),0);
        cmd.Dispatch(page.Phase==KMeansGpuPhase::Reduce?1:page.Phase==KMeansGpuPhase::Update?page.Count:(page.Count+63)/64,1,1);
        for(auto b:s.Buffers)if(b.IsValid())cmd.BufferBarrier(b,shader,shader|RHI::MemoryAccess::TransferRead);
        for(auto view:{labels,presentation})if(view.Valid())cmd.BufferBarrier(view.Buffer,shader,RHI::MemoryAccess::ShaderRead|RHI::MemoryAccess::TransferRead);
        return s.Buffers[8];
    }
    RHI::BufferHandle KMeansGpuWorkspace::Labels()const{return m_Impl->Buffers[3];}
    RHI::BufferHandle KMeansGpuWorkspace::Centroids()const{return m_Impl->Buffers[2];}
    RHI::BufferHandle KMeansGpuWorkspace::Distances()const{return m_Impl->Buffers[5];}
    RHI::BufferHandle KMeansGpuWorkspace::Diagnostics()const{return m_Impl->Buffers[8];}
    std::uint64_t KMeansGpuWorkspace::CpuUploadBytes()const{return m_Impl->Uploaded;}
    std::uint32_t KMeansGpuWorkspace::PreparedBufferCreations()const{return m_Impl->BuffersCreated;}
    std::uint32_t KMeansGpuWorkspace::PreparedPipelineCreations()const{return m_Impl->PipelinesCreated;}
}
