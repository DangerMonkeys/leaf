# CPU Utilization 2 Report

Source: `scratch/diagnostics_logs/cpu_utilization2.csv`

Comparison source: `scratch/diagnostics_logs/cpu_utilization.csv`

Capture size: 418 one-second rows, from `millis=152265` to `millis=574453`. The last ~40 seconds are treated as the active flight log/timer window.

Percent values are `block_us / 10000us * 100`, so 100% means a 10 ms task-manager block was fully consumed.

Button masks:

- `1`: UP
- `2`: DOWN
- `4`: LEFT
- `8`: RIGHT
- `16`: CENTER
- `32`: event without a button-specific mask

## Likely Audio Stall Cause

The half-second vario sound stall near the end is almost certainly the CPU utilization diagnostic writing to SD card.

The strongest signature is:

| millis | sequence | block | scheduled work | block time | button mask | event mask | previous write |
|---:|---:|---|---|---:|---:|---:|---:|
| 563373 | 408 | `b03` | CPU CSV write | 150.4 ms | 0 | 0 | 15.8 ms |
| 564553 | 409 | `b03` | CPU CSV write | 558.5 ms | 0 | 0 | 149.2 ms |
| 566093 | 410 | `b03` | CPU CSV write | 278.2 ms | 0 | 0 | 557.3 ms |

The `b03` sample at `millis=564553` is 5585% of a 10 ms slot, or about 558.5 ms. The next row's `previous_write_us` is 557305 us, which independently confirms the previous CPU-utilization SD write took about 557 ms.

This aligns very closely with the reported "stuck for maybe half a second" speaker behavior. It was not marked as button-contaminated.

There were nearby button-related spikes too, but they are smaller:

| millis | block | group | block time | button/event |
|---:|---|---|---:|---|
| 560373 | `b77` | IMU only | 16.1 ms | RIGHT event |
| 563373 | `b62` | IMU + wind | 58.3 ms | RIGHT event |
| 572373 | `b81` | base-only | 90.5 ms | CENTER held + event |

Those are real interactions, but they do not explain the half-second audio stall as well as the `b03` SD write does.

## New Log Summary

All samples versus no-button samples:

| Group | All avg % | All max % | All >100% | No-button avg % | No-button max % | No-button >100% |
|---|---:|---:|---:|---:|---:|---:|
| CPU log write | 278.1 | 6919.4 | 417 | 276.8 | 6919.4 | 412 |
| Display | 59.5 | 293.0 | 8 | 59.4 | 293.0 | 7 |
| IMU + wind | 27.7 | 582.8 | 2 | 27.5 | 68.3 | 0 |
| IMU only | 26.6 | 161.4 | 10 | 26.6 | 144.1 | 9 |
| Log | 10.4 | 1660.0 | 2 | 6.4 | 484.9 | 1 |
| Baro kickoff | 6.5 | 222.2 | 4 | 6.3 | 21.7 | 0 |
| Base only | 3.8 | 904.9 | 10 | 3.7 | 13.2 | 0 |
| GPS | 8.7 | 12.8 | 0 | 8.7 | 12.8 | 0 |
| Power | 10.2 | 72.7 | 0 | 10.0 | 12.2 | 0 |
| Temp/RH trigger | 6.1 | 10.2 | 0 | 6.1 | 10.2 | 0 |
| SD card check | 6.2 | 9.7 | 0 | 6.2 | 9.7 | 0 |
| Temp/RH read | 6.1 | 12.3 | 0 | 6.1 | 12.3 | 0 |
| Available slot | 4.5 | 123.9 | 1 | 4.2 | 8.4 | 0 |

Button filtering is useful. In this capture:

- Full log: 506 of 41799 block samples were button-contaminated, about 1.2%.
- Last 40 seconds: 326 of 3900 block samples were button-contaminated, about 8.4%.
- Last 15 seconds: 326 of 1400 block samples were button-contaminated, about 23.3%.

So the tail has a lot of user input, but the biggest audio-stall-sized event was not button-related.

## Active Flight Window

Last 40 seconds, all samples versus no-button samples:

