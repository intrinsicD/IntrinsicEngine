# CTest owns BUILD_TESTING; top-level CMake decides when tests are added.
if(NOT BUILD_TESTING)
    return()
endif()

# Helper to run tests via CTest
include(GoogleTest)
include(${CMAKE_SOURCE_DIR}/cmake/CompileShaders.cmake)

# Try to locate a symbolizer so sanitizer reports have function/file/line.
find_program(INTRINSIC_LLVM_SYMBOLIZER
        NAMES llvm-symbolizer llvm-symbolizer-18 llvm-symbolizer-19 llvm-symbolizer-20 llvm-symbolizer-21 llvm-symbolizer-22
)

# BUG-083: prefer the diagnosed stable symbolizer for the full Vulkan process.
# The generic host symbolizer may point at a newer LLVM than the configured
# compiler and has hung while resolving unloaded proprietary-driver frames.
find_program(INTRINSIC_LSAN_SYMBOLIZER
        NAMES llvm-symbolizer-18 llvm-symbolizer-20 llvm-symbolizer-19 llvm-symbolizer-21 llvm-symbolizer-22 llvm-symbolizer
)

set(_intrinsic_test_env
        "LSAN_OPTIONS=fast_unwind_on_malloc=0:detect_leaks=0"
        "ASAN_OPTIONS=symbolize=1:detect_leaks=0:fast_unwind_on_malloc=0"
)

if(INTRINSIC_LLVM_SYMBOLIZER)
    list(APPEND _intrinsic_test_env "ASAN_SYMBOLIZER_PATH=${INTRINSIC_LLVM_SYMBOLIZER}")
endif()

set(_intrinsic_default_test_timeout_seconds 30)
set(_intrinsic_slow_test_timeout_seconds 120)

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
    set(_intrinsic_test_timeout ${_intrinsic_default_test_timeout_seconds})
    list(FIND ARG_LABELS slow _intrinsic_slow_label_index)
    if(NOT _intrinsic_slow_label_index EQUAL -1)
        set(_intrinsic_test_timeout ${_intrinsic_slow_test_timeout_seconds})
    endif()
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
    intrinsic_register_test_executable(
        TARGET ${name}
        LABELS ${ARG_LABELS}
        OBJECTS ${ARG_OBJECTS}
    )
    if(NOT ARG_NO_DISCOVER)
        set_target_properties(${name} PROPERTIES
            INTRINSIC_GTEST_DISCOVERED TRUE
        )
        # CMake versions serialize list-valued discovery properties
        # differently. The generated fixup below is their canonical writer.
        gtest_discover_tests(${name}
            PROPERTIES
            TIMEOUT ${_intrinsic_test_timeout}
            DISCOVERY_MODE PRE_TEST
        )
    endif()
endfunction()

# =============================================================================
# Helper: register one grouped CTest process for a NO_DISCOVER GoogleTest target.
# =============================================================================
function(intrinsic_grouped_test name target)
    cmake_parse_arguments(ARG "" "FILTER" "LABELS" ${ARGN})
    if(NOT TARGET "${target}")
        message(FATAL_ERROR
            "Grouped test '${name}' references missing target '${target}'"
        )
    endif()
    get_target_property(
        _intrinsic_grouped_target_discovered
        "${target}"
        INTRINSIC_GTEST_DISCOVERED
    )
    if(_intrinsic_grouped_target_discovered)
        message(FATAL_ERROR
            "Grouped test '${name}' cannot duplicate discovered target '${target}'"
        )
    endif()
    intrinsic_validate_test_labels(
        CONTEXT "Grouped test '${name}'"
        LABELS ${ARG_LABELS}
    )

    set(
        _intrinsic_grouped_report_dir
        "${CMAKE_BINARY_DIR}/reports/grouped-ctest/gtest"
    )
    file(MAKE_DIRECTORY "${_intrinsic_grouped_report_dir}")
    add_test(NAME ${name}
        COMMAND $<TARGET_FILE:${target}>
            "--gtest_filter=${ARG_FILTER}"
            "--gtest_output=xml:${_intrinsic_grouped_report_dir}/${target}.xml"
    )
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "${_intrinsic_test_env}"
        LABELS "${ARG_LABELS}"
        PROCESSORS 1
        TIMEOUT ${_intrinsic_slow_test_timeout_seconds}
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    )
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
    Test_IndexedHeap.cpp
    Test.CoreError.cpp
    Test.LogRingBuffer.cpp
)
intrinsic_resolve_test_sources(_core_test_sources
    SEARCH_DIRS unit/core
    FILES ${_core_test_files}
)
intrinsic_test_object_lib(CoreTestObjs
    SOURCES
        ${_core_test_sources}
    DEPS ExtrinsicCore
)

# --- ECS tests (FrameGraph system integration) ---
set(_ecs_test_files
    Test.ECS.SceneRegistry.cpp
    Test.ECS.SceneBootstrap.cpp
    Test.ECS.Hierarchy.cpp
    Test.ECS.TransformHierarchy.cpp
    Test.ECS.BoundsPropagation.cpp
    Test.ECS.ColliderAuthoring.cpp
    Test.ECS.RenderSync.cpp
    Test.ECS.Events.cpp
    Test.ECS.GeometryDirtyDomains.cpp
    Test.ECS.GeometrySourcesPopulate.cpp
    Test.ECS.StableIdentity.cpp
)
intrinsic_resolve_test_sources(_ecs_test_sources
    SEARCH_DIRS unit/ecs contract/ecs integration/runtime
    FILES ${_ecs_test_files}
)
intrinsic_test_object_lib(ECSTestObjs
    SOURCES
        ${_ecs_test_sources}
    DEPS ExtrinsicCore ExtrinsicECS IntrinsicGeometry
)

# --- ECS contract tests (CPU-only structural checks over src/ecs sources) ---
# Reads ECS source files via std::filesystem to enforce the
# ecs -> {core, geometry} dependency contract from /AGENTS.md §2 and to keep
# canonical components free of higher-layer handles or solver-owned state.
set(_ecs_contract_test_files
    Test.ECS.LayeringBoundaries.cpp
)
intrinsic_resolve_test_sources(_ecs_contract_test_sources
    SEARCH_DIRS contract/ecs
    FILES ${_ecs_contract_test_files}
)
intrinsic_test_object_lib(EcsContractTestObjs
    SOURCES
        ${_ecs_contract_test_sources}
    DEPS ExtrinsicCore
)

# --- Runtime contract tests (CPU-only; GRAPHICS-030-Impl-A procedural geometry) ---
# These tests exercise runtime-owned value types (procedural geometry descriptor,
# cache, and packer) directly. They do not require IRenderer, GLFW, or Vulkan,
# so they live in a dedicated CPU-only contract executable rather than the
    # broader runtime/graphics integration targets.
