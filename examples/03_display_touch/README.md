# 03 Display Touch

Interactive touch drawing canvas application.

## Description

A drawing application that demonstrates touch input handling with real-time coordinate display and color selection.

## Features

- Interactive drawing canvas (460x550 pixels)
- Real-time touch coordinate display
- Touch state monitoring (pressed/released)
- 7-color palette (red, green, blue, yellow, magenta, cyan, white)
- Canvas clear functionality
- PSRAM-backed canvas buffer

## Controls

- **Draw** - Touch and drag on canvas
- **Color** - Cycle through available colors
- **Clear** - Reset canvas to blank state

## UI Elements

- Drawing canvas with dark background
- Coordinate display (X, Y position)
- Touch state label
- Color selection button
- Clear button

## Build and Flash

```bash
pio run -t upload
```
