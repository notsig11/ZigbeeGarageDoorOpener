#pragma once

// Host stub of ESP-IDF driver/gpio.h. Calls are recorded in host_fake::state().

#include <cstdint>

#include "esp_err.h"
#include "host_fakes.hpp"

typedef enum : int {
    GPIO_NUM_NC = -1,
    GPIO_NUM_0 = 0,
    GPIO_NUM_1,
    GPIO_NUM_2,
    GPIO_NUM_3,
    GPIO_NUM_4,
    GPIO_NUM_5,
    GPIO_NUM_6,
    GPIO_NUM_7,
    GPIO_NUM_8,
    GPIO_NUM_9,
    GPIO_NUM_10,
    GPIO_NUM_11,
    GPIO_NUM_12,
    GPIO_NUM_13,
    GPIO_NUM_14,
    GPIO_NUM_15,
    GPIO_NUM_16,
    GPIO_NUM_17,
    GPIO_NUM_18,
    GPIO_NUM_19,
    GPIO_NUM_20,
    GPIO_NUM_21,
    GPIO_NUM_22,
    GPIO_NUM_23,
} gpio_num_t;

typedef enum { GPIO_MODE_DISABLE = 0, GPIO_MODE_INPUT = 1, GPIO_MODE_OUTPUT = 2 } gpio_mode_t;
typedef enum { GPIO_PULLUP_DISABLE = 0, GPIO_PULLUP_ENABLE = 1 } gpio_pullup_t;
typedef enum { GPIO_PULLDOWN_DISABLE = 0, GPIO_PULLDOWN_ENABLE = 1 } gpio_pulldown_t;
typedef enum { GPIO_INTR_DISABLE = 0 } gpio_int_type_t;

typedef struct {
    uint64_t pin_bit_mask;
    gpio_mode_t mode;
    gpio_pullup_t pull_up_en;
    gpio_pulldown_t pull_down_en;
    gpio_int_type_t intr_type;
} gpio_config_t;

inline esp_err_t gpio_set_level(gpio_num_t pin, uint32_t level) {
    host_fake::state().events.push_back({host_fake::EventKind::GpioSetLevel, pin, level});
    return ESP_OK;
}

inline uint32_t gpio_get_level(gpio_num_t pin) {
    // Return the simulated level stored in host_fake state for this pin.
    // Default to 1 (pull-up high) if not explicitly set.
    return host_fake::state().gpio_levels[pin];
}

inline esp_err_t gpio_config(const gpio_config_t* cfg) {
    auto& s = host_fake::state();
    s.events.push_back({host_fake::EventKind::GpioConfig, 0, 0});
    s.last_gpio_config = {cfg->pin_bit_mask, cfg->mode, cfg->pull_up_en, cfg->pull_down_en, cfg->intr_type};
    return s.gpio_config_result;
}
