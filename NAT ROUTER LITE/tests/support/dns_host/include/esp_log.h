#pragma once
#include <stdio.h>
#define HOST_LOG(tag, ...) do { fprintf(stderr, "%s ", tag); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } while (0)
#define ESP_LOGI(tag, ...) HOST_LOG(tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...) HOST_LOG(tag, __VA_ARGS__)
#define ESP_LOGE(tag, ...) HOST_LOG(tag, __VA_ARGS__)
