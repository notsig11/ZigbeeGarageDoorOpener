#pragma once

// Host stub of FreeRTOS timers.h. Timers are recorded but not actually scheduled.

#include "FreeRTOS.h"
#include "host_fakes.hpp"

typedef void* TimerHandle_t;
typedef void (*TimerCallback_t)(TimerHandle_t);

inline TimerHandle_t xTimerCreate(const char* name, TickType_t period, UBaseType_t auto_reload,
                                   void* id, TimerCallback_t callback) {
    auto& s = host_fake::state();
    s.events.push_back({host_fake::EventKind::TimerCreate, static_cast<int64_t>(period), 0});
    (void)name;
    (void)auto_reload;
    (void)id;
    (void)callback;
    if (s.timer_create_fails) {
        return nullptr;
    }
    return reinterpret_cast<TimerHandle_t>(0x1);
}

inline BaseType_t xTimerStart(TimerHandle_t timer, TickType_t ticks_to_wait) {
    host_fake::state().events.push_back({host_fake::EventKind::TimerStart, reinterpret_cast<int64_t>(timer), 0});
    (void)ticks_to_wait;
    return pdPASS;
}

inline BaseType_t xTimerStop(TimerHandle_t timer, TickType_t ticks_to_wait) {
    host_fake::state().events.push_back({host_fake::EventKind::TimerStop, reinterpret_cast<int64_t>(timer), 0});
    (void)ticks_to_wait;
    return pdPASS;
}

inline BaseType_t xTimerChangePeriod(TimerHandle_t timer, TickType_t period, TickType_t ticks_to_wait) {
    host_fake::state().events.push_back({host_fake::EventKind::TimerChangePeriod, reinterpret_cast<int64_t>(timer), static_cast<int64_t>(period)});
    (void)ticks_to_wait;
    return pdPASS;
}
