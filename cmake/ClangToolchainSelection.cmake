# Helpers for selecting the repository-supported Clang toolchain.

set(INTRINSIC_MINIMUM_CLANG_MAJOR 20 CACHE STRING
    "Minimum Clang major version supported by IntrinsicEngine C++23 module builds")

function(intrinsic_clang_major out_var executable)
    if(NOT executable)
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()

    execute_process(
        COMMAND "${executable}" --version
        RESULT_VARIABLE _intrinsic_clang_version_result
        OUTPUT_VARIABLE _intrinsic_clang_version_output
        ERROR_VARIABLE _intrinsic_clang_version_error
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_STRIP_TRAILING_WHITESPACE
    )

    set(_intrinsic_clang_version_text
        "${_intrinsic_clang_version_output}\n${_intrinsic_clang_version_error}")
    if(_intrinsic_clang_version_result EQUAL 0
        AND _intrinsic_clang_version_text MATCHES "version[ \\t]+([0-9]+)")
        set(${out_var} "${CMAKE_MATCH_1}" PARENT_SCOPE)
        return()
    endif()

    execute_process(
        COMMAND "${executable}" -dumpversion
        RESULT_VARIABLE _intrinsic_clang_dump_result
        OUTPUT_VARIABLE _intrinsic_clang_dump_output
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(_intrinsic_clang_dump_result EQUAL 0
        AND _intrinsic_clang_dump_output MATCHES "^([0-9]+)")
        set(${out_var} "${CMAKE_MATCH_1}" PARENT_SCOPE)
        return()
    endif()

    set(${out_var} "" PARENT_SCOPE)
endfunction()

function(intrinsic_find_matching_clang_scan_deps out_var clang_major)
    find_program(_intrinsic_matching_scan_deps
        NAMES "clang-scan-deps-${clang_major}"
        HINTS "$ENV{LLVM_DIR}/bin"
        NO_CACHE
    )
    if(_intrinsic_matching_scan_deps)
        set(${out_var} "${_intrinsic_matching_scan_deps}" PARENT_SCOPE)
        return()
    endif()

    find_program(_intrinsic_generic_scan_deps
        NAMES clang-scan-deps
        HINTS "$ENV{LLVM_DIR}/bin"
        NO_CACHE
    )
    if(_intrinsic_generic_scan_deps)
        intrinsic_clang_major(_intrinsic_generic_scan_deps_major "${_intrinsic_generic_scan_deps}")
        if(_intrinsic_generic_scan_deps_major STREQUAL "${clang_major}")
            set(${out_var} "${_intrinsic_generic_scan_deps}" PARENT_SCOPE)
            return()
        endif()
    endif()

    set(${out_var} "" PARENT_SCOPE)
endfunction()

function(intrinsic_find_highest_clang_scan_deps out_path out_major)
    set(_intrinsic_scan_major 99)
    while(_intrinsic_scan_major GREATER_EQUAL "${INTRINSIC_MINIMUM_CLANG_MAJOR}")
        find_program(_intrinsic_scan_deps_candidate
            NAMES "clang-scan-deps-${_intrinsic_scan_major}"
            HINTS "$ENV{LLVM_DIR}/bin"
            NO_CACHE
        )
        if(_intrinsic_scan_deps_candidate)
            set(${out_path} "${_intrinsic_scan_deps_candidate}" PARENT_SCOPE)
            set(${out_major} "${_intrinsic_scan_major}" PARENT_SCOPE)
            return()
        endif()
        math(EXPR _intrinsic_scan_major "${_intrinsic_scan_major} - 1")
    endwhile()

    find_program(_intrinsic_generic_scan_deps
        NAMES clang-scan-deps
        HINTS "$ENV{LLVM_DIR}/bin"
        NO_CACHE
    )
    if(_intrinsic_generic_scan_deps)
        intrinsic_clang_major(_intrinsic_generic_scan_deps_major "${_intrinsic_generic_scan_deps}")
        if(_intrinsic_generic_scan_deps_major
            AND _intrinsic_generic_scan_deps_major GREATER_EQUAL "${INTRINSIC_MINIMUM_CLANG_MAJOR}")
            set(${out_path} "${_intrinsic_generic_scan_deps}" PARENT_SCOPE)
            set(${out_major} "${_intrinsic_generic_scan_deps_major}" PARENT_SCOPE)
            return()
        endif()
    endif()

    set(${out_path} "" PARENT_SCOPE)
    set(${out_major} "" PARENT_SCOPE)
endfunction()

function(intrinsic_find_highest_complete_clang_toolchain out_c out_cxx out_scan_deps out_major)
    set(_intrinsic_best_c "")
    set(_intrinsic_best_cxx "")
    set(_intrinsic_best_scan_deps "")
    set(_intrinsic_best_major "")

    set(_intrinsic_candidate_major 99)
    while(_intrinsic_candidate_major GREATER_EQUAL "${INTRINSIC_MINIMUM_CLANG_MAJOR}")
        find_program(_intrinsic_clang_c_candidate
            NAMES "clang-${_intrinsic_candidate_major}"
            HINTS "$ENV{LLVM_DIR}/bin"
            NO_CACHE
        )
        find_program(_intrinsic_clang_cxx_candidate
            NAMES "clang++-${_intrinsic_candidate_major}"
            HINTS "$ENV{LLVM_DIR}/bin"
            NO_CACHE
        )
        intrinsic_find_matching_clang_scan_deps(
            _intrinsic_scan_deps_candidate
            "${_intrinsic_candidate_major}"
        )

        if(_intrinsic_clang_c_candidate
            AND _intrinsic_clang_cxx_candidate
            AND _intrinsic_scan_deps_candidate)
            set(_intrinsic_best_c "${_intrinsic_clang_c_candidate}")
            set(_intrinsic_best_cxx "${_intrinsic_clang_cxx_candidate}")
            set(_intrinsic_best_scan_deps "${_intrinsic_scan_deps_candidate}")
            set(_intrinsic_best_major "${_intrinsic_candidate_major}")
            break()
        endif()

        math(EXPR _intrinsic_candidate_major "${_intrinsic_candidate_major} - 1")
    endwhile()

    find_program(_intrinsic_generic_clang_c
        NAMES clang
        HINTS "$ENV{LLVM_DIR}/bin"
        NO_CACHE
    )
    find_program(_intrinsic_generic_clang_cxx
        NAMES clang++
        HINTS "$ENV{LLVM_DIR}/bin"
        NO_CACHE
    )
    find_program(_intrinsic_generic_scan_deps
        NAMES clang-scan-deps
        HINTS "$ENV{LLVM_DIR}/bin"
        NO_CACHE
    )

    if(_intrinsic_generic_clang_c
        AND _intrinsic_generic_clang_cxx
        AND _intrinsic_generic_scan_deps)
        intrinsic_clang_major(_intrinsic_generic_c_major "${_intrinsic_generic_clang_c}")
        intrinsic_clang_major(_intrinsic_generic_cxx_major "${_intrinsic_generic_clang_cxx}")
        intrinsic_clang_major(_intrinsic_generic_scan_deps_major "${_intrinsic_generic_scan_deps}")

        if(_intrinsic_generic_c_major
            AND _intrinsic_generic_cxx_major
            AND _intrinsic_generic_scan_deps_major
            AND _intrinsic_generic_c_major STREQUAL "${_intrinsic_generic_cxx_major}"
            AND _intrinsic_generic_cxx_major STREQUAL "${_intrinsic_generic_scan_deps_major}"
            AND _intrinsic_generic_cxx_major GREATER_EQUAL "${INTRINSIC_MINIMUM_CLANG_MAJOR}"
            AND (NOT _intrinsic_best_major
                OR _intrinsic_generic_cxx_major GREATER "${_intrinsic_best_major}"))
            set(_intrinsic_best_c "${_intrinsic_generic_clang_c}")
            set(_intrinsic_best_cxx "${_intrinsic_generic_clang_cxx}")
            set(_intrinsic_best_scan_deps "${_intrinsic_generic_scan_deps}")
            set(_intrinsic_best_major "${_intrinsic_generic_cxx_major}")
        endif()
    endif()

    set(${out_c} "${_intrinsic_best_c}" PARENT_SCOPE)
    set(${out_cxx} "${_intrinsic_best_cxx}" PARENT_SCOPE)
    set(${out_scan_deps} "${_intrinsic_best_scan_deps}" PARENT_SCOPE)
    set(${out_major} "${_intrinsic_best_major}" PARENT_SCOPE)
endfunction()



