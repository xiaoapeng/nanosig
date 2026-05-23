/**
 * @file nanosig_signal.h
 * @brief nanosig signal/slot 类型、宏和连接 API。
 * @date 2026-05-16
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#ifndef NANOSIG_SIGNAL_H
#define NANOSIG_SIGNAL_H

#include <stddef.h>

#include <nanosig/nanosig_loop.h>
#include <nanosig/nanosig_safety.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 可静态定义的 signal 对象。
 *
 * `payload_size` 固定每次 emit 复制的字节数；0 表示该 signal 无 payload。
 * `impl` 为实现层私有状态，调用方不得直接读写。
 */
typedef struct ns_signal {
    /** 每次 emit 复制的 payload 字节数；无 payload signal 为 0。 */
    size_t payload_size;
    /** slot 容量提示；0 表示使用实现默认值。 */
    size_t slot_capacity;
    /** 调试名称；库只保存指针，不接管字符串所有权。 */
    const char *debug_name;
    /** 实现层私有指针，调用方不得直接访问。 */
    void *impl;
} ns_signal_t;

/**
 * @brief signal 与 slot 之间连接关系的不透明句柄。
 */
typedef struct ns_connection ns_connection_t;

/**
 * @brief 无 payload signal 使用的公开标记类型。
 *
 * `ns_no_payload_t` 只作为 payload 类型标记使用。把它传给
 * `NS_SIGNAL_DEFINE` / `NS_SIGNAL_INITIALIZER` / `ns_signal_init` 时，宏会把
 * `payload_size` 设为 0，而不是 `sizeof(ns_no_payload_t)`。
 *
 * C11 没有可移植的零大小完整结构体；该类型保持完整，只是为了让
 * `const ns_no_payload_t *` 成为合法 slot 签名。调用方不得创建该类型的
 * 实例，也不得把该类型对象作为 payload 传入。无 payload emit 必须使用
 * `NS_NO_PAYLOAD`，实现层必须按 0 字节 payload 处理，不发生 payload 拷贝。
 */
typedef struct ns_no_payload {
    unsigned char ns_marker;
} ns_no_payload_t;

/**
 * @brief `ns_signal_emit` 触发无 payload signal 时使用的 payload 指针常量。
 */
#define NS_NO_PAYLOAD ((const ns_no_payload_t *)NULL)

/**
 * @brief 通用 slot 函数签名。
 *
 * @param user_data 连接时传入的调用方数据，nanosig 不接管其所有权。
 * @param payload 指向 signal 固定大小 payload 的只读副本；无 payload signal
 *        调用时为 `NULL`。
 */
typedef void (*ns_slot_fn)(void *user_data, const void *payload);

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
/**
 * @brief 计算 payload 类型对应的入队字节数。
 *
 * 普通 payload 类型返回 `sizeof(payload_type)`；`ns_no_payload_t` 返回 0。
 *
 * @param payload_type signal 的 payload 类型。
 * @return payload 入队字节数。
 */
#define NS_SIGNAL_PAYLOAD_SIZE(payload_type) \
    ((size_t)_Generic(((payload_type *)NULL), ns_no_payload_t *: 0u, default: sizeof(payload_type)))

/**
 * @brief 计算 payload 指针对应的入队字节数。
 *
 * 普通 payload 指针返回 `sizeof(*payload_ptr)`；`NS_NO_PAYLOAD` 返回 0。
 *
 * @param payload_ptr 指向 payload 的指针，或 `NS_NO_PAYLOAD`。
 * @return payload 入队字节数。
 */
#define NS_SIGNAL_PAYLOAD_PTR_SIZE(payload_ptr) \
    ((size_t)_Generic((payload_ptr), \
        ns_no_payload_t *: 0u, \
        const ns_no_payload_t *: 0u, \
        default: sizeof(*(payload_ptr))))

_Static_assert(NS_SIGNAL_PAYLOAD_SIZE(ns_no_payload_t) == 0u, "ns_no_payload_t payload size must be 0");
_Static_assert(NS_SIGNAL_PAYLOAD_PTR_SIZE(NS_NO_PAYLOAD) == 0u, "NS_NO_PAYLOAD payload size must be 0");
#else
#define NS_SIGNAL_PAYLOAD_SIZE(payload_type) ((size_t)sizeof(payload_type))
#define NS_SIGNAL_PAYLOAD_PTR_SIZE(payload_ptr) ((size_t)sizeof(*(payload_ptr)))
#endif

