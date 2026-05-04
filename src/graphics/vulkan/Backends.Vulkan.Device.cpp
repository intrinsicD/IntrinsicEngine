module;

#include <cassert>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>
#include "Vulkan.hpp"

module Extrinsic.Backends.Vulkan;

import :Device;
import Extrinsic.Core.Logging;

namespace Extrinsic::Backends::Vulkan
{
namespace
{
    std::atomic<std::uint64_t> g_FallbackBindlessAllocationAttempts{0};
    std::atomic<std::uint64_t> g_FallbackTransferUploadAttempts{0};
    std::atomic<std::uint64_t> g_FallbackPipelineCreationAttempts{0};
    std::atomic<std::uint64_t> g_FallbackBeginFrameAttempts{0};
    std::atomic<std::uint8_t>  g_LastFallbackPipelineReason{
        static_cast<std::uint8_t>(FallbackPipelineReason::None)};

    [[nodiscard]] bool HasImageUsage(const VkImageUsageFlags usage, const VkImageUsageFlags bit) noexcept
    {
        return (usage & bit) != 0;
    }

    [[nodiscard]] bool IsDepthStencilFormat(const VkFormat format) noexcept
    {
        return format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT_S8_UINT;
    }
}

void NoteFallbackBindlessAllocationAttempt()
{
    g_FallbackBindlessAllocationAttempts.fetch_add(1, std::memory_order_relaxed);
}

std::uint64_t GetFallbackBindlessAllocationAttemptCount() noexcept
{
    return g_FallbackBindlessAllocationAttempts.load(std::memory_order_relaxed);
}

void NoteFallbackTransferUploadAttempt()
{
    g_FallbackTransferUploadAttempts.fetch_add(1, std::memory_order_relaxed);
}

std::uint64_t GetFallbackTransferUploadAttemptCount() noexcept
{
    return g_FallbackTransferUploadAttempts.load(std::memory_order_relaxed);
}

void NoteFallbackPipelineCreationAttempt(FallbackPipelineReason reason)
{
    g_FallbackPipelineCreationAttempts.fetch_add(1, std::memory_order_relaxed);
    g_LastFallbackPipelineReason.store(static_cast<std::uint8_t>(reason),
                                       std::memory_order_relaxed);
}

std::uint64_t GetFallbackPipelineCreationAttemptCount() noexcept
{
    return g_FallbackPipelineCreationAttempts.load(std::memory_order_relaxed);
}

FallbackPipelineReason GetLastFallbackPipelineReason() noexcept
{
    return static_cast<FallbackPipelineReason>(
        g_LastFallbackPipelineReason.load(std::memory_order_relaxed));
}

namespace
{
    void NoteFallbackBeginFrameAttempt() noexcept
    {
        g_FallbackBeginFrameAttempts.fetch_add(1, std::memory_order_relaxed);
    }
}

std::uint64_t GetFallbackBeginFrameAttemptCount() noexcept
{
    return g_FallbackBeginFrameAttempts.load(std::memory_order_relaxed);
}

FallbackDiagnosticsSnapshot GetFallbackDiagnosticsSnapshot() noexcept
{
    FallbackDiagnosticsSnapshot snapshot{};
    snapshot.BindlessAllocationAttempts =
        g_FallbackBindlessAllocationAttempts.load(std::memory_order_relaxed);
    snapshot.TransferUploadAttempts =
        g_FallbackTransferUploadAttempts.load(std::memory_order_relaxed);
    snapshot.PipelineCreationAttempts =
        g_FallbackPipelineCreationAttempts.load(std::memory_order_relaxed);
    snapshot.LastPipelineReason = static_cast<FallbackPipelineReason>(
        g_LastFallbackPipelineReason.load(std::memory_order_relaxed));
    snapshot.BeginFrameAttempts =
        g_FallbackBeginFrameAttempts.load(std::memory_order_relaxed);
    return snapshot;
}

// =============================================================================
// §12  Factory
// =============================================================================

std::unique_ptr<RHI::IDevice> CreateVulkanDevice()
{
    // Explicit base-pointer construction — unique_ptr<Derived>→unique_ptr<Base>
    // implicit conversion can confuse Clang's module-purview type resolution.
    return std::unique_ptr<RHI::IDevice>(new VulkanDevice());
}

// =============================================================================
// §11  VulkanDevice — destructor & lifecycle
// (buffer/texture/sampler/pipeline CRUD see upload path summary below)
// =============================================================================

VulkanDevice::~VulkanDevice() = default;

void VulkanDevice::Initialize(Platform::IWindow& window,
                              const Core::Config::RenderConfig& config)
{
    (void)window;

    m_ValidationEnabled = config.EnableValidation;
    m_Operational       = false;
    m_FrameSlot         = 0;
    m_GlobalFrameNumber = 0;

    Core::Log::Warn("[VulkanDevice::Initialize] Promoted Vulkan device lifecycle is present but "
                    "swapchain/device bring-up is not complete; device remains non-operational.");
}

void VulkanDevice::Shutdown()
{
    WaitIdle();

    // Shutdown invariant: deferred-deletion queues own resources already
    // removed from m_Buffers/m_Images/m_Samplers/m_Pipelines. Any handle still
    // present in a resource pool below has not been queued for destruction, so
    // pool drain is responsible for releasing it exactly once.
    for (uint32_t frameSlot = 0; frameSlot < kMaxFramesInFlight; ++frameSlot)
        FlushDeletionQueue(frameSlot);

    m_TransferQueue.reset();
    m_Profiler.reset();
    m_BindlessHeap.reset();

    const VkDevice device = m_Device;
    const VmaAllocator vma = m_Vma;

    m_Pipelines.ForEach([device](RHI::PipelineHandle, VulkanPipeline& pipeline)
    {
        if (device != VK_NULL_HANDLE)
        {
            if (pipeline.Pipeline != VK_NULL_HANDLE)
                vkDestroyPipeline(device, pipeline.Pipeline, nullptr);
            if (pipeline.OwnsLayout && pipeline.Layout != VK_NULL_HANDLE)
                vkDestroyPipelineLayout(device, pipeline.Layout, nullptr);
        }
        pipeline.Pipeline = VK_NULL_HANDLE;
        pipeline.Layout = VK_NULL_HANDLE;
    });
    m_Pipelines.Clear();

    m_Samplers.ForEach([device](RHI::SamplerHandle, VulkanSampler& sampler)
    {
        if (device != VK_NULL_HANDLE && sampler.Sampler != VK_NULL_HANDLE)
            vkDestroySampler(device, sampler.Sampler, nullptr);
        sampler.Sampler = VK_NULL_HANDLE;
    });
    m_Samplers.Clear();

    m_Images.ForEach([device, vma](RHI::TextureHandle, VulkanImage& image)
    {
        if (device != VK_NULL_HANDLE && image.View != VK_NULL_HANDLE)
            vkDestroyImageView(device, image.View, nullptr);
        if (image.OwnsMemory && vma != VK_NULL_HANDLE && image.Image != VK_NULL_HANDLE &&
            image.Allocation != VK_NULL_HANDLE)
            vmaDestroyImage(vma, image.Image, image.Allocation);
        image.Image = VK_NULL_HANDLE;
        image.View = VK_NULL_HANDLE;
        image.Allocation = VK_NULL_HANDLE;
    });
    m_Images.Clear();

    m_Buffers.ForEach([vma](RHI::BufferHandle, VulkanBuffer& buffer)
    {
        if (vma != VK_NULL_HANDLE && buffer.Buffer != VK_NULL_HANDLE &&
            buffer.Allocation != VK_NULL_HANDLE)
            vmaDestroyBuffer(vma, buffer.Buffer, buffer.Allocation);
        buffer.Buffer = VK_NULL_HANDLE;
        buffer.Allocation = VK_NULL_HANDLE;
        buffer.MappedPtr = nullptr;
    });
    m_Buffers.Clear();

    m_SwapchainHandles.clear();
    m_SwapchainViews.clear();
    m_SwapchainImages.clear();
    m_Swapchain        = VK_NULL_HANDLE;
    m_SwapchainFormat  = VK_FORMAT_UNDEFINED;
    m_SwapchainExtent  = {};
    m_DefaultSamplerHandle = {};
    m_GlobalPipelineLayout = VK_NULL_HANDLE;
    m_SamplerAnisotropySupported = false;
    m_Operational      = false;
}

void VulkanDevice::WaitIdle()
{
    if (m_Device != VK_NULL_HANDLE)
        vkDeviceWaitIdle(m_Device);
}

bool VulkanDevice::BeginFrame(RHI::FrameHandle& outFrame)
{
    outFrame = {};

    if (!m_Operational || m_Swapchain == VK_NULL_HANDLE || m_SwapchainHandles.empty())
    {
        NoteFallbackBeginFrameAttempt();
        Core::Log::Warn("[VulkanDevice::BeginFrame] device non-operational; returning fail-closed (no frame produced)");
        return false;
    }

    // BeginFrame hands out the current frame slot without rotating it. The
    // matching EndFrame call below rotates from the handed-out slot exactly
    // once, so acquire/end pairing remains single-owner even before full
    // swapchain acquire/present support lands.
    outFrame.FrameIndex = m_FrameSlot;
    outFrame.SwapchainImageIndex = m_FrameSlot % static_cast<std::uint32_t>(m_SwapchainHandles.size());
    return true;
}

void VulkanDevice::EndFrame(const RHI::FrameHandle& frame)
{
    if (!m_Operational)
        return;

    // Rotate from the slot returned by BeginFrame, then advance the global
    // post-EndFrame counter consumed by renderer maintenance/deferred-delete
    // code.
    m_FrameSlot = (frame.FrameIndex + 1u) % kMaxFramesInFlight;
    ++m_GlobalFrameNumber;
}

void VulkanDevice::Present(const RHI::FrameHandle& frame)
{
    (void)frame;
    if (!m_Operational)
        return;
}

void VulkanDevice::Resize(uint32_t width, uint32_t height)
{
    m_SwapchainExtent = VkExtent2D{.width = width, .height = height};
}

Platform::Extent2D VulkanDevice::GetBackbufferExtent() const
{
    return Platform::Extent2D{.Width = static_cast<int>(m_SwapchainExtent.width),
                              .Height = static_cast<int>(m_SwapchainExtent.height)};
}

void VulkanDevice::SetPresentMode(RHI::PresentMode mode)
{
    m_PresentMode = mode;
}

RHI::TextureHandle VulkanDevice::GetBackbufferHandle(const RHI::FrameHandle& frame) const
{
    if (frame.SwapchainImageIndex >= m_SwapchainHandles.size())
        return {};

    return m_SwapchainHandles[frame.SwapchainImageIndex];
}

RHI::ICommandContext& VulkanDevice::GetGraphicsContext(uint32_t frameIndex)
{
    return m_CmdContexts[frameIndex % kMaxFramesInFlight];
}

// =============================================================================
// §11a  VulkanDevice — buffer subsystem
//
// Upload path summary
// -------------------
// Host-visible buffers (HostVisible = true):
//   VMA maps them persistently into CPU address space at creation time.
//   WriteBuffer → memcpy directly into MappedPtr + offset.
//   Zero GPU work, zero command buffer, zero synchronisation needed.
//   Used for: per-entity dynamic geometry, uniform ring-buffers, staging.
//
// Device-local buffers (HostVisible = false):
//   Reside in VRAM (VMA_MEMORY_USAGE_GPU_ONLY), inaccessible by the CPU.
//   WriteBuffer → allocate a temporary host-visible staging VkBuffer,
//   memcpy the data, record vkCmdCopyBuffer in a one-shot command buffer,
//   submit to the graphics queue, and vkQueueWaitIdle to synchronise.
//   This is intentionally blocking — it is the scene-load path, not the
//   per-frame render path.  For non-blocking streaming use
//   IDevice::GetTransferQueue().UploadBuffer() which returns a TransferToken
//   and uses the StagingBelt ring-buffer on the dedicated transfer queue.
//
// BDA note: Storage buffers always get VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
// via ToVkBufferUsage.  GetBufferDeviceAddress queries
// vkGetBufferDeviceAddress and caches nothing; the BDA is stable for the
// lifetime of the VkBuffer.
// =============================================================================

namespace
{
    [[nodiscard]] VkImageType ToVkImageType(const RHI::TextureDimension dimension)
    {
        switch (dimension)
        {
        case RHI::TextureDimension::Tex1D: return VK_IMAGE_TYPE_1D;
        case RHI::TextureDimension::Tex2D: return VK_IMAGE_TYPE_2D;
        case RHI::TextureDimension::Tex3D: return VK_IMAGE_TYPE_3D;
        case RHI::TextureDimension::TexCube: return VK_IMAGE_TYPE_2D;
        }
        return VK_IMAGE_TYPE_2D;
    }

