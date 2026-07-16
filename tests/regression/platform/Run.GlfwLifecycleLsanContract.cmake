if(NOT DEFINED PROBE_EXE OR PROBE_EXE STREQUAL "")
    message(FATAL_ERROR "BUG-082 contract requires PROBE_EXE")
endif()

find_program(_bug082_env_program NAMES env REQUIRED)

set(_sanitizer_environment
    "ASAN_OPTIONS=detect_leaks=1:symbolize=0:fast_unwind_on_malloc=0:halt_on_error=1"
    "LSAN_OPTIONS=detect_leaks=1:fast_unwind_on_malloc=0:exitcode=86"
)

function(_bug082_run mode out_result out_log)
    execute_process(
        COMMAND
            "${_bug082_env_program}"
            ${_sanitizer_environment}
            "${PROBE_EXE}" "${mode}"
        RESULT_VARIABLE _result
        OUTPUT_VARIABLE _stdout
        ERROR_VARIABLE _stderr
        TIMEOUT 10
    )
    set(${out_result} "${_result}" PARENT_SCOPE)
    set(${out_log} "${_stdout}${_stderr}" PARENT_SCOPE)
endfunction()

# Run the negative control first so a display-less host still proves that the
# helper's sanitizer configuration detects an engine-owned allocation.
_bug082_run("synthetic-engine-leak" _leak_result _leak_log)
if(_leak_result MATCHES "timeout")
    message(FATAL_ERROR
        "BUG-082 synthetic leak process timed out.\n${_leak_log}")
endif()
if(_leak_result EQUAL 77)
    message("BUG082_SKIP: sanitizer instrumentation is unavailable")
    return()
endif()
if(_leak_result EQUAL 0)
    message(FATAL_ERROR
        "BUG-082 synthetic leak process exited zero; detect_leaks=1 is not effective.\n${_leak_log}")
endif()
if(NOT _leak_result EQUAL 86)
    message(FATAL_ERROR
        "BUG-082 synthetic leak process exited ${_leak_result}, expected LeakSanitizer exit 86.\n${_leak_log}")
endif()
if(NOT _leak_log MATCHES "BUG082_SYNTHETIC_ENGINE_LEAK_ALLOCATED: bytes=4096")
    message(FATAL_ERROR
        "BUG-082 synthetic leak process failed before allocating the named control.\n${_leak_log}")
endif()
if(NOT _leak_log MATCHES "LeakSanitizer: detected memory leaks"
   OR NOT _leak_log MATCHES "Direct leak of 4096 byte\\(s\\)")
    message(FATAL_ERROR
        "BUG-082 synthetic process failed without LeakSanitizer identifying the 4096-byte control.\n${_leak_log}")
endif()

_bug082_run("engine-static-lifetime" _clean_result _clean_log)
if(_clean_result MATCHES "timeout")
    message(FATAL_ERROR
        "BUG-082 engine-static GLFW lifetime timed out.\n${_clean_log}")
endif()
if(_clean_result EQUAL 77)
    message("BUG082_SKIP: GLFW/X11 display initialization is unavailable")
    return()
endif()
if(NOT _clean_result EQUAL 0)
    message(FATAL_ERROR
        "BUG-082 engine-static GLFW lifetime is not leak-clean.\n${_clean_log}")
endif()
if(NOT _clean_log MATCHES "BUG082_GLFW_STATIC_TEARDOWN: terminate_calls=1")
    message(FATAL_ERROR
        "BUG-082 clean process did not prove process-static GLFW teardown.\n${_clean_log}")
endif()

message(STATUS
    "BUG-082 passed: process-static GLFW teardown ran once and the unsuppressed synthetic engine leak was detected")
