option(NANOSIG_WERROR "把编译警告当作错误处理" ON)

function(nanosig_apply_interface_warnings target)
    if(MSVC)
        target_compile_options(${target} INTERFACE
            /W4
            /permissive-
            /Zc:preprocessor
        )
        if(NANOSIG_WERROR)
            target_compile_options(${target} INTERFACE /WX)
        endif()
    else()
        target_compile_options(${target} INTERFACE
            -Wall
            -Wextra
            -Wconversion
            -Wsign-conversion
            -Wno-psabi
        )
        if(NANOSIG_WERROR)
            target_compile_options(${target} INTERFACE -Werror)
        endif()
    endif()
endfunction()

function(nanosig_apply_private_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4
            /permissive-
            /Zc:preprocessor
        )
        if(NANOSIG_WERROR)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wconversion
            -Wsign-conversion
            -Wno-psabi
        )
        if(NANOSIG_WERROR)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()
