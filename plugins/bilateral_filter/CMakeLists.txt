# ARCH-022: the bilateral point filter plugin. Its code is one object library: shared builds wrap it in a MODULE
# the Sandbox loads (and reloads) at run time; static builds link it into the Sandbox, which registers it in-process.
# Tests link the objects in both. The Sandbox loads every plugin of INTRINSIC_STARTUP_PLUGINS at startup.
add_library(IntrinsicBilateralFilterPluginObjects OBJECT)
target_sources(IntrinsicBilateralFilterPluginObjects
        PUBLIC FILE_SET CXX_MODULES FILES BilateralFilter.cppm
        PRIVATE BilateralFilter.Run.cpp BilateralFilter.Tools.cpp BilateralFilter.Window.cpp BilateralFilter.Plugin.cpp)
set_target_properties(IntrinsicBilateralFilterPluginObjects PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        CXX_SCAN_FOR_MODULES ON)
target_compile_features(IntrinsicBilateralFilterPluginObjects PUBLIC cxx_std_23)
target_compile_options(IntrinsicBilateralFilterPluginObjects PRIVATE ${INTRINSIC_COMPILE_FLAGS})
target_link_libraries(IntrinsicBilateralFilterPluginObjects PUBLIC ExtrinsicRuntime
        PRIVATE nlohmann_json::nlohmann_json $<COMPILE_ONLY:imgui_core_lib>)

if(BUILD_SHARED_LIBS)
    add_library(IntrinsicBilateralFilterPlugin MODULE BilateralFilter.Entry.cpp)
    set_target_properties(IntrinsicBilateralFilterPlugin PROPERTIES
            CXX_SCAN_FOR_MODULES ON
            LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins)
    target_compile_options(IntrinsicBilateralFilterPlugin PRIVATE ${INTRINSIC_COMPILE_FLAGS})
    target_link_options(IntrinsicBilateralFilterPlugin PRIVATE ${INTRINSIC_LINK_FLAGS})
    target_link_libraries(IntrinsicBilateralFilterPlugin PRIVATE IntrinsicBilateralFilterPluginObjects)
endif()
set_property(GLOBAL APPEND PROPERTY INTRINSIC_STARTUP_PLUGINS BilateralFilter)
