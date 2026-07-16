foreach(_required IN ITEMS
        SANDBOX_EXE
        LEAK_CONTROL_EXE
        REPORT_PATH
        SUPPRESSIONS_PATH
        SYMBOLIZER_PATH)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "BUG-083 contract requires ${_required}")
    endif()
endforeach()

if(NOT EXISTS "${SUPPRESSIONS_PATH}")
    message(FATAL_ERROR "BUG-083 suppression file is missing: ${SUPPRESSIONS_PATH}")
endif()

# Keep this exact comparison beside the runtime regression. Broad module,
# allocator, loader, or pthread suppressions would make the synthetic control
# too weak even if the five-frame process happened to exit zero.
string(CONCAT _expected_suppressions
    "leak:VulkanCommandContext@Extrinsic.Backends.Vulkan::PushConstants\n"
    "leak:VmaAllocator_T::BindVulkanBuffer\n"
    "leak:dbus_connection_send_with_reply_and_block\n"
)
file(READ "${SUPPRESSIONS_PATH}" _actual_suppressions)
string(REPLACE "\r\n" "\n" _actual_suppressions "${_actual_suppressions}")
if(NOT _actual_suppressions STREQUAL _expected_suppressions)
    message(FATAL_ERROR
        "BUG-083 requires exactly the three diagnosed narrow suppressions.\n"
        "Expected:\n${_expected_suppressions}"
        "Actual:\n${_actual_suppressions}")
endif()

find_program(_bug083_env_program NAMES env REQUIRED)

set(_lsan_options
    "detect_leaks=1:fast_unwind_on_malloc=0:exitcode=86:suppressions=${SUPPRESSIONS_PATH}"
)

# Run the negative control first. It shares the exact suppression file used by
# the Sandbox subprocess, while symbolization stays off so this proof cannot
# depend on an external symbolizer or Vulkan driver stack.
execute_process(
    COMMAND
        "${_bug083_env_program}"
        "ASAN_OPTIONS=detect_leaks=1:symbolize=0:fast_unwind_on_malloc=0:halt_on_error=1"
        "LSAN_OPTIONS=${_lsan_options}"
        "${LEAK_CONTROL_EXE}" synthetic-engine-leak
    RESULT_VARIABLE _leak_result
    OUTPUT_VARIABLE _leak_stdout
    ERROR_VARIABLE _leak_stderr
    TIMEOUT 10
)
set(_leak_log "${_leak_stdout}${_leak_stderr}")
if(_leak_result MATCHES "timeout")
    message(FATAL_ERROR "BUG-083 synthetic leak control timed out.\n${_leak_log}")
endif()
if(NOT _leak_result EQUAL 86)
    message(FATAL_ERROR
        "BUG-083 synthetic leak control exited ${_leak_result}, expected LeakSanitizer exit 86.\n"
        "${_leak_log}")
endif()
if(NOT _leak_log MATCHES "BUG082_SYNTHETIC_ENGINE_LEAK_ALLOCATED: bytes=4096")
    message(FATAL_ERROR
        "BUG-083 synthetic control failed before allocating the named engine leak.\n"
        "${_leak_log}")
endif()
if(NOT _leak_log MATCHES "LeakSanitizer: detected memory leaks"
   OR NOT _leak_log MATCHES "Direct leak of 4096 byte\\(s\\)")
    message(FATAL_ERROR
        "BUG-083 suppressions hid or obscured the 4096-byte engine leak control.\n"
        "${_leak_log}")
endif()

file(REMOVE "${REPORT_PATH}")

set(_sandbox_environment
    "ASAN_OPTIONS=detect_leaks=1:symbolize=1:fast_unwind_on_malloc=0:halt_on_error=1"
    "LSAN_OPTIONS=${_lsan_options}"
)
if(DEFINED SYMBOLIZER_PATH AND NOT SYMBOLIZER_PATH STREQUAL "")
    list(APPEND _sandbox_environment "ASAN_SYMBOLIZER_PATH=${SYMBOLIZER_PATH}")
endif()

execute_process(
    COMMAND
        "${_bug083_env_program}"
        ${_sandbox_environment}
        "${SANDBOX_EXE}"
        --frame-pacing-report "${REPORT_PATH}"
        --frame-pacing-frames 5
    RESULT_VARIABLE _sandbox_result
    OUTPUT_VARIABLE _sandbox_stdout
    ERROR_VARIABLE _sandbox_stderr
    TIMEOUT 150
)
set(_sandbox_log "${_sandbox_stdout}${_sandbox_stderr}")
if(_sandbox_result MATCHES "timeout")
    message(FATAL_ERROR "BUG-083 five-frame Sandbox process timed out.\n${_sandbox_log}")
endif()
if(NOT _sandbox_result EQUAL 0)
    message(FATAL_ERROR
        "BUG-083 five-frame Sandbox process exited ${_sandbox_result}; the narrow leak policy is not clean.\n"
        "${_sandbox_log}")
endif()
if(_sandbox_log MATCHES "LeakSanitizer: detected memory leaks")
    message(FATAL_ERROR
        "BUG-083 Sandbox emitted a LeakSanitizer failure despite exiting zero.\n"
        "${_sandbox_log}")
endif()
if(NOT EXISTS "${REPORT_PATH}")
    message(FATAL_ERROR "BUG-083 frame-pacing report was not created: ${REPORT_PATH}")
endif()

file(READ "${REPORT_PATH}" _json)

string(JSON _schema ERROR_VARIABLE _schema_error GET "${_json}" schema)
if(_schema_error OR NOT _schema STREQUAL "intrinsic.frame_pacing.v1")
    message(FATAL_ERROR "BUG-083 report has an unexpected schema: ${_schema_error} ${_schema}")
endif()

string(JSON _requested_frames ERROR_VARIABLE _requested_error GET "${_json}" requested_frames)
if(_requested_error OR NOT _requested_frames EQUAL 5)
    message(FATAL_ERROR
        "BUG-083 report must record requested_frames=5: ${_requested_error} ${_requested_frames}")
endif()

string(JSON _frame_count ERROR_VARIABLE _frame_count_error GET "${_json}" frame_count)
if(_frame_count_error OR NOT _frame_count EQUAL 5)
    message(FATAL_ERROR
        "BUG-083 report must record exactly five frames: ${_frame_count_error} ${_frame_count}")
endif()

string(JSON _samples_length ERROR_VARIABLE _samples_error LENGTH "${_json}" samples)
if(_samples_error OR NOT _samples_length EQUAL 5)
    message(FATAL_ERROR
        "BUG-083 report must contain exactly five samples: ${_samples_error} ${_samples_length}")
endif()

foreach(_sample_index RANGE 0 4)
    string(JSON _renderer_completed ERROR_VARIABLE _renderer_error
        GET "${_json}" samples ${_sample_index} renderer_completed_frame)
    if(_renderer_error OR NOT _renderer_completed)
        message(FATAL_ERROR
            "BUG-083 sample ${_sample_index} did not complete a renderer frame: ${_renderer_error}")
    endif()
endforeach()

string(JSON _final_device_operational ERROR_VARIABLE _operational_error
    GET "${_json}" summary final_device_operational)
if(_operational_error OR NOT _final_device_operational)
    message(FATAL_ERROR
        "BUG-083 exact run did not finish on an operational Vulkan device: ${_operational_error}\n"
        "${_sandbox_log}")
endif()

message(STATUS
    "BUG-083 passed: exact five-frame Vulkan shutdown was leak-clean and the engine-leak control remained visible")
