/**
 * @file main.c
 * @brief 裸机后端契约测试（host harness）。
 * @date 2026-10-31
 *
 * 用假时钟驱动，覆盖 consensus plan 的 AC4–AC9 与 AC15。
 * 本 harness 以 `-DNANOSIG_PLATFORM=baremetal` 构建，因此 `NANOSIG_BAREMETAL` 生效，
 * mutex/wakeup/alloc 等原语在编译期即不存在——这正是被测形态。
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <nanosig/nanosig.h>
#include <nanosig/ns_formatio.h>
#include <nanosig_baremetal_api.h>

#include "test_macros.h"

/* ================================================================== */
/*  墙钟（用于断言"不阻塞"；不能用 clock()——那是 CPU 时间，睡眠不涨）      */
/* ================================================================== */

static double wall_ms(void)
{
    struct timespec ts;

    (void)timespec_get(&ts, TIME_UTC);
    return ((double)ts.tv_sec * 1000.0) + ((double)ts.tv_nsec / 1000000.0);
}

/* ================================================================== */
/*  假时钟                                                              */
/* ================================================================== */

static ns_platform_time_us_t g_now_us = 0u;

static uint64_t fake_now_us(void)
{
    return (uint64_t)g_now_us;
}

static void clock_advance(ns_platform_time_us_t delta_us)
{
    g_now_us += delta_us;
}

/* ================================================================== */
/*  AC5：时钟是 ns_init 的硬前置                                         */
/* ================================================================== */

static int test_requires_clock_before_init(void)
{
    /* 未装时钟：ns_platform_init 返回 NS_E_INVAL，ns_init 随之失败。 */
    EXPECT_OK(ns_init() == NS_E_INVAL);

    ns_baremetal_set_now_us(fake_now_us);
    EXPECT_OK(ns_init() == NS_OK);
    EXPECT_OK(ns_is_initialized() == 1);

    return 0;
}

/* ================================================================== */
/*  AC5：无线程、无堆，init/shutdown 可往返                              */
/* ================================================================== */

static int test_init_shutdown_roundtrip(void)
{
    EXPECT_OK(ns_shutdown() == NS_OK);
    EXPECT_OK(ns_is_initialized() == 0);

    /* 二次 init 必须成功——裸机不残留 broker 线程或单例状态。 */
    EXPECT_OK(ns_init() == NS_OK);
    EXPECT_OK(ns_is_initialized() == 1);

    return 0;
}

/* ================================================================== */
/*  AC6：时钟直接透传，库内零换算                                       */
/* ================================================================== */

static int test_clock_passthrough(void)
{
    ns_platform_time_us_t now = 0u;

    g_now_us = 1234567u;
    EXPECT_OK(ns_platform_clock_monotonic_us(&now) == NS_OK);
    EXPECT_EQ(now, (ns_platform_time_us_t)1234567u);

    EXPECT_OK(ns_platform_clock_monotonic_us(NULL) == NS_E_INVAL);
    return 0;
}

/* ================================================================== */
/*  AC4：静态单例契约                                                   */
/* ================================================================== */

/* 裸机单例句柄：由 AC4 创建，后续用例复用（禁止创建第二个）。 */
static ns_loop_t *g_loop = NULL;

static int test_loop_singleton_contract(void)
{
    ns_loop_t *loop = NULL;
    ns_loop_t *loop2 = NULL;
    ns_loop_config_t cfg;

    /* (a) 首次 create 传非零 stack_size → 裸机无线程，显式拒绝。 */
    cfg = NS_LOOP_CONFIG_DEFAULT();
    cfg.stack_size = 4096u;
    EXPECT_OK(ns_loop_create(&loop, &cfg) == NS_E_INVAL);

    /* (b) 容量超过编译期内置值 → 拒绝，不静默缩容。 */
    cfg = NS_LOOP_CONFIG_DEFAULT();
    cfg.queue_byte_capacity = (ns_capacity_t)(NANOSIG_BAREMETAL_LOOP_QUEUE_BYTES * 2u);
    EXPECT_OK(ns_loop_create(&loop, &cfg) == NS_E_INVAL);

    /* (c) 默认配置成功。 */
    EXPECT_OK(ns_loop_create(&loop, NULL) == NS_OK);
    EXPECT_OK(loop != NULL);

    /* (d) 二次 create → NS_E_EXISTS（单例判定先于配置校验）。 */
    EXPECT_OK(ns_loop_create(&loop2, NULL) == NS_E_EXISTS);
    EXPECT_OK(loop2 == NULL);

    /* (e) destroy → NS_OK。 */
    EXPECT_OK(ns_loop_destroy(loop) == NS_OK);

    /* (f) 二次 destroy → NS_E_INVAL（不静默 double-destroy）。 */
    EXPECT_OK(ns_loop_destroy(loop) == NS_E_INVAL);

    /* (g) destroy 后 poll → NS_E_INVAL。 */
    EXPECT_OK(ns_loop_poll(loop, 0u) == NS_E_INVAL);
    EXPECT_OK(ns_loop_next_timeout(loop, &g_now_us) == NS_E_INVAL);

    /* (h) 可以重新创建。 */
    EXPECT_OK(ns_loop_create(&g_loop, NULL) == NS_OK);
    EXPECT_OK(g_loop != NULL);

    return 0;
}

