module;

#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

module Extrinsic.Graphics.CullingSystem;

import Extrinsic.Core.StrongHandle;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.PipelineManager;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Types;

// ============================================================
// CullingSystem — implementation
// ============================================================
//
// Slot pool (same deque-based pattern as the other managers).
// Three GPU buffers managed through BufferManager leases:
//
//   m_CullDataBuffer      GpuCullData[capacity]  — input
//   m_DrawCommandBuffer   GpuDrawCommand[capacity] — output
//   m_VisibilityCountBuf  uint32                  — output
//
// CPU mirror:
//   m_CullData[]  — parallel to GPU, updated on Register/UpdateBounds
//   m_DrawCmds[]  — parallel to GPU, updated on Register/SetDrawTemplate
//   m_DirtySet[]  — per-slot dirty flags for SyncGpuBuffer()
//
// Frustum extraction (Gribb-Hartmann 2001):
//   Given column-major ViewProj matrix M:
//     Left   = row3 + row0
//     Right  = row3 - row0
//     Bottom = row3 + row1
//     Top    = row3 - row1
//     Near   = row2         (Vulkan NDC depth [0, 1])
//     Far    = row3 - row2
//   Plane equation: dot(normal, p) + d >= 0 for inside points.
//   Planes are NOT normalised — the sphere test uses the raw
//   plane equation; normalisation would add 6 sqrt calls for no gain.
// ============================================================

namespace Extrinsic::Graphics
{
    static constexpr std::uint32_t kInitialCapacity  = 1024;
    static constexpr std::uint32_t kCullGroupSize    = 64;   // workgroup X size in shader

    // -----------------------------------------------------------------
    // Frustum extraction
    // -----------------------------------------------------------------
    namespace
    {
        // Returns 6 frustum planes from a column-major ViewProj matrix.
        // plane.xyz = normal, plane.w = signed distance from origin.
        // Point p is INSIDE the frustum if dot(plane.xyz, p) + plane.w >= 0.
        void ExtractFrustumPlanes(const glm::mat4& vp, glm::vec4 planes[6]) noexcept
        {
            // GLM stores matrices column-major: vp[col][row].
            // Row i = (vp[0][i], vp[1][i], vp[2][i], vp[3][i]).
            auto row = [&](int r) -> glm::vec4
            {
                return {vp[0][r], vp[1][r], vp[2][r], vp[3][r]};
            };

            const glm::vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);

