# 06 SD Card

SD card file operations demonstration.

## Description

Shows how to interact with an SD card including mounting, directory listing, and file operations.

## Features

- Mount/unmount SD card toggle
- Directory listing with file/folder icons
- File size display (bytes/KB/MB)
- Write test file creation
- SD card capacity display
- Shows first 15 directory entries

## Operations

- **Mount** - Initialize SD card filesystem
- **Unmount** - Safely disconnect SD card
- **List** - Display directory contents
- **Write** - Create timestamped test file

## File Information

Directory listing shows:
- File/folder differentiation
- File names
- File sizes with appropriate units

## Hardware

- MicroSD card inserted in slot

## Build and Flash

```bash
pio run -t upload
```
