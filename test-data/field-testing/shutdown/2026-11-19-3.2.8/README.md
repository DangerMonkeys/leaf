# Deferred shutdown regression, November 19, 2026

See [the implementation and test report](../../../../docs/dev-references/shutdown-fix-review-2026-11-19.md).

- `candidate.json`: exact final source/build hashes and linked size changes; the firmware
  was built as changes on storage commit `45e599af`.
- `flash.json`, `flash-*.txt`: automatic port detection, app-mode nudge, retry and both OTA slots.
- `shutdown-fix-sim-passed.log`: complete passing final regression run. Earlier logs retain
  the thermal test's outdated provisional filename assertion; the test now follows flight ID.
- `12`–`14`: flight power-off while host replay continued, charging verification and saved
  files. `15`–`20`: wake, retained opt-in, fresh injected GPS and another replay-active shutdown.
- `06`–`10`: driver navigation mistakes selected a field instead of requesting shutdown;
  both recordings auto-stopped. These attempts are not counted as shutdown failures/passes.
- `*-producer.json`: start/stop timestamps, send count and confirmation the host did not
  disable injection. `power-off` and `wake-cycle` overlapped their charging checks.
- `card-captures/14-writable-results/`: all three valid IGCs, completed logbook entries and
  shutdown-saved Stack CSV. `igc-validation.json` records checksum results.
- `23`–`27`: restored session/settings, normal final shutdown and original file restoration.
- `harness/`: exact local drivers/specs. They contain machine-specific paths and operate
  only on the identified test card. No filler or FAT metadata operation was used here.
- `verify.py`, `validation.json`: reproducible saved-evidence checks and compact results.

```powershell
python test-data/field-testing/shutdown/2026-11-19-3.2.8/verify.py
```

The final unit is charging, originals/settings restored, with 8,503,296 bytes free.
The FANET module and a connected Bluetooth phone were not available. These are bench
regressions, not release qualification.
