/**
 * @file test_cpp_compat_compile.cpp
 * @brief Compile-only check: every public nanosig header must be includable from C++.
 *
 * This file is compiled with a C++ compiler in syntax-only mode.
 * It verifies that `extern "C"` guards and other preprocessor logic
 * do not break when the headers are processed by a C++ frontend.
 *
 * Coverage (audit findings):
 *  - All 17 public headers + umbrella header include correctly
 *  - NS_CONTAINER_OF_CONST: const-awareness via C++ template
 *  - NS_SIGNAL_PAYLOAD_SIZE / NS_SIGNAL_PAYLOAD_PTR_SIZE: no-payload returns 0
 *  - ns_atomic_* macros: C++ __atomic_* builtins
 *  - Barrier macros: std::memory_order_* path
 *  - List traversal macros: __typeof__ in C++ mode
 *  - NS_CONTAINER_OF_SAFE, NS_MEMBER_ADDRESS_IS_NONNULL, ns_read_once
 *  - NS_LOOP_CONFIG_DEFAULT: aggregate init (no compound literal)
 *  - ns_memory_order_t: std::memory_order → unscoped enum
 *  - NS_STATIC_ASSERT: active in C++11+ mode
 */

#include <cstddef>
#include <type_traits>

/* ── Individual headers ────────────────────────────────────────── */
#include <nanosig/nanosig_status.h>
#include <nanosig/nanosig_types.h>
#include <nanosig/nanosig_safety.h>
#include <nanosig/nanosig_atomic.h>
#include <nanosig/nanosig_list.h>
#include <nanosig/nanosig_slist.h>
#include <nanosig/nanosig_rbtree.h>
#include <nanosig/nanosig_hashtable.h>
#include <nanosig/nanosig_ringbuf.h>
#include <nanosig/nanosig_mpsc_record_ring.h>
#include <nanosig/nanosig_signal.h>
#include <nanosig/nanosig_loop.h>
#include <nanosig/nanosig_timer.h>
#include <nanosig/nanosig_broker.h>
#include <nanosig/nanosig_port.h>
#include <nanosig/nanosig_ds.h>

/* ── Umbrella header (includes everything + lifecycle API) ─────── */
#include <nanosig/nanosig.h>

/* ── Test: NS_CONTAINER_OF_CONST ──────────────────────────────── */

struct cpp_test_owner {
    int prefix;
    ns_list_node_t node;
    int suffix;
};

static void cpp_test_container_of_const(void)
{
    cpp_test_owner owner = {0, {}, 0};
    const cpp_test_owner const_owner = {0, {}, 0};

    cpp_test_owner *p = NS_CONTAINER_OF_CONST(&owner.node, cpp_test_owner, node);
    const cpp_test_owner *cp = NS_CONTAINER_OF_CONST(&const_owner.node, cpp_test_owner, node);

    static_assert(
        std::is_same<decltype(p), cpp_test_owner *>::value,
        "NS_CONTAINER_OF_CONST must return non-const for non-const input");
    static_assert(
        std::is_same<decltype(cp), const cpp_test_owner *>::value,
        "NS_CONTAINER_OF_CONST must return const for const input");

    (void)p;
    (void)cp;
}

/* ── Test: NS_SIGNAL_PAYLOAD_SIZE / NS_SIGNAL_PAYLOAD_PTR_SIZE ── */

static void cpp_test_payload_macros(void)
{
    /* ns_no_payload_t must yield size 0 */
    static_assert(NS_SIGNAL_PAYLOAD_SIZE(ns_no_payload_t) == 0u,
        "NS_SIGNAL_PAYLOAD_SIZE(ns_no_payload_t) must be 0 in C++");
    static_assert(NS_SIGNAL_PAYLOAD_SIZE(int) == sizeof(int),
        "NS_SIGNAL_PAYLOAD_SIZE(int) must equal sizeof(int)");

    /* NS_NO_PAYLOAD pointer must yield size 0 */
    static_assert(NS_SIGNAL_PAYLOAD_PTR_SIZE(NS_NO_PAYLOAD) == 0u,
        "NS_SIGNAL_PAYLOAD_PTR_SIZE(NS_NO_PAYLOAD) must be 0 in C++");

    /* Regular pointer yields sizeof(*ptr) */
    const int dummy = 0;
    static_assert(NS_SIGNAL_PAYLOAD_PTR_SIZE(&dummy) == sizeof(int),
        "NS_SIGNAL_PAYLOAD_PTR_SIZE(&int) must equal sizeof(int)");
}

/* ── Test: ns_atomic_* macros ─────────────────────────────────── */

