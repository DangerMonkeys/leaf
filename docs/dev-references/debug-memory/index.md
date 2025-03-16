---
title: Debugging Memory
description: Document tips for how to debug RAM
---

Today debugging RAM usage isn't great and needs much love. The Leaf only has 512KB of RAM to play with
and most of that is used by WiFi and Bluetooth stacks. Some writeup
https://www.scottyob.com/post/2025-02-27-esp32-memory/

## Enabling Periodic Heap Dump

Dumping memory state over Serial is noisy. To turn on the 1 second dump, you can
uncomment out the line in platformio.ini

```
	; -D MEMORY_PROFILING  # Enables memory profiling troubleshooting features
```

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
