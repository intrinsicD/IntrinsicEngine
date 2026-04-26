set(EXTRINSIC_BACKEND "Vulkan" CACHE STRING "Graphics backend to use (Vulkan, DirectX12, Metal, Null)")
set(EXTRINSIC_PLATFORM "Linux" CACHE STRING "Platform to build for (Windows, Linux, macOS)")

if(INTRINSIC_HEADLESS_NO_GLFW AND EXTRINSIC_BACKEND STREQUAL "Vulkan")
    message(STATUS "INTRINSIC_HEADLESS_NO_GLFW=ON: forcing EXTRINSIC_BACKEND=Null for src_new")
    set(EXTRINSIC_BACKEND "Null" CACHE STRING "Graphics backend to use (Vulkan, DirectX12, Metal, Null)" FORCE)
endif()

if(NOT INTRINSIC_HEADLESS_NO_GLFW)
    add_subdirectory(App)
endif()
add_subdirectory(Assets)
add_subdirectory(Core)
add_subdirectory(ECS)
add_subdirectory(Graphics)
add_subdirectory(Platform)
add_subdirectory(Runtime)

add_custom_target(src_new_module_inventory
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/generate_src_new_module_inventory.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Generating src_new module inventory"
)

add_custom_target(src_new_module_inventory_check
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/generate_src_new_module_inventory.py --check
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Checking src_new module inventory drift"
)
