#include "MiniTest.hpp"
#include "DoorSensor.hpp"

#include <cstdint>

using host_fake::Event;
using host_fake::EventKind;
using host_fake::state;

namespace {

using Sensors1 = DoorSensor<1>;
using Sensors4 = DoorSensor<4>;

Sensors4::Config make_config(uint32_t active_level) {
    return Sensors4::Config{
        .pins = {GPIO_NUM_0, GPIO_NUM_1, GPIO_NUM_2, GPIO_NUM_3},
        .active_level = active_level,
    };
}

} // namespace

// ---- init ------------------------------------------------------------------------------------

TEST(sensor_init_configures_inputs_with_pullups) {
    state().reset();
    CHECK_EQ(Sensors4::init(make_config(0)), ESP_OK);

    const auto& cfg = state().last_gpio_config;
    const uint64_t expected_mask = (1ULL << 0) | (1ULL << 1) | (1ULL << 2) | (1ULL << 3);
    CHECK_EQ(cfg.pin_bit_mask, expected_mask);
    CHECK_EQ(cfg.mode, GPIO_MODE_INPUT);
    CHECK_EQ(cfg.pull_up_en, GPIO_PULLUP_ENABLE);
    CHECK_EQ(cfg.pull_down_en, GPIO_PULLDOWN_DISABLE);
    CHECK_EQ(cfg.intr_type, GPIO_INTR_DISABLE);
}

TEST(sensor_init_propagates_gpio_config_failure_and_stays_uninitialized) {
    using Sensors2 = DoorSensor<2>;
    state().reset();
    state().gpio_config_result = ESP_FAIL;

    const Sensors2::Config cfg{
        .pins = {GPIO_NUM_5, GPIO_NUM_6},
        .active_level = 0,
    };
    CHECK_EQ(Sensors2::init(cfg), ESP_FAIL);

    state().events.clear();
    CHECK(!Sensors2::is_door_closed(1));
    CHECK_EQ(Sensors2::get_pin(1), GPIO_NUM_NC);
}

TEST(sensor_reinit_after_failure_recovers) {
    using Sensors2 = DoorSensor<2>;
    const Sensors2::Config cfg{
        .pins = {GPIO_NUM_5, GPIO_NUM_6},
        .active_level = 0,
    };

    state().reset();
    state().gpio_config_result = ESP_FAIL;
    CHECK_EQ(Sensors2::init(cfg), ESP_FAIL);

    state().reset();
    CHECK_EQ(Sensors2::init(cfg), ESP_OK);
    // Default level is 1 (pull-up high), active_level is 0, so not active (door open)
    CHECK(!Sensors2::is_door_closed(1));
}

TEST(sensor_init_skips_unconnected_pins) {
    state().reset();
    const DoorSensor<2>::Config cfg{
        .pins = {GPIO_NUM_5, GPIO_NUM_NC},
        .active_level = 0,
    };
    CHECK_EQ(DoorSensor<2>::init(cfg), ESP_OK);

    CHECK_EQ(state().last_gpio_config.pin_bit_mask, 1ULL << 5);
}

// ---- accessors -------------------------------------------------------------------------------

TEST(sensor_count_is_compile_time_constant) {
    static_assert(Sensors4::sensor_count() == 4);
    static_assert(Sensors4::count == 4);
    CHECK_EQ(Sensors4::sensor_count(), 4u);
}

TEST(sensor_get_pin_maps_one_based_channels_in_config_order) {
    state().reset();
    CHECK_EQ(Sensors4::init(make_config(0)), ESP_OK);

    CHECK_EQ(Sensors4::get_pin(1), GPIO_NUM_0);
    CHECK_EQ(Sensors4::get_pin(2), GPIO_NUM_1);
    CHECK_EQ(Sensors4::get_pin(3), GPIO_NUM_2);
    CHECK_EQ(Sensors4::get_pin(4), GPIO_NUM_3);
}

TEST(sensor_get_pin_rejects_out_of_range_channels) {
    state().reset();
    CHECK_EQ(Sensors4::init(make_config(0)), ESP_OK);

    CHECK_EQ(Sensors4::get_pin(0), GPIO_NUM_NC);
    CHECK_EQ(Sensors4::get_pin(5), GPIO_NUM_NC);
    CHECK_EQ(Sensors4::get_pin(255), GPIO_NUM_NC);
}

