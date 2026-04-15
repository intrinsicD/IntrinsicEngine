target_sources(${target_name}
        PUBLIC
        FILE_SET CXX_MODULES TYPE CXX_MODULES FILES
        PRIVATE
        Platform.Input.cpp
        Platform.Window.cpp
)

target_include_directories(${target_name} PUBLIC .)

target_link_libraries(${target_name}
        PUBLIC
        ExtrinsicCore
        ExtrinsicVulkan
        glfw
        imgui_lib)