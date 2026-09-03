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
