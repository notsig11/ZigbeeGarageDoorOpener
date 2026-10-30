#include "MiniTest.hpp"
#include "RelayController.hpp"

#include <chrono>
#include <cstdint>

using host_fake::Event;
using host_fake::EventKind;
using host_fake::state;

namespace {

using Relays4 = RelayController<4>;

Relays4::Config make_config(uint32_t active_level, std::chrono::milliseconds pulse = std::chrono::milliseconds{300}) {
    return Relays4::Config{
        .pins = {GPIO_NUM_18, GPIO_NUM_20, GPIO_NUM_19, GPIO_NUM_21},
        .pulse_duration = pulse,
        .active_level = active_level,
    };
}

std::size_t count_events(EventKind kind) {
    std::size_t n = 0;
    for (const Event& e : state().events) {
        if (e.kind == kind) {
            ++n;
        }
    }
    return n;
}

bool is_set_level(const Event& e, int pin, int level) {
    return e.kind == EventKind::GpioSetLevel && e.a == pin && e.b == level;
}

} // namespace

// ---- init ------------------------------------------------------------------------------------

TEST(relay_init_configures_outputs_with_pullups) {
    state().reset();
    CHECK_EQ(Relays4::init(make_config(0)), ESP_OK);

    const auto& cfg = state().last_gpio_config;
    const uint64_t expected_mask = (1ULL << 18) | (1ULL << 19) | (1ULL << 20) | (1ULL << 21);
    CHECK_EQ(cfg.pin_bit_mask, expected_mask);
    CHECK_EQ(cfg.mode, GPIO_MODE_OUTPUT);
    CHECK_EQ(cfg.pull_up_en, GPIO_PULLUP_ENABLE);
    CHECK_EQ(cfg.pull_down_en, GPIO_PULLDOWN_DISABLE);
    CHECK_EQ(cfg.intr_type, GPIO_INTR_DISABLE);
}

TEST(relay_init_drives_all_pins_inactive_active_low) {
    state().reset();
    CHECK_EQ(Relays4::init(make_config(0)), ESP_OK);

    // Active-low relay bank: inactive level is 1.
    CHECK_EQ(count_events(EventKind::GpioSetLevel), 4u);
    for (const Event& e : state().events) {
        if (e.kind == EventKind::GpioSetLevel) {
            CHECK_EQ(e.b, 1);
        }
    }
}

TEST(relay_init_drives_all_pins_inactive_active_high) {
    state().reset();
    CHECK_EQ(Relays4::init(make_config(1)), ESP_OK);

    for (const Event& e : state().events) {
        if (e.kind == EventKind::GpioSetLevel) {
            CHECK_EQ(e.b, 0);
        }
    }
}

TEST(relay_init_sets_idle_level_before_enabling_output) {
    // Avoids a glitch: the pin latch is preloaded with the inactive level before gpio_config.
    state().reset();
    CHECK_EQ(Relays4::init(make_config(0)), ESP_OK);

    std::size_t config_index = state().events.size();
    std::size_t last_set_level_index = 0;
    for (std::size_t i = 0; i < state().events.size(); ++i) {
        if (state().events[i].kind == EventKind::GpioConfig) {
            config_index = i;
        } else if (state().events[i].kind == EventKind::GpioSetLevel) {
            last_set_level_index = i;
        }
    }
    CHECK(last_set_level_index < config_index);
}

TEST(relay_init_skips_unconnected_pins) {
    state().reset();
    const RelayController<2>::Config cfg{
        .pins = {GPIO_NUM_5, GPIO_NUM_NC},
        .pulse_duration = std::chrono::milliseconds{100},
        .active_level = 1,
    };
    CHECK_EQ(RelayController<2>::init(cfg), ESP_OK);

    CHECK_EQ(state().last_gpio_config.pin_bit_mask, 1ULL << 5);
    CHECK_EQ(count_events(EventKind::GpioSetLevel), 1u);
}

