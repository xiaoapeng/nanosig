/**
 * @file nanosig_atomic.h
 * @brief nanosig C11 atomic 操作封装。
 * @date 2026-05-23
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#ifndef NANOSIG_ATOMIC_H
#define NANOSIG_ATOMIC_H

#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief atomic memory order 名称。
 *
 * 该枚举只做 nanosig 命名收口，取值映射到 C11 `<stdatomic.h>` 的标准
 * `memory_order_*`。调用方不应直接依赖编译器私有 `__ATOMIC_*` 常量。
 */
typedef enum ns_memory_order {
    ns_memory_order_relaxed = memory_order_relaxed,
    ns_memory_order_consume = memory_order_consume,
    ns_memory_order_acquire = memory_order_acquire,
    ns_memory_order_release = memory_order_release,
    ns_memory_order_acq_rel = memory_order_acq_rel,
    ns_memory_order_seq_cst = memory_order_seq_cst
} ns_memory_order_t;

/**
 * @brief 编译器屏障。
 */
#define ns_compiler_barrier() atomic_signal_fence(memory_order_seq_cst)

/**
 * @brief acquire 内存屏障。
 */
#define ns_memory_order_acquire_barrier() atomic_thread_fence(memory_order_acquire)

/**
 * @brief release 内存屏障。
 */
#define ns_memory_order_release_barrier() atomic_thread_fence(memory_order_release)

/**
 * @brief acquire-release 内存屏障。
 */
#define ns_memory_order_acq_rel_barrier() atomic_thread_fence(memory_order_acq_rel)

/**
 * @brief sequentially-consistent 内存屏障。
 */
#define ns_memory_order_seq_cst_barrier() atomic_thread_fence(memory_order_seq_cst)

#define ns_atomic_init(ptr, value) atomic_init((ptr), (value))
#define ns_atomic_store_explicit(ptr, value, order) \
    atomic_store_explicit((ptr), (value), (memory_order)(order))
#define ns_atomic_load_explicit(ptr, order) \
    atomic_load_explicit((ptr), (memory_order)(order))
#define ns_atomic_exchange_explicit(ptr, value, order) \
    atomic_exchange_explicit((ptr), (value), (memory_order)(order))
#define ns_atomic_compare_exchange_strong_explicit(ptr, expected, desired, success, failure) \
    atomic_compare_exchange_strong_explicit( \
        (ptr), \
        (expected), \
        (desired), \
        (memory_order)(success), \
        (memory_order)(failure))
#define ns_atomic_compare_exchange_strong(ptr, expected, desired) \
    atomic_compare_exchange_strong((ptr), (expected), (desired))
#define ns_atomic_compare_exchange_weak_explicit(ptr, expected, desired, success, failure) \
    atomic_compare_exchange_weak_explicit( \
        (ptr), \
        (expected), \
        (desired), \
        (memory_order)(success), \
        (memory_order)(failure))
#define ns_atomic_compare_exchange_weak(ptr, expected, desired) \
    atomic_compare_exchange_weak((ptr), (expected), (desired))
#define ns_atomic_fetch_add_explicit(ptr, value, order) \
    atomic_fetch_add_explicit((ptr), (value), (memory_order)(order))
#define ns_atomic_fetch_sub_explicit(ptr, value, order) \
    atomic_fetch_sub_explicit((ptr), (value), (memory_order)(order))
#define ns_atomic_fetch_or_explicit(ptr, value, order) \
    atomic_fetch_or_explicit((ptr), (value), (memory_order)(order))
#define ns_atomic_fetch_xor_explicit(ptr, value, order) \
    atomic_fetch_xor_explicit((ptr), (value), (memory_order)(order))
#define ns_atomic_fetch_and_explicit(ptr, value, order) \
    atomic_fetch_and_explicit((ptr), (value), (memory_order)(order))

#ifdef __cplusplus
}
#endif

#endif /* NANOSIG_ATOMIC_H */
