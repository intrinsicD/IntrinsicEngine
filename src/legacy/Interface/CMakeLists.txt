intrinsic_add_module_library(IntrinsicInterface)
set(target_name IntrinsicInterface)

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