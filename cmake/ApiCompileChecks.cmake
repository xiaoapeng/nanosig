# API compile checks — syntax-only (-fsyntax-only) contract checks over the
# public headers. Each check is emitted both as a custom target and as a ctest
# test that invokes the compiler directly, so a contract TU can be checked
# under several platform macro sets without linking. The cross-TU ABI probe
# link check is covered by the runtime/contract tests, not here.

# _nanosig_api_c_syntax_flags(<out_var> [extra_flag...])
#
# C syntax-only flag set for the active compiler frontend. Extra flags (for
# example -DNANOSIG_PLATFORM_FREERTOS=1) are appended; the result is returned
# through <out_var>.
function(_nanosig_api_c_syntax_flags out_var)
    if(CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        set(_msvc_frontend ON)
    elseif(MSVC AND NOT CMAKE_C_COMPILER_ID STREQUAL "Clang")
        set(_msvc_frontend ON)
    else()
        set(_msvc_frontend OFF)
    endif()

    if(_msvc_frontend)
        set(_flags
            /nologo
            /std:c11
            /Zs
            /W4
            /permissive-
            /Zc:preprocessor
            /I${PROJECT_SOURCE_DIR}/include
            /I${PROJECT_SOURCE_DIR}
        )
    else()
        set(_flags
            -std=c11
            -fsyntax-only
            -Wall
            -Wextra
            -Wconversion
            -Wsign-conversion
            -Wno-psabi
            -I${PROJECT_SOURCE_DIR}/include
            -I${CMAKE_BINARY_DIR}/include
            -I${PROJECT_SOURCE_DIR}
        )
    endif()

    list(APPEND _flags ${ARGN})
    set(${out_var} "${_flags}" PARENT_SCOPE)
endfunction()

# _nanosig_add_syntax_target(<name> <flags> <source>...)
#
# Emit the compile-only custom target and its ctest entry for an existing
# (already computed) flag list.
function(_nanosig_add_syntax_target name flags)
    set(_sources ${ARGN})
    if(NOT _sources)
        message(FATAL_ERROR "${name} requires at least one source")
    endif()

    set(_source_deps "")
    foreach(_source IN LISTS _sources)
        if(IS_ABSOLUTE "${_source}")
            list(APPEND _source_deps "${_source}")
        else()
            list(APPEND _source_deps "${PROJECT_SOURCE_DIR}/${_source}")
        endif()
    endforeach()

    add_custom_target(${name}
        COMMAND ${CMAKE_C_COMPILER} ${flags} ${_sources}
        DEPENDS ${_source_deps}
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        VERBATIM
    )

    add_test(
        NAME ${name}
        COMMAND ${CMAKE_C_COMPILER} ${flags} ${_sources}
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    )
endfunction()

# nanosig_add_api_compile_check(<name> <source>... [DEFINES <define>...])
#
# Optional DEFINES are appended to the compiler command line, allowing the same
# contract header to be checked under several platform macro sets (for example
# -DNANOSIG_PLATFORM_FREERTOS=1 on the host).
function(nanosig_add_api_compile_check name)
    cmake_parse_arguments(_nanosig_arg "" "" "DEFINES" ${ARGN})
    if(NOT _nanosig_arg_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "nanosig_add_api_compile_check requires at least one source")
    endif()

    _nanosig_api_c_syntax_flags(_nanosig_flags ${_nanosig_arg_DEFINES})
    _nanosig_add_syntax_target(${name} "${_nanosig_flags}"
        ${_nanosig_arg_UNPARSED_ARGUMENTS})
endfunction()

function(nanosig_add_cpp_compat_check name)
    set(_sources ${ARGN})
    if(NOT _sources)
        message(FATAL_ERROR "nanosig_add_cpp_compat_check requires at least one source")
    endif()

    set(_source_deps "")
    foreach(_source IN LISTS _sources)
        if(IS_ABSOLUTE "${_source}")
            list(APPEND _source_deps "${_source}")
        else()
            list(APPEND _source_deps "${PROJECT_SOURCE_DIR}/${_source}")
        endif()
    endforeach()

    set(_msvc_frontend OFF)
    if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        set(_msvc_frontend ON)
    elseif(MSVC AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        set(_msvc_frontend ON)
    endif()

    if(_msvc_frontend)
        set(_syntax_flags
            /nologo
            /std:c++11
            /Zs
            /W4
            /permissive-
            /Zc:preprocessor
            /I${PROJECT_SOURCE_DIR}/include
            /I${CMAKE_BINARY_DIR}/include
            /I${PROJECT_SOURCE_DIR}
        )
    else()
        set(_syntax_flags
            -std=c++11
            -fsyntax-only
            -Wall
            -Wextra
            -Wconversion
            -Wsign-conversion
            -I${PROJECT_SOURCE_DIR}/include
            -I${CMAKE_BINARY_DIR}/include
            -I${PROJECT_SOURCE_DIR}
        )
    endif()

    add_custom_target(${name}
        COMMAND ${CMAKE_CXX_COMPILER} ${_syntax_flags} ${_sources}
        DEPENDS ${_source_deps}
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        VERBATIM
    )

    add_test(
        NAME ${name}
        COMMAND ${CMAKE_CXX_COMPILER} ${_syntax_flags} ${_sources}
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    )
endfunction()
