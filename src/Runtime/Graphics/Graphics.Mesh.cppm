module;
#include <vector>
#include <memory>

export module Runtime.Graphics.Mesh;

import Runtime.RHI.Device;
import Runtime.RHI.Buffer;
import Runtime.RHI.Types;
import Runtime.RHI.CommandUtils;

export namespace Runtime::Graphics
{
    class Mesh
    {
    public:
        Mesh(std::shared_ptr<RHI::VulkanDevice> device, const std::vector<RHI::Vertex>& vertices, const std::vector<uint32_t>& indices);
        ~Mesh() = default; // UniquePtr handles destruction

        [[nodiscard]] RHI::VulkanBuffer* GetVertexBuffer() const { return m_VertexBuffer.get(); }
        [[nodiscard]] RHI::VulkanBuffer* GetIndexBuffer() const { return m_IndexBuffer.get(); }
        [[nodiscard]] uint32_t GetIndexCount() const { return m_IndexCount; }

    private:
        std::unique_ptr<RHI::VulkanBuffer> m_VertexBuffer;
        std::unique_ptr<RHI::VulkanBuffer> m_IndexBuffer;
        uint32_t m_IndexCount = 0;
    };
}
