/**
 * @file test_mpsc_record_ring.c
 * @brief Variable-size MPSC record ring unit tests.
 * @date 2026-05-30
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#include <nanosig/nanosig_mpsc_record_ring.h>

#include <stdint.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__unix__) || defined(__linux__)
#include <pthread.h>
#include <sched.h>
#else
#error "test_mpsc_record_ring requires pthreads or Win32 threads"
#endif

/* ------------------------------------------------------------------ */
/*  Test infrastructure                                                */
/* ------------------------------------------------------------------ */

typedef struct test_record_header {
    uint32_t producer_id;
    uint32_t sequence;
    uint32_t payload_size;
    uint32_t checksum;
} test_record_header_t;

typedef struct producer_ctx {
    ns_mpsc_record_ring_t *ring;
    uint32_t producer_id;
    uint32_t sequence;
    uint32_t count;
    int failed;
    int payload_mode;
    size_t fixed_payload_size;
    atomic_int done;
#if defined(_WIN32)
    HANDLE thread;
#else
    pthread_t thread;
#endif
} producer_ctx_t;

#define MAX_PAYLOAD_SIZE 64u
#define RECORD_BUF_SIZE  (sizeof(test_record_header_t) + MAX_PAYLOAD_SIZE)

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

static size_t test_payload_size(uint32_t producer_id, uint32_t sequence)
{
    return 1u + (size_t)(((producer_id * 17u) + (sequence * 7u)) % 53u);
}

static void test_fill_payload(uint8_t *payload, size_t payload_size, uint32_t producer_id, uint32_t sequence)
{
    size_t i;

    for(i = 0u; i < payload_size; ++i){
        uint32_t value = (producer_id * UINT32_C(131)) ^
                         (sequence * UINT32_C(977)) ^
                         (uint32_t)(i * 29u);
        payload[i] = (uint8_t)(value ^ (value >> 8u) ^ (value >> 17u));
    }
}

static void test_fill_record_ex(
    uint8_t *record,
    size_t *out_record_size,
    uint32_t producer_id,
    uint32_t sequence,
    size_t payload_size)
{
    test_record_header_t header;

    header.producer_id = producer_id;
    header.sequence = sequence;
    header.payload_size = (uint32_t)payload_size;
    header.checksum = 0u;

    memcpy(record, &header, sizeof(header));
    test_fill_payload(&record[sizeof(header)], payload_size, producer_id, sequence);

    header.checksum = test_checksum_bytes(record, sizeof(header) + payload_size);
    memcpy(record, &header, sizeof(header));
    *out_record_size = sizeof(header) + payload_size;
}

static int test_popped_record_matches_ex(
    const uint8_t *record,
    size_t record_size,
    size_t expected_payload_size)
{
    test_record_header_t header;
    uint32_t checksum;
    uint8_t copy[RECORD_BUF_SIZE];
    uint8_t expected[RECORD_BUF_SIZE];
    size_t expected_size = 0u;

    if(record_size < sizeof(header)) return 1;
    if(record_size > sizeof(copy)) return 1;

    memcpy(&header, record, sizeof(header));
    if(record_size != (sizeof(header) + (size_t)header.payload_size)) return 1;
    if(header.payload_size != expected_payload_size) return 1;

    checksum = header.checksum;
    header.checksum = 0u;
    memcpy(copy, record, record_size);
    memcpy(copy, &header, sizeof(header));
    if(test_checksum_bytes(copy, record_size) != checksum) return 1;

    test_fill_record_ex(expected, &expected_size, header.producer_id,
                        header.sequence, expected_payload_size);
    if(record_size != expected_size) return 1;
    if(memcmp(record, expected, expected_size) != 0) return 1;
    return 0;
}

static int test_popped_record_matches(const uint8_t *record, size_t record_size)
{
    test_record_header_t header;

    if(record_size < sizeof(header)) return 1;
    memcpy(&header, record, sizeof(header));
    return test_popped_record_matches_ex(record, record_size, header.payload_size);
}

/* ------------------------------------------------------------------ */
/*  1. Invalid accessors                                               */
/* ------------------------------------------------------------------ */

static int test_invalid_accessors(void)
{
    ns_mpsc_record_ring_t invalid = { 0 };

    if(expect_true(ns_mpsc_record_ring_capacity(NULL) == 0u) != 0) return 1;
    if(expect_true(ns_mpsc_record_ring_capacity(&invalid) == 0u) != 0) return 1;
    if(expect_true(ns_mpsc_record_ring_free_capacity(NULL) == 0u) != 0) return 1;
    if(expect_true(ns_mpsc_record_ring_free_capacity(&invalid) == 0u) != 0) return 1;
    if(expect_true(ns_mpsc_record_ring_max_record_size(NULL) == 0u) != 0) return 1;
    if(expect_true(ns_mpsc_record_ring_max_record_size(&invalid) == 0u) != 0) return 1;

    return 0;
}

