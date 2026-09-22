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
        "endif()\n")
    file(WRITE "${_release}"
        "file(LOCK [==[${_lock}]==] RELEASE)\n")
    set_property(DIRECTORY PROPERTY TEST_INCLUDE_FILES
        "${_acquire}" ${_includes} "${_release}")
endfunction()
