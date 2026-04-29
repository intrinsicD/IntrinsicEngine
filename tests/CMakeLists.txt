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
        "LSAN_OPTIONS=suppressions=${CMAKE_SOURCE_DIR}/lsan.supp:fast_unwind_on_malloc=0:detect_leaks=0"
        "ASAN_OPTIONS=symbolize=1:detect_leaks=0:fast_unwind_on_malloc=0"
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
    target_include_directories(${name} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/support
        ${CMAKE_CURRENT_SOURCE_DIR}/support/geometry
    )
endfunction()

# =============================================================================
# Helper: create a test executable from one or more OBJECT libraries.
# =============================================================================
function(intrinsic_test_executable name)
    cmake_parse_arguments(ARG "NO_DISCOVER" "" "OBJECTS;LINK_LIBS;LABELS" ${ARGN})
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
    if(NOT ARG_NO_DISCOVER)
        set(_intrinsic_test_labels "${ARG_LABELS}")
        string(REPLACE ";" "\\\\\\\\;" _intrinsic_test_labels "${_intrinsic_test_labels}")
        gtest_discover_tests(${name}
            PROPERTIES
            ENVIRONMENT "${_intrinsic_test_env}"
            LABELS "${_intrinsic_test_labels}"
            DISCOVERY_MODE PRE_TEST
        )
        set_property(GLOBAL APPEND PROPERTY INTRINSIC_REGISTERED_TEST_TARGETS ${name})
        set(_intrinsic_encoded_labels "${ARG_LABELS}")
        string(REPLACE ";" "," _intrinsic_encoded_labels "${_intrinsic_encoded_labels}")
        set_property(GLOBAL APPEND PROPERTY INTRINSIC_REGISTERED_TEST_LABELS "${name}|${_intrinsic_encoded_labels}")
    endif()
endfunction()

# =============================================================================
# Helper: resolve test source paths from categorized directories.
# =============================================================================
function(intrinsic_resolve_test_sources out_var)
    cmake_parse_arguments(ARG "" "" "SEARCH_DIRS;FILES" ${ARGN})
    set(resolved_sources "")
    foreach(src_file IN LISTS ARG_FILES)
        set(found_source "")
        foreach(search_dir IN LISTS ARG_SEARCH_DIRS)
            if(search_dir STREQUAL ".")
                set(candidate "${CMAKE_CURRENT_SOURCE_DIR}/${src_file}")
            else()
                set(candidate "${CMAKE_CURRENT_SOURCE_DIR}/${search_dir}/${src_file}")
            endif()
            if(EXISTS "${candidate}")
                if(search_dir STREQUAL ".")
                    list(APPEND resolved_sources "${src_file}")
                else()
                    list(APPEND resolved_sources "${search_dir}/${src_file}")
                endif()
                set(found_source "yes")
                break()
            endif()
        endforeach()
        if(NOT found_source)
            message(FATAL_ERROR "Could not resolve test source '${src_file}' in search dirs: ${ARG_SEARCH_DIRS}")
        endif()
    endforeach()
    set(${out_var} "${resolved_sources}" PARENT_SCOPE)
endfunction()

# =============================================================================
# OBJECT libraries — each test .cpp is compiled exactly once
# =============================================================================

# --- Core tests (pure algorithmic, no GPU, no ECS) ---
set(_core_test_files
    Test_BoundedHeap.cpp
    Test_CoreMemory.cpp
    Test_CoreTasks.cpp
    Test_CoreHandle.cpp
    Test_CoreCommands.cpp
    Test_CoreHash.cpp
    Test_CoreFrameGraph.cpp
    Test_FrameGraphTypeTokenHelper.cpp
    Test_DAGScheduler.cpp
    Test_FeatureRegistry.cpp
    Test_SystemFeatureCatalog.cpp
    Test_InplaceFunction.cpp
    Test_Profiling.cpp
    Test_CoreError.cpp
    Test_LogRingBuffer.cpp
)
intrinsic_resolve_test_sources(_core_test_sources
    SEARCH_DIRS unit/core
    FILES ${_core_test_files}
)
intrinsic_test_object_lib(CoreTestObjs
    SOURCES
        ${_core_test_sources}
    DEPS IntrinsicCore EnTT::EnTT
)

