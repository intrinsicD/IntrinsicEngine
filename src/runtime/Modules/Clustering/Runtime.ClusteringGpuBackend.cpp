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
import Extrinsic.RHI.TransferQueue;
namespace Extrinsic::Runtime
{
    namespace
    {
        // Scalar layout shared with include/kmeans_state.glsl.
        struct Push
        {
            std::uint64_t Input{}, Positions{}, Slots{}, Centroids{}, Labels{}, Previous{}, Distances{}, Sums{},
                Counts{}, Stats{}, Output{}, Presentation{};
            std::uint32_t Points{}, Clusters{}, Phase{}, First{}, Count{}, InnerFirst{}, InnerCount{};
        };
        static_assert(sizeof(Push) == 128);
    }
    struct KMeansGpuWorkspace::Impl
    {
        RHI::IDevice& Device;
        RHI::BufferManager& Scratch;
        // Positions, slots, centroids, labels, previous labels, distances, sums, counts, diagnostics, batch partials.
        std::array<RHI::BufferManager::BufferLease, 10> Buffers{};
        std::array<std::uint64_t, 10> Addresses{};
        std::array<RHI::PipelineHandle, 3> Pipelines{};
        std::uint32_t Count{}, Clusters{}, BuffersCreated{}, PipelinesCreated{};
        std::uint64_t Uploaded{};
        bool Slotted{};
        Impl(RHI::IDevice& d, RHI::BufferManager& scratch) : Device(d), Scratch(scratch) {}
        ~Impl()
        {
            for (auto p : Pipelines)
                if (p.IsValid())
                    Device.DestroyPipeline(p);
        }
    };
    KMeansGpuWorkspace::KMeansGpuWorkspace(RHI::IDevice& d, RHI::BufferManager& scratch)
        : m_Impl(std::make_unique<Impl>(d, scratch))
    {
    }
    KMeansGpuWorkspace::~KMeansGpuWorkspace() = default;
    void KMeansGpuWorkspace::ReleaseScratch()
    {
        m_Impl->Buffers = {};
        m_Impl->Addresses = {};
    }
    bool KMeansGpuWorkspace::Prepare(
        std::uint32_t count, std::span<const glm::vec3> seeds, std::span<const std::uint32_t> slots)
    {
        auto& s = *m_Impl;
        s.Count = count;
        s.Clusters = std::uint32_t(seeds.size());
        s.Slotted = !slots.empty();
        s.BuffersCreated = s.PipelinesCreated = 0;
        s.Uploaded = 0;
        if (!count || seeds.empty() || !s.Device.IsOperational())
            return false;
        const std::array<std::uint64_t, 10> sizes{std::uint64_t(count) * 12,
            slots.size_bytes(),
            seeds.size_bytes(),
            std::uint64_t(count) * 4,
            std::uint64_t(count) * 4,
            std::uint64_t(count) * 4,
            seeds.size() * 24,
            seeds.size() * 4,
            sizeof(KMeansGpuDiagnostics),
            // A batch's partials, independent of N and K: Update 64 pages × 64 centroids × 32 B (Reduce uses 64 × 16 B).
            std::uint64_t(KMeansBatchPages) * KMeansBatchPages * 32};
        // Shaders bound every access by Count/Clusters, so larger retained
        // buffers serve smaller runs; only an undersized buffer is replaced.
        const auto misses = s.Scratch.GetScratchStats().Misses;
        for (std::size_t i = 0; i < sizes.size(); ++i)
        {
            if (!sizes[i])
                continue;
            s.Addresses[i] = 0;
            if (!s.Scratch.EnsureScratch(s.Buffers[i],
                    {.SizeBytes = sizes[i],
                        .Usage =
                            RHI::BufferUsage::Storage | RHI::BufferUsage::TransferSrc | RHI::BufferUsage::TransferDst,
                        .DebugName = "KMeans.ResidentWorkspace"}))
                return false;
            s.Addresses[i] = s.Device.GetBufferDeviceAddress(s.Buffers[i].GetHandle());
        }
        s.BuffersCreated = std::uint32_t(s.Scratch.GetScratchStats().Misses - misses);
        constexpr std::array<const char*, 3> shaders{
            "shaders/kmeans_reset.comp.spv", "shaders/kmeans_assign.comp.spv", "shaders/kmeans_update.comp.spv"};
        constexpr std::array<const char*, 3> names{"KMeans.InitializeReducePreview", "KMeans.Assign", "KMeans.Update"};
        for (std::size_t i = 0; i < s.Pipelines.size(); ++i)
        {
            if (s.Pipelines[i].IsValid())
                continue;
            s.Pipelines[i] = Graphics::CreateComputePipeline(s.Device, shaders[i], sizeof(Push), names[i]);
            if (!s.Pipelines[i].IsValid())
                return false;
            ++s.PipelinesCreated;
        }
        if (!Graphics::SubmitBufferUpload(s.Device, s.Buffers[2].GetHandle(), seeds.data(), seeds.size_bytes(), 0)
                .Accepted())
            return false;
        s.Uploaded = seeds.size_bytes();
        if (!slots.empty())
        {
            if (!Graphics::SubmitBufferUpload(s.Device, s.Buffers[1].GetHandle(), slots.data(), slots.size_bytes(), 0)
                    .Accepted())
                return false;
            s.Uploaded += slots.size_bytes();
        }
        return true;
    }
    bool KMeansGpuWorkspace::Reseed(RHI::ITransferQueue& transfer, std::span<const glm::vec3> seeds)
    {
        auto& s = *m_Impl;
        if (seeds.size() != s.Clusters || !s.Buffers[2].IsValid())
            return false;
        return transfer.UploadBuffer(s.Buffers[2].GetHandle(), std::as_bytes(seeds), 0).IsValid();
    }
    RHI::BufferHandle KMeansGpuWorkspace::Record(RHI::ICommandContext& cmd,
        const Graphics::GpuPropertyView& input,
        const KMeansGpuPage& page,
        const Graphics::GpuPropertyView& labels,
        const Graphics::GpuPropertyView& presentation)
    {
        auto& s = *m_Impl;
        const auto shader = RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::ShaderWrite;
        for (const auto& b : s.Buffers)
            if (b.IsValid())
                cmd.BufferBarrier(
                    b.GetHandle(), shader | RHI::MemoryAccess::TransferWrite | RHI::MemoryAccess::TransferRead, shader);
        cmd.BufferBarrier(input.Buffer, RHI::MemoryAccess::TransferWrite | shader, RHI::MemoryAccess::ShaderRead);
        // A retained slot map from an earlier run stays allocated but unbound.
        const auto address = [&](std::size_t i)
        {
            return i == 1 && !s.Slotted ? 0 : s.Addresses[i];
        };
        Push p{input.Address,
            address(0),
            address(1),
            address(2),
            address(3),
            address(4),
            address(5),
            address(6),
            address(7),
            address(8),
            labels.Address,
            presentation.Address,
            s.Count,
            s.Clusters,
            std::uint32_t(page.Phase),
            page.First,
            page.Count,
            page.InnerFirst,
            page.InnerCount};
        // Batches: the partials' address and the page count go into fields the phase does not read.
        if (page.Phase == KMeansGpuPhase::Reduce || page.Phase == KMeansGpuPhase::ReduceCombine)
        {
            p.Sums = s.Addresses[9];
            p.InnerCount = page.Pages;
        }
        if (page.Phase == KMeansGpuPhase::Update || page.Phase == KMeansGpuPhase::UpdateCombine)
        {
            p.Previous = s.Addresses[9];
            p.Clusters = page.Pages;
        }
        if (page.Phase == KMeansGpuPhase::Assign && page.First == 0 && page.InnerFirst == 0)
        {
            cmd.FillBuffer(s.Buffers[8].GetHandle(), 0, sizeof(KMeansGpuDiagnostics), 0);
            cmd.BufferBarrier(s.Buffers[8].GetHandle(), RHI::MemoryAccess::TransferWrite, shader);
        }
        for (auto view : {labels, presentation})
            if (view.Valid())
                cmd.BufferBarrier(view.Buffer, shader | RHI::MemoryAccess::TransferRead, shader);
        const bool update = page.Phase == KMeansGpuPhase::Update || page.Phase == KMeansGpuPhase::UpdateCombine;
        const auto pipeline = page.Phase == KMeansGpuPhase::Assign ? 1 : update ? 2 : 0;
        cmd.BindPipeline(s.Pipelines[pipeline]);
        cmd.PushConstants(&p, sizeof(p), 0);
        if (page.Phase == KMeansGpuPhase::Reduce)
            cmd.Dispatch(page.Pages, 1, 1);
        else if (page.Phase == KMeansGpuPhase::Update)
            cmd.Dispatch(page.Count, page.Pages, 1);
        else if (page.Phase == KMeansGpuPhase::ReduceCombine || page.Phase == KMeansGpuPhase::UpdateCombine)
            cmd.Dispatch(1, 1, 1);
        else
            cmd.Dispatch((page.Count + 63) / 64, 1, 1);
        for (const auto& b : s.Buffers)
            if (b.IsValid())
                cmd.BufferBarrier(b.GetHandle(), shader, shader | RHI::MemoryAccess::TransferRead);
        for (auto view : {labels, presentation})
            if (view.Valid())
                cmd.BufferBarrier(view.Buffer, shader, RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::TransferRead);
        return s.Buffers[8].GetHandle();
    }
    RHI::BufferHandle KMeansGpuWorkspace::Labels() const
    {
        return m_Impl->Buffers[3].GetHandle();
    }
    RHI::BufferHandle KMeansGpuWorkspace::Centroids() const
    {
        return m_Impl->Buffers[2].GetHandle();
    }
    RHI::BufferHandle KMeansGpuWorkspace::Distances() const
    {
        return m_Impl->Buffers[5].GetHandle();
    }
    RHI::BufferHandle KMeansGpuWorkspace::Diagnostics() const
    {
        return m_Impl->Buffers[8].GetHandle();
    }
    std::uint64_t KMeansGpuWorkspace::CpuUploadBytes() const
    {
        return m_Impl->Uploaded;
    }
    std::uint32_t KMeansGpuWorkspace::PreparedBufferCreations() const
    {
        return m_Impl->BuffersCreated;
    }
    std::uint32_t KMeansGpuWorkspace::PreparedPipelineCreations() const
    {
        return m_Impl->PipelinesCreated;
    }
}
