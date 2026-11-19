/**
 * @file test_mpsc_record_ring_preempt.c
 * @brief 预留保护边界与高优先级生产者交错的确定性回归。
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <nanosig/nanosig_port.h>

#define ns_mpsc_record_ring_init test_ring_init
#define ns_mpsc_record_ring_capacity test_ring_capacity
#define ns_mpsc_record_ring_free_capacity test_ring_free_capacity
#define ns_mpsc_record_ring_max_record_size test_ring_max_record_size
#define ns_mpsc_record_ring_try_push test_ring_try_push
#define ns_mpsc_record_ring_try_pushv test_ring_try_pushv
#define ns_mpsc_record_ring_try_acquire test_ring_try_acquire
#define ns_mpsc_record_ring_release test_ring_release

#include <nanosig/nanosig_mpsc_record_ring.h>

typedef unsigned int test_preempt_state_t;
static test_preempt_state_t test_preempt_enter(void);
static void test_preempt_leave(test_preempt_state_t state);
static bool test_reserve_cas(atomic_size_t *ptr, size_t *expected,
    size_t desired, ns_memory_order_t success, ns_memory_order_t failure);
static void *test_payload_copy(void *destination, const void *source, size_t size);

/* 只拦截测试副本，生产实现不增加调度 hook。 */
#define ns_platform_preempt_state_t test_preempt_state_t
#define ns_platform_preempt_enter test_preempt_enter
#define ns_platform_preempt_leave test_preempt_leave
#undef ns_atomic_compare_exchange_weak_explicit
#define ns_atomic_compare_exchange_weak_explicit test_reserve_cas
#undef memcpy
#define memcpy test_payload_copy
#include "src/ds/ns_mpsc_record_ring.c"
#undef memcpy

static unsigned int guard_depth;
static unsigned int caller_depth;
static unsigned int enter_count;
static unsigned int leave_count;
static unsigned int reserve_count;
static unsigned int copy_count;
static unsigned int deferred_count;
static unsigned int resumed_count;
static int failures;
static bool request_preemption;
static bool pending_producer;
static ns_mpsc_record_ring_t ring;
static size_t storage[128u / sizeof(size_t)];
static const unsigned char high_payload[] = { 0x48, 0x49 };

