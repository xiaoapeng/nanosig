cmake_minimum_required(VERSION 3.20)

project(nanosig
    VERSION 0.1.0
    DESCRIPTION "C11 thread-based signal/slot library"
    LANGUAGES C CXX
)

# ── 版本头文件生成 ──────────────────────────────────────────────
# 以 CMakeLists.txt 中 project(VERSION) 为唯一事实来源，
# 自动生成 include/nanosig/nanosig_version.h（含 MAJOR / MINOR / PATCH / STRING）。
configure_file(
    ${PROJECT_SOURCE_DIR}/include/nanosig/nanosig_version.h.in
    ${PROJECT_BINARY_DIR}/include/nanosig/nanosig_version.h
    @ONLY
)

include(cmake/CompileWarnings.cmake)
include(cmake/Sanitizers.cmake)
include(cmake/Coverage.cmake)
include(cmake/ApiCompileChecks.cmake)
include(cmake/SanitizeAll.cmake)

if(UNIX)
    find_package(Threads REQUIRED)
endif()

option(NANOSIG_BUILD_TESTS "Build nanosig tests" ON)
option(NANOSIG_BUILD_DEMOS "Build nanosig demo executables" ON)
option(NANOSIG_BUILD_BENCH "Build nanosig benchmarks" OFF)
option(NANOSIG_BUILD_API_CHECKS "Register compile-only API checks" ON)

# ── 平台后端选择 ────────────────────────────────────────────────
# host    : 桌面三后端（默认）。
# freertos: FreeRTOS queue-set 后端；需 NANOSIG_FREERTOS_INCLUDE_DIRS
#           提供 FreeRTOS SDK 头文件路径，或开启 host POSIX harness 自动填充。
# zephyr  : Zephyr k_poll 后端。**不通过顶层 CMake 构建**：Zephyr 有自己的
#           构建系统，本后端经仓库根目录的 zephyr/module.yml 作为 Zephyr module
#           编入（见 zephyr/CMakeLists.txt）。顶层给出 zephyr 时只打印引导信息。
set(NANOSIG_PLATFORM "host" CACHE STRING "nanosig platform backend: host, freertos, or zephyr (zephyr builds via Zephyr module)")
set_property(CACHE NANOSIG_PLATFORM PROPERTY STRINGS host freertos zephyr)

set(NANOSIG_FREERTOS_INCLUDE_DIRS "" CACHE STRING
    "FreeRTOS SDK include dirs (required when NANOSIG_PLATFORM=freertos)")

# FreeRTOS 后端可配置常量；仅 NANOSIG_PLATFORM=freertos 时编入。
set(NANOSIG_FREERTOS_WAITSET_CAP "32" CACHE STRING
    "FreeRTOS queue-set token capacity (Sigma member capacity + max member capacity)")
set(NANOSIG_FREERTOS_THREAD_STACK_WORDS "1024" CACHE STRING
    "FreeRTOS platform task stack depth in StackType_t words")
set(NANOSIG_FREERTOS_TASK_PRIORITY "1" CACHE STRING
    "FreeRTOS platform task priority")

option(NANOSIG_BUILD_FREERTOS_POSIX
    "Build the FreeRTOS POSIX host harness and FreeRTOS contract tests" OFF)

set(NANOSIG_DS_SOURCES
    src/ds/ns_hashtbl.c
    src/ds/ns_mpsc_record_ring.c
    src/ds/ns_rbtree.c
    src/ds/ns_ringbuf.c
)

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
    set(NANOSIG_PLATFORM_SOURCES
        platform/freertos/port.c
    )
elseif(NANOSIG_PLATFORM STREQUAL "host")
    if(WIN32)
        set(NANOSIG_PLATFORM_SOURCES
            platform/windows/port.c
        )
    elseif(APPLE)
        set(NANOSIG_PLATFORM_SOURCES
            platform/macos/port.c
        )
    elseif(UNIX AND NOT APPLE)
        set(NANOSIG_PLATFORM_SOURCES
            platform/linux/port.c
        )
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
        "NANOSIG_PLATFORM must be 'host', 'freertos', or 'zephyr' "
        "(got '${NANOSIG_PLATFORM}')")
endif()

