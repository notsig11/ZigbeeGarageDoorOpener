#include "MiniTest.hpp"
#include "ZigbeeHelpers.hpp"

#include <string>

using namespace zb;

// ---- make_zcl_string -------------------------------------------------------------------------

TEST(zcl_string_prefixes_length_and_copies_text) {
    const ZclString s = make_zcl_string("GD001");
    CHECK_EQ(s[0], 5);
    CHECK_EQ(s[1], 'G');
    CHECK_EQ(s[2], 'D');
    CHECK_EQ(s[3], '0');
    CHECK_EQ(s[4], '0');
    CHECK_EQ(s[5], '1');
    CHECK_EQ(s[6], 0); // remainder stays zeroed
}

TEST(zcl_string_empty_has_zero_length) {
    const ZclString s = make_zcl_string("");
    CHECK_EQ(s[0], 0);
    CHECK_EQ(s[1], 0);
}

TEST(zcl_string_exactly_max_length_is_not_truncated) {
    const std::string text(ZCL_STRING_MAX_LEN, 'x');
    const ZclString s = make_zcl_string(text);
    CHECK_EQ(s[0], ZCL_STRING_MAX_LEN);
    CHECK_EQ(s[ZCL_STRING_MAX_LEN], 'x'); // last character lands in the final slot
}

TEST(zcl_string_longer_than_max_is_truncated_without_overflow) {
    const std::string text(ZCL_STRING_MAX_LEN + 10, 'y');
    const ZclString s = make_zcl_string(text);
    CHECK_EQ(s[0], ZCL_STRING_MAX_LEN);
    CHECK_EQ(s[ZCL_STRING_MAX_LEN], 'y');
}

TEST(zcl_string_is_usable_at_compile_time) {
    constexpr ZclString s = make_zcl_string("ab");
    static_assert(s[0] == 2 && s[1] == 'a' && s[2] == 'b');
    CHECK_EQ(s[0], 2);
}

// ---- endpoint_range_valid --------------------------------------------------------------------

TEST(endpoint_range_accepts_typical_layout) {
    CHECK(endpoint_range_valid(1, 4));
}

TEST(endpoint_range_rejects_base_zero) {
    CHECK(!endpoint_range_valid(0, 4));
}

TEST(endpoint_range_rejects_zero_count) {
    CHECK(!endpoint_range_valid(1, 0));
}

TEST(endpoint_range_upper_boundary) {
    CHECK(endpoint_range_valid(240, 1));    // last usable endpoint
    CHECK(!endpoint_range_valid(240, 2));   // would reach 241
    CHECK(endpoint_range_valid(237, 4));    // 237..240
    CHECK(!endpoint_range_valid(238, 4));   // 238..241
}

TEST(endpoint_range_rejects_base_above_max) {
    CHECK(!endpoint_range_valid(241, 1));
    CHECK(!endpoint_range_valid(255, 1));
}

// ---- endpoint_to_channel ---------------------------------------------------------------------

TEST(endpoint_maps_to_one_based_channel) {
    CHECK_EQ(*endpoint_to_channel(1, 1, 4), 1);
    CHECK_EQ(*endpoint_to_channel(2, 1, 4), 2);
    CHECK_EQ(*endpoint_to_channel(4, 1, 4), 4);
}

TEST(endpoint_below_base_is_rejected) {
    CHECK(!endpoint_to_channel(0, 1, 4).has_value());
    CHECK(!endpoint_to_channel(9, 10, 4).has_value());
}

TEST(endpoint_past_last_relay_is_rejected) {
    CHECK(!endpoint_to_channel(5, 1, 4).has_value());
    CHECK(!endpoint_to_channel(255, 1, 4).has_value());
}

TEST(endpoint_mapping_honours_non_default_base) {
    CHECK_EQ(*endpoint_to_channel(10, 10, 2), 1);
    CHECK_EQ(*endpoint_to_channel(11, 10, 2), 2);
    CHECK(!endpoint_to_channel(12, 10, 2).has_value());
}

TEST(endpoint_mapping_zero_relays_maps_nothing) {
    CHECK(!endpoint_to_channel(1, 1, 0).has_value());
}

// ---- effective_secondary_mask ----------------------------------------------------------------

TEST(secondary_mask_zero_falls_back_to_primary) {
    CHECK_EQ(effective_secondary_mask(1U << 20, 0), 1U << 20);
}

TEST(secondary_mask_explicit_value_wins) {
    CHECK_EQ(effective_secondary_mask(1U << 20, 0x07FFF800U), 0x07FFF800U);
}
