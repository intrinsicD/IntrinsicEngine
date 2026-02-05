module;

#include <algorithm>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "RHI.Vulkan.hpp"

module Graphics:GPUScene.Impl;

import :GPUScene;

import Core;
import RHI;

namespace Graphics
{
    namespace
    {
        constexpr uint32_t kPreserveGeometryId = 0xFFFFFFFFu;

        void MergeUpdate(GpuUpdatePacket& dst, const GpuUpdatePacket& src)
        {
            const bool dstDeactivate = (dst.SphereBounds.w == 0.0f);
            const bool srcDeactivate = (src.SphereBounds.w == 0.0f);
            const bool srcPreserve = (src.SphereBounds.w < 0.0f);

            if (srcDeactivate && dst.SphereBounds.w > 0.0f)
                return;
            if (dstDeactivate && srcPreserve)
                return;

            dst.Data.Model = src.Data.Model;
            dst.Data.TextureID = src.Data.TextureID;
            dst.Data.EntityID = src.Data.EntityID;

            if (src.Data.GeometryID != kPreserveGeometryId)
                dst.Data.GeometryID = src.Data.GeometryID;

            if (src.SphereBounds.w >= 0.0f)
                dst.SphereBounds = src.SphereBounds;
        }
    }

    GPUScene::GPUScene(RHI::VulkanDevice& device,
                       RHI::ComputePipeline& updatePipeline,
                       VkDescriptorSetLayout updateSetLayout,
                       uint32_t maxInstances)
        : m_Device(device)
        , m_UpdatePipeline(updatePipeline)
        , m_UpdateSetLayout(updateSetLayout)
        , m_MaxInstances(maxInstances)
    {
        EnsurePersistentBuffers();
        m_PendingUpdateIndexBySlot.assign(m_MaxInstances, -1);
        m_GeometryIdShadow.assign(m_MaxInstances, kPreserveGeometryId);

        // Enough sets for a few frames; update sets are allocated per Sync() call.
        m_UpdateSetPool = std::make_unique<RHI::PersistentDescriptorPool>(m_Device,
                                                                          /*maxSets*/ 64,
                                                                          /*storageBufferCount*/ 64 * 3,
                                                                          /*debugName*/ "GPUScene.SceneUpdate");
    }

    GPUScene::~GPUScene() = default;

    void GPUScene::EnsurePersistentBuffers()
    {
        if (m_SceneBuffer && m_BoundsBuffer)
            return;

        const VkDeviceSize sceneBytes = VkDeviceSize(m_MaxInstances) * sizeof(GpuInstanceData);
        const VkDeviceSize boundsBytes = VkDeviceSize(m_MaxInstances) * sizeof(glm::vec4);

        m_SceneBuffer = std::make_unique<RHI::VulkanBuffer>(
            m_Device,
            std::max<VkDeviceSize>(sceneBytes, 4),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY);

        m_BoundsBuffer = std::make_unique<RHI::VulkanBuffer>(
            m_Device,
            std::max<VkDeviceSize>(boundsBytes, 4),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY);
    }

    uint32_t GPUScene::AllocateSlot()
    {
        std::lock_guard lock(m_AllocMutex);

        uint32_t slot = 0;
        if (!m_FreeSlots.empty())
        {
            slot = m_FreeSlots.back();
            m_FreeSlots.pop_back();
        }
        else
        {
            if (m_NextSlot >= m_MaxInstances)
            {
                Core::Log::Error("GPUScene: Out of slots (maxInstances = {})", m_MaxInstances);
                return ~0u;
            }
            slot = m_NextSlot++;
        }

        m_ActiveCountApprox = std::max(m_ActiveCountApprox, slot + 1);
        return slot;
    }

    void GPUScene::FreeSlot(uint32_t slot)
    {
        std::lock_guard lock(m_AllocMutex);
        if (slot == ~0u || slot >= m_MaxInstances)
            return;
        m_FreeSlots.push_back(slot);
        if (slot < m_GeometryIdShadow.size())
            m_GeometryIdShadow[slot] = kPreserveGeometryId;
    }

