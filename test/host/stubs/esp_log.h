#pragma once

// Host stub of ESP-IDF esp_log.h. Logging is compiled out but format strings stay type-checked.

#include <cstdio>

#define HOST_LOG_(fmt, ...)                       \
    do {                                          \
        if (false) {                              \
            std::printf(fmt, ##__VA_ARGS__);      \
        }                                         \
    } while (false)

#define ESP_LOGE(tag, fmt, ...) HOST_LOG_(fmt, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) HOST_LOG_(fmt, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) HOST_LOG_(fmt, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) HOST_LOG_(fmt, ##__VA_ARGS__)
#define ESP_LOGV(tag, fmt, ...) HOST_LOG_(fmt, ##__VA_ARGS__)
