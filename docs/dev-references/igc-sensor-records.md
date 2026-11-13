---
title: Compact IGC sensor records
---

# Compact IGC sensor records (version 1)

Leaf records filtered g-load and ambient temperature alongside each GPS B record.
These are manufacturer L records (`LXLV`), covered by the existing G-record checksum.
Other IGC readers may ignore them. The schema declaration is:

```text
LXLVSENSOR V=1 G=MG T=DECI_C HEX=1
```

There is no flight-summary record, pre-launch sample cache, bucket reference, eligibility
flag, or commit/discard message in the IGC. Recording starts at the first usable GPS fix.
Leaf Log will need to parse these records and choose its own flight window for g-load
statistics, using the track and the existing recording-start/end information.

## Fixed-width records

All numeric fields below are uppercase hexadecimal, except the clock's UTC calendar.
Offsets are zero-based within the payload **after `LXLV`**.

| Payload | Fields | Bytes including `LXLV` and LF |
| --- | --- | ---: |
| `Smmmmmmmmggggttt` | Sensor sample: uptime ms (8), milli-g (4), signed deci-C (3) | 21 |
| `AmmmmmmmmDDMMYYHHMMSScc` | GPS clock anchor: uptime at GPS time receipt, UTC date/time and centiseconds | 28 |

The S offsets are: type 0, uptime 1-8, g-load 9-12, temperature 13-15.
G-load is an unsigned integer in **0.001 g** units. Temperature is a **12-bit
two's-complement integer in 0.1 C** units: subtract 4096 when the encoded value is
at least 2048. For example `F85` is -12.3 C.

There is no flags field. G-load `FFFF` and temperature `800` denote missing/unavailable
values; neither is a physical measurement. The IGC VAR field is unchanged, including its
existing `000` representation for unavailable climb.

Temperature is the fresh ambient reading **at the associated B/S write**, even if S's
g-load comes from an earlier extreme. Its timestamp is therefore the preceding B record's
time, not the g-load's sample uptime. If both extremes changed, two independent S records
carry the same current temperature associated with that B fix; readers may deduplicate
these identical temperature points. Historical peaks and readings written during GPS
outages also use the full S format, with temperature `800`.

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
One S per second adds 75.6 kB/hour with LF or 79.2 kB/hour with CRLF, plus occasional
clock anchors. Additional full S records cost 21 bytes each with LF (22 with CRLF).
Steady totals are approximately **270 kB/hour with LF or 277 kB/hour with CRLF**,
roughly **39-40% growth** before extra extremes.

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
