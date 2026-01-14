# 09 Sleep & Wakeup

Power management and sleep modes demonstration.

## Description

Demonstrates ESP32-P4 power management features including light sleep and deep sleep modes with various wakeup sources.

## Features

- Light sleep with timer wakeup
- Light sleep with GPIO wakeup (touch interrupt)
- Deep sleep with timer wakeup
- Wakeup cause detection and display
- Backlight control during sleep transitions

## Sleep Modes

### Light Sleep (Timer)
- 5-second timer wakeup
- CPU pauses, RAM preserved
- Fast resume

### Light Sleep (GPIO)
- Touch interrupt wakeup (GPIO4)
- 30-second timer fallback
- Ideal for touch-to-wake

### Deep Sleep
- 10-second timer wakeup
- Full chip reset on wake
- Lowest power consumption

## Wakeup Causes

Reports wakeup source:
- Timer
- GPIO (touch)
- Other sources

## Build and Flash

```bash
pio run -t upload
```