# --- ECS tests (FrameGraph system integration) ---
set(_ecs_test_files
    Test_RuntimeECS.cpp
    Test_FrameGraphSystems.cpp
    Test_EntityCommands.cpp
)
intrinsic_resolve_test_sources(_ecs_test_sources
    SEARCH_DIRS unit/ecs contract/ecs integration/runtime
    FILES ${_ecs_test_files}
)
intrinsic_test_object_lib(ECSTestObjs
    SOURCES
        ${_ecs_test_sources}
    DEPS IntrinsicCore IntrinsicECS IntrinsicGeometry
)

# --- Contract/build checks ---
set(_contract_build_test_files
    Test_CompileHotspotRefactors.cpp
)
intrinsic_resolve_test_sources(_contract_build_test_sources
    SEARCH_DIRS contract/build
    FILES ${_contract_build_test_files}
)
intrinsic_test_object_lib(ContractBuildTestObjs
    SOURCES
        ${_contract_build_test_sources}
    DEPS IntrinsicCore IntrinsicECS IntrinsicGeometry
)

# --- Benchmark/SLO checks ---
set(_benchmark_test_files
    Test_ArchitectureSLO.cpp
    Test_Benchmark.cpp
)
intrinsic_resolve_test_sources(_benchmark_test_sources
    SEARCH_DIRS benchmark/slo benchmark
    FILES ${_benchmark_test_files}
)
intrinsic_test_object_lib(BenchmarkTestObjs
    SOURCES
        ${_benchmark_test_sources}
    DEPS IntrinsicCore EnTT::EnTT
)

# --- Geometry tests (DEC, mesh algorithms, graphs — no GPU, no ECS) ---
set(_geometry_test_files
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
)
intrinsic_resolve_test_sources(_geometry_test_sources
    SEARCH_DIRS unit/geometry integration/geometry regression/numerical
    FILES ${_geometry_test_files}
)
intrinsic_test_object_lib(GeometryTestObjs
    SOURCES
        ${_geometry_test_sources}
    DEPS IntrinsicCore IntrinsicGeometry
)

# --- Relocated HARDEN-041 wrapper sources: assets/core/ecs/graphics/runtime taxonomy ---
set(_asset_unit_test_files
    Test_Asset_EventBus.cpp
    Test_Asset_PathIndex.cpp
    Test_Asset_PayloadStore.cpp
    Test_Asset_Registry.cpp
)
intrinsic_resolve_test_sources(_asset_unit_test_sources
    SEARCH_DIRS unit/assets
    FILES ${_asset_unit_test_files}
)
intrinsic_test_object_lib(AssetUnitTestObjs
    SOURCES
        ${_asset_unit_test_sources}
    DEPS IntrinsicCore IntrinsicAssets
)

set(_core_wrapper_unit_test_files
    Test_Core_CallbackRegistry.cpp
    Test_Core_Config.cpp
    Test_Core_DagScheduler.cpp
    Test_Core_ErrorLegacy.cpp
    Test_Core_Filesystem.cpp
    Test_Core_GraphCompiler.cpp
    Test_Core_HandleLease.cpp
    Test_Core_HashLegacy.cpp
    Test_Core_LockFreeQueue.cpp
    Test_Core_Logging.cpp
    Test_Core_MemoryLegacy.cpp
    Test_Core_ResourcePool.cpp
    Test_Core_StrongHandleLegacy.cpp
    Test_Core_TaskGraphLegacy.cpp
    Test_Core_TasksLegacy.cpp
)
intrinsic_resolve_test_sources(_core_wrapper_unit_test_sources
    SEARCH_DIRS unit/core
    FILES ${_core_wrapper_unit_test_files}
)
intrinsic_test_object_lib(CoreWrapperUnitTestObjs
    SOURCES
        ${_core_wrapper_unit_test_sources}
    DEPS IntrinsicCore
)

set(_graphics_unit_test_files
    Test_RHI_BufferManager.cpp
    Test_RHI_PipelineManager.cpp
    Test_RHI_SamplerManager.cpp
    Test_RHI_TextureManager.cpp
)
intrinsic_resolve_test_sources(_graphics_unit_test_sources
    SEARCH_DIRS unit/graphics
    FILES ${_graphics_unit_test_files}
)
intrinsic_test_object_lib(GraphicsUnitTestObjs
    SOURCES
        ${_graphics_unit_test_sources}
    DEPS IntrinsicCore IntrinsicRuntime
)

