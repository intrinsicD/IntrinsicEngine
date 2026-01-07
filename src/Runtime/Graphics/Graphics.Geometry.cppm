// src/Runtime/Graphics/Graphics.Geometry.cppm
module;
#include <vector>
#include <span>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <utility>
#include <queue>
#include <glm/glm.hpp>
#include "RHI.Vulkan.hpp"

export module Graphics:Geometry;

import RHI;
import Geometry;

export namespace Graphics
{
    // --- Data Structures ---

    enum class PrimitiveTopology
    {
        Triangles,
        Lines,
        Points
    };

    struct GeometryUploadRequest
    {
        std::span<const glm::vec3> Positions;
        std::span<const glm::vec3> Normals;
        std::span<const glm::vec4> Aux; // UVs packed in xy, Color/Data in zw
        std::span<const uint32_t> Indices;
        PrimitiveTopology Topology = PrimitiveTopology::Triangles;
    };

    struct GeometryCpuData
    {
        std::vector<glm::vec3> Positions;
        std::vector<glm::vec3> Normals;
        std::vector<glm::vec4> Aux;
        std::vector<uint32_t> Indices;
        PrimitiveTopology Topology = PrimitiveTopology::Triangles;

        [[nodiscard]] GeometryUploadRequest ToUploadRequest() const
        {
            return {Positions, Normals, Aux, Indices, Topology};
        }
    };

    struct GeometryCollisionData
    {
        Geometry::AABB LocalAABB;
        Geometry::Octree LocalOctree; // Static octree of mesh vertices

        // Optional: Keep positions for precise ray intersection tests after Octree broadphase
        std::vector<glm::vec3> Positions;
        std::vector<uint32_t> Indices;
    };

    struct GeometryBufferLayout
    {
        VkDeviceSize PositionsOffset = 0;
        VkDeviceSize PositionsSize = 0;

        VkDeviceSize NormalsOffset = 0;
        VkDeviceSize NormalsSize = 0;

        VkDeviceSize AuxOffset = 0;
        VkDeviceSize AuxSize = 0;
        PrimitiveTopology Topology = PrimitiveTopology::Triangles;
    };

    // --- The GPU Resource ---

    class GeometryGpuData
    {
    public:
        GeometryGpuData() = default;
        [[deprecated("Use CreateAsync to avoid stalling the Render Thread via ExecuteImmediate.")]]
        GeometryGpuData(std::shared_ptr<RHI::VulkanDevice> device, const GeometryUploadRequest& data);
        [[deprecated("Use CreateAsync. This constructor does not manage index buffer staging memory correctly.")]]
        GeometryGpuData(std::shared_ptr<RHI::VulkanDevice> device,
                   const GeometryUploadRequest& data,
                   VkCommandBuffer cmd,
                   RHI::VulkanBuffer& stagingBuffer,
                   size_t stagingOffset);
        ~GeometryGpuData() = default;

        [[nodiscard]] static std::pair<std::unique_ptr<GeometryGpuData>, RHI::TransferToken>
       CreateAsync(std::shared_ptr<RHI::VulkanDevice> device,
                   RHI::TransferManager& transferManager,
                   const GeometryUploadRequest& data);

        [[nodiscard]] RHI::VulkanBuffer* GetVertexBuffer() const { return m_VertexBuffer.get(); }
        [[nodiscard]] RHI::VulkanBuffer* GetIndexBuffer() const { return m_IndexBuffer.get(); }
        [[nodiscard]] uint32_t GetIndexCount() const { return m_IndexCount; }
        [[nodiscard]] const GeometryBufferLayout& GetLayout() const { return m_Layout; }
        [[nodiscard]] PrimitiveTopology GetTopology() const { return m_Layout.Topology; }
    private:
        std::unique_ptr<RHI::VulkanBuffer> m_VertexBuffer;
        std::unique_ptr<RHI::VulkanBuffer> m_IndexBuffer;

        GeometryBufferLayout m_Layout{};
        uint32_t m_IndexCount = 0;
    };

    struct GeometryHandle
    {
        uint32_t Index = std::numeric_limits<uint32_t>::max();
        uint32_t Generation = 0;

        auto operator<=>(const GeometryHandle&) const = default;
        [[nodiscard]] bool IsValid() const { return Index != std::numeric_limits<uint32_t>::max(); }
    };

    class GeometryStorage
    {
    public:
        // Adds ownership of the GPU data to the system and returns a handle
        GeometryHandle Add(std::unique_ptr<GeometryGpuData> data)
        {
            std::unique_lock lock(m_Mutex);

            uint32_t index;
            if (!m_FreeIndices.empty())
            {
                index = m_FreeIndices.front();
                m_FreeIndices.pop_front();
            }
            else
            {
                index = static_cast<uint32_t>(m_Slots.size());
                m_Slots.emplace_back();
            }

            Slot& slot = m_Slots[index];
            slot.Data = std::move(data);
            slot.Generation++; // Bump generation on allocation
            slot.IsActive = true;

            return {index, slot.Generation};
        }

        void Remove(GeometryHandle handle)
        {
            std::unique_lock lock(m_Mutex);

            if (handle.Index >= m_Slots.size()) return;

            Slot& slot = m_Slots[handle.Index];
            if (slot.IsActive && slot.Generation == handle.Generation)
            {
                slot.Data.reset();
                slot.IsActive = false;
                m_FreeIndices.push_back(handle.Index);
            }
        }

        [[nodiscard]] GeometryGpuData* Get(GeometryHandle handle)
        {
            std::unique_lock lock(m_Mutex);

            if (handle.Index >= m_Slots.size()) return nullptr;

            Slot& slot = m_Slots[handle.Index];
            if (slot.IsActive && slot.Generation == handle.Generation)
            {
                return slot.Data.get();
            }
            return nullptr;
        }

    private:
        struct Slot
        {
            std::unique_ptr<GeometryGpuData> Data;
            uint32_t Generation = 0;
            bool IsActive = false;
        };

        std::vector<Slot> m_Slots;
        std::deque<uint32_t> m_FreeIndices;
        std::shared_mutex m_Mutex;
    };
}
