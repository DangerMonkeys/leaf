---
title: RTOS
description: FreeRTOS related documents
---

FreeRTOS on the ESP32 has a number of tasks already setup. The default loop() function is on priority 1. Idle tasks are priority 0. There are 25 priorities configured on the ESP32. The

- Main Runloop: 10
  - Everything that hasn't been ported over yet.
  - General tasks that don't need to be real time.
- High Priority Tasks: 20-25
  - Things that power user experience, buzzer
- Less High Priority Tasks: 11-20
  - Things that collect sensor data
- Low Priority: 5-10
  - House-keeping tasks. Things like flushing SD to disk.

| Priority | Task    | Description                                            |
| -------- | ------- | ------------------------------------------------------ |
| 10       | taskman | Legacy monolithic task manager. Things not updated yet |
| 9        | Ble     | Bluetooth Low Energy send updates                      |
