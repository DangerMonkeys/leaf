# Leaf 3.2.8 full-card bench evidence — November 19, 2026

Read the [hardware report](../../../../docs/dev-references/sd-full-test-2026-11-19.md)
for outcomes and limits. This is automated indoor bench testing with synthetic GPS and
physical pressure/motion sensors, not field-flight qualification.

- `candidate.json`: source, environment, firmware/ELF hashes, device identity.
- Numbered JSON/text files: USB commands and serial output, HTTP requests/results and timestamps.
- PNGs: rendered device screenshots. XBM files preserve captured framebuffers. The early
  `web-full.xbm` filename was reused; it contains the later request's image, whose hash is in
  `49-near-web-writes.json`. `runtime-full.png` preserves the earlier Runtime SAVE screen;
  `flight-prompt.xbm` preserves the later flight summary captured using the same XBM filename.
- `*-card.json`: free space, saved-file lengths, JSON parse status and SHA-256 hashes.
- `card-captures/<step>/`: exact file bytes, including intentionally preserved empty outputs.
- `validation.json`: archive verification and IGC checksum outcomes. Expected device failures
  are findings, not failures of archive preservation.
- `harness/`: exact local drivers, request fixtures, flash logs and build result. These include
  machine-specific paths and identify volume `01114FC5`; review before adapting or rerunning.
  Never run the card mutation helper against an unidentified volume.

The original CUP and navigation-selection files were restored and hash checked. The temporary
`LEAF_SD_FULL_TEST` filler remains on the card, with zero free bytes. All other captured
disposable outputs were removed from the card; their evidence remains here. The plugged-in
unit was left charging, on console-enabled candidate `0.2.4-b1ed.bench+h3.2.8`.

Times in raw evidence use UTC (November 20); the local test date is November 19.
