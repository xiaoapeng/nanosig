/* MCXN947 single-core FreeRTOS hardware regression; replaces app/task_main.c. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "fsl_device_registers.h"
#include "FreeRTOS.h"
#include "task.h"
#include "SEGGER_RTT.h"
#include "task_main.h"

#include <nanosig/nanosig_port.h>

#define ns_mpsc_record_ring_init hw_ring_init
#define ns_mpsc_record_ring_capacity hw_ring_capacity
#define ns_mpsc_record_ring_free_capacity hw_ring_free_capacity
#define ns_mpsc_record_ring_max_record_size hw_ring_max_record_size
#define ns_mpsc_record_ring_try_push hw_ring_try_push
#define ns_mpsc_record_ring_try_pushv hw_ring_try_pushv
#define ns_mpsc_record_ring_try_acquire hw_ring_try_acquire
#define ns_mpsc_record_ring_release hw_ring_release
#include <nanosig/nanosig_mpsc_record_ring.h>

static bool hw_reserve_cas(atomic_size_t *ptr, size_t *expected,
    size_t desired, ns_memory_order_t success, ns_memory_order_t failure);
#undef ns_atomic_compare_exchange_weak_explicit
#define ns_atomic_compare_exchange_weak_explicit hw_reserve_cas

/* Add the nanosig source root to the app include directories. */
#include "src/ds/ns_mpsc_record_ring.c"

/* Public symbols retained in the ELF for J-Link memory inspection. */
volatile struct hw_mpsc_status {
    uint32_t magic;
    uint32_t stage;
    uint32_t checks;
    uint32_t failures;
    uint32_t round;
    uint32_t high_runs;
    uint32_t injections;
    uint32_t low_result;
    uint32_t high_result;
    uint32_t basepri_at_busy;
    uint32_t pending_at_busy;
    uint32_t high_saw_busy;
    uint32_t high_sleeping;
    uint32_t tick_before;
    uint32_t tick_after;
    uint32_t failure_line;
    uint32_t nested_mask;
    uint32_t restored_mask;
    uint32_t full_result;
    uint32_t systick_priority;
} g_hw_mpsc;

static ns_mpsc_record_ring_t ring;
static size_t storage[128u / sizeof(size_t)];
static TaskHandle_t high_task_handle;
static volatile uint32_t inject_armed;
static const uint32_t low_value = UINT32_C(0x4c4f5701);
static const uint32_t high_value = UINT32_C(0x48494748);

static void halt_failure(unsigned int line)
{
    char message[80];
    int length;

    ++g_hw_mpsc.failures;
    g_hw_mpsc.failure_line = line;
    g_hw_mpsc.stage = UINT32_C(0xf0000000) | line;
    length = snprintf(message, sizeof(message), "MPSC FAIL line=%u checks=%lu\n",
                      line, (unsigned long)g_hw_mpsc.checks);
    if(length > 0) (void)SEGGER_RTT_Write(0, message, (unsigned int)length);
    for(;;) vTaskDelay(portMAX_DELAY);
}

#define HW_CHECK(expression) do { \
    ++g_hw_mpsc.checks; \
    if(!(expression)) halt_failure((unsigned int)__LINE__); \
} while(0)

static bool hw_reserve_cas(atomic_size_t *ptr, size_t *expected,
    size_t desired, ns_memory_order_t success, ns_memory_order_t failure)
{
    bool acquired = atomic_compare_exchange_strong_explicit(ptr, expected, desired,
        (memory_order)success, (memory_order)failure);

    if(acquired && inject_armed != 0u){
        inject_armed = 0u;
        g_hw_mpsc.stage = 2u;
        ++g_hw_mpsc.injections;
        g_hw_mpsc.basepri_at_busy = __get_BASEPRI();
        /* High task is delayed by one tick. Pend a real SysTick while busy.
           The fixed guard must defer its delivery until busy has been cleared. */
        SCB->ICSR = SCB_ICSR_PENDSTSET_Msk;
        __DSB();
        __ISB();
        for(unsigned int i = 0u; i < 32u; ++i) __NOP();
        g_hw_mpsc.pending_at_busy = SCB->ICSR & SCB_ICSR_PENDSTSET_Msk;
    }
    return acquired;
}

