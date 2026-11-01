---
title: Compact IGC sensor records
---

# Compact IGC sensor records (version 1)

Leaf records g-load and ambient temperature alongside each GPS B record. These are
manufacturer L records (`LXLV`), covered by the existing G-record checksum. Other IGC
readers may ignore them. The schema declaration is:

```text
LXLVSENSOR V=1 G=MG T=DECI_C HEX=1
```

There is no flight-summary record or pre-launch sample cache. Normal IGC recording still
starts at the first usable GPS fix. Sensor samples are written only alongside B records;
g-load records and eligibility markers may also be written during GPS outages or finalization. Leaf Log
will need to implement this schema to show the new data.

## Fixed-width records

All numeric fields below are uppercase hexadecimal, except the clock's UTC calendar.
Offsets are zero-based within the payload **after `LXLV`**.

| Payload | Fields | Bytes including `LXLV` and LF |
| --- | --- | ---: |
| `Smmmmmmmmbbbbbbggggttt` | Sensor sample: uptime ms (8), bucket ID (6), milli-g (4), signed deci-C (3) | 27 |
| `Cbbbbbb` | Commit a bucket for flight g-load statistics | 12 |
| `Dbbbbbb` | Discard a bucket from flight g-load statistics | 12 |
| `AmmmmmmmmDDMMYYHHMMSScc` | GPS clock anchor: uptime at GPS time receipt, UTC date/time and centiseconds | 28 |

The S offsets are: type 0, uptime 1–8, bucket 9–14, g-load 15–18,
temperature 19–21. G-load is an unsigned integer in **0.001 g** units.
Temperature is a **12-bit two's-complement integer in 0.1 °C** units: subtract 4096
when the encoded value is at least 2048. For example `F85` is −12.3 °C.

There is no flags field. G-load `FFFF` and temperature `800` denote missing/unavailable
values; neither is a physical measurement. The IGC VAR field is unchanged, including its
existing `000` representation for unavailable climb.

Temperature is the fresh ambient reading **at the associated B/S write**, even if S's
g-load comes from an earlier extreme. Its timestamp is therefore the preceding B record's
time, not the g-load's sample uptime. If both extremes changed, two independent S records
carry the same current temperature associated with that B fix; consumers may deduplicate
these identical temperature points. Rollover and GPS-outage peaks also use the full S
format, with temperature `800`, since their historical temperature is not retained.

A clock anchor precedes the first B/S pair, repeats at least hourly when GPS records are
available, and is refreshed if the GPS/uptime offset changes by more than two seconds.
Use modular 32-bit millisecond subtraction relative to a nearby anchor. GPS receipt time
is an approximation to the sensor-to-UTC alignment, subject to receiver/serial latency;
the original sensor uptime is retained to millisecond resolution. UTC date handles midnight.

## Peak preservation and eligibility

The sampled signal is the same slew-limited, roughly half-second filtered magnitude used
by the LCD and g-load logbook extrema. Most seconds record its latest fresh value. If a
pending bucket's minimum or maximum has changed since it was represented, S carries that
extreme at its original sensor time. If both changed, two full S records carry the maximum
and minimum. One suffices if they encode the same milli-g value. Additional S records flush
missing peaks from the final interval before a bucket rolls, before that bucket can commit.
During a GPS outage, S preserves new bounds from both pending buckets on the same logging
cadence, with temperature marked unavailable. All sensor records use one independent format;
there are no shortened records or bucket IDs inherited from a preceding record.

Extrema comparison restarts for **each bucket**, so an excluded landing peak cannot hide
a later eligible peak. It uses milli-g precision, matching the encoded data. Two pending
bucket IDs and their emitted bounds are the only extra writer state.

Bucket zero means outside the g-load eligibility window (ground or launch guard). Nonzero
IDs increase within the timer session and are not reused on relaunch. Only an explicit C
makes a bucket eligible. D permanently excludes it. On landing or manual stop, both pending
tail buckets are discarded; committed buckets remain accepted. A truncated file's unresolved
buckets are **not** eligible. Neither a late C nor a D changes a reading's original time.
G-load timestamps need not follow file order; sort plotted g-load points by sample time.
Plotting may show all valid readings, but flight extrema must use only committed buckets.

The device's existing 15-second launch guard and 30–60-second holdback are unchanged.
An IMU reset invalidates live readings without erasing pending/committed history. After the
track begins, GPS outages do not lose committed g-load extrema: S records retain
them with original sensor timestamps. Data from before the track began is not guaranteed.

To reconstruct g-load extrema, collect S values by bucket, resolve C/D, then take the
minimum/maximum of only committed buckets. Values agree with device extrema to 0.001 g
where the bucket was represented in the IGC. This is a peak-preserving trace, not a uniform
series of instantaneous measurements; do not integrate it as such.

## Size and climb statistics

The existing six-extension B record is 54 bytes with LF (55 with CRLF): approximately
194.4 kB/hour (198 kB with CRLF), before headers and occasional diagnostics.
One S per second adds 97.2 kB/hour with LF, plus roughly 1.4 kB/hour of commits and clocks.
With CRLF the corresponding overhead is about 103 kB/hour. Additional full S records cost
27 bytes each with LF (28 with CRLF). Normal totals are approximately **293 kB/hour with LF**
or **301 kB/hour with CRLF**, before additional extremes: roughly **51–52% growth**.

An adversarial simulation changes both bounds every second and adds two boundary peaks
per bucket rollover. Even including CRLF, clock/configuration allowance and final markers,
the uniform full records can slightly exceed twice the original total in this stress case.
The test budgets 210,822 bytes/hour of sensor overhead, for approximately 409 kB/hour total
with CRLF (2.06 times the regular 198 kB/hour).
This replaces the earlier shortened-record format's strict sub-double stress-case bound;
ordinary recording with occasional second extremes remains comfortably below double. Extensive
reset/slow-write diagnostics, prolonged missing GPS, and very short-file headers are outside
that steady-state size comparison. The former verbose per-second diagnostic logging and
30-second pre-launch cache remain removed.

Logbook maximum climb and sink now use the same **fixed one-second Kalman-climb moving
average** sampled by IGC, independent of Sensitivity and Climb Avg settings. JSON retains
cm/s precision; the existing three-character IGC VAR field rounds to 0.1 m/s and is limited
to −9.9 through +99.9 m/s. Those representation limits are unchanged.

## Why the website cannot exactly replay eligibility from B records alone

The IGC preserves the result of Leaf's decision through C/D. Without it, a website can
estimate its own flight window, but cannot reproduce every firmware decision:

- The landing veto uses Sensitivity-filtered climb; VAR is a separate one-second average.
- B records round speed to whole km/h, coordinates to thousandths of a minute, altitude to
  whole meters, and climb to tenths of m/s. Launch decisions use more precise inputs.
- Sensor freshness, missing observations and sub-second bucket/launch-guard boundaries are
  not fully represented by B records. Timer auto-stop also uses instantaneous acceleration,
  whereas the logged g-load is filtered.

Duplicating the detector on the website would therefore create a second interpretation,
which can disagree with device extrema near launch or landing. An alternative future schema
could record the exact accepted time intervals instead of bucket IDs and C/D; readers would
filter original sample timestamps by those intervals without replaying the detector.
