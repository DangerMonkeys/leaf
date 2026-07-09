---
title: Factory Commissioning Follow-up
description: Future hardening notes for factory_interface commissioning and flashing behavior.
---

# Factory Commissioning Follow-up

The factory flash flow currently erases the NVS partition before writing firmware so previously
commissioned units can be recommissioned. The current Leaf partition table uses the standard Arduino
ESP32 NVS range:

- Partition: `nvs`
- Type/subtype: `data`, `nvs`
- Offset: `0x9000`
- Size: `0x5000` (`20K`)

This was verified against both the configured `default_8MB.csv` partition table and the generated
`leaf_3_2_6_release` `partitions.bin` artifact.

Future work:

- Replace the factory_interface hardcoded NVS erase range with partition-table-derived values.
- Prefer parsing the configured/generated partition table and erasing the partition named `nvs`.
- Keep the current hardcoded range acceptable while Leaf continues to use the standard
  `default_8MB.csv` layout.
