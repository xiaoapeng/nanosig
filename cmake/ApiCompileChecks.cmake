function(nanosig_add_api_compile_check name)
    set(_sources ${ARGN})
    if(NOT _sources)
        message(FATAL_ERROR "nanosig_add_api_compile_check requires at least one source")
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
    if(CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        set(_msvc_frontend ON)
    elseif(MSVC AND NOT CMAKE_C_COMPILER_ID STREQUAL "Clang")
        set(_msvc_frontend ON)
    endif()

    if(_msvc_frontend)
        set(_syntax_flags
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
        set(_syntax_flags
            -std=c11
            -fsyntax-only
            -Wall
            -Wextra
            -Wconversion
            -Wsign-conversion
            -Wno-psabi
            -I${PROJECT_SOURCE_DIR}/include
            -I${PROJECT_SOURCE_DIR}
        )
    endif()

    add_custom_target(${name}
        COMMAND ${CMAKE_C_COMPILER} ${_syntax_flags} ${_sources}
        DEPENDS ${_source_deps} ${NANOSIG_PUBLIC_HEADERS} ${NANOSIG_INTERNAL_HEADERS}
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        VERBATIM
    )

    add_test(
        NAME ${name}
        COMMAND ${CMAKE_C_COMPILER} ${_syntax_flags} ${_sources}
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    )
endfunction()
