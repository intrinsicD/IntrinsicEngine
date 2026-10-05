// Selection data carriers: selectable/selected/hovered tags and explicit pick
// IDs. Runtime/editor owns selection semantics and picking.
module;

#include <cstdint>

export module Extrinsic.ECS.Components.Selection;

export namespace Extrinsic::ECS::Components::Selection
{
    // Tag: entity can be selected/picked by editor tools.
    struct SelectableTag {};

    // Tag: the entity is currently selected.
    struct SelectedTag {};

    // Tag: the entity is currently hovered (mouse over).
    struct HoveredTag {};

    // Optional: stable explicit pick id if you want to decouple from entt::entity values.
    // For now, we can pack entt::entity into the GPU and decode it back.
    struct PickID
    {
        uint32_t Value = 0;
    };
}

