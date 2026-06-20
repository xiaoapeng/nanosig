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
#include <nanosig/nanosig_waitable.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief event broker 全局单例类型。
 *
 * broker 由 `ns_init()` 创建，由 `ns_shutdown()` 销毁。调用方通过
 * `ns_broker()` 获取指针，不直接分配或释放本类型。
 */
typedef struct ns_event_broker ns_event_broker_t;

/**
 * @brief watcher 事件 payload。
 *
 * watcher signal 的 slot 收到此 payload 后，可读取 `triggered_events` 判断
 * 本次触发的事件位。
 */
typedef struct ns_watcher_event {
    uint32_t triggered_events; /**< `NS_WAITABLE_EVENT_*` 组合 */
} ns_watcher_event_t;

/**
 * @brief watcher 对象。
 *
 * 调用方拥有本结构体的存储。`signal` 必须保持为第一个字段，事件触发时 broker
 * 会 emit 这个内嵌 signal，payload 类型为 `ns_watcher_event_t`。
 */
typedef struct ns_watcher {
    /** 事件触发时 emit 的内嵌 signal；必须保持为第一个字段。 */
    ns_signal_t signal;
    /** 可注册到 broker waitset 的平台 waitable。 */
    ns_platform_waitable_t waitable;
    /** broker 内部链表节点，调用方不得直接修改。 */
    ns_list_node_t broker_node;
} ns_watcher_t;

NS_STATIC_ASSERT(ns_offsetof(ns_watcher_t, signal) == 0u, "ns_watcher_t.signal must be the first member");

/**
 * @brief 初始化 fd watcher。
 *
 * 本函数初始化调用方提供的 `ns_watcher_t`。参数有效时，它会初始化内嵌 signal、
 * 填充 `waitable.fd` / `events` / `edge_triggered`，并初始化 broker 链表节点。
 * 调用方负责在当前平台传入 waitset 后端可等待的 fd。
 *
 * @warning 本函数不幂等；重复 init 会导致旧 mutex 泄漏。调用方必须保证
 *          init 一次 → deinit 一次。
 * @warning 本函数必须串行化调用，非 MPM-safe。
 *
 * @param watcher 待初始化的 watcher。
 * @param fd 平台 fd。
 * @param events `NS_WAITABLE_EVENT_*` 位组合。
 * @param edge_triggered 非零表示边沿触发，零表示电平触发。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
extern int ns_watcher_init_fd(ns_watcher_t *watcher, int fd, uint32_t events, int edge_triggered);

/**
 * @brief 初始化 handle watcher。
 *
 * 本函数初始化调用方提供的 `ns_watcher_t`。参数有效时，它会初始化内嵌 signal、
 * 填充 `waitable.handle` / `events` / `edge_triggered`，并初始化 broker 链表节点。
 * 调用方负责在当前平台传入 waitset 后端可等待的 handle。
 *
 * @warning 本函数不幂等；重复 init 会导致旧 mutex 泄漏。调用方必须保证
 *          init 一次 → deinit 一次。
 * @warning 本函数必须串行化调用，非 MPM-safe。
 *
 * @param watcher 待初始化的 watcher。
 * @param handle 平台 handle。
 * @param events `NS_WAITABLE_EVENT_*` 位组合。
 * @param edge_triggered 非零表示边沿触发，零表示电平触发。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
extern int ns_watcher_init_handle(ns_watcher_t *watcher, void *handle, uint32_t events, int edge_triggered);

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
 * @brief 获取全局 event broker。
 *
 * `ns_init()` 前和 `ns_shutdown()` 后返回 `NULL`。
 *
 * @return 全局 broker 指针，或 `NULL`。返回值在 `ns_init()` 与 `ns_shutdown()`
 * 之间恒定不变，可在任意线程安全读取。
 */
extern ns_event_broker_t *ns_broker(void);

/**
 * @brief 将 watcher 注册到 broker。
 *
 * 注册后，broker 线程等待 watcher 的 waitable；事件到达时 emit
 * `watcher->signal`，payload 为 `ns_watcher_event_t`。
 *
 * @param broker broker 指针，通常来自 `ns_broker()`。
 * @param watcher 已初始化的 watcher。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
extern int ns_broker_add(ns_event_broker_t *broker, ns_watcher_t *watcher);

/**
 * @brief 从 broker 注销 watcher。
 *
 * 注销不会撤销已经入队的 slot 调用；调用方仍需保证相关 `user_data`
 * 生命周期覆盖任何 in-flight emit。
 *
 * @param broker broker 指针，通常来自 `ns_broker()`。
 * @param watcher 已注册的 watcher。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
extern int ns_broker_remove(ns_event_broker_t *broker, ns_watcher_t *watcher);

#ifdef __cplusplus
}
#endif

#endif /* NANOSIG_BROKER_H */
