module;

// Null backend — stub IDevice implementation with no GPU calls. Kept
// here as a compile-time scaffold and test fixture. When a real Vulkan
// backend arrives it should live alongside this as Extrinsic.Backends.Vulkan,
// at which point the global module fragment below will grow the actual
// vulkan.h / vk_mem_alloc.h includes.
#include <cstdint>
#include <memory>
#include <vector>
#include <atomic>
#include <expected>
#include <string_view>
#include <cassert>

module Extrinsic.Backends.Null;

import Extrinsic.Core.Config.Render;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Handles;  // all typed resource handles
import Extrinsic.RHI.FrameHandle;
import Extrinsic.RHI.Transfer;
import Extrinsic.RHI.Profiler;
import Extrinsic.RHI.Bindless;
import Extrinsic.RHI.Profiler;
import Extrinsic.RHI.Bindless;
import Extrinsic.RHI.Device;
import Extrinsic.Platform.Window;

namespace Extrinsic::Backends::Null
{
    // ===========================================================
    // Internal generational pool
    // ===========================================================
    // A flat SoA-ready slot allocator.  T is the backend-private
    // resource type (e.g. VulkanBufferEntry).  The Index field of
    // every StrongHandle<Tag> maps directly into this pool.
    // ===========================================================
    template<typename T>
    class ResourcePool
    {
    public:
        // Returns {index, generation} for constructing the handle.
        std::pair<std::uint32_t, std::uint32_t> Allocate()
        {
            if (!m_FreeList.empty())
            {
                const std::uint32_t idx = m_FreeList.back();
                m_FreeList.pop_back();
                m_Alive[idx] = true;
                return {idx, m_Generations[idx]};
            }
            const std::uint32_t idx = static_cast<std::uint32_t>(m_Data.size());
            m_Data.emplace_back();
            m_Generations.push_back(0);
            m_Alive.push_back(true);
            return {idx, 0};
        }

        void Free(std::uint32_t index, std::uint32_t generation)
        {
            if (index >= m_Data.size() || !m_Alive[index] || m_Generations[index] != generation)
                return;
            m_Alive[index] = false;
            ++m_Generations[index];
            m_Data[index] = T{};   // reset
            m_FreeList.push_back(index);
        }

        [[nodiscard]] T* Get(std::uint32_t index, std::uint32_t generation)
        {
            if (index >= m_Data.size() || !m_Alive[index] || m_Generations[index] != generation)
                return nullptr;
            return &m_Data[index];
        }

        [[nodiscard]] const T* Get(std::uint32_t index, std::uint32_t generation) const
        {
            if (index >= m_Data.size() || !m_Alive[index] || m_Generations[index] != generation)
                return nullptr;
            return &m_Data[index];
        }

    private:
        std::vector<T>             m_Data;
        std::vector<std::uint32_t> m_Generations;
        std::vector<bool>          m_Alive;
        std::vector<std::uint32_t> m_FreeList;
    };

    // ===========================================================
    // Backend-private resource structs
    // (replace with real Vk* members when wiring Vulkan)
    // ===========================================================

    struct BufferEntry
    {
        std::uint64_t        SizeBytes   = 0;
        RHI::BufferUsage     Usage       = RHI::BufferUsage::None;
        bool                 HostVisible = false;
        // TODO: VkBuffer Buffer = VK_NULL_HANDLE;
        // TODO: VmaAllocation Alloc = nullptr;
    };

    struct TextureEntry
    {
        std::uint32_t        Width      = 0;
        std::uint32_t        Height     = 0;
        std::uint32_t        MipLevels  = 1;
        RHI::Format          Fmt        = RHI::Format::Undefined;
        RHI::TextureLayout   Layout     = RHI::TextureLayout::Undefined;
        // TODO: VkImage Image = VK_NULL_HANDLE;
        // TODO: VkImageView View = VK_NULL_HANDLE;
        // TODO: VmaAllocation Alloc = nullptr;
    };

    struct SamplerEntry
    {
        RHI::SamplerDesc Desc{};
        // TODO: VkSampler Sampler = VK_NULL_HANDLE;
    };

    struct PipelineEntry
    {
        std::string DebugName;
        bool        IsCompute = false;
        // TODO: VkPipeline Pipeline = VK_NULL_HANDLE;
        // TODO: VkPipelineLayout Layout = VK_NULL_HANDLE;
    };

