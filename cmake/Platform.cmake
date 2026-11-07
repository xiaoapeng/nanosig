# ── 平台后端选择 ────────────────────────────────────────────────
# host    : 桌面三后端（默认）。
# freertos: FreeRTOS queue-set 后端；需 NANOSIG_FREERTOS_INCLUDE_DIRS
#           提供 FreeRTOS SDK 头文件路径，或开启 host POSIX harness 自动填充。
# zephyr  : Zephyr k_poll 后端。**不通过顶层 CMake 构建**：Zephyr 有自己的
#           构建系统，本后端经仓库根目录的 zephyr/module.yml 作为 Zephyr module
#           编入（见 zephyr/CMakeLists.txt）。顶层给出 zephyr 时只打印引导信息。
set(NANOSIG_PLATFORM "host" CACHE STRING "nanosig platform backend: host, baremetal, freertos, or zephyr (zephyr builds via Zephyr module)")
set_property(CACHE NANOSIG_PLATFORM PROPERTY STRINGS host baremetal freertos zephyr)

set(NANOSIG_FREERTOS_INCLUDE_DIRS "" CACHE STRING
    "FreeRTOS SDK include dirs (required when NANOSIG_PLATFORM=freertos)")

# FreeRTOS 后端可配置常量；仅 NANOSIG_PLATFORM=freertos 时编入。
set(NANOSIG_FREERTOS_WAITSET_CAP "32" CACHE STRING
    "FreeRTOS queue-set token capacity (Sigma member capacity + max member capacity)")
set(NANOSIG_FREERTOS_TASK_PRIORITY "1" CACHE STRING
    "FreeRTOS platform task priority")

# 裸机后端可配置常量；仅 NANOSIG_PLATFORM=baremetal 时编入。
# 裸机内建一个静态 loop，ring 存储是编译期固定大小的静态数组，故容量不可按
# loop 定制：更小的请求按本值分配，更大的请求使 ns_loop_create 返回 NS_E_INVAL。
set(NANOSIG_BAREMETAL_LOOP_QUEUE_BYTES "1024" CACHE STRING
    "Baremetal static loop MPSC ring capacity in bytes (power of two, >= 3*sizeof(size_t))")
set(NANOSIG_BAREMETAL_POLL_MAX_ROUNDS "4" CACHE STRING
    "Baremetal ns_loop_poll() maximum dispatch rounds per call")

# broker（timer）线程的栈大小，单位字节。0 = 使用平台默认栈大小。
# 编译期锁死：库内只有这一个对外可配的栈旋钮，注入缺失即编译失败
# （src/ns_broker.c 的 #error），不会静默回落到平台默认。
set(NANOSIG_BROKER_STACK_SIZE "0" CACHE STRING
    "Broker (timer) thread stack size in bytes (0 = platform default)")

option(NANOSIG_BUILD_FREERTOS_POSIX
    "Build the FreeRTOS POSIX host harness and FreeRTOS contract tests" OFF)

if(NANOSIG_PLATFORM STREQUAL "freertos")
    if(NANOSIG_BUILD_FREERTOS_POSIX)
        include(cmake/FreeRTOSPosix.cmake)
        nanosig_configure_freertos_posix()
    endif()
    if(NOT NANOSIG_FREERTOS_INCLUDE_DIRS)
        message(FATAL_ERROR
            "NANOSIG_PLATFORM=freertos requires NANOSIG_FREERTOS_INCLUDE_DIRS "
            "(or configure with -DNANOSIG_BUILD_FREERTOS_POSIX=ON)")
    endif()
    set(NANOSIG_PLATFORM_DIR platform/freertos)
elseif(NANOSIG_PLATFORM STREQUAL "baremetal")
    # 裸机后端是**能力子集后端**：只提供 init/shutdown/clock_monotonic_us
    # 三个 ns_platform_* 符号；其余平台原语有意不定义（引用即链接期失败）。
    # broker 与 hashtbl 不编入：前者依赖线程/waitset，后者在 signal+timer
    # 路径上没有任何调用者。
    set(NANOSIG_PLATFORM_DIR platform/baremetal)
