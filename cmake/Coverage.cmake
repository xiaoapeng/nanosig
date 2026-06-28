option(NANOSIG_ENABLE_COVERAGE "Enable coverage instrumentation (gcov)" OFF)

if(NANOSIG_ENABLE_COVERAGE AND (NANOSIG_ENABLE_ASAN OR NANOSIG_ENABLE_TSAN))
    message(FATAL_ERROR "Coverage cannot be enabled together with ASAN or TSAN")
endif()

function(nanosig_apply_coverage target)
    if(NANOSIG_ENABLE_COVERAGE)
        if(MSVC)
            message(FATAL_ERROR "Coverage is not supported on MSVC")
        endif()
        target_compile_options(${target} PRIVATE --coverage -fprofile-arcs -ftest-coverage -fprofile-update=atomic)
        # PUBLIC link so consumers (tests, demos) resolve gcov symbols at link time
        # without needing --coverage compile flags on test code
        target_link_options(${target} PUBLIC --coverage)
    endif()
endfunction()
