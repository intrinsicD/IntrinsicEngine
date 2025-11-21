module;
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>

export module Runtime.ECS.Components;

import Runtime.RHI.Buffer;
import Runtime.Graphics.Mesh;
import Runtime.Graphics.Material;

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

    struct MeshRendererComponent {
        Graphics::Mesh* MeshRef;
        Graphics::Material* MaterialRef;
    };
}
