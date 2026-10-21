---
title: Debugging Memory
description: Document tips for how to debug RAM
---

Today debugging RAM usage isn't great and needs much love. The Leaf only has 512KB of RAM to play with
and most of that is used by WiFi and Bluetooth stacks. Some writeup
https://www.scottyob.com/post/2025-02-27-esp32-memory/

## Reading memory usage

Enable the runtime `dev_mode` setting, connect Leaf to WiFi, and open `/app/debug/memory` on the device webserver. This returns a memory snapshot; the retired build-time periodic serial dump is no longer available. Existing heap diagnostics remain available.

This will yield:

```
=== Memory Stats ===
Total Heap: 275 KB
Free Heap: 53 KB
Used Heap: 222 KB
Largest Free Block: 30 KB
Minimum Free Heap Ever: 31 KB
Main Task Stack High Water Mark: 12 KB
Free PSRAM: 0 bytes
Largest Free PSRAM Block: 0 bytes
====================
```
