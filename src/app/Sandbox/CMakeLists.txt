if(INTRINSIC_BUILD_SANDBOX OR INTRINSIC_BUILD_TESTS)
    intrinsic_add_module_library(ExtrinsicSandboxEditor)
    target_sources(ExtrinsicSandboxEditor
            PUBLIC
            FILE_SET CXX_MODULES TYPE CXX_MODULES FILES
            Editor/Sandbox.EditorController.cppm
            Editor/Sandbox.EditorShell.cppm
            Editor/Sandbox.DomainPanels.cppm
            Editor/Sandbox.MethodPanels.cppm
            Editor/Sandbox.MeshProcessingPanels.cppm
            PRIVATE
            Editor/Sandbox.EditorController.cpp
            Editor/Sandbox.EditorShell.cpp
            Editor/Sandbox.DomainPanels.cpp
            Editor/Sandbox.MethodPanels.cpp
            Editor/Sandbox.MeshProcessingPanels.cpp
    )
    target_link_libraries(ExtrinsicSandboxEditor
            PUBLIC
            ExtrinsicRuntime
            PRIVATE
            imgui_core_lib
    )
endif()

if(INTRINSIC_BUILD_SANDBOX AND NOT INTRINSIC_HEADLESS_NO_GLFW)
    add_executable(ExtrinsicSandbox)
    set_target_properties(ExtrinsicSandbox PROPERTIES CXX_SCAN_FOR_MODULES ON)
    target_compile_options(ExtrinsicSandbox PRIVATE ${INTRINSIC_COMPILE_FLAGS})
    target_compile_features(ExtrinsicSandbox PUBLIC cxx_std_23)
    target_link_options(ExtrinsicSandbox PRIVATE ${INTRINSIC_LINK_FLAGS})

    target_sources(ExtrinsicSandbox
        PUBLIC
        FILE_SET CXX_MODULES TYPE CXX_MODULES FILES
        Sandbox.cppm
        PRIVATE
        Sandbox.cpp
        main.cpp
    )

    target_link_libraries(ExtrinsicSandbox
        PRIVATE
        ExtrinsicRuntime
        ExtrinsicSandboxEditor
    )

    include(${CMAKE_SOURCE_DIR}/cmake/CompileShaders.cmake)

    # Ensure promoted Sandbox runtime shader artifacts are built next to the executable.
    intrinsic_add_glsl_shaders(ExtrinsicSandbox)
endif()
