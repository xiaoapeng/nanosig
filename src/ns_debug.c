/**
 * @file ns_debug.c
 * @brief nanosig 调试输出实现 — 移植自 eventhub_os eh_debug.c。
 * @date 2026-07-11
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#include <stdarg.h>
#include <stdint.h>

#include <nanosig/nanosig_port.h>
#include <nanosig/ns_formatio.h>
#include <nanosig/ns_debug.h>

/* ================================================================== */
/*  全局调试等级 & 互斥锁                                               */
/* ================================================================== */

#ifndef NS_CONFIG_DEFAULT_DEBUG_LEVEL
static enum ns_dbg_level dbg_level = NS_DBG_DEBUG;
#else
static enum ns_dbg_level dbg_level = NS_CONFIG_DEFAULT_DEBUG_LEVEL;
#endif

static ns_platform_mutex_t *ns_dbg_mutex = NULL;

int ns_debug_init(void)
{
    if (ns_dbg_mutex != NULL) return NS_E_EXISTS;
    return ns_platform_mutex_create(&ns_dbg_mutex, "nanosig-debug");
}

void ns_debug_shutdown(void)
{
    if (ns_dbg_mutex != NULL) {
        (void)ns_platform_mutex_destroy(ns_dbg_mutex);
        ns_dbg_mutex = NULL;
    }
}

static const char *ns_dbg_level_str[] = {
    [0]                   = "UNDEF",
    [NS_DBG_ERR]          = "ERR",
    [NS_DBG_WARNING]      = "WARN",
    [NS_DBG_SYS]          = "SYS",
    [NS_DBG_INFO]         = "INFO",
    [NS_DBG_DEBUG]        = "DBG",
};

/* ================================================================== */
/*  公开 API                                                             */
/* ================================================================== */

int ns_dbg_set_level(enum ns_dbg_level level)
{
    dbg_level = level;
    return 0;
}

static int ns_dbg_vprintf(enum ns_dbg_level level,
    enum ns_dbg_flags flags, const char *fmt, va_list args)
{
    int n = 0;

    if (flags & NS_DBG_FLAGS_WALL_CLOCK) {
        /* 墙上时间输出（预留） */
    }
    if (flags & NS_DBG_FLAGS_MONOTONIC_CLOCK) {
        ns_platform_time_us_t now_us = 0;
        (void)ns_platform_clock_monotonic_us(&now_us);
        n += ns_printf("[%5u.%06u] ",
            (unsigned int)(now_us / 1000000u),
            (unsigned int)(now_us % 1000000u));
    }
    if ((flags & NS_DBG_FLAGS_DEBUG_TAG) &&
        level >= NS_DBG_ERR && level <= NS_DBG_DEBUG) {
        n += ns_printf("[%4s] ", ns_dbg_level_str[level]);
    }
    n += ns_vprintf(fmt, args);
    return n;
}

int ns_dbg_raw(enum ns_dbg_level level,
    enum ns_dbg_flags flags, const char *fmt, ...)
{
    int n = 0;
    va_list args;

    if (level > dbg_level)
        return 0;
    if (ns_dbg_mutex)
        (void)ns_platform_mutex_lock(ns_dbg_mutex);
    va_start(args, fmt);
    n = ns_dbg_vprintf(level, flags, fmt, args);
    va_end(args);
    if (ns_dbg_mutex)
        (void)ns_platform_mutex_unlock(ns_dbg_mutex);
    return n;
}

int ns_vdbg_raw(enum ns_dbg_level level,
    enum ns_dbg_flags flags, const char *fmt, va_list args)
{
    int n = 0;

    if (level > dbg_level)
        return 0;
    if (ns_dbg_mutex)
        (void)ns_platform_mutex_lock(ns_dbg_mutex);
    n = ns_dbg_vprintf(level, flags, fmt, args);
    if (ns_dbg_mutex)
        (void)ns_platform_mutex_unlock(ns_dbg_mutex);
    return n;
}

int ns_dbg_hex(enum ns_dbg_level level,
    enum ns_dbg_flags flags, size_t len, const void *buf)
{
    const uint8_t *pos = (const uint8_t *)buf;
    int n = 0;
    size_t y_n, x_n;

    /* Level gate and mutex are handled inside each ns_dbg_raw call.
     * Do NOT lock here — ns_dbg_raw re-enters the lock and deadlocks. */

    y_n = len / 16;
    x_n = len % 16;

    n += ns_dbg_raw(level, flags,
        "______________________________________________________________"
        NS_DEBUG_ENTER_SIGN);
    n += ns_dbg_raw(level, flags,
        "            | 0| 1| 2| 3| 4| 5| 6| 7| 8| 9| A| B| C| D| E| F||"
        NS_DEBUG_ENTER_SIGN);
    n += ns_dbg_raw(level, flags,
        "--------------------------------------------------------------"
        NS_DEBUG_ENTER_SIGN);

    for (size_t i = 0; i < y_n; i++, pos += 16) {
        n += ns_dbg_raw(level, flags,
            "|0x%08x| %-47.*hhq||" NS_DEBUG_ENTER_SIGN,
            (unsigned int)(i * 16), 16, pos);
    }
    if (x_n) {
        n += ns_dbg_raw(level, flags,
            "|0x%08x| %-47.*hhq||" NS_DEBUG_ENTER_SIGN,
            (unsigned int)(y_n * 16), (int)x_n, pos);
    }
    n += ns_dbg_raw(level, flags,
        "--------------------------------------------------------------"
        NS_DEBUG_ENTER_SIGN);
    return n;
}
