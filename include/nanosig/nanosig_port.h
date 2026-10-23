/**
 * @file nanosig_port.h
 * @brief nanosig 平台端口 — 平台相关公开头文件。
 * @date 2026-06-14
 *
 * 本文件集中平台原语类型（由构建系统注入的平台后端头提供）、waitable 事件
 * 描述符、平台抽象层函数声明。
 *
 * 本文件**不含任何平台判定宏**：平台类型与 ABI 探针全部由固定名的平台后端头
 * `nanosig_platform_port.h`（`platform/<os>/`）提供。各平台类型都是该平台内核
 * 原语的**透明别名**，且全部为调用方拥有存储——平台层不再为平台对象分配内存。
 *
 * @thread-safety
 * 平台对象的存储由调用方拥有，库不分配也不保护其生命周期（唯一例外：Zephyr 后端的
 * 线程对象由 port 分配、`ns_platform_thread_join` 回收）：
 * - `*_init` / `*_deinit` / `ns_platform_thread_join` / `*_wakeup_wait` /
 *   `*_waitset_wait` 不得对同一对象并发调用。
 * - `ns_platform_wakeup_signal` / `ns_platform_event_signal` 允许跨线程调用。
 * - `ns_init` / `ns_shutdown` 是单线程顺序调用点，不得与任何其他 API 并发；
 *   见 `nanosig.h`。
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#ifndef NANOSIG_PORT_H
#define NANOSIG_PORT_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <nanosig/nanosig_status.h>
#include <nanosig/nanosig_types.h>

/* ================================================================== */
/*  平台后端头前置声明                                                   */
/* ================================================================== */

/**
 * @brief 平台线程入口。
 */
typedef void (*ns_platform_thread_fn)(void *arg);

/** 平台后端头持有调用方 waitable 的指针；便于零拷贝映射（Linux/macOS/Windows/FreeRTOS/Zephyr）。 */
struct ns_platform_waitable;

/* ================================================================== */
/*  平台类型 — 由平台后端头提供                                          */
/* ================================================================== */

/**
 * 构建系统只需把 `platform/<os>/` 加入 include 路径，本文件按固定名
 * `nanosig_platform_port.h` 引入平台后端头（各后端头同名，由 include 路径
 * 决定选中哪一个）。缺这条 include 路径会在编译期立刻失败，而不会静默
 * 退化为错误的平台类型。
 *
 * 平台后端头提供的符号：
 * - `ns_waitable_handle_t` 及其谓词 `ns_waitable_handle_is_valid(h)`；
 * - `NS_PLATFORM_ABI_PROBE`（跨 TU 平台错配的链接期探针）；
 * - `ns_platform_wakeup_t` / `ns_platform_mutex_t` / `ns_platform_thread_t`
 *   / `ns_platform_waitset_t`，全部为平台内核原语的透明别名。
 *
 * 本 include 有意置于 `extern "C"` 之前：Windows 后端头直接包含 `<windows.h>`，
 * 若被 C 链接包围会把 Win32 的 C++ 声明（重载/模板）一并带入 C 链接。
 */
#include <nanosig_platform_port.h>