static void expect(bool condition, const char *message)
{
    if(!condition){
        fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

static test_preempt_state_t test_preempt_enter(void)
{
    test_preempt_state_t previous = guard_depth;
    ++guard_depth;
    ++enter_count;
    return previous;
}

static void test_preempt_leave(test_preempt_state_t state)
{
    expect(guard_depth == state + 1u, "guard exits in reverse nesting order");
    expect((ns_atomic_load_explicit(&ring.write_pos, ns_memory_order_relaxed) & 1u) == 0u,
           "busy cleared before restoring preemption");
    guard_depth = state;
    ++leave_count;
}

static bool test_reserve_cas(atomic_size_t *ptr, size_t *expected,
    size_t desired, ns_memory_order_t success, ns_memory_order_t failure)
{
    bool acquired = atomic_compare_exchange_strong_explicit(ptr, expected, desired,
        (memory_order)success, (memory_order)failure);

    if(acquired){
        ++reserve_count;
        expect(guard_depth == caller_depth + 1u, "busy owner cannot be preempted");
        if(request_preemption){
            request_preemption = false;
            /* 无保护时直接报告失败，不真的递归入队并永久自旋。 */
            if(guard_depth == 0u){
                expect(false, "high-priority producer could preempt busy owner");
            }else{
                pending_producer = true;
                ++deferred_count;
            }
        }
    }
    return acquired;
}

static void *test_payload_copy(void *destination, const void *source, size_t size)
{
    ++copy_count;
    expect(guard_depth == caller_depth, "payload copied after restoring caller state");
    expect((ns_atomic_load_explicit(&ring.write_pos, ns_memory_order_relaxed) & 1u) == 0u,
           "payload copied after releasing busy");
    if(pending_producer && guard_depth == 0u){
        void *record = NULL;
        size_t record_size = 0u;

        pending_producer = false;
        ++resumed_count;
        /* 模拟解除保护后 H 抢占 L；L 的 payload 尚未复制。 */
        expect(test_ring_try_push(&ring, high_payload, sizeof(high_payload)) == NS_OK,
               "high-priority producer completes while low payload is pending");
        expect(test_ring_try_acquire(&ring, &record, &record_size) == NS_E_EMPTY,
               "consumer does not skip uncommitted low-priority record");
    }
    return memcpy(destination, source, size);
}

static void reset_ring(void)
{
    expect(guard_depth == 0u, "previous operation restored preemption");
    caller_depth = 0u;
    enter_count = 0u;
    leave_count = 0u;
    reserve_count = 0u;
    copy_count = 0u;
    deferred_count = 0u;
    resumed_count = 0u;
    request_preemption = false;
    pending_producer = false;
    expect(test_ring_init(&ring, storage, NS_CAPACITY_128) == NS_OK, "initialize ring");
}

static void expect_record(const void *expected, size_t expected_size)
{
    void *record = NULL;
    size_t size = 0u;
    int rc = test_ring_try_acquire(&ring, &record, &size);

    expect(rc == NS_OK, "record available");
    if(rc != NS_OK) return;
    expect(size == expected_size, "record payload size");
    if(size == expected_size) expect(memcmp(record, expected, size) == 0, "record payload bytes");
    expect(test_ring_release(&ring, record) == NS_OK, "release record");
}

static void test_deferred_producer(void)
{
    const unsigned char low_payload[] = { 0x4c, 0x4f };

    reset_ring();
    request_preemption = true;
    expect(test_ring_try_push(&ring, low_payload, sizeof(low_payload)) == NS_OK,
           "low-priority producer succeeds");
    expect(deferred_count == 1u && resumed_count == 1u, "preemption deferred until payload copy");
    expect(enter_count == 2u && leave_count == 2u && reserve_count == 2u,
           "both producers balance reservation protection");
    expect(copy_count == 2u && guard_depth == 0u, "both payload copies outside protection");
    expect_record(low_payload, sizeof(low_payload));
    expect_record(high_payload, sizeof(high_payload));
}

static void test_full_and_nested(void)
{
    unsigned char payload[64u - sizeof(size_t)];
    test_preempt_state_t outer;

    reset_ring();
    memset(payload, 0x42, sizeof(payload));
    expect(test_ring_try_push(&ring, payload, sizeof(payload)) == NS_OK, "fill first half");
    expect(test_ring_try_push(&ring, payload, sizeof(payload)) == NS_OK, "fill second half");
    outer = test_preempt_enter();
    caller_depth = guard_depth;
    expect(test_ring_try_push(&ring, payload, sizeof(payload)) == NS_E_QUEUE_FULL,
           "full ring fails without waiting");
    expect(guard_depth == caller_depth, "queue-full exit preserves outer protection");
    expect(enter_count == 4u && leave_count == 3u, "queue-full exit balances inner protection");
    expect_record(payload, sizeof(payload));
    expect(test_ring_try_push(&ring, payload, sizeof(payload)) == NS_OK,
           "successful push nested inside caller protection");
    expect(guard_depth == caller_depth, "success preserves outer protection");
    test_preempt_leave(outer);
    caller_depth = 0u;
    expect(guard_depth == 0u && enter_count == leave_count, "outer protection restored");
    expect_record(payload, sizeof(payload));
    expect_record(payload, sizeof(payload));
}

static void test_invalid_and_wrap(void)
{
    const unsigned char payload[] = { 0x57 };
    ns_mpsc_record_part_t invalid = { .data = NULL, .size = 1u };

    reset_ring();
    expect(test_ring_try_push(NULL, payload, sizeof(payload)) == NS_E_INVAL, "reject null ring");
    expect(test_ring_try_push(&ring, NULL, 1u) == NS_E_INVAL, "reject null payload");
    expect(test_ring_try_pushv(&ring, &invalid, 1u) == NS_E_INVAL, "reject invalid part");
    expect(enter_count == 0u && leave_count == 0u, "invalid arguments do not enter protection");

    /* 末尾只余一个 header，真实记录必须通过 fake record 绕回。 */
    ns_atomic_store_explicit(&ring.write_pos, 128u - sizeof(size_t), ns_memory_order_relaxed);
    ns_atomic_store_explicit(&ring.read_pos, 128u - sizeof(size_t), ns_memory_order_relaxed);
    expect(test_ring_try_push(&ring, payload, sizeof(payload)) == NS_OK, "wrapped push succeeds");
    expect_record(payload, sizeof(payload));
    expect(test_ring_free_capacity(&ring) == ring.capacity, "wrapped ring fully reclaimed");
    expect(enter_count == 1u && leave_count == 1u && copy_count == 1u && guard_depth == 0u,
           "wrapped push balances protection and copies outside it");
}

int main(void)
{
    test_deferred_producer();
    test_full_and_nested();
    test_invalid_and_wrap();
    return failures != 0;
}
