option(NANOSIG_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(NANOSIG_ENABLE_TSAN "Enable ThreadSanitizer" OFF)
option(NANOSIG_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)

if(NANOSIG_ENABLE_ASAN AND NANOSIG_ENABLE_TSAN)
    message(FATAL_ERROR "ASAN and TSAN cannot be enabled together")
endif()

function(nanosig_apply_sanitizers target)
    if(MSVC)
        if(NANOSIG_ENABLE_ASAN)
            target_compile_options(${target} PRIVATE /fsanitize=address)
            target_link_options(${target} PRIVATE /fsanitize=address)
        endif()
        return()
    endif()

    set(_nanosig_sanitize_flags "")

    if(NANOSIG_ENABLE_ASAN)
        list(APPEND _nanosig_sanitize_flags -fsanitize=address)
    endif()

    if(NANOSIG_ENABLE_TSAN)
        list(APPEND _nanosig_sanitize_flags -fsanitize=thread)
    endif()

    if(NANOSIG_ENABLE_UBSAN)
        list(APPEND _nanosig_sanitize_flags -fsanitize=undefined)
    endif()

    if(_nanosig_sanitize_flags)
        target_compile_options(${target} PRIVATE ${_nanosig_sanitize_flags} -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE ${_nanosig_sanitize_flags})
    endif()
endfunction()

