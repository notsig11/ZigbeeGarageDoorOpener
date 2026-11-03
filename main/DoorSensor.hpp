#pragma once

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

/**
 * @brief Configurable Door Sensor controller for magnetic reed switch input detection.
 *
 * Each sensor pin is configured as a GPIO input with pull-up. The sensor state is read
 * synchronously: when the reed switch closes (door closed), the pin reads low (active-low).
 *
 * @tparam SensorCount Number of configurable sensor channels.
 */
template <std::size_t SensorCount = 0>
requires (SensorCount >= 0)
class DoorSensor {
public:
    static constexpr std::size_t count = SensorCount;

    struct Config {
        std::array<gpio_num_t, SensorCount> pins;
        uint32_t active_level{0};  // 0 for active-low (reed switch pulls to GND), 1 for active-high
    };

private:
    static constexpr const char* TAG = "DoorSensor";

    // Populated exclusively by init(); no defaults. All access is guarded by initialized_.
    static inline Config config_{};
    static inline std::atomic<bool> initialized_{false};

public:
    /**
     * @brief Initializes the GPIO configuration for all configured sensor pins.
     * @param cfg Explicit configuration for sensor pins and active level.
     */
    static esp_err_t init(const Config& cfg) noexcept {
        config_ = cfg;
        initialized_ = false;
        const esp_err_t err = init_hardware();
        initialized_ = (err == ESP_OK);
        return err;
    }

    /**
     * @brief Returns the number of sensors.
     */
    [[nodiscard]] static constexpr std::size_t sensor_count() noexcept {
        return SensorCount;
    }

    /**
     * @brief Returns a read-only span of the configured GPIO pins.
     */
    [[nodiscard]] static std::span<const gpio_num_t, SensorCount> pins() noexcept {
        return config_.pins;
    }

    /**
     * @brief Returns the GPIO pin for the given 1-based channel.
     */
    [[nodiscard]] static gpio_num_t get_pin(uint8_t channel) noexcept {
        if (!initialized_ || channel < 1 || channel > SensorCount) {
            return GPIO_NUM_NC;
        }
        return config_.pins[channel - 1];
    }

    /**
     * @brief Reads the current state of the specified sensor channel.
     * @param channel 1-based sensor channel number (1 .. SensorCount).
     * @return true if the sensor is active (reed switch closed / door closed), false otherwise.
     *         Returns false for invalid channels or if not initialized.
     */
    [[nodiscard]] static bool is_active(uint8_t channel) noexcept {
        if (!initialized_) {
            ESP_LOGE(TAG, "is_active called before init()");
            return false;
        }

        if (channel < 1 || channel > SensorCount) {
            ESP_LOGW(TAG, "Invalid sensor channel: %u (configured range: 1..%zu)", channel, SensorCount);
            return false;
        }

        const auto target_pin = config_.pins[channel - 1];
        if (target_pin == GPIO_NUM_NC) {
            return false;
        }

        const uint32_t level = gpio_get_level(target_pin);
        const bool active = (level == config_.active_level);

        ESP_LOGD(TAG, "Sensor %u (GPIO %d): level=%u, active=%d", channel, target_pin, level, active);
        return active;
    }

    /**
     * @brief Returns true if the door is considered closed based on the sensor state.
     * For a normally-open reed switch with active-low config, closed = active (switch closed).
     * @param channel 1-based sensor channel number (1 .. SensorCount).
     * @return true if the door is closed, false if open.
     */
    [[nodiscard]] static bool is_door_closed(uint8_t channel) noexcept {
        return is_active(channel);
    }

private:
    static esp_err_t init_hardware() noexcept {
        if (SensorCount == 0) {
            ESP_LOGI(TAG, "Door sensors disabled (count = 0)");
            return ESP_OK;
        }

        uint64_t pin_mask = 0;
        for (const auto pin : config_.pins) {
            if (pin != GPIO_NUM_NC) {
                pin_mask |= (1ULL << pin);
            }
        }

        const gpio_config_t io_conf{
            .pin_bit_mask = pin_mask,
            .mode         = GPIO_MODE_INPUT,
            .pull_up_en   = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };

        auto ret = gpio_config(&io_conf);

        ESP_LOGI(TAG, "Initialized %zu door sensor channel(s)", SensorCount);
        for (std::size_t i = 0; i < SensorCount; ++i) {
            ESP_LOGI(TAG, "  - Sensor %zu: GPIO %d", i + 1, config_.pins[i]);
        }

        return ret;
    }
};