/**
 * @brief 生成静态 signal 初始化器。
 *
 * @param payload_type signal 的 payload 类型；无 payload signal 使用
 *        `ns_no_payload_t`。
 * @return 可用于 `ns_signal_t` 对象初始化的聚合初始化器。
 *
 * 该宏也用于结构体成员初始化，例如：
 * `struct app { ns_signal_t changed; };`
 * `struct app app = { .changed = NS_SIGNAL_INITIALIZER(app_payload_t) };`
 */
#define NS_SIGNAL_INITIALIZER(payload_type) \
    { \
        .payload_size = NS_SIGNAL_PAYLOAD_SIZE(payload_type), \
        .slot_capacity = 0u, \
        .debug_name = #payload_type, \
        .impl = NULL \
    }

/**
 * @brief 在头文件中声明一个外部 signal 对象。
 *
 * @param name signal 对象名。
 */
#define NS_SIGNAL_DECLARE(name) \
    extern ns_signal_t name

/**
 * @brief 定义一个 signal 对象。
 *
 * @param name signal 对象名。
 * @param payload_type signal 的 payload 类型；无 payload signal 使用
 *        `ns_no_payload_t`。
 */
#define NS_SIGNAL_DEFINE(name, payload_type) \
    ns_signal_t name = NS_SIGNAL_INITIALIZER(payload_type)

/**
 * @brief 声明一个带 payload 类型的 slot 函数指针类型。
 *
 * @param name 要生成的函数指针类型名。
 * @param payload_type slot 接收的 payload 类型；无 payload signal 使用
 *        `ns_no_payload_t`。
 */
#define NS_DEFINE_SLOT(name, payload_type) \
    typedef void (*name)(void *user_data, const payload_type *payload)

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
/**
 * @brief 在支持 `_Generic` 的 C11 编译器上检查 slot 函数签名。
 *
 * @param slot_fn 待检查的 slot 函数。
 * @param payload_type 期望的 payload 类型。
 */
#define NS_SLOT_TYPECHECK(slot_fn, payload_type) \
    ((void)sizeof(char[(_Generic((slot_fn), void (*)(void *, const payload_type *): 1, default: 0)) ? 1 : -1]))
#else
#define NS_SLOT_TYPECHECK(slot_fn, payload_type) ((void)0)
#endif

