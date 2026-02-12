module;
#include <entt/fwd.hpp>

export module ECS:Systems.AxisRotator;

import Core.FrameGraph;

export namespace ECS::Systems::AxisRotator
{
    void OnUpdate(entt::registry& registry, float dt);

    // Register this system into a FrameGraph with its dependency declarations.
    // Declares: Read<AxisRotator::Component>, Write<Transform::Component>, Write<Transform::IsDirtyTag>.
    // Must run before TransformUpdate (AxisRotator dirties transforms for the hierarchy pass).
    void RegisterSystem(Core::FrameGraph& graph, entt::registry& registry, float dt);
}
