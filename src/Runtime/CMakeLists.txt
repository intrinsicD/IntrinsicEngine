set(target_name IntrinsicRuntime)
add_library(${target_name})

set_target_properties(${target_name} PROPERTIES CXX_SCAN_FOR_MODULES ON)
target_compile_options(${target_name} PRIVATE ${INTRINSIC_COMPILE_FLAGS})
target_compile_features(${target_name} PUBLIC cxx_std_23)
target_link_options(${target_name} PRIVATE ${INTRINSIC_LINK_FLAGS})

target_sources(${target_name}
        PUBLIC
        FILE_SET CXX_MODULES TYPE CXX_MODULES FILES
        Runtime.Engine.cppm
        Runtime.GraphicsBackend.cppm
        Runtime.Selection.cppm
        Runtime.SelectionModule.cppm
        PRIVATE
        Runtime.Engine.cpp
        Runtime.GraphicsBackend.cpp
        Runtime.Selection.cpp
        Runtime.SelectionModule.cpp
)

target_include_directories(${target_name} PUBLIC .)

target_include_directories(${target_name} SYSTEM PUBLIC
        ${vma_SOURCE_DIR}/include
        ${tinygltf_SOURCE_DIR}
)

target_link_libraries(${target_name}
        PUBLIC
        IntrinsicCore
        IntrinsicGraphics
        IntrinsicGeometry
        IntrinsicInterface
        IntrinsicECS
        IntrinsicRHI
        PRIVATE
        glfw
)

add_subdirectory(ECS)
add_subdirectory(Geometry)
add_subdirectory(Graphics)
add_subdirectory(Interface)
add_subdirectory(RHI)