if(TARGET ExtrinsicRuntime)
    set(_runtime_unit_test_files
        Test.MethodFigureExport.cpp
        Test.RegistrationAlignment.cpp
    )
    intrinsic_resolve_test_sources(_runtime_unit_test_sources
        SEARCH_DIRS unit/runtime
        FILES ${_runtime_unit_test_files}
    )
    intrinsic_test_object_lib(RuntimeUnitTestObjs
        SOURCES
            ${_runtime_unit_test_sources}
        DEPS ExtrinsicRuntime nlohmann_json::nlohmann_json glm::glm
    )

    set(_runtime_contract_test_files
        Test.CoreGraphInterfaces.cpp
        Test.GpuAssetCacheFallbackBootstrap.cpp
        Test.AsyncBufferReadback.cpp
        Test.KMeansGpuBackend.cpp
        Test.GpuReadbackJob.cpp
        Test.AssetGeometryIO.cpp
        Test.AssetIngestStateMachine.cpp
        Test.AssetImportFormatCoverage.cpp
        Test.AssetMeshNormals.cpp
        Test.AssetModelSceneHandoff.cpp
        Test.AssetModelTextureHandoff.cpp
        Test.AssetModelTextureIO.cpp
        Test.AsyncWorkService.cpp
        Test.DerivedJobGraph.cpp
        Test.EditorCommandHistory.cpp
        Test.EditorPropertyWidgets.cpp
        Test.EditorUiHost.cpp
        Test.EditorWindowRegistry.cpp
        Test.ImGuiAdapter.cpp
        Test.ImGuiAdapterEngineWiring.cpp
        Test.KMeansBackend.cpp
        Test.MeshGeometryExtraction.cpp
        Test.MeshAttributeTextureBake.cpp
        Test.MeshGeometryPacker.cpp
        Test.MeshPrimitiveViewPacker.cpp
        Test.MeshPrimitiveViewExtraction.cpp
        Test.ObjectSpaceNormalBakeService.cpp
        Test.ObjectSpaceNormalBakeQueue.cpp
        Test.ObjectSpaceNormalBakeSubmission.cpp
        Test.ProgressivePoissonGpuBackend.cpp
        Test.ProgressivePresentationExtraction.cpp
        Test.ProgressiveRenderData.cpp
        Test.RenderArtifactPublication.cpp
        Test.RenderExtractionContract.cpp
        Test.GizmoInteraction.cpp
        Test.GizmoInteractionEngineWiring.cpp
        Test.GraphGeometryExtraction.cpp
        Test.GraphGeometryPacker.cpp
        Test.GeometryAvailability.cpp
        Test.PointCloudGeometryExtraction.cpp
        Test.PointCloudGeometryPacker.cpp
        Test.ParameterizationFacade.cpp
        Test.PrimitiveSelectionRefinement.cpp
        Test.PrimitiveSelectionRefinementWiring.cpp
        Test.ProceduralGeometryCache.cpp
        Test.ProceduralGeometryExtraction.cpp
        Test.RuntimeCameraControllers.cpp
        Test.RuntimeConfigControlFacade.cpp
        Test.RuntimeEngineConfigBoot.cpp
        Test.RuntimeEngineLayering.cpp
        Test.RuntimeEnginePrivateGlue.cpp
        Test.RuntimeInputActions.cpp
        Test.RuntimeRenderRecipeActivation.cpp
        Test.RuntimeCameraFocusCommand.cpp
        Test.ClusteringModule.cpp
        Test.RuntimeCommandBus.cpp
        Test.RuntimeJobService.cpp
        Test.RuntimeKernelEvents.cpp
        Test.RuntimeModule.cpp
        Test.RuntimeWorldRegistry.cpp
        Test.RuntimeEcsSystemBundle.cpp
        Test.RuntimeReferenceScene.cpp
        Test.RuntimeSceneLifecycle.cpp
        Test.RuntimeSceneSerialization.cpp
        Test.RuntimeVulkanBreadcrumb.cpp
        Test.SandboxEditorModels.cpp
        Test.SandboxEditorSceneCommands.cpp
        Test.SandboxEditorClusteringMethods.cpp
        Test.SandboxEditorMeshMethods.cpp
        Test.SandboxEditorVisualization.cpp
        Test.SandboxEditorSessionLifecycle.cpp
        Test.SelectedMeshTextureBake.cpp
        Test.SelectionController.cpp
        Test.SelectionReadbackCorrelation.cpp
        Test.SelectionSnapshotExtraction.cpp
        Test.SelectionStableLookupComposition.cpp
        Test.SpatialDebugAdapters.cpp
        Test.SpatialDebugClosestFace.cpp
        Test.StableEntityLookup.cpp
        Test.VertexAttributeBinding.cpp
        Test.VertexChannelStreams.cpp
        Test.RenderWorldPool.cpp
        Test.RenderWorldPoolEngineWiring.cpp
        Test.VisualizationAdapters.cpp
    )
    intrinsic_resolve_test_sources(_runtime_contract_test_sources
        SEARCH_DIRS contract/runtime
        FILES ${_runtime_contract_test_files}
    )
    # imgui_core_lib (backend-agnostic ImGui, no GLFW/Vulkan) gives
    # Test.ImGuiAdapter.cpp the imgui.h include path to synthesize a panel draw.
    intrinsic_test_object_lib(RuntimeContractTestObjs
        SOURCES
            ${_runtime_contract_test_sources}
        DEPS ExtrinsicRuntime IntrinsicProgressivePoissonReference imgui_core_lib nlohmann_json::nlohmann_json
    )

    intrinsic_resolve_test_sources(_runtime_contract_slow_test_sources
        SEARCH_DIRS contract/runtime
        FILES Test.AssetImportFormatCoverageSlow.cpp
    )
    intrinsic_test_object_lib(RuntimeContractSlowTestObjs
        SOURCES
            ${_runtime_contract_slow_test_sources}
        DEPS ExtrinsicRuntime
    )
endif()

# --- Sandbox editor app/runtime integration (ARCH-006) ---
if(TARGET ExtrinsicSandboxEditor)
    intrinsic_resolve_test_sources(_sandbox_editor_integration_test_sources
        SEARCH_DIRS integration/runtime
        FILES
            Test.SandboxDomainPanels.cpp
            Test.SandboxEditorPresentation.cpp
            Test.SandboxParameterizationPanel.cpp
    )
    intrinsic_test_object_lib(SandboxEditorIntegrationTestObjs
        SOURCES
            ${_sandbox_editor_integration_test_sources}
        DEPS ExtrinsicSandboxEditor ExtrinsicRuntime imgui_core_lib
    )
endif()

# --- Benchmark/SLO checks ---
set(_benchmark_test_files
    Test.ArchitectureSLO.cpp
)
intrinsic_resolve_test_sources(_benchmark_test_sources
    SEARCH_DIRS benchmark/slo benchmark
    FILES ${_benchmark_test_files}
)
intrinsic_test_object_lib(BenchmarkTestObjs
    SOURCES
        ${_benchmark_test_sources}
    DEPS ExtrinsicCore
)

# --- Physics method reference tests (CPU-only; methods/ package, no runtime/ECS) ---
# Test.PhysicsSolverParity.cpp (PHYSICS-003) compares the promoted
# `Extrinsic.Physics.World` solver against the canonical METHOD-001 CPU
# reference, so this object lib also depends on ExtrinsicPhysics.
set(_physics_method_test_files
    Test.RigidBodyReference.cpp
    Test.ParticleSpringReference.cpp
    Test.XpbdClothReference.cpp
    Test.SphFluidReference.cpp
    Test.PhysicsSolverParity.cpp
)
intrinsic_resolve_test_sources(_physics_method_test_sources
    SEARCH_DIRS unit/physics
    FILES ${_physics_method_test_files}
)
intrinsic_test_object_lib(PhysicsMethodTestObjs
    SOURCES
        ${_physics_method_test_sources}
        ${CMAKE_SOURCE_DIR}/methods/physics/rigid_body_reference/src/RigidBodyReference.cpp
        ${CMAKE_SOURCE_DIR}/methods/physics/particle_spring_reference/src/ParticleSpringReference.cpp
        ${CMAKE_SOURCE_DIR}/methods/physics/xpbd_cloth_reference/src/XpbdClothReference.cpp
        ${CMAKE_SOURCE_DIR}/methods/physics/sph_fluid_reference/src/SphFluidReference.cpp
    DEPS ExtrinsicCore ExtrinsicPhysics
)
target_include_directories(PhysicsMethodTestObjs PRIVATE
    ${CMAKE_SOURCE_DIR}/methods/physics/rigid_body_reference/include
    ${CMAKE_SOURCE_DIR}/methods/physics/particle_spring_reference/include
    ${CMAKE_SOURCE_DIR}/methods/physics/xpbd_cloth_reference/include
    ${CMAKE_SOURCE_DIR}/methods/physics/sph_fluid_reference/include
)

# --- Physics world tests (CPU-only promoted src/physics contract) ---
set(_physics_world_test_files
    Test.Physics.World.cpp
)
intrinsic_resolve_test_sources(_physics_world_test_sources
    SEARCH_DIRS unit/physics
    FILES ${_physics_world_test_files}
)
intrinsic_test_object_lib(PhysicsWorldTestObjs
    SOURCES
        ${_physics_world_test_sources}
    DEPS ExtrinsicPhysics ExtrinsicCore IntrinsicGeometry
)

# --- Runtime physics bridge tests (runtime-owned ECS <-> physics composition) ---
if(TARGET ExtrinsicRuntime)
    set(_runtime_physics_bridge_test_files
        Test.RuntimePhysicsBridge.cpp
    )
    intrinsic_resolve_test_sources(_runtime_physics_bridge_test_sources
        SEARCH_DIRS integration/runtime
        FILES ${_runtime_physics_bridge_test_files}
    )
    intrinsic_test_object_lib(RuntimePhysicsBridgeTestObjs
        SOURCES
            ${_runtime_physics_bridge_test_sources}
        DEPS ExtrinsicRuntime ExtrinsicPhysics ExtrinsicECS ExtrinsicCore glm::glm
    )
endif()

