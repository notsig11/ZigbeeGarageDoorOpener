#pragma once

// Host stub of FreeRTOS task.h. Tasks run synchronously inside xTaskCreate by default,
// and delays are recorded instead of slept.

#include "FreeRTOS.h"
#include "host_fakes.hpp"

typedef void (*TaskFunction_t)(void*);
typedef void* TaskHandle_t;

inline BaseType_t xTaskCreate(TaskFunction_t fn, const char* name, uint32_t stack, void* params,
                              UBaseType_t priority, TaskHandle_t* handle) {
    auto& s = host_fake::state();
    ++s.task_create_calls;
    s.last_task_name = name ? name : "";
    s.last_task_stack = stack;
    s.last_task_priority = priority;
    s.events.push_back({host_fake::EventKind::TaskCreate, 0, 0});
    if (handle) {
        *handle = nullptr;
    }
    if (s.task_create_fails) {
        return errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY;
    }
    if (s.run_tasks_inline) {
        fn(params);
    }
    return pdPASS;
}

inline void vTaskDelay(TickType_t ticks) {
    host_fake::state().events.push_back({host_fake::EventKind::TaskDelay, ticks, 0});
}

inline void vTaskDelete(TaskHandle_t) {
    ++host_fake::state().task_delete_calls;
}
