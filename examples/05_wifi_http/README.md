# 05 WiFi HTTP

WiFi connection and HTTP client demonstration.

## Description

Shows how to connect to a WiFi network and perform HTTP requests using the ESP-HOSTED networking stack.

## Features

- WiFi STA mode connection
- Automatic retry logic (up to 5 attempts)
- IP address display on connection
- HTTP GET request to httpbin.org/ip
- Response time measurement
- Event-driven WiFi state management

## Configuration

Edit `src/main.cpp` to set your WiFi credentials:

```cpp
#define WIFI_SSID "your_ssid"
#define WIFI_PASS "your_password"
```

## UI Elements

- Connection status display
- IP address label
- HTTP request button
- Response content display
- Response timing information

## Requirements

- ESP32-C6 co-processor flashed with ESP-HOSTED firmware
- WiFi network with internet access

## Build and Flash

```bash
pio run -t upload
```