TEST(relay_init_propagates_gpio_config_failure_and_stays_uninitialized) {
    using Relays3 = RelayController<3>;
    state().reset();
    state().gpio_config_result = ESP_FAIL;

    const Relays3::Config cfg{
        .pins = {GPIO_NUM_1, GPIO_NUM_2, GPIO_NUM_3},
        .pulse_duration = std::chrono::milliseconds{100},
        .active_level = 1,
    };
    CHECK_EQ(Relays3::init(cfg), ESP_FAIL);

    state().events.clear();
    CHECK(!Relays3::trigger_pulse(1));
    CHECK_EQ(Relays3::get_pin(1), GPIO_NUM_NC);
    CHECK_EQ(state().task_create_calls, 0);
}

TEST(relay_reinit_after_failure_recovers) {
    using Relays3 = RelayController<3>;
    const Relays3::Config cfg{
        .pins = {GPIO_NUM_1, GPIO_NUM_2, GPIO_NUM_3},
        .pulse_duration = std::chrono::milliseconds{100},
        .active_level = 1,
    };

    state().reset();
    state().gpio_config_result = ESP_FAIL;
    CHECK_EQ(Relays3::init(cfg), ESP_FAIL);

    state().reset();
    CHECK_EQ(Relays3::init(cfg), ESP_OK);
    CHECK(Relays3::trigger_pulse(2));
}

// ---- accessors -------------------------------------------------------------------------------

TEST(relay_count_is_compile_time_constant) {
    static_assert(Relays4::relay_count() == 4);
    static_assert(Relays4::count == 4);
    CHECK_EQ(Relays4::relay_count(), 4u);
}

TEST(relay_get_pin_maps_one_based_channels_in_config_order) {
    state().reset();
    CHECK_EQ(Relays4::init(make_config(0)), ESP_OK);

    CHECK_EQ(Relays4::get_pin(1), GPIO_NUM_18);
    CHECK_EQ(Relays4::get_pin(2), GPIO_NUM_20);
    CHECK_EQ(Relays4::get_pin(3), GPIO_NUM_19);
    CHECK_EQ(Relays4::get_pin(4), GPIO_NUM_21);
}

TEST(relay_get_pin_rejects_out_of_range_channels) {
    state().reset();
    CHECK_EQ(Relays4::init(make_config(0)), ESP_OK);

    CHECK_EQ(Relays4::get_pin(0), GPIO_NUM_NC);
    CHECK_EQ(Relays4::get_pin(5), GPIO_NUM_NC);
    CHECK_EQ(Relays4::get_pin(255), GPIO_NUM_NC);
}

TEST(relay_pins_span_matches_config) {
    state().reset();
    CHECK_EQ(Relays4::init(make_config(0)), ESP_OK);

    const auto pins = Relays4::pins();
    CHECK_EQ(pins.size(), 4u);
    CHECK_EQ(pins[0], GPIO_NUM_18);
    CHECK_EQ(pins[3], GPIO_NUM_21);
}

// ---- trigger_pulse ---------------------------------------------------------------------------

TEST(relay_pulse_before_init_is_rejected) {
    // RelayController<7> is never initialised anywhere in this test binary.
    state().reset();
    CHECK(!RelayController<7>::trigger_pulse(1));
    CHECK_EQ(state().task_create_calls, 0);
    CHECK_EQ(count_events(EventKind::GpioSetLevel), 0u);
}

TEST(relay_pulse_active_low_asserts_waits_then_releases) {
    state().reset();
    CHECK_EQ(Relays4::init(make_config(0, std::chrono::milliseconds{300})), ESP_OK);
    state().events.clear();

    CHECK(Relays4::trigger_pulse(2)); // channel 2 -> GPIO 20

    // Expected: TaskCreate, set(20,0), delay(30 ticks @100Hz), set(20,1)
    CHECK_EQ(state().events.size(), 4u);
    CHECK(state().events[0].kind == EventKind::TaskCreate);
    CHECK(is_set_level(state().events[1], 20, 0));
    CHECK(state().events[2].kind == EventKind::TaskDelay);
    CHECK_EQ(state().events[2].a, 30);
    CHECK(is_set_level(state().events[3], 20, 1));
}

