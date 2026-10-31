#pragma once

// Shared recording state for the host-side ESP-IDF stubs. Tests call host_fake::state().reset()
// before each case and inspect the recorded events afterwards.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace host_fake {

enum class EventKind { GpioSetLevel, GpioConfig, TaskCreate, TaskDelay };

struct Event {
    EventKind kind;
    int64_t a;  // GpioSetLevel: pin | TaskDelay: ticks | GpioConfig: unused | TaskCreate: unused
    int64_t b;  // GpioSetLevel: level
};

struct GpioConfigRecord {
    uint64_t pin_bit_mask{0};
    int mode{-1};
    int pull_up_en{-1};
    int pull_down_en{-1};
    int intr_type{-1};
};

struct PendingTask {
    void (*fn)(void*);
    void* params;
};

struct State {
    std::vector<Event> events;
    // Tasks created but not run inline; run_pending_tasks() executes them so they self-clean.
    std::vector<PendingTask> pending_tasks;

    // Behaviour switches.
    int gpio_config_result{0};       // value returned by gpio_config()
    bool task_create_fails{false};   // xTaskCreate returns errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY
    bool run_tasks_inline{true};     // run the task function synchronously inside xTaskCreate

    // Recorded details.
    GpioConfigRecord last_gpio_config;
    int task_create_calls{0};
    std::string last_task_name;
    uint32_t last_task_stack{0};
    unsigned last_task_priority{0};
    int task_delete_calls{0};

    void run_pending_tasks() {
        for (auto& t : pending_tasks) {
            t.fn(t.params);
        }
        pending_tasks.clear();
    }

    void reset() {
        run_pending_tasks();
        *this = State{};
    }
};

inline State& state() {
    static State s;
    return s;
}

} // namespace host_fake
