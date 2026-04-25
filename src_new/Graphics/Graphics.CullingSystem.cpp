module;

#include <array>
#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

module Extrinsic.Graphics.CullingSystem;

import Extrinsic.Core.StrongHandle;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.PipelineManager;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Types;
import Extrinsic.Graphics.GpuWorld;

namespace Extrinsic::Graphics
{
    static constexpr std::uint32_t kInitialCapacity = 1024;
    static constexpr std::uint32_t kCullGroupSize   = 64;

    namespace
    {
        constexpr std::size_t ToIndex(const RHI::GpuDrawBucketKind kind) noexcept
        {
            return static_cast<std::size_t>(kind);
        }

        void ExtractFrustumPlanes(const glm::mat4& vp, glm::vec4 planes[6]) noexcept
        {
            auto row = [&](int r) -> glm::vec4
            {
                return {vp[0][r], vp[1][r], vp[2][r], vp[3][r]};
            };

            const glm::vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
            planes[0] = r3 + r0;
            planes[1] = r3 - r0;
            planes[2] = r3 + r1;
            planes[3] = r3 - r1;
            planes[4] = r2;
            planes[5] = r3 - r2;
        }
    }

    struct CullSlot
    {
        RHI::BoundingSphere Sphere{};
        RHI::GpuDrawIndexedCommand DrawTemplate{};
        std::uint32_t       Generation = 0;
        bool                Live       = false;
    };

    struct BucketStorage
    {
        GpuDrawBucket Bucket{};
        RHI::BufferManager::BufferLease IndexedArgsLease{};
        RHI::BufferManager::BufferLease NonIndexedArgsLease{};
        RHI::BufferManager::BufferLease CountLease{};
        std::uint64_t IndexedArgsBDA = 0;
        std::uint64_t NonIndexedArgsBDA = 0;
        std::uint64_t CountBDA = 0;
    };

    struct CullingSystem::Impl
    {
        RHI::IDevice*         Device      = nullptr;
        RHI::BufferManager*   BufferMgr   = nullptr;
        RHI::PipelineManager* PipelineMgr = nullptr;
        RHI::PipelineManager::PipelineLease CullPipeline;
        RHI::BufferManager::BufferLease CullBucketTableLease{};
        std::uint64_t CullBucketTableBDA = 0;

        std::array<BucketStorage, static_cast<std::size_t>(RHI::GpuDrawBucketKind::Count)> Buckets{};

        std::vector<CullSlot>      Slots;
        std::vector<std::uint32_t> FreeList;
        std::uint32_t Capacity  = 0;
        std::uint32_t LiveCount = 0;

        bool AllocateBucket(RHI::GpuDrawBucketKind kind, const bool indexed, const std::uint32_t capacity)
        {
            auto& bucket = Buckets[ToIndex(kind)];

            const std::uint64_t indexedArgsBytes = capacity * sizeof(RHI::GpuDrawIndexedCommand);
            const std::uint64_t pointArgsBytes   = capacity * sizeof(RHI::GpuDrawCommand);
            const std::uint64_t countBytes       = sizeof(std::uint32_t);

            auto argsOr = BufferMgr->Create({
                .SizeBytes   = indexed ? indexedArgsBytes : pointArgsBytes,
                .Usage       = RHI::BufferUsage::Storage | RHI::BufferUsage::Indirect | RHI::BufferUsage::TransferDst,
                .HostVisible = false,
                .DebugName   = indexed ? "CullBucketIndexedArgs" : "CullBucketPointArgs",
            });
            if (!argsOr.has_value()) return false;

            auto countOr = BufferMgr->Create({
                .SizeBytes   = countBytes,
                .Usage       = RHI::BufferUsage::Storage | RHI::BufferUsage::Indirect | RHI::BufferUsage::TransferDst,
                .HostVisible = false,
                .DebugName   = "CullBucketCount",
            });
            if (!countOr.has_value()) return false;

            if (indexed)
            {
                bucket.IndexedArgsLease = std::move(*argsOr);
                bucket.Bucket.IndexedArgsBuffer = bucket.IndexedArgsLease.GetHandle();
                bucket.IndexedArgsBDA = Device->GetBufferDeviceAddress(bucket.Bucket.IndexedArgsBuffer);
            }
            else
            {
                bucket.NonIndexedArgsLease = std::move(*argsOr);
                bucket.Bucket.NonIndexedArgsBuffer = bucket.NonIndexedArgsLease.GetHandle();
                bucket.NonIndexedArgsBDA = Device->GetBufferDeviceAddress(bucket.Bucket.NonIndexedArgsBuffer);
            }

            bucket.CountLease = std::move(*countOr);
            bucket.Bucket.CountBuffer = bucket.CountLease.GetHandle();
            bucket.CountBDA = Device->GetBufferDeviceAddress(bucket.Bucket.CountBuffer);
            bucket.Bucket.Capacity = capacity;
            bucket.Bucket.Indexed = indexed;
            return true;
        }