    // ===========================================================
    // NullBindlessHeap — stub IBindlessHeap
    // ===========================================================
    class NullBindlessHeap final : public RHI::IBindlessHeap
    {
    public:
        RHI::BindlessIndex AllocateTextureSlot(RHI::TextureHandle, RHI::SamplerHandle) override
        {
            // TODO: allocate from free-list; write VkWriteDescriptorSet on flush.
            return ++m_NextSlot;
        }
        void UpdateTextureSlot(RHI::BindlessIndex, RHI::TextureHandle, RHI::SamplerHandle) override
        {
            // TODO: queue pending VkWriteDescriptorSet update (thread-safe).
        }
        void FreeSlot(RHI::BindlessIndex) override
        {
            // TODO: push slot onto free-list; rebind to default descriptor on flush.
        }
        void FlushPending() override
        {
            // TODO: vkUpdateDescriptorSets for all queued updates.
        }
        [[nodiscard]] std::uint32_t GetCapacity()           const override { return 65536; }
        [[nodiscard]] std::uint32_t GetAllocatedSlotCount() const override { return m_NextSlot; }

    private:
        std::uint32_t m_NextSlot = 1; // slot 0 reserved for default texture
    };

    // ===========================================================
    // NullProfiler — stub IProfiler
    // ===========================================================
    class NullProfiler final : public RHI::IProfiler
    {
    public:
        void BeginFrame(std::uint32_t, std::uint32_t) override {}
        void EndFrame() override {}

        [[nodiscard]] std::uint32_t BeginScope(std::string_view) override { return 0; }
        void EndScope(std::uint32_t) override {}

        [[nodiscard]] std::expected<RHI::GpuTimestampFrame, RHI::ProfilerError>
        Resolve(std::uint32_t) const override
        {
            // TODO: vkGetQueryPoolResults → nanoseconds via timestampPeriod.
            return std::unexpected(RHI::ProfilerError::NotReady);
        }

        [[nodiscard]] std::uint32_t GetFramesInFlight() const override { return m_FramesInFlight; }

        std::uint32_t m_FramesInFlight = 2;
    };

    // ===========================================================
    // NullCommandContext — stub implementing the full ICommandContext
    // interface.  Replace method bodies with real vkCmd* calls.
    // ===========================================================
    class NullCommandContext final : public RHI::ICommandContext
    {
    public:
        // Lifecycle
        void Begin() override {}
        void End()   override {}

        // Render pass
        void BeginRenderPass(RHI::TextureHandle, RHI::TextureHandle) override {}
        void EndRenderPass() override {}

        // Dynamic state
        void SetViewport(float, float, float, float, float, float) override {}
        void SetScissor(std::int32_t, std::int32_t, std::uint32_t, std::uint32_t) override {}

        // Pipeline
        void BindPipeline(RHI::PipelineHandle) override {}

        // Resource binding
        void BindVertexBuffer(std::uint32_t, RHI::BufferHandle, std::uint64_t) override {}
        void BindIndexBuffer(RHI::BufferHandle, std::uint64_t, bool) override {}

        // Push constants
        void PushConstants(const void*, std::uint32_t, std::uint32_t) override {}

        // Draw
        void Draw(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) override {}
        void DrawIndexed(std::uint32_t, std::uint32_t, std::uint32_t, std::int32_t, std::uint32_t) override {}
        void DrawIndirect(RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
        void DrawIndexedIndirect(RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}

        // Compute
        void Dispatch(std::uint32_t, std::uint32_t, std::uint32_t) override {}
        void DispatchIndirect(RHI::BufferHandle, std::uint64_t) override {}

        // Barriers
        void TextureBarrier(RHI::TextureHandle, RHI::TextureLayout, RHI::TextureLayout) override {}
        void BufferBarrier(RHI::BufferHandle) override {}

        // Copy
        void CopyBuffer(RHI::BufferHandle, RHI::BufferHandle,
                        std::uint64_t, std::uint64_t, std::uint64_t) override {}
        void CopyBufferToTexture(RHI::BufferHandle, std::uint64_t,
                                 RHI::TextureHandle, std::uint32_t, std::uint32_t) override {}
    };

