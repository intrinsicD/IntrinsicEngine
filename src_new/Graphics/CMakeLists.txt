add_subdirectory(RHI)

set(target_name ExtrinsicGraphics)
intrinsic_add_module_library(${target_name})

target_sources(${target_name}
        PUBLIC
        FILE_SET CXX_MODULES TYPE CXX_MODULES FILES
        Graphics.Renderer.cppm
        PRIVATE
        Graphics.Renderer.cpp
)

target_include_directories(${target_name} PUBLIC .)

target_link_libraries(${target_name}
        PUBLIC
        ExtrinsicRHI
        ExtrinsicCore
)

add_subdirectory(Backends)
