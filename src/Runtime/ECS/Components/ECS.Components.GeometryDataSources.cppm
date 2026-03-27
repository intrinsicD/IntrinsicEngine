module;

export module ECS:Components.GeometryDataSources;

import Geometry.Properties;

export namespace ECS::Components::GeometryDataSources
{
    struct VertexDataSource
    {
        Geometry::PropertySet Properties;
    };

    struct HalfedgeDataSource
    {
        Geometry::PropertySet Properties;
    };

    struct EdgeDataSource
    {
        Geometry::PropertySet Properties;
    };

    struct FaceDataSource
    {
        Geometry::PropertySet Properties;
    };

    struct NodeDataSource
    {
        Geometry::PropertySet Properties;
    };
}