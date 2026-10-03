/**
 * @file nanosig_port.h
 * @brief nanosig 平台端口 — 唯一的平台相关公开头文件。
 * @date 2026-06-14
 *
 * 本文件集中所有平台检测宏、平台原语类型、waitable 事件描述符、
 * 平台抽象层函数声明。nanosig 公开头文件中，只有本文件允许出现
 * `#if defined(_WIN32)` 等平台检测代码。
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#ifndef NANOSIG_PORT_H
#define NANOSIG_PORT_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <nanosig/nanosig_status.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================== */
/*  平台判定                                                            */
/* ================================================================== */

/** @brief 当前编译目标为 Windows。 */
#if defined(_WIN32)
#define NANOSIG_PLATFORM_WINDOWS 1
#elif defined(__APPLE__)
#define NANOSIG_PLATFORM_APPLE 1
#elif defined(__linux__) || defined(__unix__)
#define NANOSIG_PLATFORM_POSIX 1
#endif

/* ================================================================== */
/*  ns_waitable_handle_t — 平台可等待原语联合体                          */
/* ================================================================== */

/**
 * @brief 平台可等待原语联合体。
 *
 * 调用方根据当前平台填充对应成员，通过本文件提供的辅助宏访问。
 */
typedef union ns_waitable_handle {
    int     fd;         /**< Linux/macOS: 文件描述符 */
    void   *handle;     /**< Windows: HANDLE */
    int     event_bit;  /**< RTOS: event bit index（v2） */
} ns_waitable_handle_t;

/* ================================================================== */
/*  可等待事件位                                                         */
/* ================================================================== */

#define NS_WAITABLE_EVENT_IN   (1u << 0) /**< 可读 / signaled */
#define NS_WAITABLE_EVENT_OUT  (1u << 1) /**< 可写 */
#define NS_WAITABLE_EVENT_ERR  (1u << 2) /**< 错误 */

/* ================================================================== */
/*  ns_platform_waitable_t — 可等待事件描述符                             */
/* ================================================================== */

/**
 * @brief 可等待事件描述符。
 *
 * `primitive` 字段存储平台原语（fd / HANDLE / event_bit），调用方通过
 * `ns_watcher_init` 或平台层 API 填充，不应直接修改。
 *
 * 本结构体直接内嵌在 `ns_watcher_t` 中。
 *
 * - Linux/macOS：`primitive.fd`，eventfd / kqueue / socket / pipe fd。
 * - Windows：`primitive.handle`，HANDLE。
 * - RTOS（v2）：`primitive.event_bit`，event group 中的 bit 位置。
 */
typedef struct ns_platform_waitable {
    ns_waitable_handle_t primitive;        /**< 平台原语（fd / HANDLE / event_bit） */
    void                *user_data;        /**< 关联的用户标签，completion 中原样返回 */
    void                *registered_waitset; /**< 已注册的 waitset，平台层内部维护 */
    uint32_t             events;           /**< 关注的事件位（NS_WAITABLE_EVENT_*） */
    int                  edge_triggered;   /**< 1 = 边沿触发，0 = 电平触发 */
} ns_platform_waitable_t;

/**
 * @brief 初始化 waitable 为未注册的无效状态。
 *
 * @thread-safety unsafe 只应在初始化线程单线程调用。
 *
 * 本函数把 `primitive` 初始化为全 1 位无效值：Windows 对应
 * `INVALID_HANDLE_VALUE`，Linux/macOS fd 对应 `-1`。
 *
 * @param w 待初始化的 waitable，可为 `NULL`。
 */
static inline void ns_waitable_init(ns_platform_waitable_t *w)
{
    if(w == NULL) return;

    (void)memset(&w->primitive, 0xFF, sizeof(w->primitive));
    w->user_data = NULL;
    w->registered_waitset = NULL;
    w->events = 0u;
    w->edge_triggered = 0;
}

/* ================================================================== */
/*  辅助宏 — ns_waitable_handle_t ↔ ns_platform_waitable_t 转换        */
/* ================================================================== */

/**
 * @brief 检查 `ns_waitable_handle_t` 在当前平台是否有效。
 *
 * Windows：`.handle != NULL`；其他平台：`.fd >= 0`。
 *
 * @param h `ns_waitable_handle_t` 值。
 * @return 非零表示有效，零表示无效。
 */
#if defined(_WIN32)
#define ns_waitable_handle_is_valid(h) ((h).handle != NULL)
#else
#define ns_waitable_handle_is_valid(h) ((h).fd >= 0)
#endif

/**
 * @brief 把平台句柄值写入 `ns_platform_waitable_t::primitive`。
 *
 * @param waitable_ptr 指向 `ns_platform_waitable_t` 的指针。
 * @param handle_val   `ns_waitable_handle_t` 值。
 */
