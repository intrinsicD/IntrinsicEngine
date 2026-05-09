module;

#include <string_view>

export module Extrinsic.ECS.Scene.Bootstrap;

import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;

export namespace Extrinsic::ECS::Scene
{
    // Emplace the promoted default component contract on `entity`:
    //   - Components::MetaData{EntityName = name}
    //   - Components::Transform::Component (identity TRS)
    //   - Components::Transform::WorldMatrix (identity matrix)
    //   - Components::Hierarchy::Component (no parent / no children)
    //
    // Bootstrap intentionally does not emplace any dirty-transform tag; the
    // promoted TransformHierarchy system owns when to emit
    // Components::Transform::WorldUpdatedTag and Components::DirtyTags::DirtyTransform.
    // See HARDEN-060 contract decisions and HARDEN-061 for the system port.
    void EmplaceDefaults(Registry& registry, EntityHandle entity, std::string_view name);

    // Convenience: create an entity through the typed Registry surface and
    // emplace the default-component contract on it. Returns the new handle.
    [[nodiscard]] EntityHandle CreateDefault(Registry& registry, std::string_view name);
}