elseif(NANOSIG_PLATFORM STREQUAL "host")
    if(WIN32)
        set(NANOSIG_PLATFORM_DIR platform/windows)
    elseif(APPLE)
        set(NANOSIG_PLATFORM_DIR platform/macos)
    elseif(UNIX AND NOT APPLE)
        set(NANOSIG_PLATFORM_DIR platform/linux)
    else()
        message(FATAL_ERROR
            "nanosig host backends support only Linux, macOS, and Windows; "
            "use NANOSIG_PLATFORM=freertos for RTOS targets")
    endif()
elseif(NANOSIG_PLATFORM STREQUAL "zephyr")
    # Zephyr 有独立构建系统；后端经 zephyr/module.yml 作为 module 编入。
    # 顶层构建不链接 Zephyr kernel，故这里给出引导信息而非静默失败。
    message(FATAL_ERROR
        "NANOSIG_PLATFORM=zephyr 不通过顶层 CMake 构建。\n"
        "Zephyr 后端以 Zephyr module 形式接入，请用 Zephyr 构建系统构建测试应用：\n"
        "  west build -b native_sim/native/64 ${CMAKE_CURRENT_SOURCE_DIR}/test/zephyr \\\n"
        "      -- -DEXTRA_ZEPHYR_MODULES=${CMAKE_CURRENT_SOURCE_DIR}\n"
        "或在 west workspace 中把本仓库作为 module 后构建 test/zephyr。\n"
        "顶层构建请使用 -DNANOSIG_PLATFORM=host（默认）。")
else()
    message(FATAL_ERROR
        "NANOSIG_PLATFORM must be 'host', 'baremetal', 'freertos', or 'zephyr' "
        "(got '${NANOSIG_PLATFORM}')")
endif()


# The same backend directory selects both implementation and public platform types.
set(NANOSIG_PLATFORM_SOURCES ${NANOSIG_PLATFORM_DIR}/port.c)

function(nanosig_apply_platform target)
    # PUBLIC: consumers (contract tests / app) must see the same platform types as
    # the library — the platform header is the single source of that layout, and it
    # is selected purely by the include path.
    target_include_directories(${target} PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/${NANOSIG_PLATFORM_DIR}>
    )

    # broker（timer）线程的栈大小：库内部旋钮，PRIVATE 注入（不外泄给消费者，
    # 也不占用公共 API 面）。PRIVATE 而非 PUBLIC，因为唯一使用者是 src/ns_broker.c。
    # 裸机后端不编入 ns_broker.c，故也省略该注入（否则其 #error 守卫会误报）。
    if(NOT NANOSIG_PLATFORM STREQUAL "baremetal")
        target_compile_definitions(${target} PRIVATE
            NANOSIG_BROKER_STACK_SIZE=${NANOSIG_BROKER_STACK_SIZE}
        )
    endif()

    if(NANOSIG_PLATFORM STREQUAL "baremetal")
        # PUBLIC: 测试 harness 需要能读到同一份容量/轮数配置值。
        target_compile_definitions(${target} PUBLIC
            NANOSIG_BAREMETAL_LOOP_QUEUE_BYTES=${NANOSIG_BAREMETAL_LOOP_QUEUE_BYTES}
            NANOSIG_BAREMETAL_POLL_MAX_ROUNDS=${NANOSIG_BAREMETAL_POLL_MAX_ROUNDS}
        )
    endif()

    if(NANOSIG_PLATFORM STREQUAL "freertos")
        # PUBLIC: 后端常量让契约测试能断言同一份配置值（容量与优先级）。
        target_compile_definitions(${target} PUBLIC
            NANOSIG_FREERTOS_WAITSET_CAP=${NANOSIG_FREERTOS_WAITSET_CAP}
            NANOSIG_FREERTOS_TASK_PRIORITY=${NANOSIG_FREERTOS_TASK_PRIORITY}
        )
        target_include_directories(${target} PUBLIC ${NANOSIG_FREERTOS_INCLUDE_DIRS})
        if(TARGET nanosig_freertos_kernel)
            target_link_libraries(${target} PRIVATE nanosig_freertos_kernel)
        endif()
    endif()

    if(UNIX)
        target_link_libraries(${target} PRIVATE Threads::Threads)
    endif()

    if(WIN32 OR UNIX)
        # PUBLIC so consumers (tests, demos) get the same struct layout as the library
        target_compile_definitions(${target} PUBLIC NS_MPSC_CACHELINE_ALIGNED)
    endif()
endfunction()
