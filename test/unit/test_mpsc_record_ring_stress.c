/**
 * @file test_mpsc_record_ring_stress.c
 * @brief 8-producer / 1-consumer stress test (20 minutes by default).
 *
 * NOT part of the normal test suite. Run manually:
 *   clang -I include -I . -O2 -o build/stress.exe \
 *       test/unit/test_mpsc_record_ring_stress.c src/ds/ns_mpsc_record_ring.c
 *   NS_MPSC_RECORD_RING_STRESS_DURATION_SEC=5 ./build/stress.exe
 */

#include <nanosig/nanosig_mpsc_record_ring.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__unix__) || defined(__linux__) || defined(__APPLE__)
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#else
#error "stress test requires pthreads or Win32 threads"
#endif

/* ------------------------------------------------------------------ */
/*  Configuration                                                      */
/* ------------------------------------------------------------------ */

#define PRODUCER_COUNT     8
#define RING_CAPACITY      NS_CAPACITY_4096
#define DEFAULT_STRESS_DURATION_S  1200   /* 20 minutes */
#define REPORT_INTERVAL_S  10

static int stress_duration_seconds(void)
{
#if defined(_WIN32)
    char *env = NULL;
    size_t env_size = 0u;
    errno_t env_rc;
#else
    const char *env = getenv("NS_MPSC_RECORD_RING_STRESS_DURATION_SEC");
#endif
    char *end = NULL;
    long value;

#if defined(_WIN32)
    env_rc = _dupenv_s(&env, &env_size, "NS_MPSC_RECORD_RING_STRESS_DURATION_SEC");
    (void)env_size;
    if(env_rc != 0 || env == NULL || env[0] == '\0'){
        free(env);
        return DEFAULT_STRESS_DURATION_S;
    }
#else
    if(env == NULL || env[0] == '\0') return DEFAULT_STRESS_DURATION_S;
#endif

    value = strtol(env, &end, 10);
    if(end == env || *end != '\0' || value <= 0 || value > 86400L){
#if defined(_WIN32)
        free(env);
#endif
        return DEFAULT_STRESS_DURATION_S;
    }
#if defined(_WIN32)
    free(env);
#endif
    return (int)value;
}

/* ------------------------------------------------------------------ */
/*  Payload                                                            */
/* ------------------------------------------------------------------ */

#define PAYLOAD_MIN  1u
#define PAYLOAD_MAX  200u

static size_t stress_payload_size(uint32_t pid, uint32_t seq)
{
    return PAYLOAD_MIN + (size_t)(((pid * 17u) + (seq * 7u)) % (PAYLOAD_MAX - PAYLOAD_MIN + 1u));
}

static void stress_fill_payload(uint8_t *buf, size_t size, uint32_t pid, uint32_t seq)
{
    size_t i;
    for(i = 0u; i < size; ++i){
        uint32_t v = (pid * 131u) ^ (seq * 977u) ^ (uint32_t)(i * 29u);
        buf[i] = (uint8_t)(v ^ (v >> 8u) ^ (v >> 17u));
    }
}

/* ------------------------------------------------------------------ */
/*  Cross-platform threading                                           */
/* ------------------------------------------------------------------ */

typedef struct {
    ns_mpsc_record_ring_t *ring;
    uint32_t id;
    volatile int stop;
    uint64_t pushed;
    int failed;
    atomic_int done;
#if defined(_WIN32)
    HANDLE thread;
#else
    pthread_t thread;
#endif
} stress_producer_t;

typedef struct {
    ns_mpsc_record_ring_t *ring;
    volatile int stop;
    uint64_t popped;
    int failed;
#if defined(_WIN32)
    HANDLE thread;
#else
    pthread_t thread;
#endif
} stress_consumer_t;

static void thread_yield(void)
{
#if defined(_WIN32)
    SwitchToThread();
#else
    sched_yield();
#endif
}

#if defined(_WIN32)
static DWORD WINAPI stress_producer_fn(LPVOID arg)
#else
static void *stress_producer_fn(void *arg)
#endif
{
    stress_producer_t *ctx = (stress_producer_t *)arg;
    uint32_t seq = 0u;

    while(!ctx->stop){
        size_t psz = stress_payload_size(ctx->id, seq);
        uint8_t payload[PAYLOAD_MAX];
        uint8_t hdr[8]; /* 4-byte pid + 4-byte seq */
        ns_mpsc_record_part_t parts[2];
        int rc;

        memcpy(hdr, &ctx->id, 4);
        memcpy(hdr + 4, &seq, 4);
        stress_fill_payload(payload, psz, ctx->id, seq);

        parts[0].data = hdr;
        parts[0].size = 8u;
        parts[1].data = payload;
        parts[1].size = psz;

        do{
            if(ctx->stop) goto done;
            rc = ns_mpsc_record_ring_try_pushv(ctx->ring, parts, 2u);
            if(rc == NS_E_QUEUE_FULL) thread_yield();
            if(rc != NS_OK && rc != NS_E_QUEUE_FULL){
                ctx->failed = 1;
#if defined(_WIN32)
                return 0u;
#else
                return NULL;
#endif
            }
        }while(rc == NS_E_QUEUE_FULL);

        ++ctx->pushed;
        ++seq;
    }

done:
    ns_atomic_store_explicit(&ctx->done, 1, ns_memory_order_release);
#if defined(_WIN32)
    return 0u;
#else
    return NULL;
#endif
}

