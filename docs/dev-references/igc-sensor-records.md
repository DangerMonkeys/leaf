---
title: Compact IGC sensor records
---

# Compact IGC sensor records (version 2)

Leaf records filtered g-load in manufacturer L records (`LXLV`), and ambient temperature
and relative humidity in standard IGC J/K records. All are covered by the G-record checksum.
Other IGC readers may ignore manufacturer records. The g-load schema declaration is:

```text
LXLVSENSOR V=2 G=MG HEX=1
```

Version 2 removes temperature from the version-1 S record. The uptime/g-load offsets and
clock anchor format are unchanged. Readers must select the S length from the declaration.
Version-1 files use `LXLVSENSOR V=1 G=MG T=DECI_C HEX=1` and have a three-character
signed deci-C field after g-load; they have no humidity data. For those legacy temperatures,
subtract 4096 from encoded values at least 2048, divide by 10, and treat `800` as unavailable.

## Standard ambient records

IGC Appendix A3.4, A4.4 and A7 define J/K records and the OAT (outside air temperature,
Celsius) and HUM (relative humidity, percent) fields. See the
[FAI specification](https://www.fai.org/sites/default/files/igc_specification_dec_2024_with_al9.pdf)
and [2026 Amendment 10](https://www.fai.org/sites/default/files/document/file/igc_specification_al10_january_2026_0.pdf).
Leaf writes the J declaration immediately after I, before any task declaration or fixes:

```text
J020813OAT1418HUM
K1300000018.0065.4
K130020-012.3100.0
```

The J record declares two fields, using one-based positions from the start of each K line:

| Positions | Meaning | Example |
| --- | --- | --- |
| 1 | K record type | `K` |
| 2-7 | UTC time, HHMMSS | `130000` |
| 8-13 | OAT, decimal Celsius, six characters | `0018.0` = 18.0 C; `-012.3` = -12.3 C |
| 14-18 | HUM, decimal percent RH, five characters | `065.4` = 65.4%; `100.0` = 100% |

The explicit decimal points preserve 0.1 C and 0.1% RH resolution in the standard units;
this is storage resolution, not sensor accuracy. The temperature comes from the ambient
AHT20 path, including its existing compensation. These are current readings at the K
record's own UTC time, independent of any preserved g-load peak's sample uptime.

The first usable ambient pair is written with a GPS B fix, then at most once every 20 seconds,
the standard's recommended default K interval. Missing/stale ambient readings, non-finite
values, temperature outside -204.7 to 204.7 C, or humidity outside 0-100% before rounding
cause that K record to be omitted. A valid pair can be written as soon as the interval is due
again. No custom sentinel or fabricated zero is used in OAT/HUM. During GPS outages,
g-load peaks may still be written, but no K record is written without an associated usable fix.

There is no flight-summary record, pre-launch sample cache, bucket reference, eligibility
flag, or commit/discard message in the IGC. Recording starts at the first usable GPS fix.
Leaf Log will need to parse these records and choose its own flight window for g-load
statistics, using the track and the existing recording-start/end information.

## Fixed-width records

All numeric fields below are uppercase hexadecimal, except the clock's UTC calendar.
Offsets are zero-based within the payload **after `LXLV`**.

| Payload | Fields | Bytes including `LXLV` and LF |
| --- | --- | ---: |
| `Smmmmmmmmgggg` | G-load sample: uptime ms (8), milli-g (4) | 18 |
| `AmmmmmmmmDDMMYYHHMMSScc` | GPS clock anchor: uptime at GPS time receipt, UTC date/time and centiseconds | 28 |

The S offsets are: type 0, uptime 1-8, g-load 9-12. G-load is an unsigned integer in
**0.001 g** units. There is no flags field. G-load `FFFF` denotes a missing/unavailable
value, not a physical measurement. The IGC VAR field is unchanged, including its existing
`000` representation for unavailable climb.

A clock anchor precedes the first B/S pair, repeats at least hourly when GPS records are
available, and is refreshed if the GPS/uptime offset changes by more than two seconds.
Use modular 32-bit millisecond subtraction relative to a nearby anchor. GPS receipt time
is an approximation to sensor-to-UTC alignment, subject to receiver/serial latency;
the original sensor uptime is retained to millisecond resolution. UTC date handles midnight.

## Peak preservation and reader classification

The sampled signal is the same slew-limited, roughly half-second filtered magnitude used
by the LCD and device g-load extrema. Most seconds record its latest fresh value. When an
internal pending range reaches an unrecorded extreme, S substitutes that extreme at its
original sensor time. If both bounds changed, two full S records carry them. One suffices
if they encode the same milli-g value. Additional S records preserve missing peaks across
range rollovers and GPS outages. There are no shortened or context-dependent sensor records.

Internal comparisons restart as the pending ranges roll, so a large earlier landing or
handling peak cannot hide a later flight's smaller peak. The existing two ranges are reused;
only their already-written bounds and internal identities are retained by the writer.
These identities are not included in the IGC, and there is no sample-history buffer.

The trace includes ground and launch-guard readings as well as pending landing peaks.
Readers decide which timestamps belong to their flight window. In particular, do not take
the unqualified minimum/maximum of the entire file as a flight statistic. The existing
`START` and `END` records include recording cause and boot uptime; manual/automatic stop
and the track's altitude/motion can help readers identify landing and subsequent handling.
No altitude threshold or website flight-window algorithm is implemented by this firmware PR.

The device logbook retains its existing 15-second launch guard and 30-60-second holdback.
A website's independently chosen window can therefore produce different extrema from the
device JSON. The IGC does not encode enough of the precise, fresh detector inputs to replay
every firmware decision exactly; exact agreement is no longer a format requirement.

G-load timestamps need not follow file order; sort plotted points by sample time. This is
a peak-preserving trace, not a uniform series of instantaneous measurements; do not
integrate it as such. Data from before the track began is not guaranteed.

## Size and climb statistics

The existing six-extension B record is 54 bytes with LF (55 with CRLF): approximately
194.4 kB/hour (198 kB with CRLF), before headers and occasional diagnostics.
One S per second adds 64.8 kB/hour with LF or 68.4 kB/hour with CRLF, plus occasional
clock anchors. Additional full S records cost 18 bytes each with LF (19 with CRLF).
A K record every 20 seconds adds 3.42 kB/hour with LF or 3.6 kB/hour with CRLF.
Steady totals are approximately **263 kB/hour with LF or 270 kB/hour with CRLF**,
roughly **35-36% growth** over B records alone, before extra extremes. This is smaller than
the previous version-1 sensor trace despite adding standard humidity and temperature records.

An adversarial one-hour test changes both bounds every second and adds two missing peaks
at every range rollover. Including CRLF and metadata allowance, its sensor overhead stays
below the original 198 kB/hour B-record budget, keeping that stress case below double.
Extensive reset/slow-write diagnostics, prolonged missing GPS and very short-file headers
are outside that steady-state comparison. Verbose per-second diagnostic logging and the
30-second pre-launch cache remain removed.

Logbook maximum climb and sink use the same **fixed one-second Kalman-climb moving average**
sampled by IGC, independent of Sensitivity and Climb Avg settings. JSON retains cm/s
precision; the existing three-character IGC VAR field rounds to 0.1 m/s and is limited
to -9.9 through +99.9 m/s. Those representation limits are unchanged.
