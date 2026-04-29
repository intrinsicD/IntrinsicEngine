module;

#include <cassert>
#include <cstdio>
#include <cstring>
#include <mutex>

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
// §5  StagingBelt
// =============================================================================

StagingBelt::StagingBelt(VkDevice device, VmaAllocator vma, size_t capacityBytes)
    : m_Device(device), m_Vma(vma), m_Capacity(capacityBytes)
{
    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size  = capacityBytes;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo aci{};
    aci.usage = VMA_MEMORY_USAGE_CPU_ONLY;
    aci.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT
              | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

    VmaAllocationInfo info{};
    VK_CHECK_FATAL(vmaCreateBuffer(m_Vma, &bci, &aci, &m_Buffer, &m_Alloc, &info));
    m_Mapped = info.pMappedData;
    assert(m_Mapped);
}

StagingBelt::~StagingBelt()
{
    if (m_Buffer != VK_NULL_HANDLE)
        vmaDestroyBuffer(m_Vma, m_Buffer, m_Alloc);
}

StagingBelt::Allocation StagingBelt::Allocate(size_t sizeBytes, size_t alignment)
{
    std::scoped_lock lock{m_Mutex};
    const size_t aligned = AlignUp(m_Tail, alignment ? alignment : 1);
    const size_t end     = aligned + sizeBytes;

    if (end > m_Capacity)
    {
        // Wrap: try from zero — only valid if head hasn't wrapped here yet.
        const size_t wEnd = sizeBytes;
        if (wEnd > m_Head)
        {
            fprintf(stderr, "[StagingBelt] Out of staging memory!\n");
            return {};
        }
        m_Tail = wEnd;
        void* ptr = static_cast<char*>(m_Mapped);
        if (!m_HasPending) { m_PendingBegin = 0; m_HasPending = true; }
        m_PendingEnd = wEnd;
        return {m_Buffer, 0, ptr, sizeBytes};
    }

    if (aligned < m_Head && end > m_Head)
    {
        fprintf(stderr, "[StagingBelt] Staging belt head collision!\n");
        return {};
    }

    m_Tail = end;
    void* ptr = static_cast<char*>(m_Mapped) + aligned;
    if (!m_HasPending) { m_PendingBegin = aligned; m_HasPending = true; }
    m_PendingEnd = end;
    return {m_Buffer, aligned, ptr, sizeBytes};
}

void StagingBelt::Retire(uint64_t retireValue)
{
    std::scoped_lock lock{m_Mutex};
    if (m_HasPending)
    {
        m_InFlight.push_back({m_PendingBegin, m_PendingEnd, retireValue});
        m_HasPending = false;
    }
}

void StagingBelt::GarbageCollect(uint64_t completedValue)
{
    std::scoped_lock lock{m_Mutex};
    while (!m_InFlight.empty() && m_InFlight.front().RetireValue <= completedValue)
    {
        m_Head = m_InFlight.front().End;
        m_InFlight.pop_front();
    }
    if (m_InFlight.empty()) { m_Head = 0; m_Tail = 0; }
}

} // namespace Extrinsic::Backends::Vulkan