#if defined(_WIN32)
static DWORD WINAPI stress_consumer_fn(LPVOID arg)
#else
static void *stress_consumer_fn(void *arg)
#endif
{
    stress_consumer_t *ctx = (stress_consumer_t *)arg;
    uint32_t expected_seq[PRODUCER_COUNT];

    memset(expected_seq, 0, sizeof(expected_seq));

    while(!ctx->stop){
        void *record = NULL;
        size_t record_size = 0u;
        size_t payload_size;
        size_t i;
        uint32_t pid, seq;
        int rc;

        rc = ns_mpsc_record_ring_try_acquire(ctx->ring, &record, &record_size);
        if(rc == NS_E_EMPTY){
            thread_yield();
            if(ctx->stop) break;
            continue;
        }
        if(rc != NS_OK){
            ctx->failed = 1;
#if defined(_WIN32)
            return 0u;
#else
            return NULL;
#endif
        }

        if(record_size < 8u){
            ctx->failed = 1;
            (void)ns_mpsc_record_ring_release(ctx->ring, record);
#if defined(_WIN32)
            return 0u;
#else
            return NULL;
#endif
        }

        memcpy(&pid, record, 4);
        memcpy(&seq, (uint8_t *)record + 4, 4);

        if(pid >= PRODUCER_COUNT){
            ctx->failed = 1;
            (void)ns_mpsc_record_ring_release(ctx->ring, record);
#if defined(_WIN32)
            return 0u;
#else
            return NULL;
#endif
        }

        payload_size = stress_payload_size(pid, seq);
        if(record_size != (8u + payload_size)){
            fprintf(stderr, "SIZE VIOLATION: pid=%u seq=%u expected_size=%u got=%u\n",
                    pid, seq, (unsigned int)(8u + payload_size), (unsigned int)record_size);
            ctx->failed = 1;
            (void)ns_mpsc_record_ring_release(ctx->ring, record);
#if defined(_WIN32)
            return 0u;
#else
            return NULL;
#endif
        }

        for(i = 0u; i < payload_size; ++i){
            uint32_t v = (pid * 131u) ^ (seq * 977u) ^ (uint32_t)(i * 29u);
            uint8_t expected = (uint8_t)(v ^ (v >> 8u) ^ (v >> 17u));
            if(((uint8_t *)record)[8u + i] != expected){
                fprintf(stderr, "PAYLOAD VIOLATION: pid=%u seq=%u offset=%u\n",
                        pid, seq, (unsigned int)i);
                ctx->failed = 1;
                (void)ns_mpsc_record_ring_release(ctx->ring, record);
#if defined(_WIN32)
                return 0u;
#else
                return NULL;
#endif
            }
        }

        if(seq != expected_seq[pid]){
            fprintf(stderr, "ORDER VIOLATION: pid=%u expected_seq=%u got=%u\n",
                    pid, expected_seq[pid], seq);
            ctx->failed = 1;
            (void)ns_mpsc_record_ring_release(ctx->ring, record);
#if defined(_WIN32)
            return 0u;
#else
            return NULL;
#endif
        }

        expected_seq[pid] = seq + 1u;
        ++ctx->popped;
        if(ns_mpsc_record_ring_release(ctx->ring, record) != NS_OK){
            ctx->failed = 1;
#if defined(_WIN32)
            return 0u;
#else
            return NULL;
#endif
        }
    }

#if defined(_WIN32)
    return 0u;
#else
    return NULL;
#endif
}

static int start_producer(stress_producer_t *p)
{
#if defined(_WIN32)
    p->thread = CreateThread(NULL, 0u, stress_producer_fn, p, 0u, NULL);
    return p->thread != NULL ? 0 : 1;
#else
    return pthread_create(&p->thread, NULL, stress_producer_fn, p) == 0 ? 0 : 1;
#endif
}

static int start_consumer(stress_consumer_t *c)
{
#if defined(_WIN32)
    c->thread = CreateThread(NULL, 0u, stress_consumer_fn, c, 0u, NULL);
    return c->thread != NULL ? 0 : 1;
#else
    return pthread_create(&c->thread, NULL, stress_consumer_fn, c) == 0 ? 0 : 1;
#endif
}

