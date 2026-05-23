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
#include <nanosig/nanosig_status.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NANOSIG_VERSION_MAJOR 0
#define NANOSIG_VERSION_MINOR 1
#define NANOSIG_VERSION_PATCH 0

/**
 * @brief 初始化 nanosig 全局状态。
 *
 * 应用在调用任何 loop、signal 或 timer API 前先调用本函数。该调用为平台层、
 * loop manager 和后续 event broker 准备生命周期边界。
 *
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
int ns_init(void);

/**
 * @brief 关闭 nanosig 全局状态。
 *
 * 应用在确定不再使用 nanosig 后调用本函数，释放全局资源。调用方应先完成
 * 业务侧 loop、signal、timer 和后续 event broker 的清理。
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

#include <nanosig/nanosig_atomic.h>
#include <nanosig/nanosig_ds.h>
#include <nanosig/nanosig_loop.h>
#include <nanosig/nanosig_signal.h>
#include <nanosig/nanosig_timer.h>

#endif /* NANOSIG_H */
