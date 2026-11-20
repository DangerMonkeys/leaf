# November 14 vario timing and stack comparison

Three Leaf units recorded the same flight. Match their GPS UTC seconds
because the 3.2.6 recording started 23 seconds earlier. The times in the
last column below use the 3.2.6 first fix at 20:46:29 UTC. The track starts
showing sustained movement around 20:57:44 UTC, about 11:15 later.

## Vario timing

| Leaf | IGC writes taking at least 100 ms | Sensor and estimator events |
| --- | --- | --- |
| 3.2.5, Bluetooth and FANET | 7 writes, worst 139 ms | No logged FIFO or estimator resets or pressure or motion gaps over 500 ms. |
| 3.2.6, Bluetooth with System Events recording to SD | 1 IGC write at 470 ms | 3 ICM FIFO backlog resets, 2 motion gaps over 500 ms, and 1 pressure gap over 500 ms. |
| 3.2.7, FANET | 14 writes, worst 147 ms | No logged FIFO or estimator resets or pressure or motion gaps over 500 ms. |

The IGC reports aggregate event counts. The timestamp and duration identify
the worst event of that kind in the reporting interval. System Events
writes themselves are outside the IGC write measurement. See the
[IGC diagnostic event definitions](../../../../docs/dev-references/flights_and_logging.md#event-records).

| GPS UTC | Observation | Time from 3.2.6 first fix |
| --- | --- | --- |
| 20:47:57.5 | The 3.2.6 logged a 470 ms IGC write followed by an ICM FIFO reset and a 520 ms motion gap. | 1:28 |
| 21:08:00 and 21:08:15 | Only the 3.2.6 reported FIFO resets. The second coincided with 750 ms without a pressure sample and 780 ms without a motion sample. Both gaps began within 20 ms of a synchronous System Events checkpoint. At 21:08:15 its logged climb was +1.6 m/s, versus +2.3 m/s on both peers. | 21:31 and 21:46 |
| 21:19:56–21:20:00 | The 3.2.6 repeated and skipped some B-record timestamps. Its peers recorded normally through this interval. All three showed a broadly similar climb and there were no logged sensor-gap or reset events. | 33:27–33:31 |
| 21:28:08 | The 3.2.6 jumped from −3.2 to +0.5 m/s in one second. The 3.2.5 and 3.2.7 still showed −1.6 and −2.6 m/s, then approached zero or briefly went positive within three seconds. The 10-second altitude trends still showed descent. The 3.2.6 logged no sensor-gap or reset event here. | 41:39 |

Across 2,851 common UTC seconds from the first sustained movement onward,
the median absolute difference in the one-second IGC vario was 0.2 m/s
between the 3.2.5 and 3.2.7 and 0.4 m/s between the 3.2.6 and either peer.
Duplicate valid fixes within the same UTC second were averaged. This
compares telemetry agreement on one flight. Many later swings were shared
across the three units, including the oscillations after 21:28:08 UTC.
The IGC cadence is too sparse to reconstruct the exact display or speaker
output or distinguish a short-lived change in actual climb from a shared
response to motion.

The comparison isolates the logged FIFO resets and the longest sampling
stall to the 3.2.6. Both peers came through the same seconds without
reported gaps over 500 ms while FANET was active. This supports investigating
synchronous SD writes on the 3.2.6. The firmware, SD cards, and radio
workloads differed among the units, so the comparison does not isolate a
unit, card, setting, or build as the cause. The later outlier needs separate
sensor-age, climb-estimate, and sound timing evidence.

## Stack headroom

| Task | Allocation | Lowest unused across the three units | Follow-up |
| --- | ---: | ---: | --- |
| Main loop | 12,288 B | 7,676 B | A separate 10 KiB trial would save 2 KiB and leave 5,628 B under the heaviest observed stack use. |
| Bluetooth worker | 5,120 B | 2,124 B | Keep the allocation for further testing under connected-client traffic. |
| FANET TX | 4,096 B | 2,076 B | Keep the allocation while testing sustained peer traffic. |
| FANET RX | 4,096 B | 1,176 B | Keep the allocation. This worker had the least remaining margin. |

Compare the 3.2.6 with System Events disabled while keeping normal IGC
and Stacks enabled. Hold stack sizes and vario tuning steady for the
diagnostic comparison. [Vario timing robustness backlog](../../../../docs/dev-references/backlog/vario-timing-robustness.md)