#if defined(_WIN32)
#define NS_WAITABLE_SET(waitable_ptr, handle_val) \
    ((waitable_ptr)->primitive.handle = (handle_val).handle)
#else
#define NS_WAITABLE_SET(waitable_ptr, handle_val) \
    ((waitable_ptr)->primitive.fd = (handle_val).fd)
#endif

/**
 * @brief 从 `ns_platform_waitable_t::primitive` 提取 `ns_waitable_handle_t`。
 *
 * 供 `consume_fn` 内部使用，从 watcher waitable 中取出平台句柄。
 *
 * @param waitable_ptr 指向 `ns_platform_waitable_t` 的指针。
 * @return 对应平台的 `ns_waitable_handle_t` 值。
 */
#if defined(_WIN32)
#define NS_WAITABLE_GET(waitable_ptr) \
    ((ns_waitable_handle_t){.handle = (waitable_ptr)->primitive.handle})
#else
#define NS_WAITABLE_GET(waitable_ptr) \
    ((ns_waitable_handle_t){.fd = (waitable_ptr)->primitive.fd})
#endif

/* ================================================================== */
/*  平台抽象层 — 类型                                                    */
/* ================================================================== */

/**
 * @brief 无限等待的超时值，单位为微秒。
 */
#define NS_PLATFORM_WAIT_INFINITE_US UINT64_MAX

/**
 * @brief 平台单调时间，单位为微秒。
 */
typedef uint64_t ns_platform_time_us_t;

/**
 * @brief 平台 wakeup 句柄。
 *
 * wakeup 是 loop 的等待/唤醒原语，也是唯一的阻塞单等待原语。创建和销毁
 * 可以分配资源；`ns_platform_wakeup_signal` 不允许分配内存。
 *
 * 需要注册进 waitset 的事件源请使用 `ns_platform_event_t`；wakeup 不再
 * 暴露内部 fd/handle 转换。
 */
typedef struct ns_platform_wakeup ns_platform_wakeup_t;

/**
 * @brief 平台互斥锁句柄。
 */
typedef struct ns_platform_mutex ns_platform_mutex_t;

/**
 * @brief 平台线程句柄。
 */
typedef struct ns_platform_thread ns_platform_thread_t;

/**
 * @brief 平台线程入口。
 */
typedef void (*ns_platform_thread_fn)(void *arg);

/**
 * @brief 平台等待结果。
 */
typedef enum ns_platform_wait_result {
    NS_PLATFORM_WAIT_SIGNALED = 0,
    NS_PLATFORM_WAIT_TIMEOUT = 1
} ns_platform_wait_result_t;

/* ================================================================== */
/*  平台抽象层 — waitset                                                 */
/* ================================================================== */

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
 * - macOS：kqueue，`udata` 直接指向 caller 的 waitable（零拷贝）。
 * - Windows：WaitForMultipleObjects + 内部数组映射。
 * - RTOS（v2）：event group。
 */
typedef struct ns_platform_waitset ns_platform_waitset_t;

/* ================================================================== */
/*  平台抽象层 — 函数声明                                                */
/* ================================================================== */

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
 * 平台层之外的实现代码不得直接调用 C 库分配函数。
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
 * 超时精度随后端原生等待能力而定：Linux poll 和 Windows
 * WaitForSingleObject 以毫秒为粒度，macOS kevent 使用 timespec timeout。
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

/* ================================================================== */
/*  平台抽象层 — event                                                   */
/* ================================================================== */

/**
 * @brief 可等待的事件原语（调用方拥有存储）。
 *
 * event 是 waitable-first 的跨线程事件原语：调用方声明本结构体，通过
 * `ns_platform_event_init` 初始化，注册进 waitset 后可从任意线程
 * `ns_platform_event_signal` 触发。
 *
 * `waitable` 是等待/消费侧：注册进 waitset，`ns_platform_event_drain` 从此侧
 * 消费（`events`、`user_data` 由调用方设置）。`signal_handle` 是触发侧：
 * `ns_platform_event_signal` 写此句柄。
 *
 * 两侧在 Linux/Windows 上指向同一底层对象（eventfd / Event HANDLE），在
 * macOS 上是 pipe 的读端与写端两个不同 fd。这是实现细节而非契约：调用方
 * 只应通过 `ns_platform_event_*` API 操作，禁止依赖两侧是否同值或互换使用。
 *
 * 本原语不提供阻塞单等待；唯一阻塞单等待原语是 `ns_platform_wakeup_t`。
 * 单事件阻塞语义请建 waitset 注册 `waitable` 后调用 `ns_platform_waitset_wait`。
 *
 * 三平台统一硬语义：
 * - 布尔语义：event 是布尔量而非计数器，同轮多次 signal 可合并且合法，
 *   调用方不得依赖 signal 次数等于触发次数。
 * - signal 遇底层缓冲已满（macOS pipe EAGAIN）视为成功返回 NS_OK，
 *   因为目标 wait 面已处于 signaled；仅真实写错误才返回错误码。
 * - drain 幂等；drain 后 wait 不再重复触发。drain 是否复位底层对象
 *   由平台决定（Windows auto-reset 在 wait 返回时已复位，drain 为 no-op）。
 * - deinit 前必须先 `ns_platform_waitset_remove`，否则返回 NS_E_BUSY。
 *
 * 生命周期约束：
 * @warning `ns_platform_event_init` 非幂等：对同一 event 重复 init 而不先
 *          deinit 会覆盖字段并泄漏第一次分配的底层资源（Linux eventfd fd /
 *          macOS pipe 两个 fd / Windows CreateEvent HANDLE）。必须 init 一次、
 *          deinit 一次。
 * @pre `signal` / `drain` / `deinit` 必须在成功 `init` 之后调用；`deinit` 另需
 *      已 `ns_platform_waitset_remove`。
 */
