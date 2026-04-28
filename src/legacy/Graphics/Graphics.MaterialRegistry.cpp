// src/Runtime/Graphics/Graphics.MaterialRegistry.cpp
module;
#include <cstring>
#include <memory>
#include <mutex>
#include <string_view>

#include "RHI.Vulkan.hpp"

module Graphics.Material;

import Asset.Manager;
import Core.Logging;
import RHI.Buffer;
import RHI.Device;
import RHI.Texture;
import RHI.TextureManager;

namespace Graphics
{
    // Initial capacity of the GPU material SSBO (number of material slots).
    static constexpr uint32_t kInitialMaterialCapacity = 256;

    // Minimum SSBO size in bytes (Vulkan requires non-zero buffer bindings).
    static constexpr VkDeviceSize kMinSSBOSize = 4;

    MaterialRegistry::MaterialRegistry(RHI::VulkanDevice& device,
                                       RHI::TextureManager& textureManager,
                                       Core::Assets::AssetManager& assetManager)
        : m_Device(device), m_TextureManager(textureManager), m_AssetManager(assetManager)
    {
        m_Revisions.resize(1024u, 1u);
        InitGpuResources();
    }

    MaterialRegistry::~MaterialRegistry()
    {
        std::lock_guard lock(m_ListenerMutex);
        for(auto& [mat, listeners] : m_Listeners) {
            for(auto& entry : listeners) {
                m_AssetManager.Unlisten(entry.Asset, entry.CallbackID);
            }
        }
        m_Pool.Clear();

        VkDevice logicalDevice = m_Device.GetLogicalDevice();
        if (logicalDevice != VK_NULL_HANDLE)
        {
            if (m_MaterialDescriptorPool != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorPool(logicalDevice, m_MaterialDescriptorPool, nullptr);
                m_MaterialDescriptorPool = VK_NULL_HANDLE;
                m_MaterialDescriptorSet = VK_NULL_HANDLE;
            }
            if (m_MaterialSetLayout != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorSetLayout(logicalDevice, m_MaterialSetLayout, nullptr);
                m_MaterialSetLayout = VK_NULL_HANDLE;
            }
        }
    }

    void MaterialRegistry::InitGpuResources()
    {
        VkDevice logicalDevice = m_Device.GetLogicalDevice();

        // 1. Create descriptor set layout: one SSBO binding, readable by vertex + fragment.
        {
            VkDescriptorSetLayoutBinding binding{};
            binding.binding = 0;
            binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            binding.descriptorCount = 1;
            binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

            VkDescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            layoutInfo.bindingCount = 1;
            layoutInfo.pBindings = &binding;

            VK_CHECK_FATAL(vkCreateDescriptorSetLayout(logicalDevice, &layoutInfo, nullptr,
                                                        &m_MaterialSetLayout));
        }

        // 2. Create a small descriptor pool (we need exactly one set).
        {
            VkDescriptorPoolSize poolSize{};
            poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            poolSize.descriptorCount = 1;

            VkDescriptorPoolCreateInfo poolInfo{};
            poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
            poolInfo.maxSets = 1;
            poolInfo.poolSizeCount = 1;
            poolInfo.pPoolSizes = &poolSize;

            VK_CHECK_FATAL(vkCreateDescriptorPool(logicalDevice, &poolInfo, nullptr,
                                                   &m_MaterialDescriptorPool));
        }

        // 3. Allocate the single descriptor set.
        {
            VkDescriptorSetAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocInfo.descriptorPool = m_MaterialDescriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &m_MaterialSetLayout;

            VK_CHECK_FATAL(vkAllocateDescriptorSets(logicalDevice, &allocInfo,
                                                     &m_MaterialDescriptorSet));
        }

        // 4. Create the initial SSBO and bind it to the descriptor set.
        EnsureGpuBufferCapacity(kInitialMaterialCapacity);
    }

