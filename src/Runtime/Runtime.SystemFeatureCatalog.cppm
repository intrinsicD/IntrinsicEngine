module;

export module Runtime.SystemFeatureCatalog;

import Core.FeatureRegistry;

export namespace Runtime::SystemFeatureCatalog
{
    using Core::MakeFeatureDescriptor;

    inline constexpr Core::FeatureDescriptor TransformUpdate = MakeFeatureDescriptor(
        "TransformUpdate",
        Core::FeatureCategory::System,
        "Propagates local transforms to world matrices");

    inline constexpr Core::FeatureDescriptor MeshRendererLifecycle = MakeFeatureDescriptor(
        "MeshRendererLifecycle",
        Core::FeatureCategory::System,
        "Allocates/deallocates GPU slots for mesh renderers");

    inline constexpr Core::FeatureDescriptor PrimitiveBVHSync = MakeFeatureDescriptor(
        "PrimitiveBVHSync",
        Core::FeatureCategory::System,
        "Builds entity-attached primitive BVHs for local-space picking and future broadphase");

    inline constexpr Core::FeatureDescriptor GraphGeometrySync = MakeFeatureDescriptor(
        "GraphGeometrySync",
        Core::FeatureCategory::System,
        "Uploads graph geometry to GPU and allocates GPUScene slots");

    inline constexpr Core::FeatureDescriptor PointCloudGeometrySync = MakeFeatureDescriptor(
        "PointCloudGeometrySync",
        Core::FeatureCategory::System,
        "Uploads point clouds to GPU and allocates GPUScene slots");

    inline constexpr Core::FeatureDescriptor MeshViewLifecycle = MakeFeatureDescriptor(
        "MeshViewLifecycle",
        Core::FeatureCategory::System,
        "Creates GPU edge/vertex views from mesh via ReuseVertexBuffersFrom");

    inline constexpr Core::FeatureDescriptor GPUSceneSync = MakeFeatureDescriptor(
        "GPUSceneSync",
        Core::FeatureCategory::System,
        "Synchronizes CPU entity data to GPU scene buffers");

    inline constexpr Core::FeatureDescriptor PropertySetDirtySync = MakeFeatureDescriptor(
        "PropertySetDirtySync",
        Core::FeatureCategory::System,
        "Syncs PropertySet dirty domains to GPU buffers (per-domain incremental)");
}
