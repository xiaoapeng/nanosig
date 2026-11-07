# Syntax-only checks share the library's public configuration. Linking/ABI
# checks remain the responsibility of the runtime and negative-link tests.

# nanosig_add_api_compile_check(name source... [LANGUAGE C|CXX])
function(nanosig_add_api_compile_check name)
    cmake_parse_arguments(PARSE_ARGV 1 check "" "LANGUAGE" "")
    if(NOT check_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "${name} requires at least one source")
    endif()
    if(NOT check_LANGUAGE)
        set(check_LANGUAGE C)
    endif()
    if(NOT check_LANGUAGE MATCHES "^(C|CXX)$")
        message(FATAL_ERROR "${name}: LANGUAGE must be C or CXX")
    endif()

    if(CMAKE_${check_LANGUAGE}_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"
       OR (MSVC AND NOT CMAKE_${check_LANGUAGE}_COMPILER_ID STREQUAL "Clang"))
        set(flags /nologo /Zs /W4 /permissive- /Zc:preprocessor)
        set(include_prefix /I)
        set(define_prefix /D)
        if(check_LANGUAGE STREQUAL "CXX")
            list(APPEND flags /std:c++11)
        else()
            list(APPEND flags /std:c11)
        endif()
    else()
        set(flags -fsyntax-only -Wall -Wextra -Wconversion -Wsign-conversion)
        set(include_prefix -I)
        set(define_prefix -D)
        if(check_LANGUAGE STREQUAL "CXX")
            # Positional C++ initializers must cover every public field.
            list(APPEND flags -std=c++11 -Werror=missing-field-initializers)
        else()
            list(APPEND flags -std=c11 -Wno-psabi)
        endif()
    endif()

    get_target_property(public_includes nanosig INTERFACE_INCLUDE_DIRECTORIES)
    # Filter empty INSTALL_INTERFACE results before adding prefixes; CTest keeps empty arguments.
    set(include_flags "${include_prefix}$<JOIN:$<FILTER:${public_includes},EXCLUDE,^$>,;${include_prefix}>")
    list(APPEND flags "${include_prefix}${PROJECT_SOURCE_DIR}")
    get_target_property(public_definitions nanosig INTERFACE_COMPILE_DEFINITIONS)
    if(public_definitions)
        foreach(definition IN LISTS public_definitions)
            list(APPEND flags "${define_prefix}${definition}")
        endforeach()
    endif()

    set(sources "")
    foreach(source IN LISTS check_UNPARSED_ARGUMENTS)
        get_filename_component(source "${source}" ABSOLUTE
            BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        list(APPEND sources "${source}")
    endforeach()
    add_custom_target(${name}
        COMMAND ${CMAKE_${check_LANGUAGE}_COMPILER} ${flags} "${include_flags}" ${sources}
        DEPENDS ${sources}
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        COMMAND_EXPAND_LISTS
        VERBATIM
    )
    add_test(NAME ${name}
        COMMAND ${CMAKE_${check_LANGUAGE}_COMPILER} ${flags} "${include_flags}" ${sources}
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        COMMAND_EXPAND_LISTS
    )
    add_dependencies(api-compile-checks ${name})
endfunction()
