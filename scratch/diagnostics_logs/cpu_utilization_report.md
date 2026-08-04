# CPU Utilization Report

Source: `scratch/diagnostics_logs/cpu_utilization.csv`

Capture size: 349 one-second rows, from `millis=3922` to `millis=355295`.

Percent values are `block_us / 10000us * 100`, so 100% means a 10 ms task-manager block was fully consumed. Min/avg/max below exclude only zero-valued partial startup cells.

## Unique Scheduled Blocks

| Group | Blocks | Scheduled work | Samples | Min % | Avg % | Max % | >100% | 80-100% |
|---|---|---|---:|---:|---:|---:|---:|---:|
| CPU log write | `b03` | `cpu_utilization::writePendingReport` + base tasks | 349 | 2.8 | 215.9 | 10700.8 | 348 | 0 |
| Display | `b39`, `b89` | `display.update` + base tasks | 698 | 52.1 | 62.3 | 1174.5 | 6 | 1 |
| IMU + wind | `b02`, `b12`, `b22`, `b32`, `b42`, `b52`, `b62`, `b72`, `b82`, `b92` | `ICM20948::update`, `windEstimator.estimateWind` + base tasks | 3490 | 6.9 | 30.3 | 178.4 | 1 | 1 |
| IMU only | `b07`, `b17`, `b27`, `b37`, `b47`, `b57`, `b67`, `b77`, `b87`, `b97` | `ICM20948::update` + base tasks | 3490 | 5.4 | 29.8 | 249.1 | 8 | 3 |
| Power | `b19` | `power.update` + base tasks | 349 | 6.3 | 12.4 | 17.4 | 0 | 0 |
| GPS | `b09`, `b59` | `gps.update` + base tasks | 698 | 3.4 | 11.3 | 16.3 | 0 | 0 |
| Log | `b29` | `log_update` + base tasks | 349 | 3.1 | 11.0 | 753.1 | 3 | 0 |
| Baro kickoff | all `c10=0` and `c10=5` blocks | `ms5611.update` kickoff + base tasks | 6979 | 0.1 | 8.5 | 127.3 | 2 | 0 |
| Temp/RH trigger | `b49` | `aht20.update` trigger phase + base tasks | 349 | 3.0 | 8.4 | 62.0 | 0 | 0 |
| SD card check | `b69` | `sdcard.update` + base tasks | 349 | 2.5 | 8.3 | 13.1 | 0 | 0 |
| Temp/RH read | `b99` | `aht20.update` read/process phase + base tasks | 349 | 1.9 | 8.4 | 13.3 | 0 | 0 |
| Available slot | `b79` | no scheduled task, or `memoryStats` if built with profiling + base tasks | 349 | 0.3 | 6.3 | 59.9 | 0 | 0 |
| Base only | remaining `c10=1/3/4/6/8` blocks except `b03` | `baro/buttons/speaker` + `diagnostic_network.update` + `factoryDiscovery.update` | 17101 | 0.1 | 5.9 | 1438.3 | 5 | 0 |

Base tasks common to every measured block:

- `ms5611.update` via `performTask.baro`
- `buttons.update`
- `speaker.update`
- `diagnostic_network.update`
- `factoryDiscovery.update`
- `selfTest.update` only when `selfTest.updateNeeded()` is true

## Warmed-Up View

Excluding the first three seconds removes the initial CPU log file create/header write but keeps later SD latency spikes.

| Group | Min % | Avg % | P95 % | P99 % | Max % | >100% |
|---|---:|---:|---:|---:|---:|---:|
| CPU log write | 106.4 | 186.4 | 143.5 | 2768.0 | 4193.0 | 346 |
| Display | 52.1 | 61.9 | 66.3 | 71.0 | 1174.5 | 1 |
| IMU + wind | 7.6 | 30.4 | 34.3 | 36.0 | 178.4 | 1 |
| IMU only | 5.5 | 29.8 | 33.6 | 35.6 | 249.1 | 7 |
| Power | 6.3 | 12.4 | 14.1 | 15.4 | 17.4 | 0 |
| GPS | 5.8 | 11.3 | 13.4 | 15.1 | 16.3 | 0 |
| Log | 3.1 | 11.1 | 12.5 | 30.3 | 753.1 | 3 |
| Baro kickoff | 0.1 | 8.5 | 10.8 | 13.1 | 127.3 | 2 |
| Temp/RH trigger | 3.0 | 8.4 | 11.8 | 12.6 | 62.0 | 0 |
| SD card check | 2.5 | 8.4 | 10.0 | 12.3 | 13.1 | 0 |
| Temp/RH read | 1.9 | 8.4 | 11.9 | 12.4 | 13.3 | 0 |
| Available slot | 0.3 | 6.4 | 7.7 | 9.0 | 59.9 | 0 |
| Base only | 0.1 | 5.9 | 8.0 | 9.7 | 1438.3 | 4 |

## Overrun And Risk Summary

### Critical

`b03`, the CPU utilization CSV writer, is consistently too large for a single 10 ms block. After warm-up, it averages 186% of one block, meaning about 18.6 ms. This fits the idea that slots `3` and `4` are effectively being consumed together, but the task manager still only records `b03` as overrun. The first real file/header write hit 10700.8%: about 1.07 seconds.

There are also later SD write stalls:

- `millis=127186` to `129986`: `b03` reached 4139.6%, 4193.0%, and 1430.6%.
- `millis=257117` to `259786`: `b03` reached 2825.2%, 4178.0%, and 2768.0%.

These are large enough to collapse multiple 10 ms timer events into one pending boolean, so the current diagnostic can under-report missed scheduler ticks during those stalls.

### Worth Watching

Display is the largest normal scheduled block. It averages about 62% of a 10 ms slot and normally stays under 75%, but it had one warmed-up overrun and one startup outlier at 1174.5%. If display work grows, this is the first non-diagnostic block likely to become tight.

IMU blocks are healthy on average, around 30%, but several `IMU only` samples exceeded 100%. These appear sporadic rather than steady-state pressure.

`log_update` is normally light, about 11%, but had three large outliers, including 753.1%. Those may correspond to file I/O or flush behavior.

Base-only blocks had a few large outliers despite no special scheduled work. That suggests some unlabelled/common work, interrupt effects, or diagnostic/SD side effects can appear outside the obvious scheduled function labels.

### Comfortable

GPS, power, temp/RH, and the explicit `sdcard.update` slot are comfortably below budget in this capture. The explicit SD-card check is not the problem here; appending the CPU utilization CSV is.

## Recommendations

1. Keep this diagnostic as a development-only tool; it materially perturbs the scheduler when enabled.
2. Add a timer ISR sequence counter if we keep analyzing overruns. The current `nextTaskTimerBlock` boolean can collapse many missed 10 ms ticks into one event during long SD stalls.
3. Consider buffering multiple seconds and writing less often, or writing a more compact binary/fixed-width format, if the goal is to observe normal CPU utilization with less observer effect.
4. Treat display as the main normal workload to monitor, and `log_update` as the likely next I/O-related suspect.
