module;
#include <cstring> // memcpy
#include <memory>
#include <mutex>
#include <utility> // std::exchange
#include "RHI.Vulkan.hpp"
#include "RHI.DestructionUtils.hpp"

module RHI.Buffer;

import Core.ResourcePool;
import RHI.Device;
import Core.Logging;

namespace RHI
{
    static bool IsHostVisibleUsage(VmaMemoryUsage usage)
    {
        return usage == VMA_MEMORY_USAGE_CPU_TO_GPU ||
               usage == VMA_MEMORY_USAGE_GPU_TO_CPU ||
               usage == VMA_MEMORY_USAGE_CPU_ONLY ||
               usage == VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    }

    VulkanBuffer::VulkanBuffer(VulkanDevice& device, size_t size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage)
        : m_Device(device)
    {
        m_SizeBytes = size;

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        // IMPORTANT: Do not implicitly add SHADER_DEVICE_ADDRESS.
        // Some buffers (e.g. indirect count buffers) must be created with specific usage bits;
        // adding unrelated bits can trigger validation requirements or feature dependencies.
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = memoryUsage;
        if (IsHostVisibleUsage(memoryUsage))
        {
            allocInfo.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
            if (memoryUsage == VMA_MEMORY_USAGE_CPU_TO_GPU || memoryUsage == VMA_MEMORY_USAGE_AUTO_PREFER_HOST)
            {
                allocInfo.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
            }
            else if (memoryUsage == VMA_MEMORY_USAGE_GPU_TO_CPU)
            {
                allocInfo.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
            }
        }

        VmaAllocationInfo resultInfo{};

        if (vmaCreateBuffer(device.GetAllocator(), &bufferInfo, &allocInfo, &m_Buffer, &m_Allocation, &resultInfo) !=
            VK_SUCCESS)
        {
            Core::Log::Error("Failed to create buffer (size={})", size);
            return;
        }

        // Cache memory properties so Map() can be a fast, safe check.
        VkMemoryPropertyFlags memFlags = 0;
        vmaGetAllocationMemoryProperties(device.GetAllocator(), m_Allocation, &memFlags);
        m_IsHostVisible = (memFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;

        if (m_IsHostVisible)
        {
            if (resultInfo.pMappedData != nullptr)
            {
                m_MappedData = resultInfo.pMappedData;
                m_IsMapped = true;
            }
            else
            {
                VkResult mapRes = vmaMapMemory(device.GetAllocator(), m_Allocation, &m_MappedData);
                if (mapRes != VK_SUCCESS)
                {
                    Core::Log::Error("Failed to persistently map host-visible buffer (VkResult={}).",
                                    static_cast<int>(mapRes));
                    m_MappedData = nullptr;
                    m_IsMapped = false;
                }
                else
                {
                    m_IsMapped = true;
                }
            }
        }
        else
        {
            m_MappedData = nullptr;
        }
    }

    VulkanBuffer::~VulkanBuffer()
    {
        if (m_IsMapped && m_Allocation)
        {
            vmaUnmapMemory(m_Device.GetAllocator(), m_Allocation);
            m_MappedData = nullptr;
            m_IsMapped = false;
        }
        DestructionUtils::SafeDestroyVma(m_Device, m_Buffer, m_Allocation, vmaDestroyBuffer);
    }

    VulkanBuffer::VulkanBuffer(VulkanBuffer&& other) noexcept
        : m_Device(other.m_Device)
          , m_Buffer(std::exchange(other.m_Buffer, VK_NULL_HANDLE))
          , m_Allocation(std::exchange(other.m_Allocation, VK_NULL_HANDLE))
          , m_MappedData(std::exchange(other.m_MappedData, nullptr))
          , m_SizeBytes(other.m_SizeBytes)
          , m_IsHostVisible(other.m_IsHostVisible)
          , m_IsMapped(std::exchange(other.m_IsMapped, false))
    {
    }

    VulkanBuffer& VulkanBuffer::operator=(VulkanBuffer&& other) noexcept
    {
        if (this != &other)
        {
            // Destroy current resources if any
            DestructionUtils::SafeDestroyVma(m_Device, m_Buffer, m_Allocation, vmaDestroyBuffer);

            // Move from other
            m_Buffer = std::exchange(other.m_Buffer, VK_NULL_HANDLE);
            m_Allocation = std::exchange(other.m_Allocation, VK_NULL_HANDLE);
            m_MappedData = std::exchange(other.m_MappedData, nullptr);
            m_SizeBytes = other.m_SizeBytes;
            m_IsHostVisible = other.m_IsHostVisible;
            m_IsMapped = std::exchange(other.m_IsMapped, false);
        }
        return *this;
    }

    void* VulkanBuffer::Map()
    {
        if (!m_Allocation) return nullptr;

        if (!m_IsHostVisible)
        {
            Core::Log::Error("VulkanBuffer::Map(): Attempted to map non-host-visible memory (device-local). "
                            "Use a staging upload path or create the buffer with CPU_TO_GPU/AUTO_PREFER_HOST.");
            return nullptr;
        }

        if (!m_IsMapped)
        {
            VkResult mapRes = vmaMapMemory(m_Device.GetAllocator(), m_Allocation, &m_MappedData);
            if (mapRes != VK_SUCCESS)
            {
                Core::Log::Error("VulkanBuffer::Map(): failed to map allocation (VkResult={}).", static_cast<int>(mapRes));
                m_MappedData = nullptr;
            }
            else
            {
                m_IsMapped = true;
            }
        }

        return m_MappedData;
    }

    void VulkanBuffer::Unmap()
    {
        if (m_IsMapped && m_Allocation)
        {
            vmaUnmapMemory(m_Device.GetAllocator(), m_Allocation);
            m_MappedData = nullptr;
            m_IsMapped = false;
        }
    }

    uint64_t VulkanBuffer::GetDeviceAddress() const
    {
        VkBufferDeviceAddressInfo info{};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        info.buffer = m_Buffer;
        return vkGetBufferDeviceAddress(m_Device.GetLogicalDevice(), &info);
    }

    BufferManager::Lease::Lease(BufferManager* manager, BufferHandle handle, bool adopt)
        : m_Manager(manager)
        , m_Handle(handle)
    {
        if (m_Manager && m_Handle.IsValid() && !adopt)
        {
            m_Manager->Retain(m_Handle);
        }
    }

    BufferManager::Lease::Lease(Lease&& other) noexcept
        : m_Manager(std::exchange(other.m_Manager, nullptr))
        , m_Handle(std::exchange(other.m_Handle, BufferHandle{}))
    {
    }

    BufferManager::Lease& BufferManager::Lease::operator=(Lease&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_Manager = std::exchange(other.m_Manager, nullptr);
            m_Handle = std::exchange(other.m_Handle, BufferHandle{});
        }
        return *this;
    }

