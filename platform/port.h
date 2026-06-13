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
 * @brief 等待单个 wakeup（毫秒精度）。
 *
 * 超时精度为毫秒（Linux poll / Windows WaitForSingleObject 原生粒度）。
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

/* ------------------------------------------------------------------ */
/*  waitset（P5b broker / waitset 契约追加）                            */
/* ------------------------------------------------------------------ */

/**
 * @brief 可等待事件位。
 */
#define NS_WAITABLE_EVENT_IN   (1u << 0) /**< 可读 / signaled */
#define NS_WAITABLE_EVENT_OUT  (1u << 1) /**< 可写 */
#define NS_WAITABLE_EVENT_ERR  (1u << 2) /**< 错误 */

/**
 * @brief 可等待句柄。
 *
 * 完整的"等待描述符"：平台原语、用户标签、关注事件和触发模式。
 *
 * **生命周期要求**：调用方必须保证 waitable 在 `ns_platform_waitset_remove`
 * 之前一直有效。Linux 后端通过 epoll `data.ptr` 直接引用本结构（零拷贝），
 * Windows 后端内部保存副本。
 *
 * - Linux：`fd` 字段，eventfd / socket / pipe fd。
 * - Windows：`handle` 字段，HANDLE。
 * - RTOS（v2）：`event_bit` 字段，event group 中的 bit 位置。
 */
typedef struct ns_platform_waitable {
    union {
        int     fd;         /**< Linux: 文件描述符 */
        void   *handle;     /**< Windows: HANDLE */
        int     event_bit;  /**< RTOS: event bit index（v2） */
    };
    void    *user_data;     /**< 关联的用户标签，completion 中原样返回 */
    uint32_t events;        /**< 关注的事件位（NS_WAITABLE_EVENT_*） */
    int      edge_triggered;/**< 1 = 边沿触发（Linux EPOLLET），0 = 电平触发 */
} ns_platform_waitable_t;

/**
 * @brief 初始化 waitable 为零值。
 *
 * @return 零初始化的 waitable。
 */
static inline ns_platform_waitable_t ns_waitable_init(void)
{
    ns_platform_waitable_t w;
#ifdef _WIN32
    w.handle = NULL;  /* Windows: NULL 表示无效 */
#else
    w.fd = -1;        /* Linux: -1 表示无效 */
#endif
    w.user_data = NULL;
    w.events = 0u;
    w.edge_triggered = 0;
    return w;
}

/**
 * @brief waitset 完成事件。
 *
 * 由 `ns_platform_waitset_wait` 填写。`waitable` 指向注册时的 waitable
 * （含 `user_data`），`triggered_events` 是实际触发的事件位。
 */
typedef struct ns_platform_waitset_completion {
    const ns_platform_waitable_t *waitable;         /**< 指向注册时的 waitable */
    uint32_t                      triggered_events; /**< 实际触发的事件位 */
} ns_platform_waitset_completion_t;

/**
 * @brief 平台 waitset 句柄。
 *
 * waitset 是一次等待多个事件源的容器。
 * - Linux：epoll，`data.ptr` 直接指向 caller 的 waitable（零拷贝）。
 * - Windows：WaitForMultipleObjects + 内部数组映射。
 * - RTOS（v2）：event group。
 */
typedef struct ns_platform_waitset ns_platform_waitset_t;

/**
 * @brief 创建 waitset。
 *
 * @param out_waitset 输出 waitset 句柄。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
int ns_platform_waitset_create(ns_platform_waitset_t **out_waitset);

/**
 * @brief 销毁 waitset。
 *
 * @param waitset waitset 句柄。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
int ns_platform_waitset_destroy(ns_platform_waitset_t *waitset);

/**
 * @brief 向 waitset 注册一个 waitable。
 *
 * waitset 内部存储 caller waitable 的指针（零拷贝）。调用方必须保证
 * waitable 在 `ns_platform_waitset_remove` 之前一直有效。
 *
 * @param waitset waitset 句柄。
 * @param waitable 要注册的 waitable（含 events、edge_triggered、user_data）。
 * @return `NS_OK` 成功，`NS_E_EXISTS` 重复注册，`NS_E_TOO_MANY_HANDLES` 容量满。
 */
int ns_platform_waitset_add(
    ns_platform_waitset_t *waitset,
    const ns_platform_waitable_t *waitable);

/**
 * @brief 从 waitset 移除一个 waitable。
 *
 * @param waitset waitset 句柄。
 * @param waitable 要移除的 waitable。
 * @return `NS_OK` 成功，`NS_E_INVAL` 未注册。
 */
int ns_platform_waitset_remove(
    ns_platform_waitset_t *waitset,
    const ns_platform_waitable_t *waitable);

/**
 * @brief 等待事件（微秒精度）。
 *
 * 阻塞直到至少一个 waitable 触发或超时。timeout 单位为微秒，
 * 通过 timerfd（Linux）或 WaitableTimer（Windows）实现微秒级精度。
 * 用于 broker 的 timer deadline 等待场景。
 *
 * @param waitset waitset 句柄。
 * @param timeout_us 超时时间；0 = 非阻塞，`NS_PLATFORM_WAIT_INFINITE_US` = 无限等待。
 * @param completions 输出数组，由调用方提供。
 * @param max_completions 数组容量，最大 64。
 * @param out_count 实际触发数。
 * @return `NS_OK` 成功，失败时返回负数状态码。
 */
int ns_platform_waitset_wait(
    ns_platform_waitset_t *waitset,
    ns_platform_time_us_t timeout_us,
    ns_platform_waitset_completion_t *completions,
    size_t max_completions,
    size_t *out_count);

#ifdef __cplusplus
}
#endif

#endif /* NANOSIG_PLATFORM_PORT_H */
