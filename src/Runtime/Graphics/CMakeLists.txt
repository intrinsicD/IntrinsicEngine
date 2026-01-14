set(target_name IntrinsicGraphics)
add_library(${target_name})

set_target_properties(${target_name} PROPERTIES CXX_SCAN_FOR_MODULES ON)
target_compile_options(${target_name} PRIVATE ${INTRINSIC_COMPILE_FLAGS})
target_compile_features(${target_name} PUBLIC cxx_std_23)
target_link_options(${target_name} PRIVATE ${INTRINSIC_LINK_FLAGS})

target_sources(${target_name}
        PUBLIC
        FILE_SET CXX_MODULES TYPE CXX_MODULES FILES
        Graphics.cppm
        Graphics.Camera.cppm
        Graphics.Geometry.cppm
        Graphics.Material.cppm
        Graphics.Model.cppm
        Graphics.RenderSystem.cppm
        Graphics.RenderGraph.cppm
        Graphics.Components.cppm
        Graphics.ModelLoader.cppm
        Graphics.TextureLoader.cppm
        Graphics.AssetErrors.cppm
        PRIVATE
        Graphics.TinyGltf.cpp
        PRIVATE
        FILE_SET geometry_impl TYPE CXX_MODULES FILES
        Graphics.Camera.cpp
        Graphics.Geometry.cpp
        Graphics.Material.cpp
        Graphics.RenderSystem.cpp
        Graphics.RenderGraph.cpp
        Graphics.ModelLoader.cpp
        Graphics.TextureLoader.cpp
)

target_include_directories(${target_name} PUBLIC .)

target_include_directories(${target_name} SYSTEM PUBLIC
        ${tinygltf_SOURCE_DIR}
)

target_link_libraries(${target_name}
        PUBLIC
        IntrinsicCore
        IntrinsicGeometry
        IntrinsicRHI
        IntrinsicECS
        IntrinsicInterface
)