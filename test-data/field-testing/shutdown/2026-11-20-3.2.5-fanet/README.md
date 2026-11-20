# Live FANET and Bluetooth shutdown tests, November 20, 2026

[Full report](../../../../docs/dev-references/shutdown-radio-test-2026-11-20.md).

Two combined shutdowns and menu/Web App pause/resume passed on source `8a435a42`.
The user confirmed phone connections. Nearby peers supplied 125 logged observations,
with zero logger queue drops. Strict IGC validation fails the known Unicode pilot-name
header; raw G checksums match. Preserve that distinction when using these results.

- `candidate.json`, build/format/flash logs: exact identity and both app-slot verification.
- `01`–`04`: preparation; `05`–`09`: first connected flight, menu cycles, reception log,
  shutdown and card capture. `06` selected the adjacent forwarding setting; `07` restored it.
- `10`–`12`: wake (COM handoff/reopen), phone reconnection, second Stack/flight start.
- `13`–`17`: Web App pause/resume, read-only HTTP checks, restored logging preference.
  The nonexistent `/api/debug/memory` probe returned 404; main web checks returned 200.
- `18`: second phone-confirmed shutdown, timed from hold-command start (5.125 s).
- `19`–`22`: charging upload observation, normal cancellation, USB handoff and final capture.
- `card-captures/22-final-results`: both unchanged IGCs, complete logbooks, Stack CSVs,
  reception CSVs, and route/profile/waypoint reference copies.
- `analysis.json`: checksums, strict-validator errors, radio counts, stacks and timing events.
- `harness/`: exact local drivers, with machine-specific paths and card identity checks.
- `verify.py`, `validation.json`: reproducible archive checks and scoped outcomes.

```powershell
python test-data/field-testing/shutdown/2026-11-20-3.2.5-fanet/verify.py
```

The Leaf remains charging, original configuration restored, recordings retained on SD.
No firmware changes were needed. Vario timing, full-card behavior and release qualification
were not established by this workload. One 136 ms SD write and FIFO event are retained
for the vario timing investigation.
