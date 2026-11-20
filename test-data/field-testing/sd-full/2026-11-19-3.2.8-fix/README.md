# SD save protection and startup warning review

See [the implementation and test report](../../../../docs/dev-references/storage-fix-review-2026-11-19.md).

- `candidate.json`: exact source/build hashes and linked size comparison. The preliminary
  image in `candidate-pre-timing.json` was superseded by the final timing-enabled build.
- `screenshots/warning-dialogs.png`: actual simulator-rendered low/full/unavailable dialogs,
  scaled for review; individual native PNGs are beside it.
- Numbered JSON/serial traces preserve every attempt, including failed/discarded test setup
  operations. `12` and `19` cover the final full-card test; `37` and `44` cover successful
  recovery after reconciling a stale free-count hint. `38` is a menu-navigation detour;
  `39` is the successful Stack save. `45` restores the two originals.
- `fat-space-check.json`: read-only evidence of stale FSINFO. `final-space-check.json`:
  matching cached/actual 519 clusters after cleanup. `metadata-check/` retains sectors
  backed up before the targeted hint correction. No FAT/file data repair was performed.
- `harness/`: local drivers/specs/logs as used, with machine-specific paths. The metadata
  reconciliation script is an archived one-time test operation, not a general repair tool;
  its post-write volume flush failed. Do not run it on another card.
- `validation.json`, `verify.py`: compact results and reproducible archive checks.

From the repository root:

```powershell
python test-data/field-testing/sd-full/2026-11-19-3.2.8-fix/verify.py
```

Replay must exit and injection must be confirmed disabled before any automated shutdown.
The host driver writes `full-replay-stopped.json` only after both conditions are met.
This is a test workaround; production shutdown lock dependencies remain proposed work.
Final unit state: charging, original settings/files restored, test filler remains with
8,503,296 bytes free. This archive contains bench data, not release qualification.
