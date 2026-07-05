/**
 * @file nanosig_broker.h
 * @brief nanosig event broker 和 watcher API。
 * @date 2026-06-14
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#ifndef NANOSIG_BROKER_H
#define NANOSIG_BROKER_H

#include <stdint.h>

#include <nanosig/nanosig_list.h>
#include <nanosig/nanosig_signal.h>
#include <nanosig/nanosig_status.h>
#include <nanosig/nanosig_types.h>
#include <nanosig/nanosig_port.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief event broker 全局单例类型。
 *
 * broker 由 `ns_init()` 创建，由 `ns_shutdown()` 销毁。调用方不需持有 broker
 * 指针；`ns_broker_add()` 和 `ns_broker_remove()` 自动访问全局 broker。
 */
typedef struct ns_event_broker ns_event_broker_t;

/**
 * @brief watcher 事件 payload。
 *
 * watcher signal 的 slot 收到此 payload 后，可读取 `triggered_events` 判断
 * 本次触发的事件位。若 watcher 设置了 `consume_fn`，`consume_handle` 为
 * `consume_fn` 内通过 `ns_watcher_set_consume_handle` 填充的用户数据。
 */
typedef struct ns_watcher_event {
    uint32_t triggered_events; /**< `NS_WAITABLE_EVENT_*` 组合 */
    void    *consume_handle;   /**< consume_fn 填充的结果/句柄，无 consume_fn 时为 NULL */
} ns_watcher_event_t;

/**
 * @brief watcher 消费函数类型。
 *
 * broker 线程在 emit 前调用此函数消费 fd 数据。返回值语义：
 * - `> 0`：成功消费，broker 继续 emit signal。
 * - `0`：无数据可消费，broker 跳过 emit。
 * - `< 0`：错误，broker 跳过 emit。
 *
 * @warning 此函数在 broker 线程执行，必须非阻塞。阻塞会卡住所有 watcher。
 * @warning 此函数只应做消费动作（如 `read(fd, buf, ...)`），**不得调用
 *          任何 nanosig API**（如 `ns_signal_emit_raw`、`ns_broker_add/remove`
 *          、`ns_loop_quit` 等），否则会破坏 broker 内部循环一致性。
 *
 * @param watcher 触发的 watcher，可从中取 fd 和设置 consume_handle。
 * @return `> 0` 继续 emit，`0` 或负数跳过 emit。
 */
typedef struct ns_watcher ns_watcher_t;
typedef int (*ns_watcher_consume_fn)(ns_watcher_t *watcher);

/**
 * @brief watcher 对象。
 *
 * 调用方拥有本结构体的存储。`signal` 必须保持为第一个字段，事件触发时 broker
 * 会 emit 这个内嵌 signal，payload 类型为 `ns_watcher_event_t`。
 */
struct ns_watcher {
    /** 事件触发时 emit 的内嵌 signal；必须保持为第一个字段。 */
    ns_signal_t signal;
    /** 可注册到 broker waitset 的平台 waitable。 */
    ns_platform_waitable_t waitable;
    /** broker 内部链表节点，调用方不得直接修改。 */
    ns_list_node_t broker_node;
    /** 可选消费函数；为 NULL 时 broker 跳过 fd 消费，直接 emit signal。 */
    ns_watcher_consume_fn consume_fn;
    /** consume_fn 内通过 ns_watcher_set_consume_handle 设置的句柄，emit 时读取。 */
    void *pending_consume_handle;
};

NS_STATIC_ASSERT(ns_offsetof(ns_watcher_t, signal) == 0u, "ns_watcher_t.signal must be the first member");