#ifdef __cplusplus
extern "C" {
#endif

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
 * `primitive` 字段存储平台原语句柄（`ns_waitable_handle_t`），调用方通过
 * `ns_watcher_init` 或平台层 API 填充，不应直接修改。
 *
 * 本结构体直接内嵌在 `ns_watcher_t` 中。
 *
 * - Linux/macOS：fd（eventfd / kqueue / socket / pipe）。
 * - Windows：HANDLE。
 * - FreeRTOS：queue-set-capable 的 FreeRTOS 对象句柄（二值/计数信号量或队列）。
 * - Zephyr：`k_poll` 可等待对象指针。v1 只接受 `ns_platform_event_t` 内嵌的
 *   `k_poll_signal`（IN-only、signal 类）。
 */
typedef struct ns_platform_waitable {
    ns_waitable_handle_t primitive;          /**< 平台原语句柄 */
    void                *user_data;          /**< 关联的用户标签，completion 中原样返回 */
    void                *registered_waitset; /**< 已注册的 waitset，平台层内部维护 */
    uint32_t             events;             /**< 关注的事件位（NS_WAITABLE_EVENT_*） */
    int                  edge_triggered;     /**< 1 = 边沿，0 = 电平；Linux 映射 `EPOLLET`、macOS 映射 `EV_CLEAR`，Windows/Zephyr/FreeRTOS 忽略 */
} ns_platform_waitable_t;

/**
 * @brief 初始化 waitable 为未注册的无效状态。
 *
 * @thread-safety unsafe 只应在初始化线程单线程调用。
 *
 * 本函数把 `primitive` 初始化为全 1 位无效值：句柄型平台对应无效句柄
 * （FreeRTOS / Zephyr 约定为 `(void *)~0` 哨兵），fd 型平台对应 `-1`。
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
/*  平台抽象层 — 通用类型                                                */
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
 * @brief 空 waitset 上"无限等待"的封顶值，单位为微秒。
 *
 * `ns_platform_waitset_wait` 在 waitset 为空（调用时无任何已注册 waitable）且
 * timeout 为 `NS_PLATFORM_WAIT_INFINITE_US` 时**不会永久阻塞**：调用时没有任何
 * 事件源，真无限等待的后果就是线程静默挂死、连错误码都拿不到。此时后端把等待
 * **封顶**到本值后正常返回，使调用方总能周期性拿回控制权重估状态。本值默认
 * 1 秒：它只在调用方误用"空 waitset + 无限超时"时生效，取小值使契约测试无需实等。
 *
 * 封顶是等待时间的**上界**，不是"保证睡满本值"：等待期间若出现就绪成员（例如
 * 另一线程在期间注册且已就绪的 waitable），后端可能提前返回该 completion——能否
 * 看到后续注册取决于后端是否用调用时快照（Windows/Zephyr 用快照，看不到），与
 * 本值无关。waitable 非空时不受影响，仍是真无限等待；有限超时本就有界，也不受影响。
 *
 * 可在编译期覆盖：`-DNS_PLATFORM_WAIT_EMPTY_MAX_US=<n>`。
 */
#ifndef NS_PLATFORM_WAIT_EMPTY_MAX_US
#define NS_PLATFORM_WAIT_EMPTY_MAX_US 1000000u
#endif

/**
 * @brief 平台等待结果。
 */
typedef enum ns_platform_wait_result {
    NS_PLATFORM_WAIT_SIGNALED = 0,
    NS_PLATFORM_WAIT_TIMEOUT = 1
} ns_platform_wait_result_t;

/* ================================================================== */
/*  平台抽象层 — waitset 完成事件                                        */
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
 * 用于库内部结构（loop / broker / 数据结构 / 格式化缓冲）的通用分配。**平台对象
 * 不使用本函数分配**：平台对象由调用方拥有存储，见 `nanosig_port.h` 顶部的
 * @thread-safety。平台层之外的实现代码不得直接调用 C 库分配函数。
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
 * @brief 初始化 wakeup。
 *
 * wakeup 是 loop 的等待/唤醒原语，也是唯一的阻塞单等待原语。
 *
 * @warning 非幂等：对同一 wakeup 重复 init 而不先 deinit 会覆盖字段并泄漏第一次
 *          分配的平台资源（eventfd fd / kqueue fd / Event HANDLE / 信号量）。
 *          必须 init 一次、deinit 一次。
 * @pre 调用方必须保证 `wakeup` 指向有效存储，且 `ns_platform_wakeup_deinit` 之前
 *      不得重复调用本函数。
 *
 * @param wakeup 调用方拥有的 wakeup 存储。
 * @param debug_name 调试名称；平台层不接管字符串所有权。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
int ns_platform_wakeup_init(ns_platform_wakeup_t *wakeup, const char *debug_name);

/**
 * @brief 释放 wakeup 持有的平台资源。
 *
 * 释放后 `wakeup` 存储仍归调用方所有，但其内容不可再用（除非再次 init）。
 *
 * @param wakeup 调用方拥有的 wakeup 存储。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
int ns_platform_wakeup_deinit(ns_platform_wakeup_t *wakeup);

/**
 * @brief 触发 wakeup。
 *
 * 本函数可从跨线程 emit 或控制路径调用，不允许分配内存。
 *
 * @param wakeup wakeup。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
int ns_platform_wakeup_signal(ns_platform_wakeup_t *wakeup);

/**
 * @brief 等待单个 wakeup。
 *
 * 超时精度随后端原生等待能力而定：Linux poll 和 Windows
 * WaitForSingleObject 以毫秒为粒度，macOS kevent 使用 timespec timeout，
 * FreeRTOS / Zephyr 为系统 tick 粒度（微秒值向上取整到一个 tick）。
 *
 * @param wakeup wakeup。
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
 * FreeRTOS 上指向同一二值信号量，在 Zephyr 上指向同一 `k_poll_signal`，在 macOS
 * 上是 pipe 的读端与写端两个不同 fd。这是实现细节而非契约：调用方
 * 只应通过 `ns_platform_event_*` API 操作，禁止依赖两侧是否同值或互换使用。
 *
 * 本原语不提供阻塞单等待；唯一阻塞单等待原语是 wakeup。
 * 单事件阻塞语义请建 waitset 注册 `waitable` 后调用 `ns_platform_waitset_wait`。
 *
 * 各后端统一硬语义：
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
    ns_waitable_handle_t   signal_handle; /**< 触发侧：signal 写入此句柄（Linux/Windows/FreeRTOS/Zephyr 与 waitable 同值，macOS=pipe 写端 fd） */
} ns_platform_event_t;

