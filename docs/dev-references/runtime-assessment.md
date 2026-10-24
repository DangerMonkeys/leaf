---
title: Runtime assessment
description: Manual bounded timing capture and baseline reports for release candidates.
---

# Runtime assessment (stage 1)

This is a manual measurement tool, not an automated operational test. Before publishing a
firmware release, collect and review simulator and hardware reports for the fixed candidate,
or record an explicit exception. It is not required for every PR. See [Releasing](releasing.md)
and the [remaining backlog](backlog/runtime-resource-assessment.md).

## Capture on a Leaf

Build the matching hardware's release environment. Enable Developer mode. Leave optional
network/web/vario diagnostic SD logs disabled for a clean timing measurement. Normal IGC
logging should stay enabled when measuring flight behavior.

1. In Menu > Developer, select **Runtime: START**. This allocates a fixed capture buffer of
   bounded internal-RAM buffer before measurement; the exact size is exported as `capture_bytes`.
   Allocation requires space for the buffer plus a 32 KB reserve. A failure
   plays the error sound, shows ERR, and prints the allocation reason on serial.
2. Exercise the intended operating mode manually. Record settings, active page, sensor source,
   radio/client activity, SD card, and elapsed time. Do not assume enabled features were exercised.
3. Select **Runtime: STOP** to freeze and save a new `/diagnostics/runtime_N.json` on the SD
   card. Saving happens after measurement stops; the confirmation sound indicates success.
   The unfinished final slot is retained in the trace but excluded from aggregate statistics.
   If saving fails, the row shows **Runtime: SAVE**; click it to retry without replacing the
   capture. Returning to charging mode also freezes the 10 ms capture, leaving **Runtime: SAVE**
   available to export it later. A successful save returns the row to **Runtime: START**.
4. Retrieve the file after capture, using SD mass storage or the web app. Firmware and host
   must not own the card simultaneously. Starting another capture replaces the in-RAM data;
   previously exported files remain intact. A successful SD export releases the capture buffer;
   a failed export preserves it for retry. The memory type and footprint are reported. Until
   export, frozen storage remains allocated and can be reused by another capture.

Developer HTTP controls are also available when the device webserver is accessible:

```powershell
Invoke-WebRequest -Method Post -Uri 'http://<leaf>/api/debug/runtime' -Body @{action='start'}
Invoke-WebRequest -Method Post -Uri 'http://<leaf>/api/debug/runtime' -Body @{action='freeze'}
Invoke-WebRequest -Method Post -Uri 'http://<leaf>/api/debug/runtime' -Body @{action='export'}
Invoke-WebRequest -Uri 'http://<leaf>/api/debug/runtime/download' -OutFile runtime.json
```

The existing CPU utilization checkbox now enables bounded RAM capture; disabling it freezes.
It no longer writes a wide CSV every second. The menu's STOP action also exports; the HTTP
freeze/export actions remain separate. Commands affect the current
boot; CPU-checkbox settings saved by the existing settings UI still persist normally.

The neighbor CSV opt-in is recorded as `fanet_log` in the captured settings. Keep Log FANET
disabled for clean timing captures. Older reports without this setting remain readable, but
their settings do not exactly match new captures for baseline comparisons.

The web app stops BLE and FANET. For a flight capture, start/stop with the device menu before
opening the web app to retrieve results. Test web activity and transitions in separate captures.
UDP replay exercises downstream firmware but bypasses the real GPS UART path; retain a
real-driver capture for UART and sensor timing.

## Capture in the simulator

Use the normal Docker runner or an already-built simulator. Keep state and SD files isolated:

```sh
./sim/build/leafsim --port 0 --speed 0 --accept-warning \
  --state sim/build/runtime-state --sdcard sim/build/runtime-card \
  --scenario sim/recordings/thermal-climb.json --play --run-seconds 120 \
  --runtime-capture sim/build/runtime-sim.json
```

`--runtime-capture` enables Developer/CPU capture for this process and exports on normal exit.
It requires a finite run or a script. Fatal termination can lose the in-RAM data; preserve the
console output and mark that run failed/incomplete. This flag does not constitute an automated
burdened workload. Radios, webserver, sensor drivers and real storage timing remain stubbed.

