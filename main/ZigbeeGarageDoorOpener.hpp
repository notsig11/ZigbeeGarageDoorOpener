#pragma once

#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

// Zigbee Headers
#include "esp_zigbee.h"
#include "ezbee/nwk.h"
#include "ezbee/zha.h"
#include "ezbee/bdb.h"
#include "ezbee/zcl.h"
#include "ezbee/af.h"
#include "ezbee/app_signals.h"

#include "RelayController.hpp"
#include "ZigbeeHelpers.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string_view>

/**
 * @brief Concept verifying a valid RelayController implementation.
 */
template <typename T>
concept RelayControllerType = requires(uint8_t channel, const typename T::Config& cfg) {
    { T::relay_count() } -> std::same_as<std::size_t>;
    { T::trigger_pulse(channel) } -> std::same_as<bool>;
    { T::init(cfg) } -> std::same_as<esp_err_t>;
};

/**
 * @brief Zigbee Garage Door Opener service managing Zigbee coordinator pairing and relay endpoints.
 *
 * @tparam RelayCtrl RelayController class managing the physical GPIO relays.
 */
template <typename RelayCtrl = RelayController<2>>
requires RelayControllerType<RelayCtrl>
class ZigbeeGarageDoorOpener {
public:
    static constexpr uint8_t DEFAULT_BASE_ENDPOINT = 1;
    static constexpr uint8_t MAX_APPLICATION_ENDPOINT = zb::MAX_APPLICATION_ENDPOINT;
    static constexpr uint32_t ALL_CHANNELS_MASK = 0x07FFF800; // Channels 11-26

    struct Config {
        uint8_t base_endpoint{DEFAULT_BASE_ENDPOINT};
        uint32_t primary_channel_mask{ALL_CHANNELS_MASK};
        // Channels scanned when the primary set yields nothing. 0 means "same as primary_channel_mask",
        // i.e. never scan beyond the primary set.
        uint32_t secondary_channel_mask{0};
        std::chrono::milliseconds steering_retry_interval{3000};
        // Must refer to storage that outlives start() (e.g. string literals).
        std::string_view manufacturer_name{"Asphyxiation Organization"};
        std::string_view model_identifier{"GD001"};
    };

    // True when the device is joined to the Zigbee network; updated by the signal handler.
    // [[nodiscard]] static bool is_joined() noexcept {
    //     return joined_.load();
    // }

private:
    static constexpr const char* TAG = "ZigbeeGarageOpener";
    static constexpr std::size_t ZCL_STRING_MAX_LEN = zb::ZCL_STRING_MAX_LEN;
    static constexpr uint32_t ZB_LOCK_TIMEOUT_MS = 100;
    static constexpr int64_t FALLBACK_RETRY_MS = 1000;

    static inline Config config_{};
    static inline TimerHandle_t retry_timer_{nullptr};
    // BDB commissioning mode the retry timer re-runs when it fires.
    static inline std::atomic<ezb_bdb_comm_mode_mask_t> retry_mode_{EZB_BDB_MODE_NETWORK_STEERING};
//    static inline std::atomic<bool> joined_{false};


    // Adds a ZCL string attribute to the Basic cluster. Empty text is skipped.
    static void add_basic_string_attr(ezb_zcl_cluster_desc_t basic_cluster, uint16_t attr_id, std::string_view text) noexcept {
        if (text.empty()) {
            return;
        }
        const zb::ZclString zcl_str = zb::make_zcl_string(text);
        const ezb_err_t err = ezb_zcl_basic_cluster_desc_add_attr(basic_cluster, attr_id, zcl_str.data());
        if (err != EZB_ERR_NONE) {
            ESP_LOGW(TAG, "Failed to add Basic cluster attribute 0x%04x: %d", attr_id, static_cast<int>(err));
        }
    }

    // Maps a Zigbee endpoint to its 1-based relay channel, or nullopt if the endpoint is not ours.
    [[nodiscard]] static std::optional<uint8_t> endpoint_to_channel(uint8_t ep) noexcept {
        return zb::endpoint_to_channel(ep, config_.base_endpoint, RelayCtrl::relay_count());
    }

