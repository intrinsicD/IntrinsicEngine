# ARCH-022: the FPFH descriptor analysis plugin. Its code is one object library: shared builds wrap it in a MODULE
# the Sandbox loads (and reloads) at run time; static builds link it into the Sandbox, which registers it in-process.
# Tests link the objects in both. The Sandbox loads every plugin of INTRINSIC_STARTUP_PLUGINS at startup.
add_library(IntrinsicDescriptorAnalysisPluginObjects OBJECT)
target_sources(IntrinsicDescriptorAnalysisPluginObjects
        PUBLIC FILE_SET CXX_MODULES FILES DescriptorAnalysis.cppm
        PRIVATE DescriptorAnalysis.Run.cpp DescriptorAnalysis.Tools.cpp DescriptorAnalysis.Window.cpp DescriptorAnalysis.Plugin.cpp)
set_target_properties(IntrinsicDescriptorAnalysisPluginObjects PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        CXX_SCAN_FOR_MODULES ON)
target_compile_features(IntrinsicDescriptorAnalysisPluginObjects PUBLIC cxx_std_23)
target_compile_options(IntrinsicDescriptorAnalysisPluginObjects PRIVATE ${INTRINSIC_COMPILE_FLAGS})
target_link_libraries(IntrinsicDescriptorAnalysisPluginObjects PUBLIC ExtrinsicRuntime
        PRIVATE nlohmann_json::nlohmann_json $<COMPILE_ONLY:imgui_core_lib>)

if(BUILD_SHARED_LIBS)
    add_library(IntrinsicDescriptorAnalysisPlugin MODULE DescriptorAnalysis.Entry.cpp)
    set_target_properties(IntrinsicDescriptorAnalysisPlugin PROPERTIES
            CXX_SCAN_FOR_MODULES ON
            LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins)
    target_compile_options(IntrinsicDescriptorAnalysisPlugin PRIVATE ${INTRINSIC_COMPILE_FLAGS})
    target_link_options(IntrinsicDescriptorAnalysisPlugin PRIVATE ${INTRINSIC_LINK_FLAGS})
    target_link_libraries(IntrinsicDescriptorAnalysisPlugin PRIVATE IntrinsicDescriptorAnalysisPluginObjects)
endif()
set_property(GLOBAL APPEND PROPERTY INTRINSIC_STARTUP_PLUGINS DescriptorAnalysis)
