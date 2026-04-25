# CTest owns BUILD_TESTING; top-level CMake decides when tests are added.
if(NOT BUILD_TESTING)
    return()
endif()

# Helper to run tests via CTest
include(GoogleTest)

# Try to locate a symbolizer so sanitizer reports have function/file/line.
find_program(INTRINSIC_LLVM_SYMBOLIZER
        NAMES llvm-symbolizer llvm-symbolizer-18 llvm-symbolizer-19 llvm-symbolizer-20 llvm-symbolizer-21 llvm-symbolizer-22
)

set(_intrinsic_test_env
        "LSAN_OPTIONS=suppressions=${CMAKE_SOURCE_DIR}/lsan.supp:fast_unwind_on_malloc=0"
        "ASAN_OPTIONS=symbolize=1:detect_leaks=1:fast_unwind_on_malloc=0"
)

if(INTRINSIC_LLVM_SYMBOLIZER)
    list(APPEND _intrinsic_test_env "ASAN_SYMBOLIZER_PATH=${INTRINSIC_LLVM_SYMBOLIZER}")
endif()

# =============================================================================
# Helper: create an OBJECT library for a group of test sources.
# Each .cpp is compiled exactly once; the objects are then linked into
# one or more executable targets.
# =============================================================================
function(intrinsic_test_object_lib name)
    cmake_parse_arguments(ARG "" "" "SOURCES;DEPS" ${ARGN})
    add_library(${name} OBJECT ${ARG_SOURCES})
    target_compile_options(${name} PRIVATE ${INTRINSIC_COMPILE_FLAGS})
    target_link_options(${name} PRIVATE ${INTRINSIC_LINK_FLAGS})
    set_target_properties(${name} PROPERTIES CXX_SCAN_FOR_MODULES ON)
    target_link_libraries(${name} PRIVATE IntrinsicConfig ${ARG_DEPS} GTest::gtest_main)
endfunction()

# =============================================================================
# Helper: create a test executable from one or more OBJECT libraries.
# =============================================================================
function(intrinsic_test_executable name)
    cmake_parse_arguments(ARG "" "" "OBJECTS;LINK_LIBS" ${ARGN})
    add_executable(${name})
    set_target_properties(${name} PROPERTIES CXX_SCAN_FOR_MODULES ON)
    target_compile_options(${name} PRIVATE ${INTRINSIC_COMPILE_FLAGS})
    target_link_options(${name} PRIVATE ${INTRINSIC_LINK_FLAGS})
    # Pull in pre-compiled object files from each OBJECT library
    foreach(obj ${ARG_OBJECTS})
        target_link_libraries(${name} PRIVATE ${obj})
    endforeach()
    target_link_libraries(${name} PRIVATE TestSupportObjs)
    # Link against the actual libraries for symbol resolution
    target_link_libraries(${name} PRIVATE IntrinsicConfig ${ARG_LINK_LIBS} GTest::gtest_main dl)
    gtest_discover_tests(${name}
        PROPERTIES
        ENVIRONMENT "${_intrinsic_test_env}"
        DISCOVERY_MODE POST_BUILD
    )
endfunction()

# =============================================================================
# OBJECT libraries — each test .cpp is compiled exactly once
# =============================================================================

# --- Core tests (pure algorithmic, no GPU, no ECS) ---
intrinsic_test_object_lib(CoreTestObjs
    SOURCES
        Test_BoundedHeap.cpp
        Test_CoreMemory.cpp
        Test_CoreTasks.cpp
        Test_CoreHandle.cpp
        Test_CoreCommands.cpp
        Test_CoreHash.cpp
        Test_CoreFrameGraph.cpp
        Test_FrameGraphTypeTokenHelper.cpp
        Test_ArchitectureSLO.cpp
        Test_DAGScheduler.cpp
        Test_FeatureRegistry.cpp
        Test_SystemFeatureCatalog.cpp
        Test_InplaceFunction.cpp
        Test_Benchmark.cpp
        Test_Profiling.cpp
        Test_CoreError.cpp
        Test_LogRingBuffer.cpp
    DEPS IntrinsicCore EnTT::EnTT
)

# --- ECS tests (FrameGraph system integration) ---
intrinsic_test_object_lib(ECSTestObjs
    SOURCES
        Test_RuntimeECS.cpp
        Test_FrameGraphSystems.cpp
        Test_CompileHotspotRefactors.cpp
        Test_EntityCommands.cpp
    DEPS IntrinsicCore IntrinsicECS IntrinsicGeometry
)

