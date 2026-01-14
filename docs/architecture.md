# ESP32-P4 + ESP32-C6 Communication Architecture

**Target Board:** Guition JC4880P443C_I_W (JC-ESP32P4-M3-C6 module)

## Overview

The JC4880P443C uses a dual-chip architecture:
- **ESP32-P4** - Main processor: display, UI, touch, application logic
- **ESP32-C6** - Network processor: WiFi 6, Bluetooth 5, Zigbee/Thread

The chips communicate via **SDIO interface**. There are two main approaches for implementing this communication.

## Communication Options

### Option A: ESP-HOSTED (Recommended for most projects)

Uses Espressif's ESP-HOSTED framework. The C6 runs pre-flashed slave firmware and exposes WiFi/BT as a standard network interface to P4.

```
┌─────────────────────────────────────────────────────────────┐
│                      ESP32-P4 (Main)                        │
│  ┌───────────────────────────────────────────────────────┐  │
│  │                  Your Application                     │  │
│  │           (LVGL UI, Business Logic, etc.)             │  │
│  └──────────────────────────┬────────────────────────────┘  │
│                             │                               │
│  ┌──────────────────────────┴────────────────────────────┐  │
│  │         Standard ESP-IDF WiFi/BT APIs                 │  │
│  │    (esp_wifi_*, esp_http_client_*, esp_ble_*, etc.)   │  │
│  └──────────────────────────┬────────────────────────────┘  │
│                             │                               │
│  ┌──────────────────────────┴────────────────────────────┐  │
│  │           ESP-HOSTED Host Driver (SDIO)               │  │
│  └──────────────────────────┬────────────────────────────┘  │
└─────────────────────────────┼───────────────────────────────┘
                              │ SDIO (4-bit, 40MHz)
┌─────────────────────────────┼───────────────────────────────┐
│  ┌──────────────────────────┴────────────────────────────┐  │
│  │           ESP-HOSTED Slave Firmware                   │  │
│  │              (Pre-flashed by factory)                 │  │
│  └──────────────────────────┬────────────────────────────┘  │
│                             │                               │
│  ┌──────────────────────────┴────────────────────────────┐  │
│  │           Native WiFi 6 / BT5 / 802.15.4              │  │
│  └───────────────────────────────────────────────────────┘  │
│                      ESP32-C6 (Network)                     │
└─────────────────────────────────────────────────────────────┘
```

#### Pros

| Advantage              | Description                                                        |
|------------------------|--------------------------------------------------------------------|
| Standard APIs          | Use familiar `esp_wifi_*`, `esp_http_client_*`, `esp_ble_*` APIs   |
| No C6 development      | C6 firmware is pre-flashed, no need to write/maintain it           |
| Transparent networking | WiFi/BT appears as native interface on P4                          |
| Espressif supported    | Official framework with documentation and updates                  |
| Battle-tested          | Used in production devices                                         |
| OTA updates            | Can update C6 firmware via SDIO from P4                            |

#### Cons

| Disadvantage        | Description                                                           |
|---------------------|-----------------------------------------------------------------------|
| Less control        | Cannot customize C6 behavior or add custom logic                      |
| Resource overhead   | ESP-HOSTED adds some memory/CPU overhead on P4                        |
| Black box           | C6 is essentially a network adapter, no visibility into its operation |
| Limited to networking | Cannot offload other tasks to C6                                    |
| Firmware dependency | Tied to ESP-HOSTED slave firmware version                             |

#### When to use

- Standard WiFi/BT applications
- HTTP clients, MQTT, WebSocket
- BLE peripherals/centrals
- Quick prototyping
- When you don't need custom C6 logic

---

### Option B: Custom SDIO Protocol

Write custom firmware for both P4 (master) and C6 (slave) with your own command protocol.

```
┌─────────────────────────────────────────────────────────────┐
│                      ESP32-P4 (Main)                        │
│  ┌───────────────────────────────────────────────────────┐  │
│  │                  Your Application                     │  │
│  │           (LVGL UI, Business Logic, etc.)             │  │
│  └──────────────────────────┬────────────────────────────┘  │
│                             │                               │
│  ┌──────────────────────────┴────────────────────────────┐  │
│  │            Custom Protocol Layer                      │  │
│  │        (Your commands: HTTP_GET, SENSOR_READ, etc.)   │  │
│  └──────────────────────────┬────────────────────────────┘  │
│                             │                               │
│  ┌──────────────────────────┴────────────────────────────┐  │
│  │              SDIO Master Driver                       │  │
│  └──────────────────────────┬────────────────────────────┘  │
└─────────────────────────────┼───────────────────────────────┘
                              │ SDIO Bus
┌─────────────────────────────┼───────────────────────────────┐
│  ┌──────────────────────────┴────────────────────────────┐  │
│  │              SDIO Slave Driver                        │  │
│  └──────────────────────────┬────────────────────────────┘  │
│                             │                               │
│  ┌──────────────────────────┴────────────────────────────┐  │
│  │            Custom Command Handler                     │  │
│  │     (HTTP client, sensor polling, data processing)    │  │
│  └──────────────────────────┬────────────────────────────┘  │
│                             │                               │
│  ┌──────────────────────────┴────────────────────────────┐  │
│  │           WiFi / BT / Zigbee / Custom Logic           │  │
│  └───────────────────────────────────────────────────────┘  │
│                      ESP32-C6 (Network)                     │
└─────────────────────────────────────────────────────────────┘
```

