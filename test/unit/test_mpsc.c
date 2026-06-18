/**
 * @file test_mpsc.c
 * @brief Fixed-capacity MPSC queue unit tests.
 * @date 2026-05-23
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#include <nanosig/nanosig_mpsc.h>

#include <platform/port.h>

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__unix__) || defined(__linux__)
#include <pthread.h>
#include <sched.h>
#else
#error "test_mpsc requires pthreads or Win32 threads"
#endif

typedef struct test_item {
    uint32_t producer_id;
    uint32_t sequence;
    uint32_t mix;
    uint32_t checksum;
    uint8_t payload[48];
} test_item_t;

typedef struct producer_ctx {
    ns_mpsc_queue_t *queue;
    uint32_t producer_id;
    uint32_t sequence;
    uint32_t count;
    size_t produced_count;
    int failed;
    atomic_int stop_requested;
    atomic_int done;
#if defined(_WIN32)
    HANDLE thread;
#else
    pthread_t thread;
#endif
} producer_ctx_t;

static int expect_true(int condition)
{
    return condition ? 0 : 1;
}

static void test_yield(void)
{
#if defined(_WIN32)
    SwitchToThread();
#else
    sched_yield();
#endif
}

static uint32_t test_mix32(uint32_t value)
{
    value ^= value >> 16u;
    value *= UINT32_C(0x7feb352d);
    value ^= value >> 15u;
    value *= UINT32_C(0x846ca68b);
    value ^= value >> 16u;
    return value;
}

static uint32_t test_checksum_bytes(const uint8_t *data, size_t size)
{
    size_t i;
    uint32_t hash = UINT32_C(2166136261);

    for(i = 0u; i < size; ++i){
        hash ^= data[i];
        hash *= UINT32_C(16777619);
        hash ^= hash >> 13u;
    }

    return hash;
}

static void test_fill_item(test_item_t *item, uint32_t producer_id, uint32_t sequence)
{
    size_t i;

    item->producer_id = producer_id;
    item->sequence = sequence;
    item->mix = test_mix32(producer_id ^ (sequence * UINT32_C(0x9e3779b9)));

    for(i = 0u; i < sizeof(item->payload); ++i){
        uint32_t step = item->mix + (uint32_t)(i * 17u) + (sequence << (i & 7u));

        item->payload[i] = (uint8_t)(step ^ (step >> 11u) ^ (producer_id << (i & 3u)));
    }

    item->checksum = 0u;
    item->checksum = test_checksum_bytes((const uint8_t *)item, sizeof(*item));
}

static int test_item_matches(const test_item_t *item, uint32_t producer_id, uint32_t sequence)
{
    test_item_t expected;

    test_fill_item(&expected, producer_id, sequence);
    return memcmp(item, &expected, sizeof(expected)) == 0 ? 0 : 1;
}

static ns_platform_time_us_t test_stress_duration_us(void)
{
    const char *value = getenv("NS_MPSC_STRESS_DURATION_SEC");
    char *end = NULL;
    unsigned long seconds;

    if((value == NULL) || (value[0] == '\0')) return UINT64_C(10000000);

    seconds = strtoul(value, &end, 10);
    if((end == value) || ((end != NULL) && (*end != '\0')) || (seconds == 0ul)) return UINT64_C(10000000);

    return (ns_platform_time_us_t)seconds * UINT64_C(1000000);
}

static int test_now_us(ns_platform_time_us_t *out_now_us)
{
    return ns_platform_clock_monotonic_us(out_now_us) == NS_OK ? 0 : 1;
}

static int test_invalid_accessors(void)
{
    ns_mpsc_queue_t invalid = { 0 };

    if(expect_true(ns_mpsc_capacity(NULL) == 0u) != 0) return 1;
    if(expect_true(ns_mpsc_capacity(&invalid) == 0u) != 0) return 1;
    if(expect_true(ns_mpsc_free_capacity(NULL) == 0u) != 0) return 1;
    if(expect_true(ns_mpsc_free_capacity(&invalid) == 0u) != 0) return 1;

    return 0;
}

static int test_invalid_args(void)
{
    const ns_capacity_t storage_item_count = NS_CAPACITY_2;
    ns_mpsc_queue_t queue;
    ns_mpsc_slot_t slots[storage_item_count];
    uint8_t storage[sizeof(test_item_t) * storage_item_count];
    test_item_t item = { 0 };

    if(expect_true(ns_mpsc_init(NULL, slots, storage, NS_CAPACITY_2, sizeof(test_item_t)) == NS_E_INVAL) != 0) return 1;
    if(expect_true(ns_mpsc_init(&queue, NULL, storage, NS_CAPACITY_2, sizeof(test_item_t)) == NS_E_INVAL) != 0) return 1;
    if(expect_true(ns_mpsc_init(&queue, slots, NULL, NS_CAPACITY_2, sizeof(test_item_t)) == NS_E_INVAL) != 0) return 1;
    if(expect_true(ns_mpsc_init(&queue, slots, storage, (ns_capacity_t)0, sizeof(test_item_t)) == NS_E_INVAL) != 0) return 1;
    if(expect_true(ns_mpsc_init(&queue, slots, storage, (ns_capacity_t)3, sizeof(test_item_t)) == NS_E_INVAL) != 0) return 1;
    if(expect_true(ns_mpsc_init(&queue, slots, storage, NS_CAPACITY_2, 0u) == NS_E_INVAL) != 0) return 1;

    if(expect_true(ns_mpsc_init(&queue, slots, storage, NS_CAPACITY_2, sizeof(test_item_t)) == NS_OK) != 0) return 1;
    if(expect_true(ns_mpsc_capacity(&queue) == (size_t)NS_CAPACITY_2) != 0) return 1;
    if(expect_true(ns_mpsc_free_capacity(&queue) == (size_t)NS_CAPACITY_2) != 0) return 1;
    if(expect_true(ns_mpsc_try_push(NULL, &item) == NS_E_INVAL) != 0) return 1;
    if(expect_true(ns_mpsc_try_push(&queue, NULL) == NS_E_INVAL) != 0) return 1;
    if(expect_true(ns_mpsc_try_pop(NULL, &item) == NS_E_INVAL) != 0) return 1;
    if(expect_true(ns_mpsc_try_pop(&queue, NULL) == NS_E_INVAL) != 0) return 1;

    return 0;
}

static int test_single_thread_paths(void)
{
    const ns_capacity_t queue_capacity = NS_CAPACITY_4;
    ns_mpsc_queue_t queue;
    ns_mpsc_slot_t slots[queue_capacity];
    uint8_t storage[sizeof(test_item_t) * queue_capacity];
    test_item_t in;
    test_item_t out;
    uint32_t i;

    if(expect_true(ns_mpsc_init(&queue, slots, storage, queue_capacity, sizeof(test_item_t)) == NS_OK) != 0) return 1;
    if(expect_true(ns_mpsc_capacity(&queue) == (size_t)queue_capacity) != 0) return 1;
    if(expect_true(ns_mpsc_free_capacity(&queue) == (size_t)queue_capacity) != 0) return 1;

    memset(&out, 0, sizeof(out));
    if(expect_true(ns_mpsc_try_pop(&queue, &out) == NS_E_EMPTY) != 0) return 1;

    for(i = 0u; i < queue_capacity; ++i){
        test_fill_item(&in, 7u, i);
        if(expect_true(ns_mpsc_try_push(&queue, &in) == NS_OK) != 0) return 1;
        if(expect_true(ns_mpsc_free_capacity(&queue) == ((size_t)queue_capacity - (size_t)i - 1u)) != 0) return 1;
    }

    test_fill_item(&in, 7u, 99u);
    if(expect_true(ns_mpsc_try_push(&queue, &in) == NS_E_QUEUE_FULL) != 0) return 1;
    if(expect_true(ns_mpsc_free_capacity(&queue) == 0u) != 0) return 1;

    for(i = 0u; i < queue_capacity; ++i){
        memset(&out, 0, sizeof(out));
        if(expect_true(ns_mpsc_try_pop(&queue, &out) == NS_OK) != 0) return 1;
        if(expect_true(test_item_matches(&out, 7u, i) == 0) != 0) return 1;
        if(expect_true(ns_mpsc_free_capacity(&queue) == ((size_t)i + 1u)) != 0) return 1;
    }

    if(expect_true(ns_mpsc_try_pop(&queue, &out) == NS_E_EMPTY) != 0) return 1;

    for(i = 0u; i < 12u; ++i){
        test_fill_item(&in, 11u, i);
        if(expect_true(ns_mpsc_try_push(&queue, &in) == NS_OK) != 0) return 1;
        if(expect_true(ns_mpsc_free_capacity(&queue) == ((size_t)queue_capacity - 1u)) != 0) return 1;

        memset(&out, 0, sizeof(out));
        if(expect_true(ns_mpsc_try_pop(&queue, &out) == NS_OK) != 0) return 1;
        if(expect_true(test_item_matches(&out, 11u, i) == 0) != 0) return 1;
        if(expect_true(ns_mpsc_free_capacity(&queue) == (size_t)queue_capacity) != 0) return 1;
    }

    return 0;
}

static int test_counter_wraparound(void)
{
    const ns_capacity_t queue_capacity = NS_CAPACITY_4;
    ns_mpsc_queue_t queue;
    ns_mpsc_slot_t slots[queue_capacity];
    uint8_t storage[sizeof(test_item_t) * queue_capacity];
    size_t base = SIZE_MAX - 1u;
    test_item_t in;
    test_item_t out;
    uint32_t i;

    if(expect_true(ns_mpsc_init(&queue, slots, storage, queue_capacity, sizeof(test_item_t)) == NS_OK) != 0) return 1;

    ns_atomic_store_explicit(&queue.enqueue_pos, base, ns_memory_order_relaxed);
    ns_atomic_store_explicit(&queue.dequeue_pos, base, ns_memory_order_relaxed);
    ns_atomic_store_explicit(&slots[0].sequence, 0u, ns_memory_order_relaxed);
    ns_atomic_store_explicit(&slots[1].sequence, 1u, ns_memory_order_relaxed);
    ns_atomic_store_explicit(&slots[2].sequence, base, ns_memory_order_relaxed);
    ns_atomic_store_explicit(&slots[3].sequence, base + 1u, ns_memory_order_relaxed);

    for(i = 0u; i < queue_capacity; ++i){
        test_fill_item(&in, 17u, i);
        if(expect_true(ns_mpsc_try_push(&queue, &in) == NS_OK) != 0) return 1;
    }

    test_fill_item(&in, 17u, 99u);
    if(expect_true(ns_mpsc_try_push(&queue, &in) == NS_E_QUEUE_FULL) != 0) return 1;

    for(i = 0u; i < queue_capacity; ++i){
        memset(&out, 0, sizeof(out));
        if(expect_true(ns_mpsc_try_pop(&queue, &out) == NS_OK) != 0) return 1;
        if(expect_true(test_item_matches(&out, 17u, i) == 0) != 0) return 1;
    }

    if(expect_true(ns_mpsc_try_pop(&queue, &out) == NS_E_EMPTY) != 0) return 1;

    return 0;
}

static void producer_run(producer_ctx_t *ctx)
{
    for(;;){
        test_item_t item;
        int rc;

        if((ctx->count != 0u) && (ctx->sequence >= ctx->count)) break;
        if(ns_atomic_load_explicit(&ctx->stop_requested, ns_memory_order_relaxed) != 0) break;

        test_fill_item(&item, ctx->producer_id, ctx->sequence);

        do{
            rc = ns_mpsc_try_push(ctx->queue, &item);
            if(rc == NS_E_QUEUE_FULL){
                if(ns_atomic_load_explicit(&ctx->stop_requested, ns_memory_order_relaxed) != 0) break;
                test_yield();
            }
        }while(rc == NS_E_QUEUE_FULL);

        if(rc == NS_E_QUEUE_FULL) break;
        if(rc != NS_OK){
            ctx->failed = 1;
            ns_atomic_store_explicit(&ctx->done, 1, ns_memory_order_release);
            return;
        }

        ++ctx->sequence;
        ++ctx->produced_count;
    }

    ns_atomic_store_explicit(&ctx->done, 1, ns_memory_order_release);
}

#if defined(_WIN32)
static DWORD WINAPI producer_thread_main(LPVOID arg)
{
    producer_run((producer_ctx_t *)arg);
    return 0u;
}
#else
static void *producer_thread_main(void *arg)
{
    producer_run((producer_ctx_t *)arg);
    return NULL;
}
#endif

static int producer_start(producer_ctx_t *ctx)
{
#if defined(_WIN32)
    ctx->thread = CreateThread(NULL, 0u, producer_thread_main, ctx, 0u, NULL);
    return ctx->thread != NULL ? 0 : 1;
#else
    return pthread_create(&ctx->thread, NULL, producer_thread_main, ctx) == 0 ? 0 : 1;
#endif
}

static int producer_join(producer_ctx_t *ctx)
{
#if defined(_WIN32)
    DWORD wait_rc = WaitForSingleObject(ctx->thread, INFINITE);
    BOOL close_rc = CloseHandle(ctx->thread);

    return (wait_rc == WAIT_OBJECT_0) && (close_rc != 0) ? 0 : 1;
#else
    return pthread_join(ctx->thread, NULL) == 0 ? 0 : 1;
#endif
}

static int test_multi_producer(void)
{
    enum {
        producer_count = 4,
        per_producer_count = 2048
    };
    const ns_capacity_t queue_capacity = NS_CAPACITY_64;

    ns_mpsc_queue_t queue;
    ns_mpsc_slot_t slots[queue_capacity];
    uint8_t storage[sizeof(test_item_t) * queue_capacity];
    producer_ctx_t producers[producer_count];
    uint8_t seen[producer_count][per_producer_count];
    uint32_t started = 0u;
    uint32_t popped_count = 0u;
    uint32_t total_count;
    uint32_t i;
    int start_failed = 0;

    memset(seen, 0, sizeof(seen));
    memset(producers, 0, sizeof(producers));

    if(expect_true(ns_mpsc_init(&queue, slots, storage, queue_capacity, sizeof(test_item_t)) == NS_OK) != 0) return 1;

    for(i = 0u; i < producer_count; ++i){
        producers[i].queue = &queue;
        producers[i].producer_id = i;
        producers[i].sequence = 0u;
        producers[i].count = per_producer_count;
        producers[i].produced_count = 0u;
        producers[i].failed = 0;
        ns_atomic_init(&producers[i].stop_requested, 0);
        ns_atomic_init(&producers[i].done, 0);
        if(producer_start(&producers[i]) != 0){
            start_failed = 1;
            break;
        }
        ++started;
    }

    total_count = started * per_producer_count;

    while(popped_count < total_count){
        test_item_t out;
        int rc;

        rc = ns_mpsc_try_pop(&queue, &out);
        if(rc == NS_E_EMPTY){
            test_yield();
            continue;
        }
        if(rc != NS_OK) return 1;

        if(out.producer_id >= producer_count) return 1;
        if(out.sequence >= per_producer_count) return 1;
        if(seen[out.producer_id][out.sequence] != 0u) return 1;
        if(test_item_matches(&out, out.producer_id, out.sequence) != 0) return 1;

        seen[out.producer_id][out.sequence] = 1u;
        ++popped_count;
    }

    for(i = 0u; i < started; ++i){
        if(producer_join(&producers[i]) != 0) return 1;
        if(producers[i].failed != 0) return 1;
    }

    if(start_failed != 0) return 1;
    if(expect_true(started == producer_count) != 0) return 1;
    if(expect_true(popped_count == (producer_count * per_producer_count)) != 0) return 1;

    for(i = 0u; i < producer_count; ++i){
        uint32_t sequence;

        for(sequence = 0u; sequence < per_producer_count; ++sequence){
            if(expect_true(seen[i][sequence] == 1u) != 0) return 1;
        }
    }

    return 0;
}

static int test_stress_multi_producer_long_run(void)
{
    enum {
        producer_count = 16
    };
    const ns_capacity_t queue_capacity = NS_CAPACITY_1024;
    ns_platform_time_us_t start_us = 0u;
    ns_platform_time_us_t now_us = 0u;
    ns_platform_time_us_t deadline_us = 0u;
    ns_platform_time_us_t duration_us = test_stress_duration_us();
    ns_mpsc_queue_t queue;
    ns_mpsc_slot_t slots[queue_capacity];
    uint8_t storage[sizeof(test_item_t) * queue_capacity];
    producer_ctx_t producers[producer_count];
    uint32_t next_sequence[producer_count];
    size_t started = 0u;
    size_t total_popped = 0u;
    size_t loop_count = 0u;
    int stop_requested = 0;
    size_t i;
    size_t idle_spins = 0u;

    memset(producers, 0, sizeof(producers));
    memset(next_sequence, 0, sizeof(next_sequence));

    if(expect_true(ns_mpsc_init(&queue, slots, storage, queue_capacity, sizeof(test_item_t)) == NS_OK) != 0) return 1;
    if(expect_true(ns_mpsc_capacity(&queue) == (size_t)queue_capacity) != 0) return 1;
    if(expect_true(ns_mpsc_free_capacity(&queue) == (size_t)queue_capacity) != 0) return 1;
    if(test_now_us(&start_us) != 0) return 1;

    deadline_us = start_us + duration_us;

    for(i = 0u; i < producer_count; ++i){
        producers[i].queue = &queue;
        producers[i].producer_id = (uint32_t)i;
        producers[i].sequence = 0u;
        producers[i].count = 0u;
        producers[i].produced_count = 0u;
        producers[i].failed = 0;
        ns_atomic_init(&producers[i].stop_requested, 0);
        ns_atomic_init(&producers[i].done, 0);
        if(producer_start(&producers[i]) != 0) break;
        ++started;
    }

    if(expect_true(started == producer_count) != 0) return 1;

    for(;;){
        test_item_t out;
        int pop_rc;
        int all_done = 1;

        ++loop_count;
        pop_rc = ns_mpsc_try_pop(&queue, &out);

        if(pop_rc == NS_OK){
            if(out.producer_id >= producer_count) return 1;
            if(test_item_matches(&out, out.producer_id, next_sequence[out.producer_id]) != 0) return 1;
            ++next_sequence[out.producer_id];
            ++total_popped;
            idle_spins = 0u;
        }else if(pop_rc == NS_E_EMPTY){
            ++idle_spins;
        }else{
            return 1;
        }

        if((stop_requested == 0) && ((loop_count & 0xfffu) == 0u)){
            if(test_now_us(&now_us) != 0) return 1;
            if(now_us >= deadline_us){
                stop_requested = 1;
                for(i = 0u; i < producer_count; ++i){
                    ns_atomic_store_explicit(&producers[i].stop_requested, 1, ns_memory_order_relaxed);
                }
            }
        }

        if(stop_requested == 0) continue;

        for(i = 0u; i < producer_count; ++i){
            if(ns_atomic_load_explicit(&producers[i].done, ns_memory_order_acquire) == 0){
                all_done = 0;
                break;
            }
        }

        if((all_done != 0) && (pop_rc == NS_E_EMPTY)){
            if(ns_mpsc_try_pop(&queue, &out) == NS_E_EMPTY) break;
            if(out.producer_id >= producer_count) return 1;
            if(test_item_matches(&out, out.producer_id, next_sequence[out.producer_id]) != 0) return 1;
            ++next_sequence[out.producer_id];
            ++total_popped;
            idle_spins = 0u;
            continue;
        }

        if(pop_rc == NS_E_EMPTY) test_yield();

        if(expect_true(ns_mpsc_free_capacity(&queue) <= ns_mpsc_capacity(&queue)) != 0) return 1;
    }

    for(i = 0u; i < producer_count; ++i){
        if(producer_join(&producers[i]) != 0) return 1;
        if(producers[i].failed != 0) return 1;
        if(expect_true((size_t)next_sequence[i] == producers[i].produced_count) != 0) return 1;
        if(expect_true(producers[i].produced_count > 0u) != 0) return 1;
    }

    if(expect_true(ns_mpsc_free_capacity(&queue) == ns_mpsc_capacity(&queue)) != 0) return 1;
    if(expect_true(total_popped > 0u) != 0) return 1;

    return 0;
}

int main(void)
{
    if(test_invalid_accessors() != 0) return 1;
    if(test_invalid_args() != 0) return 1;
    if(test_single_thread_paths() != 0) return 1;
    if(test_counter_wraparound() != 0) return 1;
    if(test_multi_producer() != 0) return 1;
    if(test_stress_multi_producer_long_run() != 0) return 1;

    return 0;
}