typedef struct ns_platform_event {
    ns_platform_waitable_t waitable;      /**< 等待/消费侧：注册进 waitset，drain 读取此侧 */
    ns_waitable_handle_t   signal_handle; /**< 触发侧：signal 写入此句柄（linux/win 与 waitable 同值，macos=pipe 写端 fd） */
} ns_platform_event_t;

/**
 * @brief 初始化 event。
 *
 * @param event 调用方拥有的 event。
 * @param debug_name 调试名称；平台层不接管字符串所有权。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
int ns_platform_event_init(ns_platform_event_t *event, const char *debug_name);

/**
 * @brief 触发 event。
 *
 * 跨线程安全，不允许分配内存。同轮多次调用可合并。
 *
 * @param event event。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
int ns_platform_event_signal(ns_platform_event_t *event);

/**
 * @brief 非阻塞消费 event。
 *
 * 幂等；用于 waitset level-triggered 模式下避免 busy-loop。
 *
 * @param event event。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
int ns_platform_event_drain(ns_platform_event_t *event);

/**
 * @brief 释放 event。
 *
 * 前提：已从 waitset 移除。若仍注册则返回 `NS_E_BUSY`。
 *
 * @param event event。
 * @return `NS_OK` 表示成功，`NS_E_BUSY` 表示仍注册，失败时返回负数状态码。
 */
int ns_platform_event_deinit(ns_platform_event_t *event);

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

/**
 * @brief 创建平台线程。
 *
 * @param out_thread 输出线程句柄。
 * @param entry 线程入口。
 * @param arg 传给入口的参数。
 * @param debug_name 调试名称；平台层不接管字符串所有权。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
int ns_platform_thread_create(
    ns_platform_thread_t **out_thread,
    ns_platform_thread_fn entry,
    void *arg,
    const char *debug_name);

/**
 * @brief join 平台线程并释放线程句柄。
 *
 * @param thread 线程句柄。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
int ns_platform_thread_join(ns_platform_thread_t *thread);

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
 * waitset 必须没有已注册的 waitable；仍有注册项时返回 `NS_E_EXISTS`。
 *
 * @param waitset waitset 句柄。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
int ns_platform_waitset_destroy(ns_platform_waitset_t *waitset);

/**
 * @brief 向 waitset 注册一个 waitable。
 *
 * waitset 内部存储 caller waitable 的指针（零拷贝）。调用方必须保证
 * waitable 在 `ns_platform_waitset_remove` 之前一直有效。注册成功后平台层
 * 会写入 waitable 的注册状态，同一 waitable 不可同时注册到多个 waitset。
 *
 * @param waitset waitset 句柄。
 * @param waitable 要注册的 waitable（含 events、edge_triggered、user_data）。
 * @return `NS_OK` 成功，`NS_E_EXISTS` 表示同一 waitable 已注册，
 *         `NS_E_TOO_MANY_HANDLES` 容量满。
 */
int ns_platform_waitset_add(
    ns_platform_waitset_t *waitset,
    ns_platform_waitable_t *waitable);

/**
 * @brief 从 waitset 移除一个 waitable。
 *
 * @param waitset waitset 句柄。
 * @param waitable 要移除的 waitable。
 * @return `NS_OK` 成功，`NS_E_INVAL` 未注册。
 */
int ns_platform_waitset_remove(
    ns_platform_waitset_t *waitset,
    ns_platform_waitable_t *waitable);

/**
 * @brief 等待事件（微秒输入）。
 *
 * 阻塞直到至少一个 waitable 触发或超时。timeout 单位为微秒，
 * 通过 timerfd（Linux）、kevent timeout（macOS）或 WaitableTimer（Windows）
 * 实现微秒级精度。
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

#endif /* NANOSIG_PORT_H */