        bool AllocateGpuBuffers(const std::uint32_t capacity)
        {
            const bool surfaceOk = AllocateBucket(RHI::GpuDrawBucketKind::SurfaceOpaque, true, capacity);
            const bool alphaOk   = AllocateBucket(RHI::GpuDrawBucketKind::SurfaceAlphaMask, true, capacity);
            const bool lineOk    = AllocateBucket(RHI::GpuDrawBucketKind::Lines, true, capacity);
            const bool pointOk   = AllocateBucket(RHI::GpuDrawBucketKind::Points, false, capacity);
            const bool shadowOk  = AllocateBucket(RHI::GpuDrawBucketKind::ShadowOpaque, true, capacity);
            if (!(surfaceOk && alphaOk && lineOk && pointOk && shadowOk))
            {
                return false;
            }

            auto bucketTableOr = BufferMgr->Create({
                .SizeBytes   = sizeof(RHI::GpuCullBucketTable),
                .Usage       = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferDst,
                .HostVisible = false,
                .DebugName   = "CullBucketTable",
            });
            if (!bucketTableOr.has_value())
            {
                return false;
            }

            CullBucketTableLease = std::move(*bucketTableOr);
            CullBucketTableBDA = Device->GetBufferDeviceAddress(CullBucketTableLease.GetHandle());
            Capacity = capacity;
            Slots.assign(capacity, CullSlot{});
            return true;
        }

        [[nodiscard]] CullSlot* Resolve(const CullingHandle h) noexcept
        {
            if (!h.IsValid() || h.Index >= static_cast<std::uint32_t>(Slots.size())) return nullptr;
            auto& slot = Slots[h.Index];
            if (!slot.Live || slot.Generation != h.Generation) return nullptr;
            return &slot;
        }
    };

    CullingSystem::CullingSystem()
        : m_Impl(std::make_unique<Impl>())
    {}

    CullingSystem::~CullingSystem() = default;

    void CullingSystem::Initialize(RHI::IDevice&         device,
                                   RHI::BufferManager&   bufferMgr,
                                   RHI::PipelineManager& pipelineMgr,
                                   std::string_view      cullShaderPath)
    {
        m_Impl->Device      = &device;
        m_Impl->BufferMgr   = &bufferMgr;
        m_Impl->PipelineMgr = &pipelineMgr;

        [[maybe_unused]] const bool ok = m_Impl->AllocateGpuBuffers(kInitialCapacity);
        assert(ok && "CullingSystem: GPU buffer allocation failed");

        RHI::PipelineDesc cullDesc;
        cullDesc.ComputeShaderPath = std::string{cullShaderPath};
        cullDesc.PushConstantSize  = sizeof(RHI::GpuCullPushConstants);
        cullDesc.DebugName         = "CullComputeBuckets";

        if (auto pipelineOr = pipelineMgr.Create(cullDesc); pipelineOr.has_value())
            m_Impl->CullPipeline = std::move(*pipelineOr);
        assert(m_Impl->CullPipeline.IsValid() && "CullingSystem: cull pipeline compile failed");
    }