/**
 * @brief 初始化 event。
 *
 * @warning 非幂等：对同一 event 重复 init 而不先 deinit 会覆盖字段并泄漏第一次
 *          分配的底层资源。必须 init 一次、deinit 一次。
 * @pre 调用方必须保证 `event` 指向有效存储，且在 `ns_platform_event_deinit`
 *      之前不得重复调用本函数。
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

/* ================================================================== */
/*  平台抽象层 — mutex                                                   */
/* ================================================================== */

/**
 * @brief 初始化互斥锁。
 *
 * @warning 非幂等：重复 init 而不先 deinit 会覆盖字段并泄漏第一次分配的平台资源。
 *
 * @param mutex 调用方拥有的互斥锁存储。
 * @param debug_name 调试名称；平台层不接管字符串所有权。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
int ns_platform_mutex_init(ns_platform_mutex_t *mutex, const char *debug_name);

/**
 * @brief 释放互斥锁持有的平台资源。
 *
 * @param mutex 调用方拥有的互斥锁存储。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
int ns_platform_mutex_deinit(ns_platform_mutex_t *mutex);

/**
 * @brief 加锁。
 *
 * @param mutex 互斥锁。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
int ns_platform_mutex_lock(ns_platform_mutex_t *mutex);

/**
 * @brief 解锁。
 *
 * @param mutex 互斥锁。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
int ns_platform_mutex_unlock(ns_platform_mutex_t *mutex);

/* ================================================================== */
/*  平台抽象层 — 时钟与线程                                              */
/* ================================================================== */

