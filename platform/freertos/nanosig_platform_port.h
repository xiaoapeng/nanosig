/**
 * @file nanosig_platform_port.h
 * @brief nanosig 平台类型 — FreeRTOS 后端（构建内头，非安装头）。
 * @date 2026-10-17
 *
 * 本头文件把 nanosig 平台层使用的原语类型映射为 FreeRTOS 原语的**透明别名**，
 * 使平台对象全部变为调用方拥有存储。它只装当前平台这一个后端，由构建系统把
 * `platform/<os>/` 加入 include 路径后，按固定名 `nanosig_platform_port.h`
 * 被 `nanosig_port.h` 引入；`nanosig_port.h` 本身不含任何平台判定宏。
 *
 * 前置配置（由后端 CMake 以 PUBLIC 编译定义注入，使用者也会继承）：
 * `NANOSIG_FREERTOS_WAITSET_CAP`；SDK include 目录同样为 PUBLIC。
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#ifndef NANOSIG_PLATFORM_PORT_H
#define NANOSIG_PLATFORM_PORT_H

#include <stddef.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"

/* ================================================================== */
/*  可配置常量（可由构建命令覆盖）                                        */
/* ================================================================== */

/** queue set 固定容量；`waitset_add` 校验 Σ成员容量 + max(成员容量) 不超过它。 */
#ifndef NANOSIG_FREERTOS_WAITSET_CAP
#define NANOSIG_FREERTOS_WAITSET_CAP 32u
#endif

/* ================================================================== */
/*  ABI 一致性链接探针                                                   */
/* ================================================================== */

extern const int ns_platform_abi_freertos;
/** 指向当前平台 ABI 探针符号；消费者 TU 引用它以触发跨 TU 链接检查。 */
#define NS_PLATFORM_ABI_PROBE ns_platform_abi_freertos

/* ================================================================== */
/*  平台类型                                                             */
/* ================================================================== */

/** 平台可等待原语的句柄：FreeRTOS 为 queue-set-capable 对象句柄。 */
typedef void *ns_waitable_handle_t;

/** 句柄有效性谓词：排除 NULL 与 `ns_waitable_init` 写出的 (void *)~0 无效哨兵。 */
#define ns_waitable_handle_is_valid(h) \
    (((h) != NULL) && ((h) != (void *)(uintptr_t)~0))

/** wakeup：每对象独立二值信号量句柄。 */
typedef SemaphoreHandle_t ns_platform_wakeup_t;

/** mutex：互斥信号量句柄。 */
typedef SemaphoreHandle_t ns_platform_mutex_t;

/** thread：任务句柄 + join 信号量 + 入口/参数簿记。 */
typedef struct ns_platform_thread {
    TaskHandle_t          task;
    SemaphoreHandle_t     join_sem;
    ns_platform_thread_fn entry;
    void                 *arg;
} ns_platform_thread_t;

/** waitset：queue set + Σ/max 容量簿记 + slot → 调用方 waitable 映射。 */
typedef struct ns_platform_waitset {
    QueueSetHandle_t                set;
    size_t                          count;
    UBaseType_t                     sum_cap;
    UBaseType_t                     max_cap;
    struct ns_platform_waitable    *slots[NANOSIG_FREERTOS_WAITSET_CAP];
} ns_platform_waitset_t;

/** 抢占保护状态为不透明标记；FreeRTOS 通过任务临界区嵌套计数恢复。 */
typedef UBaseType_t ns_platform_preempt_state_t;

/** 仅任务上下文使用；必须与 leave 配对，不与手工中断掩码操作混用。 */
static inline ns_platform_preempt_state_t ns_platform_preempt_enter(void)
{
    taskENTER_CRITICAL();
    return 0u;
}

/** 恢复进入前的 FreeRTOS 任务临界区嵌套层数。 */
static inline void ns_platform_preempt_leave(ns_platform_preempt_state_t state)
{
    (void)state;
    taskEXIT_CRITICAL();
}

#endif
