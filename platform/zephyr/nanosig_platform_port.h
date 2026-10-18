/**
 * @file nanosig_platform_port.h
 * @brief nanosig 平台类型 — Zephyr 后端（构建内头，非安装头）。
 * @date 2026-10-17
 *
 * 本头文件把 nanosig 平台层使用的原语类型映射为 Zephyr 原语的**透明别名**，
 * 使平台对象变为调用方拥有存储。它只装当前平台这一个后端，由构建系统经
 * `NANOSIG_PLATFORM_HEADER` 注入到 `nanosig_port.h`；`nanosig_port.h` 本身
 * 不含任何平台判定宏。
 *
 * @note 唯一例外是 `ns_platform_thread_t`：它内嵌内核栈（`K_KERNEL_STACK_MEMBER`），
 * 对齐要求为 `Z_KERNEL_STACK_OBJ_ALIGN`。若该类型按值内嵌，这个要求会经堆上的
 * `ns_loop_t` / broker 传递到整个 `ns_platform_alloc`，使所有小对象分配都按栈尺寸
 * 对齐（开 MPU 栈保护时尤其浪费堆）。因此本后端把它做成**由 port 分配的不透明对象**：
 * `ns_platform_thread_init` 按 `Z_KERNEL_STACK_OBJ_ALIGN` 分配，
 * `ns_platform_thread_join` 回收；`ns_platform_alloc` 则保持普通 `k_malloc`。
 * 调用方仍然只声明 `ns_platform_thread_t` 变量并取地址，接口形态不变。
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

/** thread：由 port 分配、`ns_platform_thread_join` 回收的不透明对象（内嵌内核栈）。 */
struct ns_platform_thread;
typedef struct ns_platform_thread *ns_platform_thread_t;

/** waitset：`k_poll` 每次等待按活跃 slot 重建事件数组，故内联存储两者。 */
typedef struct ns_platform_waitset {
    size_t                        count;
    struct ns_platform_waitable  *slots[NS_ZEPHYR_WAITSET_CAP];
    struct k_poll_event           events[NS_ZEPHYR_WAITSET_CAP];
} ns_platform_waitset_t;

#endif
