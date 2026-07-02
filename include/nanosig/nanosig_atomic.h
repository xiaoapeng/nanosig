/**
 * @file nanosig_atomic.h
 * @brief nanosig atomic 操作封装，兼容 C11 与 C++17。
 * @date 2026-05-23
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 *
 * C++ 没有 `<stdatomic.h>`，因此本头文件在 C++ 模式下使用 `<atomic>` +
 * `__atomic_*` 编译器内建函数实现等价语义。
 */

#ifndef NANOSIG_ATOMIC_H
#define NANOSIG_ATOMIC_H

#ifdef __cplusplus
#include <atomic>
namespace nanosig_detail { using std::atomic; using std::atomic_size_t; }
using nanosig_detail::atomic;
using nanosig_detail::atomic_size_t;
#else
#include <stdatomic.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief atomic memory order 名称。
 *
 * 该枚举只做 nanosig 命名收口，取值映射到 C11 `<stdatomic.h>` 的标准
 * `memory_order_*`（C++ 下为 `std::memory_order_*`）。调用方不应直接
 * 依赖编译器私有 `__ATOMIC_*` 常量。
 */
typedef enum ns_memory_order {
#ifdef __cplusplus
    ns_memory_order_relaxed = std::memory_order_relaxed,
    ns_memory_order_consume = std::memory_order_consume,
    ns_memory_order_acquire = std::memory_order_acquire,
    ns_memory_order_release = std::memory_order_release,
    ns_memory_order_acq_rel = std::memory_order_acq_rel,
    ns_memory_order_seq_cst = std::memory_order_seq_cst
#else
    ns_memory_order_relaxed = memory_order_relaxed,
    ns_memory_order_consume = memory_order_consume,
    ns_memory_order_acquire = memory_order_acquire,
    ns_memory_order_release = memory_order_release,
    ns_memory_order_acq_rel = memory_order_acq_rel,
    ns_memory_order_seq_cst = memory_order_seq_cst
#endif
} ns_memory_order_t;

/**
 * @brief 编译器屏障。
 */
#ifdef __cplusplus
#define ns_compiler_barrier() atomic_signal_fence(std::memory_order_seq_cst)
#else
#define ns_compiler_barrier() atomic_signal_fence(memory_order_seq_cst)
#endif

/**
 * @brief acquire 内存屏障。
 */
#ifdef __cplusplus
#define ns_memory_order_acquire_barrier() \
    atomic_thread_fence(std::memory_order_acquire)
#else
#define ns_memory_order_acquire_barrier() \
    atomic_thread_fence(memory_order_acquire)
#endif

/**
 * @brief release 内存屏障。
 */
#ifdef __cplusplus
#define ns_memory_order_release_barrier() \
    atomic_thread_fence(std::memory_order_release)
#else
#define ns_memory_order_release_barrier() \
    atomic_thread_fence(memory_order_release)
#endif

/**
 * @brief acquire-release 内存屏障。
 */
#ifdef __cplusplus
#define ns_memory_order_acq_rel_barrier() \
    atomic_thread_fence(std::memory_order_acq_rel)
#else
#define ns_memory_order_acq_rel_barrier() \
    atomic_thread_fence(memory_order_acq_rel)
#endif

/**
 * @brief sequentially-consistent 内存屏障。
 */
#ifdef __cplusplus
#define ns_memory_order_seq_cst_barrier() \
    atomic_thread_fence(std::memory_order_seq_cst)
#else
#define ns_memory_order_seq_cst_barrier() \
    atomic_thread_fence(memory_order_seq_cst)
#endif

/**
 * @brief atomic 操作宏。
 *
 * C 模式下直接调用 C11 `<stdatomic.h>` 标准函数；C++ 模式下使用
 * `std::atomic` 成员函数，将 `ns_memory_order_t` 枚举强制转换为
 * `std::memory_order`。
 */
#ifdef __cplusplus

