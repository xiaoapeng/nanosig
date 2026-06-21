/**
 * @file nanosig.h
 * @brief nanosig 公开入口头文件，包含版本、状态码和全局生命周期 API。
 * @date 2026-05-16
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#ifndef NANOSIG_H
#define NANOSIG_H

#include <nanosig/nanosig_status.h>
#include <nanosig/nanosig_types.h>
#include <nanosig/nanosig_safety.h>
#include <nanosig/nanosig_version.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 nanosig 全局状态。
 *
 * 应用在调用任何 loop、signal、timer 或 watcher API 前先调用本函数。该调用为
 * 平台层和 event broker 准备生命周期边界。
 *
 * @pre 本函数不得与 `ns_shutdown()` 或自身并发调用。调用方应保证在程序生命
 *      周期的单一线程中顺序调用 `ns_init()` 和 `ns_shutdown()`。
 *
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
extern int ns_init(void);

/**
 * @brief 关闭 nanosig 全局状态。
 *
 * 应用在确定不再使用 nanosig 后调用本函数，释放全局资源。调用方应先完成
 * 业务侧 loop、signal、timer 和 watcher 的清理。
 *
 * @pre 调用前必须已销毁所有 loop（`ns_loop_destroy()`）。库不再内部检查，
 *      违反此约束将导致平台状态未定义。
 * @pre 调用前必须已销毁所有 timer（`ns_timer_destroy()`）。
 * @pre 调用前必须已注销所有 watcher（`ns_broker_remove()`）。
 * @pre 本函数不得与 `ns_init()` 或自身并发调用。调用方应保证在程序生命
 *      周期的单一线程中顺序调用 `ns_init()` 和 `ns_shutdown()`。
 *
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
extern int ns_shutdown(void);

/**
 * @brief 查询 nanosig 当前是否已经初始化。
 *
 * @param out_initialized 输出参数；非零表示已初始化，零表示未初始化。
 * @return `NS_OK` 表示成功，失败时返回负数状态码。
 */
extern int ns_is_initialized(int *out_initialized);

#ifdef __cplusplus
}
#endif

#include <nanosig/nanosig_atomic.h>
#include <nanosig/nanosig_broker.h>
#include <nanosig/nanosig_ds.h>
#include <nanosig/nanosig_loop.h>
#include <nanosig/nanosig_mpsc_record_ring.h>
#include <nanosig/nanosig_signal.h>
#include <nanosig/nanosig_timer.h>
#include <nanosig/nanosig_waitable.h>

#endif /* NANOSIG_H */
