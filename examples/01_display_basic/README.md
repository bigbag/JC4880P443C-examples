# 01 Display Basic

Basic display initialization and LVGL interaction demo.

## Description

Demonstrates fundamental display setup using MIPI-DSI interface with LVGL graphics library. Creates a simple UI with interactive elements.

## Features

- MIPI-DSI display initialization (480x800 resolution)
- LVGL rendering pipeline with PSRAM buffer
- Touch input via GT911 controller
- Interactive button with click counter
- Backlight brightness control

## UI Elements

- Title label
- Info display showing resolution
- "Click Me!" button with counter
- Real-time click count feedback

## Build and Flash

```bash
pio run -t upload
```
