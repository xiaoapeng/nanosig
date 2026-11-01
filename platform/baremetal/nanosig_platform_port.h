/**
 * @file nanosig_platform_port.h
 * @brief nanosig 平台类型 — 裸机后端（构建内头，非安装头）。
 * @date 2026-10-31
 *
 * 本头文件把裸机后端使用的平台原语类型定义为**完整的哑类型**，使
 * `nanosig/nanosig_port.h` 中的函数声明在语法上成立。
 *
 * @section 能力子集
 * 裸机后端是**能力子集后端**：它只提供 signal/slot 与 timer 所需的最小平台面。
 *
 * 提供的 `ns_platform_*` 符号**只有三个**：
 * - `ns_platform_init` / `ns_platform_shutdown`
 * - `ns_platform_clock_monotonic_us`
 *
 * 其余平台原语**完全不提供符号**（不是空桩、不是返回 `NS_E_UNSUPPORTED`）：
 * `alloc` / `free` / `mutex_*` / `wakeup_*` / `event_*` / `waitset_*` / `thread_*`。
 * 引用它们会在**链接期**失败——这是刻意的、可诊断的行为，见
 * `platform/README.md` 的「裸机后端」一节。
 *
 * 对应的核心层调用点与结构体成员由 `NANOSIG_BAREMETAL` 条件编译屏蔽：
 * `ns_signal_t.mutex`、`ns_loop_t.wakeup`、`ns_loop_t.async_thread`、
 * `ns_timer_mgr.mutex`、`ns_dbg_mutex`。因此本头提供的哑类型在该构建中
 * **永远不会产生运行期实例**。
 *
 * @section 单执行上下文
 * 裸机后端假定只有一个执行上下文（应用主循环）。锁与唤醒在该前提下
 * 在构造上无对象，故不提供、也不模拟。
 *
 * @note `ns_platform_event_t` **不由本头定义**：它由 `nanosig_port.h` 自身
 *       定义（见该文件 «ns_platform_event_t» 一节）。在本头重复定义会造成
 *       `typedef redefinition` 编译错误。
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#ifndef NANOSIG_PLATFORM_PORT_H
#define NANOSIG_PLATFORM_PORT_H

#include <stddef.h>
#include <stdint.h>

/* ================================================================== */
/*  能力宏                                                              */
/* ================================================================== */

/**
 * @brief 裸机后端判定宏。
 *
 * 这是**唯一**的裸机判定源。它在 `include/nanosig/` 中仅允许作为
 * `nanosig_signal.h` 的成员声明守卫出现；`src/` 中的使用点见
 * `docs/ARCHITECTURE.md` 的「耦合约束」一节。禁止引入任何 OS 身份宏。
 */
#define NANOSIG_BAREMETAL 1

/* ================================================================== */
/*  可配置常量                                                          */
/* ================================================================== */

/**
 * @brief 裸机静态 loop 的 MPSC ring 字节容量。
 *
 * 裸机后端内建**一个**静态 loop 实例，其 ring 存储是编译期固定大小的静态数组，
 * 因此不支持按 loop 定制容量：`ns_loop_create()` 在 `queue_byte_capacity`
 * 超过本值 时返回 `NS_E_INVAL`（不超过时按本值分配，实际容量恒为本值）。
 *
 * 必须是 2 的幂且不小于 `sizeof(size_t) * 3`（`ns_mpsc_record_ring_init` 的前置条件）。
 * 可在编译期覆盖：`-DNANOSIG_BAREMETAL_LOOP_QUEUE_BYTES=<n>`。
 */
#ifndef NANOSIG_BAREMETAL_LOOP_QUEUE_BYTES
#define NANOSIG_BAREMETAL_LOOP_QUEUE_BYTES 1024u
#endif

