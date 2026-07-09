include_guard(GLOBAL)

set(INTRINSIC_ALLOWED_TEST_LABELS
    assets
    benchmark
    build
    contract
    core
    ecs
    flaky-quarantine
    geometry
    glfw
    gpu
    graphics
    headless
    integration
    physics
    platform
    regression
    runtime
    slo
    slow
    unit
    vulkan
)

set(
    INTRINSIC_TEST_INVENTORY_DIR
    "${CMAKE_BINARY_DIR}/test-inventories"
    CACHE INTERNAL
    "Generated test target and aggregate inventories"
)

function(intrinsic_validate_test_labels)
    cmake_parse_arguments(ARG "" "CONTEXT" "LABELS" ${ARGN})
    if(ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "${ARG_CONTEXT}: unexpected arguments: ${ARG_UNPARSED_ARGUMENTS}"
        )
    endif()
    if(NOT ARG_CONTEXT)
        message(FATAL_ERROR "Test label validation requires CONTEXT")
    endif()
    if(NOT ARG_LABELS)
        message(FATAL_ERROR "${ARG_CONTEXT}: at least one CTest label is required")
    endif()

    set(_intrinsic_labels "${ARG_LABELS}")
    list(LENGTH _intrinsic_labels _intrinsic_label_count)
    list(REMOVE_DUPLICATES _intrinsic_labels)
    list(LENGTH _intrinsic_labels _intrinsic_unique_label_count)
    if(NOT _intrinsic_label_count EQUAL _intrinsic_unique_label_count)
        message(FATAL_ERROR "${ARG_CONTEXT}: duplicate CTest labels are not allowed")
    endif()

    foreach(_intrinsic_label IN LISTS _intrinsic_labels)
        list(FIND INTRINSIC_ALLOWED_TEST_LABELS
            "${_intrinsic_label}"
            _intrinsic_label_index
        )
        if(_intrinsic_label_index EQUAL -1)
            message(FATAL_ERROR
                "${ARG_CONTEXT}: undocumented CTest label '${_intrinsic_label}'. "
                "Update tests/README.md and cmake/IntrinsicTest.cmake together."
            )
        endif()
    endforeach()
endfunction()

function(intrinsic_register_test_executable)
    cmake_parse_arguments(ARG "" "TARGET" "LABELS" ${ARGN})
    if(ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "Test target registration has unexpected arguments: "
            "${ARG_UNPARSED_ARGUMENTS}"
        )
    endif()
    if(NOT ARG_TARGET)
        message(FATAL_ERROR "Test target registration requires TARGET")
    endif()
    if(NOT TARGET "${ARG_TARGET}")
        message(FATAL_ERROR
            "Cannot register missing CTest executable target '${ARG_TARGET}'"
        )
    endif()

    get_target_property(_intrinsic_target_type "${ARG_TARGET}" TYPE)
    if(NOT _intrinsic_target_type STREQUAL "EXECUTABLE")
        message(FATAL_ERROR
            "CTest producer '${ARG_TARGET}' must be an executable target, "
            "got ${_intrinsic_target_type}"
        )
    endif()

    get_property(
        _intrinsic_already_registered
        TARGET "${ARG_TARGET}"
        PROPERTY INTRINSIC_TEST_REGISTERED
        SET
    )
    if(_intrinsic_already_registered)
        message(FATAL_ERROR
            "CTest executable target '${ARG_TARGET}' is already registered"
        )
    endif()

    intrinsic_validate_test_labels(
        CONTEXT "Test target '${ARG_TARGET}'"
        LABELS ${ARG_LABELS}
    )
    set(_intrinsic_labels "${ARG_LABELS}")
    list(SORT _intrinsic_labels)

    set_property(
        TARGET "${ARG_TARGET}"
        PROPERTY INTRINSIC_TEST_REGISTERED TRUE
    )
    set_property(
        TARGET "${ARG_TARGET}"
        PROPERTY INTRINSIC_CTEST_LABELS "${_intrinsic_labels}"
    )
    set_property(
        GLOBAL APPEND
        PROPERTY INTRINSIC_REGISTERED_TEST_TARGETS "${ARG_TARGET}"
    )
endfunction()

function(intrinsic_write_test_registry)
    get_property(
        _intrinsic_targets
        GLOBAL
        PROPERTY INTRINSIC_REGISTERED_TEST_TARGETS
    )
    if(NOT _intrinsic_targets)
        message(FATAL_ERROR "No CTest executable targets were registered")
    endif()
    list(REMOVE_DUPLICATES _intrinsic_targets)
    list(SORT _intrinsic_targets)

    file(MAKE_DIRECTORY "${INTRINSIC_TEST_INVENTORY_DIR}")
    set(_intrinsic_registry "target\tlabels\n")
    foreach(_intrinsic_target IN LISTS _intrinsic_targets)
        if(NOT TARGET "${_intrinsic_target}")
            message(FATAL_ERROR
                "Registered CTest target '${_intrinsic_target}' no longer exists"
            )
        endif()
        get_target_property(
            _intrinsic_labels
            "${_intrinsic_target}"
            INTRINSIC_CTEST_LABELS
        )
        if(NOT _intrinsic_labels)
            message(FATAL_ERROR
                "Registered CTest target '${_intrinsic_target}' has no label metadata"
            )
        endif()
        list(JOIN _intrinsic_labels "," _intrinsic_encoded_labels)
        string(APPEND
            _intrinsic_registry
            "${_intrinsic_target}\t${_intrinsic_encoded_labels}\n"
        )
    endforeach()
    file(
        WRITE
        "${INTRINSIC_TEST_INVENTORY_DIR}/RegisteredTestTargets.tsv"
        "${_intrinsic_registry}"
    )
