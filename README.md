# JC4880P443C (ESP32-P4 + ESP32-C6) examples

Widget-based smart display firmware for the GUITION JC4880P443C development board.

## Hardware

**Target Device:** GUITION JC4880P443C (JC-ESP32P4-M3-C6 module)

| Component     | Specification                                |
|---------------|----------------------------------------------|
| Main SoC      | ESP32-P4 dual-core RISC-V @ 400 MHz          |
| Network SoC   | ESP32-C6 (WiFi 6, Bluetooth 5, Zigbee/Thread)|
| Display       | 4.3" IPS 480x800 via MIPI-DSI (ST7701)       |
| Touch         | Goodix GT911 capacitive touchscreen          |
| Audio         | ES8311 codec with mic and speaker            |
| Storage       | MicroSD card slot, 32 MB PSRAM               |
| Communication | SDIO interface (ESP-HOSTED protocol)         |

## Technology Stack

- **Framework:** ESP-IDF 5.4+
- **GUI:** LVGL 9.x
- **Build System:** PlatformIO

## Examples

| Example                                          | Description                                          |
|--------------------------------------------------|------------------------------------------------------|
| [01_display_basic](examples/01_display_basic/)   | Basic display initialization and LVGL interaction    |
| [02_display_images](examples/02_display_images/) | Graphics rendering: colors, gradients, shapes, animations |
| [03_display_touch](examples/03_display_touch/)   | Interactive touch drawing canvas                     |
| [04_wifi_scan](examples/04_wifi_scan/)           | WiFi network scanning via ESP-HOSTED                 |
| [05_wifi_http](examples/05_wifi_http/)           | WiFi connection and HTTP client                      |
| [06_sdcard](examples/06_sdcard/)                 | SD card file operations                              |
| [07_bluetooth](examples/07_bluetooth/)           | BLE device scanning                                  |
| [08_reset_device](examples/08_reset_device/)     | System reset and chip info                           |
| [09_sleep_wakeup](examples/09_sleep_wakeup/)     | Power management and sleep modes                     |

## Documentation

- [Architecture](docs/architecture.md) - System design, data flow, and protocols
- [Device Specs](docs/device-specs.md) - Pin mappings and hardware configuration

## Getting Started

### Prerequisites

- PlatformIO Core or IDE
- ESP-IDF 5.4+

### Build and Flash

```bash
cd examples/01_display_basic
pio run -t upload
```

Or using the Makefile:

```bash
make build EXAMPLE=01_display_basic
make upload EXAMPLE=01_display_basic
```

### List Available Examples

```bash
make list
```

### Monitor Serial Output

```bash
make monitor EXAMPLE=01_display_basic
```
