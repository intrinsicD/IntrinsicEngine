# Serialize a directory's generated test registration, including stale-file
# checks and readers. Call after its final TEST_INCLUDE_FILES registration.
function(intrinsic_serialize_test_discovery)
    get_property(_includes DIRECTORY PROPERTY TEST_INCLUDE_FILES)
    set(_lock "${CMAKE_CURRENT_BINARY_DIR}/IntrinsicTestDiscovery.lock")
    set(_acquire "${CMAKE_CURRENT_BINARY_DIR}/IntrinsicTestDiscoveryAcquire.cmake")
    set(_release "${CMAKE_CURRENT_BINARY_DIR}/IntrinsicTestDiscoveryRelease.cmake")
    # PROCESS survives the acquire include; explicit release avoids retaining
    # the lock while tests run (some tests invoke nested CTest discovery).
    # Fail within registry readers' 120-second subprocess deadline.
    file(WRITE "${_acquire}"
        "file(LOCK [==[${_lock}]==] GUARD PROCESS TIMEOUT 90 RESULT_VARIABLE _intrinsic_discovery_lock_result)\n"
        "if(NOT _intrinsic_discovery_lock_result STREQUAL \"0\")\n"
        "  message(FATAL_ERROR \"Cannot acquire CTest discovery lock '${_lock}': \${_intrinsic_discovery_lock_result}. Another CTest may still be loading registrations.\")\n"
        "endif()\n"
        "include([==[${CMAKE_CURRENT_FUNCTION_LIST_FILE}]==])\n"
        "variable_watch(result _intrinsic_report_failed_gtest_listing)\n")
    file(WRITE "${_release}"
        "file(LOCK [==[${_lock}]==] RELEASE)\n")
    set_property(DIRECTORY PROPERTY TEST_INCLUDE_FILES
        "${_acquire}" ${_includes} "${_release}")
endfunction()

# CTest-time watch on GoogleTestAddTests.cmake's listing `result` (BUG-091).
# CMake < 4 omits the discovery budget from its failure message, and any line
# on a successful listing would corrupt callers that merge stderr into
# `--show-only=json-v1` output, so this prints only for a failed listing.
# Argument names: CMake 3.31+ `arg_TEST_*`, CMake 3.28 `_TEST_*`.
function(_intrinsic_report_failed_gtest_listing variable access value)
    if(NOT access STREQUAL "MODIFIED_ACCESS" OR value STREQUAL "0")
        return()
    endif()
    if(DEFINED arg_TEST_EXECUTABLE)
        set(_executable "${arg_TEST_EXECUTABLE}")
        set(_budget "${arg_TEST_DISCOVERY_TIMEOUT}")
    elseif(DEFINED _TEST_EXECUTABLE)
        set(_executable "${_TEST_EXECUTABLE}")
        set(_budget "${_TEST_DISCOVERY_TIMEOUT}")
    else()
        return()
    endif()
    message(NOTICE "IntrinsicTestDiscovery: listing '${_executable}' failed (${value}); "
        "its GoogleTest discovery budget (TEST_DISCOVERY_TIMEOUT) is ${_budget} s. See tests/README.md.")
endfunction()
