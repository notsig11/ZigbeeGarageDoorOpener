# Zigbee Garage Door Opener

A half slop coded Zigbee garage door opener built on the ESP32-C6 using ESP-IDF.  Each relay channel is exposed as a Zigbee Door Lock cluster endpoint, allowing control from any Zigbee coordinator — including Home Assistant.

I started building this based on the ESP-IDF Zigbee examples by hand and decided to try out JetBrains Junie.  It helped a lot with debugging some pairing issues I had and with the Kconfig stuff since I haven't dealt with that in a long time.  We'll see if this gets abandoned now that I'm using it or if it gets extended.  :)  

## Hardware

- **Development Board:** [Seeed Studio XIAO ESP32C6](https://wiki.seeedstudio.com/xiao_esp32c6get_started/)
- **Relay Module:** 8-channel relay board (wired to GPIO pins configured via Kconfig)
- **Power:** 3.3V logic level for the ESP32-C6; relay module coil voltage depends on your hardware

## TODO:
- Add open/closed detection for doors.
- Investigate the device type.   Has to be something better than lock/unlock but I wanted to get this going quickly.

## Prerequisites

- [ESP-IDF v5.0+](https://github.com/espressif/esp-idf) (C++20 support required)
- ESP-IDF toolchain and Python dependencies per the [ESP-IDF setup guide](https://docs.espressif.com/projects/esp-idf/en/latest/en/get-started/)

## Build and Flash

```bash
# Configure the project (sets target to ESP32-C6 and opens menuconfig)
idf.py set-target esp32c6
idf.py menuconfig

# Build and flash
idf.py -p PORT flash monitor
```

Replace `PORT` with your serial port (e.g., `/dev/ttyACM0` on Linux or `COM3` on Windows).

## Configuration

Run `idf.py menuconfig` to adjust the following options under **Garage Door Opener**:

### Relay Configuration

| Option | Default | Description |
|--------|---------|-------------|
| Relay Count | 4 | Number of relay channels (1–8) |
| Relay Pin 1–8 | 18, 20, 19, 21, … | GPIO pin for each relay channel |
| Pulse Duration | 300 ms | How long each relay pulse stays active |
| Active Level | 0 (active-low) | Logic level that triggers the relay |

### Zigbee Channel Configuration

| Option | Default | Description |
|--------|---------|-------------|
| Primary Channel Mask | `0x07FFF800` | Bitmask of channels (11–26) scanned when joining |
| Secondary Channel Mask | `0x07FFF800` | Fallback channels if primary scan finds no network |

Default masks enable all channels 11–26.  For a single channel, use `1 << N`, e.g., `0x00100000` for channel 20.

## Pairing

1.  Put your Zigbee coordinator into pairing/permission mode.
2.  Flash the firmware to the XIAO ESP32C6.
3.  The device will automatically attempt to join via Zigbee network steering.

## Home Assistant

Once paired, each relay appears as a Door Lock entity in [Home Assistant](https://www.home-assistant.io/).  Use the Zigbee integration (Z2M, ZHA, or native ESP-Zigbee) to discover and control the doors.

## Project Structure

```
├── CMakeLists.txt              # Top-level build config
├── partitions.csv              # NVS, Zigbee storage, and factory app partitions
├── main/
│   ├── CMakeLists.txt          # Component build config
│   ├── Kconfig                 # menuconfig options
│   ├── ZigbeeGarageDoorOpener.cpp  # Entry point and Zigbee signal handlers
│   ├── ZigbeeGarageDoorOpener.hpp  # Template service class
│   ├── RelayController.hpp     # Template-based GPIO relay controller
│   └── ZigbeeHelpers.hpp       # Host-testable Zigbee helper functions
└── test/
    └── host/                   # Host-side unit tests with ESP-IDF stubs
```

## References

- [ESP-IDF](https://github.com/espressif/esp-idf) — Espressif IoT Development Framework
- [ESP-Zigbee Library](https://components.espressif.com/components/espressif/esp-zigbee-lib) — Zigbee protocol stack component
- [Seeed Studio XIAO ESP32C6 Docs](https://wiki.seeedstudio.com/xiao_esp32c6get_started/) — Getting started guide
- [Home Assistant](https://www.home-assistant.io/) — Open-source home automation platform
