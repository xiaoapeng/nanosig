# Configuration/interface checks without a Zephyr SDK; run outside ordinary CTest.
# cmake -DTEST_BINARY_ROOT=/tmp/nanosig-zephyr-cacheline -P test/cmake/check_zephyr_cacheline.cmake
cmake_minimum_required(VERSION 3.20)
if(NOT DEFINED TEST_BINARY_ROOT)
    message(FATAL_ERROR "Set TEST_BINARY_ROOT to a dedicated out-of-source test directory")
endif()
set(_source "${CMAKE_CURRENT_LIST_DIR}/zephyr_cacheline")

function(run_checked)
    execute_process(COMMAND ${ARGV} RESULT_VARIABLE _result
        OUTPUT_VARIABLE _stdout ERROR_VARIABLE _stderr)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "Command failed: ${ARGV}\n${_stdout}\n${_stderr}")
    endif()
endfunction()

function(check_case name expected)
    set(_build "${TEST_BINARY_ROOT}/${name}")
    run_checked("${CMAKE_COMMAND}" -S "${_source}" -B "${_build}"
        "-DTEST_EXPECTED_CACHELINE=${expected}" ${ARGN})
    run_checked("${CMAKE_COMMAND}" --build "${_build}" --parallel 2)
    run_checked("${CMAKE_CTEST_COMMAND}" --test-dir "${_build}" --output-on-failure)
    message(STATUS "Zephyr cache-line configuration ${name}: passed")
endfunction()

check_case(fallback 64)
check_case(kconfig 128 -DCONFIG_DCACHE_LINE_SIZE=128 -DTEST_DT_CACHELINE=64)
check_case(devicetree 128 -DCONFIG_DCACHE_LINE_SIZE=0 -DTEST_DT_CACHELINE=128)
check_case(explicit 64 -DNS_MPSC_CACHELINE_SIZE=64
    -DCONFIG_DCACHE_LINE_SIZE=128 -DTEST_DT_CACHELINE=128)
check_case(invalid-kconfig 128 -DCONFIG_DCACHE_LINE_SIZE=96 -DTEST_DT_CACHELINE=128)
check_case(invalid-devicetree 64 -DCONFIG_DCACHE_LINE_SIZE=0 -DTEST_DT_CACHELINE=96)

execute_process(COMMAND "${CMAKE_COMMAND}" -S "${_source}"
    -B "${TEST_BINARY_ROOT}/invalid-explicit"
    -DNS_MPSC_CACHELINE_SIZE=96 -DCONFIG_DCACHE_LINE_SIZE=128 -DTEST_EXPECTED_CACHELINE=128
    RESULT_VARIABLE _result OUTPUT_VARIABLE _stdout ERROR_VARIABLE _stderr)
if(_result EQUAL 0 OR NOT "${_stdout}\n${_stderr}" MATCHES "NS_MPSC_CACHELINE_SIZE must")
    message(FATAL_ERROR "Expected explicit Zephyr cache-line rejection\n${_stdout}\n${_stderr}")
endif()
message(STATUS "Invalid explicit Zephyr cache-line size: rejected")
