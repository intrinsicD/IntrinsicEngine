// src/Runtime/Graphics/Graphics.Geometry.cppm
module;
#include <vector>
#include <span>
#include <memory>
#include <glm/glm.hpp>
#include <RHI/RHI.Vulkan.hpp>

export module Runtime.Graphics.Geometry;

import Runtime.RHI.Device;
import Runtime.RHI.Buffer;
import Runtime.Geometry.AABB;
import Runtime.Geometry.Octree;

export namespace Runtime::Graphics
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
        GeometryGpuData(std::shared_ptr<RHI::VulkanDevice> device, const GeometryUploadRequest& data);
        GeometryGpuData(std::shared_ptr<RHI::VulkanDevice> device,
                   const GeometryUploadRequest& data,
                   VkCommandBuffer cmd,
                   RHI::VulkanBuffer& stagingBuffer,
                   size_t stagingOffset);
        ~GeometryGpuData() = default;

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
}