add_library(nanosig STATIC
    src/nanosig.c
    src/ns_broker.c
    src/ns_timer.c
    src/ns_formatio.c
    src/ns_debug.c
    ${NANOSIG_PLATFORM_SOURCES}
    ${NANOSIG_DS_SOURCES}
)
add_library(nanosig::nanosig ALIAS nanosig)

# Deterministic float formatting: the %e extreme-value path derives its digits
# from a power-of-10 polynomial approximation, which is sensitive to FP
# contraction (FMA). arm64 always has FMA and Clang/GCC contract by default,
# while MSVC /fp:precise and x86-64 baseline builds do not — producing
# different low-order digits. Disabling contraction keeps the formatter output
# identical across supported compilers/architectures.
if(NOT MSVC)
    set_source_files_properties(src/ns_formatio.c PROPERTIES
        COMPILE_OPTIONS "-ffp-contract=off"
    )
endif()

target_compile_features(nanosig PUBLIC c_std_11)
target_include_directories(nanosig PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_include_directories(nanosig PRIVATE
    ${PROJECT_SOURCE_DIR}
)

if(NANOSIG_PLATFORM STREQUAL "freertos")
    # PUBLIC: consumers (contract tests / app) must select the same platform
    # branch and see the same waitable layout as the library. The backend
    # constants are PUBLIC too so the contract tests can assert against the
    # same configured capacity/stack/priority values.
    target_compile_definitions(nanosig PUBLIC
        NANOSIG_PLATFORM_FREERTOS=1
        NANOSIG_FREERTOS_WAITSET_CAP=${NANOSIG_FREERTOS_WAITSET_CAP}
        NANOSIG_FREERTOS_THREAD_STACK_WORDS=${NANOSIG_FREERTOS_THREAD_STACK_WORDS}
        NANOSIG_FREERTOS_TASK_PRIORITY=${NANOSIG_FREERTOS_TASK_PRIORITY}
    )
    target_include_directories(nanosig PUBLIC ${NANOSIG_FREERTOS_INCLUDE_DIRS})
    if(TARGET nanosig_freertos_kernel)
        target_link_libraries(nanosig PRIVATE nanosig_freertos_kernel)
    endif()
endif()

if(UNIX)
    target_link_libraries(nanosig PRIVATE Threads::Threads)
endif()

if(WIN32 OR UNIX)
    # PUBLIC so consumers (tests, demos) get the same struct layout as the library
    target_compile_definitions(nanosig PUBLIC NS_MPSC_CACHELINE_ALIGNED)
endif()

nanosig_apply_private_warnings(nanosig)
nanosig_apply_sanitizers(nanosig)
nanosig_apply_coverage(nanosig)

enable_testing()

function(nanosig_add_unit_test name source)
    add_executable(${name}
        ${source}
    )
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
    )
endfunction()

