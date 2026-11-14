# Leaf USB bench console

Build `leaf_3_2_5_bench` for the 3.2.5 bench unit. This environment adds USB
control to the matching release configuration. The operating-mode SD handoff
still revokes host access and waits for pending host I/O. The bench build
reconnects USB after the handoff so the serial console remains available.
The COM port can disappear and reappear during that transition. Reopen the
port after it re-enumerates. Use the same bench environment and hardware for
later memory comparisons.

With a USB data cable, run these commands from the repository root using a
Python environment that has pySerial installed. The script selects a single
Leaf application port automatically. Put `--port COM19` before the subcommand
to specify a port explicitly.

```powershell
python utils/leaf_bench_console.py send version status config heap ip web
python utils/leaf_bench_console.py send "press DOWN" "press UP" status
python utils/leaf_bench_console.py send "hold CENTER 1000"
```

Buttons are `UP`, `DOWN`, `LEFT`, `RIGHT`, and `CENTER`. A press lasts 80 ms.
A hold accepts 850–5000 ms. Firmware handles the synthetic input through the
normal debounce and hold logic, and physical input takes precedence. A button
reply acknowledges that the action was accepted. Use `status` to check the
result. The console cannot make a fully unpowered unit boot.

`status` reports power state, page, the main-menu or top modal cursor, modal depth, warning,
vario readiness, GPS fix, flight timer, and firmware access to the SD card.
`ip` reports the station and access point IPv4 addresses. `web` gives the URL
when the Web App is active. Open the Web App through its normal menu. For HTTP
tests, connect the computer to the same network or to Leaf's access point.

## Memory baseline

Use a cold boot and let the vario finish warming up on a primary page, with
the flight timer stopped. Keep radio, network, SD and diagnostics settings
consistent between runs. Record normal on-state heap with the Web App closed
first. A second run can measure Web App idle as a separate workload.

Pass the exact `.bin` that was flashed. The capture checks for the reported
version in that image and records the binary hash.

```powershell
python utils/leaf_bench_console.py baseline --firmware .pio/build/leaf_3_2_5_bench/firmware.bin --output .pio/bench-baseline.json
```

The report includes the page and configuration, sampled free internal heap,
boot-to-date minimum, largest free internal block, and loop-task stack high
water mark, all in bytes. These values describe a USB-connected bench run.
Keep the console idle except for the same sampling commands when comparing
two images. For later Web App tests, close the app again before comparing
against the normal on-state baseline.
