module;

#include <cstdint>
#include <limits>
#include <memory>
#include <span>

export module Extrinsic.RHI.CommandContext;

import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Types;

// ============================================================
// ICommandContext — API-agnostic command recording interface.
//
// All resource references use typed handles.
// No Vulkan / OpenGL / DX type appears here.
// ============================================================

namespace Extrinsic::RHI
{
    // ----------------------------------------------------------
    // RenderPassDesc — describes a dynamic render pass.
    //
    // Color / depth targets are TextureHandle.
    //   - A valid handle   → TextureManager-owned render target.
    //   - An invalid handle → backbuffer obtained from
    //     IDevice::GetBackbufferHandle(frame), which the caller
    //     resolves before recording.
    //
    // LoadOp / StoreOp tell the backend whether to clear/discard
    // on begin and whether to resolve/discard on end, giving the
    // backend enough information to emit VkRenderingAttachmentInfo
    // (VK_KHR_dynamic_rendering) correctly.
    // ----------------------------------------------------------
    export enum class LoadOp  : std::uint8_t { Load, Clear, DontCare };
    export enum class StoreOp : std::uint8_t { Store, DontCare };

    export struct ColorAttachment
    {
        TextureHandle Target{};           // render-target texture; invalid = backbuffer
        LoadOp        Load  = LoadOp::Clear;
        StoreOp       Store = StoreOp::Store;
        float         ClearR = 0.f, ClearG = 0.f, ClearB = 0.f, ClearA = 1.f;
    };

    export struct DepthAttachment
    {
        TextureHandle Target{};           // depth texture; invalid = no depth
        LoadOp        Load        = LoadOp::Clear;
        StoreOp       Store       = StoreOp::DontCare;
        float         ClearDepth  = 1.f;
        std::uint8_t  ClearStencil = 0;
    };

    export struct RenderPassDesc
    {
        std::span<const ColorAttachment> ColorTargets{};  // 1..MaxColorTargets
        DepthAttachment                  Depth{};
    };

    // ----------------------------------------------------------
    // MemoryAccess — coarse access scope for explicit barriers.
    //
    // Tells the backend what pipeline stages and access types bracket a buffer,
    // texture, or memory barrier so it can emit the correct Sync2 masks.
    // ----------------------------------------------------------
    export enum class MemoryAccess : std::uint16_t
    {
        None                  = 0,
        IndirectRead          = 1 << 0,   // VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT
        IndexRead             = 1 << 1,   // VK_ACCESS_2_INDEX_READ_BIT
        ShaderRead            = 1 << 2,   // VK_ACCESS_2_SHADER_READ_BIT (storage / uniform)
        ShaderWrite           = 1 << 3,   // VK_ACCESS_2_SHADER_WRITE_BIT (storage write)
        TransferRead          = 1 << 4,   // VK_ACCESS_2_TRANSFER_READ_BIT
        TransferWrite         = 1 << 5,   // VK_ACCESS_2_TRANSFER_WRITE_BIT
        HostRead              = 1 << 6,   // VK_ACCESS_2_HOST_READ_BIT
        HostWrite             = 1 << 7,   // VK_ACCESS_2_HOST_WRITE_BIT
        ColorAttachmentRead   = 1 << 8,   // VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT
        ColorAttachmentWrite  = 1 << 9,   // VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
        DepthStencilRead      = 1 << 10,  // VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT
        DepthStencilWrite     = 1 << 11,  // VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
    };

    export [[nodiscard]] constexpr MemoryAccess operator|(MemoryAccess a, MemoryAccess b) noexcept
    {
        return static_cast<MemoryAccess>(
            static_cast<std::uint16_t>(a) | static_cast<std::uint16_t>(b));
    }

    // ----------------------------------------------------------
    // Barrier packet structs for explicit synchronization.
    //
    // These are backend-agnostic and map to Sync2-style barriers
    // in Vulkan backends. Queue family ownership transfer is optional
    // and represented as explicit queue-family indices.
    // ----------------------------------------------------------
    export struct TextureBarrierDesc
    {
        TextureHandle Texture{};
        TextureLayout BeforeLayout = TextureLayout::Undefined;
        TextureLayout AfterLayout = TextureLayout::Undefined;
        MemoryAccess BeforeAccess = MemoryAccess::None;
        MemoryAccess AfterAccess = MemoryAccess::None;
        std::uint32_t SrcQueueFamily = std::numeric_limits<std::uint32_t>::max();
        std::uint32_t DstQueueFamily = std::numeric_limits<std::uint32_t>::max();
    };