# --- Geometry tests (DEC, mesh algorithms, graphs — no GPU, no ECS) ---
intrinsic_test_object_lib(GeometryTestObjs
    SOURCES
        Test_RuntimeGeometry.cpp
        Test_RuntimeGraph.cpp
        Test_RuntimeGraphKNN.cpp
        Test_KDTree.cpp
        Test_Octree.cpp
        Test_BVH.cpp
        Test_DEC.cpp
        Test_MeshOperations.cpp
        Test_MeshBuilder.cpp
        Test_GeometryProcessing.cpp
        Test_SurfaceReconstruction.cpp
        Test_MeshQuality.cpp
        Test_MeshAnalysis.cpp
        Test_Parameterization.cpp
        Test_AdaptiveRemeshing.cpp
        Test_ConvexHull.cpp
        Test_PointCloud.cpp
        Test_SphereFitting.cpp
        Test_Boolean.cpp
        Test_MeshBoundary.cpp
        Test_GeometryAttributePropagation.cpp
        Test_HalfedgeMeshPropertyAccess.cpp
        Test_HalfedgeMeshTopology.cpp
        Test_HtexPatch.cpp
        Test_Validation.cpp
        Test_Curvature.cpp
        Test_Raycast.cpp
        Test_GeometryQueries.cpp
        Test_PcaPrimitives.cpp
        Test_Overlap.cpp
        Test_Grid.cpp
        Test_MarchingCubes.cpp
        Test_GJK.cpp
        Test_ImplicitPlaneField.cpp
        Test_SDF.cpp
        Test_Containment.cpp
        Test_Support.cpp
        Test_ContactManifold.cpp
        Test_Simplification.cpp
        Test_Geodesic.cpp
        Test_VectorHeatMethod.cpp
        Test_ShortestPath.cpp
        Test_Subdivision.cpp
        Test_Smoothing.cpp
        Test_NormalEstimation.cpp
        Test_Registration.cpp
        Test_PointCloudRobustness.cpp
        Test_SubmeshView.cpp
    DEPS IntrinsicCore IntrinsicGeometry
)

if(TARGET IntrinsicRuntime AND TARGET IntrinsicEditorUI)
    # --- Runtime tests (Graphics, I/O, RHI — need full Runtime) ---
    intrinsic_test_object_lib(RuntimeTestObjs
    SOURCES
        Test_CoreAssets.cpp
        Test_CoreAssetSafety.cpp
        Test_RuntimeGraphics.cpp
        Test_RuntimeRHI.cpp
        Test_RuntimeSelection.cpp
        Test_RuntimeSelection_Multi.cpp
        Test_ElementSelection.cpp
        Test_TextureMaterialHotReload.cpp
        Test_RuntimeCulling.cpp
        Test_GraphicsBackend.cpp
        Test_AssetPipeline.cpp
        Test_AssetIngestService.cpp
        Test_SceneManager.cpp
        Test_RenderOrchestrator.cpp
        Test_HeadlessEngine.cpp
        Test_IORegistry.cpp
        Test_Exporters.cpp
        Test_DebugDraw.cpp
        Test_DebugDrawVisualizations.cpp
        Test_EditorUI.cpp
        Test_PointCloudRenderPass.cpp
        Test_GraphRenderPass.cpp
        Test_RuntimeGeometry_Reuse.cpp
        Test_BDASharedBufferContract.cpp
        Test_PointCloudLifecycle.cpp
        Test_MeshViewLifecycle.cpp
        Test_GraphLifecycle.cpp
        Test_MeshRendererLifecycle.cpp
        Test_PerPassComponents.cpp
        Test_PerFaceAttributes.cpp
        Test_KMeansLabelRoundTrip.cpp
        Test_PropertySetDirtySync.cpp
        Test_SceneSerializer.cpp
        Test_TransformGizmo.cpp
        Test_CameraSpaceTransforms.cpp
        Test_CompositionAndValidation.cpp
        Test_ColorMapping.cpp
        Test_PropertyEnumerator.cpp
        Test_IsolineExtractor.cpp
        Test_EventBus.cpp
        Test_PanelRegistration.cpp
        Test_HeadlessAppSmoke.cpp
        Test_SelectionGizmoRegression.cpp
        Test_RenderUpdateIntegration.cpp
        Test_RenderExtraction.cpp
        Test_CudaDevice.cpp
        Test_PrimitiveBVHBuild.cpp
        Test_RenderGraphPackets.cpp
        Test_ImporterUtils.cpp
        Test_Importers.cpp
        Test_RenderPassContracts.cpp
        Test_RuntimeSystemBundles.cpp
        Test_RuntimeFrameLoop.cpp
        Test_RuntimeEngineConfig.cpp
        Test_MaintenanceLane.cpp
        Test_ShaderHotReload.cpp

        DEPS IntrinsicCore IntrinsicRuntime IntrinsicEditorUI nlohmann_json::nlohmann_json
    )
endif()

# Shared support for all test executables (sanitizer defaults, shared test hooks).
intrinsic_test_object_lib(TestSupportObjs
    SOURCES
        Test_SanitizerConfig.cpp
    DEPS IntrinsicCore
)

# =============================================================================
# Executable targets — link pre-compiled objects, no recompilation
# =============================================================================

if(TARGET RuntimeTestObjs)
    # --- Full suite (all tests) ---
    intrinsic_test_executable(IntrinsicTests
        OBJECTS  CoreTestObjs ECSTestObjs GeometryTestObjs RuntimeTestObjs
        LINK_LIBS IntrinsicRuntime IntrinsicEditorUI
    )
endif()

# --- Core-only (no Runtime, no ECS, no GPU) ---
intrinsic_test_executable(IntrinsicCoreTests
    OBJECTS  CoreTestObjs
    LINK_LIBS IntrinsicCore
)

# --- Core + ECS (FrameGraph system integration) ---
intrinsic_test_executable(IntrinsicECSTests
    OBJECTS  ECSTestObjs
    LINK_LIBS IntrinsicCore IntrinsicECS
)

# --- Core + Geometry (DEC, mesh algorithms — no GPU, no ECS) ---
intrinsic_test_executable(IntrinsicGeometryTests
    OBJECTS  GeometryTestObjs
    LINK_LIBS IntrinsicCore IntrinsicGeometry
)

if(TARGET IntrinsicAsset)
    add_subdirectory(Asset)
endif()
add_subdirectory(Core)
add_subdirectory(ECS)
if(TARGET IntrinsicGraphics)
    add_subdirectory(Graphics)
endif()
