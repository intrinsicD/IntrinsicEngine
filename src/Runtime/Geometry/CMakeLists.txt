set(target_name IntrinsicGeometry)
add_library(${target_name})

set_target_properties(${target_name} PROPERTIES CXX_SCAN_FOR_MODULES ON)
target_compile_options(${target_name} PRIVATE ${INTRINSIC_COMPILE_FLAGS})
target_compile_features(${target_name} PUBLIC cxx_std_23)
target_link_options(${target_name} PRIVATE ${INTRINSIC_LINK_FLAGS})

target_sources(${target_name}
        PUBLIC
        FILE_SET CXX_MODULES TYPE CXX_MODULES FILES
        Geometry.cppm
        Geometry.AABB.cppm
        Geometry.OBB.cppm
        Geometry.ContactManifold.cppm
        Geometry.Containment.cppm
        Geometry.GJK.cppm
        Geometry.EPA.cppm
        Geometry.Handle.cppm
        Geometry.Overlap.cppm
        Geometry.Primitives.cppm
        Geometry.Support.cppm
        Geometry.SDF.cppm
        Geometry.SDFContact.cppm
        Geometry.Validation.cppm
        Geometry.Properties.cppm
        Geometry.MeshUtils.cppm
        Geometry.Octree.cppm
        Geometry.KDTree.cppm
        Geometry.Raycast.cppm
        Geometry.HalfedgeMesh.cppm
        Geometry.Graph.cppm
        Geometry.DEC.cppm
        Geometry.Curvature.cppm
        Geometry.Smoothing.cppm
        Geometry.Simplification.cppm
        Geometry.Subdivision.cppm
        Geometry.Remeshing.cppm
        Geometry.Geodesic.cppm
        Geometry.CatmullClark.cppm
        Geometry.NormalEstimation.cppm
        Geometry.MeshRepair.cppm
        Geometry.MarchingCubes.cppm
        Geometry.SurfaceReconstruction.cppm
        Geometry.Parameterization.cppm
        Geometry.MeshQuality.cppm
        Geometry.AdaptiveRemeshing.cppm
        Geometry.ConvexHullBuilder.cppm
        PRIVATE
        FILE_SET geometry_impl TYPE CXX_MODULES FILES
        Geometry.MeshUtils.cpp
        Geometry.Raycast.cpp
        Geometry.EPA.cpp
        Geometry.HalfedgeMesh.cpp
        Geometry.Properties.cpp
        Geometry.Graph.cpp
        Geometry.Octree.cpp
        Geometry.KDTree.cpp
        Geometry.DEC.cpp
        Geometry.Curvature.cpp
        Geometry.Smoothing.cpp
        Geometry.Simplification.cpp
        Geometry.Subdivision.cpp
        Geometry.Remeshing.cpp
        Geometry.Geodesic.cpp
        Geometry.CatmullClark.cpp
        Geometry.NormalEstimation.cpp
        Geometry.MeshRepair.cpp
        Geometry.MarchingCubes.cpp
        Geometry.SurfaceReconstruction.cpp
        Geometry.Parameterization.cpp
        Geometry.MeshQuality.cpp
        Geometry.AdaptiveRemeshing.cpp
        Geometry.ConvexHullBuilder.cpp
)

target_include_directories(${target_name} PUBLIC .)

target_link_libraries(${target_name}
        PUBLIC
        IntrinsicCore
        glm::glm
)