    export struct BufferBarrierDesc
    {
        BufferHandle Buffer{};
        MemoryAccess BeforeAccess = MemoryAccess::None;
        MemoryAccess AfterAccess = MemoryAccess::None;
        std::uint64_t Offset = 0;
        std::uint64_t Size = ~0ull;
        std::uint32_t SrcQueueFamily = std::numeric_limits<std::uint32_t>::max();
        std::uint32_t DstQueueFamily = std::numeric_limits<std::uint32_t>::max();
    };

    export struct MemoryBarrierDesc
    {
        MemoryAccess BeforeAccess = MemoryAccess::None;
        MemoryAccess AfterAccess = MemoryAccess::None;
    };

    export struct BarrierBatchDesc
    {
        std::span<const TextureBarrierDesc> TextureBarriers{};
        std::span<const BufferBarrierDesc> BufferBarriers{};
        std::span<const MemoryBarrierDesc> MemoryBarriers{};
    };

    export class ICommandContext
    {
    public:
        virtual ~ICommandContext() = default;

        // ---- Command buffer lifecycle --------------------------------
        virtual void Begin() = 0;
        virtual void End()   = 0;

        // ---- Render pass ---------------------------------------------
        virtual void BeginRenderPass(const RenderPassDesc& desc) = 0;
        virtual void EndRenderPass() = 0;

        // ---- Dynamic state -------------------------------------------
        virtual void SetViewport(float x, float y,
                                 float width, float height,
                                 float minDepth, float maxDepth) = 0;

        virtual void SetScissor(std::int32_t  x,      std::int32_t  y,
                                std::uint32_t width,  std::uint32_t height) = 0;

        // ---- Pipeline binding ----------------------------------------
        virtual void BindPipeline(PipelineHandle pipeline) = 0;

        // Optional backend hook for the current pass's primary sampled
        // framegraph texture. Backends that use fixed/global sampled slots may
        // publish this texture before fullscreen postprocess/present draws;
        // backends with explicit descriptor binding can ignore it.
        virtual void BindFrameSampledTexture(TextureHandle texture) { (void)texture; }

        // ---- Push constants ------------------------------------------
        virtual void BindIndexBuffer(BufferHandle  buffer,
                                     std::uint64_t offset,
                                     IndexType     indexType) = 0;

        // NOTE: Vertex fetch remains BDA/manual in shaders; there is intentionally
        // no BindVertexBuffer API in this abstraction. Indexed GPU-driven draws
        // still require an index-buffer bind (BindIndexBuffer).
        /// data must point to at least `size` bytes; size <= 128 bytes (Vulkan guarantee).
        virtual void PushConstants(const void*   data,
                                   std::uint32_t size,
                                   std::uint32_t offset = 0) = 0;

        // ---- Draw calls ----------------------------------------------
        virtual void Draw(std::uint32_t vertexCount,
                          std::uint32_t instanceCount,
                          std::uint32_t firstVertex,
                          std::uint32_t firstInstance) = 0;

        virtual void DrawIndexed(std::uint32_t indexCount,
                                 std::uint32_t instanceCount,
                                 std::uint32_t firstIndex,
                                 std::int32_t  vertexOffset,
                                 std::uint32_t firstInstance) = 0;

        virtual void DrawIndirect(BufferHandle   argBuffer,
                                  std::uint64_t  offset,
                                  std::uint32_t  drawCount) = 0;

        virtual void DrawIndexedIndirect(BufferHandle  argBuffer,
                                         std::uint64_t offset,
                                         std::uint32_t drawCount) = 0;

        /// GPU-driven draw count variant (vkCmdDrawIndexedIndirectCount).
        /// drawCount is read from countBuffer at countOffset (single uint32).
        /// maxDrawCount caps the GPU iteration regardless of the buffer value.
        virtual void DrawIndexedIndirectCount(BufferHandle  argBuffer,
                                              std::uint64_t argOffset,
                                              BufferHandle  countBuffer,
                                              std::uint64_t countOffset,
                                              std::uint32_t maxDrawCount) = 0;

        virtual void DrawIndirectCount(BufferHandle  argBuffer,
                                       std::uint64_t argOffset,
                                       BufferHandle  countBuffer,
                                       std::uint64_t countOffset,
                                       std::uint32_t maxDrawCount) = 0;

        // ---- Compute dispatch ----------------------------------------
        virtual void Dispatch(std::uint32_t groupX,
                              std::uint32_t groupY,
                              std::uint32_t groupZ) = 0;

        virtual void DispatchIndirect(BufferHandle  argBuffer,
                                      std::uint64_t offset) = 0;

        // ---- Resource barriers / layout transitions ------------------
        virtual void TextureBarrier(TextureHandle texture,
                                    TextureLayout before,
                                    TextureLayout after) = 0;

        /// Full pipeline barrier for a buffer with explicit access scopes.
        /// after = what the subsequent commands will do with the buffer.
        /// before = what the preceding commands did (ShaderWrite is the common case).
        /// The backend maps these to the minimal VkBufferMemoryBarrier2 masks.
        virtual void BufferBarrier(BufferHandle  buffer,
                                   MemoryAccess  before,
                                   MemoryAccess  after) = 0;

