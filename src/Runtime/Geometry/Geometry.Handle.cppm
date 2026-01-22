module;

export module Geometry:Handle;
import Core;

export namespace Geometry
{
    struct GeometryTag {};

    using GeometryHandle = Core::StrongHandle<GeometryTag>;
}