/**
 * @brief 连接 signal 与带类型检查的 slot，默认使用当前线程 loop。
 *
 * 无 payload signal 使用 `payload_type = ns_no_payload_t`，slot 签名为
 * `void (*)(void *, const ns_no_payload_t *)`。
 *
 * @param signal_name signal 对象名。
 * @param slot_fn slot 函数。
 * @param payload_type signal 的 payload 类型。
 * @param user_data 传给 slot 的调用方数据，nanosig 不接管其所有权。
 * @param out_connection 输出连接句柄。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
#define ns_signal_connect_typed(signal_name, slot_fn, payload_type, user_data, out_connection) \
    (NS_SLOT_TYPECHECK(slot_fn, payload_type), \
     ns_signal_connect(&(signal_name), (ns_slot_fn)(slot_fn), NULL, (user_data), (out_connection)))

/**
 * @brief 连接 signal 与带类型检查的 slot，并显式指定目标 loop。
 *
 * @param signal_name signal 对象名。
 * @param slot_fn slot 函数。
 * @param payload_type signal 的 payload 类型；无 payload signal 使用
 *        `ns_no_payload_t`。
 * @param target_loop slot 回调所属的目标 loop。
 * @param user_data 传给 slot 的调用方数据，nanosig 不接管其所有权。
 * @param out_connection 输出连接句柄。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
#define ns_signal_connect_typed_to(signal_name, slot_fn, payload_type, target_loop, user_data, out_connection) \
    (NS_SLOT_TYPECHECK(slot_fn, payload_type), \
     ns_signal_connect(&(signal_name), (ns_slot_fn)(slot_fn), (target_loop), (user_data), (out_connection)))

/**
 * @brief 触发 signal。
 *
 * 普通 payload signal 传入 payload 指针；无 payload signal 传入
 * `NS_NO_PAYLOAD`。
 *
 * @param signal_name signal 对象名。
 * @param payload_ptr 指向 payload 的指针，或 `NS_NO_PAYLOAD`。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
#define ns_signal_emit(signal_name, payload_ptr) \
    ns_signal_emit_raw(&(signal_name), (const void *)(payload_ptr), NS_SIGNAL_PAYLOAD_PTR_SIZE(payload_ptr))

/**
 * @brief 动态初始化 signal。
 *
 * 适用于无法使用 `NS_SIGNAL_DEFINE` / `NS_SIGNAL_INITIALIZER` 的对象生命周期。
 * 该宏直接接收 `payload_type`，并在编译期烘焙 payload 字节数。本接口只写入
 * signal 元数据，不拥有 slot 连接资源，也不需要成对的 `ns_signal_deinit`。
 * 调用方销毁承载该 signal 的对象前，应确保相关连接已通过
 * `ns_signal_disconnect` 或 `ns_signal_disconnect_all` 断开。
 *
 * @param signal 待初始化的 signal 对象指针。
 * @param payload_type signal 的 payload 类型；无 payload signal 使用
 *        `ns_no_payload_t`。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
#define ns_signal_init(signal, payload_type) \
    ns_signal_init_raw((signal), NS_SIGNAL_PAYLOAD_SIZE(payload_type), 0u, #payload_type)

/**
 * @brief 以原始 payload 大小动态初始化 signal。
 *
 * 该函数是 `ns_signal_init` 宏的底层入口，供实现层、绑定层或无法使用 C 宏的
 * 调用方使用。普通 C 调用方应优先使用 `ns_signal_init(signal, payload_type)`。
 *
 * @param signal 待初始化的 signal 对象。
 * @param payload_size 每次 emit 复制的 payload 字节数；无 payload signal 为 0。
 * @param slot_capacity slot 容量提示；0 表示使用实现默认值。
 * @param debug_name 调试名称；库只保存指针，不接管字符串所有权。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
int ns_signal_init_raw(ns_signal_t *signal, size_t payload_size, size_t slot_capacity, const char *debug_name);

/**
 * @brief 连接 signal、slot 和目标 loop。
 *
 * `target_loop` 为 `NULL` 时使用当前线程绑定的 loop；当前线程没有 loop 时返回
 * `NS_E_NO_LOOP`。非 `NULL` 时使用调用方显式提供的目标 loop。
 *
 * @param signal 要连接的 signal。
 * @param slot_fn slot 函数。
 * @param target_loop 目标 loop；为 `NULL` 时使用当前线程 loop。
 * @param user_data 传给 slot 的调用方数据。
 * @param out_connection 输出连接句柄。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
int ns_signal_connect(
    ns_signal_t *signal,
    ns_slot_fn slot_fn,
    ns_loop_t *target_loop,
    void *user_data,
    ns_connection_t **out_connection);

/**
 * @brief 断开连接。
 *
 * 断开连接不会取消已经入队的 slot 调用；调用方仍需保证 `user_data`
 * 的在途生命周期。
 *
 * @param connection 要断开的连接句柄。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
int ns_signal_disconnect(ns_connection_t *connection);

/**
 * @brief 断开某个 signal 上的所有连接。
 *
 * 这是 teardown / 兜底接口。健康程序通常应保存每个连接句柄并显式调用
 * `ns_signal_disconnect`，使生命周期关系清晰。批量断开不会取消已经入队的
 * slot 调用；调用方仍需保证所有相关 `user_data` 长于任何 in-flight emit。
 *
 * @param signal 要断开所有连接的 signal。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
int ns_signal_disconnect_all(ns_signal_t *signal);

/**
 * @brief 以原始 payload 指针触发 signal。
 *
 * `payload_size` 必须与 signal 的固定 `payload_size` 一致。无 payload
 * signal 使用 `payload = NULL` 且 `payload_size = 0`。emit 路径不允许
 * 分配内存。
 *
 * @param signal 要触发的 signal。
 * @param payload 指向只读 payload 数据；无 payload 时为 `NULL`。
 * @param payload_size payload 字节数。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
int ns_signal_emit_raw(ns_signal_t *signal, const void *payload, size_t payload_size);

#ifdef __cplusplus
}
#endif

#endif /* NANOSIG_SIGNAL_H */