/* C++ 下 .store(relaxed) 是原子写，C11 atomic_init 是非原子初始化。
   语义差异仅在对已活跃的原子对象重复调用时可见（C11 为 UB）；
   库代码仅在对象构造后调用一次 init，两者等价。 */
#define ns_atomic_init(ptr, value) \
    ((ptr)->store((value), std::memory_order_relaxed))
#define ns_atomic_store_explicit(ptr, value, order) \
    ((ptr)->store((value), static_cast<std::memory_order>(order)))
#define ns_atomic_load_explicit(ptr, order) \
    ((ptr)->load(static_cast<std::memory_order>(order)))
#define ns_atomic_exchange_explicit(ptr, value, order) \
    ((ptr)->exchange((value), static_cast<std::memory_order>(order)))
#define ns_atomic_compare_exchange_strong_explicit( \
    ptr, expected, desired, success, failure) \
    ((ptr)->compare_exchange_strong( \
        *(expected), (desired), \
        static_cast<std::memory_order>(success), \
        static_cast<std::memory_order>(failure)))
#define ns_atomic_compare_exchange_strong(ptr, expected, desired) \
    ((ptr)->compare_exchange_strong( \
        *(expected), (desired), \
        std::memory_order_seq_cst, std::memory_order_seq_cst))
#define ns_atomic_compare_exchange_weak_explicit( \
    ptr, expected, desired, success, failure) \
    ((ptr)->compare_exchange_weak( \
        *(expected), (desired), \
        static_cast<std::memory_order>(success), \
        static_cast<std::memory_order>(failure)))
#define ns_atomic_compare_exchange_weak(ptr, expected, desired) \
    ((ptr)->compare_exchange_weak( \
        *(expected), (desired), \
        std::memory_order_seq_cst, std::memory_order_seq_cst))
#define ns_atomic_fetch_add_explicit(ptr, value, order) \
    ((ptr)->fetch_add((value), static_cast<std::memory_order>(order)))
#define ns_atomic_fetch_sub_explicit(ptr, value, order) \
    ((ptr)->fetch_sub((value), static_cast<std::memory_order>(order)))
#define ns_atomic_fetch_or_explicit(ptr, value, order) \
    ((ptr)->fetch_or((value), static_cast<std::memory_order>(order)))
#define ns_atomic_fetch_xor_explicit(ptr, value, order) \
    ((ptr)->fetch_xor((value), static_cast<std::memory_order>(order)))
#define ns_atomic_fetch_and_explicit(ptr, value, order) \
    ((ptr)->fetch_and((value), static_cast<std::memory_order>(order)))

#else /* C */

#define ns_atomic_init(ptr, value) atomic_init((ptr), (value))
#define ns_atomic_store_explicit(ptr, value, order) \
    atomic_store_explicit((ptr), (value), (memory_order)(order))
#define ns_atomic_load_explicit(ptr, order) \
    atomic_load_explicit((ptr), (memory_order)(order))
#define ns_atomic_exchange_explicit(ptr, value, order) \
    atomic_exchange_explicit((ptr), (value), (memory_order)(order))
#define ns_atomic_compare_exchange_strong_explicit( \
    ptr, expected, desired, success, failure) \
    atomic_compare_exchange_strong_explicit( \
        (ptr), (expected), (desired), \
        (memory_order)(success), (memory_order)(failure))
#define ns_atomic_compare_exchange_strong(ptr, expected, desired) \
    atomic_compare_exchange_strong((ptr), (expected), (desired))
#define ns_atomic_compare_exchange_weak_explicit( \
    ptr, expected, desired, success, failure) \
    atomic_compare_exchange_weak_explicit( \
        (ptr), (expected), (desired), \
        (memory_order)(success), (memory_order)(failure))
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

#endif /* __cplusplus */

#ifdef __cplusplus
}
#endif

#endif /* NANOSIG_ATOMIC_H */
