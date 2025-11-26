module;
#include <cstring>
#include <memory>
#include <vector>
#include <RHI/RHI.Vulkan.hpp>

module Runtime.Graphics.Mesh;

import Runtime.RHI.Device;
import Runtime.RHI.Buffer;

namespace Runtime::Graphics
{
    Mesh::Mesh(std::shared_ptr<RHI::VulkanDevice> device, const std::vector<RHI::Vertex>& vertices,
               const std::vector<uint32_t>& indices)
    {
        m_IndexCount = static_cast<uint32_t>(indices.size());

        // 1. Vertex Buffer
        VkDeviceSize vSize = sizeof(vertices[0]) * vertices.size();
        RHI::VulkanBuffer vStaging(device, vSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
        memcpy(vStaging.Map(), vertices.data(), (size_t)vSize);
        vStaging.Unmap();

        m_VertexBuffer = std::make_unique<RHI::VulkanBuffer>(
            device, vSize,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY
        );

        // 2. Index Buffer
        VkDeviceSize iSize = sizeof(indices[0]) * indices.size();
        RHI::VulkanBuffer iStaging(device, iSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
        memcpy(iStaging.Map(), indices.data(), (size_t)iSize);
        iStaging.Unmap();

        m_IndexBuffer = std::make_unique<RHI::VulkanBuffer>(
            device, iSize,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY
        );

        // 3. Upload
        RHI::CommandUtils::ExecuteImmediate(*device, [&](VkCommandBuffer cmd)
        {
            VkBufferCopy copyRegion{};
            copyRegion.size = vSize;
            vkCmdCopyBuffer(cmd, vStaging.GetHandle(), m_VertexBuffer->GetHandle(), 1, &copyRegion);

            copyRegion.size = iSize;
            vkCmdCopyBuffer(cmd, iStaging.GetHandle(), m_IndexBuffer->GetHandle(), 1, &copyRegion);
        });
    }
}
