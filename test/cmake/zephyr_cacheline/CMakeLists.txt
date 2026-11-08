cmake_minimum_required(VERSION 3.20)
project(nanosig_zephyr_cacheline_check LANGUAGES C)

get_filename_component(NANOSIG_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(CONFIG_NANOSIG_ZEPHYR ON)
set(CONFIG_NANOSIG_BROKER_STACK_SIZE 0)

function(zephyr_include_directories)
    set_property(GLOBAL APPEND PROPERTY fixture_zephyr_includes ${ARGV})
endfunction()

function(zephyr_compile_definitions)
    set_property(GLOBAL APPEND PROPERTY fixture_zephyr_definitions ${ARGV})
endfunction()

function(zephyr_library)
    add_library(zephyr_cacheline_library STATIC "${PROJECT_BINARY_DIR}/library.c")
    set(ZEPHYR_CURRENT_LIBRARY zephyr_cacheline_library PARENT_SCOPE)
endfunction()

function(zephyr_library_sources)
    # Real backend sources require the SDK; this fixture checks configuration only.
endfunction()

function(zephyr_library_compile_options)
endfunction()

function(dt_prop output)
    if(NOT "${ARGN}" STREQUAL "PATH;/cpus/cpu@0;PROPERTY;d-cache-line-size")
        message(FATAL_ERROR "Unexpected device-tree cache-line query: ${ARGN}")
    endif()
    if(DEFINED TEST_DT_CACHELINE)
        set(${output} "${TEST_DT_CACHELINE}" PARENT_SCOPE)
    else()
        unset(${output} PARENT_SCOPE)
    endif()
endfunction()

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/library.c" [=[
#include <stddef.h>
#include <nanosig/nanosig_mpsc_record_ring.h>
size_t fixture_library_spacing(void)
{
    return offsetof(ns_mpsc_record_ring_t, read_pos) - offsetof(ns_mpsc_record_ring_t, write_pos);
}
]=])

# Load the actual entry point with its normal source-directory semantics.
add_subdirectory("${NANOSIG_SOURCE_DIR}/zephyr" module)

get_property(_includes GLOBAL PROPERTY fixture_zephyr_includes)
get_property(_definitions GLOBAL PROPERTY fixture_zephyr_definitions)
set(_resolved "")
foreach(_definition IN LISTS _definitions)
    if(_definition MATCHES "^NS_MPSC_CACHELINE_SIZE=([0-9]+)$")
        set(_resolved "${CMAKE_MATCH_1}")
    endif()
endforeach()
if(NOT "${_resolved}" STREQUAL "${TEST_EXPECTED_CACHELINE}")
    message(FATAL_ERROR "Expected Zephyr cache-line size ${TEST_EXPECTED_CACHELINE}, got '${_resolved}'")
endif()
target_include_directories(zephyr_cacheline_library PUBLIC ${_includes})
target_compile_definitions(zephyr_cacheline_library PUBLIC ${_definitions})

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/consumer.c" "#define EXPECTED_CACHELINE ${TEST_EXPECTED_CACHELINE}\n")
file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/consumer.c" [=[
#include <stddef.h>
#include <nanosig/nanosig_mpsc_record_ring.h>
#ifndef NS_MPSC_CACHELINE_ALIGNED
#error "Zephyr must propagate cache-line layout to applications"
#endif
NS_STATIC_ASSERT(NS_MPSC_CACHELINE_SIZE == EXPECTED_CACHELINE, "Zephyr cache-line configuration");
NS_STATIC_ASSERT(offsetof(ns_mpsc_record_ring_t, read_pos) -
                 offsetof(ns_mpsc_record_ring_t, write_pos) == EXPECTED_CACHELINE,
                 "Zephyr producer/consumer layout");
NS_STATIC_ASSERT(offsetof(ns_mpsc_record_ring_t, storage) -
                 offsetof(ns_mpsc_record_ring_t, read_pos) == EXPECTED_CACHELINE,
                 "Zephyr consumer/read-only layout");
size_t fixture_library_spacing(void);
int main(void)
{
    return fixture_library_spacing() != EXPECTED_CACHELINE;
}
]=])
add_executable(zephyr_cacheline_check "${CMAKE_CURRENT_BINARY_DIR}/consumer.c")
target_link_libraries(zephyr_cacheline_check PRIVATE zephyr_cacheline_library)
enable_testing()
add_test(NAME zephyr_cacheline_check COMMAND zephyr_cacheline_check)