/* ================================================================== */
/*  AC7 / AC9：timer 端到端 + poll 单轮语义                             */
/* ================================================================== */

static int g_timer_seen = 0;

static void timer_slot(void *user_data, const void *payload)
{
    (void)user_data;
    (void)payload;
    ++g_timer_seen;
}

static int test_timer_oneshot_real(void)
{
    ns_loop_t *loop = g_loop;
    ns_timer_t timer;
    ns_connection_t conn;

    g_timer_seen = 0;
    g_now_us = 1000000u;

    /* 复用 AC4 创建的单例——裸机禁止创建第二个。 */
    EXPECT_OK(loop != NULL);

    EXPECT_OK(ns_timer_init(&timer, 1000u, NS_TIMER_ATTR_ONESHOT) == NS_OK);
    EXPECT_OK(ns_signal_connect(&timer.signal, timer_slot, loop, NULL, &conn) == NS_OK);

    /* timer manager 必须在裸机 ns_init 中已初始化，否则此处返回 NS_E_SHUTDOWN。 */
    EXPECT_OK(ns_timer_start(&timer) == NS_OK);

    /* 到期前：不触发。 */
    clock_advance(999u);
    EXPECT_OK(ns_loop_poll(loop, 0u) == NS_OK);
    EXPECT_EQ(g_timer_seen, 0);

    /* 到期：同一次 poll 内触发恰好一次。 */
    clock_advance(1u);
    EXPECT_OK(ns_loop_poll(loop, 0u) == NS_OK);
    EXPECT_EQ(g_timer_seen, 1);

    /* 单次型：再推进也不再触发。 */
    clock_advance(10000u);
    EXPECT_OK(ns_loop_poll(loop, 0u) == NS_OK);
    EXPECT_EQ(g_timer_seen, 1);

    EXPECT_OK(ns_signal_disconnect(&conn) == NS_OK);
    EXPECT_OK(ns_timer_deinit(&timer) == NS_OK);

    return 0;
}

/* ================================================================== */
/*  AC8：异步派发语义                                                   */
/* ================================================================== */

static int g_signal_seen = 0;

static void signal_slot(void *user_data, const void *payload)
{
    (void)user_data;
    (void)payload;
    ++g_signal_seen;
}

static int test_signal_async_dispatch(void)
{
    ns_loop_t *loop = g_loop;
    ns_signal_t sig;
    ns_connection_t conn;
    int payload = 42;

    g_signal_seen = 0;

    EXPECT_OK(loop != NULL);
    EXPECT_OK(ns_signal_init_raw(&sig, sizeof(payload), 8u, "bm-signal") == NS_OK);
    EXPECT_OK(ns_signal_connect(&sig, signal_slot, loop, NULL, &conn) == NS_OK);

    /* emit 只入队：返回时回调尚未执行。 */
    EXPECT_OK(ns_signal_emit_raw(&sig, &payload, sizeof(payload)) == NS_OK);
    EXPECT_EQ(g_signal_seen, 0);

    /* 一次 poll 派发一次。 */
    EXPECT_OK(ns_loop_poll(loop, 0u) == NS_OK);
    EXPECT_EQ(g_signal_seen, 1);

    /* 两次 emit + 一次 poll → 两次回调。 */
    EXPECT_OK(ns_signal_emit_raw(&sig, &payload, sizeof(payload)) == NS_OK);
    EXPECT_OK(ns_signal_emit_raw(&sig, &payload, sizeof(payload)) == NS_OK);
    EXPECT_EQ(g_signal_seen, 1);
    EXPECT_OK(ns_loop_poll(loop, 0u) == NS_OK);
    EXPECT_EQ(g_signal_seen, 3);

    EXPECT_OK(ns_signal_disconnect(&conn) == NS_OK);
    EXPECT_OK(ns_signal_deinit_raw(&sig) == NS_OK);

    return 0;
}

