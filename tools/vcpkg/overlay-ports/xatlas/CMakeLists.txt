cmake_minimum_required(VERSION 3.16)
project(xatlas LANGUAGES CXX)

include(CMakePackageConfigHelpers)

add_library(xatlas STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/source/xatlas/xatlas.cpp"
)
add_library(xatlas::xatlas ALIAS xatlas)

target_include_directories(xatlas
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/source/xatlas>"
        "$<INSTALL_INTERFACE:include>"
)

target_compile_features(xatlas PUBLIC cxx_std_11)

install(
    TARGETS xatlas
    EXPORT xatlas-targets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(
    FILES "${CMAKE_CURRENT_SOURCE_DIR}/source/xatlas/xatlas.h"
    DESTINATION include
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/xatlas-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/xatlas-config.cmake"
    INSTALL_DESTINATION share/xatlas
)

install(
    FILES "${CMAKE_CURRENT_BINARY_DIR}/xatlas-config.cmake"
    DESTINATION share/xatlas
)

install(
    EXPORT xatlas-targets
    NAMESPACE xatlas::
    FILE xatlas-targets.cmake
    DESTINATION share/xatlas
)
