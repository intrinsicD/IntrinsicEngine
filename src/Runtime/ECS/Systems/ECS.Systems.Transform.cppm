module;
#include <entt/fwd.hpp>

export module ECS:Systems.Transform;

export namespace Core
{
    class FrameGraph;
}

export namespace ECS::Systems::Transform
{
    void OnUpdate(entt::registry& registry);

    // Register this system into a FrameGraph with its dependency declarations.
    // Declares: Write<Transform::WorldMatrix>, Write<Transform::IsDirtyTag>, Signal("TransformUpdate").
    void RegisterSystem(Core::FrameGraph& graph, entt::registry& registry);
}