| Group | All avg % | All p99 % | All max % | All >100% | No-button avg % | No-button p99 % | No-button max % | No-button >100% |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| CPU log write | 406.8 | 2782.3 | 5585.2 | 39 | 426.5 | 2782.3 | 5585.2 | 36 |
| Display | 68.2 | 111.6 | 118.0 | 2 | 68.1 | 111.6 | 118.0 | 2 |
| IMU + wind | 29.7 | 33.4 | 582.8 | 1 | 28.1 | 32.6 | 45.1 | 0 |
| IMU only | 28.6 | 69.0 | 161.4 | 3 | 28.3 | 35.3 | 144.1 | 2 |
| Log | 14.2 | 31.0 | 31.3 | 0 | 14.5 | 31.0 | 31.3 | 0 |
| Baro kickoff | 7.4 | 11.6 | 222.2 | 2 | 6.8 | 11.2 | 21.7 | 0 |
| Base only | 4.9 | 9.2 | 904.9 | 4 | 4.0 | 7.9 | 13.2 | 0 |
| GPS | 9.2 | 12.0 | 12.8 | 0 | 9.1 | 12.0 | 12.8 | 0 |
| Power | 10.3 | 12.2 | 15.3 | 0 | 10.2 | 10.9 | 12.2 | 0 |
| Temp/RH trigger | 6.2 | 9.5 | 10.2 | 0 | 6.1 | 9.5 | 10.2 | 0 |
| SD card check | 6.4 | 9.0 | 9.7 | 0 | 6.4 | 9.0 | 9.7 | 0 |
| Temp/RH read | 6.7 | 9.9 | 11.5 | 0 | 6.7 | 9.9 | 11.5 | 0 |
| Available slot | 4.4 | 5.6 | 8.4 | 0 | 4.4 | 5.6 | 8.4 | 0 |

During active logging, `log_update` rises from a full-capture no-button average of 6.4% to about 14.5%, but remains well below overrun in the final 40 seconds. Active flight logging is adding work, but it is not the dominant stall source in this capture.

Display rises from about 59% overall to about 68% in the active window, with two slight overruns around 112-118%. Display remains the primary normal workload.

## Comparison To Previous Log

| Group | Previous avg % | Previous max % | Previous >100% | New all avg % | New all max % | New all >100% | New no-button avg % | New no-button max % | New no-button >100% |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| CPU log write | 215.9 | 10700.8 | 348 | 278.1 | 6919.4 | 417 | 276.8 | 6919.4 | 412 |
| Display | 62.3 | 1174.5 | 6 | 59.5 | 293.0 | 8 | 59.4 | 293.0 | 7 |
| IMU + wind | 30.3 | 178.4 | 1 | 27.7 | 582.8 | 2 | 27.5 | 68.3 | 0 |
| IMU only | 29.8 | 249.1 | 8 | 26.6 | 161.4 | 10 | 26.6 | 144.1 | 9 |
| Log | 11.0 | 753.1 | 3 | 10.4 | 1660.0 | 2 | 6.4 | 484.9 | 1 |
| Baro kickoff | 8.5 | 127.3 | 2 | 6.5 | 222.2 | 4 | 6.3 | 21.7 | 0 |
| Base only | 5.9 | 1438.3 | 5 | 3.8 | 904.9 | 10 | 3.7 | 13.2 | 0 |
| GPS | 11.3 | 16.3 | 0 | 8.7 | 12.8 | 0 | 8.7 | 12.8 | 0 |
| Power | 12.4 | 17.4 | 0 | 10.2 | 72.7 | 0 | 10.0 | 12.2 | 0 |
| Temp/RH trigger | 8.4 | 62.0 | 0 | 6.1 | 10.2 | 0 | 6.1 | 10.2 | 0 |
| SD card check | 8.3 | 13.1 | 0 | 6.2 | 9.7 | 0 | 6.2 | 9.7 | 0 |
| Temp/RH read | 8.4 | 13.3 | 0 | 6.1 | 12.3 | 0 | 6.1 | 12.3 | 0 |
| Available slot | 6.3 | 59.9 | 0 | 4.5 | 123.9 | 1 | 4.2 | 8.4 | 0 |

Refined interpretation:

1. The CPU utilization diagnostic writer is still the largest and most disruptive source. It is worse in the active/logging capture, and it directly matches the heard audio stall.
2. Button filtering explains many previously mysterious base-only, baro-kickoff, power, and IMU+wind outliers. For example, new no-button base-only max drops to 13.2%, and baro-kickoff max drops to 21.7%.
3. Display remains a real non-button workload. It usually sits around 60-70% and occasionally overruns. This is the main normal scheduled task to watch.
4. `log_update` can spike independently of button input. In the new log, the largest log spike is button-contaminated, but a no-button log spike still reaches 484.9%.
5. IMU-only still has some no-button overruns, though far smaller than the SD-write stalls.

## Recommendations

1. For future profiling, disable or reduce the CPU-utilization SD writer when evaluating audio smoothness. It is now clearly able to create audible stalls.
2. Add missed-tick accounting via a timer ISR sequence counter. The long SD stalls are large enough that the current boolean timer flag can hide how many 10 ms ticks were missed.
3. Generate future reports with at least three views: all samples, no-button samples, and active-flight no-button samples.
4. If keeping CSV output, consider writing every 5-10 seconds or using a compact binary/ring-buffer dump to reduce SD append frequency and FAT metadata churn.
5. Separately profile display and `log_update` after reducing observer effect; those are the remaining plausible real workload trouble spots.
