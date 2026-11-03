# ARCH-022: a feature plugin, from plugins/<folder>/CMakeLists.txt:
#   intrinsic_add_feature_plugin(<Name> <Name>.Run.cpp ...)
# <Name> is the plugin's PascalCase name: module Intrinsic.Plugin.<Name> in <Name>.cppm, namespace
# Intrinsic::Plugins::<Name> with Register and Shutdown. Its code is one object library, Intrinsic<Name>PluginObjects:
# shared builds wrap it in the MODULE Intrinsic<Name>Plugin (the v1 entry points generated from
# IntrinsicFeaturePluginEntry.cpp.in) that the Sandbox loads and reloads at run time; static builds link the objects
# into the Sandbox, which registers the plugin in-process; tests link the objects in both. The plugin joins
# INTRINSIC_STARTUP_PLUGINS, the list the Sandbox loads at startup.
function(intrinsic_add_feature_plugin name)
    set(objects Intrinsic${name}PluginObjects)
    add_library(${objects} OBJECT)
    target_sources(${objects} PUBLIC FILE_SET CXX_MODULES FILES ${name}.cppm PRIVATE ${ARGN})
    set_target_properties(${objects} PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_SCAN_FOR_MODULES ON)
    target_compile_features(${objects} PUBLIC cxx_std_23)
    target_compile_options(${objects} PRIVATE ${INTRINSIC_COMPILE_FLAGS})
    target_link_libraries(${objects} PUBLIC ExtrinsicRuntime
            PRIVATE nlohmann_json::nlohmann_json $<COMPILE_ONLY:imgui_core_lib>)
    if(BUILD_SHARED_LIBS)
        set(PLUGIN_NAME ${name})
        set(entry ${CMAKE_CURRENT_BINARY_DIR}/${name}.Entry.cpp)
        configure_file(${CMAKE_CURRENT_FUNCTION_LIST_DIR}/IntrinsicFeaturePluginEntry.cpp.in ${entry} @ONLY)
        add_library(Intrinsic${name}Plugin MODULE ${entry})
        set_target_properties(Intrinsic${name}Plugin PROPERTIES
                CXX_SCAN_FOR_MODULES ON
                LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins)
        target_compile_options(Intrinsic${name}Plugin PRIVATE ${INTRINSIC_COMPILE_FLAGS})
        target_link_options(Intrinsic${name}Plugin PRIVATE ${INTRINSIC_LINK_FLAGS})
        target_link_libraries(Intrinsic${name}Plugin PRIVATE ${objects})
    endif()
    set_property(GLOBAL APPEND PROPERTY INTRINSIC_STARTUP_PLUGINS ${name})
endfunction()
