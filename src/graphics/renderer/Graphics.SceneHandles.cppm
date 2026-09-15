// GPU scene identities shared by snapshots and residency owners without storage dependencies.
export module Extrinsic.Graphics.SceneHandles;

import Extrinsic.Core.StrongHandle;

export namespace Extrinsic::Graphics
{
    struct GpuInstanceTag;
    struct GpuGeometryTag;
    using GpuInstanceHandle = Core::StrongHandle<GpuInstanceTag>;
    using GpuGeometryHandle = Core::StrongHandle<GpuGeometryTag>;
}
