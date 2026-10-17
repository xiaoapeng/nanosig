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
 * @brief watcher 就绪处理回调类型。
 *
 * broker 线程在 watcher 的 waitable 就绪时调用此回调；每次就绪调用一次，
 * `triggered_events` 为本轮 waitset completion 报告的 `NS_WAITABLE_EVENT_*`
 * 位组合。
 *
 * 本回调是 watcher 唯一的就绪出口：库不再为 watcher 内嵌 signal，也不再 emit
 * 任何 signal。若需要把事件投递回用户 loop，请在回调内自行 emit 用户拥有的
 * `ns_signal_t`（`ns_signal_emit*` 非阻塞，允许在此调用）。
 *
 * @warning 本回调在 broker 线程执行，**必须非阻塞**。阻塞会卡住所有 watcher
 *          与 timer 投递。
 * @warning 本回调**不得调用任何会等待 broker 线程的 nanosig API**，例如
 *          `ns_broker_add`、`ns_broker_remove`、`ns_shutdown` 以及会触发 broker
 *          teardown 的调用——这些会自死锁。`ns_signal_emit*` 属于非阻塞操作，
 *          **允许**调用。
 * @warning 电平触发的 watcher 必须在回调内把事件源读净，否则每轮 waitset_wait
 *          都会重复触发。
 *
 * @param watcher 触发的 watcher。
 * @param triggered_events 本轮报告的 `NS_WAITABLE_EVENT_*` 组合。
 */
typedef struct ns_watcher ns_watcher_t;
typedef void (*ns_watcher_handler_fn)(ns_watcher_t *watcher, uint32_t triggered_events);

/**
 * @brief watcher 对象。
 *
 * 调用方拥有本结构体的存储。waitable 就绪时 broker 线程调用 `handler`。
 *
 * @pre 本结构体必须由调用方零初始化（如 `ns_watcher_t w = {0};`）或仅经
 *      `ns_watcher_init` 产出；把非零初始化的 garbage watcher 传入
 *      `ns_watcher_deinit` 属未定义输入。
 */
struct ns_watcher {
    /** 可注册到 broker waitset 的平台 waitable。 */
    ns_platform_waitable_t waitable;
    /** broker 内部链表节点，调用方不得直接修改。 */
    ns_list_node_t broker_node;
    /** 就绪回调，必填；同时作为 watcher 的生命周期 marker。 */
    ns_watcher_handler_fn handler;
};

/**
 * @brief 初始化 watcher。
 *
 * 本函数初始化调用方提供的 `ns_watcher_t`：填充 `waitable` 的平台句柄 /
 * `events` / `edge_triggered`，初始化 broker 链表节点，并保存 `handler`。
 *
 * @pre `watcher` 必须已零初始化，或来自一次成功 `ns_watcher_deinit` 的 watcher。
 * @warning 本函数不幂等，且**不检测重复初始化**：对已初始化的 watcher 再次调用
 *          属调用方错误（未定义行为，会覆盖已有状态），与 `ns_signal_init_raw`
 *          统一策略。调用方必须保证 init 一次 → deinit 一次。
 * @warning 本函数必须串行化调用，非 MPM-safe。
 *
 * @param watcher 待初始化的 watcher。
 * @param handle 平台句柄（Linux/macOS 填 `.fd`，Windows/FreeRTOS/Zephyr 填 `.handle`）。
 * @param events `NS_WAITABLE_EVENT_*` 位组合。
 * @param edge_triggered 非零表示边沿触发，零表示电平触发；Linux 映射 `EPOLLET`、
 *        macOS 映射 `EV_CLEAR`，Windows/Zephyr/FreeRTOS 后端忽略该参数。
 * @param handler 就绪回调，必填；为 NULL 时返回 `NS_E_INVAL`。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
extern int ns_watcher_init(ns_watcher_t *watcher, ns_waitable_handle_t handle,
                           uint32_t events, int edge_triggered,
                           ns_watcher_handler_fn handler);

/**
 * @brief 释放 watcher 内部资源。
 *
 * 调用前应先通过 `ns_broker_remove()` 从 broker 注销 watcher。成功时本函数把
 * `waitable`、broker 链表节点与 `handler` 完整重置为未注册状态。
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
 * 注册后，broker 线程等待 watcher 的 waitable；就绪时调用 `watcher->handler`。
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
 * 注销不会撤销已经开始的 dispatch；调用方仍需保证相关 `user_data`
 * 生命周期覆盖任何 in-flight 调用。
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
 * 供 `handler` 内部使用，从 watcher 中取出平台句柄进行 I/O 操作。
 * 返回值为 `ns_waitable_handle_t` 联合体，调用方根据当前平台访问对应成员。
 *
 * @param watcher 已初始化的 watcher。
 * @return 平台句柄联合体；watcher 为 NULL 时返回全 1 无效哨兵。
 */
extern ns_waitable_handle_t ns_watcher_handle(const ns_watcher_t *watcher);

#ifdef __cplusplus
}
#endif

#endif /* NANOSIG_BROKER_H */