    void MaterialRegistry::EnsureGpuBufferCapacity(uint32_t requiredSlots)
    {
        if (requiredSlots <= m_GpuBufferCapacity && m_GpuBuffer)
            return;

        // Grow to next power of two or double current capacity.
        uint32_t newCapacity = m_GpuBufferCapacity > 0 ? m_GpuBufferCapacity : kInitialMaterialCapacity;
        while (newCapacity < requiredSlots)
            newCapacity *= 2;

        VkDeviceSize bufferSize = static_cast<VkDeviceSize>(newCapacity) * sizeof(GpuMaterialData);
        if (bufferSize < kMinSSBOSize)
            bufferSize = kMinSSBOSize;

        m_GpuBuffer = std::make_unique<RHI::VulkanBuffer>(
            m_Device,
            bufferSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            static_cast<VmaMemoryUsage>(VMA_MEMORY_USAGE_CPU_TO_GPU));

        m_GpuBufferCapacity = newCapacity;
        m_GpuDirty = true;

        UpdateDescriptorSet();

        Core::Log::Debug("[MaterialRegistry] GPU buffer resized to {} slots ({} bytes)",
                         newCapacity, bufferSize);
    }

    void MaterialRegistry::UpdateDescriptorSet()
    {
        if (!m_GpuBuffer || m_MaterialDescriptorSet == VK_NULL_HANDLE)
            return;

        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = m_GpuBuffer->GetHandle();
        bufferInfo.offset = 0;
        bufferInfo.range = VK_WHOLE_SIZE;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = m_MaterialDescriptorSet;
        write.dstBinding = 0;
        write.dstArrayElement = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.descriptorCount = 1;
        write.pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(m_Device.GetLogicalDevice(), 1, &write, 0, nullptr);
    }

    void MaterialRegistry::SyncGpuBuffer()
    {
        if (!m_GpuDirty || !m_GpuBuffer)
            return;

        // Determine how many slots the pool currently holds.
        const uint32_t poolCapacity = m_Pool.Capacity();
        if (poolCapacity > m_GpuBufferCapacity)
            EnsureGpuBufferCapacity(poolCapacity);

        const uint32_t slotsToWrite = std::min(poolCapacity, m_GpuBufferCapacity);
        if (slotsToWrite == 0)
        {
            m_GpuDirty = false;
            return;
        }

        // Map the GPU buffer and write all material data.
        void* mapped = m_GpuBuffer->Map();
        if (!mapped)
        {
            Core::Log::Warn("[MaterialRegistry] Failed to map GPU material buffer");
            return;
        }

        auto* dst = static_cast<GpuMaterialData*>(mapped);
        for (uint32_t i = 0; i < slotsToWrite; ++i)
        {
            const MaterialData* src = m_Pool.GetByIndex(i);
            if (src)
            {
                dst[i].BaseColorFactor = src->BaseColorFactor;
                dst[i].MetallicFactor = src->MetallicFactor;
                dst[i].RoughnessFactor = src->RoughnessFactor;
                dst[i].AlbedoID = src->AlbedoID;
                dst[i].NormalID = src->NormalID;
                dst[i].MetallicRoughnessID = src->MetallicRoughnessID;
                dst[i].Flags = 0;
                dst[i]._pad0 = 0;
                dst[i]._pad1 = 0;
            }
            else
            {
                // Inactive slot: write safe defaults (white base color, default texture).
                dst[i] = GpuMaterialData{};
            }
        }

        m_GpuBuffer->Unmap();
        m_GpuDirty = false;
    }

    uint32_t MaterialRegistry::GetRevision(MaterialHandle handle) const
    {
        if (!handle.IsValid())
            return 0u;
        if (handle.Index >= m_Revisions.size())
            return 0u;
        return m_Revisions[handle.Index];
    }

    MaterialHandle MaterialRegistry::Create(const MaterialData& data)
    {
        MaterialHandle h = m_Pool.Create(data);
        if (h.IsValid())
        {
            if (h.Index >= m_Revisions.size())
                m_Revisions.resize(static_cast<size_t>(h.Index) + 1u, 1u);
            ++m_Revisions[h.Index];
            m_GpuDirty = true;
        }
        return h;
    }

