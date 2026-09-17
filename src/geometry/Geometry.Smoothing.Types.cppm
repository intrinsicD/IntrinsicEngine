// Mesh-denoising status shared by algorithm results and copied editor diagnostics.
module;

#include <cstdint>

export module Geometry.Smoothing.Types;

export namespace Geometry::Smoothing
{
    // Success means the mesh was processed; every other value leaves it unmodified.
    enum class DenoiseStatus : std::uint8_t
    {
        Success,
        EmptyMesh,
        NonManifoldInput,
        DegenerateGeometry,
        NonFiniteInput,
        InvalidParams,
    };
}