# --- Geometry tests (DEC, mesh algorithms, graphs — no GPU, no ECS) ---
set(_geometry_test_files
    Test_Statistics.cpp
    Test_RobustKernels.cpp
    Test_AABBCubify.cpp
    Test_BezierCurve.cpp
    Test_Curve.cpp
    Test_Rotation.cpp
    Test_RotationAveraging.cpp
    Test_MeshQuantities.cpp
    Test_MeshClosestFace.cpp
    Test_LinalgEigenMap.cpp
    Test_RuntimeGraph.cpp
    Test_RuntimeGraphKNN.cpp
    Test.GraphQueries.cpp
    Test.GeometryProperties.cpp
    Test_KDTree.cpp
    Test_Octree.cpp
    Test_BVH.cpp
    Test_DEC.cpp
    Test_MeshOperations.cpp
    Test.MeshConversion.cpp
    Test.MeshSoup.cpp
    Test.PointCloudConversion.cpp
    Test_MeshBuilder.cpp
    Test_GeometryProcessing.cpp
    Test_SurfaceReconstruction.cpp
    Test_MeshQuality.cpp
    Test_MeshAnalysis.cpp
    Test_Parameterization.cpp
    Test.ParameterizationDiagnostics.cpp
    Test.HarmonicParameterization.cpp
    Test.ParameterizationDispatch.cpp
    Test.BoundaryFirstFlattening.cpp
    Test.UvAtlas.cpp
    Test_AdaptiveRemeshing.cpp
    Test_ConvexHull.cpp
    Test_PointCloud.cpp
    Test_SphereFitting.cpp
    Test_TriangleMetrics.cpp
    Test_Boolean.cpp
    Test_MeshBoundary.cpp
    Test_GeometryAttributePropagation.cpp
    Test.GraphVertexNormals.cpp
    Test.HalfedgeMeshVertexNormals.cpp
    Test_HalfedgeMeshPropertyAccess.cpp
    Test_HalfedgeMeshTopology.cpp
    Test.MeshTopologyUtilities.cpp
    Test_Validation.cpp
    Test_Curvature.cpp
    Test.CurvatureTensor.cpp
    Test_Raycast.cpp
    Test_GeometryQueries.cpp
    Test_PcaPrimitives.cpp
    Test_Overlap.cpp
    Test_Grid.cpp
    Test_MarchingCubes.cpp
    Test_ImplicitPlaneField.cpp
    Test_Containment.cpp
    Test_Support.cpp
    Test_Simplification.cpp
    Test_Geodesic.cpp
    Test_VectorHeatMethod.cpp
    Test_ShortestPath.cpp
    Test_Subdivision.cpp
    Test_Smoothing.cpp
    Test.QualityMetrics.cpp
    Test.Sampling.cpp
    Test.SurfaceSampling.cpp
    Test.PointCloudNormals.cpp
    Test.PointCloudFeatures.cpp
    Test_Registration.cpp
    Test_PointCloudRobustness.cpp
    Test.PointCloudOutlierRemoval.cpp
    Test.IntersectionClassification.cpp
    Test.RaycastClassify.cpp
    Test.RobustPredicates.cpp
    Test.LinearAlgebra.cpp
    Test.Sparse.cpp
    Test.SparseFactorization.cpp
    Test.SparseNonsymmetric.cpp
    Test.SignedHeatMethod.cpp
    Test_SubmeshView.cpp
    Test.SubmeshViewDomainBorrows.cpp
)
intrinsic_resolve_test_sources(_geometry_test_sources
    SEARCH_DIRS unit/geometry integration/geometry regression/numerical
    FILES ${_geometry_test_files}
)
intrinsic_test_object_lib(GeometryTestObjs
    SOURCES
        ${_geometry_test_sources}
    DEPS ExtrinsicCore IntrinsicGeometry
)
target_compile_definitions(GeometryTestObjs PRIVATE
    INTRINSIC_TEST_SUPPORT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/support"
)

# These tests exercise production paths that write process-global telemetry or
# logging state, so they remain individually discovered.
intrinsic_resolve_test_sources(_geometry_process_state_test_sources
    SEARCH_DIRS unit/geometry
    FILES
        Test_RuntimeGeometry.cpp
        Test_HtexPatch.cpp
        Test_SDF.cpp
        Test_ContactManifold.cpp
        Test_GJK.cpp
)
intrinsic_test_object_lib(GeometryProcessStateTestObjs
    SOURCES
        ${_geometry_process_state_test_sources}
    DEPS ExtrinsicCore IntrinsicGeometry
)
target_compile_definitions(GeometryProcessStateTestObjs PRIVATE
    INTRINSIC_TEST_SUPPORT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/support"
)

# Geometry IO owns process-static temporary-path counters and filesystem
# fixtures, so it remains individually discovered in grouped configurations.
intrinsic_resolve_test_sources(_geometry_io_test_sources
    SEARCH_DIRS unit/geometry
    FILES Test.GeometryIO.cpp
)
intrinsic_test_object_lib(GeometryIoTestObjs
    SOURCES
        ${_geometry_io_test_sources}
    DEPS ExtrinsicCore IntrinsicGeometry
)
target_compile_definitions(GeometryIoTestObjs PRIVATE
    INTRINSIC_TEST_SUPPORT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/support"
)

intrinsic_resolve_test_sources(_geometry_slow_test_sources
    SEARCH_DIRS unit/geometry
    FILES Test.MeshOperationsSlow.cpp
)
intrinsic_test_object_lib(GeometrySlowTestObjs
    SOURCES
        ${_geometry_slow_test_sources}
    DEPS ExtrinsicCore IntrinsicGeometry
)
target_compile_definitions(GeometrySlowTestObjs PRIVATE
    INTRINSIC_TEST_SUPPORT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/support"
)

# --- Geometry method reference tests (CPU-only; methods/ package, no runtime/ECS) ---
# METHOD-012 geometry.progressive_poisson CPU reference. The method package is
# hermetic standard C++, so the object lib compiles its source directly and does
# not link the geometry module.
set(_geometry_method_test_files
    Test.ProgressivePoissonReference.cpp
)
intrinsic_resolve_test_sources(_geometry_method_test_sources
    SEARCH_DIRS unit/geometry
    FILES ${_geometry_method_test_files}
)
intrinsic_test_object_lib(GeometryMethodTestObjs
    SOURCES
        ${_geometry_method_test_sources}
    DEPS ExtrinsicCore IntrinsicProgressivePoissonReference
)

intrinsic_resolve_test_sources(_geometry_method_slow_test_sources
    SEARCH_DIRS unit/geometry
    FILES Test.ProgressivePoissonReferenceSlow.cpp
)
intrinsic_test_object_lib(GeometryMethodSlowTestObjs
    SOURCES
        ${_geometry_method_slow_test_sources}
    DEPS ExtrinsicCore IntrinsicProgressivePoissonReference
)

# --- Relocated HARDEN-041 wrapper sources: assets/core/ecs/graphics/runtime taxonomy ---
set(_asset_unit_test_files
    Test.Asset.EventBus.cpp
    Test.Asset.GeometryIOBridge.cpp
    Test.Asset.ImportRouter.cpp
    Test.Asset.ModelTextureIOBridge.cpp
    Test.Asset.ModelTexturePayload.cpp
    Test.Asset.OperationStatus.cpp
    Test.Asset.PathIndex.cpp
    Test.Asset.PayloadStore.cpp
    Test.Asset.Registry.cpp
)
intrinsic_resolve_test_sources(_asset_unit_test_sources
    SEARCH_DIRS unit/assets
    FILES ${_asset_unit_test_files}
)
intrinsic_test_object_lib(AssetUnitTestObjs
    SOURCES
        ${_asset_unit_test_sources}
    DEPS ExtrinsicAssets ExtrinsicCore
)

set(_core_wrapper_unit_test_files
    Test.Core.BoundedHeap.cpp
    Test.Core.CallbackRegistry.cpp
    Test.Core.Config.cpp
    Test.Core.EngineConfigLoad.cpp
    Test.Core.DagScheduler.cpp
    Test.Core.ErrorLegacy.cpp
    Test.Core.FrameClock.cpp
    Test.Core.Filesystem.cpp
    Test.CoreFrameGraph.cpp
    Test.CoreFrameGraphTypeTokenHelper.cpp
    Test.Core.GraphCompiler.cpp
    Test.Core.HandleLease.cpp
    Test.CoreHash.cpp
    Test.Core.LockFreeQueue.cpp
    Test.Core.Logging.cpp
    Test.CoreMemory.cpp
    Test.CoreProfiling.cpp
    Test.Core.ResourcePool.cpp
    Test.CoreStrongHandle.cpp
    Test.Core.TaskGraphLegacy.cpp
    Test.Core.TaskGraphCompletionLifetime.cpp
    Test.CoreTasks.cpp
)
intrinsic_resolve_test_sources(_core_wrapper_unit_test_sources
    SEARCH_DIRS unit/core
    FILES ${_core_wrapper_unit_test_files}
)
intrinsic_test_object_lib(CoreWrapperUnitTestObjs
    SOURCES
        ${_core_wrapper_unit_test_sources}
    DEPS ExtrinsicCore
)

