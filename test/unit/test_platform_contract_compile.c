/**
 * @file test_platform_contract_compile.c
 * @brief nanosig_port.h and nanosig_atomic.h syntax-only contract check.
 * @date 2026-05-16
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#include <nanosig/nanosig_port.h>
#include <nanosig/nanosig_atomic.h>

#ifndef NANOSIG_PLATFORM_HEADER
#error "NANOSIG_PLATFORM_HEADER must be injected by the build system (platform selection)"
#endif

#if NS_PLATFORM_WAIT_INFINITE_US != UINT64_MAX
#error "NS_PLATFORM_WAIT_INFINITE_US must be UINT64_MAX"
#endif

#ifndef NS_PLATFORM_WAIT_EMPTY_MAX_US
#error "NS_PLATFORM_WAIT_EMPTY_MAX_US must be defined by nanosig_port.h"
#endif

#if NS_PLATFORM_WAIT_EMPTY_MAX_US == 0u
#error "NS_PLATFORM_WAIT_EMPTY_MAX_US must be > 0"
#endif

#if NS_PLATFORM_WAIT_EMPTY_MAX_US >= NS_PLATFORM_WAIT_INFINITE_US
#error "NS_PLATFORM_WAIT_EMPTY_MAX_US must stay below NS_PLATFORM_WAIT_INFINITE_US"
#endif

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(sizeof(ns_platform_time_us_t) == sizeof(uint64_t), "platform time must be uint64_t microseconds");
#endif

/* 平台对象一律由调用方拥有存储：本 TU 只做语法检查（-fsyntax-only），
   因此声明真实存储并调用 init/deinit 就能验证公共契约的类型完整性。 */
static void platform_contract_accept_caller_storage(void)
{
    ns_platform_wakeup_t wakeup;
    ns_platform_mutex_t mutex;
    ns_platform_thread_t thread;
    ns_platform_waitset_t waitset;
    ns_platform_event_t event;
    ns_platform_thread_fn thread_fn = NULL;
    ns_platform_time_us_t timeout_us = NS_PLATFORM_WAIT_INFINITE_US;
    ns_platform_wait_result_t wait_result = NS_PLATFORM_WAIT_TIMEOUT;
    int rc;

    rc = ns_platform_wakeup_init(&wakeup, "contract");
    rc |= ns_platform_wakeup_signal(&wakeup);
    rc |= ns_platform_wakeup_wait(&wakeup, timeout_us, &wait_result);
    rc |= ns_platform_wakeup_deinit(&wakeup);

    rc |= ns_platform_mutex_init(&mutex, "contract");
    rc |= ns_platform_mutex_lock(&mutex);
    rc |= ns_platform_mutex_unlock(&mutex);
    rc |= ns_platform_mutex_deinit(&mutex);

    rc |= ns_platform_thread_init(&thread, thread_fn, NULL, "contract");
    rc |= ns_platform_thread_join(&thread);

    rc |= ns_platform_waitset_init(&waitset);
    rc |= ns_platform_waitset_deinit(&waitset);

    rc |= ns_platform_event_init(&event, "contract");
    rc |= ns_platform_event_signal(&event);
    rc |= ns_platform_event_drain(&event);
    rc |= ns_platform_event_deinit(&event);

    (void)rc;
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

    ns_waitable_init(&w);
    w.primitive = (ns_waitable_handle_t)0;
    w.user_data = NULL;
    w.registered_waitset = NULL;
    w.events = NS_WAITABLE_EVENT_IN;
    w.edge_triggered = 0;

    c.waitable = &w;
    c.triggered_events = NS_WAITABLE_EVENT_IN | NS_WAITABLE_EVENT_OUT | NS_WAITABLE_EVENT_ERR;

    (void)c;
    (void)ns_waitable_handle_is_valid(w.primitive);
    (void)NS_WAITABLE_EVENT_IN;
    (void)NS_WAITABLE_EVENT_OUT;
    (void)NS_WAITABLE_EVENT_ERR;
}

int main(void)
{
    platform_contract_accept_caller_storage();
    platform_contract_check_atomic_macros();
    platform_contract_check_waitset_types();
    (void)NS_PLATFORM_ABI_PROBE;
    return 0;
}
