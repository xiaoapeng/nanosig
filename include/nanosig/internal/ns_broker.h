/**
 * @file ns_broker.h
 * @brief nanosig internal event broker lifecycle API.
 * @date 2026-06-14
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#ifndef NANOSIG_NS_BROKER_H
#define NANOSIG_NS_BROKER_H

#ifdef __cplusplus
extern "C" {
#endif

int ns_broker_global_init(void);
void ns_broker_global_shutdown(void);

#ifdef NANOSIG_TEST
/* Test hook: set to non-NS_OK before ns_init() to inject waitset_wait failure.
 * After one injection it resets to NS_OK so the broker can recover. */
extern volatile int g_ns_test_waitset_wait_result;
#endif

#ifdef __cplusplus
}
#endif

#endif /* NANOSIG_NS_BROKER_H */