/* ------------------------------------------------------------------ */
/*  2. Invalid arguments                                               */
/* ------------------------------------------------------------------ */

static int test_invalid_args(void)
{
    size_t storage[NS_CAPACITY_128 / sizeof(size_t)];
    size_t misaligned_storage[(NS_CAPACITY_128 / sizeof(size_t)) + 1u];
    ns_mpsc_record_ring_t ring;
    ns_mpsc_record_part_t part;
    uint8_t record[8] = { 0 };
    uint8_t out[8];
    size_t record_size = 1u;

    part.data = record;
    part.size = sizeof(record);

    /* init */
    if(expect_true(ns_mpsc_record_ring_init(NULL, storage, NS_CAPACITY_128) == NS_E_INVAL) != 0) return 1;
    if(expect_true(ns_mpsc_record_ring_init(&ring, NULL, NS_CAPACITY_128) == NS_E_INVAL) != 0) return 1;
    if(expect_true(ns_mpsc_record_ring_init(&ring, &((uint8_t *)misaligned_storage)[1], NS_CAPACITY_128) == NS_E_INVAL) != 0) return 1;
    if(expect_true(ns_mpsc_record_ring_init(&ring, storage, (ns_capacity_t)0) == NS_E_INVAL) != 0) return 1;
    if(expect_true(ns_mpsc_record_ring_init(&ring, storage, (ns_capacity_t)3) == NS_E_INVAL) != 0) return 1;

    if(expect_true(ns_mpsc_record_ring_init(&ring, storage, NS_CAPACITY_128) == NS_OK) != 0) return 1;

    /* try_push */
    if(expect_true(ns_mpsc_record_ring_try_push(NULL, record, sizeof(record)) == NS_E_INVAL) != 0) return 1;
    if(expect_true(ns_mpsc_record_ring_try_push(&ring, NULL, sizeof(record)) == NS_E_INVAL) != 0) return 1;
    if(expect_true(ns_mpsc_record_ring_try_push(&ring, record, 0u) == NS_OK) != 0) return 1;

    /* try_pushv */
    if(expect_true(ns_mpsc_record_ring_try_pushv(&ring, NULL, 1u) == NS_E_INVAL) != 0) return 1;
    if(expect_true(ns_mpsc_record_ring_try_pushv(&ring, &part, 0u) == NS_OK) != 0) return 1;

    part.data = NULL;
    part.size = 1u;
    if(expect_true(ns_mpsc_record_ring_try_pushv(&ring, &part, 1u) == NS_E_INVAL) != 0) return 1;

    part.data = NULL;
    part.size = 0u;
    if(expect_true(ns_mpsc_record_ring_try_pushv(&ring, &part, 1u) == NS_OK) != 0) return 1;

    /* try_pop */
    if(expect_true(ns_mpsc_record_ring_try_pop(NULL, out, sizeof(out), &record_size) == NS_E_INVAL) != 0) return 1;
    if(expect_true(ns_mpsc_record_ring_try_pop(&ring, NULL, sizeof(out), &record_size) == NS_E_INVAL) != 0) return 1;
    if(expect_true(ns_mpsc_record_ring_try_pop(&ring, out, sizeof(out), NULL) == NS_E_INVAL) != 0) return 1;

    return 0;
}

/* ------------------------------------------------------------------ */
/*  3. Single-thread paths                                             */
/* ------------------------------------------------------------------ */

