// Canonical property value kinds for descriptors without property storage templates.
module;
#include <cstdint>
export module Geometry.Properties.Types;

export namespace Geometry
{
    enum class PropertyValueKind : std::uint8_t
    {
        Unknown,
        Bool,
        Int32,
        UInt32,
        UInt64,
        Float,
        Double,
        Vec2,
        Vec3,
        Vec4
    };
}
