#pragma once

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <span>
#include <concepts>
#include <new>

/**
 * @brief Configurable Relay Controller for momentary garage door button emulation or switch control.
 *
 * @tparam RelayCount Number of configurable relay channels.
 */
template <std::size_t RelayCount = 2>
requires (RelayCount > 0)
class RelayController {
public:
    static constexpr std::size_t count = RelayCount;

    struct Config {
        std::array<gpio_num_t, RelayCount> pins;
        std::chrono::milliseconds pulse_duration{300}; // Default 300ms momentary press
        uint32_t active_level{1};                      // 1 for active-high, 0 for active-low
    };

private:
    static constexpr const char* TAG = "RelayController";

    // Populated exclusively by init(); no defaults. All access is guarded by initialized_.
    static inline Config config_{};
    static inline bool initialized_{false};
    // Number of relay pulse tasks currently running.
    static inline std::atomic<int> active_pulses_{0};

    struct alignas(void*) PulseWorkerParams {
        gpio_num_t pin;
        uint32_t active_level;
        TickType_t duration_ticks;
    };

    // Worker task to generate a non-blocking momentary physical pulse
    static void pulse_worker(void* pvParameters) noexcept {
        auto* params = static_cast<PulseWorkerParams*>(pvParameters);
        if (!params) {
            vTaskDelete(nullptr);
            return;
        }

        const gpio_num_t pin = params->pin;
        const uint32_t active = params->active_level;
        const uint32_t inactive = (active == 1) ? 0 : 1;
        const TickType_t delay_ticks = params->duration_ticks;

        delete params;

        gpio_set_level(pin, active);
        vTaskDelay(delay_ticks);
        gpio_set_level(pin, inactive);

        active_pulses_--;

        vTaskDelete(nullptr);
    }

public:
    /**
     * @brief Initializes the GPIO configuration for all configured relay pins.
     * @param cfg Explicit configuration for relay pins, pulse duration, and active level.
     */
    static esp_err_t init(const Config &cfg) noexcept {
        config_ = cfg;
        initialized_ = false;
        const esp_err_t err = init_hardware();
        initialized_ = (err == ESP_OK);
        return err;
    }

    /**
     * @brief Returns the number of relays.
     */
    [[nodiscard]] static constexpr std::size_t relay_count() noexcept {
        return RelayCount;
    }

    /**
     * @brief Returns a read-only span of the configured GPIO pins.
     */
    [[nodiscard]] static std::span<const gpio_num_t, RelayCount> pins() noexcept {
        return config_.pins;
    }

    /**
     * @brief Returns the GPIO pin for the given 1-based channel.
     */
    [[nodiscard]] static gpio_num_t get_pin(uint8_t channel) noexcept {
        if (!initialized_ || channel < 1 || channel > RelayCount) {
            return GPIO_NUM_NC;
        }
        return config_.pins[channel - 1];
    }

    /**
     * @brief Returns true if any relay pulse is currently active.
     */
    [[nodiscard]] static bool any_relay_active() noexcept {
        return active_pulses_.load() > 0;
    }

    /**
     * @brief Triggers an isolated asynchronous momentary pulse on the requested channel.
     * @param channel 1-based relay channel number (1 .. RelayCount).
     * @return true if pulse task was dispatched, false otherwise.
     */
    [[nodiscard]] static bool trigger_pulse(uint8_t channel) noexcept {
        if (!initialized_) {
            ESP_LOGE(TAG, "trigger_pulse called before init()");
            return false;
        }

        if (channel < 1 || channel > RelayCount) {
            ESP_LOGW(TAG, "Invalid relay channel: %u (configured range: 1..%zu)", channel, RelayCount);
            return false;
        }

        const auto target_pin = config_.pins[channel - 1];
        const uint32_t inactive = (config_.active_level == 1) ? 0 : 1;
        const auto duration_ticks = pdMS_TO_TICKS(config_.pulse_duration.count());

        ESP_LOGI(TAG, "Triggering momentary pulse on Relay %u (GPIO %d) for %lld ms",
                 channel, target_pin, static_cast<long long>(config_.pulse_duration.count()));

        auto* params = new (std::nothrow) PulseWorkerParams{
            .pin = target_pin,
            .active_level = config_.active_level,
            .duration_ticks = (duration_ticks > 0) ? duration_ticks : 1
        };

        active_pulses_++;

        if (!params) {
            ESP_LOGE(TAG, "Out of memory allocating pulse worker params");
            // Fallback: synchronous pulse with short delay
            gpio_set_level(target_pin, config_.active_level);
            vTaskDelay(pdMS_TO_TICKS(50));
            gpio_set_level(target_pin, inactive);
            active_pulses_--;
            return false;
        }

        BaseType_t res = xTaskCreate(
            pulse_worker,
            "relay_pulse",
            2048,
            params,
            configMAX_PRIORITIES - 1,
            nullptr
        );

        if (res != pdPASS) {
            ESP_LOGE(TAG, "Failed to spawn pulse worker task for channel %u", channel);
            delete params;
            active_pulses_--;
            return false;
        }

        return true;
    }

private:
    static esp_err_t init_hardware() noexcept {
        uint64_t pin_mask = 0;
        for (const auto pin : config_.pins) {
            if (pin != GPIO_NUM_NC) {
                pin_mask |= (1ULL << pin);
            }
        }

        const gpio_config_t io_conf{
            .pin_bit_mask = pin_mask,
            .mode         = GPIO_MODE_OUTPUT,
            .pull_up_en   = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };

        auto ret = gpio_config(&io_conf);

        const uint32_t inactive = (config_.active_level == 1) ? 0 : 1;
        for (const auto pin : config_.pins) {
            if (pin != GPIO_NUM_NC) {
                gpio_set_level(pin, inactive);
            }
        }

        ESP_LOGI(TAG, "Initialized %zu relay channel(s)", RelayCount);
        for (std::size_t i = 0; i < RelayCount; ++i) {
            ESP_LOGI(TAG, "  - Relay %zu: GPIO %d", i + 1, config_.pins[i]);
        }

        return ret;
    }
};
