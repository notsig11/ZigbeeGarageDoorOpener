#pragma once

// Pure helpers used by ZigbeeGarageDoorOpener. This header deliberately has no ESP-IDF or Zigbee
// dependencies so it can be unit tested on the host (see test/host).

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace zb {

inline constexpr std::size_t ZCL_STRING_MAX_LEN = 32;
inline constexpr uint8_t MAX_APPLICATION_ENDPOINT = 240;

using ZclString = std::array<uint8_t, ZCL_STRING_MAX_LEN + 1>; // length byte + characters

// Builds a ZCL character string (length-prefixed), truncating to ZCL_STRING_MAX_LEN.
[[nodiscard]] constexpr ZclString make_zcl_string(std::string_view text) noexcept {
    ZclString buf{};
    const std::size_t len = std::min(text.size(), ZCL_STRING_MAX_LEN);
    buf[0] = static_cast<uint8_t>(len);
    for (std::size_t i = 0; i < len; ++i) {
        buf[i + 1] = static_cast<uint8_t>(text[i]);
    }
    return buf;
}

// True if `count` consecutive endpoints starting at `base` all lie within 1..MAX_APPLICATION_ENDPOINT.
[[nodiscard]] constexpr bool endpoint_range_valid(uint8_t base, std::size_t count) noexcept {
    return base >= 1 && count >= 1 && (static_cast<std::size_t>(base) + count - 1) <= MAX_APPLICATION_ENDPOINT;
}

// Maps a Zigbee endpoint to its 1-based relay channel, or nullopt if the endpoint is not ours.
[[nodiscard]] constexpr std::optional<uint8_t> endpoint_to_channel(uint8_t ep, uint8_t base, std::size_t count) noexcept {
    if (ep < base) {
        return std::nullopt;
    }
    const auto index = static_cast<std::size_t>(ep - base);
    if (index >= count) {
        return std::nullopt;
    }
    return static_cast<uint8_t>(index + 1);
}

// A secondary mask of 0 means "same as primary", i.e. never scan beyond the primary set.
[[nodiscard]] constexpr uint32_t effective_secondary_mask(uint32_t primary, uint32_t secondary) noexcept {
    return secondary != 0 ? secondary : primary;
}

} // namespace zb
