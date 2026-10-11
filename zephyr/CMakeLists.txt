# nanosig 作为 Zephyr module 的构建入口。
#
# 顶层 CMakeLists.txt 不构建本后端（NANOSIG_PLATFORM=zephyr 只打印引导信息）；
# Zephyr 有独立构建系统，这里把 nanosig 编译成 Zephyr 库并链接进应用。
# 参见仓库根 zephyr/module.yml。

# 版本头文件由顶层 CMake 的 project(VERSION) 生成；Zephyr 构建里没有 nanosig 的
# project()，故在本目录作用域内显式提供同名变量给 configure_file 使用。
# 保持与顶层 CMakeLists.txt 的 project(nanosig VERSION ...) 一致。
set(PROJECT_VERSION_MAJOR 0)
set(PROJECT_VERSION_MINOR 1)
set(PROJECT_VERSION_PATCH 0)
set(PROJECT_VERSION 0.1.0)

set(NANOSIG_MODULE_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/include)
file(MAKE_DIRECTORY ${NANOSIG_MODULE_INCLUDE_DIR}/nanosig)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/nanosig/nanosig_version.h.in
    ${NANOSIG_MODULE_INCLUDE_DIR}/nanosig/nanosig_version.h
    @ONLY
)

if(CONFIG_NANOSIG_ZEPHYR)
    # 应用与库必须选到同一平台分支、看到同一 waitable 布局，故 include 路径与
    # 平台选择宏对整棵构建树可见（等价于顶层 CMake 的 PUBLIC 传播）。
    zephyr_include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${NANOSIG_MODULE_INCLUDE_DIR}
    )
    zephyr_compile_definitions(
        NANOSIG_PLATFORM_ZEPHYR=1
        NS_MPSC_CACHELINE_ALIGNED
    )

    zephyr_library()

    # 与顶层构建对齐：PUBLIC c_std_11（调用方与库同一 C 标准）。
    target_compile_features(${ZEPHYR_CURRENT_LIBRARY} PUBLIC c_std_11)

    zephyr_library_sources(
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/nanosig.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/ns_broker.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/ns_timer.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/ns_formatio.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/ns_debug.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/ds/ns_hashtbl.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/ds/ns_mpsc_record_ring.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/ds/ns_rbtree.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/ds/ns_ringbuf.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../platform/zephyr/port.c
    )

    # 与顶层同因：%e 极值路径依赖“无 FP contraction”以跨编译器/架构保持一致。
    zephyr_library_compile_options(-ffp-contract=off)
endif()
