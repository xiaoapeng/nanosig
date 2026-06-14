cmake_minimum_required(VERSION 3.20)

project(nanosig
    VERSION 0.1.0
    DESCRIPTION "C11 thread-based signal/slot library"
    LANGUAGES C
)

include(cmake/CompileWarnings.cmake)
include(cmake/Sanitizers.cmake)
include(cmake/ApiCompileChecks.cmake)

if(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
endif()

option(NANOSIG_BUILD_TESTS "Build nanosig tests" ON)
option(NANOSIG_BUILD_DEMOS "Build nanosig demos once implementation phases land" OFF)
option(NANOSIG_BUILD_API_CHECKS "Register compile-only PD API checks" ON)

set(NANOSIG_PUBLIC_HEADERS
    include/nanosig/nanosig_atomic.h
    include/nanosig/nanosig_broker.h
    include/nanosig/nanosig_ds.h
    include/nanosig/nanosig.h
    include/nanosig/nanosig_hashtable.h
    include/nanosig/nanosig_list.h
    include/nanosig/nanosig_loop.h
    include/nanosig/nanosig_mpsc.h
    include/nanosig/nanosig_mpsc_record_ring.h
    include/nanosig/nanosig_rbtree.h
    include/nanosig/nanosig_ringbuf.h
    include/nanosig/nanosig_safety.h
    include/nanosig/nanosig_signal.h
    include/nanosig/nanosig_slist.h
    include/nanosig/nanosig_status.h
    include/nanosig/nanosig_timer.h
    include/nanosig/nanosig_types.h
    include/nanosig/nanosig_waitable.h
)

set(NANOSIG_INTERNAL_HEADERS
    platform/port.h
    src/ns_broker.h
    src/ns_timer_mgr.h
)

set(NANOSIG_DS_SOURCES
    src/ds/ns_hashtable.c
    src/ds/ns_mpsc.c
    src/ds/ns_mpsc_record_ring.c
    src/ds/ns_rbtree.c
    src/ds/ns_ringbuf.c
)

if(WIN32)
    set(NANOSIG_PLATFORM_SOURCES
        platform/windows/port.c
    )
elseif(UNIX AND NOT APPLE)
    set(NANOSIG_PLATFORM_SOURCES
        platform/linux/port.c
    )
else()
    message(FATAL_ERROR "nanosig P1b supports only Linux and Windows platform backends")
endif()

add_library(nanosig STATIC
    src/nanosig.c
    src/ns_broker.c
    src/ns_timer.c
    ${NANOSIG_PLATFORM_SOURCES}
    ${NANOSIG_DS_SOURCES}
    ${NANOSIG_PUBLIC_HEADERS}
    ${NANOSIG_INTERNAL_HEADERS}
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

if(UNIX AND NOT APPLE)
    target_link_libraries(nanosig PRIVATE Threads::Threads)
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
    nanosig_add_unit_test(nanosig_test_platform_backend test/unit/test_platform_backend.c)
    nanosig_add_unit_test(nanosig_test_loop test/unit/test_loop.c)
    nanosig_add_unit_test(nanosig_test_signal test/unit/test_signal.c)
    nanosig_add_unit_test(nanosig_test_timer test/unit/test_timer.c)
    nanosig_add_unit_test(nanosig_test_mpsc test/unit/test_mpsc.c)
    nanosig_add_unit_test(nanosig_test_mpsc_record_ring test/unit/test_mpsc_record_ring.c)
    set_tests_properties(nanosig_test_mpsc PROPERTIES TIMEOUT 420)
    nanosig_add_unit_test(nanosig_test_ds_list test/unit/test_ds_list.c)
    nanosig_add_unit_test(nanosig_test_ds_slist test/unit/test_ds_slist.c)
    nanosig_add_unit_test(nanosig_test_ds_ringbuf test/unit/test_ds_ringbuf.c)
    nanosig_add_unit_test(nanosig_test_ds_hashtable test/unit/test_ds_hashtable.c)
    nanosig_add_unit_test(nanosig_test_ds_rbtree test/unit/test_ds_rbtree.c)
endif()

if(NANOSIG_BUILD_API_CHECKS)
    nanosig_add_api_compile_check(nanosig_api_check_demos
        demos/demo_same_thread.c
        demos/demo_cross_thread.c
        demos/demo_timer_cross_thread.c
    )

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
            nanosig_api_check_demos
            nanosig_api_check_macro_matrix
            nanosig_api_check_platform_contract
            nanosig_api_check_broker_contract
            nanosig_api_check_data_structures
            nanosig_api_check_types
    )
endif()

add_custom_target(sanitize-all
    COMMAND ${CMAKE_COMMAND} -E echo "sanitize-all: P0 scaffold has no sanitizer/audit targets yet"
    VERBATIM
)