    void MaterialRegistry::Destroy(MaterialHandle handle)
    {
        if(!handle.IsValid()) return;

        // Bump revision so any cached per-entity state will refresh if the handle is (incorrectly) reused.
        if (handle.Index < m_Revisions.size())
            ++m_Revisions[handle.Index];

        {
            std::lock_guard lock(m_ListenerMutex);
            auto it = m_Listeners.find(handle);
            if(it != m_Listeners.end()) {
                for(auto& entry : it->second) {
                    m_AssetManager.Unlisten(entry.Asset, entry.CallbackID);
                }
                m_Listeners.erase(it);
            }
        }

        // Mark for deletion.
        // NOTE: We assume Engine calls ProcessDeletions with valid frame index.
        // If called from ~Material(), we might not know the exact frame index easily
        // without passing it in. Using 0 implies "delete when safe" if logic allows,
        // but ideally we pass m_Device.GetGlobalFrameNumber().
        // For now, we assume 0 is handled or we update signature later.
        m_Pool.Remove(handle, 0);
    }

    void MaterialRegistry::ProcessDeletions(uint64_t currentFrame)
    {
        m_Pool.ProcessDeletions(currentFrame);
    }

    const MaterialData* MaterialRegistry::GetData(MaterialHandle handle) const
    {
        auto res = m_Pool.Get(handle);
        return res ? *res : nullptr;
    }

    MaterialData* MaterialRegistry::GetData(MaterialHandle handle)
    {
        auto res = m_Pool.Get(handle);
        return res ? *res : nullptr;
    }

    void MaterialRegistry::SetAlbedoAsset(MaterialHandle material, Core::Assets::AssetHandle textureAsset)
    {
        BindTextureAsset(material, textureAsset, TextureSlot::Albedo);
    }

    void MaterialRegistry::SetNormalAsset(MaterialHandle material, Core::Assets::AssetHandle textureAsset)
    {
        BindTextureAsset(material, textureAsset, TextureSlot::Normal);
    }

    void MaterialRegistry::SetMetallicRoughnessAsset(MaterialHandle material, Core::Assets::AssetHandle textureAsset)
    {
        BindTextureAsset(material, textureAsset, TextureSlot::MetallicRoughness);
    }

    void MaterialRegistry::BindTextureAsset(MaterialHandle material, Core::Assets::AssetHandle textureAsset, TextureSlot slot)
    {
        std::lock_guard lock(m_ListenerMutex);

        auto callback = [this, material, slot](Core::Assets::AssetHandle texHandle) {
            this->OnTextureLoad(material, texHandle, slot);
        };

        const auto listenerID = m_AssetManager.Listen(textureAsset, callback);
        m_Listeners[material].push_back({textureAsset, listenerID});
    }

    void MaterialRegistry::OnTextureLoad(MaterialHandle matHandle, Core::Assets::AssetHandle texHandle, TextureSlot slot)
    {
        // 1. Get the RHI Texture (Asset Payload)
        auto* tex = m_AssetManager.TryGet<RHI::Texture>(texHandle);
        if (!tex) return;

        // 2. Get the Bindless Index
        uint32_t bindlessID = tex->GetBindlessIndex();

        // DEBUG: trace material->texture binding updates.
        constexpr auto slotToString = [](TextureSlot textureSlot) -> std::string_view {
            switch (textureSlot)
            {
                case TextureSlot::Albedo: return "Albedo";
                case TextureSlot::Normal: return "Normal";
                case TextureSlot::MetallicRoughness: return "MetallicRoughness";
            }
            return "Unknown";
        };

        Core::Log::Info("[MaterialRegistry] OnTextureLoad: mat(index={}, gen={}) texAsset(id={}) -> bindlessSlot={} slot={}",
                        matHandle.Index, matHandle.Generation,
                        static_cast<uint32_t>(texHandle.ID),
                        bindlessID, slotToString(slot));

        // 3. Update Material Data in Pool
        if (auto* data = GetData(matHandle))
        {
            switch (slot)
            {
                case TextureSlot::Albedo: data->AlbedoID = bindlessID; break;
                case TextureSlot::Normal: data->NormalID = bindlessID; break;
                case TextureSlot::MetallicRoughness: data->MetallicRoughnessID = bindlessID; break;
            }

            if (matHandle.Index >= m_Revisions.size())
                m_Revisions.resize(static_cast<size_t>(matHandle.Index) + 1u, 1u);
            ++m_Revisions[matHandle.Index];
        }
    }
}