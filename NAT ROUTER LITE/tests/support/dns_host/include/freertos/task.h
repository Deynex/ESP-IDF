#pragma once
#include "FreeRTOS.h"
#include <stdlib.h>
typedef struct { void (*fn)(void *); void *arg; } host_task_t;
static DWORD WINAPI host_task_entry(void *arg) {
    host_task_t task = *(host_task_t *)arg;
    free(arg);
    task.fn(task.arg);
    return 0;
}
static BaseType_t xTaskCreate(void (*fn)(void *), const char *name, uint32_t stack,
                             void *arg, UBaseType_t priority, TaskHandle_t *out) {
    (void)name; (void)stack; (void)priority;
    host_task_t *task = (host_task_t *)malloc(sizeof(*task));
    if (!task) return 0;
    task->fn = fn; task->arg = arg;
    HANDLE h = CreateThread(NULL, 0, host_task_entry, task, 0, NULL);
    if (!h) { free(task); return 0; }
    if (out) *out = h; else CloseHandle(h);
    return pdPASS;
}
static TickType_t xTaskGetTickCount(void) { return GetTickCount(); }
static void vTaskDelete(TaskHandle_t task) { (void)task; ExitThread(0); }
static void vTaskDelay(TickType_t delay) { Sleep(delay); }
