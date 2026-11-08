# Run separately so ordinary CTest does not recursively configure/build projects.
# cmake -DTEST_BINARY_ROOT=/tmp/nanosig-module-config -P test/cmake/check_module_config.cmake
cmake_minimum_required(VERSION 3.20)
if(NOT DEFINED TEST_BINARY_ROOT)
    message(FATAL_ERROR "Set TEST_BINARY_ROOT to a dedicated out-of-source test directory")
endif()
set(_source "${CMAKE_CURRENT_LIST_DIR}/module_config")

function(run_checked)
    execute_process(COMMAND ${ARGV} RESULT_VARIABLE _result
        OUTPUT_VARIABLE _stdout ERROR_VARIABLE _stderr)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "Command failed: ${ARGV}\n${_stdout}\n${_stderr}")
    endif()
endfunction()

set(_custom_args
    -DCHECK_CUSTOM_CONFIG=ON
    -DCHECK_ZERO_CONFIG=OFF
    -DCHECK_ESCAPED_CONFIG=OFF
    -DNS_CONFIG_DEFAULT_DEBUG_LEVEL=NS_DBG_ERR
    -DNS_DBG_MODULE_LEVEL_DEFAULT=NS_DBG_WARNING
    -DNS_DBG_MODULE_LEVEL_CORE=NS_DBG_ERR
    -DNS_DBG_MODULE_LEVEL_PLATFORM=NS_DBG_ERR
    "-DNS_CONFIG_DEBUG_FLAGS=((NS_DBG_FLAGS_DEBUG_TAG | NS_DBG_FLAGS_MONOTONIC_CLOCK) & (1u >> 1))"
    "-DNS_CONFIG_DEBUG_ENTER_SIGN=\"\\r\\n\""
    -DNS_CONFIG_STDOUT_MEM_CACHE_SIZE=128
    -DNS_CONFIG_HASHTBL_MIN_SIZE=32
    -DNS_PLATFORM_WAIT_EMPTY_MAX_US=250000)

foreach(_case default custom zero escaped cacheline64 cacheline128)
    set(_args "")
    if(_case MATCHES "^(custom|zero|escaped)$")
        set(_args ${_custom_args})
    endif()
    if(_case MATCHES "^cacheline([0-9]+)$")
        list(APPEND _args "-DNS_MPSC_CACHELINE_SIZE=${CMAKE_MATCH_1}"
            "-DCHECK_EXPECTED_CACHELINE_SIZE=${CMAKE_MATCH_1}")
    endif()
    if(_case STREQUAL "zero")
        list(APPEND _args -DCHECK_ZERO_CONFIG=ON
            -DNS_CONFIG_DEFAULT_DEBUG_LEVEL=0 -DNS_CONFIG_DEBUG_FLAGS=0)
    endif()
    if(_case STREQUAL "escaped")
        list(APPEND _args -DCHECK_ESCAPED_CONFIG=ON
            [=[-DNS_CONFIG_DEBUG_ENTER_SIGN="\043"]=])
    endif()
    set(_build "${TEST_BINARY_ROOT}/${_case}")
    run_checked("${CMAKE_COMMAND}" -S "${_source}" -B "${_build}"
        -DCMAKE_BUILD_TYPE=Debug ${_args})
    # Clang tolerates empty arguments, but GCC treats them as missing input files.
    execute_process(COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${_build}" --show-only=json-v1
        OUTPUT_VARIABLE _tests COMMAND_ERROR_IS_FATAL ANY)
    string(JSON _test_count LENGTH "${_tests}" tests)
    math(EXPR _last_test "${_test_count} - 1")
    foreach(_test RANGE ${_last_test})
        string(JSON _name GET "${_tests}" tests ${_test} name)
        if(NOT _name MATCHES "api")
            continue()
        endif()
        string(JSON _arg_count LENGTH "${_tests}" tests ${_test} command)
        math(EXPR _last_arg "${_arg_count} - 1")
        foreach(_arg RANGE ${_last_arg})
            string(JSON _value GET "${_tests}" tests ${_test} command ${_arg})
            if(_value STREQUAL "")
                message(FATAL_ERROR "${_name} has an empty compiler argument")
            endif()
        endforeach()
    endforeach()
    run_checked("${CMAKE_COMMAND}" --build "${_build}" --parallel 4)
    run_checked("${CMAKE_COMMAND}" --build "${_build}" --target api-compile-checks)
    run_checked("${CMAKE_CTEST_COMMAND}" --test-dir "${_build}" --output-on-failure)
    message(STATUS "Module configuration ${_case}: passed")
endforeach()

foreach(_setting
        NS_CONFIG_DEFAULT_DEBUG_LEVEL=6
        NS_DBG_MODULE_LEVEL_PLATFORM=INVALID
        NS_CONFIG_DEBUG_ENTER_SIGN=unquoted
        [=[NS_CONFIG_DEBUG_ENTER_SIGN="#"]=]
        NS_CONFIG_STDOUT_MEM_CACHE_SIZE=0
        NS_CONFIG_HASHTBL_MIN_SIZE=12
        NS_PLATFORM_WAIT_EMPTY_MAX_US=-1
        NS_MPSC_CACHELINE_SIZE=0
        NS_MPSC_CACHELINE_SIZE=8
        NS_MPSC_CACHELINE_SIZE=96
        NS_MPSC_CACHELINE_SIZE=invalid)
    string(REGEX REPLACE "=.*" "" _name "${_setting}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -S "${_source}"
        -B "${TEST_BINARY_ROOT}/invalid-${_name}" "-D${_setting}"
        RESULT_VARIABLE _result OUTPUT_VARIABLE _stdout ERROR_VARIABLE _stderr)
    if(_result EQUAL 0 OR NOT "${_stdout}\n${_stderr}" MATCHES "${_name} must")
        message(FATAL_ERROR "Expected configuration rejection for ${_setting}\n${_stdout}\n${_stderr}")
    endif()
endforeach()
message(STATUS "Invalid module configurations: all rejected")

# The shell audit must interpret CMake's false aliases consistently with target creation.
get_filename_component(_repo "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
foreach(_disabled OFF FALSE 0 NO)
    run_checked("${CMAKE_COMMAND}" -S "${_repo}" -B "${TEST_BINARY_ROOT}/disabled-api"
        -DNANOSIG_BUILD_TESTS=OFF -DNANOSIG_BUILD_DEMOS=OFF
        -DNANOSIG_BUILD_BENCH=OFF "-DNANOSIG_BUILD_API_CHECKS=${_disabled}"
        -DNANOSIG_ENABLE_UBSAN=ON)
    run_checked("${CMAKE_COMMAND}" --build "${TEST_BINARY_ROOT}/disabled-api"
        --target sanitize-all)
endforeach()
message(STATUS "Disabled API checks: OFF/FALSE/0/NO audits passed")