TEST(relay_pulse_active_high_levels_are_inverted) {
    state().reset();
    CHECK_EQ(Relays4::init(make_config(1, std::chrono::milliseconds{500})), ESP_OK);
    state().events.clear();

    CHECK(Relays4::trigger_pulse(1)); // GPIO 18

    CHECK_EQ(state().events.size(), 4u);
    CHECK(is_set_level(state().events[1], 18, 1));
    CHECK_EQ(state().events[2].a, 50);
    CHECK(is_set_level(state().events[3], 18, 0));
}

TEST(relay_pulse_shorter_than_one_tick_waits_at_least_one_tick) {
    state().reset();
    CHECK_EQ(Relays4::init(make_config(0, std::chrono::milliseconds{1})), ESP_OK);
    state().events.clear();

    CHECK(Relays4::trigger_pulse(1));

    CHECK_EQ(state().events.size(), 4u);
    CHECK_EQ(state().events[2].a, 1);
}

TEST(relay_pulse_targets_only_requested_channel) {
    state().reset();
    CHECK_EQ(Relays4::init(make_config(0)), ESP_OK);
    state().events.clear();

    CHECK(Relays4::trigger_pulse(4)); // GPIO 21

    for (const Event& e : state().events) {
        if (e.kind == EventKind::GpioSetLevel) {
            CHECK_EQ(e.a, 21);
        }
    }
}

TEST(relay_pulse_invalid_channel_is_rejected_without_side_effects) {
    state().reset();
    CHECK_EQ(Relays4::init(make_config(0)), ESP_OK);
    state().events.clear();
    const int tasks_before = state().task_create_calls;

    CHECK(!Relays4::trigger_pulse(0));
    CHECK(!Relays4::trigger_pulse(5));
    CHECK(!Relays4::trigger_pulse(255));

    CHECK(state().events.empty());
    CHECK_EQ(state().task_create_calls, tasks_before);
}

TEST(relay_pulse_task_creation_failure_reports_false_and_leaves_pin_idle) {
    state().reset();
    CHECK_EQ(Relays4::init(make_config(0)), ESP_OK);
    state().events.clear();
    state().task_create_fails = true;

    CHECK(!Relays4::trigger_pulse(1));

    CHECK_EQ(count_events(EventKind::GpioSetLevel), 0u); // pin was never asserted
}

TEST(relay_pulse_runs_on_dedicated_task_and_returns_before_completion) {
    state().reset();
    CHECK_EQ(Relays4::init(make_config(0)), ESP_OK);
    state().events.clear();
    state().run_tasks_inline = false; // simulate the scheduler not having run the worker yet

    CHECK(Relays4::trigger_pulse(3));

    // Caller returned without touching the pin or blocking.
    CHECK_EQ(state().task_create_calls, 1);
    CHECK(state().last_task_name == "relay_pulse");
    CHECK_EQ(count_events(EventKind::GpioSetLevel), 0u);
    CHECK_EQ(count_events(EventKind::TaskDelay), 0u);
    // NOTE: with run_tasks_inline=false the heap-allocated worker params are intentionally never
    // consumed; AddressSanitizer's leak check is disabled for the binary for this reason.
}

TEST(relay_repeated_pulses_each_produce_a_full_cycle) {
    state().reset();
    CHECK_EQ(Relays4::init(make_config(0)), ESP_OK);
    state().events.clear();

    CHECK(Relays4::trigger_pulse(1));
    CHECK(Relays4::trigger_pulse(1));

    CHECK_EQ(count_events(EventKind::GpioSetLevel), 4u);
    CHECK_EQ(count_events(EventKind::TaskDelay), 2u);
}
