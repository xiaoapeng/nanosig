/**
 * @file nanosig_status.h
 * @brief nanosig 公开状态码。
 * @date 2026-05-17
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#ifndef NANOSIG_STATUS_H
#define NANOSIG_STATUS_H

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
    NS_E_NO_LOOP = -7,
    NS_E_EMPTY = -8,
    NS_E_CORRUPT = -9,
    NS_E_NO_TIMER = -10,
    NS_E_BUSY = -11
} ns_status_t;

#endif /* NANOSIG_STATUS_H */
