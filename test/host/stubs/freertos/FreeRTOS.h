#pragma once

// Host stub of FreeRTOS.h. Uses a 100 Hz tick (10 ms) so tick rounding is observable in tests.

#include <cstdint>

typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef unsigned UBaseType_t;

#define configTICK_RATE_HZ   100
#define configMAX_PRIORITIES 25

#define pdFALSE 0
#define pdTRUE  1
#define pdPASS  pdTRUE
#define pdFAIL  pdFALSE
#define errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY (-1)

#define pdMS_TO_TICKS(ms) (static_cast<TickType_t>((static_cast<uint64_t>(ms) * configTICK_RATE_HZ) / 1000))
