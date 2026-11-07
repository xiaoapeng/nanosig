# Shared executable setup for tests, runnable demos and benchmarks.
function(nanosig_add_test_program name source)
    add_executable(${name}
        ${source}
    )
    set_target_properties(${name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
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
        WORKING_DIRECTORY "${PROJECT_BINARY_DIR}"
    )
endfunction()