            planes[0] = r3 + r0;  // left
            planes[1] = r3 - r0;  // right
            planes[2] = r3 + r1;  // bottom
            planes[3] = r3 - r1;  // top
            planes[4] = r2;       // near  (Vulkan [0,1] depth)
            planes[5] = r3 - r2;  // far
        }
    } // namespace

    // -----------------------------------------------------------------
    // Per-slot metadata
    // -----------------------------------------------------------------
    struct CullSlot
    {
        RHI::BoundingSphere Sphere{};
        RHI::GpuDrawCommand DrawTemplate{};
        std::uint32_t       Generation = 0;
        bool                Live       = false;
        bool                Dirty      = false;
    };

    // -----------------------------------------------------------------
    // Impl
    // -----------------------------------------------------------------
    struct CullingSystem::Impl
    {
        RHI::IDevice*        Device      = nullptr;
        RHI::BufferManager*  BufferMgr   = nullptr;
        RHI::PipelineManager* PipelineMgr = nullptr;

        // GPU buffer leases
        RHI::BufferManager::BufferLease CullDataBuffer;
        RHI::BufferManager::BufferLease DrawCommandBuffer;
        RHI::BufferManager::BufferLease VisibilityCountBuffer;

        // Compute pipeline
        RHI::PipelineManager::PipelineLease CullPipeline;

        // BDAs — cached after buffer allocation (device-local, stable until resize)
        std::uint64_t CullDataBDA        = 0;
        std::uint64_t DrawCommandBDA     = 0;
        std::uint64_t VisibilityCountBDA = 0;

        // CPU-side slot pool
        std::vector<CullSlot>             Slots;
        std::vector<RHI::GpuCullData>     GpuCullData;   // CPU mirror of CullDataBuffer
        std::vector<RHI::GpuDrawCommand>  GpuDrawCmds;   // CPU mirror of DrawCommandBuffer
        std::vector<bool>                 DirtySet;
        std::vector<std::uint32_t>        FreeList;

        std::uint32_t Capacity    = 0;
        std::uint32_t LiveCount   = 0;

        // -----------------------------------------------------------------
        [[nodiscard]] CullSlot* Resolve(CullingHandle h) noexcept
        {
            if (!h.IsValid() || h.Index >= static_cast<std::uint32_t>(Slots.size()))
                return nullptr;
            CullSlot& s = Slots[h.Index];
            if (!s.Live || s.Generation != h.Generation) return nullptr;
            return &s;
        }

        // -----------------------------------------------------------------
        bool AllocateGpuBuffers(std::uint32_t capacity)
        {
            const std::uint64_t cullBytes  = capacity * sizeof(RHI::GpuCullData);
            const std::uint64_t drawBytes  = capacity * sizeof(RHI::GpuDrawCommand);
            const std::uint64_t countBytes = sizeof(std::uint32_t);

            auto cullOr = BufferMgr->Create({
                .SizeBytes   = cullBytes,
                .Usage       = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferDst,
                .HostVisible = false,
                .DebugName   = "CullDataBuffer",
            });
            if (!cullOr.has_value()) return false;
            CullDataBuffer = std::move(*cullOr);

            auto drawOr = BufferMgr->Create({
                .SizeBytes   = drawBytes,
                .Usage       = RHI::BufferUsage::Storage
                              | RHI::BufferUsage::Indirect
                              | RHI::BufferUsage::TransferDst,
                .HostVisible = false,
                .DebugName   = "DrawCommandBuffer",
            });
            if (!drawOr.has_value()) return false;
            DrawCommandBuffer = std::move(*drawOr);

            auto visOr = BufferMgr->Create({
                .SizeBytes   = countBytes,
                .Usage       = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferDst,
                .HostVisible = false,
                .DebugName   = "VisibilityCountBuffer",
            });
            if (!visOr.has_value()) return false;
            VisibilityCountBuffer = std::move(*visOr);

            // Cache BDAs so DispatchCull() doesn't call GetBufferDeviceAddress() each frame.
            CullDataBDA        = Device->GetBufferDeviceAddress(CullDataBuffer.GetHandle());
            DrawCommandBDA     = Device->GetBufferDeviceAddress(DrawCommandBuffer.GetHandle());
            VisibilityCountBDA = Device->GetBufferDeviceAddress(VisibilityCountBuffer.GetHandle());

            Capacity = capacity;
            Slots        .assign(capacity, CullSlot{});
            GpuCullData  .assign(capacity, RHI::GpuCullData{});
            GpuDrawCmds  .assign(capacity, RHI::GpuDrawCommand{});
            DirtySet     .assign(capacity, false);
            return true;
        }
    };

    // -----------------------------------------------------------------
    // CullingSystem
    // -----------------------------------------------------------------
    CullingSystem::CullingSystem()
        : m_Impl(std::make_unique<Impl>())
    {}

    CullingSystem::~CullingSystem() = default;

    // -----------------------------------------------------------------
    void CullingSystem::Initialize(RHI::IDevice&         device,
                                   RHI::BufferManager&   bufferMgr,
                                   RHI::PipelineManager& pipelineMgr,
                                   std::string_view      cullShaderPath)
    {
        m_Impl->Device      = &device;
        m_Impl->BufferMgr   = &bufferMgr;
        m_Impl->PipelineMgr = &pipelineMgr;

        [[maybe_unused]] bool ok = m_Impl->AllocateGpuBuffers(kInitialCapacity);
        assert(ok && "CullingSystem: GPU buffer allocation failed");

        // Compile the compute pipeline.
        RHI::PipelineDesc cullDesc;
        cullDesc.ComputeShaderPath = std::string{cullShaderPath};
        cullDesc.PushConstantSize  = sizeof(RHI::CullPushConstants);
        cullDesc.DebugName         = "CullCompute";

        if (auto pipelineOr = pipelineMgr.Create(cullDesc); pipelineOr.has_value())
            m_Impl->CullPipeline = std::move(*pipelineOr);
        assert(m_Impl->CullPipeline.IsValid() && "CullingSystem: cull pipeline compile failed");
    }

    // -----------------------------------------------------------------
    void CullingSystem::Shutdown()
    {
        m_Impl->CullPipeline          = {};
        m_Impl->CullDataBuffer        = {};
        m_Impl->DrawCommandBuffer     = {};
        m_Impl->VisibilityCountBuffer = {};
        m_Impl->Slots.clear();
        m_Impl->GpuCullData.clear();
        m_Impl->GpuDrawCmds.clear();
        m_Impl->DirtySet.clear();
        m_Impl->FreeList.clear();
        m_Impl->LiveCount  = 0;
        m_Impl->Capacity   = 0;
        m_Impl->Device     = nullptr;
        m_Impl->BufferMgr  = nullptr;
        m_Impl->PipelineMgr= nullptr;
    }

    // -----------------------------------------------------------------
    CullingHandle CullingSystem::Register(const RHI::BoundingSphere& sphere,
                                          const RHI::GpuDrawCommand&  drawTemplate)
    {
        assert(m_Impl->Device && "Register called before Initialize()");

        std::uint32_t index;
        std::uint32_t generation;

        if (!m_Impl->FreeList.empty())
        {
            index      = m_Impl->FreeList.back();
            m_Impl->FreeList.pop_back();
            generation = m_Impl->Slots[index].Generation;
        }
        else
        {
            // Out of capacity — would need to reallocate GPU buffers.
            // For now assert; production code would resize here.
            assert(m_Impl->LiveCount < m_Impl->Capacity && "CullingSystem capacity exceeded");
            index      = m_Impl->LiveCount; // not ideal but safe for initial impl
            generation = 0;
        }

        CullSlot& slot     = m_Impl->Slots[index];
        slot.Sphere        = sphere;
        slot.DrawTemplate  = drawTemplate;
        slot.Generation    = generation;
        slot.Live          = true;
        slot.Dirty         = true;

        // Pack GpuCullData
        m_Impl->GpuCullData[index] = RHI::GpuCullData{
            .Sphere     = sphere,
            .InstanceID = index,
            .DrawID     = index,
        };
        m_Impl->GpuDrawCmds[index] = drawTemplate;
        m_Impl->DirtySet[index]    = true;

        ++m_Impl->LiveCount;
        return CullingHandle{index, generation};
    }

    // -----------------------------------------------------------------
    void CullingSystem::Unregister(CullingHandle handle)
    {
        CullSlot* slot = m_Impl->Resolve(handle);
        if (!slot) return;

        slot->Live = false;
        slot->Generation++;
        m_Impl->FreeList.push_back(handle.Index);
        --m_Impl->LiveCount;

        // Zero out the GPU entry so the compute shader skips it cleanly.
        m_Impl->GpuCullData[handle.Index] = RHI::GpuCullData{};
        m_Impl->DirtySet[handle.Index]    = true;
    }

    // -----------------------------------------------------------------
    void CullingSystem::UpdateBounds(CullingHandle              handle,
                                     const RHI::BoundingSphere& sphere)
    {
        CullSlot* slot = m_Impl->Resolve(handle);
        if (!slot) return;

        slot->Sphere                           = sphere;
        m_Impl->GpuCullData[handle.Index].Sphere = sphere;
        m_Impl->DirtySet[handle.Index]           = true;
    }

    // -----------------------------------------------------------------
    void CullingSystem::SetDrawTemplate(CullingHandle              handle,
                                        const RHI::GpuDrawCommand& cmd)
    {
        CullSlot* slot = m_Impl->Resolve(handle);
        if (!slot) return;

        slot->DrawTemplate                    = cmd;
        m_Impl->GpuDrawCmds[handle.Index]     = cmd;
        m_Impl->DirtySet[handle.Index]        = true;
    }

    // -----------------------------------------------------------------
    void CullingSystem::SyncGpuBuffer()
    {
        if (!m_Impl->Device) return;

        constexpr std::uint64_t kCullStride = sizeof(RHI::GpuCullData);
        constexpr std::uint64_t kDrawStride = sizeof(RHI::GpuDrawCommand);
        const std::uint32_t n = m_Impl->Capacity;

        std::uint32_t rangeStart = UINT32_MAX;

        auto flush = [&](std::uint32_t end)
        {
            if (rangeStart == UINT32_MAX) return;
            const std::uint64_t byteOff  = rangeStart * kCullStride;
            const std::uint64_t byteSize = (end - rangeStart) * kCullStride;
            m_Impl->Device->WriteBuffer(
                m_Impl->CullDataBuffer.GetHandle(),
                m_Impl->GpuCullData.data() + rangeStart,
                byteSize, byteOff);

            const std::uint64_t dOff  = rangeStart * kDrawStride;
            const std::uint64_t dSize = (end - rangeStart) * kDrawStride;
            m_Impl->Device->WriteBuffer(
                m_Impl->DrawCommandBuffer.GetHandle(),
                m_Impl->GpuDrawCmds.data() + rangeStart,
                dSize, dOff);

            rangeStart = UINT32_MAX;
        };

        for (std::uint32_t i = 0; i < n; ++i)
        {
            if (m_Impl->DirtySet[i])
            {
                if (rangeStart == UINT32_MAX) rangeStart = i;
                m_Impl->DirtySet[i] = false;
            }
            else
            {
                flush(i);
            }
        }
        flush(n);
    }

    // -----------------------------------------------------------------
    void CullingSystem::ResetCounters(RHI::ICommandContext& cmd)
    {
        // Write 0 to the count buffer via a 4-byte upload.
        // In Vulkan this maps to vkCmdFillBuffer(buffer, 0, 4, 0).
        // Here we use the API-agnostic path: WriteBuffer + a barrier.
        const std::uint32_t zero = 0;
        m_Impl->Device->WriteBuffer(
            m_Impl->VisibilityCountBuffer.GetHandle(),
            &zero, sizeof(zero), 0);

        cmd.BufferBarrier(m_Impl->VisibilityCountBuffer.GetHandle(),
                          RHI::MemoryAccess::TransferWrite,
                          RHI::MemoryAccess::ShaderWrite);
    }

    // -----------------------------------------------------------------
    void CullingSystem::DispatchCull(RHI::ICommandContext& cmd,
                                     const RHI::CameraUBO& camera)
    {
        if (!m_Impl->CullPipeline.IsValid()) return;

        const RHI::PipelineHandle pipe =
            m_Impl->PipelineMgr->GetDeviceHandle(m_Impl->CullPipeline.GetHandle());
        if (!pipe.IsValid()) return;

        // Extract 6 frustum planes from ViewProj (column-major, Gribb-Hartmann).
        RHI::CullPushConstants pc{};
        ExtractFrustumPlanes(camera.ViewProj, pc.FrustumPlanes);
        pc.DrawCount          = m_Impl->LiveCount;
        pc.CullDataBDA        = m_Impl->CullDataBDA;
        pc.DrawCommandBDA     = m_Impl->DrawCommandBDA;
        pc.VisibilityCountBDA = m_Impl->VisibilityCountBDA;

        cmd.BindPipeline(pipe);
        cmd.PushConstants(&pc, sizeof(pc));

        // Dispatch: one thread per cull entry, rounded up to workgroup size.
        const std::uint32_t groups =
            (m_Impl->LiveCount + kCullGroupSize - 1) / kCullGroupSize;
        if (groups > 0)
            cmd.Dispatch(groups, 1, 1);

        // UAV barriers: subsequent indirect-draw and visibility-count reads
        // must see the compute shader's writes.
        cmd.BufferBarrier(m_Impl->DrawCommandBuffer.GetHandle(),
                          RHI::MemoryAccess::ShaderWrite,
                          RHI::MemoryAccess::IndirectRead);
        cmd.BufferBarrier(m_Impl->VisibilityCountBuffer.GetHandle(),
                          RHI::MemoryAccess::ShaderWrite,
                          RHI::MemoryAccess::ShaderRead);
    }

    // -----------------------------------------------------------------
    RHI::BufferHandle CullingSystem::GetDrawCommandBuffer() const noexcept
    {
        return m_Impl->DrawCommandBuffer.GetHandle();
    }

    RHI::BufferHandle CullingSystem::GetVisibilityCountBuffer() const noexcept
    {
        return m_Impl->VisibilityCountBuffer.GetHandle();
    }

    std::uint32_t CullingSystem::GetRegisteredCount() const noexcept
    {
        return m_Impl->LiveCount;
    }

    std::uint32_t CullingSystem::GetCapacity() const noexcept
    {
        return m_Impl->Capacity;
    }

} // namespace Extrinsic::Graphics