    BufferManager::Lease::~Lease()
    {
        Reset();
    }

    VulkanBuffer* BufferManager::Lease::Get() const
    {
        return (m_Manager && m_Handle.IsValid()) ? m_Manager->GetIfValid(m_Handle) : nullptr;
    }

    BufferManager::Lease BufferManager::Lease::Share() const
    {
        return (m_Manager && m_Handle.IsValid()) ? m_Manager->AcquireLease(m_Handle) : Lease{};
    }

    void BufferManager::Lease::Reset()
    {
        if (m_Manager && m_Handle.IsValid())
        {
            m_Manager->Release(m_Handle);
        }

        m_Handle = {};
        m_Manager = nullptr;
    }

    BufferManager::BufferManager(VulkanDevice& device)
        : m_Device(device)
    {
    }

    BufferManager::~BufferManager()
    {
        Clear();
    }

    BufferHandle BufferManager::Create(size_t size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage)
    {
        auto buffer = std::make_unique<VulkanBuffer>(m_Device, size, usage, memoryUsage);
        if (!buffer || !buffer->IsValid())
            return {};

        std::lock_guard lock(m_Mutex);
        BufferRecord record{};
        record.Buffer = std::move(buffer);
        record.RefCount = 1u;
        return m_Pool.Add(std::move(record));
    }

    BufferManager::Lease BufferManager::CreateLease(size_t size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage)
    {
        const BufferHandle handle = Create(size, usage, memoryUsage);
        return handle.IsValid() ? Lease(this, handle, true) : Lease{};
    }

    BufferManager::Lease BufferManager::AcquireLease(BufferHandle handle)
    {
        Retain(handle);
        return handle.IsValid() ? Lease(this, handle, true) : Lease{};
    }

    VulkanBuffer* BufferManager::Get(BufferHandle handle) const
    {
        return GetIfValid(handle);
    }

    VulkanBuffer* BufferManager::GetIfValid(BufferHandle handle) const
    {
        std::lock_guard lock(m_Mutex);
        if (auto* record = m_Pool.GetIfValid(handle))
        {
            return record->Buffer.get();
        }
        return nullptr;
    }

    void BufferManager::Retain(BufferHandle handle)
    {
        if (!handle.IsValid())
            return;

        std::lock_guard lock(m_Mutex);
        if (auto* record = m_Pool.GetIfValid(handle))
        {
            ++record->RefCount;
        }
    }

    void BufferManager::Release(BufferHandle handle)
    {
        if (!handle.IsValid())
            return;

        std::lock_guard lock(m_Mutex);
        auto* record = m_Pool.GetIfValid(handle);
        if (!record || !record->Buffer)
            return;

        if (record->RefCount > 1u)
        {
            --record->RefCount;
            return;
        }

        m_Pool.Remove(handle, m_Device.GetGlobalFrameNumber());
    }

    void BufferManager::ProcessDeletions()
    {
        std::lock_guard lock(m_Mutex);
        m_Pool.ProcessDeletions(m_Device.GetGlobalFrameNumber());
    }

    void BufferManager::Clear()
    {
        std::lock_guard lock(m_Mutex);
        m_Pool.Clear();
    }

    void VulkanBuffer::Invalidate(size_t offset, size_t size)
    {
        if (!m_Allocation || !m_IsHostVisible) return;
        vmaInvalidateAllocation(m_Device.GetAllocator(), m_Allocation, static_cast<VkDeviceSize>(offset),
                                static_cast<VkDeviceSize>(size));
    }

    void VulkanBuffer::Flush(size_t offset, size_t size)
    {
        if (!m_Allocation || !m_IsHostVisible) return;
        vmaFlushAllocation(m_Device.GetAllocator(), m_Allocation, static_cast<VkDeviceSize>(offset),
                           static_cast<VkDeviceSize>(size));
    }
}
