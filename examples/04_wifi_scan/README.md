# 04 WiFi Scan

WiFi network scanning via ESP-HOSTED.

## Description

Demonstrates WiFi scanning functionality using the ESP-HOSTED protocol over SDIO interface between ESP32-P4 and ESP32-C6.

## Features

- Live WiFi network discovery
- RSSI signal strength display (dBm)
- Security type detection (Open, WEP, WPA, WPA2, WPA3)
- Channel information
- Manual scan trigger
- Displays up to 20 networks

## Network Information

Each discovered network shows:
- SSID (network name)
- Signal strength (RSSI in dBm)
- Security type
- Channel number

## Requirements

- ESP32-C6 co-processor flashed with ESP-HOSTED firmware

## Build and Flash

```bash
pio run -t upload
```
