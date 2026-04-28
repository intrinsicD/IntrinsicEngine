module;
#include <cstdint>
#include "RHI.Vulkan.hpp"

export module RHI.QueueDomain;

import RHI.Device;

export namespace RHI
{
    // =========================================================================
    // Queue Domain Abstraction (B2)
    // =========================================================================
    // Conceptual queue domains for GPU work submission. The initial
    // implementation maps each domain to the physical queue discovered by
    // VulkanDevice, but the abstraction enables future multi-queue strategies
    // (e.g. async compute, dedicated transfer) without changing callers.
    //
    // The mapping is:
    //   Graphics → VulkanDevice::GetGraphicsQueue()
    //   Compute  → VulkanDevice::GetGraphicsQueue()  (same queue until async compute)
    //   Transfer → VulkanDevice::GetTransferQueue()   (may be dedicated or graphics fallback)
    //
    // Presentation is NOT modeled as a QueueDomain. Present is always a
    // swapchain operation (vkQueuePresentKHR) routed through
    // VulkanDevice::Present(), which is a fundamentally different operation
    // from compute/render submission (VkSubmitInfo). The present queue is
    // often the same physical queue as graphics but uses a different Vulkan
    // entry point and synchronization model (binary semaphores only).

    enum class QueueDomain : uint8_t
    {
        Graphics = 0,
        Compute  = 1,
        Transfer = 2,
    };

    // Human-readable domain name (useful for logging and diagnostics).
    [[nodiscard]] constexpr const char* QueueDomainName(QueueDomain domain)
    {
        switch (domain)
        {
            case QueueDomain::Graphics: return "Graphics";
            case QueueDomain::Compute:  return "Compute";
            case QueueDomain::Transfer: return "Transfer";
        }
        return "Unknown";
    }

    // =========================================================================
    // QueueSubmitter — unified domain-aware submission
    // =========================================================================
    // Non-owning wrapper around VulkanDevice that routes queue submissions
    // by domain. Provides domain-level queries for queue family indices and
    // ownership transfer decisions.
    //
    // Lifetime: must not outlive the VulkanDevice it references.
    //
    // Thread safety: Submit() acquires VulkanDevice::GetQueueMutex() to
    // serialize all queue submissions. This is correct but conservative —
    // submissions to distinct physical queues (e.g. dedicated transfer)
    // could theoretically proceed concurrently. The single-mutex approach
    // matches the existing VulkanDevice contract and avoids lock-ordering
    // complexity.
    //
    // Lock ordering contract: device queue mutex is always the outermost
    // lock. Subsystem-internal mutexes (e.g. TransferManager::m_Mutex)
    // must be acquired after the queue mutex, never before.
    //
    // Future extension: VkSubmitInfo2 / vkQueueSubmit2 (Vulkan 1.3) enables
    // per-command-buffer stage masks. A Submit2() overload can be added here
    // when the engine migrates to VkSubmitInfo2.

    class QueueSubmitter
    {
    public:
        explicit QueueSubmitter(VulkanDevice& device);

        // Non-copyable, non-movable (reference member, no ownership semantics).
        QueueSubmitter(const QueueSubmitter&) = delete;
        QueueSubmitter& operator=(const QueueSubmitter&) = delete;
        QueueSubmitter(QueueSubmitter&&) = delete;
        QueueSubmitter& operator=(QueueSubmitter&&) = delete;

        // Submit work to a specific domain's queue.
        // Returns VK_SUCCESS on success, or a Vulkan error code.
        // The caller is responsible for correct synchronization (fences, semaphores).
        [[nodiscard]] VkResult Submit(QueueDomain domain,
                                      const VkSubmitInfo& submitInfo,
                                      VkFence fence = VK_NULL_HANDLE);

        // Resolve the VkQueue handle for a domain.
        [[nodiscard]] VkQueue GetQueue(QueueDomain domain) const;

        // Resolve the queue family index for a domain.
        [[nodiscard]] uint32_t GetQueueFamilyIndex(QueueDomain domain) const;

        // Check whether two domains map to different queue families.
        // When true, resources shared between these domains require explicit
        // queue family ownership transfers (VkBufferMemoryBarrier / VkImageMemoryBarrier
        // with srcQueueFamilyIndex / dstQueueFamilyIndex).
        [[nodiscard]] bool RequiresOwnershipTransfer(QueueDomain from, QueueDomain to) const;

        // Convenience: check whether a domain has a dedicated (non-graphics) queue family.
        [[nodiscard]] bool HasDedicatedQueue(QueueDomain domain) const;

        // Access the underlying device (e.g. for timeline semaphore operations).
        [[nodiscard]] VulkanDevice& GetDevice() { return m_Device; }
        [[nodiscard]] const VulkanDevice& GetDevice() const { return m_Device; }

    private:
        VulkanDevice& m_Device;
    };
}