static void join_thread_handle(
#if defined(_WIN32)
    HANDLE h
#else
    pthread_t t
#endif
)
{
#if defined(_WIN32)
    WaitForSingleObject(h, INFINITE);
    CloseHandle(h);
#else
    pthread_join(t, NULL);
#endif
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */

int main(void)
{
    size_t storage[RING_CAPACITY / sizeof(size_t)];
    ns_mpsc_record_ring_t ring;
    stress_producer_t producers[PRODUCER_COUNT];
    stress_consumer_t consumer;
    time_t start, now, last_report;
    uint64_t last_pushed, last_popped;
    uint32_t i;
    int stress_duration_s = stress_duration_seconds();
    int rc = 0;

    if(ns_mpsc_record_ring_init(&ring, storage, RING_CAPACITY) != NS_OK){
        fprintf(stderr, "FAIL: ring init\n");
        return 1;
    }

    memset(producers, 0, sizeof(producers));
    memset(&consumer, 0, sizeof(consumer));

    consumer.ring = &ring;
    consumer.stop = 0;

    for(i = 0u; i < PRODUCER_COUNT; ++i){
        producers[i].ring = &ring;
        producers[i].id = i;
        producers[i].stop = 0;
        ns_atomic_init(&producers[i].done, 0);
    }

    fprintf(stderr, "Stress test: %d producers, 1 consumer, %d seconds, ring=%d\n",
            PRODUCER_COUNT, stress_duration_s, (int)RING_CAPACITY);

    /* Start consumer first */
    if(start_consumer(&consumer) != 0){
        fprintf(stderr, "FAIL: consumer start\n");
        return 1;
    }

    /* Start producers */
    for(i = 0u; i < PRODUCER_COUNT; ++i){
        if(start_producer(&producers[i]) != 0){
            fprintf(stderr, "FAIL: producer %u start\n", i);
            /* Signal already-started producers to stop */
            uint32_t j;
            for(j = 0u; j < i; ++j) producers[j].stop = 1;
            consumer.stop = 1;
            for(j = 0u; j < i; ++j) join_thread_handle(producers[j].thread);
            join_thread_handle(consumer.thread);
            return 1;
        }
    }

    /* Monitor loop */
    start = time(NULL);
    last_report = start;
    last_pushed = 0u;
    last_popped = 0u;

    for(;;){
        uint64_t total_pushed = 0u;
        uint64_t total_popped;

        now = time(NULL);
        if((uintmax_t)(now - start) >= (uintmax_t)stress_duration_s) break;

        if((uintmax_t)(now - last_report) >= (uintmax_t)REPORT_INTERVAL_S){
            for(i = 0u; i < PRODUCER_COUNT; ++i){
                if(producers[i].failed){
                    fprintf(stderr, "FAIL: producer %u failed\n", i);
                    rc = 1;
                    goto stop;
                }
                total_pushed += producers[i].pushed;
            }
            total_popped = consumer.popped;
            if(consumer.failed){
                fprintf(stderr, "FAIL: consumer failed\n");
                rc = 1;
                goto stop;
            }

            {
                double elapsed = (double)(now - start);
                double push_rate = (double)(total_pushed - last_pushed) / (double)REPORT_INTERVAL_S;
                double pop_rate = (double)(total_popped - last_popped) / (double)REPORT_INTERVAL_S;

                fprintf(stderr, "  [%5.0fs] pushed=%" "llu" " popped=%" "llu" " (+%.0f/+%.0f rec/s)\n",
                        elapsed,
                        (unsigned long long)total_pushed,
                        (unsigned long long)total_popped,
                        push_rate, pop_rate);
            }

            last_report = now;
            last_pushed = total_pushed;
            last_popped = total_popped;
        }

#if defined(_WIN32)
        Sleep(1000u);
#else
        sleep(1u);
#endif
    }

stop:
    /* Signal all threads to stop */
    for(i = 0u; i < PRODUCER_COUNT; ++i) producers[i].stop = 1;
    consumer.stop = 1;

    /* Join all threads */
    for(i = 0u; i < PRODUCER_COUNT; ++i) join_thread_handle(producers[i].thread);
    join_thread_handle(consumer.thread);

    /* Final report */
    {
        uint64_t total_pushed = 0u;
        uint64_t total_popped = consumer.popped;
        int any_failed = consumer.failed;

        for(i = 0u; i < PRODUCER_COUNT; ++i){
            total_pushed += producers[i].pushed;
            if(producers[i].failed) any_failed = 1;
        }

        fprintf(stderr, "\n=== RESULT ===\n");
        fprintf(stderr, "Duration:  %d seconds\n", (int)(now - start));
        fprintf(stderr, "Pushed:    %" "llu" " records\n", (unsigned long long)total_pushed);
        fprintf(stderr, "Popped:    %" "llu" " records\n", (unsigned long long)total_popped);
        fprintf(stderr, "Status:    %s\n", any_failed ? "FAIL" : "PASS");

        if(rc == 0 && any_failed) rc = 1;
    }

    return rc;
}
