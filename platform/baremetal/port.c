/**
 * @file port.c
 * @brief nanosig 平台层实现 — 裸机后端（能力子集）。
 * @date 2026-10-31
 *
 * 本文件**只定义三个** `ns_platform_*` 符号：
 * - `ns_platform_init`
 * - `ns_platform_shutdown`
 * - `ns_platform_clock_monotonic_us`
 *
 * 其余平台原语（alloc/free、mutex_*、wakeup_*、event_*、waitset_*、thread_*）
 * 有意**不定义**：引用它们的程序在链接期失败，这是可诊断的"该能力在本后端
 * 不存在"信号，而不是静默成功的空桩。
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#include <nanosig/nanosig_port.h>

/* ------------------------------------------------------------------ */
/*  时钟注入                                                            */
/* ------------------------------------------------------------------ */

static ns_baremetal_now_us_fn g_ns_baremetal_now_us = NULL;

void ns_baremetal_set_now_us(ns_baremetal_now_us_fn fn)
{
    g_ns_baremetal_now_us = fn;
}

/* ------------------------------------------------------------------ */
/*  ABI 探针                                                            */
/* ------------------------------------------------------------------ */

const int ns_nanosig_abi_baremetal = 0;

/* ------------------------------------------------------------------ */
/*  生命周期                                                            */
/* ------------------------------------------------------------------ */

int ns_platform_init(void)
{
    return (g_ns_baremetal_now_us == NULL) ? NS_E_INVAL : NS_OK;
}

int ns_platform_shutdown(void)
{
    return NS_OK;
}

/* ------------------------------------------------------------------ */
/*  时间                                                                */
/* ------------------------------------------------------------------ */

int ns_platform_clock_monotonic_us(ns_platform_time_us_t *out_now_us)
{
    if(out_now_us == NULL) return NS_E_INVAL;

    /* 直接透传应用时钟：无 tick 换算、无回绕扩展。
       单调性与 64 位不回绕由 ns_baremetal_now_us_fn 的契约保证。 */
    *out_now_us = g_ns_baremetal_now_us();
    return NS_OK;
}