TEST(sensor_pins_span_matches_config) {
    state().reset();
    CHECK_EQ(Sensors4::init(make_config(0)), ESP_OK);

    const auto pins = Sensors4::pins();
    CHECK_EQ(pins.size(), 4u);
    CHECK_EQ(pins[0], GPIO_NUM_0);
    CHECK_EQ(pins[3], GPIO_NUM_3);
}

// ---- is_active / is_door_closed --------------------------------------------------------------

TEST(sensor_active_low_door_open_when_pullup_high) {
    // Active-low: reed switch pulls pin to GND when closed.
    // Default gpio_levels are 1 (pull-up high) → door open.
    state().reset();
    CHECK_EQ(Sensors1::init(Sensors1::Config{
        .pins = {GPIO_NUM_0},
        .active_level = 0,
    }), ESP_OK);

    CHECK(!Sensors1::is_door_closed(1));
    CHECK(!Sensors1::is_active(1));
}

TEST(sensor_active_low_door_closed_when_pin_pulled_low) {
    // Simulate reed switch closing: pin pulled to GND (level 0).
    state().reset();
    CHECK_EQ(Sensors1::init(Sensors1::Config{
        .pins = {GPIO_NUM_0},
        .active_level = 0,
    }), ESP_OK);
    state().gpio_levels[0] = 0;

    CHECK(Sensors1::is_door_closed(1));
    CHECK(Sensors1::is_active(1));
}

TEST(sensor_active_high_door_closed_when_pin_high) {
    // Active-high: reed switch pulls pin to VCC when closed.
    state().reset();
    CHECK_EQ(Sensors1::init(Sensors1::Config{
        .pins = {GPIO_NUM_0},
        .active_level = 1,
    }), ESP_OK);
    // Default level is 1 (high) → door closed.

    CHECK(Sensors1::is_door_closed(1));
    CHECK(Sensors1::is_active(1));
}

TEST(sensor_active_high_door_open_when_pin_low) {
    // Active-high: pin pulled low → door open.
    state().reset();
    CHECK_EQ(Sensors1::init(Sensors1::Config{
        .pins = {GPIO_NUM_0},
        .active_level = 1,
    }), ESP_OK);
    state().gpio_levels[0] = 0;

    CHECK(!Sensors1::is_door_closed(1));
    CHECK(!Sensors1::is_active(1));
}

TEST(sensor_active_before_init_is_rejected) {
    // DoorSensor<7> is never initialised anywhere in this test binary.
    state().reset();
    CHECK(!DoorSensor<7>::is_door_closed(1));
    CHECK(!DoorSensor<7>::is_active(1));
}

TEST(sensor_invalid_channel_is_rejected) {
    state().reset();
    CHECK_EQ(Sensors4::init(make_config(0)), ESP_OK);

    CHECK(!Sensors4::is_door_closed(0));
    CHECK(!Sensors4::is_door_closed(5));
    CHECK(!Sensors4::is_door_closed(255));
}

TEST(sensor_unconnected_pin_returns_false) {
    state().reset();
    const DoorSensor<2>::Config cfg{
        .pins = {GPIO_NUM_5, GPIO_NUM_NC},
        .active_level = 0,
    };
    CHECK_EQ(DoorSensor<2>::init(cfg), ESP_OK);

    CHECK(DoorSensor<2>::get_pin(1) == GPIO_NUM_5);
    CHECK(DoorSensor<2>::get_pin(2) == GPIO_NUM_NC);
    CHECK(!DoorSensor<2>::is_door_closed(2));
}

TEST(sensor_multiple_channels_independent_state) {
    state().reset();
    CHECK_EQ(Sensors4::init(make_config(0)), ESP_OK);

    // Set pin 0 low (closed), pin 1 high (open), pin 2 low (closed), pin 3 high (open)
    state().gpio_levels[0] = 0;
    state().gpio_levels[1] = 1;
    state().gpio_levels[2] = 0;
    state().gpio_levels[3] = 1;

    CHECK(Sensors4::is_door_closed(1));   // GPIO 0 → closed
    CHECK(!Sensors4::is_door_closed(2));  // GPIO 1 → open
    CHECK(Sensors4::is_door_closed(3));   // GPIO 2 → closed
    CHECK(!Sensors4::is_door_closed(4));  // GPIO 3 → open
}
