# 07 Bluetooth

BLE device scanning demonstration.

## Description

Demonstrates Bluetooth Low Energy scanning via ESP-HOSTED protocol, discovering nearby BLE devices.

## Features

- BLE device discovery
- Device name resolution
- MAC address display (XX:XX:XX:XX:XX:XX format)
- RSSI signal strength monitoring
- Thread-safe device list with mutex
- Tracks up to 20 devices
- Scan complete detection

## Device Information

Each discovered device shows:
- Device name (if advertised)
- MAC address (BDA)
- Signal strength (RSSI)

## Controls

- **Scan** - Start new BLE scan
- **Refresh** - Update device list

## Requirements

- ESP32-C6 co-processor flashed with ESP-HOSTED firmware

## Build and Flash

```bash
pio run -t upload
```
