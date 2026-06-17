/**
 * @file ns_timer_mgr.h
 * @brief nanosig internal timer manager API.
 * @date 2026-06-13
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#ifndef NANOSIG_NS_TIMER_MGR_H
#define NANOSIG_NS_TIMER_MGR_H

#include <platform/port.h>
#include <nanosig/nanosig_status.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ns_timer_notify_fn)(void *ctx);

int ns_timer_mgr_global_init(ns_timer_notify_fn notify, void *ctx);
void ns_timer_mgr_global_shutdown(void);

int ns_timer_mgr_next_timeout(ns_platform_time_us_t *out_timeout_us);
int ns_timer_mgr_fire_expired(void);

#ifdef __cplusplus
}
#endif

#endif /* NANOSIG_NS_TIMER_MGR_H */
