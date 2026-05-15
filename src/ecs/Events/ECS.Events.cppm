module;

export module Extrinsic.ECS.Events;

import Extrinsic.ECS.Scene.Handle;

export namespace Extrinsic::ECS::Events
{
    // Fired after the set of entities carrying Components::Selection::SelectedTag
    // changes. Producers fire one event per logical selection mutation
    // (replace/add/toggle/clear), not one per affected entity.
    struct SelectionChanged
    {
        EntityHandle Entity = InvalidEntityHandle; // InvalidEntityHandle = deselect-all
    };

    // Fired after the entity carrying Components::Selection::HoveredTag changes.
    struct HoverChanged
    {
        EntityHandle Entity = InvalidEntityHandle; // InvalidEntityHandle = hover cleared
    };

    // Fired after a new entity is added through a promoted scene creation
    // path (Scene::CreateDefault, EmplaceDefaults, asset instantiation).
    struct EntitySpawned
    {
        EntityHandle Entity = InvalidEntityHandle;
    };

    // Fired after a CPU-side geometry operator modifies mesh/graph/point-cloud
    // data on an entity. Components::DirtyTags::* remain the structured
    // per-domain record; this event is the coarse-grained notification.
    struct GeometryModified
    {
        EntityHandle Entity = InvalidEntityHandle;
    };
}