    [[nodiscard]] VkImageViewType ToVkImageViewType(const RHI::TextureDimension dimension)
    {
        switch (dimension)
        {
        case RHI::TextureDimension::Tex1D: return VK_IMAGE_VIEW_TYPE_1D;
        case RHI::TextureDimension::Tex2D: return VK_IMAGE_VIEW_TYPE_2D;
        case RHI::TextureDimension::Tex3D: return VK_IMAGE_VIEW_TYPE_3D;
        case RHI::TextureDimension::TexCube: return VK_IMAGE_VIEW_TYPE_CUBE;
        }
        return VK_IMAGE_VIEW_TYPE_2D;
    }

    [[nodiscard]] VkSampleCountFlagBits ToVkSampleCount(const std::uint32_t sampleCount)
    {
        switch (sampleCount)
        {
        case 1: return VK_SAMPLE_COUNT_1_BIT;
        case 2: return VK_SAMPLE_COUNT_2_BIT;
        case 4: return VK_SAMPLE_COUNT_4_BIT;
        case 8: return VK_SAMPLE_COUNT_8_BIT;
        case 16: return VK_SAMPLE_COUNT_16_BIT;
        case 32: return VK_SAMPLE_COUNT_32_BIT;
        case 64: return VK_SAMPLE_COUNT_64_BIT;
        default: return VK_SAMPLE_COUNT_1_BIT;
        }
    }

