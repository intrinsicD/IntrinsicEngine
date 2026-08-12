add_executable(IntrinsicCurvatureCorpusProbe
    curvature/IntrinsicCurvatureCorpusProbe.cpp
)

set_target_properties(
    IntrinsicCurvatureCorpusProbe
    PROPERTIES CXX_SCAN_FOR_MODULES ON
)
target_compile_features(IntrinsicCurvatureCorpusProbe PRIVATE cxx_std_23)
target_compile_options(
    IntrinsicCurvatureCorpusProbe PRIVATE ${INTRINSIC_COMPILE_FLAGS}
)
target_link_options(
    IntrinsicCurvatureCorpusProbe PRIVATE ${INTRINSIC_LINK_FLAGS}
)
target_link_libraries(
    IntrinsicCurvatureCorpusProbe
    PRIVATE IntrinsicConfig ExtrinsicCore IntrinsicGeometry
)
