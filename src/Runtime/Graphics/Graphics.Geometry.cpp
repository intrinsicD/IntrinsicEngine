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
import Core.Logging;

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
                                 const GeometryUploadRequest& data,
                                 const GeometryPool* existingPool)
    {
        auto result = std::make_unique<GeometryGpuData>();
        result->m_IndexCount = static_cast<uint32_t>(data.Indices.size());
        result->m_Layout.Topology = data.Topology;

        const bool wantsReuse = data.ReuseVertexBuffersFrom.IsValid();

        // ---------------------------------------------------------------------
        // 1) Vertex buffer setup (reuse vs allocate)
        // ---------------------------------------------------------------------
        VkDeviceSize totalVertexSize = 0;
        if (wantsReuse)
        {
            if (!existingPool)
            {
                Core::Log::Error("GeometryGpuData::CreateAsync: ReuseVertexBuffersFrom requested but no GeometryPool provided.");
                return { std::unique_ptr<GeometryGpuData>{}, RHI::TransferToken{} };
            }

            const GeometryGpuData* source = existingPool->GetUnchecked(data.ReuseVertexBuffersFrom);
            if (!source || !source->m_VertexBuffer)
            {
                Core::Log::Error("GeometryGpuData::CreateAsync: ReuseVertexBuffersFrom handle invalid or source has no vertex buffer.");
                return { std::unique_ptr<GeometryGpuData>{}, RHI::TransferToken{} };
            }

            // Validate that the source has a meaningful layout.
            const VkDeviceSize sourceTotal = source->m_Layout.AuxOffset + source->m_Layout.AuxSize;
            if (sourceTotal == 0)
            {
                Core::Log::Error("GeometryGpuData::CreateAsync: ReuseVertexBuffersFrom refers to geometry with empty vertex layout.");
                return { std::unique_ptr<GeometryGpuData>{}, RHI::TransferToken{} };
            }

            // If the caller supplied spans anyway, they must match the reused layout sizes.
            // This catches accidental mismatches early (e.g., trying to reuse a mesh vertex buffer for a point cloud).
            if (!data.Positions.empty() && data.Positions.size_bytes() != source->m_Layout.PositionsSize)
            {
                Core::Log::Error(
                    "GeometryGpuData::CreateAsync: ReuseVertexBuffersFrom mismatch: Positions span bytes ({}) != source layout bytes ({}).",
                    data.Positions.size_bytes(), source->m_Layout.PositionsSize);
                return { std::unique_ptr<GeometryGpuData>{}, RHI::TransferToken{} };
            }
            if (!data.Normals.empty() && data.Normals.size_bytes() != source->m_Layout.NormalsSize)
            {
                Core::Log::Error(
                    "GeometryGpuData::CreateAsync: ReuseVertexBuffersFrom mismatch: Normals span bytes ({}) != source layout bytes ({}).",
                    data.Normals.size_bytes(), source->m_Layout.NormalsSize);
                return { std::unique_ptr<GeometryGpuData>{}, RHI::TransferToken{} };
            }
            if (!data.Aux.empty() && data.Aux.size_bytes() != source->m_Layout.AuxSize)
            {
                Core::Log::Error(
                    "GeometryGpuData::CreateAsync: ReuseVertexBuffersFrom mismatch: Aux span bytes ({}) != source layout bytes ({}).",
                    data.Aux.size_bytes(), source->m_Layout.AuxSize);
                return { std::unique_ptr<GeometryGpuData>{}, RHI::TransferToken{} };
            }

            // Shared ownership: views alias the same vertex buffer.
            result->m_VertexBuffer = source->m_VertexBuffer;

            // Copy layout for vertex streams exactly. Topology remains view-specific.
            const PrimitiveTopology topo = result->m_Layout.Topology;
            result->m_Layout = source->m_Layout;
            result->m_Layout.Topology = topo;

            totalVertexSize = sourceTotal;
        }
        else
        {
            // Calculate Layout & Sizes from provided spans.
            const VkDeviceSize posSize = data.Positions.size_bytes();
            const VkDeviceSize normSize = data.Normals.size_bytes();
            const VkDeviceSize auxSize = data.Aux.size_bytes();

            result->m_Layout.PositionsOffset = 0;
            result->m_Layout.PositionsSize = posSize;
            result->m_Layout.NormalsOffset = AlignSize(result->m_Layout.PositionsOffset + result->m_Layout.PositionsSize, 16);
            result->m_Layout.NormalsSize = normSize;
            result->m_Layout.AuxOffset = AlignSize(result->m_Layout.NormalsOffset + result->m_Layout.NormalsSize, 16);
            result->m_Layout.AuxSize = auxSize;

            totalVertexSize = result->m_Layout.AuxOffset + result->m_Layout.AuxSize;
        }

        const VkDeviceSize idxSize = data.Indices.size_bytes();

        // ---------------------------------------------------------------------
        // 2) Direct mode: CPU->GPU mapped uploads (no transfer token)
        // ---------------------------------------------------------------------
        if (data.UploadMode == GeometryUploadMode::Direct)
        {
            if (!wantsReuse && totalVertexSize > 0)
            {
                result->m_VertexBuffer = std::make_shared<RHI::VulkanBuffer>(
                    *device, totalVertexSize,
                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                    VMA_MEMORY_USAGE_CPU_TO_GPU);

                if (!data.Positions.empty()) result->m_VertexBuffer->Write(data.Positions.data(), data.Positions.size_bytes(), result->m_Layout.PositionsOffset);
                if (!data.Normals.empty()) result->m_VertexBuffer->Write(data.Normals.data(), data.Normals.size_bytes(), result->m_Layout.NormalsOffset);
                if (!data.Aux.empty()) result->m_VertexBuffer->Write(data.Aux.data(), data.Aux.size_bytes(), result->m_Layout.AuxOffset);
            }

            if (result->m_IndexCount > 0)
            {
                result->m_IndexBuffer = std::make_shared<RHI::VulkanBuffer>(
                    *device, idxSize,
                    VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                    VMA_MEMORY_USAGE_CPU_TO_GPU);

                result->m_IndexBuffer->Write(data.Indices.data(), idxSize, 0);
            }

            return { std::move(result), RHI::TransferToken{} };
        }

        // ---------------------------------------------------------------------
        // 3) Staged mode: GPU-only buffers + transfer manager copy
        // ---------------------------------------------------------------------
        std::vector<std::unique_ptr<RHI::VulkanBuffer>> stagingBuffers;
        VkCommandBuffer cmd = transferManager.Begin();
        bool hasCopies = false;

        // Vulkan requires srcOffset to be aligned.
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(device->GetPhysicalDevice(), &props);
        const size_t copyAlign = std::max<size_t>(16, static_cast<size_t>(props.limits.optimalBufferCopyOffsetAlignment));

        // Vertex Buffer Upload (if not reused)
        if (!wantsReuse && totalVertexSize > 0)
        {
            // Try staging belt (fast path)
            auto alloc = transferManager.AllocateStaging(static_cast<size_t>(totalVertexSize), copyAlign);

            VkBuffer stagingHandle = VK_NULL_HANDLE;
            VkDeviceSize stagingOffset = 0;

            if (alloc.Buffer != VK_NULL_HANDLE)
            {
                uint8_t* ptr = static_cast<uint8_t*>(alloc.MappedPtr);

                if (!data.Positions.empty()) memcpy(ptr + result->m_Layout.PositionsOffset, data.Positions.data(), data.Positions.size_bytes());
                if (!data.Normals.empty()) memcpy(ptr + result->m_Layout.NormalsOffset, data.Normals.data(), data.Normals.size_bytes());
                if (!data.Aux.empty()) memcpy(ptr + result->m_Layout.AuxOffset, data.Aux.data(), data.Aux.size_bytes());

                stagingHandle = alloc.Buffer;
                stagingOffset = static_cast<VkDeviceSize>(alloc.Offset);
            }
            else
            {
                // Slow path fallback: dedicated staging allocation
                auto staging = std::make_unique<RHI::VulkanBuffer>(*device, totalVertexSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
                uint8_t* ptr = static_cast<uint8_t*>(staging->Map());

                if (!data.Positions.empty()) memcpy(ptr + result->m_Layout.PositionsOffset, data.Positions.data(), data.Positions.size_bytes());
                if (!data.Normals.empty()) memcpy(ptr + result->m_Layout.NormalsOffset, data.Normals.data(), data.Normals.size_bytes());
                if (!data.Aux.empty()) memcpy(ptr + result->m_Layout.AuxOffset, data.Aux.data(), data.Aux.size_bytes());
                staging->Unmap();

                stagingHandle = staging->GetHandle();
                stagingOffset = 0;

                stagingBuffers.push_back(std::move(staging));
            }

            result->m_VertexBuffer = std::make_shared<RHI::VulkanBuffer>(
                *device, totalVertexSize,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                VMA_MEMORY_USAGE_GPU_ONLY);

            VkBufferCopy copyRegion{};
            copyRegion.srcOffset = stagingOffset;
            copyRegion.dstOffset = 0;
            copyRegion.size = totalVertexSize;
            vkCmdCopyBuffer(cmd, stagingHandle, result->m_VertexBuffer->GetHandle(), 1, &copyRegion);
            hasCopies = true;
        }

        // Index Buffer Upload (always unique to this view)
        if (result->m_IndexCount > 0)
        {
            auto alloc = transferManager.AllocateStaging(static_cast<size_t>(idxSize), copyAlign);

            VkBuffer stagingHandle = VK_NULL_HANDLE;
            VkDeviceSize stagingOffset = 0;

            if (alloc.Buffer != VK_NULL_HANDLE)
            {
                memcpy(alloc.MappedPtr, data.Indices.data(), idxSize);
                stagingHandle = alloc.Buffer;
                stagingOffset = static_cast<VkDeviceSize>(alloc.Offset);
            }
            else
            {
                auto iStaging = std::make_unique<RHI::VulkanBuffer>(*device, idxSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
                memcpy(iStaging->Map(), data.Indices.data(), idxSize);
                iStaging->Unmap();

                stagingHandle = iStaging->GetHandle();
                stagingOffset = 0;
                stagingBuffers.push_back(std::move(iStaging));
            }

            result->m_IndexBuffer = std::make_shared<RHI::VulkanBuffer>(
                *device, idxSize,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                VMA_MEMORY_USAGE_GPU_ONLY);

            VkBufferCopy copyRegion{};
            copyRegion.srcOffset = stagingOffset;
            copyRegion.dstOffset = 0;
            copyRegion.size = idxSize;
            vkCmdCopyBuffer(cmd, stagingHandle, result->m_IndexBuffer->GetHandle(), 1, &copyRegion);
            hasCopies = true;
        }

        // If we recorded no copies, return an invalid token (no work submitted).
        if (!hasCopies)
            return { std::move(result), RHI::TransferToken{} };

        RHI::TransferToken token = stagingBuffers.empty()
            ? transferManager.Submit(cmd)
            : transferManager.Submit(cmd, std::move(stagingBuffers));

        return { std::move(result), token };
    }
}
