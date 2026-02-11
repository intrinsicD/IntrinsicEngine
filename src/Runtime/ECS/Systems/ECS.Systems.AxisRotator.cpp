module;
#include <entt/entity/registry.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp>

module ECS:Systems.AxisRotator.Impl;
import :Systems.AxisRotator;
import :Components.Transform;
import :Components.AxisRotator;
import Core;

namespace ECS::Systems::AxisRotator
{
    void OnUpdate(entt::registry& registry, float dt)
    {
        auto view = registry.view<Components::Transform::Component, Components::AxisRotator::Component>();
        for (auto [entity, transform, rotator] : view.each())
        {
            glm::quat deltaRotation = glm::angleAxis(glm::radians(rotator.Speed * dt), rotator.axis);
            transform.Rotation = glm::normalize(deltaRotation * transform.Rotation);

            // Ensure the transform system recomputes WorldMatrix this tick.
            registry.emplace_or_replace<Components::Transform::IsDirtyTag>(entity);
        }
    }

    void RegisterSystem(Core::FrameGraph& graph, entt::registry& registry, float dt)
    {
        graph.AddPass("AxisRotator",
            [](Core::FrameGraphBuilder& builder)
            {
                builder.Read<Components::AxisRotator::Component>();
                builder.Write<Components::Transform::Component>();
                builder.Write<Components::Transform::IsDirtyTag>();
            },
            [&registry, dt]()
            {
                OnUpdate(registry, dt);
            });
    }
}
