# FANET ID Allocator for ESP32

> **Warning:** This tool will clear any user-saved settings on your device. Use with caution.

## Overview

The FANET ID Allocator is a Python-based utility for assigning and writing FANET IDs to ESP32 devices. It automatically generates an NVS (Non-Volatile Storage) binary containing the assigned FANET ID and flashes it to the target ESP32. The tool also checks if a MAC address already has a FANET ID and writes a CSV file for record-keeping.

This tool is primarily intended for developers working with ESP32 devices and the FANET protocol, providing a simple way to initialize and manage device IDs.

## How It Works

1. The script checks the MAC address of your ESP32 to see if it already has an assigned FANET ID.
2. If the device does not yet have a FANET ID, a new one is generated.
3. An NVS binary is created with multipage blob support enabled.
4. The binary is flashed to the ESP32 via `esptool.py`.
5. A temporary CSV is written to record the mapping between MAC addresses and FANET IDs.

The flashing process handles detection of the ESP32 chip type, connection via USB, and verification of the flashed data.

## Using

1. Source your ESP-IDF environment (Linux example):

```bash
. ~/Documents/esp-idf/export.sh
```

2. Run the script:
```bash
python src/scripts/allocate_fanet_id.py --esp-idf ~/Documents/esp-idf
```

Example Output:
```
scott@Bethanys-MacBook-Air leaf % python src/scripts/allocate_fanet_id.py --esp-idf ~/Documents/esp-idf
MAC f0:f5:bd:53:5f:20 already has FANET 0C0001
NVM CSV written to /var/folders/g1/sv5fp2j91l76qqg86z5f78wh0000gp/T/nvm.csv

Creating NVS binary with version: V2 - Multipage Blob Support Enabled

Created NVS binary: ===> /var/folders/g1/sv5fp2j91l76qqg86z5f78wh0000gp/T/flash.bin
Created NVS binary: /var/folders/g1/sv5fp2j91l76qqg86z5f78wh0000gp/T/flash.bin
esptool.py v4.9.0
Found 2 serial ports
Serial port /dev/cu.usbmodem11121301
Connecting...
Detecting chip type... ESP32-S3
Chip is ESP32-S3 (QFN56) (revision v0.2)
Features: WiFi, BLE, Embedded Flash 8MB (GD)
Crystal is 40MHz
USB mode: USB-Serial/JTAG
MAC: f0:f5:bd:53:5f:20
Uploading stub...
Running stub...
Stub running...
Configuring flash size...
Flash will be erased from 0x00009000 to 0x0000dfff...
Compressed 20480 bytes to 177...
Wrote 20480 bytes (177 compressed) at 0x00009000 in 0.2 seconds (effective 769.6 kbit/s)...
Hash of data verified.

Leaving...
Hard resetting via RTS pin...
Flashed /var/folders/g1/sv5fp2j91l76qqg86z5f78wh0000gp/T/flash.bin to device at offset 0x9000
```