---
title: IGC Format and Reader Compatibility Improvements
description: Proposed improvements to Leaf IGC metadata, measurement encoding, task declarations, and compatibility testing with third-party readers.
---

# IGC Format and Reader Compatibility Improvements

Status: proposed backlog, recorded **2026-11-25**.

Review baseline: `55cb360982612432452d5ab9fbb3f78d0c7dc5ec`, following
[PR #452](https://github.com/DangerMonkeys/leaf/pull/452).
The items below are candidates for investigation and later implementation, not a claim
that every reader rejects current files or that an approval requirement applies to every
Leaf use case.

## Goal

Make Leaf's IGC files easy to import and interpret correctly across third-party readers.
Prefer standard record types and codes where their meaning matches the measured quantity,
declare units and altitude references accurately, and keep necessary Leaf-specific data
in documented manufacturer L records.

Treat file-format compatibility, correct interpretation of measurements, and authenticated
contest validation as distinct acceptance criteria. Select the applicable IGC/CIVL profile
before changing altitude or approval-related behavior.

## Current foundation

- Standard A, H, I/B, C, E, L and G records already provide recorder metadata, track fixes,
  navigation declarations, saved-point events, diagnostics and a checksum.
- PR #452 added standard OAT/HUM fields through a J declaration and timestamped K records
  at a 20-second interval, with 0.1 C and 0.1% RH resolution. Unavailable ambient pairs
  are omitted. See [IGC sensor records](../igc-sensor-records.md) for the exact format.
- Version 2 manufacturer sensor records retain filtered g-load and its original sample
  uptime. Legacy version 1 reading remains supported by the repository's validation tool.
- The combined output checksum covers the new J/K records and library-generated records.
  This remains the existing unkeyed, canonical-LF MD5 scheme.
- [IGC text normalization](../igc-text-normalization.md) already converts profile text to
  supported characters before writing and hashing it.

## First priorities

### Recorder identity and UTC filenames

The A-record identifier is fixed across devices, and filenames use serial `000` and a
local calendar date. H-record dates and fix times use UTC.

Investigate a stable, unique recorder serial shared by the A record and filename. Use the
UTC date of the first recorded fix for the filename, and retain a distinct flight number
for consecutive files. Coordinate identity changes with the signing backlog so future
registration and validation do not introduce a second incompatible identity scheme.

### Header limits and metadata

Cap normalized pilot and glider header text to the applicable record-length limit,
including the record prefix. Keep the existing character normalization. Review recorder,
GNSS receiver and pressure-sensor descriptions against the standard manufacturer/model
layout; populate optional glider or competition identifiers only when the source data exists.

The time-zone header currently truncates fractional-hour offsets. Preserve half-hour and
quarter-hour offsets accurately, or omit the optional header if it cannot be represented
faithfully. Fix timestamps must remain UTC.

### Altitude references

Leaf's pressure altitude uses the standard 1013.25 hPa reference. GNSS altitude is taken
from the receiver's GGA altitude field, which Quectel documents as mean-sea-level/geoid
height. The existing WGS84 coordinate datum header does not explicitly distinguish that
vertical reference from ellipsoid height.

Add accurate ALG/ALP altitude-reference headers. Confirm the intended IGC/CIVL profile
before considering any conversion: strict IGC ellipsoid-height expectations and the
paragliding profile must not be conflated. A conversion would require valid geoid
separation and corresponding fixtures; do not simply relabel the existing values.

### Missing and stale measurements

Several B-record extensions use `000` when unavailable. A reader can interpret these as
real zero speed, calm wind, northward direction or level flight. Speed/course validity
should also include freshness, not only whether a value was once received.

Review same-width unavailable markers supported by the selected standard edition and
target readers. The 2026 amendment permits question marks for unavailable optional data.
Test older readers before changing exported values, and preserve fixed field widths and
I-record offsets. Keep valid zero measurements distinguishable from unavailable data.

## Further record reviews

| Record or field | Proposed investigation |
| --- | --- |
| H/B FXA accuracy | Reconcile the nominal 2 m CEP header and HDOP-derived fix estimate with the standard's approximately 95% accuracy meaning. Use a defensible accuracy source and handle invalid estimates explicitly. |
| I/B GSP, TRT, WDI, WSP | Keep the appropriate standard codes and intended km/h/degrees units. Verify field scaling, precision and freshness through actual reader imports before changing widths. |
| I/B VAR | Widen or otherwise revise the three-character field so sink below -9.9 m/s is not clipped. Retain the correct uncompensated-vario meaning and update I offsets and all dependent readers/tests together. |
| B fix validity and outages | Distinguish 2D, 3D and lost fixes using coherent GNSS inputs. Investigate continuing pressure-altitude records with V validity, last known coordinates, the prescribed unavailable GNSS altitude and an accurate UTC clock during outages. Do not invent new positions. |
| J/K OAT and HUM | Verify actual imports, signs, decimal precision, timestamp association, cadence and unavailable-data handling in target readers. Standard codes alone do not establish that a reader displays the fields. |
| C task declarations | Decide when navigation context constitutes an intentional pre-flight task declaration. Current context can include a remembered or sole loaded route. Review conventional point ordering, role mapping, descriptions, turnpoint counts and one-point routes. |
| E saved-point events | Ensure a PEV precedes an associated B fix at the same UTC timestamp. Define behavior when a usable fix is unavailable. |
| SIU and F satellite information | Consider recording actual satellites in use. Source SIU from coherent fix information and F constellation details from suitable receiver reports; satellites visible in GSV are not equivalent to satellites used. |
| Manufacturer L records | Retain documented g-load timing/peaks, clock anchors, navigation metadata, start/end causes and diagnostics where no equivalent standard quantity exists. Axis acceleration codes are not a substitute for filtered total g-load magnitude. |
| G security | Keep complete checksum coverage and independent tamper controls. Authenticated signing, manufacturer registration and contest acceptance belong to the existing signing/validation backlog. |

For C-record changes, reuse the known first-point rendering investigation in
[IGC task start rendering](igc-task-start-rendering.md). For identity and security work,
coordinate with [signed IGC files and XContest validation](signed-igc-xcontest-validation.md).

## Reader compatibility evidence

Build a small import matrix using the readers Leaf pilots actually use. Pin versions and
retain the input file plus observed imported values, warnings and rendered task points.

Source inspection of GPSBabel shows that its reader skips J/K records and applies fixed
scaling factors to supported B extensions, including a factor of 100 for GSP. This is a
reason to test imported values, not proof of an executed import or a universal reader rule.
Determine the best encoding with fixtures before changing current precision or adding
duplicate fields solely for a particular reader.

## Verification and acceptance

- Generate fixtures through the real firmware/simulator writer and record the exact revision.
  Include legacy version 1 files and new version 2 files.
- Cover UTC midnight, positive/negative time zones, half-hour and quarter-hour offsets,
  consecutive flights and filenames from more than one recorder.
- Exercise long and accented profile names, supported characters, record lengths, declared
  field offsets, record order and negative/zero values.
- Cover explicit altitude references, nonzero geoid separation, 2D/no-fix transitions,
  pressure-altitude continuity, stale speed/course, missing wind/vario and strong sink.
- Include ordinary and minimal routes, explicit task roles, remembered navigation context,
  first-point visibility and PEV/fix timestamp association.
- Check OAT/HUM units and precision, genuine zero readings, stale/missing pairs, recovery,
  interval timing and clock rollover.
- Import fixtures in the agreed reader matrix and compare actual values, not just whether
  the file opens. Record any remaining reader-specific limitations.
- Run the independent checksum validator and altered-record controls after layout changes,
  including changes to declarations and ambient records. Preserve checksum reset between files.
- Follow repository verification requirements for each implementation. Document any schema
  change and migration required by Leaf Log or other existing consumers.

Start with generated-file import tests and the UTC filename, altitude-header and header-length
fixes. Use the results to scope subsequent encoding, task/event and outage changes separately.
Approval or authenticated-signing claims require the separate acceptance process, even when
all format/import checks pass.

## References and implementation entry points

- [FAI/IGC technical specification, Appendix A](https://www.fai.org/sites/default/files/igc_specification_dec_2024_with_al9.pdf)
- [IGC Amendment 10, effective 31 January 2026](https://www.fai.org/sites/default/files/document/file/igc_specification_al10_january_2026_0.pdf)
- [FAI/CIVL flight-recorder specification and acceptance resources](https://www.fai.org/page/xc-instruments-accepted)
- [Quectel LC86G-series GNSS protocol specification](https://www.quectel.com/content/uploads/2024/04/Quectel_LC26GLC26G-TLC76GLC86G_Series_GNSS_Protocol_Specification_V1.5.pdf)
- [GPSBabel IGC reader](https://github.com/GPSBabel/gpsbabel/blob/master/igc.cc)
  and [extension scaling definitions](https://github.com/GPSBabel/gpsbabel/blob/master/igc.h)
- Firmware entry points: `src/vario/logbook/igc.cpp`, `igc.h`, `igc_ambient_log.h`,
  `igc_sensor_log.h`, `igc_record_output.h`, and `src/vario/instruments/gps.cpp`.
- Validation entry points: `sim/tests/igc_sensor_log.cpp`, `g_load_integration.cpp`,
  `check_g_load_igc.py`, `check_igc_text.py`, and `utils/igc_validator/`.