    [[nodiscard]] std::uint32_t MipExtent(const std::uint32_t extent, const std::uint32_t mipLevel)
    {
        const std::uint32_t shifted = extent >> mipLevel;
        return shifted == 0 ? 1u : shifted;
    }

    [[nodiscard]] std::uint32_t FormatBlockByteSize(const VkFormat format)
    {
        switch (format)
        {
        case VK_FORMAT_R8_UNORM: return 1;
        case VK_FORMAT_R8G8_UNORM: return 2;
        case VK_FORMAT_R16_SFLOAT:
        case VK_FORMAT_R16_UINT:
        case VK_FORMAT_R16_UNORM: return 2;
        case VK_FORMAT_R16G16_SFLOAT: return 4;
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB:
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_SRGB:
        case VK_FORMAT_R32_SFLOAT:
        case VK_FORMAT_R32_UINT:
        case VK_FORMAT_R32_SINT: return 4;
        case VK_FORMAT_R16G16B16A16_SFLOAT: return 8;
        case VK_FORMAT_R32G32_SFLOAT: return 8;
        case VK_FORMAT_R32G32B32_SFLOAT: return 12;
        case VK_FORMAT_R32G32B32A32_SFLOAT: return 16;
        case VK_FORMAT_D16_UNORM: return 2;
        case VK_FORMAT_D32_SFLOAT: return 4;
        case VK_FORMAT_D24_UNORM_S8_UINT:
        case VK_FORMAT_D32_SFLOAT_S8_UINT: return 0;
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK: return 8;
        case VK_FORMAT_BC3_UNORM_BLOCK:
        case VK_FORMAT_BC5_UNORM_BLOCK:
        case VK_FORMAT_BC7_UNORM_BLOCK:
        case VK_FORMAT_BC7_SRGB_BLOCK: return 16;
        default: return 0;
        }
    }

