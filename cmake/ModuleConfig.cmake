# Empty cache values keep the module's own default (also used without CMake).
include_guard(GLOBAL)

# Debug: runtime default, compile-time filtering, and output format.
set(NS_CONFIG_DEFAULT_DEBUG_LEVEL "" CACHE STRING "Initial runtime log level (0..5 or NS_DBG_*); empty uses module default")
set(NS_DBG_MODULE_LEVEL_DEFAULT "" CACHE STRING "Default compile-time module log level (0..5 or NS_DBG_*); empty uses module default")
foreach(_module CORE TIMER BROKER MPSC PLATFORM)
    set(NS_DBG_MODULE_LEVEL_${_module} "" CACHE STRING "Compile-time ${_module} log level (0..5 or NS_DBG_*); empty follows the default module level")
endforeach()
set(NS_CONFIG_DEBUG_FLAGS "" CACHE STRING "Log prefix flags as a C expression; empty uses module default")
set(NS_CONFIG_DEBUG_ENTER_SIGN "" CACHE STRING "Log line ending as a quoted C string literal; empty uses module default")

# Format I/O, hash table, and platform wait policy.
set(NS_CONFIG_STDOUT_MEM_CACHE_SIZE "" CACHE STRING "Stdout cache bytes (positive decimal); empty uses module default")
set(NS_CONFIG_HASHTBL_MIN_SIZE "" CACHE STRING "Minimum hash buckets (decimal power of two >= 8); empty uses module default")
set(NS_PLATFORM_WAIT_EMPTY_MAX_US "" CACHE STRING "Empty waitset timeout in microseconds (positive decimal); empty uses module default")

function(_nanosig_apply_module_definition target visibility name)
    if("${${name}}" STREQUAL "")
        return()
    endif()
    if("${${name}}" MATCHES "[;\r\n]")
        message(FATAL_ERROR "${name} must be one compile definition without semicolons or literal newlines")
    endif()
    # CMake drops definitions containing #, unlike the syntax-only compiler calls.
    if("${${name}}" MATCHES "#")
        message(FATAL_ERROR "${name} must not contain a literal #; use \\043 in C strings instead")
    endif()
    if(name MATCHES "^NS_DBG_MODULE_LEVEL_" OR name STREQUAL "NS_CONFIG_DEFAULT_DEBUG_LEVEL")
        if(NOT "${${name}}" MATCHES "^([0-5]|NS_DBG_(SUPPRESS|ERR|WARNING|SYS|INFO|DEBUG))$")
            message(FATAL_ERROR "${name} must be 0..5 or an NS_DBG_* level constant")
        endif()
    elseif(name STREQUAL "NS_CONFIG_DEBUG_ENTER_SIGN")
        if(NOT "${${name}}" MATCHES [[^"([^"\\]|\\.)*"$]])
            message(FATAL_ERROR "${name} must be a quoted C string literal, for example \"\\r\\n\"")
        endif()
    elseif(name STREQUAL "NS_CONFIG_STDOUT_MEM_CACHE_SIZE" OR
           name STREQUAL "NS_CONFIG_HASHTBL_MIN_SIZE" OR
           name STREQUAL "NS_PLATFORM_WAIT_EMPTY_MAX_US")
        if(NOT "${${name}}" MATCHES "^[1-9][0-9]*$")
            message(FATAL_ERROR "${name} must be a positive decimal integer")
        endif()
        if(name STREQUAL "NS_CONFIG_HASHTBL_MIN_SIZE")
            math(EXPR _remainder "${${name}} & (${${name}} - 1)")
            if(${name} LESS 8 OR NOT _remainder EQUAL 0)
                message(FATAL_ERROR "${name} must be a power of two and at least 8")
            endif()
        endif()
    endif()
    target_compile_definitions(${target} ${visibility} "${name}=${${name}}")
endfunction()

function(nanosig_apply_module_config target)
    # Header-visible configuration must also reach applications and API checks.
    foreach(_name NS_DBG_MODULE_LEVEL_DEFAULT NS_CONFIG_DEBUG_FLAGS
            NS_CONFIG_DEBUG_ENTER_SIGN NS_CONFIG_HASHTBL_MIN_SIZE
            NS_PLATFORM_WAIT_EMPTY_MAX_US)
        _nanosig_apply_module_definition(${target} PUBLIC ${_name})
    endforeach()
    foreach(_name NS_CONFIG_DEFAULT_DEBUG_LEVEL NS_CONFIG_STDOUT_MEM_CACHE_SIZE
            NS_DBG_MODULE_LEVEL_CORE NS_DBG_MODULE_LEVEL_TIMER NS_DBG_MODULE_LEVEL_BROKER
            NS_DBG_MODULE_LEVEL_MPSC NS_DBG_MODULE_LEVEL_PLATFORM)
        _nanosig_apply_module_definition(${target} PRIVATE ${_name})
    endforeach()
endfunction()
