# FreeRTOSPosix.cmake — host-side FreeRTOS POSIX harness wiring.
#
# Pulls a pinned FreeRTOS-Kernel and provides:
#   * nanosig_freertos_kernel : the kernel + GCC/Posix port + heap_4 as a static
#     library for the host (Linux/macOS) contract harness.
#   * NANOSIG_FREERTOS_INCLUDE_DIRS : include dirs fed to the nanosig FreeRTOS
#     variant so platform/freertos/port.c can include FreeRTOS headers.
#
# This harness is host-only (never the shipped backend). It exists to exercise
# platform/freertos/port.c against real queue-set semantics. The shipped
# FreeRTOS backend is selected with NANOSIG_PLATFORM=freertos and a user-provided
# NANOSIG_FREERTOS_INCLUDE_DIRS (or this harness, default OFF).

include(FetchContent)

# CMP0169: the direct FetchContent_Populate form used below. We populate without
# add_subdirectory on purpose (the vendored kernel ships its own CMakeLists that
# requires FREERTOS_PORT / heap options we do not want to adopt).
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()

function(nanosig_configure_freertos_posix)
    set(_freertos_tag "V11.1.0")
    set(_freertos_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../test/freertos")

    FetchContent_Declare(freertos_kernel
        GIT_REPOSITORY https://github.com/FreeRTOS/FreeRTOS-Kernel.git
        GIT_TAG ${_freertos_tag}
        GIT_SHALLOW TRUE
    )
    FetchContent_GetProperties(freertos_kernel)
    if(NOT freertos_kernel_POPULATED)
        FetchContent_Populate(freertos_kernel)
    endif()

    set(_kernel_root "${freertos_kernel_SOURCE_DIR}")
    set(_posix_port "${_kernel_root}/portable/ThirdParty/GCC/Posix")

    # macOS: the stock POSIX port pins the pthread stack to the (small) FreeRTOS
    # task stack via pthread_attr_setstacksize. On AppleClang that faults inside
    # _pthread_create with a guard-page write, so the first task never runs.
    # Drop the pinned size and let pthread use its default host stack.
    if(APPLE)
        set(_port_c "${_posix_port}/port.c")
        file(READ "${_port_c}" _port_src)
        if(NOT _port_src MATCHES "nanosig:macos-posix-stack-patch")
            set(_patch_needle
"    iRet = pthread_attr_setstacksize( &xThreadAttributes, ulStackSize );")
            set(_patch_repl
"    iRet = 0; /* nanosig:macos-posix-stack-patch: use pthread default stack */\n    ( void ) ulStackSize;")

            # Fail loudly instead of silently leaving the unpatched line behind
            # when a FreeRTOS-Kernel upgrade changes this source line.
            string(FIND "${_port_src}" "${_patch_needle}" _patch_pos)
            if(_patch_pos EQUAL -1)
                message(FATAL_ERROR
                    "nanosig macOS POSIX patch target not found in ${_port_c}. "
                    "FreeRTOS-Kernel ${_freertos_tag} source layout changed; "
                    "re-verify the pthread_attr_setstacksize patch before upgrading.")
            endif()

            string(REPLACE "${_patch_needle}" "${_patch_repl}"
                _port_src "${_port_src}")
            file(WRITE "${_port_c}" "${_port_src}")
        endif()
    endif()

    set(_incs
        "${_kernel_root}/include"
        "${_posix_port}"
        "${_freertos_root}"
    )

    add_library(nanosig_freertos_kernel STATIC
        "${_kernel_root}/tasks.c"
        "${_kernel_root}/queue.c"
        "${_kernel_root}/list.c"
        "${_kernel_root}/timers.c"
        "${_kernel_root}/stream_buffer.c"
        "${_posix_port}/port.c"
        "${_posix_port}/utils/wait_for_event.c"
        "${_kernel_root}/portable/MemMang/heap_4.c"
    )
    target_compile_features(nanosig_freertos_kernel PUBLIC c_std_11)
    target_include_directories(nanosig_freertos_kernel PUBLIC ${_incs})
    if(UNIX)
        target_link_libraries(nanosig_freertos_kernel PUBLIC Threads::Threads)
    endif()

    set(NANOSIG_FREERTOS_INCLUDE_DIRS ${_incs} PARENT_SCOPE)
endfunction()
