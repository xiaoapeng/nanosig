/**
 * @file nanosig_timer.h
 * @brief nanosig timer API。
 * @date 2026-05-16
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#ifndef NANOSIG_TIMER_H
#define NANOSIG_TIMER_H

#include <stddef.h>
#include <stdint.h>

#include <nanosig/nanosig_loop.h>
#include <nanosig/nanosig_rbtree.h>
#include <nanosig/nanosig_signal.h>
#include <nanosig/nanosig_safety.h>
#include <nanosig/nanosig_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief timer 时间间隔，单位为微秒。
 */
typedef uint64_t ns_time_us_t;

/**
 * @brief timer 行为标志。
 */
enum {
    /** 默认单次 timer，到期触发一次后停止。 */
    NS_TIMER_ATTR_ONESHOT = 0u,
    /** bit0: 自动重复，到期后重新装载。 */
    NS_TIMER_ATTR_REPEAT = 1u << 0,
    /**
     * bit1: 重复装载时以当前时间为基准。
     *
     * 未设置时，下一次 deadline 优先按上一次 deadline + interval_us 步进；
     * 如果该时间已经落后于当前时间，则退化为 current_time + interval_us。
     */
    NS_TIMER_ATTR_RELOAD_FROM_NOW = 1u << 1
};

/**
 * @brief timer 对象。
 *
 * `signal` 必须是第一个字段，便于需要时把 timer 当作其内嵌无 payload signal
 * 使用。Timer 到期时触发 `signal`，payload 固定为 `NS_NO_PAYLOAD`。
 */
typedef struct ns_timer {
    /** 到期时触发的内嵌无 payload signal；必须保持为第一个字段。 */
    ns_signal_t signal;
    /** 触发间隔，单位为微秒。 */
    ns_time_us_t interval_us;
    /** 下一次到期时间，单位为微秒；实现层维护。 */
    ns_time_us_t expire_us;
    /** `NS_TIMER_ATTR_*` 位图。 */
    uint32_t attr;
    /** 实现层 rbtree 节点，调用方不得直接访问。 */
    ns_rbtree_node_t rb_node;
} ns_timer_t;

NS_STATIC_ASSERT(ns_offsetof(ns_timer_t, signal) == 0u, "ns_timer_t.signal must be the first member");

/**
 * @brief 创建 timer。
 *
 * 本函数初始化调用方提供的 `ns_timer_t` 对象。timer 只支持无参数触发；
 * 到期时使用 `NS_NO_PAYLOAD` 触发 `timer->signal`。
 *
 * @attention 本函数不是线程安全的。同一 timer 对象上的并发
 *            `ns_timer_init`/`ns_timer_deinit`/`ns_timer_start`/
 *            `ns_timer_cancel`/`ns_timer_restart` 调用需要调用方
 *            提供外部同步。
 *
 * @param timer 待初始化的 timer 对象。
 * @param interval_us 触发间隔，单位为微秒。
 * @param attr `NS_TIMER_ATTR_*` 位图。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
extern int ns_timer_init(ns_timer_t *timer, ns_time_us_t interval_us, uint32_t attr);

/**
 * @brief 启动 timer。
 *
 * 到期后，timer 触发其内嵌 signal。调用方通过连接 `timer->signal` 选择目标
 * loop 和 slot。
 *
 * @attention 本函数不是线程安全的。同一 timer 对象上的并发调用需要外部同步。
 *
 * @param timer 要启动的 timer 句柄。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
extern int ns_timer_start(ns_timer_t *timer);

/**
 * @brief 取消 timer。
 *
 * 对任何已由 `ns_timer_init` 成功初始化的 timer 都可以调用本函数；如果 timer
 * 当前未运行，则保持停止状态并返回成功。取消不保证已经入队的 slot 调用被撤销。
 *
 * @attention 本函数不是线程安全的。同一 timer 对象上的并发调用需要外部同步。
 *
 * @param timer 要取消的 timer 句柄。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
extern int ns_timer_cancel(ns_timer_t *timer);

/**
 * @brief 重启 timer。
 *
 * 若 timer 未运行，则等价于 `ns_timer_start`；若正在运行，则先移除旧 deadline，
 * 再以当前时间为基准重新装载。
 *
 * @attention 本函数不是线程安全的。同一 timer 对象上的并发调用需要外部同步。
 *            **失败时的状态**：如果 `ns_timer_restart` 失败（例如 runtime 已
 *            shutdown），正在运行的 timer 会被从树中移除但不会重新插入，状态
 *            从"运行中"变为"已停止"。
 *
 * @param timer 要重启的 timer 对象。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
extern int ns_timer_restart(ns_timer_t *timer);

/**
 * @brief 销毁 timer。
 *
 * 销毁前应确保 timer 已不再被业务使用，且所有对同一 timer 的并发操作已结束。
 * 本函数会停止仍在运行的 timer，但不会自动断开 `timer->signal` 上的连接；
 * 健康程序应保存连接句柄并显式 `ns_signal_disconnect`。
 *
 * @attention 如果 `ns_timer_cancel` 在销毁时失败（例如已 shutdown），本函数
 *            会提前返回而不清理 signal 资源。调用者应重试或在适当的生命周期
 *            阶段调用 `ns_timer_deinit`。
 *
 * @param timer 要销毁的 timer 句柄。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
extern int ns_timer_deinit(ns_timer_t *timer);

#ifdef __cplusplus
}
#endif

#endif /* NANOSIG_TIMER_H */