static int test_single_thread_paths(void)
{
    size_t storage[NS_CAPACITY_128 / sizeof(size_t)];
    ns_mpsc_record_ring_t ring;
    uint8_t out[64];
    size_t record_size = 99u;
    const char *hello_a = "hel";
    const char *hello_b = "lo";
    ns_mpsc_record_part_t parts[2];

    parts[0].data = hello_a;
    parts[0].size = 3u;
    parts[1].data = hello_b;
    parts[1].size = 2u;

    if(expect_true(ns_mpsc_record_ring_init(&ring, storage, NS_CAPACITY_128) == NS_OK) != 0) return 1;
    if(expect_true(ns_mpsc_record_ring_capacity(&ring) == (size_t)NS_CAPACITY_128) != 0) return 1;
    if(expect_true(ns_mpsc_record_ring_free_capacity(&ring) == (size_t)NS_CAPACITY_128) != 0) return 1;
    if(expect_true(ns_mpsc_record_ring_max_record_size(&ring) > 0u) != 0) return 1;

    /* pop empty → NS_E_EMPTY */
    if(expect_true(ns_mpsc_record_ring_try_pop(&ring, out, sizeof(out), &record_size) == NS_E_EMPTY) != 0) return 1;
    if(expect_true(record_size == 0u) != 0) return 1;

    /* single push/pop */
    if(expect_true(ns_mpsc_record_ring_try_push(&ring, "A", 1u) == NS_OK) != 0) return 1;
    if(expect_true(ns_mpsc_record_ring_try_pop(&ring, out, sizeof(out), &record_size) == NS_OK) != 0) return 1;
    if(expect_true(record_size == 1u) != 0) return 1;
    if(expect_true(memcmp(out, "A", 1u) == 0) != 0) return 1;

    /* pushv multi-part */
    if(expect_true(ns_mpsc_record_ring_try_pushv(&ring, parts, 2u) == NS_OK) != 0) return 1;
    if(expect_true(ns_mpsc_record_ring_try_pop(&ring, out, sizeof(out), &record_size) == NS_OK) != 0) return 1;
    if(expect_true(record_size == 5u) != 0) return 1;
    if(expect_true(memcmp(out, "hello", 5u) == 0) != 0) return 1;

    /* pop NOMEM then retry */
    if(expect_true(ns_mpsc_record_ring_try_push(&ring, "abcdef", 6u) == NS_OK) != 0) return 1;
    if(expect_true(ns_mpsc_record_ring_try_pop(&ring, out, 3u, &record_size) == NS_E_NOMEM) != 0) return 1;
    if(expect_true(record_size == 6u) != 0) return 1;
    if(expect_true(ns_mpsc_record_ring_try_pop(&ring, out, sizeof(out), &record_size) == NS_OK) != 0) return 1;
    if(expect_true(memcmp(out, "abcdef", 6u) == 0) != 0) return 1;

    /* pop empty again */
    if(expect_true(ns_mpsc_record_ring_try_pop(&ring, out, sizeof(out), &record_size) == NS_E_EMPTY) != 0) return 1;

    return 0;
}

/* ------------------------------------------------------------------ */
/*  4. Full queue                                                      */
/* ------------------------------------------------------------------ */

