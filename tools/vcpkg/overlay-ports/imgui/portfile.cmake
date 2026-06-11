vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

if("docking-experimental" IN_LIST FEATURES)
    vcpkg_from_github(
        OUT_SOURCE_PATH SOURCE_PATH
        REPO ocornut/imgui
        REF "v${VERSION}-docking"
        SHA512 927ecf72f00a228e0899d5b8008575b44748c49b083b9425b5f2a6b4490a9900eae111afad23f2bf0a1c9c62cf1fea80c903eb3076d7e7ea901a5625f09df78e
        HEAD_REF docking
    )
else()
    vcpkg_from_github(
        OUT_SOURCE_PATH SOURCE_PATH
        REPO ocornut/imgui
        REF "v${VERSION}"
        SHA512 60eb4f8478ae998ae68efa33b2e3c9f331f5e373a1272472f93befd9fd6cab4ed73935bb540e728b5abb154469fbc6c0fd69f7aaf54cd3187eefede6cb145a10
        HEAD_REF master
    )
endif()

file(COPY "${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt" DESTINATION "${SOURCE_PATH}")
file(COPY "${CMAKE_CURRENT_LIST_DIR}/imgui-config.cmake.in" DESTINATION "${SOURCE_PATH}")

vcpkg_cmake_configure(SOURCE_PATH "${SOURCE_PATH}")
vcpkg_cmake_install()
vcpkg_cmake_config_fixup()

file(INSTALL
    "${SOURCE_PATH}/backends/imgui_impl_glfw.cpp"
    "${SOURCE_PATH}/backends/imgui_impl_vulkan.cpp"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/backends"
)
file(INSTALL
    "${SOURCE_PATH}/backends/imgui_impl_glfw.h"
    "${SOURCE_PATH}/backends/imgui_impl_vulkan.h"
    DESTINATION "${CURRENT_PACKAGES_DIR}/include/backends"
)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include" "${CURRENT_PACKAGES_DIR}/debug/share")

vcpkg_copy_pdbs()
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.txt")
