module;

#include <cstdint>

export module Extrinsic.RHI.CommandContext;

import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Handles;  // re-exports BufferHandle

// ============================================================
// ICommandContext — API-agnostic command recording interface.
//
// All resource references use typed handles.
// No Vulkan / OpenGL / DX type appears here.
// ============================================================

namespace Extrinsic::RHI
{
    export class ICommandContext
    {
    public:
        virtual ~ICommandContext() = default;

        // ---- Command buffer lifecycle --------------------------------
        virtual void Begin() = 0;
        virtual void End()   = 0;

        // ---- Render pass ---------------------------------------------
        /// colorTarget / depthTarget may be invalid handles for swapchain targets.
        virtual void BeginRenderPass(TextureHandle colorTarget,
                                     TextureHandle depthTarget) = 0;
        virtual void EndRenderPass() = 0;

        // ---- Dynamic state -------------------------------------------
        virtual void SetViewport(float x, float y,
                                 float width, float height,
                                 float minDepth = 0.0f, float maxDepth = 1.0f) = 0;

        virtual void SetScissor(std::int32_t  x,      std::int32_t  y,
                                std::uint32_t width,  std::uint32_t height)    = 0;

        // ---- Pipeline binding ----------------------------------------
        virtual void BindPipeline(PipelineHandle pipeline) = 0;

        // ---- Resource binding ----------------------------------------
        virtual void BindVertexBuffer(std::uint32_t slot, BufferHandle buffer,
                                      std::uint64_t offset = 0) = 0;

        virtual void BindIndexBuffer(BufferHandle buffer,
                                     std::uint64_t offset = 0,
                                     bool use16BitIndices  = false) = 0;

        // ---- Push constants ------------------------------------------
        /// data must point to at least `size` bytes; size <= 128 bytes (Vulkan guarantee).
        virtual void PushConstants(const void* data,
                                   std::uint32_t size,
                                   std::uint32_t offset = 0) = 0;

        // ---- Draw calls ----------------------------------------------
        virtual void Draw(std::uint32_t vertexCount,
                          std::uint32_t instanceCount = 1,
                          std::uint32_t firstVertex   = 0,
                          std::uint32_t firstInstance = 0) = 0;

        virtual void DrawIndexed(std::uint32_t indexCount,
                                 std::uint32_t instanceCount  = 1,
                                 std::uint32_t firstIndex     = 0,
                                 std::int32_t  vertexOffset   = 0,
                                 std::uint32_t firstInstance  = 0) = 0;

        virtual void DrawIndirect(BufferHandle argBuffer,
                                  std::uint64_t offset,
                                  std::uint32_t drawCount) = 0;

        virtual void DrawIndexedIndirect(BufferHandle argBuffer,
                                         std::uint64_t offset,
                                         std::uint32_t drawCount) = 0;

        /// GPU-driven draw count variant (vkCmdDrawIndexedIndirectCount / D3D ExecuteIndirect).
        /// drawCount is read from countBuffer at countOffset (single uint32).
        /// maxDrawCount caps the GPU iteration regardless of the buffer value.
        virtual void DrawIndexedIndirectCount(BufferHandle  argBuffer,
                                              std::uint64_t argOffset,
                                              BufferHandle  countBuffer,
                                              std::uint64_t countOffset,
                                              std::uint32_t maxDrawCount) = 0;

        // ---- Compute dispatch ----------------------------------------
        virtual void Dispatch(std::uint32_t groupX,
                              std::uint32_t groupY = 1,
                              std::uint32_t groupZ = 1) = 0;

        virtual void DispatchIndirect(BufferHandle argBuffer,
                                      std::uint64_t offset) = 0;

        // ---- Resource barriers / layout transitions ------------------
        virtual void TextureBarrier(TextureHandle   texture,
                                    TextureLayout   before,
                                    TextureLayout   after) = 0;

        virtual void BufferBarrier(BufferHandle buffer) = 0;

        // ---- Copy operations -----------------------------------------
        virtual void CopyBuffer(BufferHandle src, BufferHandle dst,
                                std::uint64_t srcOffset, std::uint64_t dstOffset,
                                std::uint64_t size) = 0;

        virtual void CopyBufferToTexture(BufferHandle  src,
                                         std::uint64_t srcOffset,
                                         TextureHandle dst,
                                         std::uint32_t mipLevel   = 0,
                                         std::uint32_t arrayLayer = 0) = 0;
    };
}
