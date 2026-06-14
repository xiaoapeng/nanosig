/**
 * @file nanosig_waitable.h
 * @brief nanosig waitable 事件描述符公开类型。
 * @date 2026-06-14
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#ifndef NANOSIG_WAITABLE_H
#define NANOSIG_WAITABLE_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 可等待事件位。
 */
#define NS_WAITABLE_EVENT_IN   (1u << 0) /**< 可读 / signaled */
#define NS_WAITABLE_EVENT_OUT  (1u << 1) /**< 可写 */
#define NS_WAITABLE_EVENT_ERR  (1u << 2) /**< 错误 */

/**
 * @brief 可等待句柄。
 *
 * 本结构体直接内嵌在 `ns_watcher_t` 中。调用方通常通过
 * `ns_watcher_init_fd` 或 `ns_watcher_init_handle` 填充它，而不是直接修改
 * union 成员。
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
    void    *user_data;      /**< 关联的用户标签，completion 中原样返回 */
    uint32_t events;         /**< 关注的事件位（NS_WAITABLE_EVENT_*） */
    int      edge_triggered; /**< 1 = 边沿触发，0 = 电平触发 */
} ns_platform_waitable_t;

/**
 * @brief 初始化 waitable 为无效零值。
 *
 * 返回值把平台原语 union 初始化为全 1 位无效值：Windows 对应
 * `INVALID_HANDLE_VALUE`，Linux fd 对应 `-1`。
 *
 * @return 初始化后的 waitable。
 */
static inline ns_platform_waitable_t ns_waitable_init(void)
{
    ns_platform_waitable_t w;

    (void)memset(&w, 0xFF, sizeof(w));
    w.user_data = NULL;
    w.events = 0u;
    w.edge_triggered = 0;
    return w;
}

#ifdef __cplusplus
}
#endif

#endif /* NANOSIG_WAITABLE_H */