set(_platform_test_files
    Test.NullPlatform.cpp
    Test.PlatformLayering.cpp
)
intrinsic_resolve_test_sources(_platform_test_sources
    SEARCH_DIRS unit/platform contract/platform
    FILES ${_platform_test_files}
)
intrinsic_test_object_lib(PlatformTestObjs
    SOURCES
        ${_platform_test_sources}
    DEPS ExtrinsicPlatform ExtrinsicCore
)
target_compile_definitions(PlatformTestObjs PRIVATE INTRINSIC_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

if(INTRINSIC_PLATFORM_BACKEND_SELECTED STREQUAL "Glfw")
    set(_platform_glfw_smoke_test_files
        Test.GlfwPlatformSmoke.cpp
    )
    intrinsic_resolve_test_sources(_platform_glfw_smoke_test_sources
        SEARCH_DIRS integration/platform
        FILES ${_platform_glfw_smoke_test_files}
    )
    intrinsic_test_object_lib(PlatformGlfwSmokeTestObjs
        SOURCES
            ${_platform_glfw_smoke_test_sources}
        DEPS ExtrinsicPlatform ExtrinsicCore
    )
endif()

if(TARGET ExtrinsicBackendsVulkan AND INTRINSIC_PLATFORM_BACKEND_SELECTED STREQUAL "Glfw")
    set(_graphics_vulkan_smoke_test_files
        Test.BufferReadbackGpuSmoke.cpp
        Test.ComputeParallelPrimitivesGpuSmoke.cpp
        Test.GpuTransferFacadeGpuSmoke.cpp
        Test.TextureReadbackGpuSmoke.cpp
        Test.DefaultRecipeSurfaceGpuSmoke.cpp
        Test.HzbOcclusionConservatismGpuSmoke.cpp
        Test.ImGuiSurfaceGpuSmoke.cpp
        Test.ObjectSpaceNormalTextureBakeGpuSmoke.cpp
        Test.TransientDebugSurfaceGpuSmoke.cpp
        Test.UvViewGpuSmoke.cpp
        Test.VisualizationOverlaySurfaceGpuSmoke.cpp
        Test.VulkanBootstrapSmoke.cpp
    )
    intrinsic_resolve_test_sources(_graphics_vulkan_smoke_test_sources
        SEARCH_DIRS gpu integration/graphics
        FILES ${_graphics_vulkan_smoke_test_files}
    )
    intrinsic_test_object_lib(GraphicsVulkanSmokeTestObjs
        SOURCES
            ${_graphics_vulkan_smoke_test_sources}
        DEPS ExtrinsicRuntime ExtrinsicGraphics ExtrinsicBackendsVulkan ExtrinsicPlatform ExtrinsicRHI ExtrinsicCore imgui_core_lib
    )
endif()

# RUNTIME-095 Slice 3 — opt-in `gpu;vulkan;integration` working-sandbox present
# smoke. The fixture drives the runtime `Engine` over an ECS-authored
# acceptance scene and composes the app-owned Sandbox editor controller, so it
# links the editor plus runtime/graphics/Vulkan stack; it is gated on the
# promoted Vulkan backend + GLFW platform exactly like the graphics smokes
# above.
if(TARGET ExtrinsicBackendsVulkan AND INTRINSIC_PLATFORM_BACKEND_SELECTED STREQUAL "Glfw")
    set(_runtime_sandbox_acceptance_gpu_smoke_test_files
        Test.RuntimeSandboxAcceptanceGpuSmoke.cpp
    )
    intrinsic_resolve_test_sources(_runtime_sandbox_acceptance_gpu_smoke_test_sources
        SEARCH_DIRS integration/runtime
        FILES ${_runtime_sandbox_acceptance_gpu_smoke_test_files}
    )
    intrinsic_test_object_lib(RuntimeSandboxAcceptanceGpuSmokeTestObjs
        SOURCES
            ${_runtime_sandbox_acceptance_gpu_smoke_test_sources}
        DEPS ExtrinsicSandboxEditor ExtrinsicRuntime ExtrinsicGraphics ExtrinsicBackendsVulkan ExtrinsicPlatform ExtrinsicRHI ExtrinsicECS ExtrinsicCore glm::glm
    )
endif()

# GEOM-056 Slice D — opt-in `gpu;vulkan` KMeans backend parity smoke. Kept in
# its own target so KMeans parity can be selected without running broader
# runtime integration suites.
if(TARGET ExtrinsicRuntime AND TARGET ExtrinsicBackendsVulkan AND INTRINSIC_PLATFORM_BACKEND_SELECTED STREQUAL "Glfw")
    set(_runtime_kmeans_gpu_smoke_test_files
        Test.KMeansGpuBackendGpuSmoke.cpp
    )
    intrinsic_resolve_test_sources(_runtime_kmeans_gpu_smoke_test_sources
        SEARCH_DIRS integration/runtime
        FILES ${_runtime_kmeans_gpu_smoke_test_files}
    )
    intrinsic_test_object_lib(RuntimeKMeansGpuSmokeTestObjs
        SOURCES
            ${_runtime_kmeans_gpu_smoke_test_sources}
        DEPS ExtrinsicRuntime ExtrinsicGraphics ExtrinsicBackendsVulkan ExtrinsicPlatform ExtrinsicRHI ExtrinsicCore IntrinsicGeometry glm::glm
    )
endif()

set(_graphics_unit_test_files
    Test.RHI.BufferManager.cpp
    Test.RHI.SamplerManager.cpp
    Test.RHI.TextureManager.cpp
)
intrinsic_resolve_test_sources(_graphics_unit_test_sources
    SEARCH_DIRS unit/graphics
    FILES ${_graphics_unit_test_files}
)
intrinsic_test_object_lib(GraphicsUnitTestObjs
    SOURCES
        ${_graphics_unit_test_sources}
    DEPS ExtrinsicRHI ExtrinsicCore ExtrinsicPlatform
)

set(_graphics_rhi_cpu_unit_test_files
    Test.RHI.PipelineManager.cpp
    Test.RHI.PipelineRegistry.cpp
)
intrinsic_resolve_test_sources(_graphics_rhi_cpu_unit_test_sources
    SEARCH_DIRS unit/graphics
    FILES ${_graphics_rhi_cpu_unit_test_files}
)
intrinsic_test_object_lib(GraphicsRhiCpuUnitTestObjs
    SOURCES
        ${_graphics_rhi_cpu_unit_test_sources}
    DEPS ExtrinsicRHI ExtrinsicCore ExtrinsicPlatform
)

set(_graphics_buffer_transfer_test_files
    Test.BufferTransfer.cpp
)
intrinsic_resolve_test_sources(_graphics_buffer_transfer_test_sources
    SEARCH_DIRS contract/graphics
    FILES ${_graphics_buffer_transfer_test_files}
)
intrinsic_test_object_lib(GraphicsBufferTransferTestObjs
    SOURCES
        ${_graphics_buffer_transfer_test_sources}
    DEPS ExtrinsicRHI ExtrinsicCore
)

set(_graphics_renderer_cpu_unit_test_files
    Test.Graphics.ColormapSystem.cpp
    Test.Graphics.DebugViewSystem.cpp
    Test.Graphics.GpuSceneSlot.cpp
    Test.Graphics.ImGuiOverlaySystem.cpp
    Test.Graphics.MaterialSystem.cpp
    Test.Graphics.PostProcessSystem.cpp
    Test.RenderGraphDebugDump.cpp
    Test.Graphics.SpatialDebugVisualizers.cpp
    Test.Graphics.VisualizationPackets.cpp
)
intrinsic_resolve_test_sources(_graphics_renderer_cpu_unit_test_sources
    SEARCH_DIRS unit/graphics
    FILES ${_graphics_renderer_cpu_unit_test_files}
)
intrinsic_test_object_lib(GraphicsRendererCpuUnitTestObjs
    SOURCES
        ${_graphics_renderer_cpu_unit_test_sources}
    DEPS ExtrinsicGraphics ExtrinsicGraphicsAssets ExtrinsicAssets ExtrinsicRHI ExtrinsicCore ExtrinsicPlatform glm::glm
)

# Graphics-assets bridge tests — pure CPU mock-driven, no real GPU.
# Kept in a separate object lib + target because their asset/cache link closure
# differs from the RHI-manager MockDevice unit target.
set(_graphics_assets_unit_test_files
    Test.GpuAssetCache.cpp
)
intrinsic_resolve_test_sources(_graphics_assets_unit_test_sources
    SEARCH_DIRS unit/graphics
    FILES ${_graphics_assets_unit_test_files}
)
intrinsic_test_object_lib(GraphicsAssetsUnitTestObjs
    SOURCES
        ${_graphics_assets_unit_test_sources}
    DEPS ExtrinsicCore ExtrinsicAssets ExtrinsicRHI ExtrinsicPlatform ExtrinsicGraphicsAssets
)

set(_graphics_contract_test_files
    Test.GpuTransferFacade.cpp
    Test.MinimalTriangleAcceptance.cpp
    Test.ObjectSpaceNormalTextureBake.cpp
    Test.RHI.CommandContextBarriers.cpp
    Test.RendererRhiBoundary.cpp
    Test.TextureReadback.cpp
    Test.TextureUploadLayout.cpp
    Test.TransferQueueReadback.cpp
)
intrinsic_resolve_test_sources(_graphics_contract_test_sources
    SEARCH_DIRS contract/graphics
    FILES ${_graphics_contract_test_files}
)
intrinsic_test_object_lib(GraphicsContractTestObjs
    SOURCES
        ${_graphics_contract_test_sources}
    DEPS ExtrinsicGraphics ExtrinsicRHI ExtrinsicCore ExtrinsicPlatform glm::glm
)

if(TARGET ExtrinsicBackendsVulkan)
    # Constructor/fail-closed Vulkan coverage is CPU-only (no device, surface, or
    # swapchain creation) but can only be compiled in configurations that build
    # the Vulkan backend target. Keep labels contract;graphics so non-headless
    # CPU gates exercise the seam without making gpu/vulkan runtime tests
    # mandatory.
    intrinsic_resolve_test_sources(_graphics_vulkan_contract_test_sources
        SEARCH_DIRS contract/graphics
        FILES Test.VulkanFailClosedContract.cpp
              Test.VulkanOperationalStatusEvaluator.cpp
              Test.VulkanOperationalDiagnosticsSnapshot.cpp
    )
    intrinsic_test_object_lib(GraphicsVulkanContractTestObjs
        SOURCES
            ${_graphics_vulkan_contract_test_sources}
        DEPS ExtrinsicBackendsVulkan ExtrinsicRHI ExtrinsicCore ExtrinsicPlatform glm::glm
    )
endif()

set(_graphics_contract_cpu_test_files
    Test.CrossQueueTimeline.cpp
    Test.CurrentRendererContractAdapter.cpp
    Test.CullingPassContracts.cpp
    Test.DefaultRecipeBackbufferReadback.cpp
    Test.ComputeParallelPrimitives.cpp
    Test.DebugViewContract.cpp
    Test.DebugViewPass.cpp
    Test.FrameRecipeContract.cpp
    Test.GpuWorldCompactionContract.cpp
    Test.GpuWorldLifetimeContract.cpp
    Test.GpuWorldStoragePlanContract.cpp
    Test.HZBResourceLifetime.cpp
    Test.ImGuiPass.cpp
    Test.ImGuiPresentContract.cpp
    Test.LightingShadowContracts.cpp
    Test.LightClusterGrid.cpp
    Test.LinePointPassContracts.cpp
    Test.MaterialSubstitutionContract.cpp
    Test.MinimalTriangleReadbackHarness.cpp
    Test.OwnershipTransferBarriers.cpp
    Test.PostProcessChainContract.cpp
    Test.PresentPass.cpp
    Test.QueueAffinity.cpp
    Test.RHIResourceSlotRecycling.cpp
    Test.ReconstructionContract.cpp
    Test.RenderCommandRouter.cpp
    Test.RenderingContract.cpp
    Test.RenderRecipeConfig.cpp
    Test.RenderPrepPipeline.cpp
    Test.RenderSubsystemRegistry.cpp
    Test.RendererFrameLifecycle.cpp
    Test.RenderWorldContract.cpp
    Test.RenderGraphParallelRecording.cpp
    Test.RenderGraphValidation.cpp
    Test.SharedRenderRecipeExecution.cpp
    Test.SelectionPassContracts.cpp
    Test.SelectionSystemContracts.cpp
    Test.SurfacePassContracts.cpp
    Test.TransformSyncSystem.cpp
    Test.TransientDebugSurfacePass.cpp
    Test.UvView.cpp
    Test.VisualizationOverlayPass.cpp
    Test.VisualizationPropertyBufferResidency.cpp
)
intrinsic_resolve_test_sources(_graphics_contract_cpu_test_sources
    SEARCH_DIRS contract/graphics
    FILES ${_graphics_contract_cpu_test_files}
)
intrinsic_test_object_lib(GraphicsContractCpuTestObjs
    SOURCES
        ${_graphics_contract_cpu_test_sources}
    DEPS ExtrinsicGraphics ExtrinsicRHI ExtrinsicCore ExtrinsicPlatform glm::glm
)

set(_runtime_graphics_cpu_test_files
    Test.RuntimeRenderExtraction.cpp
    Test.RuntimeDeviceSelection.cpp
    Test.RuntimeFrameLoopContract.cpp
    Test.RenderWorldPoolPipelined.cpp
    Test.RuntimeSandboxAcceptance.cpp
)
intrinsic_resolve_test_sources(_runtime_graphics_cpu_test_sources
    SEARCH_DIRS integration/runtime contract/runtime
    FILES ${_runtime_graphics_cpu_test_files}
)
intrinsic_test_object_lib(RuntimeGraphicsCpuTestObjs
    SOURCES
        ${_runtime_graphics_cpu_test_sources}
    DEPS ExtrinsicRuntime ExtrinsicGraphics ExtrinsicRHI ExtrinsicECS ExtrinsicCore ExtrinsicPlatform glm::glm
)

# Promoted CPU integration checks that survived the legacy test-consumer
# cleanup. Their dependency set is runtime/assets/core only.
if(TARGET ExtrinsicRuntime)
    set(_runtime_integration_cpu_test_files
        Test.AssetLoadPipeline.cpp
        Test.AssetService.cpp
        Test.CoreFrameGraphParallel.cpp
        Test.CoreGraphStress.cpp
        Test.CoreProcess.cpp
        Test.RuntimeStreamingExecutor.cpp
    )
    intrinsic_resolve_test_sources(_runtime_integration_cpu_test_sources
        SEARCH_DIRS integration/runtime
        FILES ${_runtime_integration_cpu_test_files}
    )
    intrinsic_test_object_lib(RuntimeIntegrationCpuTestObjs
        SOURCES
            ${_runtime_integration_cpu_test_sources}
        DEPS ExtrinsicRuntime ExtrinsicAssets ExtrinsicCore
    )
endif()

# Mock-driven graphics integration tests require renderer/RHI symbols but no
# live graphics backend or Runtime link closure.
if(TARGET ExtrinsicGraphics)
    set(_graphics_integration_cpu_test_files
        Test.GpuWorldAndCulling.cpp
        Test.RenderGraphLegacy.cpp
        Test.RendererLegacy.cpp
    )
    intrinsic_resolve_test_sources(_graphics_integration_cpu_test_sources
        SEARCH_DIRS integration/graphics
        FILES ${_graphics_integration_cpu_test_files}
    )
    intrinsic_test_object_lib(GraphicsIntegrationCpuTestObjs
        SOURCES
            ${_graphics_integration_cpu_test_sources}
        DEPS ExtrinsicGraphics ExtrinsicRHI ExtrinsicCore ExtrinsicPlatform glm::glm
    )
endif()

# The one operational readback case owns the Vulkan/GLFW capability boundary.
if(TARGET ExtrinsicRuntime
   AND TARGET ExtrinsicBackendsVulkan
   AND INTRINSIC_PLATFORM_BACKEND_SELECTED STREQUAL "Glfw")
    intrinsic_resolve_test_sources(_runtime_gpu_readback_smoke_test_sources
        SEARCH_DIRS integration/runtime
        FILES Test.GpuReadbackJobGpuSmoke.cpp
    )
    intrinsic_test_object_lib(RuntimeGpuReadbackSmokeTestObjs
        SOURCES
            ${_runtime_gpu_readback_smoke_test_sources}
        DEPS ExtrinsicRuntime ExtrinsicGraphics ExtrinsicBackendsVulkan ExtrinsicPlatform ExtrinsicRHI ExtrinsicCore IntrinsicGeometry glm::glm
    )
endif()

# Shared support for all test executables (sanitizer defaults, shared test hooks).
intrinsic_test_object_lib(TestSupportObjs
    SOURCES
        support/Test_SanitizerConfig.cpp
    DEPS ExtrinsicCore
)

# =============================================================================
# Executable targets — link pre-compiled objects, no recompilation
# =============================================================================

if(TARGET RuntimeIntegrationCpuTestObjs)
    intrinsic_test_executable(IntrinsicRuntimeIntegrationTests
        OBJECTS RuntimeIntegrationCpuTestObjs
        LINK_LIBS ExtrinsicRuntime ExtrinsicAssets ExtrinsicCore
        LABELS integration runtime
    )
endif()

if(TARGET GraphicsIntegrationCpuTestObjs)
    intrinsic_test_executable(IntrinsicGraphicsIntegrationCpuTests
        OBJECTS GraphicsIntegrationCpuTestObjs
        LINK_LIBS ExtrinsicGraphics ExtrinsicRHI ExtrinsicCore ExtrinsicPlatform
        LABELS integration graphics
    )
endif()

# --- Core-only (no Runtime, no ECS, no GPU) ---
intrinsic_test_executable(IntrinsicCoreTests
    OBJECTS  CoreTestObjs
    LINK_LIBS ExtrinsicCore
    LABELS unit core
)

# --- Core + ECS (FrameGraph system integration) ---
intrinsic_test_executable(IntrinsicECSTests
    OBJECTS  ECSTestObjs
    LINK_LIBS ExtrinsicCore ExtrinsicECS
    LABELS unit ecs
)

# --- ECS contract tests (CPU-only structural checks) ---
intrinsic_test_executable(IntrinsicEcsContractTests
    OBJECTS  EcsContractTestObjs
    LINK_LIBS ExtrinsicCore
    LABELS contract ecs
)

# --- Runtime contract tests (CPU-only procedural geometry contracts) ---
if(TARGET RuntimeUnitTestObjs)
    intrinsic_test_executable(IntrinsicRuntimeUnitTests
        OBJECTS RuntimeUnitTestObjs
        LINK_LIBS ExtrinsicRuntime
        LABELS unit runtime
    )
endif()

if(TARGET RuntimeContractTestObjs)
    intrinsic_test_executable(IntrinsicRuntimeContractTests
        OBJECTS RuntimeContractTestObjs
        LINK_LIBS ExtrinsicRuntime IntrinsicProgressivePoissonReference
        LABELS contract runtime
    )
endif()

if(TARGET RuntimeContractSlowTestObjs)
    intrinsic_test_executable(IntrinsicRuntimeContractSlowTests
        OBJECTS RuntimeContractSlowTestObjs
        LINK_LIBS ExtrinsicRuntime
        LABELS contract runtime slow
    )
endif()

if(TARGET SandboxEditorIntegrationTestObjs)
    intrinsic_test_executable(IntrinsicSandboxEditorIntegrationTests
        OBJECTS SandboxEditorIntegrationTestObjs
        LINK_LIBS ExtrinsicSandboxEditor ExtrinsicRuntime
        LABELS integration runtime
    )
endif()

# --- Benchmark and SLO checks ---
intrinsic_test_executable(IntrinsicBenchmarkTests
    OBJECTS  BenchmarkTestObjs
    LINK_LIBS ExtrinsicCore
    LABELS benchmark slo slow
)

set(_intrinsic_pure_ctest_registration)
if(INTRINSIC_GROUP_PURE_CTEST)
    set(_intrinsic_pure_ctest_registration NO_DISCOVER)
endif()

# --- Physics method reference tests (CPU-only) ---
intrinsic_test_executable(IntrinsicPhysicsMethodTests
    OBJECTS PhysicsMethodTestObjs
    LINK_LIBS ExtrinsicCore ExtrinsicPhysics
    LABELS unit physics
    ${_intrinsic_pure_ctest_registration}
)
if(INTRINSIC_GROUP_PURE_CTEST)
    intrinsic_grouped_test(
        IntrinsicPhysicsMethodTests.Grouped
        IntrinsicPhysicsMethodTests
        FILTER "*"
        LABELS unit physics
    )
endif()

# --- Geometry method reference tests (CPU-only) ---
intrinsic_test_executable(IntrinsicGeometryMethodTests
    OBJECTS GeometryMethodTestObjs
    LINK_LIBS ExtrinsicCore IntrinsicProgressivePoissonReference
    LABELS unit geometry
    ${_intrinsic_pure_ctest_registration}
)
if(INTRINSIC_GROUP_PURE_CTEST)
    intrinsic_grouped_test(
        IntrinsicGeometryMethodTests.Grouped
        IntrinsicGeometryMethodTests
        FILTER "*"
        LABELS unit geometry
    )
endif()

intrinsic_test_executable(IntrinsicGeometryMethodSlowTests
    OBJECTS GeometryMethodSlowTestObjs
    LINK_LIBS ExtrinsicCore IntrinsicProgressivePoissonReference
    LABELS unit geometry slow
)

# --- Physics world tests (CPU-only) ---
intrinsic_test_executable(IntrinsicPhysicsWorldTests
    OBJECTS PhysicsWorldTestObjs
    LINK_LIBS ExtrinsicPhysics
    LABELS unit physics
    ${_intrinsic_pure_ctest_registration}
)
if(INTRINSIC_GROUP_PURE_CTEST)
    intrinsic_grouped_test(
        IntrinsicPhysicsWorldTests.Grouped
        IntrinsicPhysicsWorldTests
        FILTER "*"
        LABELS unit physics
    )
endif()

if(TARGET RuntimePhysicsBridgeTestObjs)
    intrinsic_test_executable(IntrinsicRuntimePhysicsBridgeTests
        OBJECTS RuntimePhysicsBridgeTestObjs
        LINK_LIBS ExtrinsicRuntime ExtrinsicPhysics ExtrinsicECS ExtrinsicCore
        LABELS integration runtime ecs physics
    )
endif()

# --- Core + Geometry (DEC, mesh algorithms — no GPU, no ECS) ---
intrinsic_test_executable(IntrinsicGeometryTests
    OBJECTS  GeometryTestObjs
    LINK_LIBS ExtrinsicCore IntrinsicGeometry
    LABELS unit geometry
    ${_intrinsic_pure_ctest_registration}
)
if(INTRINSIC_GROUP_PURE_CTEST)
    intrinsic_grouped_test(
        IntrinsicGeometryTests.Grouped
        IntrinsicGeometryTests
        FILTER "*"
        LABELS unit geometry
    )
endif()

intrinsic_test_executable(IntrinsicGeometryIoTests
    OBJECTS GeometryIoTestObjs
    LINK_LIBS ExtrinsicCore IntrinsicGeometry
    LABELS unit geometry
)

intrinsic_test_executable(IntrinsicGeometryProcessStateTests
    OBJECTS GeometryProcessStateTestObjs
    LINK_LIBS ExtrinsicCore IntrinsicGeometry
    LABELS unit geometry
)

intrinsic_test_executable(IntrinsicGeometrySlowTests
    OBJECTS GeometrySlowTestObjs
    LINK_LIBS ExtrinsicCore IntrinsicGeometry
    LABELS unit geometry slow
)

intrinsic_test_executable(IntrinsicAssetUnitTests
    OBJECTS AssetUnitTestObjs
    LINK_LIBS ExtrinsicAssets ExtrinsicCore
    LABELS unit assets
)

intrinsic_test_executable(IntrinsicCoreWrapperUnitTests
    OBJECTS CoreWrapperUnitTestObjs
    LINK_LIBS ExtrinsicCore
    LABELS unit core
)

intrinsic_test_executable(IntrinsicPlatformTests
    OBJECTS PlatformTestObjs
    LINK_LIBS ExtrinsicPlatform ExtrinsicCore
    LABELS unit contract platform headless
)

if(TARGET PlatformGlfwSmokeTestObjs)
    intrinsic_test_executable(IntrinsicPlatformGlfwSmokeTests
        OBJECTS PlatformGlfwSmokeTestObjs
        LINK_LIBS ExtrinsicPlatform ExtrinsicCore
        LABELS integration platform glfw vulkan
    )
endif()

# BUG-082: an intentionally standalone sanitizer process. It must not link
# TestSupportObjs because their default sanitizer options/suppressions would
# invalidate the leak-on negative control.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux"
   AND INTRINSIC_PLATFORM_BACKEND_SELECTED STREQUAL "Glfw")
    add_executable(IntrinsicGlfwLifecycleLsanProcess
        regression/platform/Test.GlfwLifecycleLsanProcess.cpp
    )
    set_target_properties(IntrinsicGlfwLifecycleLsanProcess PROPERTIES
        CXX_SCAN_FOR_MODULES ON
    )
    target_compile_options(IntrinsicGlfwLifecycleLsanProcess PRIVATE
        ${INTRINSIC_COMPILE_FLAGS}
    )
    target_link_options(IntrinsicGlfwLifecycleLsanProcess PRIVATE
        ${INTRINSIC_LINK_FLAGS}
        "LINKER:--wrap=glfwTerminate"
    )
    target_link_libraries(IntrinsicGlfwLifecycleLsanProcess PRIVATE
        IntrinsicConfig
        ExtrinsicPlatform
        ExtrinsicCore
    )
    intrinsic_register_test_executable(
        TARGET IntrinsicGlfwLifecycleLsanProcess
        LABELS regression platform glfw
    )

    add_test(NAME GlfwLifecycleLsan.EngineStaticTeardownAndLeakControl
        COMMAND ${CMAKE_COMMAND}
            -DPROBE_EXE=$<TARGET_FILE:IntrinsicGlfwLifecycleLsanProcess>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/regression/platform/Run.GlfwLifecycleLsanContract.cmake
    )
    set_tests_properties(
        GlfwLifecycleLsan.EngineStaticTeardownAndLeakControl
        PROPERTIES
            LABELS "regression;platform;glfw"
            SKIP_REGULAR_EXPRESSION "BUG082_SKIP:"
            TIMEOUT ${_intrinsic_default_test_timeout_seconds}
    )
