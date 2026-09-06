# Opt-in curvature-patch profiler target.

add_executable(IntrinsicCurvaturePatchProfile EXCLUDE_FROM_ALL
    ../runners/CurvaturePatchProfileRunner.cpp
)
set_target_properties(
    IntrinsicCurvaturePatchProfile
    PROPERTIES CXX_SCAN_FOR_MODULES ON
)
target_compile_features(
    IntrinsicCurvaturePatchProfile PRIVATE cxx_std_23
)
target_compile_options(
    IntrinsicCurvaturePatchProfile PRIVATE ${INTRINSIC_COMPILE_FLAGS}
)
target_link_options(
    IntrinsicCurvaturePatchProfile PRIVATE ${INTRINSIC_LINK_FLAGS}
)
target_link_libraries(
    IntrinsicCurvaturePatchProfile
    PRIVATE ExtrinsicCore IntrinsicGeometry
)

add_executable(IntrinsicCurvatureBoundaryMesh EXCLUDE_FROM_ALL
    ../runners/CurvatureBoundaryMeshRunner.cpp)
set_target_properties(IntrinsicCurvatureBoundaryMesh PROPERTIES CXX_SCAN_FOR_MODULES ON)
target_compile_features(IntrinsicCurvatureBoundaryMesh PRIVATE cxx_std_23)
target_compile_options(IntrinsicCurvatureBoundaryMesh PRIVATE ${INTRINSIC_COMPILE_FLAGS})
target_link_options(IntrinsicCurvatureBoundaryMesh PRIVATE ${INTRINSIC_LINK_FLAGS})
target_link_libraries(IntrinsicCurvatureBoundaryMesh PRIVATE ExtrinsicCore IntrinsicGeometry)

add_executable(IntrinsicCurvatureExtremaMesh EXCLUDE_FROM_ALL
    ../runners/CurvatureExtremaMeshRunner.cpp)
set_target_properties(IntrinsicCurvatureExtremaMesh PROPERTIES CXX_SCAN_FOR_MODULES ON)
target_compile_features(IntrinsicCurvatureExtremaMesh PRIVATE cxx_std_23)
target_compile_options(IntrinsicCurvatureExtremaMesh PRIVATE ${INTRINSIC_COMPILE_FLAGS})
target_link_options(IntrinsicCurvatureExtremaMesh PRIVATE ${INTRINSIC_LINK_FLAGS})
target_link_libraries(IntrinsicCurvatureExtremaMesh PRIVATE ExtrinsicCore IntrinsicGeometry nlohmann_json::nlohmann_json)
