module;

#include <cassert>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <vector>

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>
#include "Vulkan.hpp"

module Extrinsic.Backends.Vulkan;

import :Device;

namespace Extrinsic::Backends::Vulkan
{

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
    m_NeedsResize       = false;
    m_FrameSlot         = 0;
    m_GlobalFrameNumber = 0;

    std::fprintf(stderr,
                 "[VulkanDevice::Initialize] Promoted Vulkan device lifecycle is present but "
                 "swapchain/device bring-up is not complete; device remains non-operational.\n");
}

void VulkanDevice::Shutdown()
{
    WaitIdle();

    m_TransferQueue.reset();
    m_Profiler.reset();
    m_BindlessHeap.reset();
    m_SwapchainHandles.clear();
    m_SwapchainViews.clear();
    m_SwapchainImages.clear();
    m_Swapchain        = VK_NULL_HANDLE;
    m_SwapchainFormat  = VK_FORMAT_UNDEFINED;
    m_SwapchainExtent  = {};
    m_DefaultSamplerHandle = {};
    m_GlobalPipelineLayout = VK_NULL_HANDLE;
    m_Operational      = false;
    m_NeedsResize      = false;
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
        return false;

    outFrame.FrameIndex = m_FrameSlot;
    outFrame.SwapchainImageIndex = m_FrameSlot % static_cast<std::uint32_t>(m_SwapchainHandles.size());
    return true;
}

void VulkanDevice::EndFrame(const RHI::FrameHandle& frame)
{
    if (!m_Operational)
        return;

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
    m_NeedsResize = true;
}

Platform::Extent2D VulkanDevice::GetBackbufferExtent() const
{
    return Platform::Extent2D{.Width = static_cast<int>(m_SwapchainExtent.width),
                              .Height = static_cast<int>(m_SwapchainExtent.height)};
}

void VulkanDevice::SetPresentMode(RHI::PresentMode mode)
{
    m_PresentMode = mode;
    m_NeedsResize = true;
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

RHI::BufferHandle VulkanDevice::CreateBuffer(const RHI::BufferDesc& desc)
{
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
    DeferDelete([vma, vkBuf, vkAlloc]() mutable
    {
        vmaDestroyBuffer(vma, vkBuf, vkAlloc);
    });
}

void VulkanDevice::WriteBuffer(RHI::BufferHandle handle, const void* data,
                                uint64_t size, uint64_t offset)
{
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
        fprintf(stderr, "[VulkanDevice::WriteBuffer] Failed to allocate staging buffer\n");
        return;
    }

    // 2. Copy data into the staging buffer.
    std::memcpy(stagingInfo.pMappedData, data, static_cast<size_t>(size));

    // 3. Record and submit a one-shot vkCmdCopyBuffer.
    VkCommandBuffer cmd = BeginOneShot();
    VkBufferCopy region{};
    region.srcOffset = 0;
    region.dstOffset = offset;
    region.size      = size;
    vkCmdCopyBuffer(cmd, stagingBuf, buf->Buffer, 1, &region);
    EndOneShot(cmd);  // submits + vkQueueWaitIdle → GPU work is complete

    // 4. The GPU has finished reading from the staging buffer — safe to destroy.
    vmaDestroyBuffer(m_Vma, stagingBuf, stagingAlloc);
}

uint64_t VulkanDevice::GetBufferDeviceAddress(RHI::BufferHandle handle) const
{
    const VulkanBuffer* buf = m_Buffers.GetIfValid(handle);
    if (!buf || !buf->HasBDA) return 0;

    VkBufferDeviceAddressInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    info.buffer = buf->Buffer;
    return vkGetBufferDeviceAddress(m_Device, &info);
}

} // namespace Extrinsic::Backends::Vulkan