    // ===========================================================
    // NullDevice — stub IDevice with handle-indexed tables
    // ===========================================================
    // Returns well-formed pool handles so upstream managers can exercise
    // their full refcount / lease / dedup machinery without a live GPU.
    // IsOperational() returns false so managers short-circuit Create() with
    // Core::ErrorCode::DeviceNotOperational rather than wrapping a handle
    // that points to nothing.
    //
    // TODO markers throughout this file mark the seams where a real Vulkan
    // implementation would plug in. When that real backend arrives it
    // should live in a sibling Backends/Vulkan/ directory as its own module
    // rather than an in-place rewrite of this file — keeping the null
    // backend as a reliable test fixture.
    class NullDevice final : public RHI::IDevice
    {
    public:
        // ---- Backend status ------------------------------------------
        [[nodiscard]] bool IsOperational() const noexcept override { return false; }

        // ---- Device lifecycle ----------------------------------------
        void Initialize(Platform::IWindow& window,
                        const Core::Config::RenderConfig& config) override
        {
            (void)config;
            m_BackbufferExtent = window.GetFramebufferExtent();
            // TODO: vkCreateInstance, physical-device selection,
            //       vkCreateDevice, swapchain, command pools, VMA init.
        }

        void Shutdown() override
        {
            // TODO: destroy all live resources, then vkDestroyDevice / vmaDestroyAllocator.
        }

        void WaitIdle() override
        {
            // TODO: vkDeviceWaitIdle(m_Device);
        }

        // ---- Frame lifecycle -----------------------------------------
        bool BeginFrame(RHI::FrameHandle& outFrame) override
        {
            outFrame.FrameIndex          = m_FrameIndex++;
            outFrame.SwapchainImageIndex = 0;
            // TODO: vkAcquireNextImageKHR, fence wait, command buffer reset.
            return true;
        }

        void EndFrame(const RHI::FrameHandle&) override
        {
            // TODO: vkEndCommandBuffer, queue submit.
        }

        void Present(const RHI::FrameHandle&) override
        {
            // TODO: vkQueuePresentKHR.
        }

        void Resize(std::uint32_t width, std::uint32_t height) override
        {
            m_BackbufferExtent = {.Width  = static_cast<int>(width),
                                   .Height = static_cast<int>(height)};
            // TODO: recreate swapchain.
        }

        Platform::Extent2D GetBackbufferExtent() const override
        {
            return m_BackbufferExtent;
        }

        // ---- Command context -----------------------------------------
        RHI::ICommandContext& GetGraphicsContext(std::uint32_t /*frameIndex*/) override
        {
            return m_CommandContext;
        }

        // ---- Buffer resources ----------------------------------------
        RHI::BufferHandle CreateBuffer(const RHI::BufferDesc& desc) override
        {
            auto [idx, gen] = m_Buffers.Allocate();
            BufferEntry* e  = m_Buffers.Get(idx, gen);
            e->SizeBytes    = desc.SizeBytes;
            e->Usage        = desc.Usage;
            e->HostVisible  = desc.HostVisible;
            // TODO: vmaCreateBuffer → e->Buffer / e->Alloc
            return RHI::BufferHandle{idx, gen};
        }

        void DestroyBuffer(RHI::BufferHandle handle) override
        {
            if (!handle.IsValid()) return;
            // TODO: vmaDestroyBuffer on the entry
            m_Buffers.Free(handle.Index, handle.Generation);
        }

        void WriteBuffer(RHI::BufferHandle handle, const void* data,
                         std::uint64_t size, std::uint64_t offset) override
        {
            (void)handle; (void)data; (void)size; (void)offset;
            // TODO: vmaMapMemory / memcpy / vmaUnmapMemory (or staging upload).
        }

        // ---- Texture resources ---------------------------------------
        RHI::TextureHandle CreateTexture(const RHI::TextureDesc& desc) override
        {
            auto [idx, gen] = m_Textures.Allocate();
            TextureEntry* e = m_Textures.Get(idx, gen);
            e->Width        = desc.Width;
            e->Height       = desc.Height;
            e->MipLevels    = desc.MipLevels;
            e->Fmt          = desc.Fmt;
            e->Layout       = desc.InitialLayout;
            // TODO: vmaCreateImage / vkCreateImageView → e->Image / e->View / e->Alloc
            return RHI::TextureHandle{idx, gen};
        }

        void DestroyTexture(RHI::TextureHandle handle) override
        {
            if (!handle.IsValid()) return;
            // TODO: vkDestroyImageView / vmaDestroyImage on entry
            m_Textures.Free(handle.Index, handle.Generation);
        }

        void WriteTexture(RHI::TextureHandle handle, const void* data,
                          std::uint64_t dataSizeBytes,
                          std::uint32_t mipLevel, std::uint32_t arrayLayer) override
        {
            (void)handle; (void)data; (void)dataSizeBytes;
            (void)mipLevel; (void)arrayLayer;
            // TODO: staging buffer → vkCmdCopyBufferToImage.
        }

