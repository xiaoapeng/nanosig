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

#ifdef __cplusplus
}
#endif

#endif /* NANOSIG_NS_BROKER_H */
