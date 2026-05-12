module;

#include <array>
#include <cstdint>

export module Extrinsic.ECS.Component.ProceduralGeometryRef;

export namespace Extrinsic::ECS::Components
{
    enum class ProceduralGeometryKind : std::uint8_t
    {
        Triangle = 0,
    };

    struct ProceduralGeometryParams
    {
        std::uint32_t VertexCount = 0;
        std::uint32_t IndexCount = 0;
        std::array<float, 8> Payload{};
    };

    struct ProceduralGeometryRef
    {
        ProceduralGeometryKind Kind = ProceduralGeometryKind::Triangle;
        ProceduralGeometryParams Params{};
    };
}
