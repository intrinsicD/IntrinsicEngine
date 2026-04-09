module;

#include <cstddef>

export module ECS:Components.GeometrySources;

import Geometry.Properties;

export namespace ECS::Components::GeometrySources
{
    struct Vertices
    {
        Geometry::PropertySet Properties{};

        size_t NumDeleted{0};
    };

    struct Edges
    {
        Geometry::PropertySet Properties{};

        size_t NumDeleted{0};
    };

    struct Halfedges
    {
        Geometry::PropertySet Properties{};
    };

    struct Faces
    {
        Geometry::PropertySet Properties{};

        size_t NumDeleted{0};
    };

    struct Nodes
    {
        Geometry::PropertySet Properties{};

        size_t NumDeleted{0};
    };

    //TODO: these here should be the authoritive sources of cpu data for meshes, graphs and point clouds in the ecs.
    //TODO: use these for the respective entities so downstream algorithms can quickly discover vertices, edges, halfedges, faces or nodes for an entity.


}