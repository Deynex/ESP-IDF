#pragma once
#include <stdint.h>
#include <WinSock2.h>
#include <Windows.h>
typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef unsigned UBaseType_t;
typedef HANDLE TaskHandle_t;
#define pdMS_TO_TICKS(x) ((TickType_t)(x))
#define pdPASS 1
#define pdFALSE 0
#define portMAX_DELAY UINT32_MAX
