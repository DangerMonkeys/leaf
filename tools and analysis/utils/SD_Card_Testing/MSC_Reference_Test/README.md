# ESP32-S3 SDMMC USB MSC reference test

This isolated firmware tests the SD-to-USB data path without mounting FAT in firmware and without
running the Leaf application, Leaf Log, diagnostics, Wi-Fi, or the SD ownership state machine.

Configuration:

- Leaf hardware v3.2.6/v3.2.7 SDMMC pins
- Native ESP32-S3 USB device peripheral
- USB MSC only
- 4-bit SDMMC at 20 MHz
- 4 KiB DMA-capable staging buffer
- One multi-sector `sdmmc_read_sectors()` or `sdmmc_write_sectors()` operation per USB callback

The green status LED is steady when initialization succeeds. It blinks rapidly after the first
SDMMC read or write failure.

Build from this directory with:

```powershell
C:\Users\oxoth\.platformio\penv\Scripts\pio.exe run -e leaf_3_2_6_msc_reference
```

Test with the same card, cable, computer, and large source file used for the Leaf firmware tests.
Record elapsed time, Windows' displayed rate, whether the LUN disappears, and the final file hash.
Eject the volume before removing power whenever possible.

## Comparison results

Using the same approximately 45 MB file, SD card, Leaf, cable, and Windows computer:

- 4 KiB multi-sector transactions completed a write in approximately 90 seconds, about 500 KB/s
  actual throughput, without disconnects.
- 512-byte single-sector transactions completed the same write in approximately 7.5 minutes,
  about 100 KB/s actual throughput, without disconnects.
- Raising the 4 KiB test from 20 MHz to 40 MHz did not improve end-to-end throughput.

These results show that multi-sector batching accounts for most of the performance improvement,
while 20 MHz already exceeds the throughput required by the USB MSC path.
