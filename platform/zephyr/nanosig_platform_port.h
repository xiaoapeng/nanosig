/**
 * @file nanosig_platform_port.h
 * @brief nanosig 平台类型 — Zephyr 后端（构建内头，非安装头）。
 * @date 2026-10-17
 *
 * 本头文件把 nanosig 平台层使用的原语类型映射为 Zephyr 原语的**透明别名**，
 * 使平台对象全部变为调用方拥有存储。它只装当前平台这一个后端，由构建系统经
 * `NANOSIG_PLATFORM_HEADER` 注入到 `nanosig_port.h`；`nanosig_port.h` 本身
 * 不含任何平台判定宏。
 *
 * @warning `ns_platform_thread_t` 内嵌 `K_KERNEL_STACK_MEMBER`，其对齐要求为
 * `Z_KERNEL_STACK_OBJ_ALIGN`，并因此**抬高整个结构体的对齐要求**。声明的存储
 * （static / 全局 / 结构体成员 / 局部变量）由编译器自动满足该对齐；若用
 * `k_malloc` 在堆上自行分配，必须改用
 * `k_aligned_alloc((size_t)Z_KERNEL_STACK_OBJ_ALIGN, ...)`。
 * `ns_platform_alloc` 已按该对齐返回内存（库内 ns_loop_t / broker 就内联了该类型），
 * 因此用 `ns_platform_alloc` 分配是安全的。
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#ifndef NANOSIG_PLATFORM_PORT_H
#define NANOSIG_PLATFORM_PORT_H

#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>

/* ================================================================== */
/*  可配置常量（合并优先级固定：CONFIG_* > 裸 -D > 默认）                  */
/* ================================================================== */

/** waitset 注册容量（活跃 waitable 数上限）。 */
#if defined(CONFIG_NANOSIG_ZEPHYR_WAITSET_CAP)
#define NS_ZEPHYR_WAITSET_CAP CONFIG_NANOSIG_ZEPHYR_WAITSET_CAP
#elif defined(NANOSIG_ZEPHYR_WAITSET_CAP)
#define NS_ZEPHYR_WAITSET_CAP NANOSIG_ZEPHYR_WAITSET_CAP
#else
#define NS_ZEPHYR_WAITSET_CAP 32
#endif

/** 平台线程的内嵌内核栈字节数。 */
#if defined(CONFIG_NANOSIG_ZEPHYR_THREAD_STACK_SIZE)
#define NS_ZEPHYR_THREAD_STACK_SIZE CONFIG_NANOSIG_ZEPHYR_THREAD_STACK_SIZE
#elif defined(NANOSIG_ZEPHYR_THREAD_STACK_SIZE)
#define NS_ZEPHYR_THREAD_STACK_SIZE NANOSIG_ZEPHYR_THREAD_STACK_SIZE
#else
#define NS_ZEPHYR_THREAD_STACK_SIZE 4096
#endif

/** 平台线程优先级（抢占式）。 */
#if defined(CONFIG_NANOSIG_ZEPHYR_THREAD_PRIORITY)
#define NS_ZEPHYR_THREAD_PRIORITY CONFIG_NANOSIG_ZEPHYR_THREAD_PRIORITY
#elif defined(NANOSIG_ZEPHYR_THREAD_PRIORITY)
#define NS_ZEPHYR_THREAD_PRIORITY NANOSIG_ZEPHYR_THREAD_PRIORITY
#else
#define NS_ZEPHYR_THREAD_PRIORITY K_PRIO_PREEMPT(1)
#endif

/* ================================================================== */
/*  ABI 一致性链接探针                                                   */
/* ================================================================== */

extern const int ns_platform_abi_zephyr;
/** 指向当前平台 ABI 探针符号；消费者 TU 引用它以触发跨 TU 链接检查。 */
#define NS_PLATFORM_ABI_PROBE ns_platform_abi_zephyr

/* ================================================================== */
/*  平台类型                                                             */
/* ================================================================== */

/** 平台可等待原语的句柄：Zephyr v1 支持 signal 类，即 `struct k_poll_signal *`。 */
typedef void *ns_waitable_handle_t;

/** 句柄有效性谓词：排除 NULL 与 `ns_waitable_init` 写出的 (void *)~0 无效哨兵。 */
#define ns_waitable_handle_is_valid(h) \
    (((h) != NULL) && ((h) != (void *)(uintptr_t)~0))

/** wakeup：每对象独立 `k_sem`（上限 1，二值语义），按值内嵌。 */
typedef struct k_sem ns_platform_wakeup_t;

/** mutex：`k_mutex`，按值内嵌。 */
typedef struct k_mutex ns_platform_mutex_t;

/** thread：`k_thread` + 内嵌内核栈 + 入口/参数簿记。 */
typedef struct ns_platform_thread {
    struct k_thread        thread;
    ns_platform_thread_fn  entry;
    void                  *arg;
    K_KERNEL_STACK_MEMBER(stack, NS_ZEPHYR_THREAD_STACK_SIZE);
} ns_platform_thread_t;

/** waitset：`k_poll` 每次等待按活跃 slot 重建事件数组，故内联存储两者。 */
typedef struct ns_platform_waitset {
    size_t                        count;
    struct ns_platform_waitable  *slots[NS_ZEPHYR_WAITSET_CAP];
    struct k_poll_event           events[NS_ZEPHYR_WAITSET_CAP];
} ns_platform_waitset_t;

#endif