endif()

if(TARGET GraphicsVulkanSmokeTestObjs)
    intrinsic_test_executable(IntrinsicGraphicsVulkanSmokeTests
        OBJECTS GraphicsVulkanSmokeTestObjs
        LINK_LIBS ExtrinsicRuntime ExtrinsicBackendsVulkan ExtrinsicPlatform ExtrinsicRHI ExtrinsicCore
        LABELS gpu vulkan graphics
    )
    intrinsic_add_glsl_shaders(IntrinsicGraphicsVulkanSmokeTests)
endif()

# RUNTIME-095 Slice 3 — opt-in `gpu;vulkan;integration` working-sandbox present smoke.
# LINK_LIBS unions the proven default-recipe GPU smoke set (Runtime + Vulkan
# backend + Platform/RHI/Core) with the ECS/Graphics symbols the acceptance-scene
# authoring needs (matching the `IntrinsicRuntimeGraphicsCpuTests` link set used
# by the Slice 1/2 CPU acceptance test).
if(TARGET RuntimeSandboxAcceptanceGpuSmokeTestObjs)
    intrinsic_test_executable(IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests
        OBJECTS RuntimeSandboxAcceptanceGpuSmokeTestObjs
        LINK_LIBS ExtrinsicSandboxEditor ExtrinsicRuntime ExtrinsicGraphics ExtrinsicBackendsVulkan ExtrinsicPlatform ExtrinsicRHI ExtrinsicECS ExtrinsicCore
        LABELS gpu vulkan integration runtime graphics
    )
    intrinsic_add_glsl_shaders(IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests)
