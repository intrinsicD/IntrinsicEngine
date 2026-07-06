if(NOT DEFINED SANDBOX_EXE)
    message(FATAL_ERROR "SANDBOX_EXE is required")
endif()
if(NOT DEFINED REPORT_PATH)
    message(FATAL_ERROR "REPORT_PATH is required")
endif()
if(NOT DEFINED FRAME_COUNT)
    set(FRAME_COUNT 8)
endif()
if(NOT DEFINED MIN_SAMPLES)
    set(MIN_SAMPLES 2)
endif()

file(REMOVE "${REPORT_PATH}")

execute_process(
    COMMAND "${SANDBOX_EXE}"
            --frame-pacing-report "${REPORT_PATH}"
            --frame-pacing-frames "${FRAME_COUNT}"
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _stdout
    ERROR_VARIABLE _stderr
    TIMEOUT 180
)

if(NOT _result EQUAL 0)
    message(FATAL_ERROR
        "ExtrinsicSandbox frame-pacing capture failed with ${_result}\n"
        "stdout:\n${_stdout}\n"
        "stderr:\n${_stderr}")
endif()

set(_combined_output "${_stdout}\n${_stderr}")
if(_combined_output MATCHES "VulkanRequestedButNotOperational[^\n]*reason=BarrierValidationFailed")
    set(_barrier_validation_fallback TRUE)
else()
    set(_barrier_validation_fallback FALSE)
endif()
if(_combined_output MATCHES "SPIR-V Interface[^\n]*not an Input declared")
    message(FATAL_ERROR
        "Frame-pacing capture emitted shader-interface validation warnings.\n"
        "stdout:\n${_stdout}\n"
        "stderr:\n${_stderr}")
endif()

if(NOT EXISTS "${REPORT_PATH}")
    message(FATAL_ERROR "Frame-pacing report was not created: ${REPORT_PATH}")
endif()

file(READ "${REPORT_PATH}" _json)

string(JSON _schema ERROR_VARIABLE _schema_error GET "${_json}" schema)
if(_schema_error OR NOT _schema STREQUAL "intrinsic.frame_pacing.v1")
    message(FATAL_ERROR "Unexpected frame-pacing schema: ${_schema_error} ${_schema}")
endif()

string(JSON _frame_count ERROR_VARIABLE _frame_count_error GET "${_json}" frame_count)
if(_frame_count_error)
    message(FATAL_ERROR "Missing frame_count: ${_frame_count_error}")
endif()
if(_frame_count LESS MIN_SAMPLES)
    message(FATAL_ERROR
        "Expected at least ${MIN_SAMPLES} frame-pacing samples, got ${_frame_count}")
endif()

string(JSON _samples_length ERROR_VARIABLE _samples_error LENGTH "${_json}" samples)
if(_samples_error)
    message(FATAL_ERROR "Missing samples array: ${_samples_error}")
endif()
if(NOT _samples_length EQUAL _frame_count)
    message(FATAL_ERROR
        "frame_count (${_frame_count}) does not match samples length (${_samples_length})")
endif()

string(JSON _total_micros ERROR_VARIABLE _total_error GET "${_json}" samples 0 total_micros)
if(_total_error OR _total_micros LESS_EQUAL 0)
    message(FATAL_ERROR "First sample total_micros is invalid: ${_total_error} ${_total_micros}")
endif()

string(JSON _renderer_completed ERROR_VARIABLE _renderer_error GET "${_json}" samples 0 renderer_completed_frame)
if(_renderer_error OR NOT _renderer_completed)
    message(FATAL_ERROR "First sample did not complete renderer frame: ${_renderer_error}")
endif()

string(JSON _top_phase ERROR_VARIABLE _top_phase_error GET "${_json}" summary top_phase_by_total)
if(_top_phase_error OR _top_phase STREQUAL "")
    message(FATAL_ERROR "Missing summary top_phase_by_total: ${_top_phase_error}")
endif()

string(JSON _final_device_operational ERROR_VARIABLE _final_device_operational_error
    GET "${_json}" summary final_device_operational)
if(_final_device_operational_error)
    message(FATAL_ERROR
        "Missing summary final_device_operational: ${_final_device_operational_error}")
endif()
if(_barrier_validation_fallback AND NOT _final_device_operational)
    message(FATAL_ERROR
        "Frame-pacing capture hit the BUG-056 Vulkan validation fallback and never reached operational Vulkan.\n"
        "stdout:\n${_stdout}\n"
        "stderr:\n${_stderr}")
endif()
if(NOT _final_device_operational)
    if(_combined_output MATCHES "VulkanRequestedButNotOperational[^\n]*reason=(MissingInstance|MissingSurface|NoSuitablePhysicalDevice|MissingRequiredExtension|MissingRequiredFeature)")
        message(STATUS
            "Frame-pacing capture did not reach operational Vulkan for an environment capability reason.")
    else()
        message(FATAL_ERROR
            "Frame-pacing capture did not reach operational Vulkan and did not report an environment capability reason.\n"
            "stdout:\n${_stdout}\n"
            "stderr:\n${_stderr}")
    endif()
endif()

string(JSON _phase_total ERROR_VARIABLE _phase_total_error GET "${_json}" summary top_phase_total_micros)
if(_phase_total_error OR _phase_total LESS_EQUAL 0)
    message(FATAL_ERROR "Invalid top_phase_total_micros: ${_phase_total_error} ${_phase_total}")
endif()

message(STATUS "Validated frame-pacing report: ${REPORT_PATH}")
