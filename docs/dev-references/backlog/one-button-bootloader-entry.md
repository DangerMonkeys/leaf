---
title: One-button Bootloader Entry
description: Possible firmware-assisted flow for entering ESP32-S3 download mode with only the BOOT button.
---

# One-button Bootloader Entry

Leaf currently enters the ESP32-S3 ROM serial bootloader with the normal manual sequence:

1. Press and hold the BOOT button to pull `GPIO0` low.
2. Press and release RESET so the chip samples `GPIO0` during reset.
3. Release BOOT after the ROM bootloader starts.

This backlog item tracks a possible convenience flow: while the application is running, detect a
long hold of the BOOT button (`GPIO0` low) and call a firmware restart. If the ESP32-S3 samples the
strap pins during that software-triggered reset, keeping BOOT held should cause the next boot to
enter ROM download mode. That would make the working flow "hold BOOT until Leaf restarts into the
bootloader" instead of requiring both BOOT and RESET.

Current understanding:

- ESP32-S3 enters download mode when `GPIO0` is held low at reset.
- Existing auto-reset support normally requires host-controlled DTR/RTS lines wired to `GPIO0` and
  `EN`, which Leaf may not have.
- Firmware-assisted entry would only work while the application is alive enough to read the button
  and restart. The physical RESET button remains the fallback for crashes or hangs.
- This should be verified on hardware because it depends on whether the current software restart
  path re-samples the strap state in the way needed for ROM download mode.

Future work:

- Add a developer-mode-only or hidden long-press handler for `GPIO0`.
- Use a hold duration long enough to avoid accidental bootloader entry, for example 1-2 seconds.
- Show a short display or serial message such as `Entering USB bootloader...` before restarting.
- Call `ESP.restart()` or `esp_restart()` while the user continues holding BOOT.
- Verify the ROM boot log reports download mode instead of normal `SPI_FAST_FLASH_BOOT`.
- Confirm the flow still has a clear fallback when firmware is wedged, asleep, or unable to service
  the button handler.