static void high_task(void *argument)
{
    (void)argument;
    for(;;){
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        g_hw_mpsc.high_sleeping = 1u;
        vTaskDelay((TickType_t)1u);
        g_hw_mpsc.high_sleeping = 0u;
        g_hw_mpsc.stage = 3u;
        g_hw_mpsc.high_saw_busy = (uint32_t)(
            ns_atomic_load_explicit(&ring.write_pos, ns_memory_order_acquire) & 1u);
        if(g_hw_mpsc.high_saw_busy != 0u){
            (void)SEGGER_RTT_WriteString(0, "MPSC HIGH entered while busy\n");
        }
        /* On the old source, this call spins forever and g_hw_mpsc.stage stays 3. */
        g_hw_mpsc.high_result = (uint32_t)hw_ring_try_push(&ring, &high_value, sizeof(high_value));
        ++g_hw_mpsc.high_runs;
    }
}

static void expect_u32_record(uint32_t expected)
{
    void *record = NULL;
    size_t size = 0u;
    uint32_t actual = 0u;

    HW_CHECK(hw_ring_try_acquire(&ring, &record, &size) == NS_OK);
    HW_CHECK(size == sizeof(actual));
    memcpy(&actual, record, sizeof(actual));
    HW_CHECK(actual == expected);
    HW_CHECK(hw_ring_release(&ring, record) == NS_OK);
}

static void test_forced_preemption(void)
{
    void *record = NULL;
    size_t size = 0u;
    uint32_t previous_runs = g_hw_mpsc.high_runs;
    uint32_t saved_systick = SysTick->CTRL;

    HW_CHECK(hw_ring_init(&ring, storage, NS_CAPACITY_128) == NS_OK);
    /* Only stop periodic tick requests. PendSV can still run the high task,
       which calls vTaskDelay(1) before control returns here. */
    SysTick->CTRL = saved_systick & ~SysTick_CTRL_TICKINT_Msk;
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
    __DSB();
    __ISB();
    g_hw_mpsc.high_result = UINT32_MAX;
    g_hw_mpsc.high_saw_busy = UINT32_MAX;
    g_hw_mpsc.high_sleeping = 0u;
    xTaskNotifyGive(high_task_handle);
    HW_CHECK(g_hw_mpsc.high_sleeping == 1u);
    g_hw_mpsc.tick_before = (uint32_t)xTaskGetTickCount();
    inject_armed = 1u;
    g_hw_mpsc.low_result = (uint32_t)hw_ring_try_push(&ring, &low_value, sizeof(low_value));
    /* Tick and high task must have run at the guard's exit, before L returns. */
    SysTick->CTRL = saved_systick;
    __DSB();
    __ISB();
    g_hw_mpsc.tick_after = (uint32_t)xTaskGetTickCount();
    HW_CHECK(inject_armed == 0u);
    HW_CHECK(g_hw_mpsc.low_result == (uint32_t)NS_OK);
    HW_CHECK(g_hw_mpsc.high_result == (uint32_t)NS_OK);
    HW_CHECK(g_hw_mpsc.high_runs == previous_runs + 1u);
    HW_CHECK(g_hw_mpsc.high_saw_busy == 0u);
    HW_CHECK(g_hw_mpsc.basepri_at_busy != 0u);
    HW_CHECK(g_hw_mpsc.pending_at_busy != 0u);
    HW_CHECK(g_hw_mpsc.tick_after != g_hw_mpsc.tick_before);
    expect_u32_record(low_value);
    expect_u32_record(high_value);
    HW_CHECK(hw_ring_try_acquire(&ring, &record, &size) == NS_E_EMPTY);
    HW_CHECK(hw_ring_free_capacity(&ring) == ring.capacity);
    g_hw_mpsc.stage = 4u;
}