    // Records which commissioning mode to re-run and (re)starts the one-shot retry timer.
    static void schedule_retry(ezb_bdb_comm_mode_mask_t mode) noexcept {
        if (!retry_timer_) {
            return;
        }
        retry_mode_.store(mode);

        const auto interval_ms = config_.steering_retry_interval.count();
        const TickType_t ticks = pdMS_TO_TICKS(interval_ms > 0 ? interval_ms : FALLBACK_RETRY_MS);
        // xTimerChangePeriod also starts a dormant timer.
        if (xTimerChangePeriod(retry_timer_, ticks > 0 ? ticks : 1, 0) != pdPASS) {
            ESP_LOGE(TAG, "Failed to schedule commissioning retry (timer command queue full)");
        }
    }

    static void cancel_retry() noexcept {
        if (retry_timer_) {
            xTimerStop(retry_timer_, 0);
        }
    }

    // FreeRTOS Timer callback for delayed commissioning retry
    static void on_retry_timer(TimerHandle_t) noexcept {
        const ezb_bdb_comm_mode_mask_t mode = retry_mode_.load();

        // Bounded wait: this runs on the shared FreeRTOS timer service task.
        if (!esp_zigbee_lock_acquire(pdMS_TO_TICKS(ZB_LOCK_TIMEOUT_MS))) {
            ESP_LOGW(TAG, "Zigbee lock busy; deferring commissioning retry");
            schedule_retry(mode);
            return;
        }

        if (mode == EZB_BDB_MODE_NETWORK_STEERING && ezb_bdb_dev_joined()) {
            ESP_LOGI(TAG, "Already joined; skipping network steering retry");
        } else {
            ESP_LOGI(TAG, "Retrying %s...",
                     mode == EZB_BDB_MODE_INITIALIZATION ? "stack initialization (rejoin)"
                                                         : "network steering to pair with Zigbee coordinator");
            ezb_bdb_start_top_level_commissioning(mode);
        }
        esp_zigbee_lock_release();
    }

    // Zigbee Application Signal Handler (ZDO/BDB)
    static bool app_signal_handler(const ezb_app_signal_t* app_signal) noexcept {
        const ezb_app_signal_type_t sig_type = ezb_app_signal_get_type(app_signal);
        const void* p_params = ezb_app_signal_get_params(app_signal);

        switch (sig_type) {
        case EZB_ZDO_SIGNAL_SKIP_STARTUP:
            ESP_LOGI(TAG, "Initialize Zigbee stack");
            ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_INITIALIZATION);
            break;

        case EZB_BDB_SIGNAL_DEVICE_FIRST_START:
        case EZB_BDB_SIGNAL_DEVICE_REBOOT: {
            const auto* p_simple = static_cast<const ezb_bdb_signal_simple_params_t*>(p_params);
            const bool first_start = (sig_type == EZB_BDB_SIGNAL_DEVICE_FIRST_START);
            if (p_simple && p_simple->status == EZB_BDB_STATUS_SUCCESS) {
                if (ezb_bdb_is_factory_new()) {
                    ESP_LOGI(TAG, "Device is factory new (%s). Initiating network steering...",
                             first_start ? "first start" : "reboot");
                    ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_NETWORK_STEERING);
                } else {
                    cancel_retry();
//                    joined_.store(true);
                    ESP_LOGI(TAG, "Device rebooted & rejoined network! PAN ID: 0x%04x | Channel: %u | Short Addr: 0x%04x",
                             ezb_nwk_get_panid(),
                             ezb_nwk_get_current_channel(),
                             ezb_nwk_get_short_address());
                }
            } else {
                ESP_LOGE(TAG, "BDB %s signal reported a failure status (0x%02x). Retrying stack initialization in %lld ms...",
                         first_start ? "Device First Start" : "Device Reboot",
                         p_simple ? static_cast<unsigned>(p_simple->status) : 0xFFu,
                         static_cast<long long>(config_.steering_retry_interval.count()));
                schedule_retry(EZB_BDB_MODE_INITIALIZATION);
            }
            break;
        }

        case EZB_BDB_SIGNAL_STEERING: {
            const auto* p_simple = static_cast<const ezb_bdb_signal_simple_params_t*>(p_params);
            if (p_simple && p_simple->status == EZB_BDB_STATUS_SUCCESS) {
//                joined_.store(true);
                ESP_LOGI(TAG, ">>> Successfully paired with Zigbee Coordinator! <<<");
                ESP_LOGI(TAG, "    PAN ID:        0x%04X", ezb_nwk_get_panid());
                ESP_LOGI(TAG, "    Channel:       %u", ezb_nwk_get_current_channel());
                ESP_LOGI(TAG, "    Short Address: 0x%04X", ezb_nwk_get_short_address());
                cancel_retry();
            } else {
                const unsigned status = p_simple ? static_cast<unsigned>(p_simple->status) : 0xFFu;
                ESP_LOGW(TAG, "Network steering failed with status %u (searching for coordinator). Retrying in %lld ms...",
                         status, static_cast<long long>(config_.steering_retry_interval.count()));
                schedule_retry(EZB_BDB_MODE_NETWORK_STEERING);
            }
            break;
        }

