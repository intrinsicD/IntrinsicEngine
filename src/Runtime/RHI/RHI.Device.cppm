module;
#include <vector>
#include <optional>
#include <mutex>
#include <functional>
#include <memory>
#include <atomic>
#include "RHI.Vulkan.hpp"

export module RHI:Device;

import :Context;

namespace RHI
{
    export struct QueueFamilyIndices
    {
        std::optional<uint32_t> GraphicsFamily;
        std::optional<uint32_t> PresentFamily;
        std::optional<uint32_t> TransferFamily;

        [[nodiscard]] bool IsComplete() const
        {
            return GraphicsFamily.has_value() && PresentFamily.has_value() && TransferFamily.has_value();
        }
    };

    struct SwapchainSupportDetails
    {
        VkSurfaceCapabilitiesKHR Capabilities;
        std::vector<VkSurfaceFormatKHR> Formats;
        std::vector<VkPresentModeKHR> PresentModes;
    };

    export class VulkanDevice
    {
    public:
        VulkanDevice(VulkanContext& context, VkSurfaceKHR surface);
        ~VulkanDevice();

        // No copy
        VulkanDevice(const VulkanDevice&) = delete;
        VulkanDevice& operator=(const VulkanDevice&) = delete;

        [[nodiscard]] VkDevice GetLogicalDevice() const { return m_Device; }
        [[nodiscard]] VkPhysicalDevice GetPhysicalDevice() const { return m_PhysicalDevice; }
        [[nodiscard]] VkQueue GetGraphicsQueue() const { return m_GraphicsQueue; }
        [[nodiscard]] VkQueue GetPresentQueue() const { return m_PresentQueue; }
        [[nodiscard]] VkQueue GetTransferQueue() const { return m_TransferQueue; }
        [[nodiscard]] QueueFamilyIndices GetQueueIndices() const { return m_Indices; }
        [[nodiscard]] VkResult SubmitToGraphicsQueue(const VkSubmitInfo& submitInfo, VkFence fence);
        [[nodiscard]] VkResult Present(const VkPresentInfoKHR& presentInfo);
        [[nodiscard]] VkSurfaceKHR GetSurface() const { return m_Surface; }
        [[nodiscard]] VkCommandPool GetCommandPool() const { return m_CommandPool; }
        [[nodiscard]] VmaAllocator GetAllocator() const { return m_Allocator; }
        [[nodiscard]] bool IsValid() const { return m_IsValid; }
        [[nodiscard]] static constexpr uint32_t GetFramesInFlight() { return MAX_FRAMES_IN_FLIGHT; }
        [[nodiscard]] constexpr uint32_t GetCurrentFrameIndex() const { return m_CurrentFrameIndex; }
        [[nodiscard]] uint64_t GetGlobalFrameNumber() const { return m_GlobalFrameNumber; }

        // Called once per frame after the CPU has started building a new frame.
        // This is a monotonic counter (never wraps) and is safe for lifetime tracking.
        void IncrementGlobalFrame() {
            ++m_GlobalFrameNumber;
        }

        // Advances the in-flight frame slot (wraps by MAX_FRAMES_IN_FLIGHT).
        // Only the renderer should do this, and only when clearly synchronized with per-frame fences.
        void AdvanceFrameIndex() {
            m_CurrentFrameIndex = (m_CurrentFrameIndex + 1) % MAX_FRAMES_IN_FLIGHT;
        }

        // Backward-compat: previously did both (global++ and index++). Prefer the split APIs.
        [[deprecated("Use IncrementGlobalFrame() and/or AdvanceFrameIndex()")]]
        void IncrementFrame() {
            IncrementGlobalFrame();
            AdvanceFrameIndex();
        }

        // Helper to query swapchain support (used later by Swapchain module)
        [[nodiscard]] SwapchainSupportDetails QuerySwapchainSupport() const;

        [[nodiscard]] std::mutex& GetQueueMutex() { return m_QueueMutex; }

        void RegisterThreadLocalPool(VkCommandPool pool);

        void FlushDeletionQueue(uint32_t frameIndex);

        // New: execute and clear *all* deferred deletions immediately.
        // Safe to call only when the GPU is idle.
        void FlushAllDeletionQueues();

