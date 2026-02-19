# =============================================================================
# intrinsic_add_module_library(<target_name>)
#
# Creates a C++23 module library target with the standard IntrinsicEngine
# compile/link flags and module scanning enabled.
#
# Usage:
#   intrinsic_add_module_library(IntrinsicFoo)
#   target_sources(IntrinsicFoo PUBLIC FILE_SET CXX_MODULES TYPE CXX_MODULES FILES ...)
#   target_link_libraries(IntrinsicFoo PUBLIC ...)
# =============================================================================
function(intrinsic_add_module_library target_name)
    add_library(${target_name})
    set_target_properties(${target_name} PROPERTIES CXX_SCAN_FOR_MODULES ON)
    target_compile_options(${target_name} PRIVATE ${INTRINSIC_COMPILE_FLAGS})
    target_compile_features(${target_name} PUBLIC cxx_std_23)
    target_link_options(${target_name} PRIVATE ${INTRINSIC_LINK_FLAGS})
endfunction()