endif()

if(TARGET RuntimeKMeansGpuSmokeTestObjs)
    intrinsic_test_executable(IntrinsicRuntimeKMeansGpuSmokeTests
        OBJECTS RuntimeKMeansGpuSmokeTestObjs
        LINK_LIBS ExtrinsicRuntime ExtrinsicGraphics ExtrinsicBackendsVulkan ExtrinsicPlatform ExtrinsicRHI ExtrinsicCore IntrinsicGeometry
        LABELS gpu vulkan integration runtime geometry
    )
    intrinsic_add_glsl_shaders(IntrinsicRuntimeKMeansGpuSmokeTests)
endif()

if(TARGET RuntimeGpuReadbackSmokeTestObjs)
    intrinsic_test_executable(IntrinsicRuntimeGpuReadbackSmokeTests
        OBJECTS RuntimeGpuReadbackSmokeTestObjs
        LINK_LIBS ExtrinsicRuntime ExtrinsicGraphics ExtrinsicBackendsVulkan ExtrinsicPlatform ExtrinsicRHI ExtrinsicCore IntrinsicGeometry
        LABELS gpu vulkan integration runtime graphics slow
    )
endif()

if(TARGET ExtrinsicSandbox
   AND INTRINSIC_RUNTIME_ENABLE_PROMOTED_VULKAN
   AND INTRINSIC_PLATFORM_BACKEND_SELECTED STREQUAL "Glfw")
    add_test(NAME ExtrinsicSandbox.FramePacingDiagnosticCapture
        COMMAND ${CMAKE_COMMAND}
            -DSANDBOX_EXE=$<TARGET_FILE:ExtrinsicSandbox>
            -DREPORT_PATH=${CMAKE_BINARY_DIR}/Testing/Temporary/extrinsic_sandbox_frame_pacing.json
            -DFRAME_COUNT=8
            -DMIN_SAMPLES=2
            -P ${CMAKE_SOURCE_DIR}/tools/diagnostics/validate_frame_pacing_capture.cmake
    )
    set_tests_properties(ExtrinsicSandbox.FramePacingDiagnosticCapture PROPERTIES
        ENVIRONMENT "${_intrinsic_test_env}"
        LABELS "gpu;vulkan;integration;runtime;graphics"
        TIMEOUT 180
    )

    # BUG-083: unlike the general fixture environment above, this subprocess
    # contract explicitly enables leak detection. The standalone BUG-082 helper
    # supplies an unsuppressed engine-owned negative control before the exact
    # five-frame Vulkan shutdown run.
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux"
       AND INTRINSIC_SANITIZER_HAS_ADDRESS
       AND TARGET IntrinsicGlfwLifecycleLsanProcess)
        if(NOT INTRINSIC_LSAN_SYMBOLIZER)
            message(FATAL_ERROR
                "BUG-083 requires llvm-symbolizer for narrow Vulkan leak suppressions"
            )
        endif()
        add_test(NAME ExtrinsicSandbox.VulkanShutdownLsanContract
            COMMAND ${CMAKE_COMMAND}
                -DSANDBOX_EXE=$<TARGET_FILE:ExtrinsicSandbox>
                -DLEAK_CONTROL_EXE=$<TARGET_FILE:IntrinsicGlfwLifecycleLsanProcess>
                -DREPORT_PATH=${CMAKE_BINARY_DIR}/Testing/Temporary/extrinsic_sandbox_vulkan_shutdown_lsan.json
                -DSUPPRESSIONS_PATH=${CMAKE_SOURCE_DIR}/lsan.supp
                -DSYMBOLIZER_PATH=${INTRINSIC_LSAN_SYMBOLIZER}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/regression/graphics/Run.VulkanShutdownLsanContract.cmake
        )
        set_tests_properties(ExtrinsicSandbox.VulkanShutdownLsanContract PROPERTIES
            LABELS "gpu;vulkan;regression;runtime;graphics"
            TIMEOUT 180
        )
    endif()
