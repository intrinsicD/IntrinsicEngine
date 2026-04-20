module;

#include <cstdint>
#include <memory>
#include <string_view>

export module Extrinsic.Graphics.CullingSystem;

import Extrinsic.Core.StrongHandle;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.PipelineManager;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Types;

// ============================================================
// CullingSystem
// ============================================================
// GPU-driven frustum culling via a compute shader.
//
// Architecture overview:
//
//   CPU registers renderables → CullDataBuffer (SSBO, device-local)
//   DispatchCull() → compute shader tests each sphere → populates:
//     DrawCommandBuffer (GpuDrawCommand[], indirect draw args)
//     VisibilityCountBuffer (single uint32 atomic counter)
//   Renderer calls DrawIndexedIndirectCount(DrawCommandBuffer,
//     VisibilityCountBuffer) — zero CPU readback needed.
//
// Buffer layout (all device-local, bound via BDA in push constants):
//
//   CullDataBuffer       — GpuCullData[]         input  (set once / per-frame if dirty)
//   DrawCommandBuffer    — GpuDrawCommand[]       output (written by compute, read by draw)
//   VisibilityCountBuffer — uint32                output (atomic counter reset each frame)
//
// Push constants (exactly 128 bytes = CullPushConstants in RHI.Types):
//   - 6 frustum planes extracted from the ViewProj matrix (Gribb-Hartmann)
//   - DrawCount (total registered entries)
//   - BDA pointers for all three buffers
//
// Per-frame usage:
//
//   // 1. Sync any updated bounds to GPU
//   cull.SyncGpuBuffer();
//
//   // 2. Reset the visibility counter at frame start
//   cull.ResetCounters(cmd);
//   cmd.BufferBarrier(cull.GetVisibilityCountBuffer());
//
//   // 3. Dispatch the cull compute (inserts UAV barriers internally)
//   cull.DispatchCull(cmd, cameraUBO);
//
//   // 4. Draw with the GPU-determined count
//   cmd.DrawIndexedIndirectCount(
//       cull.GetDrawCommandBuffer(), 0,
//       cull.GetVisibilityCountBuffer(), 0,
//       cull.GetCapacity());
//
// Thread-safety:
//   - Register / Unregister / UpdateBounds — render thread only.
//   - DispatchCull / ResetCounters / SyncGpuBuffer — render thread only.
//   - GetXxxBuffer() — lock-free read, any thread.
// ============================================================

export namespace Extrinsic::Graphics
{
    struct CullingTag;
    using CullingHandle = Core::StrongHandle<CullingTag>;

    class CullingSystem
    {
    public:
        // -----------------------------------------------------------------
        // Lifecycle
        // -----------------------------------------------------------------
        CullingSystem();
        ~CullingSystem();

        CullingSystem(const CullingSystem&)            = delete;
        CullingSystem& operator=(const CullingSystem&) = delete;

        /// Allocate GPU buffers and compile the cull compute pipeline.
        /// cullShaderPath — path to the compiled cull SPIR-V (e.g. "shaders/cull.comp.spv").
        void Initialize(RHI::IDevice&        device,
                        RHI::BufferManager&  bufferMgr,
                        RHI::PipelineManager& pipelineMgr,
                        std::string_view      cullShaderPath);

        void Shutdown();

        // -----------------------------------------------------------------
        // Renderable registration
        // -----------------------------------------------------------------

        /// Register a renderable for culling.
        /// sphere     — bounding sphere in world space.
        /// drawTemplate — the GpuDrawCommand that will be written to the
        ///                draw-command buffer if this entry is visible.
        ///                Typically set once; update via SetDrawTemplate().
        [[nodiscard]] CullingHandle Register(const RHI::BoundingSphere& sphere,
                                             const RHI::GpuDrawCommand&  drawTemplate);

        /// Remove a previously registered renderable.  Its slot is recycled.
        void Unregister(CullingHandle handle);

        /// Update the world-space bounding sphere.  Marks the slot dirty
        /// for next SyncGpuBuffer() call.
        void UpdateBounds(CullingHandle handle, const RHI::BoundingSphere& sphere);

        /// Update the draw template (e.g. LOD switch changes index count).
        void SetDrawTemplate(CullingHandle handle, const RHI::GpuDrawCommand& cmd);

        // -----------------------------------------------------------------
        // Per-frame GPU operations — call in order each frame
        // -----------------------------------------------------------------

        /// Upload dirty GpuCullData entries to the GPU input buffer.
        /// Call before DispatchCull().
        void SyncGpuBuffer();

        /// Clear the visibility atomic counter to 0.
        /// Call once at frame start, before DispatchCull().
        /// Inserts a BufferBarrier on VisibilityCountBuffer.
        void ResetCounters(RHI::ICommandContext& cmd);

        /// Bind the cull pipeline, push frustum constants, and dispatch.
        /// Extracts 6 frustum planes from camera.ViewProj (Gribb-Hartmann).
        /// Inserts UAV barriers on DrawCommandBuffer and VisibilityCountBuffer
        /// after the dispatch so subsequent indirect draws see the results.
        void DispatchCull(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera);

        // -----------------------------------------------------------------
        // Output buffer handles — bind these for indirect draw submission
        // -----------------------------------------------------------------

        /// SSBO of GpuDrawCommand[] — pass as argBuffer to DrawIndexedIndirectCount.
        [[nodiscard]] RHI::BufferHandle GetDrawCommandBuffer()     const noexcept;

        /// Single uint32 — pass as countBuffer to DrawIndexedIndirectCount.
        [[nodiscard]] RHI::BufferHandle GetVisibilityCountBuffer() const noexcept;

        // -----------------------------------------------------------------
        // Diagnostics
        // -----------------------------------------------------------------
        [[nodiscard]] std::uint32_t GetRegisteredCount() const noexcept;
        [[nodiscard]] std::uint32_t GetCapacity()        const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}

