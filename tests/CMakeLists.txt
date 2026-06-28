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

set(_intrinsic_test_env
        "LSAN_OPTIONS=suppressions=${CMAKE_SOURCE_DIR}/lsan.supp:fast_unwind_on_malloc=0:detect_leaks=0"
        "ASAN_OPTIONS=symbolize=1:detect_leaks=0:fast_unwind_on_malloc=0"
)

if(INTRINSIC_LLVM_SYMBOLIZER)
    list(APPEND _intrinsic_test_env "ASAN_SYMBOLIZER_PATH=${INTRINSIC_LLVM_SYMBOLIZER}")
endif()

set(_intrinsic_allowed_test_labels
    assets
    benchmark
    build
    contract
    core
    ecs
    flaky-quarantine
    geometry
    glfw
    gpu
    graphics
    headless
    integration
    physics
    platform
    regression
    runtime
    slo
    slow
    unit
    vulkan
)

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
    foreach(label IN LISTS ARG_LABELS)
        list(FIND _intrinsic_allowed_test_labels "${label}" _intrinsic_label_index)
        if(_intrinsic_label_index EQUAL -1)
            message(FATAL_ERROR
                "Test target '${name}' uses undocumented CTest label '${label}'. "
                "Update tests/README.md and _intrinsic_allowed_test_labels in tests/CMakeLists.txt before using a new label."
            )
        endif()
    endforeach()
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
    if(NOT ARG_NO_DISCOVER)
        set(_intrinsic_test_labels "${ARG_LABELS}")
        string(REPLACE ";" "\\\\\\\\;" _intrinsic_test_labels "${_intrinsic_test_labels}")
        gtest_discover_tests(${name}
            PROPERTIES
            ENVIRONMENT "${_intrinsic_test_env}"
            LABELS "${_intrinsic_test_labels}"
            TIMEOUT ${_intrinsic_test_timeout}
            DISCOVERY_MODE PRE_TEST
        )
        set_property(GLOBAL APPEND PROPERTY INTRINSIC_REGISTERED_TEST_TARGETS ${name})
        set(_intrinsic_encoded_labels "${ARG_LABELS}")
        string(REPLACE ";" "," _intrinsic_encoded_labels "${_intrinsic_encoded_labels}")
        set_property(GLOBAL APPEND PROPERTY INTRINSIC_REGISTERED_TEST_LABELS "${name}|${_intrinsic_encoded_labels}")
    endif()
endfunction()

# =============================================================================
# Helper: add an additional grouped CTest entry for selected GoogleTest suites.
# Individual gtest_discover_tests registrations remain intact; grouped entries
# are opt-in and are used only where a shared per-process fixture has been added.
# =============================================================================
function(intrinsic_grouped_test name target)
    cmake_parse_arguments(ARG "" "FILTER" "LABELS" ${ARGN})
    foreach(label IN LISTS ARG_LABELS)
        list(FIND _intrinsic_allowed_test_labels "${label}" _intrinsic_label_index)
        if(_intrinsic_label_index EQUAL -1)
            message(FATAL_ERROR
                "Grouped test '${name}' uses undocumented CTest label '${label}'. "
                "Update tests/README.md and _intrinsic_allowed_test_labels in tests/CMakeLists.txt before using a new label."
            )
        endif()
    endforeach()

    set(_intrinsic_test_timeout ${_intrinsic_default_test_timeout_seconds})
    list(FIND ARG_LABELS slow _intrinsic_slow_label_index)
    if(NOT _intrinsic_slow_label_index EQUAL -1)
        set(_intrinsic_test_timeout ${_intrinsic_slow_test_timeout_seconds})
    endif()

    add_test(NAME ${name}
        COMMAND $<TARGET_FILE:${target}> "--gtest_filter=${ARG_FILTER}"
    )
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "${_intrinsic_test_env}"
        LABELS "${ARG_LABELS}"
        TIMEOUT ${_intrinsic_test_timeout}
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
    Test_BoundedHeap.cpp
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
    DEPS IntrinsicCore ExtrinsicCore EnTT::EnTT
)