/**
 * @brief 读取平台单调时间。
 *
 * 单位为微秒。该时间只保证单调递增，不能解释为真实日历时间。
 *
 * @note FreeRTOS 后端以 `xTaskGetTickCount` 为时间源，在内部把可能回绕的 `TickType_t`
 *       扩展为 64 位累加 tick（无符号回绕差值 + 临界区）后再换算微秒，回绕后仍保持单调。
 *       **前提**：FreeRTOS 自身的 tick 回绕不能太快——相邻两次采样之间必须最多发生一次
 *       回绕（采样间隔小于一个 `TickType_t` 回绕周期），否则会少计整周期而出现时间跳变。
 *       典型配置下回绕周期很长（32 位 tick @`configTICK_RATE_HZ=1000` 约 49.7 天），远大于
 *       正常采样间隔；若 `configTICK_RATE_HZ` 与 tick 位宽使回绕周期明显更短，需提高采样
 *       频率或改用更宽的 tick。
 *
 * @param out_now_us 输出当前单调时间。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
int ns_platform_clock_monotonic_us(ns_platform_time_us_t *out_now_us);

/**
 * @brief 查询当前后端接受的线程栈大小区间。
 *
 * 供调用方在把线程栈调小（例如嵌入式场景省 RAM）之前确认合法下界，也供契约测试
 * 用同一份判据覆盖全部后端。返回值是**后端策略**：只有 POSIX 的下界来自平台保证
 * （`PTHREAD_STACK_MIN`）；Windows、FreeRTOS 与 Zephyr 下界都是 nanosig 自定的
 * 策略值，只用于**拒绝明显过小的请求**，不保证不会栈溢出。
 *
 * @param min_out 输出**最小可接受**的栈大小请求（字节）；可为 `NULL`。
 *        `*min_out - 1` 必然被后端拒绝。后端会按自身粒度向上取整请求值
 *        （FreeRTOS 按 `StackType_t`），故本值是"最小可接受请求"而非"下限字节数"。
 * @param max_out 输出最大可接受栈大小（字节）；可为 `NULL`。无实际上限时为 `SIZE_MAX`。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
int ns_platform_thread_stack_range(size_t *min_out, size_t *max_out);

/**
 * @brief 初始化平台线程并立即启动。
 *
 * @warning 非幂等：重复 init 而不先 `ns_platform_thread_join` 会覆盖字段并泄漏第一次
 *          分配的平台资源。
 * @pre 调用方必须保证 `thread` 在 `ns_platform_thread_join` 返回前一直有效。
 * @note Zephyr 后端的 `ns_platform_thread_t` 是由 port 分配、`ns_platform_thread_join`
 *       回收的不透明对象（内核栈由 `k_thread_stack_alloc` 单独分配），因此 init 可能
 *       返回 `NS_E_NOMEM`；**全部后端**的该类型都不再需要特殊对齐。
 *
 * @param thread 线程存储；Zephyr 后端为 port 分配的对象句柄。
 * @param entry 线程入口。
 * @param arg 传给入口的参数。
 * @param debug_name 调试名称；平台层不接管字符串所有权。
 * @param stack_size 线程栈大小，单位**字节**。`0` 表示使用后端默认栈大小。
 *        小于 `ns_platform_thread_stack_range` 给出的下界时返回 `NS_E_INVAL` 且
 *        **不创建线程**。实际分配的栈永远不小于请求值：POSIX 后端向上取整到页
 *        粒度（4096 字节），FreeRTOS 向上取整到 `StackType_t` 整数倍。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
int ns_platform_thread_init(
    ns_platform_thread_t *thread,
    ns_platform_thread_fn entry,
    void *arg,
    const char *debug_name,
    size_t stack_size);

/**
 * @brief 阻塞等待平台线程退出，并释放其持有的平台资源。
 *
 * 返回后 `thread` 存储仍归调用方所有，但其内容不可再用（除非再次 init）。
 *
 * @note `ns_platform_thread_init` **失败**后不得视为线程已创建：此时 join 返回
 *       `NS_E_INVAL`（不阻塞、不崩溃）。五个后端一致。
 * @note Zephyr 后端 join 失败（如自 join）时**不回收**线程对象与栈 —— 目标线程
 *       可能仍在运行，回收会造成 use-after-free。句柄保留，由调用方处理。
 *
 * @param thread 调用方拥有的线程存储。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
int ns_platform_thread_join(ns_platform_thread_t *thread);

/* ================================================================== */
/*  平台抽象层 — waitset                                                 */
/* ================================================================== */

/**
 * @brief 初始化 waitset。
 *
 * waitset 是一次等待多个事件源的容器。各后端内部映射：
 * - Linux：epoll，`data.ptr` 直接指向 caller 的 waitable（零拷贝）。
 * - macOS：kqueue，`udata` 直接指向 caller 的 waitable（零拷贝）。
 * - Windows：WaitForMultipleObjects + 内部数组映射。
 * - FreeRTOS：queue set（`xQueueCreateSet` + `xQueueSelectFromSet`）。
 * - Zephyr：`k_poll`（每次 `wait` 由活跃 slot 构造 `struct k_poll_event[]`）。
 *
 * @warning 非幂等：重复 init 而不先 deinit 会覆盖字段并泄漏第一次分配的平台资源。
 *
 * @param waitset 调用方拥有的 waitset 存储。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
int ns_platform_waitset_init(ns_platform_waitset_t *waitset);

/**
 * @brief 释放 waitset 持有的平台资源。
 *
 * waitset 必须没有已注册的 waitable；仍有注册项时返回 `NS_E_EXISTS`。
 *
 * @param waitset 调用方拥有的 waitset 存储。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
int ns_platform_waitset_deinit(ns_platform_waitset_t *waitset);

/**
 * @brief 向 waitset 注册一个 waitable。
 *
 * waitset 内部存储 caller waitable 的指针（零拷贝）。调用方必须保证
 * waitable 在 `ns_platform_waitset_remove` 之前一直有效。注册成功后平台层
 * 会写入 waitable 的注册状态，同一 waitable 不可同时注册到多个 waitset。
 *
 * @param waitset waitset。
 * @param waitable 要注册的 waitable（含 events、edge_triggered、user_data）。
 * @return `NS_OK` 成功，`NS_E_EXISTS` 表示同一 waitable 已注册，
 *         `NS_E_TOO_MANY_HANDLES` 容量满。
 *
 * @note FreeRTOS 与 Zephyr 后端只接受 `NS_WAITABLE_EVENT_IN`：含 OUT/ERR 位的
 *       waitable 返回 `NS_E_INVAL`；Zephyr 还只接受 `k_poll_signal`（event 类）
 *       waitable。FreeRTOS 在加入 queue set 前成员必须为空，否则也返回 `NS_E_INVAL`。
 *       无效句柄（`NULL` 或 `ns_waitable_init` 写入的 `(void *)~0` 哨兵）返回
 *       `NS_E_INVAL`，不注册：FreeRTOS / Zephyr 与 Linux / macOS 一致，避免把哨兵
 *       交给底层等待原语解引用。
 *       FreeRTOS 忽略 `edge_triggered`（后端无原生电平/边沿模式，同 Windows/Zephyr）；
 *       其就绪是 queue set 的 token 语义（每次 send/give 记一次、取走 token 不消费
 *       成员数据、不对仍非空的成员重复上报），调用方应把每次就绪处理到空。
 */