static void test_nested_and_full(void)
{
    unsigned char payload[64u - sizeof(size_t)];
    ns_platform_preempt_state_t outer;
    ns_platform_preempt_state_t inner;
    uint32_t before = __get_BASEPRI();
    uint32_t during;
    uint32_t after_inner;
    uint32_t after_pair;

    memset(payload, 0x42, sizeof(payload));
    HW_CHECK(hw_ring_init(&ring, storage, NS_CAPACITY_128) == NS_OK);
    HW_CHECK(hw_ring_try_push(&ring, payload, sizeof(payload)) == NS_OK);
    HW_CHECK(hw_ring_try_push(&ring, payload, sizeof(payload)) == NS_OK);
    taskENTER_CRITICAL();
    during = __get_BASEPRI();
    outer = ns_platform_preempt_enter();
    inner = ns_platform_preempt_enter();
    ns_platform_preempt_leave(inner);
    after_inner = __get_BASEPRI();
    ns_platform_preempt_leave(outer);
    after_pair = __get_BASEPRI();
    g_hw_mpsc.full_result = (uint32_t)hw_ring_try_push(&ring, payload, sizeof(payload));
    g_hw_mpsc.nested_mask = __get_BASEPRI();
    taskEXIT_CRITICAL();
    g_hw_mpsc.restored_mask = __get_BASEPRI();
    HW_CHECK(during != 0u);
    HW_CHECK(after_inner == during && after_pair == during);
    HW_CHECK(g_hw_mpsc.full_result == (uint32_t)NS_E_QUEUE_FULL);
    HW_CHECK(g_hw_mpsc.nested_mask == during);
    HW_CHECK(g_hw_mpsc.restored_mask == before);
    HW_CHECK((ns_atomic_load_explicit(&ring.write_pos, ns_memory_order_acquire) & 1u) == 0u);
}

void main_task(void *argument)
{
    char message[128];
    int length;
    (void)argument;

    g_hw_mpsc.magic = UINT32_C(0x4d505343);
    g_hw_mpsc.stage = 1u;
    g_hw_mpsc.systick_priority = NVIC_GetPriority(SysTick_IRQn);
    (void)SEGGER_RTT_WriteString(0, "MPSC hardware begin\n");
    HW_CHECK(configMAX_PRIORITIES > 3);
    HW_CHECK(xTaskCreate(high_task, "mpsc-high", configMINIMAL_STACK_SIZE * 4u,
                        NULL, 3u, &high_task_handle) == pdPASS);
    test_nested_and_full();
    for(uint32_t round = 0u; round < 128u; ++round){
        g_hw_mpsc.round = round;
        test_forced_preemption();
    }
    g_hw_mpsc.stage = 5u;
    length = snprintf(message, sizeof(message), "MPSC PASS rounds=%lu checks=%lu high=%lu irq=%lu\n",
        (unsigned long)(g_hw_mpsc.round + 1u), (unsigned long)g_hw_mpsc.checks,
        (unsigned long)g_hw_mpsc.high_runs, (unsigned long)g_hw_mpsc.injections);
    if(length > 0) (void)SEGGER_RTT_Write(0, message, (unsigned int)length);
    for(;;) vTaskDelay(portMAX_DELAY);
}

void vAssertCalled(const char *file, unsigned long line)
{
    char message[128];
    int length = snprintf(message, sizeof(message), "ASSERT %s:%lu\n", file, line);
    g_hw_mpsc.stage = UINT32_C(0xe0000001);
    g_hw_mpsc.failure_line = (uint32_t)line;
    if(length >= (int)sizeof(message)) length = (int)sizeof(message) - 1;
    if(length > 0) (void)SEGGER_RTT_Write(0, message, (unsigned int)length);
    taskDISABLE_INTERRUPTS();
    for(;;) __NOP();
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;
    (void)name;
    g_hw_mpsc.stage = UINT32_C(0xe0000002);
    (void)SEGGER_RTT_WriteString(0, "MPSC STACK OVERFLOW\n");
    taskDISABLE_INTERRUPTS();
    for(;;) __NOP();
}

void vApplicationMallocFailedHook(void)
{
    g_hw_mpsc.stage = UINT32_C(0xe0000003);
    (void)SEGGER_RTT_WriteString(0, "MPSC MALLOC FAILED\n");
    taskDISABLE_INTERRUPTS();
    for(;;) __NOP();
}
