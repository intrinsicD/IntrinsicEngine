intrinsic_add_module_library(IntrinsicAsset)
set(target_name IntrinsicAsset)

# Setup Sources
target_sources(${target_name}
        PUBLIC
        FILE_SET CXX_MODULES TYPE CXX_MODULES FILES
        Asset.Manager.cppm
        Asset.Errors.cppm
        Asset.Pipeline.cppm
        PRIVATE
        Asset.Manager.cpp
        Asset.Pipeline.cpp
)

target_include_directories(${target_name} PUBLIC .)

set_target_properties(EnTT PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES
        $<TARGET_PROPERTY:EnTT,INTERFACE_INCLUDE_DIRECTORIES>) #Silences warnings about SYSTEM includes

# Link Dependencies
target_link_libraries(${target_name}
        PUBLIC
        IntrinsicCore
        IntrinsicRHI
        glm::glm
        EnTT::EnTT
        PRIVATE
        imgui_lib
)

target_compile_definitions(${target_name}
        PUBLIC
        # Pass the absolute path to the source root as a string macro
        ENGINE_ROOT_DIR="${CMAKE_SOURCE_DIR}"
)