/**
 * @brief 初始化 watcher。
 *
 * 本函数初始化调用方提供的 `ns_watcher_t`。参数有效时，它会初始化内嵌 signal、
 * 填充 `waitable` 的平台句柄 / `events` / `edge_triggered`，并初始化 broker
 * 链表节点。`consume_fn` 可选；为 NULL 时 broker 跳过 fd 消费，直接 emit signal。
 *
 * @warning 本函数不幂等；重复 init 会导致旧 mutex 泄漏。调用方必须保证
 *          init 一次 → deinit 一次。
 * @warning 本函数必须串行化调用，非 MPM-safe。
 *
 * @param watcher 待初始化的 watcher。
 * @param handle 平台句柄（Linux/macOS 填 `.fd`，Windows 填 `.handle`）。
 * @param events `NS_WAITABLE_EVENT_*` 位组合。
 * @param edge_triggered 非零表示边沿触发，零表示电平触发。
 * @param consume_fn 可选消费函数；为 NULL 时 broker 跳过 fd 消费，直接 emit signal。
 *        非 NULL 时 broker 线程在 emit 前调用，
 *        返回 `> 0` 时 emit，`0` 或负数跳过 emit。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
extern int ns_watcher_init(ns_watcher_t *watcher, ns_waitable_handle_t handle,
                           uint32_t events, int edge_triggered,
                           ns_watcher_consume_fn consume_fn);

/**
 * @brief 释放 watcher 内部资源。
 *
 * 调用前应先通过 `ns_broker_remove()` 从 broker 注销 watcher。本函数释放内嵌
 * signal 的内部资源，并把 waitable 和 broker 链表节点重置为未注册状态。
 *
 * @warning 本函数必须串行化调用，非 MPM-safe。
 *
 * @param watcher 待释放的 watcher。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
extern int ns_watcher_deinit(ns_watcher_t *watcher);

/**
 * @brief 将 watcher 注册到 broker。
 *
 * 注册后，broker 线程等待 watcher 的 waitable；事件到达时 emit
 * `watcher->signal`，payload 为 `ns_watcher_event_t`。
 *
 * 必须在 `ns_init()` 之后、`ns_shutdown()` 之前调用。
 *
 * @warning 不得与 `ns_shutdown()` 并发调用。
 *
 * @param watcher 已初始化的 watcher。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
extern int ns_broker_add(ns_watcher_t *watcher);

/**
 * @brief 从 broker 注销 watcher。
 *
 * 注销不会撤销已经入队的 slot 调用；调用方仍需保证相关 `user_data`
 * 生命周期覆盖任何 in-flight emit。
 *
 * @warning 不得与 `ns_shutdown()` 并发调用。
 *
 * @param watcher 已注册的 watcher。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
extern int ns_broker_remove(ns_watcher_t *watcher);

/**
 * @brief 获取 watcher 的平台句柄。
 *
 * 供 `consume_fn` 内部使用，从 watcher 中取出平台句柄进行 I/O 操作。
 * 返回值为 `ns_waitable_handle_t` 联合体，调用方根据当前平台访问对应成员。
 *
 * @param watcher 已初始化的 watcher。
 * @return 平台句柄联合体；watcher 为 NULL 时返回全 1 无效哨兵。
 */
extern ns_waitable_handle_t ns_watcher_handle(const ns_watcher_t *watcher);

/**
 * @brief 设置 consume_handle。
 *
 * `pending_consume_handle` 是 watcher 的持久句柄，可在一早（如初始化后）设置，
 * 跨多次 dispatch 有效，生命周期由调用方保证。`consume_fn` 内也可更新。
 *
 * 每次 emit 时，`pending_consume_handle` 的当前值会填入
 * `ns_watcher_event_t.consume_handle` 传递给 slot。若不设值则为 NULL。
 *
 * @note 不设值（保持 NULL）或设值为 NULL 的语义相同：slot 收到 NULL consume_handle。
 *
 * @warning 本函数非线程安全。通常仅在 `consume_fn` 内或初始化阶段串行调用。
 *
 * @param watcher 触发的 watcher。
 * @param handle 用户数据句柄，可为 NULL。
 */
extern void ns_watcher_set_consume_handle(ns_watcher_t *watcher, void *handle);

#ifdef __cplusplus
}
#endif

#endif /* NANOSIG_BROKER_H */
