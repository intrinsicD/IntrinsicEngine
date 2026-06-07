target_sources(${target_name}
        PUBLIC
        FILE_SET CXX_MODULES TYPE CXX_MODULES FILES
        ECS.Component.AssetInstance.cppm
        ECS.Component.Collider.cppm
        ECS.Component.Culling.Local.cppm
        ECS.Component.Culling.World.cppm
        ECS.Component.Culling.Proxy.cppm
        ECS.Component.DirtyTags.cppm
        ECS.Component.GeometrySources.cppm
        ECS.Component.GeometrySourcesPopulate.cppm
        ECS.Component.Hierarchy.cppm
        ECS.Component.Light.cppm
        ECS.Component.MetaData.cppm
        ECS.Component.ProceduralGeometryRef.cppm
        ECS.Component.RigidBody.cppm
        ECS.Component.Selection.cppm
        ECS.Component.ShadowCaster.cppm
        ECS.Component.SpatialDebugBinding.cppm
        ECS.Component.StableId.cppm
        ECS.Component.Transform.Local.cppm
        ECS.Component.Transform.World.cppm
        PRIVATE
        ECS.Component.Collider.cpp
        ECS.Component.GeometrySources.cpp
        ECS.Component.RigidBody.cpp
        ECS.Component.StableId.cpp
        ECS.Component.Transform.Local.cpp
        ECS.Component.GeometrySourcesPopulate.cpp
)

target_include_directories(${target_name} PUBLIC .)

# ExtrinsicECS link policy: only the dependencies actually imported by the
# component module surface are linked here. The promoted ECS dependency
# contract is `ecs -> {core, geometry}` (see /AGENTS.md §2). Components import
# `Geometry.*` types directly (Sphere, AABB, OBB, Octree, ConvexHull,
# Properties, plus HalfedgeMesh/Graph/PointCloud for the `GeometrySources`
# populate helpers) and use `glm` for transform math; they do not import any
# `Extrinsic.Asset.*`, `Extrinsic.RHI.*`, `Extrinsic.Graphics.*`,
# `Extrinsic.Runtime.*`, or `Extrinsic.Platform.*` modules. ExtrinsicCore is
# linked transitively via `Systems/CMakeLists.txt` for the
# `Extrinsic.Core.FrameGraph` import in `ECS.System.TransformHierarchy`.
target_link_libraries(${target_name} PRIVATE
        IntrinsicGeometry
        glm::glm)