endif()

intrinsic_test_executable(IntrinsicGraphicsUnitTests
    OBJECTS GraphicsUnitTestObjs
    LINK_LIBS ExtrinsicRHI ExtrinsicCore ExtrinsicPlatform
    LABELS unit graphics
)

# CPU-only graphics tests — runs in the default CPU gate.
intrinsic_test_executable(IntrinsicGraphicsAssetsUnitTests
    OBJECTS GraphicsAssetsUnitTestObjs
    LINK_LIBS ExtrinsicCore ExtrinsicAssets ExtrinsicRHI ExtrinsicPlatform ExtrinsicGraphicsAssets
    LABELS unit graphics
)

intrinsic_test_executable(IntrinsicGraphicsRhiCpuUnitTests
    OBJECTS GraphicsRhiCpuUnitTestObjs
    LINK_LIBS ExtrinsicRHI ExtrinsicCore ExtrinsicPlatform
    LABELS unit graphics
)

intrinsic_test_executable(IntrinsicGraphicsBufferTransferTests
    OBJECTS GraphicsBufferTransferTestObjs
    LINK_LIBS ExtrinsicRHI ExtrinsicCore
    LABELS unit graphics
    ${_intrinsic_pure_ctest_registration}
)
if(INTRINSIC_GROUP_PURE_CTEST)
    intrinsic_grouped_test(
        IntrinsicGraphicsBufferTransferTests.Grouped
        IntrinsicGraphicsBufferTransferTests
        FILTER "*"
        LABELS unit graphics
    )
endif()
unset(_intrinsic_pure_ctest_registration)

intrinsic_test_executable(IntrinsicGraphicsRendererCpuUnitTests
    OBJECTS GraphicsRendererCpuUnitTestObjs
    LINK_LIBS ExtrinsicGraphics ExtrinsicRHI ExtrinsicCore ExtrinsicPlatform
    LABELS unit graphics
)
intrinsic_test_executable(IntrinsicGraphicsContractTests
    OBJECTS GraphicsContractTestObjs
    LINK_LIBS ExtrinsicGraphics ExtrinsicRHI ExtrinsicECS ExtrinsicCore ExtrinsicPlatform
    LABELS contract graphics
)

if(TARGET GraphicsVulkanContractTestObjs)
    intrinsic_test_executable(IntrinsicGraphicsVulkanContractTests
        OBJECTS GraphicsVulkanContractTestObjs
        LINK_LIBS ExtrinsicBackendsVulkan ExtrinsicRHI ExtrinsicCore ExtrinsicPlatform
        LABELS contract graphics
    )
endif()

intrinsic_test_executable(IntrinsicGraphicsContractCpuTests
    OBJECTS GraphicsContractCpuTestObjs
    LINK_LIBS ExtrinsicGraphics ExtrinsicRHI ExtrinsicCore ExtrinsicPlatform
    LABELS contract graphics
)

intrinsic_test_executable(IntrinsicRuntimeGraphicsCpuTests
    OBJECTS RuntimeGraphicsCpuTestObjs
    LINK_LIBS ExtrinsicRuntime ExtrinsicGraphics ExtrinsicRHI ExtrinsicECS ExtrinsicCore ExtrinsicPlatform
    LABELS integration runtime graphics
)

# Old wrapper directories remain on disk only as empty compatibility folders.
# Their relocated taxonomy sources are now compiled/registered via categorized targets.

