cmake_minimum_required(VERSION 3.20)

project(nanosig
    VERSION 0.1.0
    DESCRIPTION "C11 thread-based signal/slot library"
    LANGUAGES C
)

include(cmake/CompileWarnings.cmake)
include(cmake/Sanitizers.cmake)
include(cmake/ApiCompileChecks.cmake)

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

add_library(nanosig STATIC
    src/nanosig.c
    ${NANOSIG_PUBLIC_HEADERS}
)
add_library(nanosig::nanosig ALIAS nanosig)

target_compile_features(nanosig PUBLIC c_std_11)
target_include_directories(nanosig PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

nanosig_apply_private_warnings(nanosig)
nanosig_apply_sanitizers(nanosig)

enable_testing()

if(NANOSIG_BUILD_API_CHECKS)
    nanosig_add_api_compile_check(nanosig_api_check_demos
        demos/demo_same_thread.c
        demos/demo_cross_thread.c
        demos/demo_timer_cross_thread.c
    )

    nanosig_add_api_compile_check(nanosig_api_check_macro_matrix
        test/unit/test_macro_expansion.c
    )

    add_custom_target(api-compile-checks
        DEPENDS
            nanosig_api_check_demos
            nanosig_api_check_macro_matrix
    )
endif()

add_custom_target(sanitize-all
    COMMAND ${CMAKE_COMMAND} -E echo "sanitize-all: P0 scaffold has no sanitizer/audit targets yet"
    VERBATIM
)