static int test_full_queue(void)
{
    size_t storage[NS_CAPACITY_128 / sizeof(size_t)];
    ns_mpsc_record_ring_t ring;
    uint8_t out[8];
    size_t record_size = 0u;
    size_t i;

    if(expect_true(ns_mpsc_record_ring_init(&ring, storage, NS_CAPACITY_128) == NS_OK) != 0) return 1;

    for(i = 0u; i < 8u; ++i){
        if(expect_true(ns_mpsc_record_ring_try_push(&ring, "x", 1u) == NS_OK) != 0) return 1;
    }

    if(expect_true(ns_mpsc_record_ring_free_capacity(&ring) == 0u) != 0) return 1;
    if(expect_true(ns_mpsc_record_ring_try_push(&ring, "y", 1u) == NS_E_QUEUE_FULL) != 0) return 1;

    for(i = 0u; i < 8u; ++i){
        if(expect_true(ns_mpsc_record_ring_try_pop(&ring, out, sizeof(out), &record_size) == NS_OK) != 0) return 1;
        if(expect_true(record_size == 1u) != 0) return 1;
        if(expect_true(out[0] == (uint8_t)'x') != 0) return 1;
    }

    if(expect_true(ns_mpsc_record_ring_free_capacity(&ring) == ns_mpsc_record_ring_capacity(&ring)) != 0) return 1;

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Shared test payload                                                */
/* ------------------------------------------------------------------ */

static uint8_t g_test_payload[64];

static void test_fill_global_payload(void)
{
    memset(g_test_payload, 0x5a, sizeof(g_test_payload));
}

/* ------------------------------------------------------------------ */
/*  5. Padding and data wrap                                           */
/* ------------------------------------------------------------------ */

static int test_padding_and_data_wrap(void)
{
    size_t storage[NS_CAPACITY_128 / sizeof(size_t)];
    ns_mpsc_record_ring_t ring;
    uint8_t out[64];
    size_t max_payload;
    size_t record_size = 0u;
    size_t padding_payload_size;

    if(expect_true(ns_mpsc_record_ring_init(&ring, storage, NS_CAPACITY_128) == NS_OK) != 0) return 1;
    max_payload = ns_mpsc_record_ring_max_record_size(&ring);

    /* push max_payload, pop it → advances write_pos by one full slot */
    if(expect_true(ns_mpsc_record_ring_try_push(&ring, g_test_payload, max_payload) == NS_OK) != 0) return 1;
    if(expect_true(ns_mpsc_record_ring_try_pop(&ring, out, sizeof(out), &record_size) == NS_OK) != 0) return 1;
    if(expect_true(record_size == max_payload) != 0) return 1;
    if(expect_true(memcmp(out, g_test_payload, max_payload) == 0) != 0) return 1;

    /* push a record that triggers alignment padding, pop and verify */
    padding_payload_size = max_payload - NS_MPSC_RECORD_RING_ALIGNMENT;
    if(expect_true(ns_mpsc_record_ring_try_push(&ring, g_test_payload, padding_payload_size) == NS_OK) != 0) return 1;
    if(expect_true(ns_mpsc_record_ring_try_pop(&ring, out, sizeof(out), &record_size) == NS_OK) != 0) return 1;
    if(expect_true(record_size == padding_payload_size) != 0) return 1;
    if(expect_true(memcmp(out, g_test_payload, padding_payload_size) == 0) != 0) return 1;

    return 0;
}

/* ------------------------------------------------------------------ */
/*  6. Uncommitted slot: consumer must not see it                      */
/* ------------------------------------------------------------------ */

static int test_reserved_slot_does_not_replay_stale_record(void)
{
    size_t storage[NS_CAPACITY_128 / sizeof(size_t)];
    ns_mpsc_record_ring_t ring;
    uint8_t out[16];
    size_t record_size = 0u;
    size_t stale_record_total_size;

    if(expect_true(ns_mpsc_record_ring_init(&ring, storage, NS_CAPACITY_128) == NS_OK) != 0) return 1;

    /* Compute a valid total_size for a 1-byte payload */
    stale_record_total_size = ns_align_up(sizeof(size_t) + 1u, NS_MPSC_RECORD_RING_ALIGNMENT);

    /* push/pop one record to advance positions past 0 */
    if(expect_true(ns_mpsc_record_ring_try_push(&ring, "A", 1u) == NS_OK) != 0) return 1;
    if(expect_true(ns_mpsc_record_ring_try_pop(&ring, out, sizeof(out), &record_size) == NS_OK) != 0) return 1;
    if(expect_true(record_size == 1u) != 0) return 1;
    if(expect_true(out[0] == (uint8_t)'A') != 0) return 1;

    /*
     * Simulate a slow producer: reserve_pos advanced but write_pos lags.
     * reserve_pos == write_pos ensures CAS consistency if try_push is called.
     * The consumer sees write_pos == read_pos → NS_E_EMPTY.
     */
    ns_atomic_store_explicit(&ring.reserve_pos, ring.capacity, ns_memory_order_relaxed);
    ns_atomic_store_explicit(&ring.write_pos, ring.capacity, ns_memory_order_relaxed);
    ns_atomic_store_explicit(&ring.read_pos, ring.capacity, ns_memory_order_relaxed);
    ns_atomic_store_explicit(&ring.reserve_pos, ring.capacity + stale_record_total_size, ns_memory_order_release);

    record_size = 99u;
    out[0] = 0u;
    if(expect_true(ns_mpsc_record_ring_try_pop(&ring, out, sizeof(out), &record_size) == NS_E_EMPTY) != 0) return 1;
    if(expect_true(record_size == 0u) != 0) return 1;

    return 0;
}

/* ------------------------------------------------------------------ */
/*  7. Wrap-around: record data spans buffer boundary                  */
/* ------------------------------------------------------------------ */

static int test_wrap_record(void)
{
    size_t storage[NS_CAPACITY_128 / sizeof(size_t)];
    ns_mpsc_record_ring_t ring;
    uint8_t out[64];
    size_t record_size = 0u;
    size_t max_payload;
    size_t i;

    if(expect_true(ns_mpsc_record_ring_init(&ring, storage, NS_CAPACITY_128) == NS_OK) != 0) return 1;
    max_payload = ns_mpsc_record_ring_max_record_size(&ring);

    /*
     * Advance write_pos to 80 via push/pop, then push a record whose
     * payload wraps around the buffer boundary:
     *
     *   total_size = align_up(8 + 24) = 32
     *   write_pos 80 + 32 = 112 ≤ capacity(128)  → fits at tail
     *
     * For a real wrap we push max_payload first (48-byte total) to consume
     * half the buffer, then an 8-byte record, pop both, and push a 24-byte
     * payload whose 32-byte record spans the boundary.
     */
    /* push+pop max_payload → write_pos = 48+8 = 56 (aligned) */
    if(expect_true(ns_mpsc_record_ring_try_push(&ring, g_test_payload, max_payload) == NS_OK) != 0) return 1;
    if(expect_true(ns_mpsc_record_ring_try_pop(&ring, out, sizeof(out), &record_size) == NS_OK) != 0) return 1;

    /* push+pop 8-byte → write_pos = 80 */
    if(expect_true(ns_mpsc_record_ring_try_push(&ring, "ABCDEFGH", 8u) == NS_OK) != 0) return 1;
    if(expect_true(ns_mpsc_record_ring_try_pop(&ring, out, sizeof(out), &record_size) == NS_OK) != 0) return 1;

    /* push 24-byte payload; total_size=32, record at 80..111 */
    {
        uint8_t wrap_payload[24];
        for(i = 0u; i < sizeof(wrap_payload); ++i) wrap_payload[i] = (uint8_t)(0xA0u + i);
        if(expect_true(ns_mpsc_record_ring_try_push(&ring, wrap_payload, sizeof(wrap_payload)) == NS_OK) != 0) return 1;
    }

    /* pop and verify wrapped data integrity */
    if(expect_true(ns_mpsc_record_ring_try_pop(&ring, out, sizeof(out), &record_size) == NS_OK) != 0) return 1;
    if(expect_true(record_size == 24u) != 0) return 1;
    for(i = 0u; i < 24u; ++i){
        if(expect_true(out[i] == (uint8_t)(0xA0u + i)) != 0) return 1;
    }

    /* ring should be empty */
    if(expect_true(ns_mpsc_record_ring_try_pop(&ring, out, sizeof(out), &record_size) == NS_E_EMPTY) != 0) return 1;
    if(expect_true(ns_mpsc_record_ring_free_capacity(&ring) == ring.capacity) != 0) return 1;

    return 0;
}

/* ------------------------------------------------------------------ */
/*  8. Zero-size record                                                */
/* ------------------------------------------------------------------ */

static int test_zero_size_record(void)
{
    size_t storage[NS_CAPACITY_128 / sizeof(size_t)];
    ns_mpsc_record_ring_t ring;
    ns_mpsc_record_part_t zero_part;
    uint8_t out[8];
    size_t record_size = 99u;

    if(expect_true(ns_mpsc_record_ring_init(&ring, storage, NS_CAPACITY_128) == NS_OK) != 0) return 1;

    zero_part.data = NULL;
    zero_part.size = 0u;
    if(expect_true(ns_mpsc_record_ring_try_pushv(&ring, &zero_part, 1u) == NS_OK) != 0) return 1;

    if(expect_true(ns_mpsc_record_ring_try_pop(&ring, out, sizeof(out), &record_size) == NS_OK) != 0) return 1;
    if(expect_true(record_size == 0u) != 0) return 1;

    return 0;
}

/* ------------------------------------------------------------------ */
/*  9. Pop NOMEM retry preserves record                                */
/* ------------------------------------------------------------------ */

static int test_pop_nomem_retry(void)
{
    size_t storage[NS_CAPACITY_128 / sizeof(size_t)];
    ns_mpsc_record_ring_t ring;
    uint8_t out[64];
    size_t record_size = 0u;

    if(expect_true(ns_mpsc_record_ring_init(&ring, storage, NS_CAPACITY_128) == NS_OK) != 0) return 1;
    if(expect_true(ns_mpsc_record_ring_try_push(&ring, "abcdef", 6u) == NS_OK) != 0) return 1;

    /* first attempt: buffer too small */
    if(expect_true(ns_mpsc_record_ring_try_pop(&ring, out, 3u, &record_size) == NS_E_NOMEM) != 0) return 1;
    if(expect_true(record_size == 6u) != 0) return 1;

    /* retry: record is still there and intact */
    if(expect_true(ns_mpsc_record_ring_try_pop(&ring, out, sizeof(out), &record_size) == NS_OK) != 0) return 1;
    if(expect_true(record_size == 6u) != 0) return 1;
    if(expect_true(memcmp(out, "abcdef", 6u) == 0) != 0) return 1;

    return 0;
}

/* ------------------------------------------------------------------ */
/*  10. Push/pop position tracking and free_capacity                   */
/* ------------------------------------------------------------------ */

static int test_push_pop_positions(void)
{
    size_t storage[NS_CAPACITY_128 / sizeof(size_t)];
    ns_mpsc_record_ring_t ring;
    uint8_t out[64];
    size_t record_size = 0u;
    size_t cap;
    size_t stride_a;
    size_t stride_b;

    if(expect_true(ns_mpsc_record_ring_init(&ring, storage, NS_CAPACITY_128) == NS_OK) != 0) return 1;
    cap = ring.capacity;
    (void)ns_mpsc_record_ring_max_record_size(&ring);

    /* initial state */
    if(expect_true(ns_mpsc_record_ring_free_capacity(&ring) == cap) != 0) return 1;

    /* push "A" (1 byte) → free decreases by aligned header+1 */
    if(expect_true(ns_mpsc_record_ring_try_push(&ring, "A", 1u) == NS_OK) != 0) return 1;
    stride_a = ns_align_up(sizeof(size_t) + 1u, NS_MPSC_RECORD_RING_ALIGNMENT);
    if(expect_true(ns_mpsc_record_ring_free_capacity(&ring) == cap - stride_a) != 0) return 1;

    /* push "BC" (2 bytes) → free decreases again */
    if(expect_true(ns_mpsc_record_ring_try_push(&ring, "BC", 2u) == NS_OK) != 0) return 1;
    stride_b = ns_align_up(sizeof(size_t) + 2u, NS_MPSC_RECORD_RING_ALIGNMENT);
    if(expect_true(ns_mpsc_record_ring_free_capacity(&ring) == cap - stride_a - stride_b) != 0) return 1;

    /* pop "A" */
    if(expect_true(ns_mpsc_record_ring_try_pop(&ring, out, sizeof(out), &record_size) == NS_OK) != 0) return 1;
    if(expect_true(record_size == 1u) != 0) return 1;
    if(expect_true(out[0] == 'A') != 0) return 1;
    if(expect_true(ns_mpsc_record_ring_free_capacity(&ring) == cap - stride_b) != 0) return 1;

    /* pop "BC" */
    if(expect_true(ns_mpsc_record_ring_try_pop(&ring, out, sizeof(out), &record_size) == NS_OK) != 0) return 1;
    if(expect_true(record_size == 2u) != 0) return 1;
    if(expect_true(memcmp(out, "BC", 2u) == 0) != 0) return 1;
    if(expect_true(ns_mpsc_record_ring_free_capacity(&ring) == cap) != 0) return 1;

    /* fill and drain the whole ring */
    {
        size_t i;
        size_t stride_1 = ns_align_up(sizeof(size_t) + 1u, NS_MPSC_RECORD_RING_ALIGNMENT);
        size_t count = cap / stride_1;

        for(i = 0u; i < count; ++i){
            if(expect_true(ns_mpsc_record_ring_try_push(&ring, "x", 1u) == NS_OK) != 0) return 1;
        }
        if(expect_true(ns_mpsc_record_ring_free_capacity(&ring) == 0u) != 0) return 1;
        if(expect_true(ns_mpsc_record_ring_try_push(&ring, "y", 1u) == NS_E_QUEUE_FULL) != 0) return 1;

        for(i = 0u; i < count; ++i){
            if(expect_true(ns_mpsc_record_ring_try_pop(&ring, out, sizeof(out), &record_size) == NS_OK) != 0) return 1;
            if(expect_true(record_size == 1u) != 0) return 1;
        }
        if(expect_true(ns_mpsc_record_ring_free_capacity(&ring) == cap) != 0) return 1;
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/*  11. Multi-producer concurrency (shared infrastructure)             */
/* ------------------------------------------------------------------ */

enum {
    MP_PAYLOAD_VARIABLE = 0,  /* 1..53 bytes per record (from test_payload_size) */
    MP_PAYLOAD_FIXED    = 1   /* fixed size from ctx->fixed_payload_size */
};

static void producer_run(producer_ctx_t *ctx)
{
    while(ctx->sequence < ctx->count){
        uint8_t payload_data[MAX_PAYLOAD_SIZE];
        test_record_header_t header;
        size_t payload_size;
        ns_mpsc_record_part_t parts[2];
        int rc;

        if(ctx->payload_mode == MP_PAYLOAD_FIXED){
            payload_size = ctx->fixed_payload_size;
        }else{
            payload_size = test_payload_size(ctx->producer_id, ctx->sequence);
        }

        header.producer_id = ctx->producer_id;
        header.sequence = ctx->sequence;
        header.payload_size = (uint32_t)payload_size;
        header.checksum = 0u;

        if(payload_size != 0u){
            test_fill_payload(payload_data, payload_size, ctx->producer_id, ctx->sequence);
        }
        {
            uint8_t record[RECORD_BUF_SIZE];

            memcpy(record, &header, sizeof(header));
            if(payload_size != 0u){
                memcpy(&record[sizeof(header)], payload_data, payload_size);
            }
            header.checksum = test_checksum_bytes(record, sizeof(header) + payload_size);
        }

        parts[0].data = &header;
        parts[0].size = sizeof(header);
        parts[1].data = payload_data;
        parts[1].size = payload_size;

        do{
            rc = ns_mpsc_record_ring_try_pushv(ctx->ring, parts, 2u);
            if(rc == NS_E_QUEUE_FULL) test_yield();
        }while(rc == NS_E_QUEUE_FULL);

        if(rc != NS_OK){
            ctx->failed = 1;
            break;
        }

        ++ctx->sequence;
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

#define MP_MAX_PRODUCERS 8

/**
 * @brief Generic multi-producer test driver.
 *
 * Starts @p producer_count producers, each pushing @p per_producer_count
 * records into @p ring.  The consumer pops all records and validates:
 *   - checksum integrity
 *   - no duplicates (per-producer seen[][] matrix)
 *   - per-producer strict ordering (expected_seq[])
 *   - completeness
 */
static int run_mp_test(
    ns_mpsc_record_ring_t *ring,
    uint32_t producer_count,
    uint32_t per_producer_count,
    int payload_mode,
    size_t fixed_payload_size)
{
    producer_ctx_t producers[MP_MAX_PRODUCERS];
    uint8_t seen[MP_MAX_PRODUCERS][512];  /* max per_producer_count for these tests */
    uint32_t expected_seq[MP_MAX_PRODUCERS];
    uint32_t started = 0u;
    uint32_t popped_count = 0u;
    uint32_t total_count;
    uint32_t i;
    int start_failed = 0;

    if(producer_count > MP_MAX_PRODUCERS || per_producer_count > 512u) return 1;

    memset(producers, 0, sizeof(producers));
    memset(seen, 0, sizeof(seen));
    for(i = 0u; i < producer_count; ++i) expected_seq[i] = 0u;

    for(i = 0u; i < producer_count; ++i){
        producers[i].ring = ring;
        producers[i].producer_id = i;
        producers[i].sequence = 0u;
        producers[i].count = per_producer_count;
        producers[i].failed = 0;
        producers[i].payload_mode = payload_mode;
        producers[i].fixed_payload_size = fixed_payload_size;
        ns_atomic_init(&producers[i].done, 0);
        if(producer_start(&producers[i]) != 0){
            start_failed = 1;
            break;
        }
        ++started;
    }

    total_count = started * per_producer_count;

    if(start_failed != 0){
        for(i = 0u; i < started; ++i) producer_join(&producers[i]);
        return 1;
    }

    /* Consumer loop */
    while(popped_count < total_count){
        uint8_t out[RECORD_BUF_SIZE];
        test_record_header_t header;
        size_t record_size = 0u;
        int rc;

        rc = ns_mpsc_record_ring_try_pop(ring, out, sizeof(out), &record_size);
        if(rc == NS_E_EMPTY){
            test_yield();
            continue;
        }
        if(rc != NS_OK) return 1;

        /* Verify record integrity — use _ex variant for fixed mode
           so the expected payload size matches what the producer pushed */
        if(payload_mode == MP_PAYLOAD_FIXED){
            if(test_popped_record_matches_ex(out, record_size, fixed_payload_size) != 0) return 1;
        }else{
            if(test_popped_record_matches(out, record_size) != 0) return 1;
        }
        memcpy(&header, out, sizeof(header));
        if(header.producer_id >= producer_count) return 1;
        if(header.sequence >= per_producer_count) return 1;

        /* Per-producer ordering */
        if(header.sequence != expected_seq[header.producer_id]) return 1;
        expected_seq[header.producer_id] = header.sequence + 1;

        /* No duplicates */
        if(seen[header.producer_id][header.sequence] != 0u) return 1;
        seen[header.producer_id][header.sequence] = 1u;
        ++popped_count;
    }

    for(i = 0u; i < started; ++i){
        if(producer_join(&producers[i]) != 0) return 1;
        if(producers[i].failed != 0) return 1;
    }

    if(expect_true(started == producer_count) != 0) return 1;
    if(expect_true(popped_count == (producer_count * per_producer_count)) != 0) return 1;

    /* Completeness */
    for(i = 0u; i < producer_count; ++i){
        uint32_t seq;

        if(expect_true(expected_seq[i] == per_producer_count) != 0) return 1;
        for(seq = 0u; seq < per_producer_count; ++seq){
            if(expect_true(seen[i][seq] == 1u) != 0) return 1;
        }
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/*  11a. 4 producers × 512, variable payload (1..53 bytes)             */
/* ------------------------------------------------------------------ */

static int test_mp_4p_variable(void)
{
    size_t storage[NS_CAPACITY_1024 / sizeof(size_t)];
    ns_mpsc_record_ring_t ring;

    if(expect_true(ns_mpsc_record_ring_init(&ring, storage, NS_CAPACITY_1024) == NS_OK) != 0) return 1;
    return run_mp_test(&ring, 4, 512, MP_PAYLOAD_VARIABLE, 0u);
}

/* ------------------------------------------------------------------ */
/*  11b. 1 producer × 512, no contention (MPSC baseline)               */
/* ------------------------------------------------------------------ */

static int test_mp_1p_baseline(void)
{
    size_t storage[NS_CAPACITY_1024 / sizeof(size_t)];
    ns_mpsc_record_ring_t ring;

    if(expect_true(ns_mpsc_record_ring_init(&ring, storage, NS_CAPACITY_1024) == NS_OK) != 0) return 1;
    return run_mp_test(&ring, 1, 512, MP_PAYLOAD_VARIABLE, 0u);
}

/* ------------------------------------------------------------------ */
/*  11c. 2 producers × 256, tiny queue (heavy CAS contention)          */
/* ------------------------------------------------------------------ */

static int test_mp_2p_tiny_queue(void)
{
    size_t storage[NS_CAPACITY_128 / sizeof(size_t)];
    ns_mpsc_record_ring_t ring;

    if(expect_true(ns_mpsc_record_ring_init(&ring, storage, NS_CAPACITY_128) == NS_OK) != 0) return 1;
    return run_mp_test(&ring, 2, 256, MP_PAYLOAD_VARIABLE, 0u);
}

/* ------------------------------------------------------------------ */
/*  11d. 8 producers × 256, high thread count                          */
/* ------------------------------------------------------------------ */

static int test_mp_8p_high_contention(void)
{
    size_t storage[NS_CAPACITY_1024 / sizeof(size_t)];
    ns_mpsc_record_ring_t ring;

    if(expect_true(ns_mpsc_record_ring_init(&ring, storage, NS_CAPACITY_1024) == NS_OK) != 0) return 1;
    return run_mp_test(&ring, 8, 256, MP_PAYLOAD_VARIABLE, 0u);
}

/* ------------------------------------------------------------------ */
/*  11e. 4 producers × 512, fixed payload (identical stride)           */
/* ------------------------------------------------------------------ */

static int test_mp_4p_fixed_payload(void)
{
    size_t storage[NS_CAPACITY_1024 / sizeof(size_t)];
    ns_mpsc_record_ring_t ring;

    if(expect_true(ns_mpsc_record_ring_init(&ring, storage, NS_CAPACITY_1024) == NS_OK) != 0) return 1;
    /* 16-byte payload → total_size = align_up(8+16) = 24, all same stride */
    return run_mp_test(&ring, 4, 512, MP_PAYLOAD_FIXED, 16u);
}

/* ------------------------------------------------------------------ */
/*  11f. 4 producers × 512, minimal fixed payload (1 byte)             */
/* ------------------------------------------------------------------ */

static int test_mp_4p_min_payload(void)
{
    size_t storage[NS_CAPACITY_1024 / sizeof(size_t)];
    ns_mpsc_record_ring_t ring;

    if(expect_true(ns_mpsc_record_ring_init(&ring, storage, NS_CAPACITY_1024) == NS_OK) != 0) return 1;
    /* 1-byte payload → smallest non-trivial record, maximizes record count */
    return run_mp_test(&ring, 4, 512, MP_PAYLOAD_FIXED, 1u);
}

/* ------------------------------------------------------------------ */
/*  11g. Sequential: producer 0 finishes, then producer 1 starts       */
/* ------------------------------------------------------------------ */

static int test_mp_sequential(void)
{
    size_t storage[NS_CAPACITY_1024 / sizeof(size_t)];
    ns_mpsc_record_ring_t ring;

    if(expect_true(ns_mpsc_record_ring_init(&ring, storage, NS_CAPACITY_1024) == NS_OK) != 0) return 1;

    /* Producer 0: 512 records */
    if(run_mp_test(&ring, 1, 512, MP_PAYLOAD_VARIABLE, 0u) != 0) return 1;
    /* Producer 1: another 512 records on the same (now-empty) ring */
    if(run_mp_test(&ring, 1, 512, MP_PAYLOAD_VARIABLE, 0u) != 0) return 1;

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */


int main(void)
{
    test_fill_global_payload();
    if(test_invalid_accessors() != 0) return 1;
    if(test_invalid_args() != 0) return 1;
    if(test_single_thread_paths() != 0) return 1;
    if(test_full_queue() != 0) return 1;
    if(test_padding_and_data_wrap() != 0) return 1;
    if(test_reserved_slot_does_not_replay_stale_record() != 0) return 1;
    if(test_wrap_record() != 0) return 1;
    if(test_zero_size_record() != 0) return 1;
    if(test_pop_nomem_retry() != 0) return 1;
    if(test_push_pop_positions() != 0) return 1;

    /* Multi-producer variants */
    if(test_mp_1p_baseline() != 0) return 1;
    if(test_mp_4p_variable() != 0) return 1;
    if(test_mp_2p_tiny_queue() != 0) return 1;
    if(test_mp_8p_high_contention() != 0) return 1;
    if(test_mp_4p_fixed_payload() != 0) return 1;
    if(test_mp_4p_min_payload() != 0) return 1;
    if(test_mp_sequential() != 0) return 1;

    return 0;
}