        // Drain *timeline-based* deferred deletions immediately.
        // Contract: only call when the GPU is idle (e.g., after vkDeviceWaitIdle).
        void FlushTimelineDeletionQueueNow();

        // ---------------------------------------------------------------------
        // GPU-completion based deferred destruction (timeline).
        // ---------------------------------------------------------------------
        // Returns the last value signaled on the graphics timeline.
        // Thread-safe: may be called from any thread (e.g. asset loaders calling SafeDestroy).
        [[nodiscard]] uint64_t GetGraphicsTimelineValue() const
        {
            return m_GraphicsTimelineValue.load(std::memory_order_acquire);
        }

        // Signal the graphics timeline and return the signaled value.
        // Call from the renderer exactly once per graphics-queue submit.
        [[nodiscard]] uint64_t SignalGraphicsTimeline();

        // Query GPU-completed value for the graphics timeline.
        [[nodiscard]] uint64_t GetGraphicsTimelineCompletedValue() const;

        // Execute and erase any deferred deletions whose timeline value has completed.
        void CollectGarbage();

        // Like SafeDestroy(), but deferred until the graphics timeline reaches 'value'.
        void SafeDestroyAfter(uint64_t value, std::function<void()>&& deleteFn);

        // Existing API: keep name, but implement in terms of timeline.
        void SafeDestroy(std::function<void()>&& deleteFn);

        // Graphics timeline semaphore handle used for vkQueueSubmit signaling.
        [[nodiscard]] VkSemaphore GetGraphicsTimelineSemaphore() const { return m_GraphicsTimelineSemaphore; }

    private:
        VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
        VkDevice m_Device = VK_NULL_HANDLE;

        VkSurfaceKHR m_Surface = VK_NULL_HANDLE; // Weak Ref (Owned by Context/Window logic usually, but we hold handle)

        VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
        VkQueue m_PresentQueue = VK_NULL_HANDLE;
        VkQueue m_TransferQueue = VK_NULL_HANDLE;
        QueueFamilyIndices m_Indices;

        VmaAllocator m_Allocator = VK_NULL_HANDLE;
        VkCommandPool m_CommandPool = VK_NULL_HANDLE;

        mutable std::mutex m_QueueMutex;
        std::mutex m_ThreadPoolsMutex;
        std::vector<VkCommandPool> m_ThreadCommandPools;

        bool m_IsValid = true;

        // NOTE: 3 frames-in-flight gives the deferred deletion queues enough slack for transient
        // RenderGraph resources that are recreated every frame.
        static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 3;
        std::vector<std::function<void()>> m_DeletionQueue[MAX_FRAMES_IN_FLIGHT];
        uint32_t m_CurrentFrameIndex = 0;
        uint64_t m_GlobalFrameNumber = 0; // Monotonically increasing frame counter
        std::mutex m_DeletionMutex;

        // The last frame-slot whose fence has been waited and whose deletion queue was flushed.
        // SafeDestroy schedules into (m_CurrentSafeDestroyFrame + 1) % MAX_FRAMES_IN_FLIGHT.
        uint32_t m_CurrentSafeDestroyFrame = 0;

        void PickPhysicalDevice(VkInstance instance);
        void CreateLogicalDevice(VulkanContext & context);
        void CreateCommandPool();

        bool IsDeviceSuitable(VkPhysicalDevice device);
        QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device);
        bool CheckDeviceExtensionSupport(VkPhysicalDevice device);

        // Opaque pointer to device-owned transient allocator (defined + managed in RHI.Device.cpp).
        void* m_TransientAllocator = nullptr;

        // Timeline semaphore to track completion on the graphics queue.
        VkSemaphore m_GraphicsTimelineSemaphore = VK_NULL_HANDLE;
        std::atomic<uint64_t> m_GraphicsTimelineNextValue{1};
        std::atomic<uint64_t> m_GraphicsTimelineValue{0};

        struct DeferredDelete
        {
            uint64_t Value = 0;
            std::function<void()> Fn;
        };

        std::vector<DeferredDelete> m_TimelineDeletionQueue;
    };
}
