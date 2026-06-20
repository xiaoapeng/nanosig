/**
 * @file bench_common.h
 * @brief Benchmark 计时与统计公共头文件。
 */

#ifndef NANOSIG_BENCH_COMMON_H
#define NANOSIG_BENCH_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <time.h>
#endif

/* ------------------------------------------------------------------ */
/*  高精度计时                                                         */
/* ------------------------------------------------------------------ */

static inline uint64_t bench_now_ns(void)
{
#ifdef _WIN32
    static LARGE_INTEGER freq = {0};
    LARGE_INTEGER counter;

    if(freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return (uint64_t)(counter.QuadPart * 1000000000ULL / freq.QuadPart);
#else
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
#endif
}

/* ------------------------------------------------------------------ */
/*  样本采集与统计                                                       */
/* ------------------------------------------------------------------ */

typedef struct bench_stats {
    const char *name;
    uint64_t   *samples;
    size_t      count;
    size_t      capacity;
    uint64_t    total_ns;
} bench_stats_t;

static inline int bench_stats_init(bench_stats_t *s, const char *name, size_t capacity)
{
    s->name = name;
    s->samples = (uint64_t *)malloc(capacity * sizeof(uint64_t));
    if(s->samples == NULL) return -1;
    s->count = 0;
    s->capacity = capacity;
    s->total_ns = 0;
    return 0;
}

static inline void bench_stats_record(bench_stats_t *s, uint64_t elapsed_ns)
{
    if(s->count < s->capacity){
        s->samples[s->count] = elapsed_ns;
    }
    s->total_ns += elapsed_ns;
    s->count++;
}

static inline int bench_compare(const void *a, const void *b)
{
    uint64_t va = *(const uint64_t *)a;
    uint64_t vb = *(const uint64_t *)b;
    return (va > vb) ? 1 : (va < vb) ? -1 : 0;
}

static inline void bench_stats_report(const bench_stats_t *s)
{
    uint64_t min_ns, max_ns, avg_ns, p50_ns, p99_ns;

    if(s->count == 0u) return;

    /* sort samples for percentile */
    qsort(s->samples, s->count, sizeof(uint64_t), bench_compare);

    min_ns = s->samples[0];
    max_ns = s->samples[s->count - 1];
    avg_ns = s->total_ns / s->count;
    p50_ns = s->samples[(size_t)(s->count * 0.50)];
    p99_ns = s->samples[(size_t)(s->count * 0.99)];

    fprintf(stdout, "\n=== nanosig bench: %s ===\n", s->name);
    fprintf(stdout, "  iterations: %zu\n", s->count);
    fprintf(stdout, "  avg:  %5.2f ns  (%5.3f us)\n", (double)avg_ns, (double)avg_ns / 1000.0);
    fprintf(stdout, "  p50:  %5.2f ns  (%5.3f us)\n", (double)p50_ns, (double)p50_ns / 1000.0);
    fprintf(stdout, "  p99:  %5.2f ns  (%5.3f us)\n", (double)p99_ns, (double)p99_ns / 1000.0);
    fprintf(stdout, "  min:  %5.2f ns  (%5.3f us)\n", (double)min_ns, (double)min_ns / 1000.0);
    fprintf(stdout, "  max:  %5.2f ns  (%5.3f us)\n", (double)max_ns, (double)max_ns / 1000.0);
    fflush(stdout);
}

static inline void bench_stats_destroy(bench_stats_t *s)
{
    free(s->samples);
    s->samples = NULL;
    s->count = 0;
    s->capacity = 0;
}

/* ------------------------------------------------------------------ */
/*  预热辅助                                                           */
/* ------------------------------------------------------------------ */

#endif /* NANOSIG_BENCH_COMMON_H */
