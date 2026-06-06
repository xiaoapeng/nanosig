/**
 * @file port.h
 * @brief nanosig 内部平台抽象层接口。
 * @date 2026-05-16
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#ifndef NANOSIG_PLATFORM_PORT_H
#define NANOSIG_PLATFORM_PORT_H

#include <stddef.h>
#include <stdint.h>

#include <nanosig/nanosig.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 无限等待的超时值，单位为微秒。
 */
#define NS_PLATFORM_WAIT_INFINITE_US UINT64_MAX

/**
 * @brief 平台单调时间，单位为微秒。
 */
typedef uint64_t ns_platform_time_us_t;

/**
 * @brief 平台 TLS key 句柄。
 */
typedef struct ns_platform_tls_key ns_platform_tls_key_t;

/**
 * @brief 平台 wakeup 句柄。
 *
 * wakeup 是 loop 的等待/唤醒原语。创建和销毁可以分配资源；
 * `ns_platform_wakeup_signal` 不允许分配内存。
 */
typedef struct ns_platform_wakeup ns_platform_wakeup_t;

/**
 * @brief 平台互斥锁句柄。
 */
typedef struct ns_platform_mutex ns_platform_mutex_t;

/**
 * @brief 平台等待结果。
 */
typedef enum ns_platform_wait_result {
    NS_PLATFORM_WAIT_SIGNALED = 0,
    NS_PLATFORM_WAIT_TIMEOUT = 1
} ns_platform_wait_result_t;

/**
 * @brief 初始化平台层全局状态。
 *
 * `ns_init` 调用期间执行本函数。后端可在这里初始化全局时钟或 loop 所需等待设施。
 *
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
int ns_platform_init(void);

/**
 * @brief 关闭平台层全局状态。
 *
 * `ns_shutdown` 调用期间执行本函数。调用前核心层应已经销毁所有 loop。
 *
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
int ns_platform_shutdown(void);

/**
 * @brief 通过平台层分配内存。
 *
 * 平台层之外的实现代码不得直接调用 C 库分配函数。emit 路径不得调用本函数。
 *
 * @param size 分配字节数。
 * @return 成功时返回非空指针，失败时返回 `NULL`。
 */
void *ns_platform_alloc(size_t size);

/**
 * @brief 释放平台层分配的内存。
 *
 * @param ptr 待释放指针，可为 `NULL`。
 */
void ns_platform_free(void *ptr);

/**
 * @brief 创建 TLS key。
 *
 * TLS key 用于 loop manager 的当前线程 fast path。
 *
 * @param out_key 输出 TLS key。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
int ns_platform_tls_key_create(ns_platform_tls_key_t **out_key);

/**
 * @brief 销毁 TLS key。
 *
 * @param key TLS key。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
int ns_platform_tls_key_destroy(ns_platform_tls_key_t *key);

/**
 * @brief 读取当前线程在 TLS key 上绑定的值。
 *
 * @param key TLS key。
 * @param out_value 输出当前线程保存的指针值。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
int ns_platform_tls_get(ns_platform_tls_key_t *key, void **out_value);

/**
 * @brief 设置当前线程在 TLS key 上绑定的值。
 *
 * @param key TLS key。
 * @param value 要保存的指针值，可为 `NULL`。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
int ns_platform_tls_set(ns_platform_tls_key_t *key, void *value);

/**
 * @brief 创建 wakeup handle。
 *
 * @param out_wakeup 输出 wakeup 句柄。
 * @param debug_name 调试名称；平台层不接管字符串所有权。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
int ns_platform_wakeup_create(ns_platform_wakeup_t **out_wakeup, const char *debug_name);

/**
 * @brief 销毁 wakeup handle。
 *
 * @param wakeup wakeup 句柄。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
int ns_platform_wakeup_destroy(ns_platform_wakeup_t *wakeup);

/**
 * @brief 触发 wakeup。
 *
 * 本函数可从跨线程 emit 或控制路径调用，不允许分配内存。
 *
 * @param wakeup wakeup 句柄。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
int ns_platform_wakeup_signal(ns_platform_wakeup_t *wakeup);

/**
 * @brief 等待单个 wakeup。
 *
 * @param wakeup wakeup 句柄。
 * @param timeout_us 超时时间，单位为微秒；`NS_PLATFORM_WAIT_INFINITE_US`
 *        表示无限等待。
 * @param out_result 输出等待结果。
 * @return `NS_OK` 表示等待操作本身成功，失败时返回负数状态码。
 */
int ns_platform_wakeup_wait(
    ns_platform_wakeup_t *wakeup,
    ns_platform_time_us_t timeout_us,
    ns_platform_wait_result_t *out_result);

/**
 * @brief 创建互斥锁。
 *
 * @param out_mutex 输出互斥锁句柄。
 * @param debug_name 调试名称；平台层不接管字符串所有权。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
int ns_platform_mutex_create(ns_platform_mutex_t **out_mutex, const char *debug_name);

/**
 * @brief 销毁互斥锁。
 *
 * @param mutex 互斥锁句柄。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
int ns_platform_mutex_destroy(ns_platform_mutex_t *mutex);

/**
 * @brief 加锁。
 *
 * @param mutex 互斥锁句柄。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
int ns_platform_mutex_lock(ns_platform_mutex_t *mutex);

/**
 * @brief 解锁。
 *
 * @param mutex 互斥锁句柄。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
int ns_platform_mutex_unlock(ns_platform_mutex_t *mutex);

/**
 * @brief 读取平台单调时间。
 *
 * 单位为微秒。该时间只保证单调递增，不能解释为真实日历时间。
 *
 * @param out_now_us 输出当前单调时间。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
int ns_platform_clock_monotonic_us(ns_platform_time_us_t *out_now_us);

#ifdef __cplusplus
}
#endif

#endif /* NANOSIG_PLATFORM_PORT_H */