        case EZB_ZDO_SIGNAL_LEAVE: {
//            joined_.store(false);
            ESP_LOGW(TAG, "Device left Zigbee network. Restarting pairing steering...");
            schedule_retry(EZB_BDB_MODE_NETWORK_STEERING);
            break;
        }

        default:
            ESP_LOGI(TAG, "Received Zigbee signal: %s (0x%04x)", ezb_app_signal_to_string(sig_type), sig_type);
            break;
        }

        return true;
    }

    // Handles a Lock/Unlock command: pulses the mapped relay and, on success, updates the LockState attribute.
    [[nodiscard]] static ezb_zcl_status_t handle_door_command(uint8_t ep, uint8_t new_lock_state, const char* label) noexcept {
        const auto channel = endpoint_to_channel(ep);
        if (!channel) {
            ESP_LOGW(TAG, "DoorLock %s command for unknown endpoint: %u", label, ep);
            return EZB_ZCL_STATUS_NOT_FOUND;
        }

        ESP_LOGI(TAG, "Triggering Relay Channel %u momentary pulse (%s)", *channel, label);
        if (!RelayCtrl::trigger_pulse(*channel)) {
            ESP_LOGE(TAG, "Relay Channel %u pulse failed (%s)", *channel, label);
            return EZB_ZCL_STATUS_FAIL;
        }

        ezb_zcl_set_attr_value(ep,
                               EZB_ZCL_CLUSTER_ID_DOOR_LOCK,
                               EZB_ZCL_CLUSTER_SERVER,
                               EZB_ZCL_ATTR_DOOR_LOCK_LOCK_STATE_ID,
                               EZB_ZCL_STD_MANUF_CODE,
                               &new_lock_state,
                               false);
        return EZB_ZCL_STATUS_SUCCESS;
    }

    // ZCL Core Action Callback Handler (Receives DoorLock cluster commands and attribute writes)
    static void zcl_action_handler(ezb_zcl_core_action_callback_id_t callback_id, void* message) noexcept {
        if (!message) {
            return;
        }

        switch (callback_id) {
        case EZB_ZCL_CORE_DOOR_LOCK_LOCK_DOOR_CB_ID: {
            auto* msg = static_cast<ezb_zcl_door_lock_lock_door_message_t*>(message);
            const uint8_t ep = msg->info.dst_ep;
            ESP_LOGI(TAG, "ZCL DoorLock Lock command received for Endpoint %u", ep);
            msg->out.result = handle_door_command(ep, EZB_ZCL_DOOR_LOCK_LOCK_STATE_LOCKED, "Lock");
            break;
        }

        case EZB_ZCL_CORE_DOOR_LOCK_UNLOCK_DOOR_CB_ID: {
            auto* msg = static_cast<ezb_zcl_door_lock_unlock_door_message_t*>(message);
            const uint8_t ep = msg->info.dst_ep;
            ESP_LOGI(TAG, "ZCL DoorLock Unlock command received for Endpoint %u", ep);
            msg->out.result = handle_door_command(ep, EZB_ZCL_DOOR_LOCK_LOCK_STATE_UNLOCKED, "Unlock");
            break;
        }

        case EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID: {
            auto* msg = static_cast<ezb_zcl_set_attr_value_message_t*>(message);
            const uint8_t ep = msg->info.dst_ep;

            if (msg->info.cluster_id == EZB_ZCL_CLUSTER_ID_DOOR_LOCK &&
                msg->in.attribute.id == EZB_ZCL_ATTR_DOOR_LOCK_LOCK_STATE_ID &&
                msg->in.attribute.data.value != nullptr) {
                const auto lock_val = *static_cast<const uint8_t*>(msg->in.attribute.data.value);
                ESP_LOGI(TAG, "ZCL Set Attribute: Endpoint %u | LockState = %u", ep, lock_val);

                if (const auto channel = endpoint_to_channel(ep)) {
                    ESP_LOGI(TAG, "Triggering Relay Channel %u momentary pulse (LockState set)", *channel);
                    if (!RelayCtrl::trigger_pulse(*channel)) {
                        ESP_LOGE(TAG, "Relay Channel %u pulse failed (LockState set)", *channel);
                    }
                }
            }
            break;
        }

        default:
            ESP_LOGD(TAG, "Unhandled ZCL action callback: 0x%04x", callback_id);
            break;
        }
    }

    // Zigbee stack main loop task
    static void zigbee_task(void*) noexcept {
        ESP_LOGI(TAG, "Starting Zigbee stack mainloop task...");
        esp_zigbee_launch_mainloop();
        vTaskDelete(nullptr);
    }

