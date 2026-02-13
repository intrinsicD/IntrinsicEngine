module;

export module Geometry:Handle;
import Core.Handle;
import Core.ResourcePool;
import Core.Error;

export namespace Geometry
{
    struct GeometryTag {};

    using GeometryHandle = Core::StrongHandle<GeometryTag>;
}