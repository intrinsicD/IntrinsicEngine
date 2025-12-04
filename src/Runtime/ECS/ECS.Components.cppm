module;
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>
#include <memory>

export module Runtime.ECS.Components;

import Runtime.RHI.Buffer;
import Runtime.Graphics.Material;
import Runtime.Graphics.Geometry;

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
            glm::mat4 mat = glm::translate(glm::mat4(1.0f), Position);

            mat = glm::rotate(mat, glm::radians(Rotation.x), glm::vec3(1, 0, 0));
            mat = glm::rotate(mat, glm::radians(Rotation.y), glm::vec3(0, 1, 0));
            mat = glm::rotate(mat, glm::radians(Rotation.z), glm::vec3(0, 0, 1));

            mat = glm::scale(mat, Scale);
            return mat;
        }
    };

    class TransformController
    {
    public:
        virtual ~TransformController() = default;
        virtual void OnUpdate(TransformComponent& transform, float dt) = 0;
    };

    class TransformRotating : public TransformController
    {
    public:
        float Speed = 45.0f;
        void OnUpdate(TransformComponent& transform, float dt) override
        {
            transform.Rotation.y += Speed * dt;
        }
    };

    struct MeshRendererComponent {
        std::shared_ptr<Graphics::GeometryGpuData> GeometryRef; // Renamed from MeshRef
        std::shared_ptr<Graphics::Material> MaterialRef;
    };
}
