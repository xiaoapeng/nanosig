function(nanosig_apply_interface_warnings target)
    if(MSVC)
        target_compile_options(${target} INTERFACE
            /W4
            /permissive-
            /Zc:preprocessor
        )
    else()
        target_compile_options(${target} INTERFACE
            -Wall
            -Wextra
            -Wconversion
            -Wsign-conversion
            -Wno-psabi
        )
    endif()
endfunction()

function(nanosig_apply_private_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4
            /permissive-
            /Zc:preprocessor
        )
    else()
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wconversion
            -Wsign-conversion
            -Wno-psabi
        )
    endif()
endfunction()
