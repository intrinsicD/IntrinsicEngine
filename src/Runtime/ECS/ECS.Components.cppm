module;
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>

export module Runtime.ECS.Components;

import Runtime.RHI.Buffer;

export namespace Runtime::ECS
{
    struct TagComponent
    {
        std::string Name;
    };

    struct TransformComponent
    {
        glm::vec3 Position{0.0f};
        glm::vec3 Rotation{0.0f}; // Euler angles for simplicity now
        glm::vec3 Scale{1.0f};

        [[nodiscard]] glm::mat4 GetTransform() const
        {
            // Simple matrix generation (Optimization: cache this)
            glm::mat4 mat = glm::translate(glm::mat4(1.0f), Position);
            // Rotate logic omitted for brevity, adding simple Identity or basic rotation in main is fine for now
            return mat;
        }
    };

    // Simple pointer to the mesh data (In reality, this would be a Resource Handle)
    struct MeshComponent
    {
        // We store raw pointers just to link ECS to our RHI resources for this step
        // In the future, use AssetID
        // Note: We don't own these resources here!
        class Runtime::RHI::VulkanBuffer* VertexBuffer;
        class Runtime::RHI::VulkanBuffer* IndexBuffer;
        uint32_t IndexCount;
    };
}