if(NANOSIG_BUILD_TESTS AND NANOSIG_PLATFORM STREQUAL "host")
    # Event broker tests — split by concern for faster parallel builds
    nanosig_add_unit_test(nanosig_test_broker_lifecycle test/unit/test_broker_lifecycle.c)
    target_compile_definitions(nanosig_test_broker_lifecycle PRIVATE NANOSIG_TEST)
    nanosig_add_unit_test(nanosig_test_broker_event test/unit/test_broker_event.c)
    target_compile_definitions(nanosig_test_broker_event PRIVATE NANOSIG_TEST)
    nanosig_add_unit_test(nanosig_test_broker_consume_fn test/unit/test_broker_consume_fn.c)
    target_compile_definitions(nanosig_test_broker_consume_fn PRIVATE NANOSIG_TEST)
    nanosig_add_unit_test(nanosig_test_broker_multi test/unit/test_broker_multi.c)
    target_compile_definitions(nanosig_test_broker_multi PRIVATE NANOSIG_TEST)
    target_compile_definitions(nanosig PRIVATE NANOSIG_TEST)
    nanosig_add_unit_test(nanosig_test_platform_backend test/unit/test_platform_backend.c)
    nanosig_add_unit_test(nanosig_test_loop test/unit/test_loop.c)
    nanosig_add_unit_test(nanosig_test_signal test/unit/test_signal.c)
    nanosig_add_unit_test(nanosig_test_timer test/unit/test_timer.c)
    nanosig_add_unit_test(nanosig_test_mpsc_record_ring test/unit/test_mpsc_record_ring.c)
    nanosig_add_unit_test(nanosig_test_ds_list test/unit/test_ds_list.c)
    nanosig_add_unit_test(nanosig_test_ds_slist test/unit/test_ds_slist.c)
    nanosig_add_unit_test(nanosig_test_ds_ringbuf test/unit/test_ds_ringbuf.c)
    nanosig_add_unit_test(nanosig_test_ds_hashtable test/unit/test_ds_hashtable.c)
    nanosig_add_unit_test(nanosig_test_ds_rbtree test/unit/test_ds_rbtree.c)
    nanosig_add_unit_test(nanosig_test_formatio test/unit/test_formatio.c)
    nanosig_add_unit_test(nanosig_test_debug test/unit/test_debug.c)

    # ── P0 高危 API 缺口补齐 ──────────────────────────────────────
    # 单一文件、单一关注点；每文件对应报告 §2.1 表格中的一行。
    nanosig_add_unit_test(nanosig_test_signal_null_slot test/unit/test_signal_null_slot.c)
    nanosig_add_unit_test(nanosig_test_signal_double_deinit test/unit/test_signal_double_deinit.c)
    nanosig_add_unit_test(nanosig_test_loop_reuse test/unit/test_loop_reuse.c)
    nanosig_add_unit_test(nanosig_test_timer_deinit_use test/unit/test_timer_deinit_use.c)
    nanosig_add_unit_test(nanosig_test_broker_concurrent_remove test/unit/test_broker_concurrent_remove.c)

    # Previously unregistered stress test
    nanosig_add_unit_test(nanosig_test_mpsc_record_ring_stress test/unit/test_mpsc_record_ring_stress.c)
    set_tests_properties(nanosig_test_mpsc_record_ring_stress PROPERTIES
        TIMEOUT 1300
        LABELS "stress;long-stability"
        ENVIRONMENT "NS_MPSC_RECORD_RING_STRESS_DURATION_SEC=20")

    # ==========================================
    # Integration tests — layered targets
    # ==========================================
    # Layer-1: Basic complex combinations (4 scenarios)
    nanosig_add_unit_test(nanosig_test_integration_layer1 test/integration/test_layer1.c)
    set_tests_properties(nanosig_test_integration_layer1 PROPERTIES LABELS "integration;quick" TIMEOUT 30)

    # Layer-2: Stress tests (4 scenarios)
    nanosig_add_unit_test(nanosig_test_integration_layer2 test/integration/test_layer2.c)
    set_tests_properties(nanosig_test_integration_layer2 PROPERTIES LABELS "integration;quick" TIMEOUT 60)

    # Layer-3: Behavior verification (6 scenarios — includes marathon/idle)
    nanosig_add_unit_test(nanosig_test_integration_layer3 test/integration/test_layer3.c)
    set_tests_properties(nanosig_test_integration_layer3 PROPERTIES LABELS "integration;nightly" TIMEOUT 180)

    # Long-stability: standalone for TIMEOUT+LABELS (1 scenario)
    nanosig_add_unit_test(nanosig_test_integration_hive test/integration/test_hive.c)
    set_tests_properties(nanosig_test_integration_hive PROPERTIES
        LABELS "integration;nightly;long-stability" TIMEOUT 1500)

    # ── Individual scenarios — each is also a standalone test ──
    foreach(scenario_name
        cascade cleanup_verify cocktail dual_loop idle
        lifecycle_chaos lifecycle_marathon multi_route
        random_chaos reentrant_race signal_storm
        timer_precision timer_storm watchdog)
        nanosig_add_unit_test(nanosig_test_scenario_${scenario_name}
            test/integration/scenario_${scenario_name}.c)
        target_compile_definitions(nanosig_test_scenario_${scenario_name} PRIVATE SCENARIO_MAIN)
        set_tests_properties(nanosig_test_scenario_${scenario_name}
            PROPERTIES LABELS "integration;quick" TIMEOUT 30)
    endforeach()
endif()

