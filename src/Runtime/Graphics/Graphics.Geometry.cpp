// src/Runtime/Graphics/Graphics.Geometry.cpp
module;
#include <cstring>
#include <memory>
#include <numeric>
#include <vector>
#include "RHI.Vulkan.hpp"

module Graphics:Geometry.Impl;
import :Geometry;
import RHI;
import Core;

namespace Graphics
{
    // Helper to align offsets to 16 bytes for SIMD safety
    static VkDeviceSize AlignSize(VkDeviceSize size, VkDeviceSize alignment)
    {
        return (size + alignment - 1) & ~(alignment - 1);
    }

    std::pair<std::unique_ptr<GeometryGpuData>, RHI::TransferToken>
    GeometryGpuData::CreateAsync(std::shared_ptr<RHI::VulkanDevice> device,
                                 RHI::TransferManager& transferManager,
                                 const GeometryUploadRequest& data)
    {
        auto result = std::make_unique<GeometryGpuData>();
        result->m_IndexCount = static_cast<uint32_t>(data.Indices.size());
        result->m_Layout.Topology = data.Topology;

        // 1. Calculate Layout & Sizes
        VkDeviceSize posSize = data.Positions.size_bytes();
        VkDeviceSize normSize = data.Normals.size_bytes();
        VkDeviceSize auxSize = data.Aux.size_bytes();

        result->m_Layout.PositionsOffset = 0;
        result->m_Layout.PositionsSize = posSize;
        result->m_Layout.NormalsOffset = AlignSize(result->m_Layout.PositionsOffset + result->m_Layout.PositionsSize, 16);
        result->m_Layout.NormalsSize = normSize;
        result->m_Layout.AuxOffset = AlignSize(result->m_Layout.NormalsOffset + result->m_Layout.NormalsSize, 16);
        result->m_Layout.AuxSize = auxSize;

        VkDeviceSize totalVertexSize = result->m_Layout.AuxOffset + result->m_Layout.AuxSize;
        VkDeviceSize idxSize = data.Indices.size_bytes();

        // 2. Prepare Transfer
        std::vector<std::unique_ptr<RHI::VulkanBuffer>> stagingBuffers;
        VkCommandBuffer cmd = transferManager.Begin();

        // Vertex Buffer Upload
        if (totalVertexSize > 0) {
            auto staging = std::make_unique<RHI::VulkanBuffer>(device, totalVertexSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
            uint8_t* ptr = static_cast<uint8_t*>(staging->Map());

            if (!data.Positions.empty()) memcpy(ptr + result->m_Layout.PositionsOffset, data.Positions.data(), posSize);
            if (!data.Normals.empty()) memcpy(ptr + result->m_Layout.NormalsOffset, data.Normals.data(), normSize);
            if (!data.Aux.empty()) memcpy(ptr + result->m_Layout.AuxOffset, data.Aux.data(), auxSize);
            staging->Unmap();

            result->m_VertexBuffer = std::make_unique<RHI::VulkanBuffer>(
                device, totalVertexSize,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VMA_MEMORY_USAGE_GPU_ONLY
            );

            VkBufferCopy copyRegion{};
            copyRegion.size = totalVertexSize;
            vkCmdCopyBuffer(cmd, staging->GetHandle(), result->m_VertexBuffer->GetHandle(), 1, &copyRegion);

            // Move ownership to staging list so it survives until GPU is done
            stagingBuffers.push_back(std::move(staging));
        }

        // Index Buffer Upload
        if (result->m_IndexCount > 0) {
            auto iStaging = std::make_unique<RHI::VulkanBuffer>(device, idxSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
            memcpy(iStaging->Map(), data.Indices.data(), idxSize);
            iStaging->Unmap();

            result->m_IndexBuffer = std::make_unique<RHI::VulkanBuffer>(
                device, idxSize,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                VMA_MEMORY_USAGE_GPU_ONLY
            );

            VkBufferCopy copyRegion{};
            copyRegion.size = idxSize;
            vkCmdCopyBuffer(cmd, iStaging->GetHandle(), result->m_IndexBuffer->GetHandle(), 1, &copyRegion);

            stagingBuffers.push_back(std::move(iStaging));
        }

        // 3. Submit
        RHI::TransferToken token = transferManager.Submit(cmd, std::move(stagingBuffers));

        return { std::move(result), token };
    }
}