    [[nodiscard]] bool IsBlockCompressedFormat(const VkFormat format)
    {
        switch (format)
        {
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
        case VK_FORMAT_BC3_UNORM_BLOCK:
        case VK_FORMAT_BC5_UNORM_BLOCK:
        case VK_FORMAT_BC7_UNORM_BLOCK:
        case VK_FORMAT_BC7_SRGB_BLOCK:
            return true;
        default:
            return false;
        }
    }

    [[nodiscard]] std::uint64_t RequiredUploadBytes(const VulkanImage& image,
                                                    const std::uint32_t mipLevel)
    {
        const std::uint32_t blockBytes = FormatBlockByteSize(image.Format);
        if (blockBytes == 0)
            return 0;

        const std::uint32_t width = MipExtent(image.Width, mipLevel);
        const std::uint32_t height = MipExtent(image.Height, mipLevel);
        const std::uint32_t depth = MipExtent(image.Depth, mipLevel);

        if (IsBlockCompressedFormat(image.Format))
        {
            const std::uint64_t blocksWide = (static_cast<std::uint64_t>(width) + 3u) / 4u;
            const std::uint64_t blocksHigh = (static_cast<std::uint64_t>(height) + 3u) / 4u;
            return blocksWide * blocksHigh * depth * blockBytes;
        }

        return static_cast<std::uint64_t>(width) * height * depth * blockBytes;
    }