static void cpp_test_atomic_ops(void)
{
    atomic_size_t counter;
    ns_atomic_init(&counter, 0u);

    ns_atomic_store_explicit(&counter, 42u, ns_memory_order_release);
    size_t val = ns_atomic_load_explicit(&counter, ns_memory_order_acquire);
    (void)val;

    size_t prev = ns_atomic_fetch_add_explicit(&counter, 1u, ns_memory_order_relaxed);
    (void)prev;
    prev = ns_atomic_fetch_sub_explicit(&counter, 1u, ns_memory_order_relaxed);
    (void)prev;
    prev = ns_atomic_fetch_or_explicit(&counter, 0xFu, ns_memory_order_relaxed);
    (void)prev;
    prev = ns_atomic_fetch_and_explicit(&counter, 0x3u, ns_memory_order_relaxed);
    (void)prev;
    prev = ns_atomic_fetch_xor_explicit(&counter, 0x5u, ns_memory_order_relaxed);
    (void)prev;

    prev = ns_atomic_exchange_explicit(&counter, 0u, ns_memory_order_seq_cst);
    (void)prev;

    size_t expected = 0u;
    (void)ns_atomic_compare_exchange_strong_explicit(
        &counter, &expected, 1u, ns_memory_order_seq_cst, ns_memory_order_relaxed);
    expected = 0u;
    (void)ns_atomic_compare_exchange_weak_explicit(
        &counter, &expected, 2u, ns_memory_order_seq_cst, ns_memory_order_relaxed);
    expected = 2u;
    (void)ns_atomic_compare_exchange_strong(&counter, &expected, 3u);
    expected = 3u;
    (void)ns_atomic_compare_exchange_weak(&counter, &expected, 4u);
}

/* ── Test: barrier macros ─────────────────────────────────────── */

static void cpp_test_barriers(void)
{
    ns_compiler_barrier();
    ns_memory_order_acquire_barrier();
    ns_memory_order_release_barrier();
    ns_memory_order_acq_rel_barrier();
    ns_memory_order_seq_cst_barrier();
}

/* ── Test: list traversal macros (typeof/__typeof__) ──────────── */

struct cpp_test_list_item {
    int data;
    ns_list_node_t node;
};

static void cpp_test_list_entry_traversal(void)
{
    ns_list_node_t head;
    ns_list_init(&head);

    cpp_test_list_item items[3] = {{1, {}}, {2, {}}, {3, {}}};
    for(int i = 0; i < 3; ++i)
        ns_list_push_back(&head, &items[i].node);

    cpp_test_list_item *pos;
    ns_list_for_each_entry(pos, &head, node){
        (void)pos->data;
    }

    cpp_test_list_item *n;
    ns_list_for_each_entry_safe(pos, n, &head, node){
        (void)pos->data;
    }

    ns_list_for_each_prev_entry(pos, &head, node){
        (void)pos->data;
    }

    ns_list_for_each_prev_entry_safe(pos, n, &head, node){
        (void)pos->data;
    }
}

/* ── Test: __typeof__ safety macros ───────────────────────────── */

static void cpp_test_typeof_safety_macros(void)
{
    cpp_test_owner owner = {1, {}, 3};

    cpp_test_owner *safe = NS_CONTAINER_OF_SAFE(&owner.node, cpp_test_owner, node);
    (void)safe;

    ns_list_node_t *null_node = nullptr;
    cpp_test_owner *null_result = NS_CONTAINER_OF_SAFE(null_node, cpp_test_owner, node);
    (void)null_result;

    bool nonnull = NS_MEMBER_ADDRESS_IS_NONNULL(&owner, node);
    (void)nonnull;

    int v = 42;
    int rd = ns_read_once(v);
    (void)rd;
}

/* ── Test: NS_LOOP_CONFIG_DEFAULT ─────────────────────────────── */

static void cpp_test_loop_config_default(void)
{
    ns_loop_config_t cfg = NS_LOOP_CONFIG_DEFAULT();
    (void)cfg.queue_byte_capacity;
    (void)cfg.flags;
    (void)cfg.debug_name;
}

/* ── Test: ns_memory_order_t enum ─────────────────────────────── */

static void cpp_test_memory_order_enum(void)
{
    static_assert(static_cast<int>(ns_memory_order_relaxed) >= 0,
        "ns_memory_order_relaxed must be non-negative");
    static_assert(static_cast<int>(ns_memory_order_seq_cst) >=
                  static_cast<int>(ns_memory_order_relaxed),
        "ns_memory_order_seq_cst >= ns_memory_order_relaxed");

    atomic_size_t v;
    ns_atomic_init(&v, 0u);
    (void)ns_atomic_load_explicit(&v, ns_memory_order_acquire);
    ns_atomic_store_explicit(&v, 1u, ns_memory_order_release);
}

/* ── Test: NS_STATIC_ASSERT ───────────────────────────────────── */

static void cpp_test_static_assert_macro(void)
{
    NS_STATIC_ASSERT(sizeof(int) >= sizeof(char),
        "int must be at least as large as char");
    NS_STATIC_ASSERT(true, "NS_STATIC_ASSERT must work in C++ mode");
}

/* ── Force instantiation of all test functions ────────────────── */

static void cpp_test_all(void)
{
    cpp_test_container_of_const();
    cpp_test_payload_macros();
    cpp_test_atomic_ops();
    cpp_test_barriers();
    cpp_test_list_entry_traversal();
    cpp_test_typeof_safety_macros();
    cpp_test_loop_config_default();
    cpp_test_memory_order_enum();
    cpp_test_static_assert_macro();
}
