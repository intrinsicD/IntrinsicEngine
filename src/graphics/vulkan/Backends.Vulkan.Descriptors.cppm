module;

#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

#include "Vulkan.hpp"

export module Extrinsic.Backends.Vulkan:Descriptors;

export import Extrinsic.RHI.Bindless;
export import Extrinsic.RHI.Handles;

namespace Extrinsic::Backends::Vulkan
{
    export constexpr uint32_t kBindlessCapacity = 65536;
    // Descriptor elements 0..2 are temporarily reserved for framegraph-sampled
    // textures updated directly by VulkanCommandContext:
    //   0 = default sampled input, 1 = DebugView, 2 = Present.
    // Real bindless texture leases must start after those bridge slots.
    constexpr RHI::BindlessIndex kFirstTextureBindlessSlot = 3;

    export class VulkanDevice;

    export class VulkanBindlessHeap final : public RHI::IBindlessHeap
    {
    public:
        explicit VulkanBindlessHeap(VkDevice device, uint32_t capacity = kBindlessCapacity);
        ~VulkanBindlessHeap() override;

        [[nodiscard]] bool IsValid() const noexcept;
        void SetDefault(VkImageView view, VkSampler sampler);
        void SetTextureSamplerResolver(std::function<bool(RHI::TextureHandle,
                                                          RHI::SamplerHandle,
                                                          VkImageView&,
                                                          VkSampler&)> resolver);

        [[nodiscard]] VkDescriptorSetLayout GetLayout() const { return m_Layout; }
        [[nodiscard]] VkDescriptorSet       GetSet()    const { return m_Set;    }

        [[nodiscard]] RHI::BindlessIndex AllocateTextureSlot(RHI::TextureHandle, RHI::SamplerHandle) override;
        void UpdateTextureSlot(RHI::BindlessIndex, RHI::TextureHandle, RHI::SamplerHandle) override;
        void FreeSlot(RHI::BindlessIndex) override;
        void FlushPending() override;
        [[nodiscard]] uint32_t GetCapacity()           const override { return m_Capacity; }
        [[nodiscard]] uint32_t GetAllocatedSlotCount() const override;

    private:
        friend class VulkanDevice;
        void EnqueueRawUpdate(RHI::BindlessIndex slot, VkImageView view, VkSampler sampler,
                              VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        enum class OpType : uint8_t { Allocate, UpdateRaw, Free };
        struct PendingOp
        {
            OpType          Type{};
            RHI::BindlessIndex Slot{};
            VkImageView     View    = VK_NULL_HANDLE;
            VkSampler       Sampler = VK_NULL_HANDLE;
            VkImageLayout   Layout  = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        };

        VkDevice              m_Device   = VK_NULL_HANDLE;
        VkDescriptorPool      m_Pool     = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_Layout   = VK_NULL_HANDLE;
        VkDescriptorSet       m_Set      = VK_NULL_HANDLE;
        VkImageView           m_DefaultView = VK_NULL_HANDLE;
        VkSampler             m_DefaultSampler = VK_NULL_HANDLE;
        uint32_t              m_Capacity = 0;
        uint32_t              m_NextSlot = kFirstTextureBindlessSlot;
        uint32_t              m_AllocCount = 0;
        std::vector<RHI::BindlessIndex> m_FreeSlots;
        std::vector<PendingOp>          m_Pending;
        std::function<bool(RHI::TextureHandle, RHI::SamplerHandle, VkImageView&, VkSampler&)> m_ResolveTextureSampler;
        mutable std::mutex              m_Mutex;
    };
}
