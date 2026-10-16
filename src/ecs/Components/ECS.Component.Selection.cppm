// Selectable, selected, and hovered tags; runtime/editor owns selection and picking.
module;

export module Extrinsic.ECS.Components.Selection;

export namespace Extrinsic::ECS::Components::Selection
{
    // Tag: entity can be selected/picked by editor tools.
    struct SelectableTag {};

    // Tag: the entity is currently selected.
    struct SelectedTag {};

    // Tag: the entity is currently hovered (mouse over).
    struct HoveredTag {};

}