#### Pros

| Advantage              | Description                                                   |
|------------------------|---------------------------------------------------------------|
| Full control           | Complete control over both chips' behavior                    |
| Offload processing     | Move HTTP parsing, data processing, encryption to C6          |
| Custom protocols       | Implement proprietary or specialized protocols on C6          |
| Optimized communication| Send only processed data to P4, reduce SDIO traffic           |
| Use C6 peripherals     | Access C6's ADC, GPIO, timers for additional I/O              |
| Lower P4 memory        | HTTP/JSON parsing on C6 means less RAM needed on P4           |
| Zigbee/Thread          | Full control over 802.15.4 stack                              |

#### Cons

| Disadvantage           | Description                                                   |
|------------------------|---------------------------------------------------------------|
| Development effort     | Must write and maintain firmware for both chips               |
| C6 flashing complexity | C6 UART not exposed; need SDIO OTA or custom flashing setup   |
| No standard APIs       | Cannot use `esp_wifi_*` directly on P4                        |
| Testing complexity     | Must test both firmwares and their interaction                |
| Protocol design        | Must design robust command/response protocol                  |
| Debugging harder       | Issues can be on either chip or in communication              |

#### When to use

- Need to offload processing from P4 (JSON parsing, encryption)
- Custom network protocols
- Zigbee/Thread mesh networking with custom logic
- Using C6 as sensor hub (ADC, GPIO expansion)
- Strict memory constraints on P4
- Need maximum control and optimization

---

## SDIO Pin Configuration

| Signal   | P4 GPIO | Description          |
|----------|---------|----------------------|
| D0       | GPIO14  | Data line 0          |
| D1       | GPIO15  | Data line 1          |
| D2       | GPIO16  | Data line 2          |
| D3       | GPIO17  | Data line 3          |
| CLK      | GPIO18  | Clock (up to 40MHz)  |
| CMD      | GPIO19  | Command line         |
| C6_RESET | GPIO54  | Reset C6 from P4     |

## Custom Protocol Example

### Command Format (P4 → C6)

```
┌──────────┬─────────────┬──────────────┐
│ cmd (1B) │ length (2B) │ payload (nB) │
└──────────┴─────────────┴──────────────┘
```

### Response Format (C6 → P4)

```
┌────────────┬─────────────┬──────────────┐
│ status (1B)│ length (2B) │ payload (nB) │
└────────────┴─────────────┴──────────────┘
```

### Example Commands

| Code | Command         | Payload        | Response           |
|------|-----------------|----------------|--------------------|
| 0x00 | NOP             | -              | Status only        |
| 0x01 | WIFI_STATUS     | -              | Connected/SSID/RSSI|
| 0x02 | WIFI_CONNECT    | SSID\0PASSWORD | Success/Error      |
| 0x03 | WIFI_DISCONNECT | -              | Success            |
| 0x10 | HTTP_GET        | URL string     | HTTP response body |
| 0x11 | HTTP_POST       | URL\0BODY      | HTTP response body |
| 0x20 | BLE_SCAN        | Duration (ms)  | Device list        |
| 0x30 | SENSOR_READ     | Sensor ID      | Sensor data        |

## Decision Matrix

| Requirement           | ESP-HOSTED         | Custom Protocol   |
|-----------------------|--------------------|-------------------|
| Quick development     | ✅ Best            | ⚠️ More work       |
| Standard WiFi/BT      | ✅ Best            | ⚠️ Must implement  |
| Custom C6 logic       | ❌ Not possible    | ✅ Best            |
| Offload processing    | ❌ Limited         | ✅ Best            |
| Zigbee/Thread control | ⚠️ Limited         | ✅ Best            |
| Memory constrained P4 | ⚠️ Higher overhead | ✅ Best            |
| C6 as sensor hub      | ❌ Not possible    | ✅ Best            |
| Maintainability       | ✅ Best            | ⚠️ More code       |
| Debugging ease        | ✅ Best            | ⚠️ More complex    |

## References

- [ESP-HOSTED GitHub](https://github.com/espressif/esp-hosted) - Official WiFi/BT bridging framework
- [ESP-IDF SDIO Slave](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/api-reference/peripherals/sdio_slave.html) - C6 SDIO slave driver
- [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/) - ESP32-P4 documentation
- [LVGL Documentation](https://docs.lvgl.io/master/) - Graphics library