    void CullingSystem::Shutdown()
    {
        m_Impl->CullPipeline = {};
        for (auto& bucket : m_Impl->Buckets)
        {
            bucket.Bucket = {};
            bucket.IndexedArgsLease = {};
            bucket.NonIndexedArgsLease = {};
            bucket.CountLease = {};
            bucket.IndexedArgsBDA = 0;
            bucket.NonIndexedArgsBDA = 0;
            bucket.CountBDA = 0;
        }
        m_Impl->CullBucketTableLease = {};
        m_Impl->CullBucketTableBDA = 0;
        m_Impl->Slots.clear();
        m_Impl->FreeList.clear();
        m_Impl->Capacity = 0;
        m_Impl->LiveCount = 0;
        m_Impl->Device = nullptr;
        m_Impl->BufferMgr = nullptr;
        m_Impl->PipelineMgr = nullptr;
    }

    CullingHandle CullingSystem::Register(const RHI::BoundingSphere& sphere,
                                          const RHI::GpuDrawIndexedCommand& drawTemplate)
    {
        assert(m_Impl->Device && "Register called before Initialize()");

        std::uint32_t index = 0;
        std::uint32_t generation = 0;
        if (!m_Impl->FreeList.empty())
        {
            index = m_Impl->FreeList.back();
            m_Impl->FreeList.pop_back();
            generation = m_Impl->Slots[index].Generation;
        }
        else
        {
            assert(m_Impl->LiveCount < m_Impl->Capacity && "CullingSystem capacity exceeded");
            index = m_Impl->LiveCount;
        }

        auto& slot = m_Impl->Slots[index];
        slot.Sphere = sphere;
        slot.DrawTemplate = drawTemplate;
        slot.Generation = generation;
        slot.Live = true;
        ++m_Impl->LiveCount;
        return CullingHandle{index, generation};
    }

    void CullingSystem::Unregister(const CullingHandle handle)
    {
        auto* slot = m_Impl->Resolve(handle);
        if (!slot) return;

        slot->Live = false;
        ++slot->Generation;
        m_Impl->FreeList.push_back(handle.Index);
        --m_Impl->LiveCount;
    }

    void CullingSystem::UpdateBounds(const CullingHandle handle, const RHI::BoundingSphere& sphere)
    {
        auto* slot = m_Impl->Resolve(handle);
        if (!slot) return;
        slot->Sphere = sphere;
    }

    void CullingSystem::SetDrawTemplate(const CullingHandle handle, const RHI::GpuDrawIndexedCommand& cmd)
    {
        auto* slot = m_Impl->Resolve(handle);
        if (!slot) return;
        slot->DrawTemplate = cmd;
    }

    void CullingSystem::SyncGpuBuffer()
    {
        // Bucketed culling reads directly from GpuWorld SSBOs.
        // No CPU-authored cull input buffer remains.
    }

    void CullingSystem::ResetCounters(RHI::ICommandContext& cmd)
    {
        for (auto& bucket : m_Impl->Buckets)
        {
            cmd.FillBuffer(bucket.Bucket.CountBuffer, 0, sizeof(std::uint32_t), 0);
            cmd.BufferBarrier(bucket.Bucket.CountBuffer,
                              RHI::MemoryAccess::TransferWrite,
                              RHI::MemoryAccess::ShaderWrite);
        }
    }