# --- ECS tests (FrameGraph system integration) ---
set(_ecs_test_files
    Test_RuntimeECS.cpp
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
    DEPS IntrinsicCore IntrinsicECS IntrinsicGeometry ExtrinsicECS
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
# headless-gated IntrinsicRuntimeTests umbrella.
if(TARGET ExtrinsicRuntime)
    set(_runtime_unit_test_files
        Test.MethodFigureExport.cpp
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
        Test.GpuAssetCacheFallbackBootstrap.cpp
        Test.GpuReadbackJob.cpp
        Test.AssetGeometryIO.cpp
        Test.AssetIngestStateMachine.cpp
        Test.AssetImportFormatCoverage.cpp
        Test.AssetMeshNormals.cpp
        Test.AssetModelSceneHandoff.cpp
        Test.AssetModelTextureHandoff.cpp
        Test.AssetModelTextureIO.cpp
        Test.DerivedJobGraph.cpp
        Test.EditorCommandHistory.cpp
        Test.ImGuiAdapter.cpp
        Test.ImGuiAdapterEngineWiring.cpp
        Test.KMeansBackend.cpp
        Test.MeshGeometryExtraction.cpp
        Test.MeshAttributeTextureBake.cpp
        Test.MeshGeometryPacker.cpp
        Test.MeshPrimitiveViewPacker.cpp
        Test.MeshPrimitiveViewExtraction.cpp
        Test.ProgressivePresentationExtraction.cpp
        Test.ProgressiveRenderData.cpp
        Test.RenderArtifactPublication.cpp
        Test.GizmoInteraction.cpp
        Test.GizmoInteractionEngineWiring.cpp
        Test.GraphGeometryExtraction.cpp
        Test.GraphGeometryPacker.cpp
        Test.GeometryAvailability.cpp
        Test.PointCloudGeometryExtraction.cpp
        Test.PointCloudGeometryPacker.cpp
        Test.PrimitiveSelectionRefinement.cpp
        Test.PrimitiveSelectionRefinementWiring.cpp
        Test.ProceduralGeometryCache.cpp
        Test.ProceduralGeometryExtraction.cpp
        Test.RuntimeCameraControllers.cpp
        Test.RuntimeConfigControlFacade.cpp
        Test.RuntimeEngineConfigBoot.cpp
        Test.RuntimeRenderRecipeActivation.cpp
        Test.RuntimeCameraFocusCommand.cpp
        Test.RuntimeEcsSystemBundle.cpp
        Test.RuntimeReferenceScene.cpp
        Test.RuntimeSceneLifecycle.cpp
        Test.RuntimeSceneSerialization.cpp
        Test.RuntimeVulkanBreadcrumb.cpp
        Test.SandboxEditorUi.cpp
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
        DEPS ExtrinsicRuntime imgui_core_lib nlohmann_json::nlohmann_json
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
    DEPS IntrinsicCore ExtrinsicPhysics
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
    Test_RuntimeGeometry.cpp
    Test.GeometryIO.cpp
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
    Test_HtexPatch.cpp
    Test_Validation.cpp
    Test_Curvature.cpp
    Test.CurvatureTensor.cpp
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
    Test.QualityMetrics.cpp
    Test.SurfaceSampling.cpp
    Test.PointCloudNormals.cpp
    Test_Registration.cpp
    Test_PointCloudRobustness.cpp
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
    DEPS IntrinsicCore IntrinsicGeometry
)
target_compile_definitions(GeometryTestObjs PRIVATE
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
        ${CMAKE_SOURCE_DIR}/methods/geometry/progressive_poisson/src/ProgressivePoissonReference.cpp
    DEPS IntrinsicCore
)
target_include_directories(GeometryMethodTestObjs PRIVATE
    ${CMAKE_SOURCE_DIR}/methods/geometry/progressive_poisson/include
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
        Test.GpuTransferFacadeGpuSmoke.cpp
        Test.TextureReadbackGpuSmoke.cpp
        Test.DefaultRecipeSurfaceGpuSmoke.cpp
        Test.HzbOcclusionConservatismGpuSmoke.cpp
        Test.ImGuiSurfaceGpuSmoke.cpp
        Test.ObjectSpaceNormalTextureBakeGpuSmoke.cpp
        Test.TransientDebugSurfaceGpuSmoke.cpp
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
# smoke. Runtime-owned (drives the `Engine` over an ECS-authored acceptance
# scene) so it lives next to the Slice 1/2 CPU acceptance test and links the
# runtime/graphics/Vulkan stack; gated on the promoted Vulkan backend + GLFW
# platform exactly like the graphics smokes above.
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
        DEPS ExtrinsicRuntime ExtrinsicGraphics ExtrinsicBackendsVulkan ExtrinsicPlatform ExtrinsicRHI ExtrinsicECS ExtrinsicCore glm::glm
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
# Kept in a separate object lib + target so they carry only `unit graphics`
# labels and participate in the default CPU-supported correctness gate
# (the existing GraphicsUnitTestObjs target inherits HARDEN-041 `gpu`/`vulkan`
# label debt that excludes it from the CPU gate).
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
    Test.DebugViewContract.cpp
    Test.DebugViewPass.cpp
    Test.FrameRecipeContract.cpp
    Test.GpuWorldCompactionContract.cpp
    Test.GpuWorldLifetimeContract.cpp
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
    Test.RenderGraphValidation.cpp
    Test.SharedRenderRecipeExecution.cpp
    Test.SelectionPassContracts.cpp
    Test.SelectionSystemContracts.cpp
    Test.SurfacePassContracts.cpp
    Test.TransientDebugSurfacePass.cpp
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

# Runtime/integration test object libraries pull in test sources that
# `#include "RHI.Vulkan.hpp"` (a legacy-RHI header) and `import` the legacy
# single-segment modules (`RHI`, `Graphics`, `Asset.Pipeline`, ...). Those
# legacy modules are only built when `INTRINSIC_HEADLESS_NO_GLFW=OFF`
# (top-level `CMakeLists.txt:215-221`), so when configure auto-detects a
# headless environment (no X11/Vulkan dev headers) the tests would fail to
# compile.
#
# Gate the Runtime/Integration object libs behind both `ExtrinsicRuntime`
# being available *and* the legacy GLFW stack being built. Headless CI jobs
# (pr-fast, sanitizer-tests, benchmark-smoke) consequently don't compile the
# legacy-Vulkan-dependent tests; an alternative `IntrinsicTests` umbrella
# target is provided in the headless branch below so workflow steps that
# build `IntrinsicTests` keep working.
if(TARGET ExtrinsicRuntime AND NOT INTRINSIC_HEADLESS_NO_GLFW)
    # --- Runtime tests (Graphics, I/O, RHI — need full Runtime) ---
    set(_runtime_test_files
        Test_RuntimeGraphics.cpp
        Test_RuntimeRHI.cpp
        Test_RuntimeSelection.cpp
        Test_TextureMaterialHotReload.cpp
        Test_RuntimeCulling.cpp
        Test_GraphicsBackend.cpp
        Test_SceneManager.cpp
        Test_RenderOrchestrator.cpp
        Test_HeadlessEngine.cpp
        Test_IORegistry.cpp
        Test_Exporters.cpp
        Test_DebugDraw.cpp
        Test_DebugDrawVisualizations.cpp
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
        Test_ShaderHotReload.cpp
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
        DEPS ExtrinsicRuntime nlohmann_json::nlohmann_json EnTT::EnTT glm::glm IntrinsicRHI IntrinsicGraphics IntrinsicRuntime
    )

    set(_runtime_integration_test_files
        Test.AssetLoadPipeline.cpp
        Test.AssetService.cpp
        Test.CoreFrameGraphParallel.cpp
        Test.CoreGraphInterfaces.cpp
        Test.CoreGraphStress.cpp
        Test.CoreProcess.cpp
        Test.RuntimeFrameLoopContract.cpp
        Test.GpuReadbackJobGpuSmoke.cpp
        Test.RuntimeEngineLayering.cpp
        Test.RuntimeStreamingExecutor.cpp
        Test.GpuWorldAndCulling.cpp
        Test.RenderGraphLegacy.cpp
        Test.RendererLegacy.cpp
    )
    intrinsic_resolve_test_sources(_runtime_integration_test_sources
        SEARCH_DIRS integration/runtime contract/runtime integration/graphics
        FILES ${_runtime_integration_test_files}
    )
    intrinsic_test_object_lib(RuntimeIntegrationTestObjs
        SOURCES
            ${_runtime_integration_test_sources}
        DEPS ExtrinsicRuntime glm::glm
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
        LINK_LIBS IntrinsicRuntime
        NO_DISCOVER
    )

    # --- Runtime/headless coverage ---
    # RuntimeTestObjs currently mixes CPU-side runtime contracts with Vulkan-backed
    # headless graphics tests. Label the executable conservatively as gpu/vulkan
    # until HARDEN-041 splits test sources into finer taxonomy targets.
    intrinsic_test_executable(IntrinsicRuntimeTests
        OBJECTS RuntimeTestObjs
        LINK_LIBS IntrinsicRuntime
        LABELS integration runtime headless graphics gpu vulkan slow
    )

    intrinsic_grouped_test(IntrinsicRuntimeTests.RuntimeRHIGrouped IntrinsicRuntimeTests
        FILTER "RHIBindless.*:RHIBuffer.*:RHIImage.*:RHIShader.*:RHITexture.*:RHIDescriptors.*:RHITransfer.*:RHIStagingBelt.*:DescriptorAllocatorTest.*:TransferTest.*:RHIPipeline.*:PipelineBuilderTest.*:PresentPolicy.*:VkCheckPolicy.*:VkCheckPolicyDeathTest.*:QueueFamilyIndices.*:QueueFamilyContract.*:QueueDomain.*:QueueSubmitter.*"
        LABELS integration runtime headless graphics gpu vulkan slow
    )

    intrinsic_grouped_test(IntrinsicRuntimeTests.RenderOrchestratorHeadlessGrouped IntrinsicRuntimeTests
        FILTER "RenderOrchestratorHeadlessTest.*"
        LABELS integration runtime headless graphics gpu vulkan slow
    )

    intrinsic_grouped_test(IntrinsicRuntimeTests.HeadlessEngineGrouped IntrinsicRuntimeTests
        FILTER "HeadlessEngineTest.*"
        LABELS integration runtime headless graphics gpu vulkan slow
    )

    intrinsic_grouped_test(IntrinsicRuntimeTests.RenderGraphPacketsGrouped IntrinsicRuntimeTests
        FILTER "RenderGraphPacketTest.*"
        LABELS integration runtime headless graphics gpu vulkan slow
    )

    intrinsic_grouped_test(IntrinsicRuntimeTests.GraphicsBackendHeadlessGrouped IntrinsicRuntimeTests
        FILTER "GraphicsBackendHeadlessTest.*"
        LABELS integration runtime headless graphics gpu vulkan slow
    )

    intrinsic_grouped_test(IntrinsicRuntimeTests.HeadlessAppSmokeGrouped IntrinsicRuntimeTests
        FILTER "HeadlessAppSmokeTest.*"
        LABELS integration runtime headless graphics gpu vulkan slow
    )

    intrinsic_grouped_test(IntrinsicRuntimeTests.SceneManagerGpuHooksGrouped IntrinsicRuntimeTests
        FILTER "SceneManagerGpuHooksHeadlessTest.*"
        LABELS integration runtime headless graphics gpu vulkan slow
    )

    intrinsic_grouped_test(IntrinsicRuntimeTests.GeometryReuseGrouped IntrinsicRuntimeTests
        FILTER "GeometryReuseTest.*"
        LABELS integration runtime headless graphics gpu vulkan slow
    )

    intrinsic_test_executable(IntrinsicRuntimeIntegrationTests
        OBJECTS RuntimeIntegrationTestObjs
        LINK_LIBS ExtrinsicRuntime
        LABELS integration runtime graphics gpu vulkan slow
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
    LINK_LIBS IntrinsicCore IntrinsicECS ExtrinsicECS
    LABELS unit ecs
)

# --- Contract/build checks ---
intrinsic_test_executable(IntrinsicContractBuildTests
    OBJECTS  ContractBuildTestObjs
    LINK_LIBS IntrinsicCore IntrinsicECS
    LABELS contract build
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
        LINK_LIBS ExtrinsicRuntime
        LABELS contract runtime
    )
endif()

# --- Benchmark and SLO checks ---
intrinsic_test_executable(IntrinsicBenchmarkTests
    OBJECTS  BenchmarkTestObjs
    LINK_LIBS ExtrinsicCore
    LABELS benchmark slo slow
)

# --- Physics method reference tests (CPU-only) ---
intrinsic_test_executable(IntrinsicPhysicsMethodTests
    OBJECTS PhysicsMethodTestObjs
    LINK_LIBS IntrinsicCore ExtrinsicPhysics
    LABELS unit physics
)

# --- Geometry method reference tests (CPU-only) ---
intrinsic_test_executable(IntrinsicGeometryMethodTests
    OBJECTS GeometryMethodTestObjs
    LINK_LIBS IntrinsicCore
    LABELS unit geometry
)

# --- Physics world tests (CPU-only) ---
intrinsic_test_executable(IntrinsicPhysicsWorldTests
    OBJECTS PhysicsWorldTestObjs
    LINK_LIBS ExtrinsicPhysics
    LABELS unit physics
)

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
    LINK_LIBS IntrinsicCore IntrinsicGeometry
    LABELS unit geometry
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
        LINK_LIBS ExtrinsicRuntime ExtrinsicGraphics ExtrinsicBackendsVulkan ExtrinsicPlatform ExtrinsicRHI ExtrinsicECS ExtrinsicCore
        LABELS gpu vulkan integration runtime graphics
    )
    intrinsic_add_glsl_shaders(IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests)
endif()

intrinsic_test_executable(IntrinsicGraphicsUnitTests
    OBJECTS GraphicsUnitTestObjs
    LINK_LIBS ExtrinsicRHI ExtrinsicCore ExtrinsicPlatform
    LABELS unit graphics gpu vulkan
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
)

intrinsic_test_executable(IntrinsicGraphicsRendererCpuUnitTests
    OBJECTS GraphicsRendererCpuUnitTestObjs
    LINK_LIBS ExtrinsicGraphics ExtrinsicRHI ExtrinsicCore ExtrinsicPlatform
    LABELS unit graphics
)
if(TARGET IntrinsicTests)
    add_dependencies(IntrinsicTests IntrinsicGraphicsRendererCpuUnitTests)
endif()

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
    # Some CI configurations intentionally skip RuntimeTestObjs (e.g. the
    # headless `pr-fast`, `sanitizer-tests`, and `benchmark-smoke` workflows
    # that don't install GLFW/Vulkan dev packages). Keep a stable aggregate
    # build target for agent/CI verification commands.
    add_custom_target(IntrinsicTests)
endif()

# Make the aggregate build everything that registered itself with the helper,
# whether or not RuntimeTestObjs was created. Workflows that invoke
# `cmake --build --target IntrinsicTests` then exercise the full set of
# headless-buildable test executables.
get_property(_intrinsic_registered_test_targets GLOBAL PROPERTY INTRINSIC_REGISTERED_TEST_TARGETS)
if(TARGET IntrinsicTests AND _intrinsic_registered_test_targets)
    add_dependencies(IntrinsicTests ${_intrinsic_registered_test_targets})
endif()