    void GPUScene::QueueUpdate(uint32_t slot, const GpuInstanceData& data, const glm::vec4& sphereBounds)
    {
        if (slot == ~0u || slot >= m_MaxInstances)
            return;

        std::lock_guard lock(m_UpdateMutex);

        if (m_PendingUpdateIndexBySlot.empty())
            m_PendingUpdateIndexBySlot.assign(m_MaxInstances, -1);
        if (m_GeometryIdShadow.empty())
            m_GeometryIdShadow.assign(m_MaxInstances, kPreserveGeometryId);

        GpuUpdatePacket p{};
        p.SlotIndex = slot;
        p.Data = data;
        p.SphereBounds = sphereBounds;

        if (p.Data.GeometryID == kPreserveGeometryId)
        {
            const uint32_t shadowId = m_GeometryIdShadow[slot];
            if (shadowId != kPreserveGeometryId)
                p.Data.GeometryID = shadowId;
        }
        else
        {
            m_GeometryIdShadow[slot] = p.Data.GeometryID;
        }

        const int32_t existingIndex = m_PendingUpdateIndexBySlot[slot];
        if (existingIndex >= 0)
        {
            MergeUpdate(m_PendingUpdates[static_cast<size_t>(existingIndex)], p);
            return;
        }

        m_PendingUpdateIndexBySlot[slot] = static_cast<int32_t>(m_PendingUpdates.size());
        m_PendingUpdates.push_back(p);
    }

    void GPUScene::Sync(VkCommandBuffer cmd)
    {
        // Move updates out under lock so we minimize contention.
        std::vector<GpuUpdatePacket> updates;
        {
            std::lock_guard lock(m_UpdateMutex);
            if (m_PendingUpdates.empty())
                return;
            updates.swap(m_PendingUpdates);
            std::fill(m_PendingUpdateIndexBySlot.begin(), m_PendingUpdateIndexBySlot.end(), -1);
        }

        const size_t bytes = updates.size() * sizeof(GpuUpdatePacket);
        if (bytes == 0)
            return;

        // Ensure transient buffer exists and is big enough.
        if (!m_UpdatesStaging || bytes > m_UpdatesStagingCapacity)
        {
            m_UpdatesStagingCapacity = std::max(bytes, size_t(4));
            m_UpdatesStaging = std::make_unique<RHI::VulkanBuffer>(
                m_Device,
                m_UpdatesStagingCapacity,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VMA_MEMORY_USAGE_CPU_TO_GPU);
        }

        m_UpdatesStaging->Write(updates.data(), bytes);

        if (!m_UpdateSetPool)
        {
            Core::Log::Error("GPUScene::Sync: update descriptor pool missing.");
            return;
        }

        VkDescriptorSet set = m_UpdateSetPool->Allocate(m_UpdateSetLayout);
        if (set == VK_NULL_HANDLE)
        {
            Core::Log::Error("GPUScene::Sync: failed to allocate update descriptor set.");
            return;
        }

        VkDescriptorBufferInfo updatesInfo{};
        updatesInfo.buffer = m_UpdatesStaging->GetHandle();
        updatesInfo.offset = 0;
        updatesInfo.range = VK_WHOLE_SIZE;

        VkDescriptorBufferInfo sceneInfo{};
        sceneInfo.buffer = m_SceneBuffer->GetHandle();
        sceneInfo.offset = 0;
        sceneInfo.range = VK_WHOLE_SIZE;

        VkDescriptorBufferInfo boundsInfo{};
        boundsInfo.buffer = m_BoundsBuffer->GetHandle();
        boundsInfo.offset = 0;
        boundsInfo.range = VK_WHOLE_SIZE;

        VkWriteDescriptorSet w[3]{};
        auto setBuf = [&](uint32_t i, uint32_t binding, const VkDescriptorBufferInfo* info)
        {
            w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[i].dstSet = set;
            w[i].dstBinding = binding;
            w[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            w[i].descriptorCount = 1;
            w[i].pBufferInfo = info;
        };

        setBuf(0, 0, &updatesInfo);
        setBuf(1, 1, &sceneInfo);
        setBuf(2, 2, &boundsInfo);

        vkUpdateDescriptorSets(m_Device.GetLogicalDevice(), 3, w, 0, nullptr);

        struct Push
        {
            uint32_t UpdateCount;
            uint32_t _pad0;
            uint32_t _pad1;
            uint32_t _pad2;
        } pc{ static_cast<uint32_t>(updates.size()), 0, 0, 0 };

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_UpdatePipeline.GetHandle());
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_UpdatePipeline.GetLayout(), 0, 1, &set, 0, nullptr);
        vkCmdPushConstants(cmd, m_UpdatePipeline.GetLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Push), &pc);

        const uint32_t wg = 64;
        const uint32_t groups = (pc.UpdateCount + wg - 1) / wg;
        vkCmdDispatch(cmd, groups, 1, 1);
    }
}