set(_graphics_contract_test_files
    Test_MinimalTriangleAcceptance.cpp
    Test_RHI_CommandContextBarriers.cpp
)
intrinsic_resolve_test_sources(_graphics_contract_test_sources
    SEARCH_DIRS contract/graphics
    FILES ${_graphics_contract_test_files}
)
intrinsic_test_object_lib(GraphicsContractTestObjs
    SOURCES
        ${_graphics_contract_test_sources}
    DEPS IntrinsicCore IntrinsicRuntime
)

if(TARGET IntrinsicRuntime AND TARGET IntrinsicEditorUI)
    # --- Runtime tests (Graphics, I/O, RHI — need full Runtime) ---
    set(_runtime_test_files
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
        Test_CoreAssets.cpp
        Test_CoreAssetSafety.cpp
    )
    intrinsic_resolve_test_sources(_runtime_test_sources
        SEARCH_DIRS
            unit/core
            unit/assets
            unit/geometry
            unit/runtime
            unit/graphics
            integration/runtime
            integration/app
            integration/graphics
            contract/runtime
            contract/ui
            contract/graphics
            regression
            gpu
        FILES ${_runtime_test_files}
    )
    intrinsic_test_object_lib(RuntimeTestObjs
        SOURCES
            ${_runtime_test_sources}
        DEPS IntrinsicCore IntrinsicRuntime IntrinsicEditorUI nlohmann_json::nlohmann_json
    )

    set(_runtime_integration_test_files
        Test_AssetLoadPipeline.cpp
        Test_AssetService.cpp
        Test_CoreFrameGraphParallel.cpp
        Test_CoreGraphInterfaces.cpp
        Test_CoreGraphStress.cpp
        Test_CoreProcess.cpp
        Test_RuntimeEngineLayering.cpp
        Test_RuntimeStreamingExecutor.cpp
        Test_GpuWorldAndCulling.cpp
        Test_RenderGraphLegacy.cpp
        Test_RendererLegacy.cpp
    )
    intrinsic_resolve_test_sources(_runtime_integration_test_sources
        SEARCH_DIRS integration/runtime contract/runtime integration/graphics
        FILES ${_runtime_integration_test_files}
    )
    intrinsic_test_object_lib(RuntimeIntegrationTestObjs
        SOURCES
            ${_runtime_integration_test_sources}
        DEPS IntrinsicCore IntrinsicRuntime IntrinsicEditorUI
    )
endif()

# Shared support for all test executables (sanitizer defaults, shared test hooks).
intrinsic_test_object_lib(TestSupportObjs
    SOURCES
        support/Test_SanitizerConfig.cpp
    DEPS IntrinsicCore
)

# =============================================================================
# Executable targets — link pre-compiled objects, no recompilation
# =============================================================================

if(TARGET RuntimeTestObjs)
    # --- Full suite link target (build-only; category targets own CTest registration) ---
    intrinsic_test_executable(IntrinsicTests
        OBJECTS  CoreTestObjs ECSTestObjs ContractBuildTestObjs BenchmarkTestObjs GeometryTestObjs RuntimeTestObjs
        LINK_LIBS IntrinsicRuntime IntrinsicEditorUI
        NO_DISCOVER
    )

    # --- Runtime/headless coverage ---
    # RuntimeTestObjs currently mixes CPU-side runtime contracts with Vulkan-backed
    # headless graphics tests. Label the executable conservatively as gpu/vulkan
    # until HARDEN-041 splits test sources into finer taxonomy targets.
    intrinsic_test_executable(IntrinsicRuntimeTests
        OBJECTS RuntimeTestObjs
        LINK_LIBS IntrinsicRuntime IntrinsicEditorUI
        LABELS integration runtime headless graphics gpu vulkan
    )

    intrinsic_test_executable(IntrinsicRuntimeIntegrationTests
        OBJECTS RuntimeIntegrationTestObjs
        LINK_LIBS IntrinsicRuntime IntrinsicEditorUI
        LABELS integration runtime graphics gpu vulkan
    )
endif()

# --- Core-only (no Runtime, no ECS, no GPU) ---
intrinsic_test_executable(IntrinsicCoreTests
    OBJECTS  CoreTestObjs
    LINK_LIBS IntrinsicCore
    LABELS unit core
)

