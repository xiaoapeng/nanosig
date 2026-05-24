/**
 * @file nanosig_loop.h
 * @brief nanosig 线程绑定事件循环 API。
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

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 线程绑定事件循环的不透明句柄。
 *
 * 每个线程最多创建并绑定一个 `ns_loop_t`。名称保留为 loop，因为该对象
 * 仍然表示事件循环；线程绑定是它的生命周期约束，而不是新的 public 类型。
 */
typedef struct ns_loop ns_loop_t;

/**
 * @brief 事件循环创建配置。
 *
 * 调用方通常从 `NS_LOOP_CONFIG_DEFAULT()` 开始，再覆盖需要调整的字段。
 */
typedef struct ns_loop_config {
    /** MPSC 入队容量，决定跨线程 emit 可排队的最大任务数。 */
    size_t queue_capacity;
    /** 单个 payload 可复制进入队列的最大字节数。 */
    size_t max_payload_size;
    /** 分发 signal 时可快照的 slot 数量上限。 */
    size_t slot_snapshot_capacity;
    /** 预留标志位，当前 PD 阶段必须为 0。 */
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
        .queue_capacity = 1024u, \
        .max_payload_size = 256u, \
        .slot_snapshot_capacity = 32u, \
        .flags = 0u, \
        .debug_name = NULL \
    })

/**
 * @brief 在当前线程创建并绑定事件循环。
 *
 * `config` 为 `NULL` 时使用默认配置。成功后，当前线程成为该 loop 的拥有者。
 * 如果当前线程已经绑定 loop，本函数返回 `NS_E_EXISTS`。
 *
 * @param out_loop 输出创建得到的 loop 句柄。
 * @param config loop 配置；可为 `NULL`。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
extern int ns_loop_create(ns_loop_t **out_loop, const ns_loop_config_t *config);

/**
 * @brief 销毁事件循环并解除当前线程绑定。
 *
 * 调用方应确保该 loop 不再运行，并且没有新的 signal emit 继续以它为目标。
 * 本函数必须由该 loop 的拥有者线程调用。
 *
 * @param loop 要销毁的 loop 句柄。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
extern int ns_loop_destroy(ns_loop_t *loop);

/**
 * @brief 运行事件循环。
 *
 * 本函数持续等待并分发队列中的 slot 调用，直到收到 `ns_loop_quit()` 请求。
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
 * @brief 获取当前线程绑定的事件循环。
 *
 * 如果当前线程尚未创建 loop，本函数返回 `NS_E_NO_LOOP`。
 *
 * @param out_loop 输出当前线程绑定的 loop 句柄。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
extern int ns_loop_current(ns_loop_t **out_loop);

/**
 * @brief 检查调用线程是否为指定 loop 的拥有者线程。
 *
 * @param loop 要检查的 loop 句柄。
 * @param out_is_owner 输出参数；非零表示调用线程是拥有者。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
extern int ns_loop_is_owner(const ns_loop_t *loop, int *out_is_owner);

#ifdef __cplusplus
}
#endif

#endif /* NANOSIG_LOOP_H */
