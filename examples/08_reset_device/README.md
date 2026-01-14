# 08 Reset Device

System reset and chip information demonstration.

## Description

Shows system reset functionality and displays chip/system information. Reports the reason for the last reset on boot.

## Features

- Reset reason detection and display
- Immediate software reset
- Countdown timer reset (5 seconds) with cancel
- Chip information display
- Free heap memory display

## Reset Reasons

Detects and reports:
- Power-on reset
- External pin reset
- Software reset
- Panic/exception reset
- Watchdog reset
- Deep sleep wakeup
- Brownout reset
- SDIO reset

## Chip Information

- Chip model
- Number of cores
- Chip revision
- Free heap memory

## Controls

- **Reset Now** - Immediate software reset
- **Reset in 5s** - Countdown with cancel option

## Build and Flash

```bash
pio run -t upload
```