/* ================================================================== */
/*  AC9：poll / next_timeout 错误码                                     */
/* ================================================================== */

static int test_poll_error_codes(void)
{
    ns_loop_t *loop = g_loop;
    ns_platform_time_us_t next = 0u;

    EXPECT_OK(loop != NULL);

    EXPECT_OK(ns_loop_poll(NULL, 0u) == NS_E_INVAL);
    EXPECT_OK(ns_loop_next_timeout(NULL, &next) == NS_E_INVAL);
    EXPECT_OK(ns_loop_next_timeout(loop, NULL) == NS_E_INVAL);

    /* 无已装载 timer → 哨兵值，而不是未初始化的输出。 */
    next = 0u;
    EXPECT_OK(ns_loop_next_timeout(loop, &next) == NS_OK);
    EXPECT_EQ(next, NS_PLATFORM_WAIT_INFINITE_US);

    /* 非零等待预算下也必须立即返回：库从不睡眠。 */
    {
        double t0 = wall_ms();
        double elapsed_ms;

        EXPECT_OK(ns_loop_poll(loop, 1000000u) == NS_OK);
        elapsed_ms = wall_ms() - t0;

        /* 给 1 秒预算；若实现真睡了，实测会接近 1000 ms。
           阈值取 100 ms：远低于 1 s，又给慢速 CI 留足余量。 */
        if(elapsed_ms >= 100.0){
            fprintf(stderr,
                    "ns_loop_poll(max_wait_us=1000000) blocked %.1f ms; "
                    "it must never sleep\n", elapsed_ms);
            return 1;
        }
    }

    return 0;
}

/* ================================================================== */
/*  AC15：formatio 溢出路径返回 0（可观测契约）                          */
/* ================================================================== */

static int test_formatio_overflow(void)
{
    char buf[64];

    /* 常规宽度：正常输出。 */
    (void)memset(buf, 0, sizeof(buf));
    EXPECT_OK(ns_snprintf(buf, sizeof(buf), "a=%llu;b", (unsigned long long)12345u) > 0);
    EXPECT_OK(strcmp(buf, "a=12345;b") == 0);

    /* 20 位 > FORMAT_STACK_CACHE_SIZE(16)：该项输出为空，但格式串其余部分仍打印。 */
    (void)memset(buf, 0, sizeof(buf));
    (void)ns_snprintf(buf, sizeof(buf), "a=%llu;b", (unsigned long long)ULLONG_MAX);
    EXPECT_OK(strcmp(buf, "a=;b") == 0);

    return 0;
}

/* ================================================================== */
/*  main                                                               */
/* ================================================================== */

int main(void)
{
    static const struct {
        const char *name;
        int (*fn)(void);
    } tests[] = {
        { "AC5 requires_clock_before_init", test_requires_clock_before_init },
        { "AC5 init_shutdown_roundtrip",    test_init_shutdown_roundtrip    },
        { "AC6 clock_passthrough",          test_clock_passthrough          },
        { "AC4 loop_singleton_contract",    test_loop_singleton_contract    },
        { "AC7 timer_oneshot",              test_timer_oneshot_real         },
        { "AC8 signal_async_dispatch",      test_signal_async_dispatch      },
        { "AC9 poll_error_codes",           test_poll_error_codes           },
        { "AC15 formatio_overflow",         test_formatio_overflow          },
    };
    size_t i;
    int failures = 0;

    for(i = 0; i < (sizeof(tests) / sizeof(tests[0])); i++){
        int rc = tests[i].fn();

        if(rc != 0){
            fprintf(stderr, "FAIL: %s\n", tests[i].name);
            ++failures;
        }else{
            printf("ok: %s\n", tests[i].name);
        }
    }

    if(failures != 0){
        fprintf(stderr, "\n%d test(s) failed\n", failures);
        return 1;
    }

    printf("\nall baremetal contract tests passed\n");
    return 0;
}