        /// Explicit barrier-batch API. Backends that do not support
        /// fine-grained barrier packets can rely on the default fallback,
        /// which maps each entry through TextureBarrier/BufferBarrier.
        /// Memory barriers are ignored by the fallback.
        virtual void SubmitBarriers(const BarrierBatchDesc& batch)
        {
            if (reinterpret_cast<std::uintptr_t>(std::addressof(batch)) % alignof(BarrierBatchDesc) != 0u)
            {
                return;
            }

            if (batch.TextureBarriers.data() != nullptr &&
                (reinterpret_cast<std::uintptr_t>(batch.TextureBarriers.data()) % alignof(TextureBarrierDesc) != 0u))
            {
                return;
            }

            if (batch.BufferBarriers.data() != nullptr &&
                (reinterpret_cast<std::uintptr_t>(batch.BufferBarriers.data()) % alignof(BufferBarrierDesc) != 0u))
            {
                return;
            }

            for (const TextureBarrierDesc& barrier : batch.TextureBarriers)
            {
                TextureBarrier(barrier.Texture, barrier.BeforeLayout, barrier.AfterLayout);
            }

            for (const BufferBarrierDesc& barrier : batch.BufferBarriers)
            {
                BufferBarrier(barrier.Buffer, barrier.BeforeAccess, barrier.AfterAccess);
            }
        }

        // ---- Copy operations -----------------------------------------
        virtual void FillBuffer(BufferHandle  buffer,
                                std::uint64_t offset,
                                std::uint64_t size,
                                std::uint32_t value) = 0;

        virtual void CopyBuffer(BufferHandle  src,
                                BufferHandle  dst,
                                std::uint64_t srcOffset,
                                std::uint64_t dstOffset,
                                std::uint64_t size) = 0;

        virtual void CopyBufferToTexture(BufferHandle  src,
                                         std::uint64_t srcOffset,
                                         TextureHandle dst,
                                         std::uint32_t mipLevel,
                                         std::uint32_t arrayLayer) = 0;

        // GRAPHICS-033D — texture-to-buffer readback used by the opt-in
        // `gpu;vulkan` MinimalDebug visible-triangle smoke (and the canonical
        // GRAPHICS-076/081 default-recipe equivalent once those land). The
        // caller is responsible for transitioning `src` into TransferSrc layout
        // before this call and back to its prior layout afterwards. The mip-
        // level / array-layer subresource extent specified by
        // `(srcOffsetX, srcOffsetY, srcWidth, srcHeight)` is copied into `dst`
        // starting at `dstOffset`; `dst` must have been created with
        // `BufferUsage::TransferDst`. The default `(0, 0, 0, 0)` extent means
        // "copy the whole mip extent" — backwards compatible with the
        // GRAPHICS-033D backbuffer readback. GRAPHICS-074 Slice D.2 uses the
        // single-pixel form `(pickPixel.X, pickPixel.Y, 1, 1)` to read one
        // EntityId/PrimitiveId texel into the renderer-owned 8-byte slot.
        // The default body is a no-op so existing CPU contract mocks remain
        // unaffected; the Vulkan backend overrides it to issue
        // `vkCmdCopyImageToBuffer`.
        //
        // HARDEN-072 — default arguments on this virtual function previously
        // tripped a Clang 20 C++23-modules bug where the consumer's vtable
        // slot mangled the symbol without the defaulted parameters
        // (six-arg form) while the exporting module emitted the full
        // ten-arg definition, leaving every downstream class with an
        // unresolved vtable entry at link time (e.g.
        // `Test.DebugViewContract.cpp`'s `RecordingCommandContext`,
        // `Backends.Null.cpp`'s `NullCommandContext`). The defaults are
        // removed; callers that want the "whole mip extent" semantics now
        // pass `0u, 0u, 0u, 0u` explicitly. See HARDEN-072 task notes.
        virtual void CopyTextureToBuffer(TextureHandle src,
                                         TextureLayout srcLayout,
                                         std::uint32_t mipLevel,
                                         std::uint32_t arrayLayer,
                                         BufferHandle  dst,
                                         std::uint64_t dstOffset,
                                         std::uint32_t srcOffsetX,
                                         std::uint32_t srcOffsetY,
                                         std::uint32_t srcWidth,
                                         std::uint32_t srcHeight)
        {
            (void)src;
            (void)srcLayout;
            (void)mipLevel;
            (void)arrayLayer;
            (void)dst;
            (void)dstOffset;
            (void)srcOffsetX;
            (void)srcOffsetY;
            (void)srcWidth;
            (void)srcHeight;
        }
    };
}
