module;
#include <mutex>
#include "RHI.Vulkan.hpp"

module RHI.QueueDomain;

import RHI.Device;
import Core.Logging;

namespace RHI
{
    QueueSubmitter::QueueSubmitter(VulkanDevice& device)
        : m_Device(device)
    {
    }

    VkResult QueueSubmitter::Submit(QueueDomain domain,
                                    const VkSubmitInfo& submitInfo,
                                    VkFence fence)
    {
        VkQueue queue = GetQueue(domain);
        if (queue == VK_NULL_HANDLE)
        {
            Core::Log::Error("QueueSubmitter::Submit: no queue available for domain {}.",
                             QueueDomainName(domain));
            return VK_ERROR_INITIALIZATION_FAILED;
        }

        // All queue submissions go through the device queue mutex to prevent
        // concurrent vkQueueSubmit calls across domains that share the same
        // physical queue.
        std::scoped_lock lock(m_Device.GetQueueMutex());
        return vkQueueSubmit(queue, 1, &submitInfo, fence);
    }

    VkQueue QueueSubmitter::GetQueue(QueueDomain domain) const
    {
        switch (domain)
        {
            case QueueDomain::Graphics:
                return m_Device.GetGraphicsQueue();
            case QueueDomain::Compute:
                // Compute currently maps to the graphics queue.
                // When async compute is supported, this will resolve to a
                // dedicated compute queue if available.
                return m_Device.GetGraphicsQueue();
            case QueueDomain::Transfer:
                return m_Device.GetTransferQueue();
        }
        return VK_NULL_HANDLE;
    }

    uint32_t QueueSubmitter::GetQueueFamilyIndex(QueueDomain domain) const
    {
        const auto indices = m_Device.GetQueueIndices();
        switch (domain)
        {
            case QueueDomain::Graphics:
                return indices.Graphics();
            case QueueDomain::Compute:
                // Compute shares the graphics family until async compute is added.
                return indices.Graphics();
            case QueueDomain::Transfer:
                return indices.Transfer();
        }
        return indices.Graphics();
    }

    bool QueueSubmitter::RequiresOwnershipTransfer(QueueDomain from, QueueDomain to) const
    {
        if (from == to)
            return false;
        return GetQueueFamilyIndex(from) != GetQueueFamilyIndex(to);
    }

    bool QueueSubmitter::HasDedicatedQueue(QueueDomain domain) const
    {
        switch (domain)
        {
            case QueueDomain::Graphics:
                return true; // Graphics always has its own queue.
            case QueueDomain::Compute:
                return false; // Compute is not yet dedicated.
            case QueueDomain::Transfer:
                return m_Device.GetQueueIndices().HasDistinctTransfer();
        }
        return false;
    }
}
