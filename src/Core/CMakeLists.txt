# Define the library
set(target_name IntrinsicCore)
add_library(${target_name})

# Enable Module Scanning JUST for this target
set_target_properties(${target_name} PROPERTIES CXX_SCAN_FOR_MODULES ON)

# Apply Strict Flags ONLY to our code
target_compile_options(${target_name} PRIVATE ${INTRINSIC_COMPILE_FLAGS})
target_compile_features(${target_name} PUBLIC cxx_std_23)
target_link_options(${target_name} PRIVATE ${INTRINSIC_LINK_FLAGS})

# Setup Sources
target_sources(${target_name}
        PUBLIC
        FILE_SET CXX_MODULES FILES
        Utils/Utils.BoundedHeap.cppm
        PUBLIC
        FILE_SET CXX_MODULES TYPE CXX_MODULES FILES
        Core.cppm
        Core.Memory.cppm
        Core.Logging.cppm
        Core.Tasks.cppm
        Core.Window.cppm
        Core.Filesystem.cppm
        Core.Hash.cppm
        Core.Input.cppm
        Core.Assets.cppm
        Core.Profiling.cppm
        PRIVATE
        FILE_SET core_impl TYPE CXX_MODULES FILES
        Core.Memory.cpp
        Core.Logging.cpp
        Core.Tasks.cpp
        Core.Window.cpp
        Core.Filesystem.cpp
        Core.Input.cpp
        Core.Assets.cpp
)

set_target_properties(EnTT PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES
        $<TARGET_PROPERTY:EnTT,INTERFACE_INCLUDE_DIRECTORIES>) #Silences warnings about SYSTEM includes

# Link Dependencies
target_link_libraries(${target_name}
        PUBLIC
        IntrinsicConfig
        glm::glm
        glfw
        PRIVATE
        imgui_lib
        volk::volk
        EnTT::EnTT
        # Telemetry libs will go here
)

# Note: Since we disabled exceptions globally, if any external lib needs them,
# we might need to wrap includes, but GLM is fine.

target_compile_definitions(${target_name}
        PUBLIC
        # Pass the absolute path to the source root as a string macro
        ENGINE_ROOT_DIR="${CMAKE_SOURCE_DIR}"
)