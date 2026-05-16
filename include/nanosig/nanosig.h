/**
 * @file nanosig.h
 * @brief nanosig 公开入口头文件，包含版本、状态码和全局生命周期 API。
 * @date 2026-05-16
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#ifndef NANOSIG_H
#define NANOSIG_H

#include <nanosig/nanosig_safety.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NANOSIG_VERSION_MAJOR 0
#define NANOSIG_VERSION_MINOR 1
#define NANOSIG_VERSION_PATCH 0

/**
 * @brief nanosig 公开状态码。
 *
 * 所有公开函数以 `int` 返回这些状态码，`NS_OK` 表示成功，负数表示失败。
 */
typedef enum ns_status {
    NS_OK = 0,
    NS_E_QUEUE_FULL = -1,
    NS_E_NOMEM = -2,
    NS_E_INVAL = -3,
    NS_E_TOO_MANY_HANDLES = -4,
    NS_E_SHUTDOWN = -5,
    NS_E_EXISTS = -6,
    NS_E_NO_LOOP = -7
} ns_status_t;

/**
 * @brief 初始化 nanosig 全局状态。
 *
 * 应用在调用任何 loop、signal 或 timer API 前先调用本函数。该调用为全局
 * timer 服务和后续平台资源准备生命周期边界。
 *
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
int ns_init(void);

/**
 * @brief 关闭 nanosig 全局状态。
 *
 * 应用在确定不再使用 nanosig 后调用本函数，停止全局 timer 服务并释放
 * 全局资源。调用方应先完成业务侧 loop、signal 和 timer 的清理。
 *
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
int ns_shutdown(void);

/**
 * @brief 查询 nanosig 当前是否已经初始化。
 *
 * @param out_initialized 输出参数；非零表示已初始化，零表示未初始化。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
int ns_is_initialized(int *out_initialized);

#ifdef __cplusplus
}
#endif

#include <nanosig/nanosig_loop.h>
#include <nanosig/nanosig_signal.h>
#include <nanosig/nanosig_timer.h>

#endif /* NANOSIG_H */