# Exact CTest reservations for CPU cases that create more than one scheduler
# worker. Keep these case-scoped: producer-wide weights would serialize the
# thousands of neighboring tests that do not own a worker pool.
set(_intrinsic_multiworker_test_budgets
    "IntrinsicBenchmarkTests|ArchitectureSLO.FrameGraphP95P99BudgetsAt2000Nodes|8"
    "IntrinsicBenchmarkTests|ArchitectureSLO.TaskSchedulerLocalStealAndWakeCompletionBudgets|8"
    "IntrinsicCoreWrapperUnitTests|CoreTasks.BasicDispatch|3"
    "IntrinsicCoreWrapperUnitTests|CoreTasks.WaitForAllWakesForChildDispatchedAfterHelperRegisters|3"
    "IntrinsicCoreWrapperUnitTests|CoreTasks.WorkProgressTokenFromPriorSchedulerInstanceFailsClosed|3"
    "IntrinsicCoreWrapperUnitTests|CoreTasks.ContendedDispatchCompletes|4"
    "IntrinsicCoreWrapperUnitTests|CoreTasks.CoroutineDispatch|3"
    "IntrinsicCoreWrapperUnitTests|CoreTasks.NestedCoroutines|3"
    "IntrinsicCoreWrapperUnitTests|CoreTasks.CoroutinesActuallyYield|3"
    "IntrinsicCoreWrapperUnitTests|CoreTasks.CounterEventParksAndUnparksContinuation|3"
    "IntrinsicCoreWrapperUnitTests|CoreTasks.CounterEventMultipleWaitersResumeExactlyOnce|4"
    "IntrinsicCoreWrapperUnitTests|CoreTasks.CounterEventCanBeRearmedAfterReady|3"
    "IntrinsicCoreWrapperUnitTests|CoreTasks.CounterEventProgressObservedBeforeQueueCheckCannotLoseWake|3"
    "IntrinsicCoreWrapperUnitTests|CoreTasks.CounterEventResumeBeforeAwaitSuspendReturnsDoesNotTouchDestroyedFrame|4"
    "IntrinsicCoreWrapperUnitTests|CoreTasks.CounterEventReleaseReclaimsParkedFrameWithoutResuming|3"
    "IntrinsicCoreWrapperUnitTests|CoreTasks.SchedulerShutdownReclaimsParkedFrameWithoutResuming|3"
    "IntrinsicCoreWrapperUnitTests|CoreTasks.StaleWaitTokenUnparkDoesNotResumeNewWaiters|3"
    "IntrinsicCoreWrapperUnitTests|CoreTasks.WaitTokenFromPriorSchedulerInstanceCannotReleaseCurrentSlot|3"
    "IntrinsicCoreWrapperUnitTests|CoreTasks.CounterEventHighFanInRandomizedSignalsResumeExactlyOnce|4"
    "IntrinsicCoreWrapperUnitTests|CoreTasks.SchedulerStatsExposeQueueAndStealTelemetry|3"
    "IntrinsicCoreWrapperUnitTests|CoreTasks.SchedulerStatsCanBeExportedToFrameTelemetry|3"
    "IntrinsicCoreWrapperUnitTests|CoreTasks.UndispatchedJobDestruction_NoLeak|3"
    "IntrinsicCoreWrapperUnitTests|CoreTasks.JobMoveDoesNotDoubleFree|3"
    "IntrinsicCoreWrapperUnitTests|CoreTasks.JobMoveAssignment_CleansUpPrevious|3"
    "IntrinsicCoreWrapperUnitTests|CoreFrameGraph.ParallelReaders|4"
    "IntrinsicCoreWrapperUnitTests|CoreFrameGraph.ReadyQueueExecutionBypassesCoarseLayerBarrier|3"
    "IntrinsicCoreWrapperUnitTests|CoreFrameGraph.ParallelExecutionStress|4"
    "IntrinsicCoreWrapperUnitTests|CoreFrameGraph.ReadyQueueExecutionDoesNotWaitForSlowSiblingLayerBarrier|4"
    "IntrinsicCoreWrapperUnitTests|CoreFrameGraph.ReadyQueueNestedDispatchStressHighWorkerCount|4"
    "IntrinsicCoreWrapperUnitTests|CoreTaskGraph.MainThreadReadyQueueUsesPriorityAndCostOrdering|4"
    "IntrinsicCoreWrapperUnitTests|CoreTaskGraphCompletionLifetime.SubmittedGraphAllowsUnrelatedWorkBeforeDependencyOrderedWait|3"
    "IntrinsicCoreWrapperUnitTests|CoreTaskGraphCompletionLifetime.MainThreadPassesRunOnlyWhenOwnerPumps|3"
    "IntrinsicCoreWrapperUnitTests|CoreTaskGraphCompletionLifetime.ResetFailsClosedWhileSubmissionIsLive|3"
    "IntrinsicCoreWrapperUnitTests|CoreTaskGraphCompletionLifetime.WorkerCompletionWakesOwnerForMainThreadSuccessor|3"
    "IntrinsicCoreWrapperUnitTests|CoreTaskGraphCompletionLifetime.WaitWakesForChildDispatchedAfterHelperRegisters|3"
    "IntrinsicCoreWrapperUnitTests|CoreTaskGraphCompletionLifetime.WorkerOwnedWaitWakesForChildDispatchedAfterHelperRegisters|3"
    "IntrinsicCoreWrapperUnitTests|CoreTaskGraphCompletionLifetime.NonOwnerCanPollCopiedCompletionUntilReady|4"
    "IntrinsicCoreWrapperUnitTests|CoreTaskGraphCompletionLifetime.PendingWorkerSubmissionFailsClosedAfterSchedulerReplacement|3"
    "IntrinsicCoreWrapperUnitTests|CoreTaskGraphCompletionLifetime.SubmittedIndependentPassesRunInParallel|4"
    "IntrinsicCoreWrapperUnitTests|CoreTaskGraphCompletionLifetime.SubmitKeepsCompletionStateAliveUntilWorkerClosuresRetire|4"
    "IntrinsicRuntimeIntegrationTests|FrameGraphParallel.TypedReadReadCanRunInParallel|4"
    "IntrinsicRuntimeIntegrationTests|FrameGraphParallel.MainThreadOnlyPassRunsOnCallerThread|4"
    "IntrinsicRuntimeIntegrationTests|FrameGraphParallel.GraphLocalCompletionDoesNotWaitForUnrelatedSchedulerWork|4"
    "IntrinsicRuntimeIntegrationTests|CoreTaskGraphStress.WideGraphExecutesAcrossWorkerThreads|4"
    "IntrinsicRuntimeContractTests|RuntimeJobService.SubmitRunsOnPoolThreadAndPublishesAtGate|3"
    "IntrinsicRuntimeContractTests|RuntimeJobService.CancelMidFlightIsObservedAndDropsCompletion|3"
    "IntrinsicRuntimeContractTests|RuntimeJobService.CancelAfterWorkerFinishSuppressesGatePublication|3"
    "IntrinsicRuntimeContractTests|RuntimeJobService.ReapCompletedRemovesTerminalRecords|3"
    "IntrinsicRuntimeContractTests|RuntimeWorldRegistry.DestroyAnnouncesCancelsJobsAndTearsDownNextMaintenance|3"
    "IntrinsicRuntimeContractTests|ClusteringModule.EngineRunCommitsLabelsAndPublishesChangeEvent|3"
    "IntrinsicRuntimeContractTests|ClusteringModule.RunKMeansWithoutModuleFailsClosedAtCommandDrain|3"
    "IntrinsicGraphicsContractCpuTests|RenderGraphParallelRecording.SeededDagsPreserveSerialSubmitOrder|4"
    "IntrinsicGraphicsContractCpuTests|RenderGraphParallelRecording.IndependentLayerRecordsOnWorkers|4"
    "IntrinsicGraphicsContractCpuTests|RenderGraphParallelRecording.RecordFailureJoinsWorkersAndSkipsSubmit|4"
    "IntrinsicGraphicsContractCpuTests|RendererFrameLifecycle.ParallelRecordingUsesSchedulerWorkersWhenAvailable|4"
    "IntrinsicGraphicsContractCpuTests|RendererFrameLifecycle.ParallelRecordingUsesAcceptedContextsForAsyncComputeQueuePlan|4"
)

get_property(_intrinsic_registered_test_targets GLOBAL PROPERTY INTRINSIC_REGISTERED_TEST_TARGETS)
if(_intrinsic_registered_test_targets)
    set(_intrinsic_property_fixup_script "# Generated by tests/CMakeLists.txt. Apply canonical list-valued CTest properties after GoogleTest discovery.\n")
    foreach(_intrinsic_label_target IN LISTS _intrinsic_registered_test_targets)
        get_target_property(
            _intrinsic_label_value
            "${_intrinsic_label_target}"
            INTRINSIC_CTEST_LABELS
        )
        get_target_property(
            _intrinsic_gtest_discovered
            "${_intrinsic_label_target}"
            INTRINSIC_GTEST_DISCOVERED
        )
        string(APPEND _intrinsic_property_fixup_script
            "if(DEFINED ${_intrinsic_label_target}_TESTS)\n"
            "  set_tests_properties($\{${_intrinsic_label_target}_TESTS} PROPERTIES\n"
            "    ENVIRONMENT \"${_intrinsic_test_env}\"\n"
            "    LABELS \"${_intrinsic_label_value}\"\n"
            "  )\n"
            "endif()\n"
        )
        if(_intrinsic_gtest_discovered)
            string(APPEND _intrinsic_property_fixup_script
                "if(NOT EXISTS \"$<TARGET_FILE:${_intrinsic_label_target}>\")\n"
                "  set_tests_properties(${_intrinsic_label_target}_NOT_BUILT PROPERTIES LABELS \"${_intrinsic_label_value}\")\n"
                "endif()\n"
            )
        endif()
    endforeach()
    set(_intrinsic_weighted_test_names)
    foreach(_intrinsic_worker_budget_entry IN LISTS _intrinsic_multiworker_test_budgets)
        string(REPLACE "|" ";" _intrinsic_worker_budget_fields
            "${_intrinsic_worker_budget_entry}"
        )
        list(LENGTH _intrinsic_worker_budget_fields _intrinsic_worker_budget_field_count)
        if(NOT _intrinsic_worker_budget_field_count EQUAL 3)
            message(FATAL_ERROR
                "Malformed multi-worker test budget '${_intrinsic_worker_budget_entry}'"
            )
        endif()
        list(GET _intrinsic_worker_budget_fields 0 _intrinsic_worker_budget_target)
        list(GET _intrinsic_worker_budget_fields 1 _intrinsic_worker_budget_test)
        list(GET _intrinsic_worker_budget_fields 2 _intrinsic_worker_budget_processors)
        if(NOT TARGET "${_intrinsic_worker_budget_target}")
            message(FATAL_ERROR
                "Multi-worker test '${_intrinsic_worker_budget_test}' references "
                "missing target '${_intrinsic_worker_budget_target}'"
            )
        endif()
        get_target_property(
            _intrinsic_worker_budget_discovered
            "${_intrinsic_worker_budget_target}"
            INTRINSIC_GTEST_DISCOVERED
        )
        if(NOT _intrinsic_worker_budget_discovered)
            message(FATAL_ERROR
                "Multi-worker test '${_intrinsic_worker_budget_test}' must use "
                "individual GoogleTest discovery"
            )
        endif()
        if(NOT _intrinsic_worker_budget_processors MATCHES "^[2-9][0-9]*$")
            message(FATAL_ERROR
                "Multi-worker test '${_intrinsic_worker_budget_test}' has invalid "
                "PROCESSORS '${_intrinsic_worker_budget_processors}'"
            )
        endif()
        if(_intrinsic_worker_budget_test IN_LIST _intrinsic_weighted_test_names)
            message(FATAL_ERROR
                "Duplicate multi-worker test budget for "
                "'${_intrinsic_worker_budget_test}'"
            )
        endif()
        list(APPEND _intrinsic_weighted_test_names "${_intrinsic_worker_budget_test}")

        string(APPEND _intrinsic_property_fixup_script
            "if(EXISTS \"$<TARGET_FILE:${_intrinsic_worker_budget_target}>\")\n"
            "  if(NOT DEFINED ${_intrinsic_worker_budget_target}_TESTS)\n"
            "    message(FATAL_ERROR \"Discovery did not publish ${_intrinsic_worker_budget_target}_TESTS\")\n"
            "  endif()\n"
            "  list(FIND ${_intrinsic_worker_budget_target}_TESTS \"${_intrinsic_worker_budget_test}\" _intrinsic_worker_budget_index)\n"
            "  if(_intrinsic_worker_budget_index EQUAL -1)\n"
            "    message(FATAL_ERROR \"Declared multi-worker test '${_intrinsic_worker_budget_test}' was not discovered in ${_intrinsic_worker_budget_target}\")\n"
            "  endif()\n"
            "  set_tests_properties(\"${_intrinsic_worker_budget_test}\" PROPERTIES PROCESSORS ${_intrinsic_worker_budget_processors})\n"
            "endif()\n"
        )
    endforeach()
    set(_intrinsic_property_fixup_file "${CMAKE_CURRENT_BINARY_DIR}/IntrinsicTestPropertyFixups.cmake")
    file(GENERATE OUTPUT "${_intrinsic_property_fixup_file}" CONTENT "${_intrinsic_property_fixup_script}")
    set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES "${_intrinsic_property_fixup_file}")
endif()
