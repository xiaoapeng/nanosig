/*
 * FreeRTOSConfig.h — host POSIX harness configuration for the nanosig
 * FreeRTOS backend contract tests.
 *
 * This is NOT a target-shipping configuration. It exists only so the pinned
 * FreeRTOS-Kernel + GCC/Posix port can run on a development host and exercise
 * platform/freertos/port.c against real queue-set semantics.
 */

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <stdio.h>
#include <stdlib.h>

#define configUSE_PREEMPTION                     1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION  0
#define configUSE_TICKLESS_IDLE                  0
#define configCPU_CLOCK_HZ                       1000000
#define configTICK_RATE_HZ                       1000
#define configMAX_PRIORITIES                     8
#define configMINIMAL_STACK_SIZE                 1024
#define configMAX_TASK_NAME_LEN                  16
#define configUSE_16_BIT_TICKS                   0
#define configIDLE_SHOULD_YIELD                  1
#define configUSE_MUTEXES                        1
#define configUSE_RECURSIVE_MUTEXES              0
#define configUSE_COUNTING_SEMAPHORES            1
#define configUSE_QUEUE_SETS                     1
#define configUSE_TASK_NOTIFICATIONS             1
#define configUSE_TRACE_FACILITY                 0
#define configUSE_STATS_FORMATTING_FUNCTIONS     0
#define configSUPPORT_DYNAMIC_ALLOCATION         1
#define configSUPPORT_STATIC_ALLOCATION          0
#define configTOTAL_HEAP_SIZE                    (1024 * 1024)
#define configUSE_IDLE_HOOK                      0
#define configUSE_TICK_HOOK                      0
#define configCHECK_FOR_STACK_OVERFLOW           0
#define configUSE_MALLOC_FAILED_HOOK             0
#define configQUEUE_REGISTRY_SIZE                0
#define configUSE_TIMERS                         0
#define configUSE_TIME_SLICING                   1
#define configUSE_NEWLIB_REENTRANT               0
#define INCLUDE_vTaskDelay                       1
#define INCLUDE_vTaskDelete                      1
#define INCLUDE_xTaskGetCurrentTaskHandle        1
#define INCLUDE_uxTaskGetStackHighWaterMark      0

#define configASSERT(x)                                                    \
    do {                                                                   \
        if(!(x)){                                                          \
            fprintf(stderr, "FreeRTOS ASSERT %s:%d\n", __FILE__, __LINE__);\
            abort();                                                       \
        }                                                                  \
    } while(0)

#endif /* FREERTOS_CONFIG_H */
