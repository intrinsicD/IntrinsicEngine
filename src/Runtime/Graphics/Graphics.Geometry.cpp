// src/Runtime/Graphics/Graphics.Geometry.cpp
module;
#include <cstring>
#include <memory>
#include <numeric>
#include <RHI/RHI.Vulkan.hpp>

module Runtime.Graphics.Geometry;

import Runtime.RHI.CommandUtils;
import Core.Logging;

namespace Runtime::Graphics
{
    // Helper to align offsets to 16 bytes for SIMD safety
    static VkDeviceSize AlignSize(VkDeviceSize size, VkDeviceSize alignment) {
        return (size + alignment - 1) & ~(alignment - 1);
    }

    GeometryGpuData::GeometryGpuData(std::shared_ptr<RHI::VulkanDevice> device, const GeometryUploadRequest& data)
    {
        m_IndexCount = static_cast<uint32_t>(data.Indices.size());

        // 1. Calculate Layout (Structure of Arrays)
        // We pack [Positions | Normals | Aux] into one buffer.

        VkDeviceSize posSize = data.Positions.size_bytes();
        VkDeviceSize normSize = data.Normals.size_bytes();
        VkDeviceSize auxSize = data.Aux.size_bytes();

        m_Layout.PositionsOffset = 0;
        m_Layout.PositionsSize = posSize;

        // Align each section to 16 bytes
        m_Layout.NormalsOffset = AlignSize(m_Layout.PositionsOffset + m_Layout.PositionsSize, 16);
        m_Layout.NormalsSize = normSize;

        m_Layout.AuxOffset = AlignSize(m_Layout.NormalsOffset + m_Layout.NormalsSize, 16);
        m_Layout.AuxSize = auxSize;

        VkDeviceSize totalVertexSize = m_Layout.AuxOffset + m_Layout.AuxSize;

        // 2. Vertex Buffer (Staging -> GPU)
        if (totalVertexSize > 0)
        {
            RHI::VulkanBuffer staging(device, totalVertexSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
            uint8_t* ptr = static_cast<uint8_t*>(staging.Map());

            if (!data.Positions.empty())
                memcpy(ptr + m_Layout.PositionsOffset, data.Positions.data(), posSize);

            if (!data.Normals.empty())
                memcpy(ptr + m_Layout.NormalsOffset, data.Normals.data(), normSize);

            if (!data.Aux.empty())
                memcpy(ptr + m_Layout.AuxOffset, data.Aux.data(), auxSize);

            staging.Unmap();

            m_VertexBuffer = std::make_unique<RHI::VulkanBuffer>(
                device, totalVertexSize,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VMA_MEMORY_USAGE_GPU_ONLY
            );

            RHI::CommandUtils::ExecuteImmediate(*device, [&](VkCommandBuffer cmd) {
                VkBufferCopy copyRegion{};
                copyRegion.size = totalVertexSize;
                vkCmdCopyBuffer(cmd, staging.GetHandle(), m_VertexBuffer->GetHandle(), 1, &copyRegion);
            });
        }

        // 3. Index Buffer
        if (m_IndexCount > 0)
        {
            VkDeviceSize idxSize = data.Indices.size_bytes();
            RHI::VulkanBuffer iStaging(device, idxSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
            memcpy(iStaging.Map(), data.Indices.data(), idxSize);
            iStaging.Unmap();

            m_IndexBuffer = std::make_unique<RHI::VulkanBuffer>(
                device, idxSize,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                VMA_MEMORY_USAGE_GPU_ONLY
            );

            RHI::CommandUtils::ExecuteImmediate(*device, [&](VkCommandBuffer cmd) {
                VkBufferCopy copyRegion{};
                copyRegion.size = idxSize;
                vkCmdCopyBuffer(cmd, iStaging.GetHandle(), m_IndexBuffer->GetHandle(), 1, &copyRegion);
            });
        }

        Core::Log::Info("Geometry Loaded: {} verts, {} indices. SoA Layout [P:{} | N:{} | A:{}]",
            data.Positions.size(), m_IndexCount, m_Layout.PositionsOffset, m_Layout.NormalsOffset, m_Layout.AuxOffset);
    }
}