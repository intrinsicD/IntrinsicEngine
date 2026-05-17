module;


#include "Vulkan.hpp"

export module Extrinsic.Backends.Vulkan:CommandPools;

export import Extrinsic.Core.ResourcePool;
export import Extrinsic.Core.Telemetry;
export import Extrinsic.RHI.CommandContext;
export import Extrinsic.RHI.Handles;
export import Extrinsic.RHI.Types;
export import :Memory;
export import :Pipelines;
export import :Sync;

namespace Extrinsic::Backends::Vulkan
{
    export class VulkanCommandContext final : public RHI::ICommandContext
    {
    public:
        VulkanCommandContext() = default;

        void Bind(VkDevice device,
                  VkCommandBuffer cmd,
                  VkPipelineLayout globalLayout,
                  VkDescriptorSet  bindlessSet,
                  Core::ResourcePool<VulkanBuffer,   RHI::BufferHandle,   kMaxFramesInFlight>* buffers,
                  Core::ResourcePool<VulkanImage,    RHI::TextureHandle,  kMaxFramesInFlight>* images,
                  Core::ResourcePool<VulkanPipeline, RHI::PipelineHandle, kMaxFramesInFlight>* pipelines,
                  uint32_t graphicsQueueFamily = VK_QUEUE_FAMILY_IGNORED,
                  uint32_t presentQueueFamily  = VK_QUEUE_FAMILY_IGNORED,
                  uint32_t transferQueueFamily = VK_QUEUE_FAMILY_IGNORED);

        // GRAPHICS-033F: backend-local readiness predicate consumed by the
        // gate-8 (`PublicServiceReconciled`) precondition check. Returns true
        // when this context is bound to a live command buffer / global pipeline
        // layout / bindless descriptor set. `VulkanDevice::Shutdown()`
        // rebinds every context to `VK_NULL_HANDLE`, so this flips back to
        // false on teardown without needing a separate reset path.
        [[nodiscard]] bool IsBound() const noexcept
        {
            return m_Device       != VK_NULL_HANDLE &&
                   m_Cmd          != VK_NULL_HANDLE &&
                   m_GlobalLayout != VK_NULL_HANDLE &&
                   m_BindlessSet  != VK_NULL_HANDLE;
        }

        void Begin() override;
        void End()   override;

        void BeginRenderPass(const RHI::RenderPassDesc& desc) override;
        void EndRenderPass() override;

        void SetViewport(float x, float y, float w, float h, float minD, float maxD) override;
        void SetScissor(int32_t x, int32_t y, uint32_t w, uint32_t h) override;

        void BindPipeline(RHI::PipelineHandle pipeline) override;
        void BindIndexBuffer(RHI::BufferHandle buffer, uint64_t offset,
                             RHI::IndexType indexType) override;
        void PushConstants(const void* data, uint32_t size, uint32_t offset) override;

        void Draw(uint32_t vertexCount, uint32_t instanceCount,
                  uint32_t firstVertex, uint32_t firstInstance) override;
        void DrawIndexed(uint32_t indexCount, uint32_t instanceCount,
                         uint32_t firstIndex, int32_t vertexOffset,
                         uint32_t firstInstance) override;
        void DrawIndirect(RHI::BufferHandle argBuf, uint64_t offset, uint32_t drawCount) override;
        void DrawIndexedIndirect(RHI::BufferHandle argBuf, uint64_t offset, uint32_t drawCount) override;
        void DrawIndexedIndirectCount(RHI::BufferHandle argBuf, uint64_t argOffset,
                                      RHI::BufferHandle cntBuf, uint64_t cntOffset,
                                      uint32_t maxDraw) override;
        void DrawIndirectCount(RHI::BufferHandle argBuf, uint64_t argOffset,
                               RHI::BufferHandle cntBuf, uint64_t cntOffset,
                               uint32_t maxDraw) override;
        void Dispatch(uint32_t gx, uint32_t gy, uint32_t gz) override;
        void DispatchIndirect(RHI::BufferHandle argBuf, uint64_t offset) override;

        void TextureBarrier(RHI::TextureHandle tex, RHI::TextureLayout before,
                            RHI::TextureLayout after) override;
        void BufferBarrier(RHI::BufferHandle buf, RHI::MemoryAccess before,
                           RHI::MemoryAccess after) override;
        void SubmitBarriers(const RHI::BarrierBatchDesc& batch) override;

        void FillBuffer(RHI::BufferHandle buffer, uint64_t offset, uint64_t size,
                        uint32_t value) override;

        void CopyBuffer(RHI::BufferHandle src, RHI::BufferHandle dst,
                        uint64_t srcOff, uint64_t dstOff, uint64_t size) override;
        void CopyBufferToTexture(RHI::BufferHandle src, uint64_t srcOff,
                                 RHI::TextureHandle dst,
                                 uint32_t mipLevel, uint32_t arrayLayer) override;
        void CopyTextureToBuffer(RHI::TextureHandle src,
                                 RHI::TextureLayout srcLayout,
                                 uint32_t mipLevel, uint32_t arrayLayer,
                                 RHI::BufferHandle dst,
                                 uint64_t dstOffset) override;

    private:
        [[nodiscard]] bool CanBegin() const;
        [[nodiscard]] bool CanRecord(const char* operation) const;

        VkDevice         m_Device        = VK_NULL_HANDLE;
        VkCommandBuffer  m_Cmd           = VK_NULL_HANDLE;
        VkPipelineLayout m_GlobalLayout  = VK_NULL_HANDLE;
        VkDescriptorSet  m_BindlessSet   = VK_NULL_HANDLE;
        VkPipelineBindPoint m_BindPoint  = VK_PIPELINE_BIND_POINT_GRAPHICS;
        bool             m_Recording     = false;

        Core::ResourcePool<VulkanBuffer,   RHI::BufferHandle,   kMaxFramesInFlight>* m_Buffers   = nullptr;
        Core::ResourcePool<VulkanImage,    RHI::TextureHandle,  kMaxFramesInFlight>* m_Images    = nullptr;
        Core::ResourcePool<VulkanPipeline, RHI::PipelineHandle, kMaxFramesInFlight>* m_Pipelines = nullptr;
        uint32_t         m_GraphicsQueueFamily = VK_QUEUE_FAMILY_IGNORED;
        uint32_t         m_PresentQueueFamily  = VK_QUEUE_FAMILY_IGNORED;
        uint32_t         m_TransferQueueFamily = VK_QUEUE_FAMILY_IGNORED;
    };
}

