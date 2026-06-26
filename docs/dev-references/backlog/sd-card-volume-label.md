---
title: SD Card Volume Label Backlog
description: Future improvement for keeping the Leaf SD card FAT volume label stable.
---

# SD Card Volume Label Backlog

Leaf sets the SD card FAT volume label to `LEAF VARIO` during some SD card setup/format paths, but field testing showed the label can later appear blank in Windows after the card was mounted through a phone over USB mass storage. Windows then displays the volume as a generic `USB Drive`, even though the USB device/product identity still appears branded.

Future work:

- Reassert the FAT volume label during normal SD mount, not only during formatting or production/self-test paths.
- Keep this best-effort so label failure does not block normal SD use.
- Confirm the label remains `LEAF VARIO` after mounting from Windows and Android/iOS hosts over USB mass storage.
