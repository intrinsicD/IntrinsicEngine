module;

#include <cassert>
#include <cstring>
#include <mutex>

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>
#include "Vulkan.hpp"

module Extrinsic.Backends.Vulkan;

import :Transfer;
import Extrinsic.Core.Logging;

namespace Extrinsic::Backends::Vulkan
{

// =============================================================================
// §5  StagingBelt
// =============================================================================

StagingBelt::StagingBelt(VkDevice device, VmaAllocator vma, size_t capacityBytes)
    : m_Device(device), m_Vma(vma), m_Capacity(capacityBytes), m_Ring(capacityBytes)
{
    if (m_Device == VK_NULL_HANDLE || m_Vma == VK_NULL_HANDLE || m_Capacity == 0)
    {
        Core::Log::Error("[StagingBelt] Cannot create staging belt without a valid device/VMA/capacity");
        return;
    }

    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size  = capacityBytes;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo aci{};
    aci.usage = VMA_MEMORY_USAGE_CPU_ONLY;
    aci.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT
              | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

    VmaAllocationInfo info{};
    const VkResult result = vmaCreateBuffer(m_Vma, &bci, &aci, &m_Buffer, &m_Alloc, &info);
    if (result != VK_SUCCESS || m_Buffer == VK_NULL_HANDLE || m_Alloc == VK_NULL_HANDLE || info.pMappedData == nullptr)
    {
        Core::Log::Error("[StagingBelt] vmaCreateBuffer failed; transfer staging unavailable");
        if (m_Buffer != VK_NULL_HANDLE && m_Alloc != VK_NULL_HANDLE)
            vmaDestroyBuffer(m_Vma, m_Buffer, m_Alloc);
        m_Buffer = VK_NULL_HANDLE;
        m_Alloc = VK_NULL_HANDLE;
        m_Mapped = nullptr;
        return;
    }
    m_Mapped = info.pMappedData;
    assert(m_Mapped);
}

StagingBelt::~StagingBelt()
{
    if (m_Buffer != VK_NULL_HANDLE)
        vmaDestroyBuffer(m_Vma, m_Buffer, m_Alloc);
}

bool StagingBelt::IsValid() const noexcept
{
    return m_Device != VK_NULL_HANDLE && m_Vma != VK_NULL_HANDLE &&
           m_Buffer != VK_NULL_HANDLE && m_Alloc != VK_NULL_HANDLE &&
           m_Mapped != nullptr && m_Capacity > 0;
}

StagingBelt::Allocation StagingBelt::Allocate(size_t sizeBytes, size_t alignment)
{
    std::scoped_lock lock{m_Mutex};
    const auto offset = m_Ring.Allocate(sizeBytes, alignment);
    if (!offset)
    {
        Core::Log::Error("[StagingBelt] Out of staging memory");
        return {};
    }
    return {m_Buffer, *offset, static_cast<char*>(m_Mapped) + *offset, sizeBytes};
}

void StagingBelt::Retire(uint64_t retireValue)
{
    std::scoped_lock lock{m_Mutex};
    m_Ring.Retire(retireValue);
}

void StagingBelt::CancelPending()
{
    std::scoped_lock lock{m_Mutex};
    m_Ring.CancelPending();
}

void StagingBelt::GarbageCollect(uint64_t completedValue)
{
    std::scoped_lock lock{m_Mutex};
    m_Ring.GarbageCollect(completedValue);
}

} // namespace Extrinsic::Backends::Vulkan

