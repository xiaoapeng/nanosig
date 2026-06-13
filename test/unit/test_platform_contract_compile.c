/**
 * @file test_platform_contract_compile.c
 * @brief P1a platform/port.h and nanosig_atomic.h syntax-only contract check.
 * @date 2026-05-16
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#include "platform/port.h"
#include <nanosig/nanosig_atomic.h>

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#if NS_PLATFORM_WAIT_INFINITE_US != UINT64_MAX
#error "NS_PLATFORM_WAIT_INFINITE_US must be UINT64_MAX"
#endif

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(sizeof(ns_platform_time_us_t) == sizeof(uint64_t), "platform time must be uint64_t microseconds");
#endif

static void platform_contract_accept_opaque_handles(void)
{
    ns_platform_tls_key_t *tls_key = NULL;
    ns_platform_wakeup_t *wakeup = NULL;
    ns_platform_mutex_t *mutex = NULL;
    ns_platform_time_us_t timeout_us = NS_PLATFORM_WAIT_INFINITE_US;
    ns_platform_wait_result_t wait_result = NS_PLATFORM_WAIT_TIMEOUT;

    (void)tls_key;
    (void)wakeup;
    (void)mutex;
    (void)timeout_us;
    (void)wait_result;
}

static void platform_contract_check_atomic_macros(void)
{
    atomic_int value;
    int expected;

    ns_atomic_init(&value, 1);
    ns_atomic_store_explicit(&value, 2, ns_memory_order_release);
    (void)ns_atomic_load_explicit(&value, ns_memory_order_acquire);
    (void)ns_atomic_exchange_explicit(&value, 3, ns_memory_order_acq_rel);

    expected = 3;
    (void)ns_atomic_compare_exchange_strong_explicit(
        &value,
        &expected,
        4,
        ns_memory_order_acq_rel,
        ns_memory_order_acquire);

    expected = 4;
    (void)ns_atomic_compare_exchange_weak_explicit(
        &value,
        &expected,
        5,
        ns_memory_order_acq_rel,
        ns_memory_order_acquire);

    expected = 5;
    (void)ns_atomic_compare_exchange_strong(&value, &expected, 6);

    expected = 6;
    (void)ns_atomic_compare_exchange_weak(&value, &expected, 7);

    (void)ns_atomic_fetch_add_explicit(&value, 1, ns_memory_order_acq_rel);
    (void)ns_atomic_fetch_sub_explicit(&value, 1, ns_memory_order_acq_rel);
    (void)ns_atomic_fetch_or_explicit(&value, 1, ns_memory_order_acq_rel);
    (void)ns_atomic_fetch_xor_explicit(&value, 1, ns_memory_order_acq_rel);
    (void)ns_atomic_fetch_and_explicit(&value, 1, ns_memory_order_acq_rel);

    ns_compiler_barrier();
    ns_memory_order_acquire_barrier();
    ns_memory_order_release_barrier();
    ns_memory_order_acq_rel_barrier();
    ns_memory_order_seq_cst_barrier();
}

static void platform_contract_check_waitset_types(void)
{
    ns_platform_waitable_t w;
    ns_platform_waitset_completion_t c;
    ns_platform_waitset_t *ws = NULL;

    w = ns_waitable_init();
    w.fd = 0;
    w.handle = NULL;
    w.event_bit = 0;
    w.user_data = NULL;
    w.events = NS_WAITABLE_EVENT_IN;
    w.edge_triggered = 0;

    c.waitable = &w;
    c.triggered_events = NS_WAITABLE_EVENT_IN | NS_WAITABLE_EVENT_OUT | NS_WAITABLE_EVENT_ERR;

    (void)ws;
    (void)NS_WAITABLE_EVENT_IN;
    (void)NS_WAITABLE_EVENT_OUT;
    (void)NS_WAITABLE_EVENT_ERR;
}

int main(void)
{
    platform_contract_accept_opaque_handles();
    platform_contract_check_atomic_macros();
    platform_contract_check_waitset_types();
    return 0;
}
