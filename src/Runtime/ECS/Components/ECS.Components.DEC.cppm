module;

#include <memory>

export module ECS:Components.DEC;

import Geometry;

export namespace ECS::Components::DEC
{
    // Stores the full set of discrete exterior calculus operators computed
    // from a mesh entity's halfedge topology and geometry.
    //
    // Attach this component to any entity that has a mesh to cache its
    // DEC operators (exterior derivatives, Hodge stars, Laplacian).
    // The operators are heap-allocated via shared_ptr because DECOperators
    // contains vectors and EnTT requires components to be movable.
    //
    // Usage:
    //   auto& dec = registry.emplace<DEC::Component>(entity);
    //   dec.Operators = std::make_shared<Geometry::DEC::DECOperators>(
    //       Geometry::DEC::BuildOperators(mesh));
    //
    // When the mesh geometry changes, invalidate by re-computing:
    //   dec.Operators = std::make_shared<Geometry::DEC::DECOperators>(
    //       Geometry::DEC::BuildOperators(updatedMesh));

    struct Component
    {
        std::shared_ptr<Geometry::DEC::DECOperators> Operators;
    };

    // Tag: DEC operators need recomputation (mesh geometry changed).
    struct DirtyTag {};
}
