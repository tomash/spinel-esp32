/* firmware-pico/config/FreeRTOSConfig.h -- FreeRTOS for spinel-mote on the
 * Pico 2 W. Based on pico-examples' FreeRTOSConfig_examples_common.h.
 *
 * Why an RTOS at all: lwIP's BSD socket API (what spinel's TCPServer and
 * the runtime's net/io code call, as on ESP-IDF) needs one. The Ruby program
 * runs in its own task (sp_pico_adapter.c); lwIP's tcpip thread and the
 * CYW43 driver run beside it.
 *
 * Both cores, SMP: the configuration Raspberry Pi's port and pico-examples
 * run (a single-core build of this SMP port died before USB came up). Memory: heap_3 (FreeRTOS-Kernel-Heap3) wraps malloc, so the
 * kernel, lwIP's tasks and the Ruby heap share newlib's one heap instead of
 * a fixed configTOTAL_HEAP_SIZE pool. */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#define configUSE_PREEMPTION                    1
#define configUSE_TICKLESS_IDLE                 0
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configTICK_RATE_HZ                      ( ( TickType_t ) 1000 )
#define configMAX_PRIORITIES                    32
#define configMINIMAL_STACK_SIZE                ( configSTACK_DEPTH_TYPE ) 512
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             1
#define configUSE_APPLICATION_TASK_TAG          0
#define configUSE_COUNTING_SEMAPHORES           1
#define configQUEUE_REGISTRY_SIZE               8
#define configUSE_QUEUE_SETS                    1
#define configUSE_TIME_SLICING                  1
#define configUSE_NEWLIB_REENTRANT              0
/* lwIP's FreeRTOS sys_arch still uses the old names */
#define configENABLE_BACKWARD_COMPATIBILITY     1
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS 5
#define configSTACK_DEPTH_TYPE                  uint32_t
#define configMESSAGE_BUFFER_LENGTH_TYPE        size_t

#define configSUPPORT_STATIC_ALLOCATION         0
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configTOTAL_HEAP_SIZE                   (16 * 1024)  /* unused with heap_3 */
#define configAPPLICATION_ALLOCATED_HEAP        0

#define configCHECK_FOR_STACK_OVERFLOW          2
#define configUSE_MALLOC_FAILED_HOOK            1
#define configUSE_DAEMON_TASK_STARTUP_HOOK      0

#define configGENERATE_RUN_TIME_STATS           0
#define configUSE_TRACE_FACILITY                1
#define configUSE_STATS_FORMATTING_FUNCTIONS    0

#define configUSE_CO_ROUTINES                   0
#define configMAX_CO_ROUTINE_PRIORITIES         1

#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               ( configMAX_PRIORITIES - 1 )
#define configTIMER_QUEUE_LENGTH                10
#define configTIMER_TASK_STACK_DEPTH            1024

#define configNUMBER_OF_CORES                   2
#define configTICK_CORE                         0
#define configRUN_MULTIPLE_PRIORITIES           1
#define configUSE_CORE_AFFINITY                 1
#define configUSE_PASSIVE_IDLE_HOOK             0

#define configSUPPORT_PICO_SYNC_INTEROP         1
#define configSUPPORT_PICO_TIME_INTEROP         1

#include <assert.h>
#define configASSERT(x)                         assert(x)

#define INCLUDE_vTaskPrioritySet                1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_vTaskDelayUntil                 1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define INCLUDE_uxTaskGetStackHighWaterMark     1
#define INCLUDE_xTaskGetIdleTaskHandle          1
#define INCLUDE_eTaskGetState                   1
#define INCLUDE_xTimerPendFunctionCall          1
#define INCLUDE_xTaskAbortDelay                 1
#define INCLUDE_xTaskGetHandle                  1
#define INCLUDE_xTaskResumeFromISR              1
#define INCLUDE_xQueueGetMutexHolder            1

#endif /* FREERTOS_CONFIG_H */
