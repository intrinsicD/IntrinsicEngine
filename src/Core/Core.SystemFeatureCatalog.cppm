module;
#include <string_view>

export module Core.SystemFeatureCatalog;

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

    inline constexpr Core::FeatureDescriptor PrimitiveBVHBuild = MakeFeatureDescriptor(
        "PrimitiveBVHBuild",
        Core::FeatureCategory::System,
        "Builds entity-attached primitive BVHs for local-space picking and future broadphase");

    inline constexpr Core::FeatureDescriptor GraphLifecycle = MakeFeatureDescriptor(
        "GraphLifecycle",
        Core::FeatureCategory::System,
        "Manages graph GPU resource lifecycle: uploads geometry, allocates GPUScene slots, populates render components");

    inline constexpr Core::FeatureDescriptor PointCloudLifecycle = MakeFeatureDescriptor(
        "PointCloudLifecycle",
        Core::FeatureCategory::System,
        "Manages point cloud GPU resource lifecycle: uploads geometry, allocates GPUScene slots, populates render components");

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

    // -------------------------------------------------------------------------
    // Pass Names — single source of truth for FrameGraph pass name strings.
    // Use these in RegisterSystem() calls instead of string literals to
    // prevent drift between feature catalog entries and graph pass names.
    // -------------------------------------------------------------------------
    namespace PassNames
    {
        inline constexpr std::string_view TransformUpdate       = "TransformUpdate";
        inline constexpr std::string_view PropertySetDirtySync  = "PropertySetDirtySync";
        inline constexpr std::string_view PrimitiveBVHBuild     = "PrimitiveBVHBuild";
        inline constexpr std::string_view GraphLifecycle        = "GraphLifecycle";
        inline constexpr std::string_view MeshRendererLifecycle = "MeshRendererLifecycle";
        inline constexpr std::string_view PointCloudLifecycle   = "PointCloudLifecycle";
        inline constexpr std::string_view MeshViewLifecycle     = "MeshViewLifecycle";
        inline constexpr std::string_view GPUSceneSync          = "GPUSceneSync";
    }
}
