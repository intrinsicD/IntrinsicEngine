module;

#include <cstdint>
#include <optional>

export module Geometry.MeshBuilder;

import Geometry.HalfedgeMesh;
import Geometry.Primitives;

export namespace Geometry::Halfedge
{
    [[nodiscard]] std::optional<Mesh> MakeMesh(const AABB& aabb) noexcept;
    [[nodiscard]] std::optional<Mesh> MakeMesh(const OBB& obb) noexcept;
    [[nodiscard]] std::optional<Mesh> MakeMesh(const Sphere& sphere, uint8_t subdiv_level = 3) noexcept;
    [[nodiscard]] std::optional<Mesh> MakeMesh(const Capsule& capsule) noexcept;
    [[nodiscard]] std::optional<Mesh> MakeMesh(const Cylinder& cylinder) noexcept;
    [[nodiscard]] std::optional<Mesh> MakeMesh(const Ellipsoid& ellipsoid) noexcept;
    [[nodiscard]] Mesh MakeMeshTetrahedron() noexcept;
    [[nodiscard]] Mesh MakeMeshIcosahedron() noexcept;
    [[nodiscard]] Mesh MakeMeshDodecahedron() noexcept;
    [[nodiscard]] Mesh MakeMeshOctahedron() noexcept;
}
