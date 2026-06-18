module;

export module Geometry.Handle;
import Extrinsic.Core.StrongHandle;

export namespace Geometry
{
    struct GeometryTag {};

    using GeometryHandle = Extrinsic::Core::StrongHandle<GeometryTag>;
}