Simulator work is measured with the device thread's **host CPU clock**, independently of the
virtual firmware clock. Its bars are gray and scaled to host work; there is no conversion to
ESP32 utilization yet. Deadline timestamps use virtual time and reflect explicit simulated
delays, not ESP32 computation. Device heap/stack values are omitted because the HAL values
are placeholders. Run memory sanitizers separately in a later assessment stage.

Scenario delivery invokes bus subscribers directly. GPS subscriber callbacks are included in
GPS work; other sensor subscriber callbacks are included in services. Recording generation and
wire-format parsing are excluded. Callback costs are attributed to the logical slot active at
delivery; this does not reproduce device UART arrival or real driver scheduling.
Host UDP receiver callbacks run on another thread. They are excluded from loop-owned slot
accounting and counted as unmeasured asynchronous segments, making the report incomplete.
Use scenario playback on the device thread for stage 1 simulator captures.

## Generate and compare reports

The current provisional device reference is
[Leaf 3.2.8 Navigate v1](../../utils/runtime_report/baselines/leaf-3_2_8-navigate-v1/README.md).
Its bundle pins the latest repeat capture, firmware/source identity, known issues and repeat
instructions. Use its `report.json` as `--baseline`; aim for 120 seconds total recorded time
and matching settings/workload. Start from its candidate metadata template so stable scenario
identifiers remain separate from run-specific descriptions. This reference covers Navigate
only and does not replace the wider release assessments. See the
[reference registry](../../utils/runtime_report/baselines/README.md) before selecting a baseline.

Create a metadata JSON alongside the raw capture, for example:

```json
{
  "label": "Candidate flight baseline",
  "commit": "<exact tested commit>",
  "profile": "manual-flight-v1",
  "scenario": "real-drivers-static-bench-v1",
  "toolchain": "<PlatformIO environment and pinned toolchain/container identity>",
  "coverage_notes": "Describe actual radio clients, GPS fix, pages and IGC output",
  "device_id": "<test unit identifier>",
  "sd_card": "<card model and capacity>",
  "limitations": "Static bench run; not flight replay"
}
```

```powershell
python utils/runtime_report/report.py runtime.json --metadata metadata.json `
  --firmware-binary .pio/build/leaf_3_2_8_release/firmware.bin --output report.html
python utils/runtime_report/report.py runtime.json --metadata metadata.json `
  --firmware-binary .pio/build/leaf_3_2_8_release/firmware.bin `
  --baseline previous-report.json --output comparison.html
