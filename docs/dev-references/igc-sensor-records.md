# Compact IGC sensor records (version 1)

Leaf records g-load and ambient temperature alongside each GPS B record. These are
manufacturer L records (`LXLV`), covered by the existing G-record checksum. Other IGC
readers may ignore them. The schema declaration is:

```text
LXLVSENSOR V=1 G=MG T=DECI_C HEX=1
```

There is no flight-summary record or pre-launch sample cache. Normal IGC recording still
starts at the first usable GPS fix. Sensor samples are written only alongside B records;
peak-only and eligibility records may also be written during GPS outages or finalization. Leaf Log
will need to implement this schema to show the new data.

## Fixed-width records

All numeric fields below are uppercase hexadecimal, except the clock's UTC calendar.
Offsets are zero-based within the payload **after `LXLV`**.

| Payload | Fields | Bytes including `LXLV` and LF |
| --- | --- | ---: |
| `Smmmmmmmmbbbbbbggggtttf` | Sensor sample: uptime ms (8), bucket ID (6), milli-g (4), signed deci-C (3), flags (1) | 28 |
| `Xmmmmmmmmgggg` | Other extreme in the immediately preceding S record's bucket | 18 |
| `Pmmmmmmmmbbbbbbgggg` | Bucket peak at rollover or during a GPS outage | 24 |
| `Cbbbbbb` | Commit a bucket for flight g-load statistics | 12 |
| `Dbbbbbb` | Discard a bucket from flight g-load statistics | 12 |
| `AmmmmmmmmDDMMYYHHMMSScc` | GPS clock anchor: uptime at GPS time receipt, UTC date/time and centiseconds | 28 |

The S offsets are: type 0, uptime 1–8, bucket 9–14, g-load 15–18,
temperature 19–21, flags 22. G-load is an unsigned integer in **0.001 g** units.
Temperature is a **12-bit two's-complement integer in 0.1 °C** units: subtract 4096
when the encoded value is at least 2048. For example `F85` is −12.3 °C.

S flags:

| Bit | Meaning |
| --- | --- |
| `1` | G-load is valid at its original sample time; otherwise g is `FFFF`. |
| `2` | Temperature is fresh and representable; otherwise temperature is `800`. |
| `4` | G-load substitutes a newly reached bucket extreme for the latest filtered value. |
| `8` | The preceding B record's one-second climb value is valid. |

Temperature is the fresh ambient reading **at the associated B/S write**, even if S's
g-load comes from an earlier extreme. Its timestamp is therefore the preceding B record's
time, not the g-load's sample uptime. X and P contain only g-load; they are always valid
filtered extremes, with their own original sample times.

A clock anchor precedes the first B/S pair, repeats at least hourly when GPS records are
available, and is refreshed if the GPS/uptime offset changes by more than two seconds.
Use modular 32-bit millisecond subtraction relative to a nearby anchor. GPS receipt time
is an approximation to the sensor-to-UTC alignment, subject to receiver/serial latency;
the original sensor uptime is retained to millisecond resolution. UTC date handles midnight.

## Peak preservation and eligibility

The sampled signal is the same slew-limited, roughly half-second filtered magnitude used
by the LCD and g-load logbook extrema. Most seconds record its latest fresh value. If a
pending bucket's minimum or maximum has changed since it was represented, S carries that
extreme at its original sensor time. If both changed, S carries the maximum and X carries
the minimum. No X is needed if they encode the same milli-g value. P flushes any missing
peaks from the final interval before a bucket rolls, before that bucket can commit. During
a GPS outage, P preserves new bounds from both pending buckets on the same logging cadence,
without writing S or temperature readings that lack an associated position/time record.

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
track begins, GPS outages do not lose committed g-load extrema: peak-only P records retain
them with original sensor timestamps. Data from before the track began is not guaranteed.

To reconstruct g-load extrema, collect S/X/P values by bucket, resolve C/D, then take the
minimum/maximum of only committed buckets. Values agree with device extrema to 0.001 g
where the bucket was represented in the IGC. This is a peak-preserving trace, not a uniform
series of instantaneous measurements; do not integrate it as such.

## Size and climb statistics

The existing six-extension B record is 54 bytes with LF (55 with CRLF): approximately
194.4 kB/hour (198 kB with CRLF), before headers and occasional diagnostics.
One S per second adds 100.8 kB/hour with LF, plus roughly 1.4 kB/hour of commits and clocks.
Normal totals are approximately **297 kB/hour**, about **53% growth**.

An adversarial simulation changes both bounds every second and adds two boundary peaks
per bucket rollover. Even including CRLF, clock/configuration allowance and final markers,
its sensor overhead remains below the original 198 kB/hour B-record budget. Extensive
reset/slow-write diagnostics, prolonged missing GPS, and very short-file headers are outside
that steady-state size comparison. The former verbose per-second diagnostic logging and
30-second pre-launch cache remain removed.

Logbook maximum climb and sink now use the same **fixed one-second Kalman-climb moving
average** sampled by IGC, independent of Sensitivity and Climb Avg settings. JSON retains
cm/s precision; the existing three-character IGC VAR field rounds to 0.1 m/s and is limited
to −9.9 through +99.9 m/s. Those representation limits are unchanged. The S climb-valid
flag distinguishes an unavailable VAR `000` from genuine zero for schema-aware readers.