int ns_platform_waitset_add(
    ns_platform_waitset_t *waitset,
    ns_platform_waitable_t *waitable);

/**
 * @brief 从 waitset 移除一个 waitable。
 *
 * 返回值只有 `NS_OK` 和 `NS_E_INVAL` 两种。实现负责该 waitable 残留就绪 token 的
 * 内部生命周期，调用方不需要、也不会看到 `NS_E_BUSY` 或其他失败码。
 *
 * @note 就绪 token 生命周期只涉及 waitset 内部的就绪记账，不消费成员对象自身的数据：
 *       fd / socket / HANDLE / `k_poll` 对象的未读数据不受 remove 影响，remove 不
 *       reset、不消费。**FreeRTOS 后端是已知例外/取舍**：其 queue set 的
 *       `xQueueRemoveFromSet` 要求成员为空，后端为摘除成员而先排空它，因此对**数据队列**
 *       成员会丢弃队列中所有未取走的消息（payload）——这是向 queue set 约束妥协的结果，
 *       而非有意设计。需要保留数据的调用方必须在调用本函数前自行取走。
 *
 * @param waitset waitset。
 * @param waitable 要移除的 waitable。
 * @return `NS_OK` 成功，`NS_E_INVAL` 未注册。
 */
int ns_platform_waitset_remove(
    ns_platform_waitset_t *waitset,
    ns_platform_waitable_t *waitable);

/**
 * @brief 等待事件（微秒输入）。
 *
 * 阻塞直到至少一个 waitable 触发或超时。timeout 单位为微秒；
 * Linux（timerfd）、macOS（kevent timeout）、Windows（WaitableTimer）可达微秒级精度，
 * FreeRTOS / Zephyr 受系统 tick 限制（微秒值向上取整到一个 tick，不保证微秒级）。
 * 用于 broker 的 timer deadline 等待场景。
 *
 * @param waitset waitset。
 * @param timeout_us 超时时间；0 = 非阻塞，`NS_PLATFORM_WAIT_INFINITE_US` = 无限等待。
 *        例外：waitset **为空**（调用时无任何已注册 waitable）时，无限等待被封顶到
 *        `NS_PLATFORM_WAIT_EMPTY_MAX_US`，避免没有任何事件源时的静默挂死。封顶是等待
 *        时间的**上界**，不是保证睡满：等待期间若另一线程注册的 waitable 就绪，后端
 *        可能提前返回该 completion（Windows/Zephyr 用调用时快照，看不到后续注册）。
 *        有限超时本就有界，不受影响。
 * @param completions 输出数组，由调用方提供。
 * @param max_completions 输出数组容量；实际写入数不超过同时就绪的成员数。各后端
 *        可注册成员上限为 Linux/macOS 64、Windows 63（预留 1 个内部 timer slot）、
 *        FreeRTOS/Zephyr 默认 32，故容量取 64 可覆盖所有后端可能的最大同时就绪数。
 *        为 0 表示"这次不接收任何 completion"：函数立即返回 `NS_OK` 且
 *        `out_count == 0`，不做任何等待（没有可写入空间时等待没有意义），五个后端一致。
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
