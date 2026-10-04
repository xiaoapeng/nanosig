/**
 * @file test_freertos_broker.c
 * @brief FreeRTOS broker ↔ queue-set integration smoke test (host POSIX harness).
 * @date 2026-10-04
 *
 * 验证事件 broker 在 FreeRTOS 后端上的端到端路径：ns_init 启动 broker 线程，
 * watcher 的 waitable 被注册进 queue set，signal 触发后 broker 选中该 token、
 * 调用 consume_fn 消费、再移除并优雅关闭。断言在 FreeRTOS 任务中执行。
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#include <stdio.h>
#include <stdlib.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include <nanosig/nanosig.h>
#include <nanosig/nanosig_port.h>

static int g_failures;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if(!(cond)){                                                       \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);\
            fflush(stderr);                                                \
            g_failures++;                                                  \
        }                                                                  \
    } while(0)

static volatile int g_consumed;
static SemaphoreHandle_t g_broker_sem;

/* consume_fn 等价于桌面后端的 read(fd)：非阻塞取走队列集成员的就绪状态。 */
static int broker_consume(ns_watcher_t *watcher)
{
    (void)watcher;
    (void)xSemaphoreTake(g_broker_sem, 0);
    g_consumed = 1;
    return 1;
}

static void test_broker_roundtrip(void)
{
    ns_platform_event_t ev;
    ns_watcher_t watcher;
    ns_waitable_handle_t h;
    size_t heap_before;
    int i;

    /* 取基线前先让 idle 任务回收上一轮可能残留的已删除任务。 */
    vTaskDelay(pdMS_TO_TICKS(5));
    heap_before = xPortGetFreeHeapSize();

    CHECK(ns_init() == NS_OK);

    CHECK(ns_platform_event_init(&ev, "broker-ev") == NS_OK);
    g_broker_sem = (SemaphoreHandle_t)ev.waitable.primitive.handle;
    g_consumed = 0;

    h = NS_WAITABLE_GET(&ev.waitable);
    CHECK(ns_watcher_init(&watcher, h, NS_WAITABLE_EVENT_IN, 0, broker_consume) == NS_OK);
    CHECK(ns_broker_add(&watcher) == NS_OK);

    CHECK(ns_platform_event_signal(&ev) == NS_OK);

    for(i = 0; (i < 2000) && (g_consumed == 0); i++){
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    CHECK(g_consumed != 0);

    CHECK(ns_broker_remove(&watcher) == NS_OK);
    CHECK(ns_watcher_deinit(&watcher) == NS_OK);
    CHECK(ns_platform_event_deinit(&ev) == NS_OK);

    CHECK(ns_shutdown() == NS_OK);

    /* ns_shutdown 后 broker 任务已自删，等 idle 任务回收其 TCB/栈，断言无泄漏。 */
    for(i = 0; (i < 500) && (xPortGetFreeHeapSize() != heap_before); i++){
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    CHECK(xPortGetFreeHeapSize() == heap_before);
}

static void runner_task(void *arg)
{
    (void)arg;

    test_broker_roundtrip();

    fflush(NULL);
    _Exit(g_failures != 0 ? 1 : 0);
}

int main(void)
{
    /* 跨 TU 平台 ABI 探针：消费者必须与库选到同一平台分支，否则链接期失败。 */
    if(NS_PLATFORM_ABI_PROBE != 0){
        fprintf(stderr, "platform ABI probe mismatch\n");
        return 2;
    }

    if(xTaskCreate(runner_task, "runner", 4096, NULL, 2, NULL) != pdPASS){
        fprintf(stderr, "failed to create runner task\n");
        return 2;
    }

    vTaskStartScheduler();

    return 3; /* 不可达：runner 用 _Exit 结束进程 */
}
