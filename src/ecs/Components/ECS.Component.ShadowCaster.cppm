module;
export module Extrinsic.ECS.Component.ShadowCaster;

namespace Extrinsic::ECS::Components::Shadows
{
    struct CasterTag
    {
        // Marker component for shadow-casting geometry.
        // Presence of this component enables rendering into shadow maps.
        // Absence excludes the entity from shadow passes, even if it has
        // renderable geometry and materials.
    };
}