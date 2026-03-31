module;
#include <entt/fwd.hpp>
#include <glm/vec3.hpp>
#include <glm/fwd.hpp>
#include <span>

export module ECS:Systems.AxisRotator;

import Core.FrameGraph;

export namespace ECS::Systems::AxisRotator
{
    struct AxisAngularVelocity
    {
        glm::vec3 Axis{0.0f, 1.0f, 0.0f};
        float DegreesPerSecond = 0.0f;
    };

    // SoA-ready batch kernel for deterministic unit testing and hot-loop reuse.
    // Updates each input quaternion in place using the matching axis/speed entry.
    void RotateBatch(std::span<glm::quat> rotations, std::span<const AxisAngularVelocity> angularVelocities, float dt);

    void OnUpdate(entt::registry& registry, float dt);

    // Register this system into a FrameGraph with its dependency declarations.
    // Declares: Read<AxisRotator::Component>, Write<Transform::Component>, Write<Transform::IsDirtyTag>.
    // Must run before TransformUpdate (AxisRotator dirties transforms for the hierarchy pass).
    void RegisterSystem(Core::FrameGraph& graph, entt::registry& registry, float dt);
}
