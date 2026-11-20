# IGC text checksum regression evidence

Synthetic recordings generated on November 20, 2026, using the real firmware logger in
the simulator. The January 2025 recording date comes from the scenario, not a field flight.

- `tracks/`: four consecutive, byte-preserved generated IGC files.
- `checksum-windows.json`, `checksum-linux.json`: four original-file passes and eight
  altered-file rejections per validator, including original-file SHA-256 and G records.
- `candidate.json`: source hashes and local 3.2.6 release-build identity.

[Behavior, test procedure and acceptance limits](https://leafvario.com/dev-references/igc-text-normalization/).

```powershell
python sim/tests/check_igc_text.py --validator utils/igc_validator/dist/vali-xlv.exe --tracks test-data/igc-text/2026-11-20/tracks
```

The eight altered copies are temporary and are not archived as flights. Original stored
profile text is separately asserted unchanged by `sim/tests/g_load_integration.cpp`.
These checksum results do not establish authenticated signing or contest approval.
