---
title: IGC special-character normalization and checksum verification
description: ASCII normalization of IGC text, preserved profile values, checksum evidence, and the boundary between integrity checks and contest acceptance.
---

# IGC text normalization and checksum verification

**Status:** Implemented on `codex/igc-header-text`; original fix `00275016`,
integrated with main `ba64a320` on November 20, 2026.
**Scope:** Newly written IGC text. Existing recordings and stored profile names retain their original bytes.

## Behavior

Pilot and glider headers now use the same bounded IGC text conversion as navigation declarations,
saved-point events and navigation comments. Common Latin accents become ASCII base letters;
curly apostrophes/quotes and dashes become their ASCII equivalents; selected letters expand
to forms such as AE, OE or ss. Combining accents are dropped. Unsupported or malformed
characters, controls and reserved symbols become spaces. Empty normalized profile values use
the existing Unknown fallback. Output length limits remain enforced.

For example, `José O’Neiĺ\r\nJr.` is written as `Jose O'Neil  Jr.`, and
`Crème – Æro^2` as `Creme - AEro 2`. The original UTF-8 profile values remain in the
profile store and are checked by the simulator test. Consecutive flights rebuild their
headers after the existing header-storage cleanup; an unsupported name cannot retain the
previous pilot or glider.

The output follows the character range and reserved-symbol rules in
[FAI IGC specification, Appendix A6](https://www.fai.org/sites/default/files/igc_specification_2024_with_al9_0.pdf).
This is limited transliteration, not a general translation of every writing system.

## Checksum ordering

Conversion occurs before `IgcLogger` writes a record and feeds that record to its incremental
MD5 calculation. The final G record therefore describes the normalized bytes actually emitted.
Each record contributes its contents followed by one LF to the digest, independently of the
physical LF/CRLF line ending. The G record is excluded from its own digest.

A changed header naturally produces a different checksum. The requirement is that the new
checksum matches the new content. No post-recording conversion or checksum repair is performed
on archived flights.

## Recorded verification

The [evidence bundle](https://github.com/DangerMonkeys/leaf/tree/main/test-data/igc-text/2026-11-20)
contains four unmodified simulator-generated IGCs, Windows/Linux validator reports, source
hashes and the local release-build identity. Their January 2025 dates come from the synthetic
scenario; they are not physical flights.

| Consecutive flight | Expected header behavior | Windows C checker | Linux C checker |
| --- | --- | --- | --- |
| Long ASCII pilot and glider | Original text retained | Passed | Passed |
| Accents, curly punctuation, combining accent, CR/LF and reserved caret | Expected ASCII text, no injected records | Passed | Passed |
| Unsupported-only pilot and glider | Unknown; previous names not retained | Passed | Passed |
| Missing profiles | Unknown after prior header storage was freed | Passed | Passed |

Both independently built C checkers accepted all four original files. Python also recomputed
their canonical-LF MD5 values and verified the final G records. Every output record passed
the A6 character check. Each checker rejected eight temporary copies: one pilot-header byte
and one GPS-fix byte altered in each file, while leaving its G record unchanged. Each
rejection was specifically `G checksum mismatch`, rather than an unrelated parse error.

The simulator's G-load/header integration and logbook I/O regression suites passed, and
`leaf_3_2_6_release` built successfully. The local build integrated the fix and current main
before the merge commit was created; source hashes identify the tested tree. No physical-flight
capture or scoring-site submission is claimed.

## Repeat the checks

Generate new files and validate them with the native C checker in the simulator container
(OpenSSL headers and libcrypto are required):

```sh
make -C sim -j8 test-igc-text
```

This runs the real firmware logger in four isolated consecutive flights, then checks the
resulting files and tamper controls. To validate the archived evidence on Windows:

```powershell
python sim/tests/check_igc_text.py --validator utils/igc_validator/dist/vali-xlv.exe --tracks test-data/igc-text/2026-11-20/tracks
```

The IGC text checksums workflow builds the C checker from source and validates the archived
fixtures and negative controls on PRs/main. That CI fixture check protects the evidence and
validator; the simulator target separately exercises the current firmware producer.
Git attributes preserve the fixture bytes rather than normalizing their line endings.

## Contest acceptance remains separate

Leaf currently uses an unkeyed MD5 G record. A passing local validator establishes compatibility
with that integrity scheme, not authenticated recorder signing, CIVL registration or deployment
of a manufacturer validator by a scoring service. The algorithm and logger dependency are
unchanged by this fix.

[XContest's FAQ](https://www.xcontest.org/2026/world/en/faq/) describes its requirement for a
proper G-record signature. Keep external acceptance and authenticated signing under the
[signed-IGC backlog](https://leafvario.com/dev-references/backlog/signed-igc-xcontest-validation/).
The November 20 field captures with the old Unicode header remain preserved as recorded;
this fix does not retroactively make those files format-valid.
