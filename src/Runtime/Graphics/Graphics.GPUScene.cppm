module;

#include <cstdint>
#include <mutex>
#include <vector>
#include <memory>

#include <glm/glm.hpp>

#include "RHI.Vulkan.hpp"

export module Graphics:GPUScene;

import Core;
import RHI;

export namespace Graphics
{
    // Matches assets/shaders/instance_cull.comp (InstanceData) exactly.
    struct GpuInstanceData
    {
        glm::mat4 Model{1.0f};
        uint32_t TextureID = 0;
        uint32_t EntityID = 0;
        uint32_t Pad0 = 0;
        uint32_t Pad1 = 0;
    };

    // Matches assets/shaders/scene_update.comp (InstanceUpdate) closely.
    struct GpuUpdatePacket
    {
        uint32_t SlotIndex = 0;
        uint32_t _pad0 = 0;
        uint32_t _pad1 = 0;
        uint32_t _pad2 = 0;
        GpuInstanceData Data{};
        glm::vec4 SphereBounds{0.0f, 0.0f, 0.0f, 0.0f};
    };

    class GPUScene final
    {
    public:
        GPUScene(RHI::VulkanDevice& device,
                 RHI::ComputePipeline& updatePipeline,
                 VkDescriptorSetLayout updateSetLayout,
                 uint32_t maxInstances = 100'000);
        ~GPUScene();

        [[nodiscard]] uint32_t AllocateSlot();
        void FreeSlot(uint32_t slot);

        // Thread-safe: may be called from ECS update systems.
        void QueueUpdate(uint32_t slot, const GpuInstanceData& data, const glm::vec4& sphereBounds);

        // Record scatter compute into cmd. Caller is responsible for providing a valid command buffer.
        // This uploads pending updates to a transient SSBO and dispatches scene_update.comp.
        void Sync(VkCommandBuffer cmd);

        [[nodiscard]] uint32_t GetMaxInstances() const { return m_MaxInstances; }

        [[nodiscard]] RHI::VulkanBuffer& GetSceneBuffer() const { return *m_SceneBuffer; }
        [[nodiscard]] RHI::VulkanBuffer& GetBoundsBuffer() const { return *m_BoundsBuffer; }

        // Current live slot count estimate (maxAllocatedIndex+1). Used as the instance count for culling/dispatch.
        [[nodiscard]] uint32_t GetActiveCountApprox() const { return m_ActiveCountApprox; }

    private:
        void EnsurePersistentBuffers();

        RHI::VulkanDevice& m_Device;
        RHI::ComputePipeline& m_UpdatePipeline;
        VkDescriptorSetLayout m_UpdateSetLayout = VK_NULL_HANDLE;

        uint32_t m_MaxInstances = 0;

        std::unique_ptr<RHI::VulkanBuffer> m_SceneBuffer;
        std::unique_ptr<RHI::VulkanBuffer> m_BoundsBuffer;

        // Slot allocator (simple free list). Dense allocation + an approximate active count.
        std::vector<uint32_t> m_FreeSlots;
        uint32_t m_NextSlot = 0;
        uint32_t m_ActiveCountApprox = 0;
        std::mutex m_AllocMutex;

        std::vector<GpuUpdatePacket> m_PendingUpdates;
        std::mutex m_UpdateMutex;

        // Per-sync transient resources.
        std::unique_ptr<RHI::VulkanBuffer> m_UpdatesStaging;
        size_t m_UpdatesStagingCapacity = 0;

        std::unique_ptr<RHI::PersistentDescriptorPool> m_UpdateSetPool;
    };
}
