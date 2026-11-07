if(NANOSIG_BUILD_TESTS AND NANOSIG_PLATFORM STREQUAL "host")
    # Event broker tests — split by concern for faster parallel builds
    nanosig_add_test_program(nanosig_test_broker_lifecycle unit/test_broker_lifecycle.c)
    target_compile_definitions(nanosig_test_broker_lifecycle PRIVATE NANOSIG_TEST)
    nanosig_add_test_program(nanosig_test_broker_event unit/test_broker_event.c)
    target_compile_definitions(nanosig_test_broker_event PRIVATE NANOSIG_TEST)
    nanosig_add_test_program(nanosig_test_broker_handler unit/test_broker_handler.c)
    target_compile_definitions(nanosig_test_broker_handler PRIVATE NANOSIG_TEST)
    nanosig_add_test_program(nanosig_test_broker_multi unit/test_broker_multi.c)
    target_compile_definitions(nanosig_test_broker_multi PRIVATE NANOSIG_TEST)
    nanosig_add_test_program(nanosig_test_broker_delivery unit/test_broker_delivery.c)
    target_compile_definitions(nanosig_test_broker_delivery PRIVATE NANOSIG_TEST)
    # broker add/remove 是同步会合且无等待上界：契约被违反时表现为永久阻塞，
    # 没有 TIMEOUT 会拖到 ctest 默认上限（1500s）并烧掉 CI job。
    set_tests_properties(
        nanosig_test_broker_lifecycle
        nanosig_test_broker_event
        nanosig_test_broker_handler
        nanosig_test_broker_multi
        nanosig_test_broker_delivery
        PROPERTIES TIMEOUT 60)
    target_compile_definitions(nanosig PRIVATE NANOSIG_TEST)
    nanosig_add_test_program(nanosig_test_platform_backend unit/test_platform_backend.c)
    nanosig_add_test_program(nanosig_test_loop unit/test_loop.c)
    nanosig_add_test_program(nanosig_test_signal unit/test_signal.c)
    nanosig_add_test_program(nanosig_test_timer unit/test_timer.c)
    nanosig_add_test_program(nanosig_test_mpsc_record_ring unit/test_mpsc_record_ring.c)
    nanosig_add_test_program(nanosig_test_ds_list unit/test_ds_list.c)
    nanosig_add_test_program(nanosig_test_ds_slist unit/test_ds_slist.c)
    nanosig_add_test_program(nanosig_test_ds_ringbuf unit/test_ds_ringbuf.c)
    nanosig_add_test_program(nanosig_test_ds_hashtable unit/test_ds_hashtable.c)
    nanosig_add_test_program(nanosig_test_ds_rbtree unit/test_ds_rbtree.c)
    nanosig_add_test_program(nanosig_test_formatio unit/test_formatio.c)
    nanosig_add_test_program(nanosig_test_debug unit/test_debug.c)

    # ── 高危 API 缺口补齐 ─────────────────────────────────────────
    # 单一文件、单一关注点；每文件对应报告 §2.1 表格中的一行。
    nanosig_add_test_program(nanosig_test_signal_null_slot unit/test_signal_null_slot.c)
    nanosig_add_test_program(nanosig_test_signal_double_deinit unit/test_signal_double_deinit.c)
    nanosig_add_test_program(nanosig_test_loop_reuse unit/test_loop_reuse.c)
    # C++ 侧的运行时契约：test_cpp_compat_compile.cpp 只做 -fsyntax-only 且无 main，
    # 覆盖不到 NS_LOOP_CONFIG_DEFAULT() 的 C++ 位置初始化分支。
    nanosig_add_test_program(nanosig_test_cpp_compat_runtime unit/test_cpp_compat_runtime.cpp)
    nanosig_add_test_program(nanosig_test_timer_deinit_use unit/test_timer_deinit_use.c)
    nanosig_add_test_program(nanosig_test_broker_concurrent_remove unit/test_broker_concurrent_remove.c)
    set_tests_properties(nanosig_test_broker_concurrent_remove PROPERTIES TIMEOUT 60)

    # Previously unregistered stress test
    nanosig_add_test_program(nanosig_test_mpsc_record_ring_stress unit/test_mpsc_record_ring_stress.c)
    set_tests_properties(nanosig_test_mpsc_record_ring_stress PROPERTIES
        TIMEOUT 1300
        LABELS "stress;long-stability"
        ENVIRONMENT "NS_MPSC_RECORD_RING_STRESS_DURATION_SEC=20")

    # ==========================================
    # Integration tests — layered targets
    # ==========================================
    # Layer-1: Basic complex combinations (4 scenarios)
    nanosig_add_test_program(nanosig_test_integration_layer1 integration/test_layer1.c)
    set_tests_properties(nanosig_test_integration_layer1 PROPERTIES LABELS "integration;quick" TIMEOUT 30)

    # Layer-2: Stress tests (4 scenarios)
    nanosig_add_test_program(nanosig_test_integration_layer2 integration/test_layer2.c)
    set_tests_properties(nanosig_test_integration_layer2 PROPERTIES LABELS "integration;quick" TIMEOUT 60)

    # Layer-3: Behavior verification (6 scenarios — includes marathon/idle)
    nanosig_add_test_program(nanosig_test_integration_layer3 integration/test_layer3.c)
    set_tests_properties(nanosig_test_integration_layer3 PROPERTIES LABELS "integration;nightly" TIMEOUT 180)

    # Long-stability: standalone for TIMEOUT+LABELS (1 scenario)
    nanosig_add_test_program(nanosig_test_integration_hive integration/test_hive.c)
    set_tests_properties(nanosig_test_integration_hive PROPERTIES
        LABELS "integration;nightly;long-stability" TIMEOUT 1500)

    # ── Individual scenarios — each is also a standalone test ──
    foreach(scenario_name
        cascade cleanup_verify cocktail dual_loop idle
        lifecycle_chaos lifecycle_marathon multi_route
        random_chaos reentrant_race signal_storm
        timer_precision timer_storm watchdog)
        nanosig_add_test_program(nanosig_test_scenario_${scenario_name}
            integration/scenario_${scenario_name}.c)
        target_compile_definitions(nanosig_test_scenario_${scenario_name} PRIVATE SCENARIO_MAIN)
        set_tests_properties(nanosig_test_scenario_${scenario_name}
            PROPERTIES LABELS "integration;quick" TIMEOUT 30)
    endforeach()
