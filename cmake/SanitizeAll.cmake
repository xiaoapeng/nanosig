# SanitizeAll.cmake — drive the `sanitize-all` audit target.
#
# Replaces the placeholder echo with a real audit pipeline that runs only
# when NANOSIG_ENABLE_ASAN / NANOSIG_ENABLE_TSAN / NANOSIG_ENABLE_UBSAN is ON.
# It does not invoke ctest; ctest is owned by CI steps.

include(CMakeParseArguments)

find_program(NANOSIG_PYTHON3_EXECUTABLE
    NAMES python3 python
    DOC   "Python 3 interpreter for optional header/encoding audits"
)
if(NOT NANOSIG_PYTHON3_EXECUTABLE)
    message(STATUS "sanitize-all: python3 not found, header/encoding audits will be skipped")
endif()

set(_nanosig_audit_runner "${PROJECT_SOURCE_DIR}/cmake/scripts/audit_runner.sh")

function(nanosig_add_sanitize_subtarget _kind)
    set(_name "nanosig_audit_${_kind}")

    add_custom_target(${_name}
        COMMAND
            "${_nanosig_audit_runner}"
                "${_kind}"
                "${NANOSIG_ENABLE_ASAN}"
                "${NANOSIG_ENABLE_TSAN}"
                "${NANOSIG_ENABLE_UBSAN}"
                "${NANOSIG_BUILD_API_CHECKS}"
                "${PROJECT_SOURCE_DIR}"
                "${CMAKE_BINARY_DIR}"
                "${NANOSIG_PYTHON3_EXECUTABLE}"
                "${CMAKE_COMMAND}"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        COMMENT "sanitize-all (${_kind}) audit pipeline (A1 api-contract, A2 config, A3 headers, A4 encoding)"
        VERBATIM
    )
endfunction()

function(nanosig_configure_sanitize_all)
    set(_audit_targets "")

    if(NANOSIG_ENABLE_ASAN)
        nanosig_add_sanitize_subtarget(asan)
        list(APPEND _audit_targets nanosig_audit_asan)
    endif()

    if(NANOSIG_ENABLE_UBSAN)
        nanosig_add_sanitize_subtarget(ubsan)
        list(APPEND _audit_targets nanosig_audit_ubsan)
    endif()

    if(NANOSIG_ENABLE_TSAN)
        if(WIN32)
            message(WARNING "sanitize-all: TSAN requested on Windows, ignored (Windows has no TSAN preset)")
        else()
            nanosig_add_sanitize_subtarget(tsan)
            list(APPEND _audit_targets nanosig_audit_tsan)
        endif()
    endif()

    if(NOT _audit_targets)
        add_custom_target(sanitize-all
            COMMAND ${CMAKE_COMMAND} -E echo "sanitize-all: no sanitizer enabled. Set NANOSIG_ENABLE_ASAN TSAN or UBSAN to ON."
        )
        return()
    endif()

    add_custom_target(sanitize-all
        DEPENDS ${_audit_targets}
    )
endfunction()
