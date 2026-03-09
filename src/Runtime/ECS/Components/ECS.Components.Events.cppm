module;
#include <entt/entity/entity.hpp>

export module ECS:Components.Events;

export namespace ECS::Events
{
    // Fired after ApplySelection() changes the set of entities with SelectedTag.
    // Consumers: property panels, hierarchy highlight, gizmo pivot, undo stack.
    struct SelectionChanged
    {
        // The entity that was acted upon (may be entt::null for deselect-all).
        entt::entity Entity = entt::null;
    };

    // Fired after ApplyHover() changes the entity with HoveredTag.
    // Consumers: tooltip, highlight outline, status bar.
    struct HoverChanged
    {
        entt::entity Entity = entt::null;
    };

    // Fired after GPU pick readback completes on the main thread.
    // Consumers: SelectionModule (to apply the pick result).
    struct GpuPickCompleted
    {
        entt::entity Entity = entt::null;
    };

    // Fired after a new entity is spawned via SceneManager or asset drop.
    // Consumers: hierarchy panel, dirty tracker, undo stack.
    struct EntitySpawned
    {
        entt::entity Entity = entt::null;
    };

    // Fired after a geometry operator modifies mesh/graph/point cloud data.
    // Consumers: edge/vertex view re-sync, scene dirty state, undo stack.
    struct GeometryModified
    {
        entt::entity Entity = entt::null;
    };
}