# ── FreeRTOS contract tests (host POSIX harness only) ────────────────
# These exercise platform/freertos/port.c against real queue-set semantics.
# Host-only tests above are intentionally NOT registered in this configuration.
if(NANOSIG_BUILD_TESTS AND NANOSIG_PLATFORM STREQUAL "freertos" AND NANOSIG_BUILD_FREERTOS_POSIX)
    nanosig_add_unit_test(nanosig_test_freertos_waitset test/freertos/test_freertos_waitset.c)
    set_tests_properties(nanosig_test_freertos_waitset PROPERTIES TIMEOUT 60)
    nanosig_add_unit_test(nanosig_test_freertos_broker test/freertos/test_freertos_broker.c)
    set_tests_properties(nanosig_test_freertos_broker PROPERTIES TIMEOUT 60)
endif()

if(NANOSIG_BUILD_DEMOS AND NANOSIG_PLATFORM STREQUAL "host")
    nanosig_add_unit_test(nanosig_demo_same_thread demos/demo_same_thread.c)
    nanosig_add_unit_test(nanosig_demo_cross_thread demos/demo_cross_thread.c)
    set_tests_properties(nanosig_demo_cross_thread PROPERTIES TIMEOUT 10)
    nanosig_add_unit_test(nanosig_demo_timer_cross_thread demos/demo_timer_cross_thread.c)
    set_tests_properties(nanosig_demo_timer_cross_thread PROPERTIES TIMEOUT 10)
endif()

if(NANOSIG_BUILD_BENCH AND NANOSIG_PLATFORM STREQUAL "host")
    nanosig_add_unit_test(nanosig_bench_same_thread bench/bench_same_thread.c)
    nanosig_add_unit_test(nanosig_bench_cross_thread bench/bench_cross_thread.c)
    nanosig_add_unit_test(nanosig_bench_throughput bench/bench_throughput.c)
    set_tests_properties(nanosig_bench_throughput PROPERTIES TIMEOUT 30)
endif()

if(NANOSIG_BUILD_API_CHECKS AND NANOSIG_PLATFORM STREQUAL "host")
    nanosig_add_api_compile_check(nanosig_api_check_macro_matrix
        test/unit/test_macro_expansion.c
    )

    nanosig_add_api_compile_check(nanosig_api_check_platform_contract
        test/unit/test_platform_contract_compile.c
    )

    nanosig_add_api_compile_check(nanosig_api_check_broker_contract
        test/unit/test_broker_contract_compile.c
    )

    nanosig_add_api_compile_check(nanosig_api_check_data_structures
        test/unit/test_data_structures_contract_compile.c
    )

    nanosig_add_api_compile_check(nanosig_api_check_types
        test/unit/test_types_contract_compile.c
    )

    nanosig_add_cpp_compat_check(nanosig_api_check_cpp_compat
        test/unit/test_cpp_compat_compile.cpp
    )

    add_custom_target(api-compile-checks
        DEPENDS
            nanosig_api_check_macro_matrix
            nanosig_api_check_platform_contract
            nanosig_api_check_broker_contract
            nanosig_api_check_data_structures
            nanosig_api_check_types
            nanosig_api_check_cpp_compat
    )
endif()

# sanitize-all — driven by cmake/SanitizeAll.cmake; see that module for the
# audit pipeline (A1 api-contract, A2 config consistency, A3 header tags, A4
# encoding). Runs only when NANOSIG_ENABLE_ASAN / TSAN / UBSAN is ON.
nanosig_configure_sanitize_all()

# ── Coverage report ──────────────────────────────────────────────────
if(NANOSIG_ENABLE_COVERAGE)
    find_program(LCOV lcov)
    find_program(GENHTML genhtml)
    if(LCOV AND GENHTML)
        add_custom_target(coverage
            COMMAND ${LCOV} --capture --ignore-errors gcov,negative --directory . --output-file coverage.info
            COMMAND ${LCOV} --remove coverage.info --ignore-errors unused '/usr/*' '*/test/*' '*/include/nanosig/internal/*' --output-file coverage.info
            COMMAND ${GENHTML} coverage.info --output-directory coverage_report
            COMMAND ${CMAKE_COMMAND} -E echo "Coverage report: file://${CMAKE_BINARY_DIR}/coverage_report/index.html"
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Generating code coverage report..."
        )
    else()
        add_custom_target(coverage
            COMMAND ${CMAKE_COMMAND} -E echo "lcov and/or genhtml not found — install with: sudo apt install lcov"
        )
    endif()
endif()
