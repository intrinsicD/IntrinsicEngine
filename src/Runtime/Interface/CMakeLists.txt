set(target_name IntrinsicInterface)
add_library(${target_name})

set_target_properties(${target_name} PROPERTIES CXX_SCAN_FOR_MODULES ON)
target_compile_options(${target_name} PRIVATE ${INTRINSIC_COMPILE_FLAGS})
target_compile_features(${target_name} PUBLIC cxx_std_23)
target_link_options(${target_name} PRIVATE ${INTRINSIC_LINK_FLAGS})

target_sources(${target_name}
        PUBLIC
        FILE_SET CXX_MODULES TYPE CXX_MODULES FILES
        Interface.cppm
        Gui.cppm
        PRIVATE
        FILE_SET gui_impl TYPE CXX_MODULES FILES
        Gui.cpp
)

target_include_directories(${target_name} PUBLIC .)

target_link_libraries(${target_name}
        PUBLIC
        IntrinsicCore
        IntrinsicRHI
        imgui_lib
)