        // ---- Sampler resources ---------------------------------------
        RHI::SamplerHandle CreateSampler(const RHI::SamplerDesc& desc) override
        {
            auto [idx, gen] = m_Samplers.Allocate();
            SamplerEntry* e = m_Samplers.Get(idx, gen);
            e->Desc         = desc;
            // TODO: vkCreateSampler → e->Sampler
            return RHI::SamplerHandle{idx, gen};
        }

        void DestroySampler(RHI::SamplerHandle handle) override
        {
            if (!handle.IsValid()) return;
            // TODO: vkDestroySampler
            m_Samplers.Free(handle.Index, handle.Generation);
        }

        // ---- Pipeline resources --------------------------------------
        RHI::PipelineHandle CreatePipeline(const RHI::PipelineDesc& desc) override
        {
            auto [idx, gen]   = m_Pipelines.Allocate();
            PipelineEntry* e  = m_Pipelines.Get(idx, gen);
            e->IsCompute      = !desc.ComputeShaderPath.empty();
            e->DebugName      = desc.DebugName ? desc.DebugName : "";
            // TODO: load SPIR-V, vkCreateGraphicsPipelines / vkCreateComputePipelines
            return RHI::PipelineHandle{idx, gen};
        }

        void DestroyPipeline(RHI::PipelineHandle handle) override
        {
            if (!handle.IsValid()) return;
            // TODO: vkDestroyPipeline / vkDestroyPipelineLayout
            m_Pipelines.Free(handle.Index, handle.Generation);
        }

        // ---- Async transfer ------------------------------------------
        RHI::TransferToken UploadBuffer(RHI::BufferHandle dst,
                                        const void*        data,
                                        std::uint64_t      size,
                                        std::uint64_t      offset) override
        {
            (void)dst; (void)data; (void)size; (void)offset;
            // TODO: AllocateStaging → memcpy → vkCmdCopyBuffer → Submit → TransferToken.
            return RHI::TransferToken{++m_NextTransferValue};
        }

        RHI::TransferToken UploadTexture(RHI::TextureHandle dst,
                                         const void*        data,
                                         std::uint64_t      dataSizeBytes,
                                         std::uint32_t      mipLevel,
                                         std::uint32_t      arrayLayer) override
        {
            (void)dst; (void)data; (void)dataSizeBytes; (void)mipLevel; (void)arrayLayer;
            // TODO: AllocateStagingForImage → memcpy → vkCmdCopyBufferToImage → Submit.
            return RHI::TransferToken{++m_NextTransferValue};
        }

        [[nodiscard]] bool IsTransferComplete(RHI::TransferToken token) const override
        {
            (void)token;
            // TODO: query timeline semaphore value vs token.Value.
            return true; // stub: always complete
        }

        void CollectCompletedTransfers() override
        {
            // TODO: StagingBelt::GarbageCollect + free completed staging buffers.
        }

        // ---- Bindless heap -------------------------------------------
        RHI::IBindlessHeap& GetBindlessHeap() override { return m_BindlessHeap; }

        // ---- GPU profiler --------------------------------------------
        RHI::IProfiler* GetProfiler() override { return &m_Profiler; }

        // ---- Frame utilities -----------------------------------------
        [[nodiscard]] std::uint32_t GetFramesInFlight()    const override { return kFramesInFlight; }
        [[nodiscard]] std::uint64_t GetGlobalFrameNumber() const override { return m_FrameIndex; }

    private:
        static constexpr std::uint32_t kFramesInFlight = 2;

        std::uint32_t      m_FrameIndex{0};
        std::uint64_t      m_NextTransferValue{0};
        Platform::Extent2D m_BackbufferExtent{};
        NullCommandContext m_CommandContext{};
        NullBindlessHeap   m_BindlessHeap{};
        NullProfiler       m_Profiler{};

        // SoA-style resource tables — Vulkan objects live ONLY here.
        ResourcePool<BufferEntry>   m_Buffers;
        ResourcePool<TextureEntry>  m_Textures;
        ResourcePool<SamplerEntry>  m_Samplers;
        ResourcePool<PipelineEntry> m_Pipelines;
    };

    // ===========================================================
    // Public factory — the only symbol exported by this module
    // ===========================================================
    std::unique_ptr<RHI::IDevice> CreateNullDevice()
    {
        return std::make_unique<NullDevice>();
    }
}


