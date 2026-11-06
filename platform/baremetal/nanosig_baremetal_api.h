/**
 * @file nanosig_baremetal_api.h
 * @brief nanosig 裸机后端专属 API（构建内头，非安装头）。
 * @date 2026-10-31
 *
 * 本头只随 `platform/baremetal/` 的 include 路径分发，因此桌面/RTOS 构建
 * **看不到**这些声明——也就不存在"声明了却链接不到"的公开面。
 *
 * @section 驱动模型
 * 裸机后端没有 broker 线程：signal 的异步派发与 timer 到期判定全部由应用
 * 主循环调用 `ns_loop_poll()` 驱动。
 *
 * ```
 * ns_baremetal_set_now_us(my_now_us);   // 必须在 ns_init() 之前
 * ns_init();
 * ns_loop_create(&loop, NULL);          // 返回内建静态单例，不分配
 *
 * for(;;){
 *     ns_loop_poll(loop, 1000);         // 派发一轮
 *     my_sleep_us(...);                 // 睡眠策略由应用决定
 * }
 * ```
 *
 * @section 语义不变
 * `ns_signal_emit()` 仍然**只入队**，被 emit 的 slot 不会在 emit 调用内执行；
 * 真正的回调发生在 `ns_loop_poll()` 中。这与桌面/RTOS 后端的语义一致，
 * 只是"由谁驱动派发"从 broker 线程换成了应用主循环。
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#ifndef NANOSIG_BAREMETAL_API_H
#define NANOSIG_BAREMETAL_API_H

#include <nanosig/nanosig_port.h>
#include <nanosig/nanosig_loop.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 执行一轮派发。
 *
 * 单次调用内循环至多 `NANOSIG_BAREMETAL_POLL_MAX_ROUNDS` 轮，每轮为：
 * 1. 派发所有已到期的 timer（其 slot 调用被推入目标 loop 的 ring）；
 * 2. drain 目标 loop 的 ring，调用记录中的 slot 回调。
 *
 * 因此**同一次调用内**即可观察到 timer 触发其 slot——第二步与第一步的顺序
 * 不可交换，否则 timer 回调会被推迟到下一次 poll。
 *
 * @warning 本函数**从不睡眠**。`max_wait_us` 只是"调用方打算睡多久"的上界声明，
 *          实际阻塞时间为 0。需要低功耗等待时请用 `ns_loop_next_timeout()`
 *          自行决定睡眠时长。
 *
 * @pre `loop` 必须是 `ns_loop_create()` 返回的裸机单例。
 * @pre 仅允许最外层应用主循环调用；同一 loop 的前一次调用返回前不得再次调用。
 *      slot 回调不得直接或间接重入本函数；库不检测重入，违反此前提的行为未定义。
 *
 * @param loop        裸机静态 loop 句柄。
 * @param max_wait_us 调用方的等待预算上界，单位微秒（仅用于约束内部补轮次数，
 *                    0 表示只跑最小轮次）。库不保证任何睡眠。
 *
 * @retval NS_OK          一轮派发完成。
 * @retval NS_E_INVAL     `loop` 为 `NULL`、不是裸机单例、或尚未创建。
 * @retval NS_E_SHUTDOWN  库尚未 `ns_init()` 或已 `ns_shutdown()`。
 * @retval <other>        timer 派发阶段返回的错误（例如 ring 满导致的
 *                        `NS_E_QUEUE_FULL`），**不会被吞掉**。
 */
extern int ns_loop_poll(ns_loop_t *loop, uint32_t max_wait_us);

/**
 * @brief 查询距离下一个 timer 到期还有多久。
 *
 * 供应用实现低功耗睡眠：`sleep(min(next_timeout, 你的其他唤醒源))`，醒来后
 * 再调 `ns_loop_poll()`。
 *
 * @param loop   裸机静态 loop 句柄。
 * @param out_us 输出剩余微秒数；无任何已装载 timer 时写入
 *               `NS_PLATFORM_WAIT_INFINITE_US`。
 *
 * @retval NS_OK          查询成功。
 * @retval NS_E_INVAL     `loop` 或 `out_us` 为 `NULL`，或 loop 尚未创建。
 * @retval NS_E_SHUTDOWN  库尚未初始化或已关闭。
 */
extern int ns_loop_next_timeout(ns_loop_t *loop, ns_platform_time_us_t *out_us);

#ifdef __cplusplus
}
#endif

#endif /* NANOSIG_BAREMETAL_API_H */