```

For simulation, hash the exact `leafsim` executable instead. Do not infer an older capture's
commit from the current checkout. Keep dirty-diff evidence when measuring uncommitted work;
release evidence must identify the final clean candidate and its actual firmware artifact.

Outputs are self-contained HTML and a matching report JSON containing telemetry, its raw-file hash,
metadata, and advisory findings. Retain raw captures and metadata too. Baseline comparison is
withheld when capture schema, source, clock, hardware, CPU frequency, capture-buffer size, captured settings,
profile, scenario, or toolchain differ/miss required comparison metadata, slot coverage is
incomplete, or measurement duration differs by more than 10%. Also match device/SD/network conditions
manually. Stage 1 does not issue an automatic release pass or enforce performance thresholds.

The grid is row-major: row 0 is slots 00–09 (first 100 ms), row 1 is 10–19, etc. All slots
include base barometer/button/speaker work; labels identify additional scheduled functions.
Bars show full-capture maxima, with histogram-bounded p95 and sample counts. Maxima in the
100 cells generally occurred in different seconds. Green <5 ms, yellow 5–7.5 ms, orange
7.5–10 ms, red >10 ms or any deadline crossing are provisional review aids. A preceding-slot
and worst-incident table preserves chronological context. Hover cells for component totals.

### Display pages and button recovery

The **Display pages** section covers Basic, User, Thermal Core, Thermal Track, Navigate and the
two debug pages. Menus, warnings and startup/charging overlays are excluded. It reports each
page's render count, mean, maximum, histogram-bounded p95, maximum occupied slot containing a
render, and slot deadline crossings. Unvisited pages are explicitly unmeasured. Render time
includes SPI-lock waiting, drawing and transfer, and is already included in occupied slot time;
do not add it again. A seven-bit `display_page_mask` on each trace slot identifies all rendered
pages (bits 0..6 correspond to render contexts 10..16). Multiple renders in one slot are counted
individually; the enclosing slot is counted once per rendered page.

The normal grid and page summaries exclude slots with a held button or dispatched button event.
Their counts and timing remain in **Buttons and recovery**. The next two dispatched slots are
classified separately as recovery; the window extends until two dispatches settle without a
deadline crossing or more than 1 ms start delay. Recovery retains per-slot counts, peak occupied
time, start/completion delay, coalesced ticks, barometer first-call delay and a worst-event trace.
It does not silently remove downstream misses. Repeated actions reset the window; normal
coverage can therefore be incomplete if the operator navigates continuously.

Synthetic checks cover 10–20 ms late dispatch and timer gaps. These validate accounting and
continued dispatch, not real sensor deadlines or flight correctness. Hardware assessment must
check sensor/GPS/IGC continuity after button actions. No observed button activity means recovery
is untested. Hold each flight page for at least a minute after navigating so normal samples
accumulate, alongside the planned GPS-fix/BLE/flight-timer/IGC workload. Test menus and web
activity separately; opening the web app changes radio operation.

Schema 2 and later captures provide these summaries. Older schema 1 captures remain readable, but are
marked legacy: their aggregate data cannot retrospectively separate button/recovery slots or
provide render durations per page. They cannot serve as a comparable revised baseline. Raw
historical captures are preserved unchanged; obsolete memory fields are omitted from reports.

### GPS sentence profiling (schema 3)

The **GPS sentence handling** section groups GGA, RMC, GSV, GSA, VTG, GLL, ZDA and Other
messages across all talkers. For each type it reports counts, parser/checksum acceptance and
rejection, mean/maximum/histogram-bounded p95 processing cost, sentence deadline crossings,
and maximum completion delay. An accepted checksum does not establish a valid position fix.
These aggregates include button/recovery activity and must not be interpreted as filtered
normal-slot totals. Sentence crossings and slot crossings count different events.

Exclusive stages measure active UART-read chunks, message-bus mutex acquisition, other mutex
acquisition, parsing, immediate GPS updates, downstream `GpsReading` callbacks and BLE enqueue.
Nested waits are removed from the enclosing callback's time. Remaining measured processing is
shown as Other. Costs include blocking/preemption on hardware and use host thread CPU in sim;
they do not identify which other task preempted the loop. BLE transmission occurs in a separate
task; elapsed enqueue time can include that task waking and preempting the caller. The scheduled
`gps.update()` navigation/glide calculations remain ordinary task work in slots 09/59, rather
than sentence-callback stages.

A 100-slot phase chart counts complete-line dispatch starts. Retained events include talker/type,
parser result, logical slot and physical tick, observed first UART-read time, assembled-line/dispatch
start and end, stage costs and button/recovery/input-source flags. Ready/end offsets share the
slot's intended start: end beyond +10 ms is a sentence deadline crossing. The first UART-read
timestamp observes firmware ingestion, not actual arrival on the wire; bytes may already be
buffered. The observed read span includes gaps between fragments. Processing cost sums active
read chunks and dispatch work, excluding those idle gaps. A sentence begun before capture is
marked partial. Injected/scenario messages have no UART ingestion cost or wire-arrival evidence.
Slot GPS work is classified inside the UART driver when bytes are actually available, rather
than from an earlier scheduler availability check. This also covers arrivals between those
checks. One truly empty poll per dispatched slot is sampled separately.

The UART reader checks the task timer between characters, retains partial sentences when the
slot expires, and resumes after the next slot's scheduled tasks. The check uses the existing
atomic tick counter; it does not read a clock or create a telemetry record per character.
Complete-sentence parsing and callbacks still run inline, so a sentence completed near the
boundary can cross its deadline. This intentionally remains visible in the report.

The profiler retains eight recent sentences and the four largest sentence deadline crossings,
plus full-run per-type statistics and phase counts. It emits no sentence text/coordinates and
performs no formatting, allocation or I/O while recording. Recent slot history is eight slots
and memory history sixteen samples; endpoints, sampled minima, and separately retained worst
slot/predecessor records remain intact. The current 3.2.8 capture is 22,496 bytes, only 176 bytes
more than schema 2. The exported size/capacities remain authoritative for each build. Profile
record-bookkeeping maximum is disclosed, but not a complete isolated profiler overhead bound.

Only the loop thread mutates the profiler. Asynchronous GPS injections are counted as excluded;
nested GPS-message dispatches are omitted and counted without overwriting the outer record.
Missing parser observations and excluded dispatches mark profiling incomplete and withhold
baseline comparison. UART buffer resets and partial ingestion are disclosed. Schema 1/2 reports
remain readable but cannot reconstruct these fields; their raw captures remain unchanged.

For diagnosis, capture at least 60–90 seconds with a GPS fix, the same flight page and recorded
BLE/IGC conditions. This should reveal whether a recurring message type starts near a boundary,
which stage consumes time, and whether contention contributes. A separate otherwise comparable
BLE-off capture can test the BLE contribution; do not change workload settings inside one capture.
External UART/GPIO or RTOS tracing is still needed to establish actual wire arrival or isolated
preemption costs.

### Normal production operation

Use one firmware image. With capture off, there is no capture-buffer allocation, periodic
memory sampling, per-slot timestamp collection or timing-log writing. Hooks use inline relaxed atomic
flag reads; capture-only slot calculations are guarded. Developer controls allocate their web
routes only in Developer mode. The timer counter and small control state remain present.
Capture cannot start with Developer mode off, and disabling that mode freezes active capture.
STOP deliberately retains data until export; successful export releases the buffer.

## Accounting and limits

- Expected start is anchored at timer arming, not reset to actual dispatch after a delay.
  `timer_epoch_uncertainty_us` bounds the timer-arming call window. A sequence counter observes
  ticks; the scheduler still dispatches one next logical slot when multiple ticks accumulated.
  Logical slot number is therefore distinct from physical tick number after a stall.
- Assigned tasks, GPS parsing (including partial sentences), and active foreground services
  are measured separately. One empty UART poll pass per dispatched slot is sampled; its total
  is not a measurement of all idle polling. Quiescent service checks avoid per-pass timestamping.
  Assigned completion, last useful-work completion, and
  close/next-dispatch timestamps differ deliberately. A 10 ms span is not 10 ms of work.
  Work spanning a deadline remains attributed to its dispatch interval. Services before the next
  dispatch can contribute to both the previous interval's completion and next start lateness.
  A trace identifies contributors, not proof of which RTOS task caused preemption.
- Elapsed device measurements include blocking and preemption. Empty UART polls are reported
  separately and excluded from occupied bars. The polling task-manager service calls remain
  measured; total device CPU usage is not inferred from these values.
- Fixed storage holds full-run aggregates, eight recent slots, separately retained worst
  slot/predecessor records, sixteen memory samples and the bounded GPS profile. Ring overwrites do not discard aggregates. Capture
  until reboot is lost on a crash; serial/crash evidence is still needed. Start a new capture for
  a new configuration; different tracked endpoint settings flag comparisons as incompatible.
- No CPU telemetry SD writes occur during capture. Heap checkpoints retain RAM records and
  suppress their synchronous SD writes. Other optional diagnostic writers are not redesigned;
  their enabled settings are disclosed. Production IGC and application I/O remain measured.
- Clock-pair calibration, slot-bookkeeping maximum, and memory-sampling maximum are disclosed.
  Bookkeeping and periodic memory sampling consume measured headroom. Timestamp/scope costs,
  memory traffic and the timer ISR counter still have observer effects. An external instrumented
  versus disabled comparison is required before claiming a complete overhead bound.
- Memory is sampled every five seconds plus endpoints. Sampled minima survive ring wrapping;
  allocator low watermarks are boot-lifetime values. Stack zero is ambiguous: unavailable,
  unregistered, or no measured headroom. Positive stack minima skip zero readings; inspect
  raw samples and active task state before drawing conclusions about a zero.
  Allocation failures are counted through the IDF callback while capturing. Samples can miss
  transient fragmentation; no leak conclusion follows from one negative endpoint delta.
- Barometer first-call start delay is recorded relative to the intended slot start. It is a
  useful warning, not validation of the sensor's conversion-ready timing or I2C behavior.

## Verification

```sh
make -C sim test-runtime-capture
make -C sim test-runtime-lifecycle
make -C sim test-runtime-integration
make -C sim test-gps-uart-yield
python -m unittest discover -s utils/runtime_report -p 'test_*.py'
```

These test accounting and report generation. They do not run the future automated workloads
or certify worst-case execution time. See the backlog for calibrated simulator cost models,
verified traffic, recovery cases, sustained heap trends, and release automation.