endif()

# ── FreeRTOS contract tests (host POSIX harness only) ────────────────
# These exercise platform/freertos/port.c against real queue-set semantics.
# Host-only tests above are intentionally NOT registered in this configuration.
if(NANOSIG_BUILD_TESTS AND NANOSIG_PLATFORM STREQUAL "freertos" AND NANOSIG_BUILD_FREERTOS_POSIX)
    nanosig_add_test_program(nanosig_test_freertos_waitset freertos/test_freertos_waitset.c)
    set_tests_properties(nanosig_test_freertos_waitset PROPERTIES TIMEOUT 60)
    nanosig_add_test_program(nanosig_test_freertos_broker freertos/test_freertos_broker.c)
    set_tests_properties(nanosig_test_freertos_broker PROPERTIES TIMEOUT 60)
endif()

if(NANOSIG_BUILD_TESTS AND NANOSIG_PLATFORM STREQUAL "baremetal")
    # 裸机契约测试：假时钟驱动，覆盖 AC4–AC9 与 AC15。
    nanosig_add_test_program(nanosig_test_baremetal baremetal/main.c)
    nanosig_add_test_program(nanosig_test_baremetal_cpp_link baremetal/test_cpp_link.cpp)
    target_compile_features(nanosig_test_baremetal_cpp_link PRIVATE cxx_std_11)

    # 负向门：watcher/broker API 在裸机后端**必须**链接不过。
    # 放在配置期做，避免把一个注定失败的目标挂到默认构建里。
    # 把库源与负向用例一起编：configure 期还没有 libnanosig.a，
    # 用 LINK_LIBRARIES 会退化成 "library not found"，门就成了永真摆设。
    set(NANOSIG_NEGLINK_SOURCES ${NANOSIG_LIB_SOURCES})
    list(TRANSFORM NANOSIG_NEGLINK_SOURCES PREPEND "${PROJECT_SOURCE_DIR}/")
    get_target_property(_neg_definitions nanosig COMPILE_DEFINITIONS)
    list(TRANSFORM _neg_definitions PREPEND "-D")
    try_compile(NANOSIG_BAREMETAL_NEGLINK_OK
        ${PROJECT_BINARY_DIR}/neg_link
        SOURCES ${NANOSIG_NEGLINK_SOURCES} ${PROJECT_SOURCE_DIR}/test/baremetal/no_watcher.c
        CMAKE_FLAGS
            "-DINCLUDE_DIRECTORIES=${PROJECT_SOURCE_DIR}/include;${PROJECT_SOURCE_DIR}/include/nanosig;${PROJECT_SOURCE_DIR}/${NANOSIG_PLATFORM_DIR};${PROJECT_BINARY_DIR}/include"
        COMPILE_DEFINITIONS ${_neg_definitions}
        OUTPUT_VARIABLE NANOSIG_BAREMETAL_NEGLINK_OUT
    )
    if(NANOSIG_BAREMETAL_NEGLINK_OK)
        message(FATAL_ERROR
            "baremetal backend must NOT link ns_watcher_init: watcher/broker "
            "APIs are out of scope and their symbols must be absent.\n"
            "${NANOSIG_BAREMETAL_NEGLINK_OUT}")
    endif()
    # 反向断言：try_compile 失败也可能只是"根本没编译到链接阶段"（include 路径错、
    # 语法错等），那样这道门什么都没验证却照样打印通过。要求输出里确实出现
    # 未定义符号的诊断，否则视为门本身失效。
    if(NOT NANOSIG_BAREMETAL_NEGLINK_OUT MATCHES "ns_watcher_init"
       AND NOT NANOSIG_BAREMETAL_NEGLINK_OUT MATCHES "undefined reference"
       AND NOT NANOSIG_BAREMETAL_NEGLINK_OUT MATCHES "Undefined symbols")
        message(FATAL_ERROR
            "baremetal negative-link gate did not reach the linker: the try_compile "
            "output contains no unresolved-symbol diagnostic, so 'link failure' was "
            "caused by something else (bad include path, syntax error, ...) and the "
            "gate verified nothing.\n"
            "${NANOSIG_BAREMETAL_NEGLINK_OUT}")
    endif()
    message(STATUS "baremetal: ns_watcher_init correctly absent (link-time gate ok)")
endif()

# API checks remain available even when runtime tests are disabled.
if(NANOSIG_BUILD_API_CHECKS)
    include(${PROJECT_SOURCE_DIR}/cmake/ApiCompileChecks.cmake)
    add_custom_target(api-compile-checks)
    nanosig_add_api_compile_check(nanosig_api_check_platform_contract
        unit/test_platform_contract_compile.c)
    if(NOT NANOSIG_PLATFORM STREQUAL "freertos")
        nanosig_add_api_compile_check(nanosig_api_check_macro_matrix
            unit/test_macro_expansion.c)
        nanosig_add_api_compile_check(nanosig_api_check_data_structures
            unit/test_data_structures_contract_compile.c)
        nanosig_add_api_compile_check(nanosig_api_check_types
            unit/test_types_contract_compile.c)
        nanosig_add_api_compile_check(nanosig_api_check_cpp_compat
            unit/test_cpp_compat_compile.cpp LANGUAGE CXX)
    endif()
    if(NANOSIG_PLATFORM STREQUAL "host")
        nanosig_add_api_compile_check(nanosig_api_check_broker_contract
            unit/test_broker_contract_compile.c)
    endif()
endif()