    void CullingSystem::DispatchCull(RHI::ICommandContext& cmd,
                                     const RHI::CameraUBO& camera,
                                     const GpuWorld&       gpuWorld)
    {
        if (!m_Impl->CullPipeline.IsValid()) return;

        const auto pipe = m_Impl->PipelineMgr->GetDeviceHandle(m_Impl->CullPipeline.GetHandle());
        if (!pipe.IsValid()) return;

        bool resizedBuckets = false;
        if (gpuWorld.GetInstanceCapacity() > m_Impl->Capacity)
        {
            [[maybe_unused]] const bool resized = m_Impl->AllocateGpuBuffers(gpuWorld.GetInstanceCapacity());
            assert(resized && "CullingSystem: failed to resize cull buckets to instance capacity");
            if (!resized)
            {
                return;
            }
            resizedBuckets = true;
        }

        if (resizedBuckets)
        {
            ResetCounters(cmd);
        }

        RHI::GpuCullPushConstants pc{};
        ExtractFrustumPlanes(camera.ViewProj, pc.FrustumPlanes);
        pc.SceneTableBDA = gpuWorld.GetSceneTableBDA();
        pc.CullBucketTableBDA = m_Impl->CullBucketTableBDA;

        const auto& surface = m_Impl->Buckets[ToIndex(RHI::GpuDrawBucketKind::SurfaceOpaque)];
        const auto& alpha   = m_Impl->Buckets[ToIndex(RHI::GpuDrawBucketKind::SurfaceAlphaMask)];
        const auto& lines   = m_Impl->Buckets[ToIndex(RHI::GpuDrawBucketKind::Lines)];
        const auto& points  = m_Impl->Buckets[ToIndex(RHI::GpuDrawBucketKind::Points)];
        const auto& shadow  = m_Impl->Buckets[ToIndex(RHI::GpuDrawBucketKind::ShadowOpaque)];

        pc.InstanceCapacity = gpuWorld.GetInstanceCapacity();

        RHI::GpuCullBucketTable bucketTable{};
        bucketTable.SurfaceOpaque.ArgsBDA = surface.IndexedArgsBDA;
        bucketTable.SurfaceOpaque.CountBDA = surface.CountBDA;
        bucketTable.SurfaceOpaque.Capacity = surface.Bucket.Capacity;
        bucketTable.SurfaceAlphaMask.ArgsBDA = alpha.IndexedArgsBDA;
        bucketTable.SurfaceAlphaMask.CountBDA = alpha.CountBDA;
        bucketTable.SurfaceAlphaMask.Capacity = alpha.Bucket.Capacity;
        bucketTable.Lines.ArgsBDA = lines.IndexedArgsBDA;
        bucketTable.Lines.CountBDA = lines.CountBDA;
        bucketTable.Lines.Capacity = lines.Bucket.Capacity;
        bucketTable.Points.ArgsBDA = points.NonIndexedArgsBDA;
        bucketTable.Points.CountBDA = points.CountBDA;
        bucketTable.Points.Capacity = points.Bucket.Capacity;
        bucketTable.ShadowOpaque.ArgsBDA = shadow.IndexedArgsBDA;
        bucketTable.ShadowOpaque.CountBDA = shadow.CountBDA;
        bucketTable.ShadowOpaque.Capacity = shadow.Bucket.Capacity;
        m_Impl->Device->WriteBuffer(m_Impl->CullBucketTableLease.GetHandle(), &bucketTable, sizeof(bucketTable), 0);
        cmd.BufferBarrier(m_Impl->CullBucketTableLease.GetHandle(),
                          RHI::MemoryAccess::TransferWrite,
                          RHI::MemoryAccess::ShaderRead);

        cmd.BindPipeline(pipe);
        cmd.PushConstants(&pc, sizeof(pc));

        const std::uint32_t groups = (gpuWorld.GetInstanceCapacity() + kCullGroupSize - 1) / kCullGroupSize;
        if (groups > 0)
        {
            cmd.Dispatch(groups, 1, 1);
        }

        for (auto& bucket : m_Impl->Buckets)
        {
            const auto args = bucket.Bucket.Indexed ? bucket.Bucket.IndexedArgsBuffer : bucket.Bucket.NonIndexedArgsBuffer;
            cmd.BufferBarrier(args,
                              RHI::MemoryAccess::ShaderWrite,
                              RHI::MemoryAccess::IndirectRead);
            cmd.BufferBarrier(bucket.Bucket.CountBuffer,
                              RHI::MemoryAccess::ShaderWrite,
                              RHI::MemoryAccess::IndirectRead);
        }
    }

    const GpuDrawBucket& CullingSystem::GetBucket(const RHI::GpuDrawBucketKind kind) const
    {
        return m_Impl->Buckets[ToIndex(kind)].Bucket;
    }

    RHI::BufferHandle CullingSystem::GetDrawCommandBuffer() const noexcept
    {
        return m_Impl->Buckets[ToIndex(RHI::GpuDrawBucketKind::SurfaceOpaque)].Bucket.IndexedArgsBuffer;
    }

    RHI::BufferHandle CullingSystem::GetVisibilityCountBuffer() const noexcept
    {
        return m_Impl->Buckets[ToIndex(RHI::GpuDrawBucketKind::SurfaceOpaque)].Bucket.CountBuffer;
    }

    std::uint32_t CullingSystem::GetRegisteredCount() const noexcept
    {
        return m_Impl->LiveCount;
    }

    std::uint32_t CullingSystem::GetCapacity() const noexcept
    {
        return m_Impl->Capacity;
    }
}