endfunction()

function(intrinsic_add_test_aggregate)
    cmake_parse_arguments(
        ARG
        ""
        "NAME"
        "INCLUDE_ANY;INCLUDE_ALL;EXCLUDE_ANY"
        ${ARGN}
    )
    if(ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "Test aggregate has unexpected arguments: ${ARG_UNPARSED_ARGUMENTS}"
        )
    endif()
    if(NOT ARG_NAME)
        message(FATAL_ERROR "Test aggregate requires NAME")
    endif()
    if(TARGET "${ARG_NAME}")
        message(FATAL_ERROR
            "Test aggregate target '${ARG_NAME}' already exists"
        )
    endif()

    foreach(_intrinsic_filter IN ITEMS INCLUDE_ANY INCLUDE_ALL EXCLUDE_ANY)
        if(ARG_${_intrinsic_filter})
            intrinsic_validate_test_labels(
                CONTEXT "Test aggregate '${ARG_NAME}' ${_intrinsic_filter}"
                LABELS ${ARG_${_intrinsic_filter}}
            )
        endif()
    endforeach()

    get_property(
        _intrinsic_targets
        GLOBAL
        PROPERTY INTRINSIC_REGISTERED_TEST_TARGETS
    )
    if(NOT _intrinsic_targets)
        message(FATAL_ERROR
            "Test aggregate '${ARG_NAME}' has no registered targets to select"
        )
    endif()
    list(REMOVE_DUPLICATES _intrinsic_targets)
    list(SORT _intrinsic_targets)

    set(_intrinsic_selected_targets)
    foreach(_intrinsic_target IN LISTS _intrinsic_targets)
        get_target_property(
            _intrinsic_labels
            "${_intrinsic_target}"
            INTRINSIC_CTEST_LABELS
        )
        if(NOT _intrinsic_labels)
            message(FATAL_ERROR
                "Registered CTest target '${_intrinsic_target}' has ambiguous "
                "label metadata"
            )
        endif()

        set(_intrinsic_selected TRUE)
        if(ARG_INCLUDE_ANY)
            set(_intrinsic_matches_any FALSE)
            foreach(_intrinsic_label IN LISTS ARG_INCLUDE_ANY)
                list(FIND
                    _intrinsic_labels
                    "${_intrinsic_label}"
                    _intrinsic_label_index
                )
                if(NOT _intrinsic_label_index EQUAL -1)
                    set(_intrinsic_matches_any TRUE)
                    break()
                endif()
            endforeach()
            if(NOT _intrinsic_matches_any)
                set(_intrinsic_selected FALSE)
            endif()
        endif()

        if(_intrinsic_selected AND ARG_INCLUDE_ALL)
            foreach(_intrinsic_label IN LISTS ARG_INCLUDE_ALL)
                list(FIND
                    _intrinsic_labels
                    "${_intrinsic_label}"
                    _intrinsic_label_index
                )
                if(_intrinsic_label_index EQUAL -1)
                    set(_intrinsic_selected FALSE)
                    break()
                endif()
            endforeach()
        endif()

        if(_intrinsic_selected AND ARG_EXCLUDE_ANY)
            foreach(_intrinsic_label IN LISTS ARG_EXCLUDE_ANY)
                list(FIND
                    _intrinsic_labels
                    "${_intrinsic_label}"
                    _intrinsic_label_index
                )
                if(NOT _intrinsic_label_index EQUAL -1)
                    set(_intrinsic_selected FALSE)
                    break()
                endif()
            endforeach()
        endif()

        if(_intrinsic_selected)
            list(APPEND _intrinsic_selected_targets "${_intrinsic_target}")
        endif()
    endforeach()

    if(NOT _intrinsic_selected_targets)
        message(FATAL_ERROR
            "Test aggregate '${ARG_NAME}' selected no executable targets"
        )
    endif()

    add_custom_target("${ARG_NAME}")
    add_dependencies("${ARG_NAME}" ${_intrinsic_selected_targets})
    set_property(
        TARGET "${ARG_NAME}"
        PROPERTY INTRINSIC_TEST_AGGREGATE_MEMBERS
        "${_intrinsic_selected_targets}"
    )

    file(MAKE_DIRECTORY "${INTRINSIC_TEST_INVENTORY_DIR}")
    set(_intrinsic_inventory)
    foreach(_intrinsic_target IN LISTS _intrinsic_selected_targets)
        string(APPEND _intrinsic_inventory "${_intrinsic_target}\n")
    endforeach()
    file(
        WRITE
        "${INTRINSIC_TEST_INVENTORY_DIR}/${ARG_NAME}.txt"
        "${_intrinsic_inventory}"
    )
endfunction()
