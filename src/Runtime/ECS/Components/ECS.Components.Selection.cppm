module;
#include <cstdint>

export module ECS:Components.Selection;

export namespace ECS::Components::Selection
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