    void ImageBarrier(VkCommandBuffer cmd,
                      VkImage image,
                      VkImageAspectFlags aspectMask,
                      std::uint32_t mipLevel,
                      std::uint32_t arrayLayer,
                      VkImageLayout oldLayout,
                      VkImageLayout newLayout,
                      VkPipelineStageFlags2 srcStage,
                      VkAccessFlags2 srcAccess,
                      VkPipelineStageFlags2 dstStage,
                      VkAccessFlags2 dstAccess)
    {
        VkImageMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.srcStageMask = srcStage;
        barrier.srcAccessMask = srcAccess;
        barrier.dstStageMask = dstStage;
        barrier.dstAccessMask = dstAccess;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.image = image;
        barrier.subresourceRange = VkImageSubresourceRange{.aspectMask = aspectMask,
                                                           .baseMipLevel = mipLevel,
                                                           .levelCount = 1,
                                                           .baseArrayLayer = arrayLayer,
                                                           .layerCount = 1};

        VkDependencyInfo dependency{};
        dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(cmd, &dependency);
    }
}

RHI::BufferHandle VulkanDevice::CreateBuffer(const RHI::BufferDesc& desc)
{
    if (!m_Operational || m_Device == VK_NULL_HANDLE || m_Vma == VK_NULL_HANDLE || desc.SizeBytes == 0)
        return {};

    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size  = desc.SizeBytes;
    bci.usage = ToVkBufferUsage(desc.Usage) | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    // TransferSrc always present so WriteBuffer's staging copy (dst→src) works.
    bci.usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo aci{};
    VmaAllocationInfo info{};

    if (desc.HostVisible)
    {
        // Persistently mapped, coherent where possible.
        aci.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        aci.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT
                  | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    }
    else
    {
        aci.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    }

    VulkanBuffer buf{};
    buf.SizeBytes   = desc.SizeBytes;
    buf.HostVisible = desc.HostVisible;
    buf.HasBDA      = RHI::HasUsage(desc.Usage, RHI::BufferUsage::Storage);

    if (vmaCreateBuffer(m_Vma, &bci, &aci,
                        &buf.Buffer, &buf.Allocation, &info) != VK_SUCCESS)
        return {};

    if (desc.HostVisible)
        buf.MappedPtr = info.pMappedData;

    if (desc.DebugName && m_ValidationEnabled)
    {
        VkDebugUtilsObjectNameInfoEXT nm{};
        nm.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        nm.objectType   = VK_OBJECT_TYPE_BUFFER;
        nm.objectHandle = reinterpret_cast<uint64_t>(buf.Buffer);
        nm.pObjectName  = desc.DebugName;
        vkSetDebugUtilsObjectNameEXT(m_Device, &nm);
    }

    return m_Buffers.Add(std::move(buf));
}

void VulkanDevice::DestroyBuffer(RHI::BufferHandle handle)
{
    VulkanBuffer* buf = m_Buffers.GetIfValid(handle);
    if (!buf) return;

    // Move the Vulkan objects out so the pool slot can be reclaimed.
    VkBuffer      vkBuf   = buf->Buffer;
    VmaAllocation vkAlloc = buf->Allocation;
    buf->Buffer     = VK_NULL_HANDLE;
    buf->Allocation = VK_NULL_HANDLE;
    buf->MappedPtr  = nullptr;

    m_Buffers.Remove(handle, m_GlobalFrameNumber);

    // Defer the actual VMA destroy until this frame's resources are safe to release.
    VmaAllocator vma = m_Vma;
    if (vma == VK_NULL_HANDLE || vkBuf == VK_NULL_HANDLE || vkAlloc == VK_NULL_HANDLE)
        return;

    DeferDelete([vma, vkBuf, vkAlloc]() mutable
    {
        vmaDestroyBuffer(vma, vkBuf, vkAlloc);
    });
}

void VulkanDevice::WriteBuffer(RHI::BufferHandle handle, const void* data,
                                uint64_t size, uint64_t offset)
{
    if (!m_Operational || m_Device == VK_NULL_HANDLE || m_Vma == VK_NULL_HANDLE)
        return;

    if (!data || size == 0) return;
    VulkanBuffer* buf = m_Buffers.GetIfValid(handle);
    if (!buf) return;

    if (buf->HostVisible)
    {
        // ----------------------------------------------------------------
        // Fast path: direct memcpy into the persistently-mapped pointer.
        // No GPU work, no synchronisation.
        // ----------------------------------------------------------------
        assert(buf->MappedPtr && "HostVisible buffer has null MappedPtr");
        std::memcpy(static_cast<char*>(buf->MappedPtr) + offset, data, size);
        // Note: VMA_MEMORY_USAGE_CPU_TO_GPU selects HOST_COHERENT memory when
        // available (integrated GPUs and most desktops).  On some discrete GPUs
        // that lack HOST_COHERENT the mapping is write-combined; a coherent
        // flush would be needed.  For strict portability:
        //   vmaFlushAllocation(m_Vma, buf->Allocation, offset, size);
        // This is left as a TODO for production hardening.
        return;
    }

    // ----------------------------------------------------------------
    // Slow path: device-local buffer — upload via temporary staging buffer.
    // This is synchronous (vkQueueWaitIdle).  Only used for scene loading
    // and rare CPU→GPU writes (e.g. CullingSystem::SyncGpuBuffer).
    // For async streaming use IDevice::GetTransferQueue().UploadBuffer().
    // ----------------------------------------------------------------

    // 1. Create a temporary host-visible staging buffer.
    VkBufferCreateInfo stagingCI{};
    stagingCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingCI.size  = size;
    stagingCI.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo stagingACI{};
    stagingACI.usage = VMA_MEMORY_USAGE_CPU_ONLY;
    stagingACI.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT
                     | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

    VkBuffer      stagingBuf{};
    VmaAllocation stagingAlloc{};
    VmaAllocationInfo stagingInfo{};

    if (vmaCreateBuffer(m_Vma, &stagingCI, &stagingACI,
                        &stagingBuf, &stagingAlloc, &stagingInfo) != VK_SUCCESS)
    {
        Core::Log::Error("[VulkanDevice::WriteBuffer] Failed to allocate staging buffer");
        return;
    }

    // 2. Copy data into the staging buffer.
    std::memcpy(stagingInfo.pMappedData, data, static_cast<size_t>(size));

    // 3. Record and submit a one-shot vkCmdCopyBuffer.
    VkCommandBuffer cmd = BeginOneShot();
    if (cmd == VK_NULL_HANDLE)
    {
        vmaDestroyBuffer(m_Vma, stagingBuf, stagingAlloc);
        return;
    }
    VkBufferCopy region{};
    region.srcOffset = 0;
    region.dstOffset = offset;
    region.size      = size;
    vkCmdCopyBuffer(cmd, stagingBuf, buf->Buffer, 1, &region);
    (void)EndOneShot(cmd);  // submits + vkQueueWaitIdle → GPU work is complete when true

    // 4. The GPU has finished reading from the staging buffer — safe to destroy.
    vmaDestroyBuffer(m_Vma, stagingBuf, stagingAlloc);
}

uint64_t VulkanDevice::GetBufferDeviceAddress(RHI::BufferHandle handle) const
{
    if (!m_Operational || m_Device == VK_NULL_HANDLE)
        return 0;

    const VulkanBuffer* buf = m_Buffers.GetIfValid(handle);
    if (!buf || !buf->HasBDA) return 0;

    VkBufferDeviceAddressInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    info.buffer = buf->Buffer;
    return vkGetBufferDeviceAddress(m_Device, &info);
}

RHI::TextureHandle VulkanDevice::CreateTexture(const RHI::TextureDesc& desc)
{
    if (!m_Operational || m_Device == VK_NULL_HANDLE || m_Vma == VK_NULL_HANDLE)
        return {};

    const VkFormat format = ToVkFormat(desc.Fmt);
    const VkImageUsageFlags usage = ToVkTextureUsage(desc.Usage);
    if (format == VK_FORMAT_UNDEFINED || usage == 0 || desc.Width == 0 || desc.Height == 0 ||
        desc.DepthOrArrayLayers == 0 || desc.MipLevels == 0)
    {
        return {};
    }

    if (desc.Dimension == RHI::TextureDimension::TexCube && desc.DepthOrArrayLayers != 6)
        return {};

    // RHI::TextureDesc::DepthOrArrayLayers maps to depth for Tex3D and to
    // array-layer count for Tex1D/Tex2D/TexCube. Cubes require exactly six
    // layers in the current RHI contract; 2D array textures are represented by
    // Tex2D with DepthOrArrayLayers > 1.
    VulkanImage image{};
    image.Format = format;
    image.Usage = usage;
    image.Width = desc.Width;
    image.Height = desc.Height;
    image.Depth = desc.Dimension == RHI::TextureDimension::Tex3D ? desc.DepthOrArrayLayers : 1u;
    image.MipLevels = desc.MipLevels;
    image.ArrayLayers = desc.Dimension == RHI::TextureDimension::Tex3D ? 1u : desc.DepthOrArrayLayers;
    image.CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image.OwnsMemory = true;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = ToVkImageType(desc.Dimension);
    imageInfo.format = format;
    imageInfo.extent = VkExtent3D{.width = desc.Width,
                                  .height = desc.Height,
                                  .depth = desc.Dimension == RHI::TextureDimension::Tex3D
                                               ? desc.DepthOrArrayLayers
                                               : 1u};
    imageInfo.mipLevels = desc.MipLevels;
    imageInfo.arrayLayers = image.ArrayLayers;
    imageInfo.samples = ToVkSampleCount(desc.SampleCount);
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = usage;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (desc.Dimension == RHI::TextureDimension::TexCube)
        imageInfo.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(m_Vma, &imageInfo, &allocationInfo, &image.Image, &image.Allocation, nullptr) != VK_SUCCESS)
    {
        Core::Log::Error("[VulkanDevice::CreateTexture] Failed to allocate image");
        return {};
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image.Image;
    viewInfo.viewType = ToVkImageViewType(desc.Dimension);
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = AspectFromFormat(format);
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = desc.MipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = image.ArrayLayers;

    if (vkCreateImageView(m_Device, &viewInfo, nullptr, &image.View) != VK_SUCCESS)
    {
        Core::Log::Error("[VulkanDevice::CreateTexture] Failed to create image view");
        vmaDestroyImage(m_Vma, image.Image, image.Allocation);
        return {};
    }

    if (desc.DebugName && m_ValidationEnabled && vkSetDebugUtilsObjectNameEXT)
    {
        VkDebugUtilsObjectNameInfoEXT imageName{};
        imageName.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        imageName.objectType = VK_OBJECT_TYPE_IMAGE;
        imageName.objectHandle = reinterpret_cast<std::uint64_t>(image.Image);
        imageName.pObjectName = desc.DebugName;
        vkSetDebugUtilsObjectNameEXT(m_Device, &imageName);

        VkDebugUtilsObjectNameInfoEXT viewName{};
        viewName.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        viewName.objectType = VK_OBJECT_TYPE_IMAGE_VIEW;
        viewName.objectHandle = reinterpret_cast<std::uint64_t>(image.View);
        viewName.pObjectName = desc.DebugName;
        vkSetDebugUtilsObjectNameEXT(m_Device, &viewName);
    }

    return m_Images.Add(std::move(image));
}

void VulkanDevice::DestroyTexture(RHI::TextureHandle handle)
{
    VulkanImage* image = m_Images.GetIfValid(handle);
    if (!image)
        return;

    const VkDevice device = m_Device;
    const VmaAllocator vma = m_Vma;
    const VkImage vkImage = image->Image;
    const VkImageView view = image->View;
    const VmaAllocation allocation = image->Allocation;
    const bool ownsMemory = image->OwnsMemory;

    image->Image = VK_NULL_HANDLE;
    image->View = VK_NULL_HANDLE;
    image->Allocation = VK_NULL_HANDLE;
    image->CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    m_Images.Remove(handle, m_GlobalFrameNumber);

    if (device == VK_NULL_HANDLE)
        return;

    DeferDelete([device, vma, vkImage, view, allocation, ownsMemory]() mutable
    {
        if (view != VK_NULL_HANDLE)
            vkDestroyImageView(device, view, nullptr);
        if (ownsMemory && vma != VK_NULL_HANDLE && vkImage != VK_NULL_HANDLE && allocation != VK_NULL_HANDLE)
            vmaDestroyImage(vma, vkImage, allocation);
    });
}

void VulkanDevice::WriteTexture(RHI::TextureHandle handle,
                                const void* data,
                                uint64_t dataSizeBytes,
                                uint32_t mipLevel,
                                uint32_t arrayLayer)
{
    if (!m_Operational || m_Device == VK_NULL_HANDLE || m_Vma == VK_NULL_HANDLE)
        return;

    if (!data || dataSizeBytes == 0)
        return;

    VulkanImage* image = m_Images.GetIfValid(handle);
    if (!image || image->Image == VK_NULL_HANDLE)
        return;

    if (!HasImageUsage(image->Usage, VK_IMAGE_USAGE_SAMPLED_BIT))
    {
        Core::Log::Warn("[VulkanDevice::WriteTexture] Skipping upload for texture without sampled usage");
        return;
    }

    if (IsDepthStencilFormat(image->Format))
    {
        Core::Log::Warn("[VulkanDevice::WriteTexture] Depth-stencil uploads are not supported by the current one-shot path");
        return;
    }

    if (mipLevel >= image->MipLevels || arrayLayer >= image->ArrayLayers)
        return;

    const std::uint64_t requiredBytes = RequiredUploadBytes(*image, mipLevel);
    if (requiredBytes == 0 || dataSizeBytes != requiredBytes)
    {
        Core::Log::Warn("[VulkanDevice::WriteTexture] Upload size mismatch: expected={} actual={}",
                        requiredBytes,
                        dataSizeBytes);
        return;
    }

    VkBufferCreateInfo stagingInfo{};
    stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingInfo.size = requiredBytes;
    stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
    allocationInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT
                         | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VmaAllocation stagingAllocation = VK_NULL_HANDLE;
    VmaAllocationInfo stagingAllocationInfo{};

    if (vmaCreateBuffer(m_Vma,
                        &stagingInfo,
                        &allocationInfo,
                        &stagingBuffer,
                        &stagingAllocation,
                        &stagingAllocationInfo) != VK_SUCCESS)
    {
        Core::Log::Error("[VulkanDevice::WriteTexture] Failed to allocate staging buffer");
        return;
    }

    std::memcpy(stagingAllocationInfo.pMappedData, data, static_cast<std::size_t>(requiredBytes));

    VkCommandBuffer cmd = BeginOneShot();
    if (cmd == VK_NULL_HANDLE)
    {
        vmaDestroyBuffer(m_Vma, stagingBuffer, stagingAllocation);
        return;
    }

    const VkImageAspectFlags aspectMask = AspectFromFormat(image->Format);
    ImageBarrier(cmd,
                 image->Image,
                 aspectMask,
                 mipLevel,
                 arrayLayer,
                 image->CurrentLayout,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 image->CurrentLayout == VK_IMAGE_LAYOUT_UNDEFINED ? VK_PIPELINE_STAGE_2_NONE
                                                                   : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                 image->CurrentLayout == VK_IMAGE_LAYOUT_UNDEFINED ? 0
                                                                   : VK_ACCESS_2_MEMORY_WRITE_BIT |
                                                                         VK_ACCESS_2_MEMORY_READ_BIT,
                 VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                 VK_ACCESS_2_TRANSFER_WRITE_BIT);

    const std::uint32_t mipWidth = MipExtent(image->Width, mipLevel);
    const std::uint32_t mipHeight = MipExtent(image->Height, mipLevel);
    const std::uint32_t mipDepth = MipExtent(image->Depth, mipLevel);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.imageSubresource = VkImageSubresourceLayers{.aspectMask = aspectMask,
                                                       .mipLevel = mipLevel,
                                                       .baseArrayLayer = arrayLayer,
                                                       .layerCount = 1};
    region.imageExtent = VkExtent3D{.width = mipWidth, .height = mipHeight, .depth = mipDepth};
    vkCmdCopyBufferToImage(cmd,
                           stagingBuffer,
                           image->Image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1,
                           &region);

    ImageBarrier(cmd,
                 image->Image,
                 aspectMask,
                 mipLevel,
                 arrayLayer,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                 VK_ACCESS_2_TRANSFER_WRITE_BIT,
                 VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                 VK_ACCESS_2_SHADER_READ_BIT);

    if (EndOneShot(cmd))
    {
        image->CurrentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    vmaDestroyBuffer(m_Vma, stagingBuffer, stagingAllocation);
}

RHI::SamplerHandle VulkanDevice::CreateSampler(const RHI::SamplerDesc& desc)
{
    if (!m_Operational || m_Device == VK_NULL_HANDLE)
        return {};

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = ToVkFilter(desc.MagFilter);
    samplerInfo.minFilter = ToVkFilter(desc.MinFilter);
    samplerInfo.mipmapMode = ToVkMipmapMode(desc.MipFilter);
    samplerInfo.addressModeU = ToVkAddressMode(desc.AddressU);
    samplerInfo.addressModeV = ToVkAddressMode(desc.AddressV);
    samplerInfo.addressModeW = ToVkAddressMode(desc.AddressW);
    samplerInfo.mipLodBias = desc.MipLodBias;
    samplerInfo.minLod = desc.MinLod;
    samplerInfo.maxLod = desc.MaxLod;
    // RHI::SamplerDesc does not yet expose a border-color selector; keep the
    // Vulkan default deterministic until GRAPHICS-018S adds the API surface.
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = desc.CompareEnable ? VK_TRUE : VK_FALSE;
    samplerInfo.compareOp = ToVkCompareOp(desc.Compare);

    const bool enableAnisotropy = m_SamplerAnisotropySupported && desc.MaxAnisotropy > 1.0f;
    samplerInfo.anisotropyEnable = enableAnisotropy ? VK_TRUE : VK_FALSE;
    samplerInfo.maxAnisotropy = enableAnisotropy ? desc.MaxAnisotropy : 1.0f;

    VulkanSampler sampler{};
    if (vkCreateSampler(m_Device, &samplerInfo, nullptr, &sampler.Sampler) != VK_SUCCESS)
    {
        Core::Log::Error("[VulkanDevice::CreateSampler] Failed to create sampler");
        return {};
    }

    if (desc.DebugName && m_ValidationEnabled && vkSetDebugUtilsObjectNameEXT)
    {
        VkDebugUtilsObjectNameInfoEXT nameInfo{};
        nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        nameInfo.objectType = VK_OBJECT_TYPE_SAMPLER;
        nameInfo.objectHandle = reinterpret_cast<std::uint64_t>(sampler.Sampler);
        nameInfo.pObjectName = desc.DebugName;
        vkSetDebugUtilsObjectNameEXT(m_Device, &nameInfo);
    }

    return m_Samplers.Add(std::move(sampler));
}

void VulkanDevice::DestroySampler(RHI::SamplerHandle handle)
{
    VulkanSampler* sampler = m_Samplers.GetIfValid(handle);
    if (!sampler)
        return;

    const VkDevice device = m_Device;
    const VkSampler vkSampler = sampler->Sampler;
    sampler->Sampler = VK_NULL_HANDLE;
    m_Samplers.Remove(handle, m_GlobalFrameNumber);

    if (device == VK_NULL_HANDLE || vkSampler == VK_NULL_HANDLE)
        return;

    DeferDelete([device, vkSampler]() mutable
    {
        vkDestroySampler(device, vkSampler, nullptr);
    });
}

RHI::PipelineHandle VulkanDevice::CreatePipeline(const RHI::PipelineDesc& desc)
{
    (void)desc;
    if (!m_Operational || m_Device == VK_NULL_HANDLE || m_GlobalPipelineLayout == VK_NULL_HANDLE)
    {
        NoteFallbackPipelineCreationAttempt(FallbackPipelineReason::PreBringUp);
        Core::Log::Warn("[VulkanDevice] CreatePipeline rejected; device is non-operational or global pipeline layout missing");
        return {};
    }

    // Shader-module and pipeline construction remains a later GRAPHICS-018 slice.
    NoteFallbackPipelineCreationAttempt(FallbackPipelineReason::ShaderMissing);
    Core::Log::Warn("[VulkanDevice] CreatePipeline reached operational guard but shader/pipeline construction is not yet implemented");
    return {};
}

void VulkanDevice::DestroyPipeline(RHI::PipelineHandle handle)
{
    VulkanPipeline* pipeline = m_Pipelines.GetIfValid(handle);
    if (!pipeline)
        return;

    const VkDevice device = m_Device;
    const VkPipeline vkPipeline = pipeline->Pipeline;
    const VkPipelineLayout layout = pipeline->Layout;
    const bool ownsLayout = pipeline->OwnsLayout;
    pipeline->Pipeline = VK_NULL_HANDLE;
    pipeline->Layout = VK_NULL_HANDLE;
    m_Pipelines.Remove(handle, m_GlobalFrameNumber);

    if (device == VK_NULL_HANDLE)
        return;

    DeferDelete([device, vkPipeline, layout, ownsLayout]() mutable
    {
        if (vkPipeline != VK_NULL_HANDLE)
            vkDestroyPipeline(device, vkPipeline, nullptr);
        if (ownsLayout && layout != VK_NULL_HANDLE)
            vkDestroyPipelineLayout(device, layout, nullptr);
    });
}

VkCommandBuffer VulkanDevice::BeginOneShot()
{
    if (!m_Operational || m_Device == VK_NULL_HANDLE || m_FrameSlot >= kMaxFramesInFlight)
        return VK_NULL_HANDLE;

    const VkCommandBuffer cmd = m_Frames[m_FrameSlot].CmdBuffer;
    if (cmd == VK_NULL_HANDLE)
        return VK_NULL_HANDLE;

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return cmd;
}

bool VulkanDevice::EndOneShot(VkCommandBuffer cmd)
{
    if (!m_Operational || m_Device == VK_NULL_HANDLE || cmd == VK_NULL_HANDLE)
        return false;

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
        return false;

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;

    if (m_GraphicsQueue != VK_NULL_HANDLE)
    {
        if (vkQueueSubmit(m_GraphicsQueue, 1, &submit, VK_NULL_HANDLE) != VK_SUCCESS)
            return false;
        if (vkQueueWaitIdle(m_GraphicsQueue) != VK_SUCCESS)
            return false;
        return true;
    }
    return false;
}

void VulkanDevice::DeferDelete(VulkanDeferredDelete fn)
{
    if (!fn)
        return;

    if (!m_Operational || m_FrameSlot >= kMaxFramesInFlight)
    {
        return;
    }

    m_Frames[m_FrameSlot].DeletionQueue.push_back(std::move(fn));
}

void VulkanDevice::FlushDeletionQueue(uint32_t frameSlot)
{
    if (frameSlot >= kMaxFramesInFlight)
        return;

    auto& queue = m_Frames[frameSlot].DeletionQueue;
    for (auto& fn : queue)
    {
        if (fn)
            fn();
    }
    queue.clear();
}

} // namespace Extrinsic::Backends::Vulkan