/**
 * @brief `ns_loop_poll()` 单次调用的最大派发轮数。
 *
 * 一轮 = 派发到期 timer（入队）→ drain ring 并调用 slot。slot 若重新装载 timer，
 * 会在下一轮被再次派发。本值是该补偿循环的上界，防止无界重入；
 * 耗尽仍有到期项时 `ns_loop_poll()` 仍返回 `NS_OK`，语义由文档说明。
 *
 * 可在编译期覆盖：`-DNANOSIG_BAREMETAL_POLL_MAX_ROUNDS=<n>`。
 */
#ifndef NANOSIG_BAREMETAL_POLL_MAX_ROUNDS
#define NANOSIG_BAREMETAL_POLL_MAX_ROUNDS 4
#endif

/* ================================================================== */
/*  waitable 句柄                                                       */
/* ================================================================== */

/** @brief 可等待句柄。裸机后端无 waitset 能力，此类型仅为满足公共声明。 */
typedef void *ns_waitable_handle_t;

/**
 * @brief 判断 waitable 句柄是否有效（约定：非 NULL 且非全 1 哨兵）。
 */
static inline int ns_waitable_handle_is_valid(ns_waitable_handle_t h)
{
    return (h != NULL) && (h != (void *)~0);
}

/* ================================================================== */
/*  平台原语类型（完整哑类型）                                            */
/* ================================================================== */

/**
 * @section 说明
 * 以下四个类型必须存在，因为 `nanosig_port.h` 以它们作函数参数声明。它们不是
 * 不透明前向声明——C 不允许以不完整类型作函数参数或结构体成员，故必须是
 * **完整类型**。
 *
 * 裸机构建中，持有这些类型的结构体成员会被 `NANOSIG_BAREMETAL` 屏蔽，
 * 因此它们不会产生运行期实例，占用 1 字节在信息上是无代价的。
 */

/** @brief mutex 类型。裸机单执行上下文下不存在 mutex。 */
typedef uint8_t ns_platform_mutex_t;

/** @brief wakeup 类型。裸机下 `ns_loop_poll()` 从不睡眠，故不存在 wakeup。 */
typedef uint8_t ns_platform_wakeup_t;

/** @brief waitset 类型。裸机不提供 waitset 能力。 */
typedef uint8_t ns_platform_waitset_t;

/** @brief 线程类型。裸机不提供线程能力。 */
typedef struct ns_baremetal_thread_stub {
    uint8_t _opaque;
} ns_platform_thread_t;

/* ================================================================== */
/*  ABI 一致性链接探针                                                   */
/* ================================================================== */

/**
 * @brief 跨 TU 平台错配的链接期探针。
 *
 * 符号定义在 `platform/baremetal/port.c`。命名刻意**不**带 `ns_platform_` 前缀，
 * 以便「裸机归档恰好定义三个 `ns_platform_*` 符号」这一断言保持精确。
 */
extern const int ns_nanosig_abi_baremetal;

#define NS_PLATFORM_ABI_PROBE ns_nanosig_abi_baremetal

/* ================================================================== */
/*  时钟注入                                                            */
/* ================================================================== */

/**
 * @brief 应用提供的单调时钟回调。
 *
 * @return 单调不减的微秒计数（64 位，永不回绕）。
 */
typedef uint64_t (*ns_baremetal_now_us_fn)(void);

/**
 * @brief 注册单调时钟回调。
 *
 * @pre 必须在 `ns_init()` **之前**调用；`ns_platform_init()` 在回调为空时
 *      返回 `NS_E_INVAL`，使"忘记装时钟"在初始化期即硬失败，而不是在第一次
 *      timer 操作时表现为诡异的时间跳变。
 *
 * @note 库**不做**任何 tick 换算、也不做回绕扩展：回调契约本身就是
 *       「uint64 微秒、单调不减、永不回绕」，回绕责任完全在应用侧。
 *
 * @param fn 时钟回调；`NULL` 表示注销。
 */
void ns_baremetal_set_now_us(ns_baremetal_now_us_fn fn);

#endif /* NANOSIG_PLATFORM_PORT_H */