# --- Core + ECS (FrameGraph system integration) ---
intrinsic_test_executable(IntrinsicECSTests
    OBJECTS  ECSTestObjs
    LINK_LIBS IntrinsicCore IntrinsicECS
    LABELS unit ecs
)

# --- Contract/build checks ---
intrinsic_test_executable(IntrinsicContractBuildTests
    OBJECTS  ContractBuildTestObjs
    LINK_LIBS IntrinsicCore IntrinsicECS
    LABELS contract build
)

# --- Benchmark and SLO checks ---
intrinsic_test_executable(IntrinsicBenchmarkTests
    OBJECTS  BenchmarkTestObjs
    LINK_LIBS IntrinsicCore
    LABELS benchmark slo
)

# --- Core + Geometry (DEC, mesh algorithms — no GPU, no ECS) ---
intrinsic_test_executable(IntrinsicGeometryTests
    OBJECTS  GeometryTestObjs
    LINK_LIBS IntrinsicCore IntrinsicGeometry
    LABELS unit geometry
)

intrinsic_test_executable(IntrinsicAssetUnitTests
    OBJECTS AssetUnitTestObjs
    LINK_LIBS IntrinsicCore IntrinsicAssets
    LABELS unit assets
)

intrinsic_test_executable(IntrinsicCoreWrapperUnitTests
    OBJECTS CoreWrapperUnitTestObjs
    LINK_LIBS IntrinsicCore
    LABELS unit core
)

intrinsic_test_executable(IntrinsicGraphicsUnitTests
    OBJECTS GraphicsUnitTestObjs
    LINK_LIBS IntrinsicRuntime
    LABELS unit graphics gpu vulkan
)

intrinsic_test_executable(IntrinsicGraphicsContractTests
    OBJECTS GraphicsContractTestObjs
    LINK_LIBS IntrinsicRuntime
    LABELS contract graphics gpu vulkan
)

# Old wrapper directories remain on disk only as empty compatibility folders.
# Their relocated taxonomy sources are now compiled/registered via categorized targets.

get_property(_intrinsic_registered_test_labels GLOBAL PROPERTY INTRINSIC_REGISTERED_TEST_LABELS)
if(_intrinsic_registered_test_labels)
    set(_intrinsic_label_fixup_script "# Generated by tests/CMakeLists.txt. Reapply full CTest label lists after GoogleTest discovery.\n")
    foreach(_intrinsic_label_entry IN LISTS _intrinsic_registered_test_labels)
        string(REPLACE "|" ";" _intrinsic_label_parts "${_intrinsic_label_entry}")
        list(GET _intrinsic_label_parts 0 _intrinsic_label_target)
        list(REMOVE_AT _intrinsic_label_parts 0)
        list(JOIN _intrinsic_label_parts "" _intrinsic_label_value)
        string(REPLACE "," ";" _intrinsic_label_value "${_intrinsic_label_value}")
        string(APPEND _intrinsic_label_fixup_script
            "if(DEFINED ${_intrinsic_label_target}_TESTS)\n"
            "  set_tests_properties($\{${_intrinsic_label_target}_TESTS} PROPERTIES LABELS \"${_intrinsic_label_value}\")\n"
            "endif()\n"
        )
    endforeach()
    set(_intrinsic_label_fixup_file "${CMAKE_CURRENT_BINARY_DIR}/IntrinsicTestLabelFixups.cmake")
    file(GENERATE OUTPUT "${_intrinsic_label_fixup_file}" CONTENT "${_intrinsic_label_fixup_script}")
    set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES "${_intrinsic_label_fixup_file}")
endif()

if(NOT TARGET IntrinsicTests)
    # Some CI configurations intentionally skip RuntimeTestObjs; keep a stable
    # aggregate build target for agent/CI verification commands.
    add_custom_target(IntrinsicTests)
endif()

if(TARGET RuntimeTestObjs)
    get_property(_intrinsic_registered_test_targets GLOBAL PROPERTY INTRINSIC_REGISTERED_TEST_TARGETS)
    if(TARGET IntrinsicTests AND _intrinsic_registered_test_targets)
        add_dependencies(IntrinsicTests ${_intrinsic_registered_test_targets})
    endif()
endif()
