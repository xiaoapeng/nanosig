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

function(nanosig_configure_coverage_report)
    if(NANOSIG_ENABLE_COVERAGE)
        find_program(LCOV lcov)
        find_program(GENHTML genhtml)
        if(LCOV AND GENHTML)
            add_custom_target(coverage
                COMMAND ${LCOV} --capture --ignore-errors gcov,negative --directory . --output-file coverage.info
                COMMAND ${LCOV} --remove coverage.info --ignore-errors unused '/usr/*' '*/test/*' '*/include/nanosig/internal/*' --output-file coverage.info
                COMMAND ${GENHTML} coverage.info --output-directory coverage_report
                COMMAND ${CMAKE_COMMAND} -E echo "Coverage report: file://${CMAKE_BINARY_DIR}/coverage_report/index.html"
                WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
                COMMENT "Generating code coverage report..."
            )
        else()
            add_custom_target(coverage
                COMMAND ${CMAKE_COMMAND} -E echo "lcov and/or genhtml not found — install with: sudo apt install lcov"
            )
        endif()
    endif()
endfunction()
