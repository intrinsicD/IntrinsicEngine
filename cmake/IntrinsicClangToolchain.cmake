include("${CMAKE_CURRENT_LIST_DIR}/ClangToolchainSelection.cmake")

if((NOT DEFINED CMAKE_C_COMPILER OR CMAKE_C_COMPILER STREQUAL "")
    AND (NOT DEFINED CMAKE_CXX_COMPILER OR CMAKE_CXX_COMPILER STREQUAL ""))
    intrinsic_find_highest_complete_clang_toolchain(
        _intrinsic_selected_clang_c
        _intrinsic_selected_clang_cxx
        _intrinsic_selected_scan_deps
        _intrinsic_selected_clang_major
    )

    if(NOT _intrinsic_selected_clang_c OR NOT _intrinsic_selected_clang_cxx)
        message(FATAL_ERROR
            "Could not find a complete Clang ${INTRINSIC_MINIMUM_CLANG_MAJOR}+ toolchain. "
            "Install matching clang, clang++, and clang-scan-deps binaries "
            "(for example clang-${INTRINSIC_MINIMUM_CLANG_MAJOR}, "
            "clang++-${INTRINSIC_MINIMUM_CLANG_MAJOR}, and "
            "clang-scan-deps-${INTRINSIC_MINIMUM_CLANG_MAJOR}).")
    endif()

    set(CMAKE_C_COMPILER "${_intrinsic_selected_clang_c}" CACHE FILEPATH
        "IntrinsicEngine selected C compiler" FORCE)
    set(CMAKE_CXX_COMPILER "${_intrinsic_selected_clang_cxx}" CACHE FILEPATH
        "IntrinsicEngine selected C++ compiler" FORCE)
    set(CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS "${_intrinsic_selected_scan_deps}"
        CACHE FILEPATH "IntrinsicEngine selected clang-scan-deps" FORCE)
    set(INTRINSIC_CLANG_TOOLCHAIN_AUTO_SELECTED ON CACHE INTERNAL
        "Whether the IntrinsicEngine toolchain file selected the Clang tools")

    message(STATUS
        "IntrinsicEngine selected Clang ${_intrinsic_selected_clang_major}: "
        "${CMAKE_C_COMPILER}, ${CMAKE_CXX_COMPILER}, "
        "${CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS}")
elseif((DEFINED CMAKE_C_COMPILER AND NOT CMAKE_C_COMPILER STREQUAL "")
    AND (DEFINED CMAKE_CXX_COMPILER AND NOT CMAKE_CXX_COMPILER STREQUAL ""))
    if(INTRINSIC_CLANG_TOOLCHAIN_AUTO_SELECTED)
        message(STATUS
            "IntrinsicEngine using auto-selected compilers: "
            "${CMAKE_C_COMPILER}, ${CMAKE_CXX_COMPILER}")
    else()
        message(STATUS
            "IntrinsicEngine using caller-provided compilers: "
            "${CMAKE_C_COMPILER}, ${CMAKE_CXX_COMPILER}")
    endif()
else()
    message(FATAL_ERROR
        "Set both CMAKE_C_COMPILER and CMAKE_CXX_COMPILER, or neither so "
        "IntrinsicEngine can auto-select the highest complete Clang "
        "${INTRINSIC_MINIMUM_CLANG_MAJOR}+ toolchain.")
endif()

