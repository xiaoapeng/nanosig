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
    include/nanosig/nanosig.h
    include/nanosig/nanosig_loop.h
    include/nanosig/nanosig_safety.h
    include/nanosig/nanosig_signal.h
    include/nanosig/nanosig_timer.h
)

set(NANOSIG_INTERNAL_HEADERS
    platform/port.h
    src/internal/ns_atomic.h
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
    ${NANOSIG_PLATFORM_SOURCES}
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

if(NANOSIG_BUILD_TESTS)
    add_executable(nanosig_test_platform_backend
        test/unit/test_platform_backend.c
    )
    target_link_libraries(nanosig_test_platform_backend PRIVATE nanosig::nanosig)
    target_include_directories(nanosig_test_platform_backend PRIVATE
        ${PROJECT_SOURCE_DIR}
    )
    nanosig_apply_private_warnings(nanosig_test_platform_backend)

    add_test(
        NAME nanosig_test_platform_backend
        COMMAND nanosig_test_platform_backend
    )
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

    add_custom_target(api-compile-checks
        DEPENDS
            nanosig_api_check_demos
            nanosig_api_check_macro_matrix
            nanosig_api_check_platform_contract
    )
endif()

add_custom_target(sanitize-all
    COMMAND ${CMAKE_COMMAND} -E echo "sanitize-all: P0 scaffold has no sanitizer/audit targets yet"
    VERBATIM
)
