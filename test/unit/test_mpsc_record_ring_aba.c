/**
 * @file test_mpsc_record_ring_aba.c
 * @brief 位置整周回绕与预留所有权的确定性回归。
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ns_mpsc_record_ring_init test_ring_init
#define ns_mpsc_record_ring_capacity test_ring_capacity
#define ns_mpsc_record_ring_free_capacity test_ring_free_capacity
#define ns_mpsc_record_ring_max_record_size test_ring_max_record_size
#define ns_mpsc_record_ring_try_push test_ring_try_push
#define ns_mpsc_record_ring_try_pushv test_ring_try_pushv
#define ns_mpsc_record_ring_try_acquire test_ring_try_acquire
#define ns_mpsc_record_ring_release test_ring_release

#include <nanosig/nanosig_mpsc_record_ring.h>

static bool test_reserve_cas(atomic_size_t *ptr, size_t *expected,
    size_t desired, ns_memory_order_t success, ns_memory_order_t failure);

/* 仅测试副本拦截 CAS；生产实现不添加调度 hook。 */
#undef ns_atomic_compare_exchange_weak_explicit
#define ns_atomic_compare_exchange_weak_explicit test_reserve_cas
#include "src/ds/ns_mpsc_record_ring.c"

#define CHECK(condition) do { \
    if(!(condition)){ \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while(0)

enum test_schedule { SCHEDULE_NONE, SCHEDULE_FULL, SCHEDULE_IN_FLIGHT };
static enum test_schedule schedule;
static ns_mpsc_record_ring_t ring;
static size_t storage[128u / sizeof(size_t)];
static unsigned char payload_b[64u - sizeof(size_t)];
static size_t stale_expected;
static size_t stale_desired;
static int hook_failed;
static int probe_in_flight;
static int probe_count;

static void seed_empty_ring(size_t position)
{
    /* 跳过已完成且环为空的历史周期；最终跨零由真实 push/release 完成。
       单线程模拟 actor 的交错，不依赖 OS 调度或窄化位置算术。 */
    ns_atomic_store_explicit(&ring.write_pos, position, ns_memory_order_relaxed);
    ns_atomic_store_explicit(&ring.read_pos, position, ns_memory_order_relaxed);
}

static int advance_other_actors(enum test_schedule selected)
{
    seed_empty_ring((size_t)0u - (selected == SCHEDULE_FULL ? 128u : 64u));
    probe_in_flight = selected == SCHEDULE_IN_FLIGHT;
    CHECK(test_ring_try_push(&ring, payload_b, sizeof(payload_b)) == NS_OK);
    if(selected == SCHEDULE_FULL){
        CHECK(test_ring_try_push(&ring, payload_b, sizeof(payload_b)) == NS_OK);
        CHECK(test_ring_free_capacity(&ring) == 0u);
    }
    CHECK(ns_atomic_load_explicit(&ring.write_pos, ns_memory_order_relaxed) == 0u);
    return 0;
}

static bool test_reserve_cas(atomic_size_t *ptr, size_t *expected,
    size_t desired, ns_memory_order_t success, ns_memory_order_t failure)
{
    bool acquired;

    if(schedule != SCHEDULE_NONE){
        enum test_schedule selected = schedule;
        schedule = SCHEDULE_NONE;
        stale_expected = *expected;
        stale_desired = desired;
        hook_failed |= advance_other_actors(selected);
    }
    /* 强 CAS 是弱 CAS 不发生伪失败的合法执行，用于固定本测试的交错。 */
    acquired = atomic_compare_exchange_strong_explicit(ptr, expected, desired,
        (memory_order)success, (memory_order)failure);
    if(acquired && probe_in_flight){
        size_t probe_expected = stale_expected;
        bool stolen;

        probe_in_flight = 0;
        ++probe_count;
        /* B 已取得预留，但尚未执行 write_pos 发布；此时探测 A 的旧 CAS。 */
        stolen = atomic_compare_exchange_strong_explicit(ptr, &probe_expected,
            stale_desired, memory_order_acq_rel, memory_order_relaxed);
        if(stolen){
            fprintf(stderr, "stale CAS stole an in-flight reservation\n");
            hook_failed = 1;
            atomic_store_explicit(ptr, desired, memory_order_release);
        }
        /* 标志位不能被容量观测当成一个已使用的字节。 */
        if(test_ring_free_capacity(&ring) != ring.capacity) hook_failed = 1;
    }
    return acquired;
}

static int consume_payload(unsigned char byte)
{
    void *record = NULL;
    size_t size = 0u;
    unsigned char expected[sizeof(payload_b)];

    memset(expected, byte, sizeof(expected));
    CHECK(test_ring_try_acquire(&ring, &record, &size) == NS_OK);
    CHECK(size == sizeof(expected));
    CHECK(memcmp(record, expected, size) == 0);
    CHECK(test_ring_release(&ring, record) == NS_OK);
    return 0;
}

static int test_wrapped_full_ring(void)
{
    unsigned char payload_a[sizeof(payload_b)];
    void *record = NULL;
    size_t size = 0u;

    CHECK(test_ring_init(&ring, storage, NS_CAPACITY_128) == NS_OK);
    memset(payload_a, 0x41, sizeof(payload_a));
    hook_failed = 0;
    schedule = SCHEDULE_FULL;
    CHECK(test_ring_try_push(&ring, payload_a, sizeof(payload_a)) == NS_E_QUEUE_FULL);
    CHECK(!hook_failed);
    CHECK(ns_atomic_load_explicit(&ring.write_pos, ns_memory_order_relaxed) == 0u);
    CHECK(test_ring_free_capacity(&ring) == 0u);
    CHECK(consume_payload(0x42) == 0);
    CHECK(consume_payload(0x42) == 0);
    CHECK(test_ring_try_acquire(&ring, &record, &size) == NS_E_EMPTY);
    CHECK(test_ring_try_push(&ring, payload_a, sizeof(payload_a)) == NS_OK);
    CHECK(consume_payload(0x41) == 0);
    CHECK(test_ring_free_capacity(&ring) == ring.capacity);
    return 0;
}

static int test_wrapped_in_flight_reservation(void)
{
    unsigned char payload_a[sizeof(payload_b)];

    CHECK(test_ring_init(&ring, storage, NS_CAPACITY_128) == NS_OK);
    memset(payload_a, 0x41, sizeof(payload_a));
    hook_failed = 0;
    probe_count = 0;
    schedule = SCHEDULE_IN_FLIGHT;
    CHECK(test_ring_try_push(&ring, payload_a, sizeof(payload_a)) == NS_OK);
    CHECK(!hook_failed);
    CHECK(probe_count == 1);
    CHECK(test_ring_free_capacity(&ring) == 0u);
    CHECK(consume_payload(0x42) == 0);
    CHECK(consume_payload(0x41) == 0);
    CHECK(test_ring_free_capacity(&ring) == ring.capacity);
    return 0;
}

static int test_fake_tail_across_counter_wrap(void)
{
    void *record = NULL;
    size_t size = 0u;

    CHECK(test_ring_init(&ring, storage, NS_CAPACITY_128) == NS_OK);
    seed_empty_ring((size_t)0u - NS_MPSC_RECORD_RING_ALIGNMENT);
    CHECK(test_ring_try_push(&ring, payload_b, sizeof(payload_b)) == NS_OK);
    CHECK(test_ring_free_capacity(&ring) == 64u - NS_MPSC_RECORD_RING_ALIGNMENT);
    CHECK(consume_payload(0x42) == 0);
    CHECK(test_ring_try_acquire(&ring, &record, &size) == NS_E_EMPTY);
    CHECK(test_ring_free_capacity(&ring) == ring.capacity);
    return 0;
}

static int test_busy_consumer_snapshots(void)
{
    ns_mpsc_record_header_t *pending;
    size_t pending_meta;
    void *record = NULL;
    size_t size = 0u;

    CHECK(test_ring_init(&ring, storage, NS_CAPACITY_128) == NS_OK);
    CHECK(test_ring_try_push(&ring, payload_b, sizeof(payload_b)) == NS_OK);
    CHECK(test_ring_try_push(&ring, payload_b, sizeof(payload_b)) == NS_OK);
    pending = ns_mpsc_record_ring_header_at(&ring, 64u);
    pending_meta = ns_atomic_load_explicit(&pending->meta, ns_memory_order_relaxed);
    ns_mpsc_record_ring_mark_uncommitted(pending);
    /* 一个已发布记录尚未提交，另一生产者正持有下次预留的 busy。 */
    ns_atomic_store_explicit(&ring.write_pos, 128u | (size_t)1u, ns_memory_order_release);
    CHECK(test_ring_free_capacity(&ring) == 0u);
    CHECK(consume_payload(0x42) == 0);
    CHECK(test_ring_free_capacity(&ring) == 64u);
    CHECK(test_ring_try_acquire(&ring, &record, &size) == NS_E_EMPTY);
    CHECK(record == NULL && size == 0u);
    ns_atomic_store_explicit(&pending->meta, pending_meta, ns_memory_order_release);
    CHECK(consume_payload(0x42) == 0);
    CHECK(test_ring_free_capacity(&ring) == ring.capacity);
    CHECK(test_ring_try_acquire(&ring, &record, &size) == NS_E_EMPTY);
    /* 掩掉 busy 不能掩盖真正超容量的位置差。 */
    ns_atomic_store_explicit(&ring.write_pos,
        256u + NS_MPSC_RECORD_RING_ALIGNMENT + (size_t)1u, ns_memory_order_release);
    CHECK(test_ring_free_capacity(&ring) == 0u);
    CHECK(test_ring_try_acquire(&ring, &record, &size) == NS_E_CORRUPT);
    return 0;
}

int main(void)
{
    int failures;

    memset(payload_b, 0x42, sizeof(payload_b));
    failures = test_wrapped_full_ring();
    failures += test_wrapped_in_flight_reservation();
    failures += test_fake_tail_across_counter_wrap();
    failures += test_busy_consumer_snapshots();
    return failures != 0;
}
