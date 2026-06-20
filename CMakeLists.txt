cmake_minimum_required(VERSION 3.20)

project(nanosig
    VERSION 0.1.0
    DESCRIPTION "C11 thread-based signal/slot library"
    LANGUAGES C
)

include(cmake/CompileWarnings.cmake)
include(cmake/Sanitizers.cmake)
include(cmake/ApiCompileChecks.cmake)

if(UNIX)
    find_package(Threads REQUIRED)
endif()

option(NANOSIG_BUILD_TESTS "Build nanosig tests" ON)
option(NANOSIG_BUILD_DEMOS "Build nanosig demo executables" ON)
option(NANOSIG_BUILD_BENCH "Build nanosig benchmarks" OFF)
option(NANOSIG_BUILD_API_CHECKS "Register compile-only API checks" ON)

set(NANOSIG_DS_SOURCES
    src/ds/ns_hashtable.c
    src/ds/ns_mpsc_record_ring.c
    src/ds/ns_rbtree.c
    src/ds/ns_ringbuf.c
)

if(WIN32)
    set(NANOSIG_PLATFORM_SOURCES
        platform/windows/port.c
    )
elseif(APPLE)
    set(NANOSIG_PLATFORM_SOURCES
        platform/macos/port.c
    )
elseif(UNIX AND NOT APPLE)
    set(NANOSIG_PLATFORM_SOURCES
        platform/linux/port.c
    )
else()
    message(FATAL_ERROR "nanosig supports only Linux, macOS, and Windows platform backends")
endif()

add_library(nanosig STATIC
    src/nanosig.c
    src/ns_broker.c
    src/ns_timer.c
    ${NANOSIG_PLATFORM_SOURCES}
    ${NANOSIG_DS_SOURCES}
)
add_library(nanosig::nanosig ALIAS nanosig)

target_compile_features(nanosig PUBLIC c_std_11)
target_include_directories(nanosig PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_include_directories(nanosig PRIVATE
    ${PROJECT_SOURCE_DIR}
)

if(UNIX)
    target_link_libraries(nanosig PRIVATE Threads::Threads)
endif()

if(WIN32 OR UNIX)
    # PUBLIC so consumers (tests, demos) get the same struct layout as the library
    target_compile_definitions(nanosig PUBLIC NS_MPSC_CACHELINE_ALIGNED)
endif()

nanosig_apply_private_warnings(nanosig)
nanosig_apply_sanitizers(nanosig)

enable_testing()

function(nanosig_add_unit_test name source)
    add_executable(${name}
        ${source}
    )
    target_link_libraries(${name} PRIVATE nanosig::nanosig)
    target_include_directories(${name} PRIVATE
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/test
    )
    nanosig_apply_private_warnings(${name})
    nanosig_apply_sanitizers(${name})

    add_test(
        NAME ${name}
        COMMAND ${name}
    )
endfunction()

if(NANOSIG_BUILD_TESTS)
    nanosig_add_unit_test(nanosig_test_broker test/unit/test_broker.c)
    target_compile_definitions(nanosig_test_broker PRIVATE NANOSIG_TEST)
    target_compile_definitions(nanosig PRIVATE NANOSIG_TEST)
    nanosig_add_unit_test(nanosig_test_platform_backend test/unit/test_platform_backend.c)
    nanosig_add_unit_test(nanosig_test_loop test/unit/test_loop.c)
    nanosig_add_unit_test(nanosig_test_signal test/unit/test_signal.c)
    nanosig_add_unit_test(nanosig_test_timer test/unit/test_timer.c)
    nanosig_add_unit_test(nanosig_test_mpsc_record_ring test/unit/test_mpsc_record_ring.c)
    nanosig_add_unit_test(nanosig_test_ds_list test/unit/test_ds_list.c)
    nanosig_add_unit_test(nanosig_test_ds_slist test/unit/test_ds_slist.c)
    nanosig_add_unit_test(nanosig_test_ds_ringbuf test/unit/test_ds_ringbuf.c)
    nanosig_add_unit_test(nanosig_test_ds_hashtable test/unit/test_ds_hashtable.c)
    nanosig_add_unit_test(nanosig_test_ds_rbtree test/unit/test_ds_rbtree.c)

    # Previously unregistered stress test
    nanosig_add_unit_test(nanosig_test_mpsc_record_ring_stress test/unit/test_mpsc_record_ring_stress.c)
    set_tests_properties(nanosig_test_mpsc_record_ring_stress PROPERTIES TIMEOUT 1300 LABELS "stress;long-stability")
endif()

if(NANOSIG_BUILD_DEMOS)
    nanosig_add_unit_test(nanosig_demo_same_thread demos/demo_same_thread.c)
    nanosig_add_unit_test(nanosig_demo_cross_thread demos/demo_cross_thread.c)
    set_tests_properties(nanosig_demo_cross_thread PROPERTIES TIMEOUT 10)
    nanosig_add_unit_test(nanosig_demo_timer_cross_thread demos/demo_timer_cross_thread.c)
    set_tests_properties(nanosig_demo_timer_cross_thread PROPERTIES TIMEOUT 10)
endif()

if(NANOSIG_BUILD_BENCH)
    nanosig_add_unit_test(nanosig_bench_same_thread bench/bench_same_thread.c)
    nanosig_add_unit_test(nanosig_bench_cross_thread bench/bench_cross_thread.c)
    nanosig_add_unit_test(nanosig_bench_throughput bench/bench_throughput.c)
    set_tests_properties(nanosig_bench_throughput PROPERTIES TIMEOUT 30)
endif()

if(NANOSIG_BUILD_API_CHECKS)
    nanosig_add_api_compile_check(nanosig_api_check_macro_matrix
        test/unit/test_macro_expansion.c
    )

    nanosig_add_api_compile_check(nanosig_api_check_platform_contract
        test/unit/test_platform_contract_compile.c
    )

    nanosig_add_api_compile_check(nanosig_api_check_broker_contract
        test/unit/test_broker_contract_compile.c
    )

    nanosig_add_api_compile_check(nanosig_api_check_data_structures
        test/unit/test_data_structures_contract_compile.c
    )

    nanosig_add_api_compile_check(nanosig_api_check_types
        test/unit/test_types_contract_compile.c
    )

    add_custom_target(api-compile-checks
        DEPENDS
            nanosig_api_check_macro_matrix
            nanosig_api_check_platform_contract
            nanosig_api_check_broker_contract
            nanosig_api_check_data_structures
            nanosig_api_check_types
    )
endif()

add_custom_target(sanitize-all
    COMMAND ${CMAKE_COMMAND} -E echo "sanitize-all: no sanitizer/audit targets yet"
    VERBATIM
)