public:
    /**
     * @brief Initialize and start the Zigbee Garage Door Opener service.
     * @param cfg Configuration for endpoints, channels, and retry timers.
     * @return ESP_OK on success, error code otherwise.
     */
    static esp_err_t start(const Config& cfg) noexcept {
        if (!zb::endpoint_range_valid(cfg.base_endpoint, RelayCtrl::relay_count())) {
            ESP_LOGE(TAG, "Invalid endpoint range: base %u with %zu relay(s) exceeds 1..%u",
                     cfg.base_endpoint, RelayCtrl::relay_count(), MAX_APPLICATION_ENDPOINT);
            return ESP_ERR_INVALID_ARG;
        }

        config_ = cfg;

        if (config_.manufacturer_name.size() > ZCL_STRING_MAX_LEN) {
            ESP_LOGW(TAG, "Manufacturer name longer than %zu characters; it will be truncated", ZCL_STRING_MAX_LEN);
        }
        if (config_.model_identifier.size() > ZCL_STRING_MAX_LEN) {
            ESP_LOGW(TAG, "Model identifier longer than %zu characters; it will be truncated", ZCL_STRING_MAX_LEN);
        }

        // Create timer for commissioning retry
        retry_timer_ = xTimerCreate(
            "zb_retry_tmr",
            pdMS_TO_TICKS(config_.steering_retry_interval.count() > 0 ? config_.steering_retry_interval.count()
                                                                      : FALLBACK_RETRY_MS),
            pdFALSE,
            nullptr,
            on_retry_timer
        );
        if (!retry_timer_) {
            ESP_LOGE(TAG, "Failed to create commissioning retry timer");
            return ESP_ERR_NO_MEM;
        }

        // Zigbee device & platform configuration (Router)
        esp_zigbee_config_t zb_config{};
        zb_config.device_config.device_type = EZB_NWK_DEVICE_TYPE_ROUTER;
        zb_config.device_config.install_code_policy = false;
        zb_config.device_config.zczr_config.max_children = 10;
        zb_config.platform_config.storage_partition_name = "nvs";
        zb_config.platform_config.radio_config.radio_mode = ESP_ZIGBEE_RADIO_MODE_NATIVE;

        ESP_LOGI(TAG, "Initializing Zigbee stack...");
        esp_err_t err = esp_zigbee_init(&zb_config);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_zigbee_init failed: %s", esp_err_to_name(err));
            return err;
        }

        // Set maximum RF transmit power (+20 dBm for ESP32-C6)
        ezb_set_tx_power(20);

        // Set primary and secondary scanning channels
        const uint32_t secondary_mask =
            zb::effective_secondary_mask(config_.primary_channel_mask, config_.secondary_channel_mask);
        ezb_bdb_set_primary_channel_set(config_.primary_channel_mask);
        ezb_bdb_set_secondary_channel_set(secondary_mask);

        // Increase scan dwell time per channel (value 5 = ~500ms listening window per channel)
        ezb_bdb_set_scan_duration(5);

        // Register application signal and ZCL core action callbacks
        ezb_app_signal_add_handler(app_signal_handler);
        ezb_zcl_core_action_handler_register(zcl_action_handler);

        // Create device descriptor and register endpoints for each relay channel
        ezb_af_device_desc_t dev_desc = ezb_af_create_device_desc();
        if (dev_desc == EZB_INVALID_AF_DEVICE_DESC) {
            ESP_LOGE(TAG, "Failed to allocate Zigbee device descriptor");
            return ESP_FAIL;
        }

        ESP_LOGI(TAG, "Configuring %zu Zigbee Door Lock Endpoint(s) (Base EP: %u)...",
                 RelayCtrl::relay_count(), config_.base_endpoint);

        for (std::size_t i = 0; i < RelayCtrl::relay_count(); ++i) {
            const uint8_t ep_id = static_cast<uint8_t>(config_.base_endpoint + i);
            ezb_zha_door_lock_config_t ep_cfg{};
            ep_cfg.basic_cfg.zcl_version = EZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE;
            ep_cfg.basic_cfg.power_source = EZB_ZCL_BASIC_POWER_SOURCE_DC_SOURCE;
            ep_cfg.identify_cfg.identify_time = EZB_ZCL_IDENTIFY_IDENTIFY_TIME_DEFAULT_VALUE;
            ep_cfg.door_lock_cfg.lock_state = EZB_ZCL_DOOR_LOCK_LOCK_STATE_NOT_FULLY_LOCKED;
            ep_cfg.door_lock_cfg.lock_type = EZB_ZCL_DOOR_LOCK_LOCK_TYPE_DEAD_BOLT;
            ep_cfg.door_lock_cfg.actuator_enabled = true;
            ep_cfg.groups_cfg.name_support = EZB_ZCL_GROUPS_NAME_SUPPORT_DEFAULT_VALUE;
            ep_cfg.scenes_cfg.scene_count = EZB_ZCL_SCENES_SCENE_COUNT_DEFAULT_VALUE;
            ep_cfg.scenes_cfg.current_scene = EZB_ZCL_SCENES_CURRENT_SCENE_DEFAULT_VALUE;
            ep_cfg.scenes_cfg.current_group = EZB_ZCL_SCENES_CURRENT_GROUP_DEFAULT_VALUE;
            ep_cfg.scenes_cfg.scene_valid = EZB_ZCL_SCENES_SCENE_VALID_DEFAULT_VALUE;
            ep_cfg.scenes_cfg.name_support = EZB_ZCL_SCENES_NAME_SUPPORT_DEFAULT_VALUE;
            ep_cfg.scenes_cfg.scene_table_size = EZB_ZCL_SCENES_SCENE_TABLE_SIZE_DEFAULT_VALUE;

            ezb_af_ep_desc_t ep_desc = ezb_zha_create_door_lock(ep_id, &ep_cfg);
            if (ep_desc == EZB_INVALID_AF_EP_DESC) {
                ESP_LOGE(TAG, "Failed to create endpoint descriptor for EP %u", ep_id);
                ezb_af_free_device_desc(dev_desc);
                return ESP_FAIL;
            }

            // Set Manufacturer Name and Model Identifier on Basic cluster
            ezb_zcl_cluster_desc_t basic_cluster = ezb_af_endpoint_get_cluster_desc(
                ep_desc, EZB_ZCL_CLUSTER_ID_BASIC, EZB_ZCL_CLUSTER_SERVER
            );
            if (basic_cluster != EZB_INVALID_ZCL_CLUSTER_DESC) {
                add_basic_string_attr(basic_cluster, EZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, config_.manufacturer_name);
                add_basic_string_attr(basic_cluster, EZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, config_.model_identifier);
            }

            const ezb_err_t add_err = ezb_af_device_add_endpoint_desc(dev_desc, ep_desc);
            if (add_err != EZB_ERR_NONE) {
                ESP_LOGE(TAG, "Failed to add endpoint descriptor for EP %u: %d", ep_id, static_cast<int>(add_err));
                ezb_af_free_device_desc(dev_desc);
                return esp_zigbee_err_to_esp(add_err);
            }
            ESP_LOGI(TAG, "  - Endpoint %u mapped to Relay Channel %zu", ep_id, i + 1);
        }

        const ezb_err_t reg_err = ezb_af_device_desc_register(dev_desc);
        if (reg_err != EZB_ERR_NONE) {
            ESP_LOGE(TAG, "Failed to register device descriptor: %d", static_cast<int>(reg_err));
            return esp_zigbee_err_to_esp(reg_err);
        }

        // Start Zigbee stack in no-autostart mode so commissioning is triggered via SKIP_STARTUP signal
        err = esp_zigbee_start(false);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_zigbee_start failed: %s", esp_err_to_name(err));
            ezb_af_free_device_desc(dev_desc);
            return err;
        }

        // Launch Zigbee main loop in a dedicated FreeRTOS task
        const BaseType_t task_res = xTaskCreate(
            zigbee_task,
            "zb_main",
            4096,
            nullptr,
            5,
            nullptr
        );

        if (task_res != pdPASS) {
            ESP_LOGE(TAG, "Failed to create Zigbee main loop task");
            return ESP_FAIL;
        }

        ESP_LOGI(TAG, "Zigbee Garage Door Opener service started successfully");
        return ESP_OK;
    }
};

