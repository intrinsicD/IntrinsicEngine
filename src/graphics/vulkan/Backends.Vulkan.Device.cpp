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

import :Internal;

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

