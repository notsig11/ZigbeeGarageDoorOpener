#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "RelayController.hpp"
#include "ZigbeeGarageDoorOpener.hpp"

namespace {
constexpr const char* TAG = "Main";

// Map a 0-based relay index to its corresponding Kconfig GPIO pin value.
constexpr gpio_num_t relay_pin_for(int index) {
    switch (index) {
        case 0: return static_cast<gpio_num_t>(CONFIG_GARAGE_RELAY_PIN_1);
#if CONFIG_GARAGE_RELAY_COUNT >= 2
        case 1: return static_cast<gpio_num_t>(CONFIG_GARAGE_RELAY_PIN_2);
#endif
#if CONFIG_GARAGE_RELAY_COUNT >= 3
        case 2: return static_cast<gpio_num_t>(CONFIG_GARAGE_RELAY_PIN_3);
#endif
#if CONFIG_GARAGE_RELAY_COUNT >= 4
        case 3: return static_cast<gpio_num_t>(CONFIG_GARAGE_RELAY_PIN_4);
#endif
#if CONFIG_GARAGE_RELAY_COUNT >= 5
        case 4: return static_cast<gpio_num_t>(CONFIG_GARAGE_RELAY_PIN_5);
#endif
#if CONFIG_GARAGE_RELAY_COUNT >= 6
        case 5: return static_cast<gpio_num_t>(CONFIG_GARAGE_RELAY_PIN_6);
#endif
#if CONFIG_GARAGE_RELAY_COUNT >= 7
        case 6: return static_cast<gpio_num_t>(CONFIG_GARAGE_RELAY_PIN_7);
#endif
#if CONFIG_GARAGE_RELAY_COUNT >= 8
        case 7: return static_cast<gpio_num_t>(CONFIG_GARAGE_RELAY_PIN_8);
#endif
        default: return GPIO_NUM_NC;
    }
}

// Build the relay pin array from Kconfig values based on relay count.
template <int N>
constexpr std::array<gpio_num_t, N> get_relay_pins() {
    std::array<gpio_num_t, N> pins{};
    for (int i = 0; i < N; ++i) {
        pins[i] = relay_pin_for(i);
    }
    return pins;
}

using GarageRelays = RelayController<CONFIG_GARAGE_RELAY_COUNT>;
using ZigbeeService = ZigbeeGarageDoorOpener<GarageRelays>;

// Enables the external antenna switch: GPIO 3 low, GPIO 14 high.
void enable_external_antenna() noexcept {
    gpio_config_t ant_cfg{};
    ant_cfg.pin_bit_mask = (1ULL << GPIO_NUM_3) | (1ULL << GPIO_NUM_14);
    ant_cfg.mode = GPIO_MODE_OUTPUT;
    ant_cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    ant_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    ant_cfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&ant_cfg);
    gpio_set_level(GPIO_NUM_3, 0);
    gpio_set_level(GPIO_NUM_14, 1);

    // Google says that the RF stack resets this... I'm not so sure.
    gpio_hold_en(GPIO_NUM_3);
    gpio_hold_en(GPIO_NUM_14);
    ESP_LOGI(TAG, "External antenna enabled (GPIO 3 low, GPIO 14 high)");
}

// // Status LED task: solid on when disconnected from coordinator, flashes when relay is active, off otherwise.
// void led_status_task(void*) noexcept {
//     constexpr uint32_t poll_interval_ms = 125;
//     bool led_on = false;
//
//     while (true) {
//         if (!ZigbeeService::is_joined()) {
//             // Disconnected — keep LED solid on
//             gpio_set_level(GPIO_NUM_15, 1);
//         } else if (GarageRelays::any_relay_active()) {
//             // Relay active — flash the LED
//             gpio_set_level(GPIO_NUM_15, led_on ? 1 : 0);
//             led_on = !led_on;
//         } else {
//             // Connected and idle — LED off
//             gpio_set_level(GPIO_NUM_15, 0);
//         }
//         vTaskDelay(pdMS_TO_TICKS(poll_interval_ms));
//     }
// }

// Initialize the status LED on GPIO15 and start the indicator task.
// void init_status_led() noexcept {
//     gpio_config_t led_cfg{};
//     led_cfg.pin_bit_mask = (1ULL << GPIO_NUM_15);
//     led_cfg.mode = GPIO_MODE_OUTPUT;
//     led_cfg.pull_up_en = GPIO_PULLUP_DISABLE;
//     led_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
//     led_cfg.intr_type = GPIO_INTR_DISABLE;
//     gpio_config(&led_cfg);
//     gpio_set_level(GPIO_NUM_15, 0);
//
//     BaseType_t res = xTaskCreate(led_status_task, "led_status", 1024, nullptr, 1, nullptr);
//     if (res != pdPASS) {
//         ESP_LOGE(TAG, "Failed to create status LED task");
//     } else {
//         ESP_LOGI(TAG, "Status LED initialized on GPIO 15");
//     }
// }

extern "C" void app_main() noexcept {
    ESP_LOGI(TAG, "=== ESP32-C6 Zigbee Garage Door Opener Initializing ===");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    GarageRelays::Config relay_config{
        .pins = get_relay_pins<CONFIG_GARAGE_RELAY_COUNT>(),
        .pulse_duration = std::chrono::milliseconds{CONFIG_GARAGE_RELAY_PULSE_DURATION_MS},
        .active_level = CONFIG_GARAGE_RELAY_ACTIVE_LEVEL
    };
    ESP_ERROR_CHECK(GarageRelays::init(relay_config));

    enable_external_antenna();

    // init_status_led();

    ZigbeeService::Config zb_config{
        .base_endpoint = 1,
        .primary_channel_mask = CONFIG_GARAGE_ZB_PRIMARY_CHANNEL_MASK,
        .secondary_channel_mask = CONFIG_GARAGE_ZB_SECONDARY_CHANNEL_MASK,
        .steering_retry_interval = std::chrono::milliseconds{3000}
    };

    ret = ZigbeeService::start(zb_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Zigbee Garage Door Opener service: %s", esp_err_to_name(ret));
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart();
        return;
    }

    ESP_LOGI(TAG, "=== Garage Door Opener running with %zu relay channel(s) ===", GarageRelays::relay_count());
}
} // anonymous namespace
