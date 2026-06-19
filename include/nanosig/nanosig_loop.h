/**
 * @file nanosig_loop.h
 * @brief nanosig 事件循环 API。
 * @date 2026-05-16
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#ifndef NANOSIG_LOOP_H
#define NANOSIG_LOOP_H

#include <stddef.h>
#include <stdint.h>

#include <nanosig/nanosig_safety.h>
#include <nanosig/nanosig_status.h>
#include <nanosig/nanosig_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 事件循环不透明句柄。
 *
 * `ns_loop_t` 表示一个事件循环实例。调用方负责管理其生命周期，库不绑定线程。
 */
typedef struct ns_loop ns_loop_t;

/**
 * @brief 事件循环创建配置。
 *
 * 调用方通常从 `NS_LOOP_CONFIG_DEFAULT()` 开始，再覆盖需要调整的字段。
 */
typedef struct ns_loop_config {
    /** MPSC record ring 字节容量，决定跨线程 emit 可排队的总字节数。须为 2 的幂。 */
    ns_capacity_t queue_byte_capacity;
    /** 预留标志位，当前必须为 0。 */
    uint32_t flags;
    /** 调试名称；库只保存指针，不接管字符串所有权。 */
    const char *debug_name;
} ns_loop_config_t;

/**
 * @brief 返回默认 loop 配置。
 *
 * @return `ns_loop_config_t` 复合字面量，适合用作局部变量初始化值。
 */
#define NS_LOOP_CONFIG_DEFAULT() \
    ((ns_loop_config_t){ \
        .queue_byte_capacity = NS_CAPACITY_1024, \
        .flags = 0u, \
        .debug_name = NULL \
    })

/**
 * @brief 创建事件循环。
 *
 * `config` 为 `NULL` 时使用默认配置。loop 不绑定线程，调用方负责管理其生命周期。
 *
 * @pre 本函数不得与 `ns_loop_destroy()` 或自身并发调用。
 *
 * @param out_loop 输出创建得到的 loop 句柄；可为 `NULL`，此时不向外传值。
 * @param config loop 配置；可为 `NULL`。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
extern int ns_loop_create(ns_loop_t **out_loop, const ns_loop_config_t *config);

/**
 * @brief 销毁事件循环。
 *
 * @pre 调用前必须确保该 loop 不再运行（`ns_loop_run()` 已返回）。
 * @pre 调用前必须断开所有以该 loop 为目标的 signal connection，并确保没有
 *      in-flight 的 `ns_signal_emit_raw()` 仍持有指向该 loop 的指针。
 * @pre `ns_shutdown()` 尚未调用；shutdown 后调用本函数行为未定义。
 * @pre 本函数不得与 `ns_loop_create()` 或自身并发调用。
 *
 * @param loop 要销毁的 loop 句柄。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
extern int ns_loop_destroy(ns_loop_t *loop);

/**
 * @brief 运行事件循环。
 *
 * 本函数持续等待并分发队列中的 slot 调用，直到收到 `ns_loop_quit()` 请求。
 * 同一个 loop 不允许被两个线程同时 run（返回 `NS_E_EXISTS`）。
 * 已通过 `ns_loop_start()` 启动后台线程的 loop 不可再调 `ns_loop_run()`，
 * 会返回 `NS_E_BUSY`。
 *
 * @param loop 要运行的 loop 句柄。
 * @return `NS_OK` 表示正常退出，失败时返回负数状态码。
 */
extern int ns_loop_run(ns_loop_t *loop);

/**
 * @brief 请求事件循环退出。
 *
 * 可由拥有该 loop 的线程调用，也可由跨线程控制路径调用。退出请求会唤醒
 * 正在等待的 loop。
 *
 * @param loop 目标 loop 句柄。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
extern int ns_loop_quit(ns_loop_t *loop);

/**
 * @brief 在后台线程中启动事件循环。
 *
 * 创建一个新的平台线程，在新线程中调用 `ns_loop_run()`。调用方可通过
 * `ns_loop_stop()` 终止循环并等待线程退出。
 *
 * 同一 loop 不可重复调用 `ns_loop_start()`（返回 `NS_E_BUSY`）。
 * 已 start 的 loop 不可再调 `ns_loop_run()`（返回 `NS_E_BUSY`）。
 *
 * @param loop 目标 loop 句柄。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
extern int ns_loop_start(ns_loop_t *loop);

/**
 * @brief 停止后台事件循环并等待线程退出。
 *
 * 内部调用 `ns_loop_quit()` 请求退出，然后 join 后台线程。join 完成后
 * 释放线程句柄，loop 状态恢复为可再次 `start`。
 *
 * @param loop 目标 loop 句柄。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
extern int ns_loop_stop(ns_loop_t *loop);

#ifdef __cplusplus
}
#endif

#endif /* NANOSIG_LOOP_H */
