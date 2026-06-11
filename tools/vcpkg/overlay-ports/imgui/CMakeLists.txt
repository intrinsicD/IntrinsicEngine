cmake_minimum_required(VERSION 3.16)
project(imgui LANGUAGES CXX)

include(CMakePackageConfigHelpers)

add_library(imgui STATIC)
add_library(imgui::imgui ALIAS imgui)

target_sources(imgui
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/imgui.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/imgui_demo.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/imgui_draw.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/imgui_tables.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/imgui_widgets.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp"
)

target_include_directories(imgui
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/backends>"
        "$<INSTALL_INTERFACE:include>"
        "$<INSTALL_INTERFACE:include/backends>"
)

target_compile_features(imgui PUBLIC cxx_std_11)

install(
    TARGETS imgui
    EXPORT imgui-targets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(
    FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/imgui.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/imconfig.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/imgui_internal.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/imstb_textedit.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/imstb_rectpack.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/imstb_truetype.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/misc/cpp/imgui_stdlib.h"
    DESTINATION include
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/imgui-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/imgui-config.cmake"
    INSTALL_DESTINATION share/imgui
)

install(
    FILES "${CMAKE_CURRENT_BINARY_DIR}/imgui-config.cmake"
    DESTINATION share/imgui
)

install(
    EXPORT imgui-targets
    NAMESPACE imgui::
    FILE imgui-targets.cmake
    DESTINATION share/imgui
)
