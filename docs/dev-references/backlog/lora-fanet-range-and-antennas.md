---
title: LoRa and FANET Range and Antenna Evaluation
description: Firmware adjustments, antenna candidates, and controlled bench and flight tests for improving Leaf's FANET range.
---

# LoRa and FANET Range and Antenna Evaluation

Status: open; investigation recorded 2026-10-24. Capture tooling is implemented; the RF
configuration changes and physical measurements below remain outstanding. Work branch:
`codex/lora-fanet-range`.

Improve useful direct FANET range while preserving interoperability with other FANET instruments.
Evaluate firmware and antenna placement independently so that a change in one does not obscure
the effect of the other. Published antenna gain and advertised product range are supporting
information, not acceptance evidence for the completed Leaf assembly.

## Implemented capture tooling

- [x] Add a persistent **Log FANET** checkbox below **Fanet ReTX** in the Developer menu.
  While the flight timer runs, write an independent neighbor CSV under `/fanetlogs/`, with a
  new file for each flight or enabled segment. Capture elapsed time, neighbor ID, RSSI/SNR,
  positions, horizontal distance when Leaf has a usable GPS fix, and queue-drop counts.
  Record firmware, receiver ID, region and forwarding setting at session start. See
  [Neighbor CSV logging](../fanet/index.md#neighbor-csv-logging).
- [x] Remove FANET comment generation from bus logging. Queue numeric observations on RX and
  perform bounded CSV batches on the main flight-logging task, independently of full bus logs.
- [x] Combine runtime STOP and SD export, retaining **Runtime: SAVE** for failed-save retries
  and automatically frozen captures. Record the FANET logging setting in runtime reports.
- [x] Gate FANET device menus, screens, icons and radio actions on detected hardware. Remove
  the web app FANET button and its unsafe diagnostics endpoint; the normal web app pauses FANET.

Formatter, simulator regressions, developer-card checks, runtime-report tests and the v3.2.6
firmware build passed. No Leaf was detected for flashing; radio operation and range remain
unverified on hardware. The CSV does not yet classify direct versus forwarded reception or
measure expected packet counts and CRC/header failures; those diagnostics remain below.

## Existing hardware and flight observations

- Leaf's external case dimensions are 90 x 42 x 20 mm. Its stack is LCD, PCB, then battery.
  The battery is shorter than the PCB, and the GPS patch occupies the rounded GPS end.
- Prefer an antenna shorter than 50 mm. A longer, narrow, flexible whip is acceptable if it bends
  out of the way of paraglider lines and straps. Connector and mounting-base bulk are especially
  important: prefer a direct micro-coax lead to the radio's I-PEX connector over an SMA bulkhead.
  Verify the exact mating connector family before ordering; I-PEX alone is not a connector size.
- Flight-deck mounting raises the GPS end approximately 20-30 degrees above horizontal; riser
  mounting raises it closer to 90 degrees. Evaluate both attitudes and different pilot headings.
- The narrow TE L000549-02 was mounted along the internal sidewall. The square TE L000528-02
  was mounted inside the rear case under a section of PCB without battery behind it.
- Both TE installations produced disappointing range, below one mile, compared with external
  whips. Both varios were airborne a few hundred feet above the ocean along a ridgeline, using
  Leaf's US setting. Exact clearances, packet-delivery rates, antenna combinations at both ends,
  and comparable whip distances have not yet been recorded.
- Absence of battery behind an antenna does not establish adequate clearance from PCB copper,
  LCD metalwork, wiring, or the pilot. The airborne result is not a ground-level range baseline.

## Intended firmware adjustments, in priority order

### 1. Configure and verify the SX1262 transmitter current limit

- [ ] After successful radio initialization, explicitly set the SX1262 current limit to 140 mA
  with `setCurrentLimit(140.0f)`, checking the return status. Read back and log the configured limit.
- [ ] Retain a record of requested transmit power separately from measured RF output power.
- [ ] Measure conducted output at 920.8 MHz before and after the change, using suitable RF test
  equipment and attenuation. Check the module supply during TX bursts, including lower battery
  charge, for voltage drop or resets. Record TX current and any battery-life impact.

The reviewed Leaf configuration requests 22 dBm, but installed RadioLib 7.1.2 initializes the
current limit to 60 mA and restores that limit when setting output power. Leaf does not currently
override it. This is a strong candidate for reduced output, not a measurement of Leaf's actual
power. Seeed specifies approximately 125 mA module current at 22 dBm; Semtech identifies 140 mA
as the SX1262 OCP configuration after PA setup. A separate RadioLib user's report measured
16-17 dBm at a 60 mA limit versus 21.5 dBm at 140 mA; do not assume the same gain for Leaf.

### 2. Apply the FANET library's adaptive coding rate

- [ ] Restore application of the requested coding rate in `FanetRadio::fanet_sendFrame()`:
  currently the protocol requests 4/8 below eight neighbors and 4/5 at eight or more neighbors,
  but Leaf's setter is commented out and the radio remains at the initial 4/5 setting.
- [ ] Handle radio standby/configuration/TX transitions under the existing synchronization.
  Compare return status with `RADIOLIB_ERR_NONE`; the old commented `if (!setCodingRate(...))`
  check treats zero/success as failure and must not simply be uncommented.
- [ ] Verify interoperability with an independent FANET instrument and transitions across the
  neighbor threshold. Keep explicit headers, payload CRC, and the standard coding-rate format.
- [ ] Make airtime accounting reflect actual regional bandwidth and applied coding rate.
  The reviewed protocol dependency calculates airtime using a fixed 250 kHz bandwidth; US
  operation uses 500 kHz. Account for the longer packets when stronger coding is selected.
- [ ] Measure packet delivery near the reception limit and under interference, alongside airtime.
  Stronger payload coding is not a substitute for receiving the preamble/header successfully.

### 3. Review reception reliability and add useful RF diagnostics

- [ ] Avoid restarting reception on every transmit-task pass when no transmission or recovery
  requires it. Verify whether current restarts interrupt incoming packets before changing behavior.
- [ ] Complete the review of transmitting while an incoming packet is in progress, with bounded
  channel-access/backoff behavior and prompt return to RX after TX or a recoverable error.
- [ ] Keep packet reads and RSSI/SNR acquisition together under the shared SPI lock so display
  activity or another task cannot interfere with measurement or attribution to a packet.
- [ ] Capture timestamp, source, direct/forwarded status, RSSI, SNR, successful packets, CRC/header
  failures where observable, TX failures, and RX restart/recovery failures. Distinguish successful
  local transmission from delivery to a remote receiver. Keep capture overhead bounded.
- [ ] Compare normal operation with diagnostic WiFi/BLE disabled and with display/SD activity
  varied to look for receiver desensitization or contention. Good TX with poor RX warrants a
  receiver-noise investigation rather than assuming antenna inefficiency alone.

Reception handling findings are review candidates, not established causes of the sub-mile result.

### Settings to preserve during compatible FANET evaluation

- US: 920.8 MHz, 500 kHz bandwidth, SF7; Europe: 868.2 MHz, 250 kHz bandwidth, SF7.
- Correct FANET sync word, explicit packet headers, and payload CRC.
- Boosted receiver gain is already enabled with retention. Installed RadioLib already applies
  its SX1262 PA-clamping and 500 kHz modulation workaround; verify these remain active.
- Increasing spreading factor or reducing bandwidth would require matching changes at the other
  endpoint and would prevent normal FANET interoperability. Any such experiment must be explicitly
  isolated from the compatible FANET configuration and must not become its default.
- Confirm region/module requirements when implementing power changes; the current 22 dBm request
  is an observation of the code, not a statement that every region permits that configuration.

## Antenna candidates

The user intends to order Molex samples. Start with those and the existing TE antennas, using a
known-good external whip as the reference. Other candidates remain optional comparisons.

| Antenna | Role and physical considerations | Published reference data / caveat |
| --- | --- | --- |
| Molex 2111400100 | First new internal candidate; 38 x 10 x 0.1 mm flex, 100 mm coax lead, U.FL-style connector; dual 868/915 MHz bands. | Average total efficiency >60% over 902-928 MHz and >55% over 868-870 MHz; peak gain 1.0/0.3 dBi respectively, in the manufacturer's reference fixture. |
| TE L000549-02 | Existing narrow sidewall baseline; 35 x 9.2 mm outline, 100 mm lead with MHF connector. | 902-928 MHz; published average efficiency 46%, peak gain 3.3 dBi. Molex's reference efficiency advantage is only about 1.2 dB using 60%; a large improvement is not assured. |
| TE L000528-02 | Existing square rear-case baseline; 25 x 22.4 mm outline, 100 mm lead with MHF connector. | 902-928 MHz; published average efficiency 16%, peak gain -1.7 dBi. Molex's reference efficiency advantage is about 5.7 dB using 60%, before installation and pattern differences. |
| Modtronix ANT-UFL-FLEX-915; corresponding 868 MHz variant | Thin flexible wire alternative without SMA bulk; 915 MHz version has approximately 82 mm radiator and only 10 mm shielded lead. | Verify the appropriate regional version and feed length. Do not use the active radiator as an internal feedline beside the PCB; source a longer coax lead if required. |
| TrueRC BARDpole 915; BARDpole 868 | Longer flexible PCB dipole with direct micro-coax connection; consider an external GPS-end shoulder mount. | Match the band and connector to the unit. Approximately 20 mm of inactive base was identified for mounting; verify the selected version's drawing and overall size before case design. |
| Taoglas FXP890.07.0100C | Optional internal flex comparison; approximately 62.2 x 20 x 1.6 mm with 100 mm U.FL lead. | Lower priority for the existing enclosure: its 20 mm width already equals Leaf's entire external thickness. Verify clearance and usable internal dimensions before purchase. |

Efficiency figures come from different reference fixtures and are band averages, not installed
gain at precisely 920.8 MHz. Peak gain describes the strongest direction, not coverage across
pilot headings. Confirm current drawings, lead lengths, mating connectors, and stock before ordering.

## Placement and mechanical experiments

- [ ] Compare the two long sidewalls with the flex plane perpendicular to the PCB, initially in
  the button-side half where practical. Position by actual available conductive clearance rather
  than choosing a wall solely from the photos. Avoid placing the radiator over the battery or LCD.
- [ ] For TE L000549, respect its reference keep-out drawing: approximately 10 mm around the
  outline, 10 mm on the antenna side of the mounting plastic, and 5 mm on the opposite side.
- [ ] For TE L000528, its reference drawing calls for approximately 10 mm around the outline
  and on both sides of the mounting plastic. Reassess the under-PCB position against that volume.
- [ ] For Molex, use its recommended minimum 5 mm from a nearby ground plane as a starting
  constraint, then test 10 and 20 mm. Its guide also calls for the returning coax to stay at least
  5 mm from the flex edge. A plastic wall alone does not provide conductive clearance.
- [ ] Compare inside mounting, the outside of the same wall, and controlled 5/10/20 mm separations
  on plastic supports. Add a 50 mm separation as a diagnostic reference when the lead permits.
  Record the nearest PCB/battery/LCD distances, antenna plane, lead routing, and mounting material.
- [ ] If performance requires extra space, prototype a shallow smooth plastic side blister or an
  extension beyond the PCB outline. Evaluate whether it meets the handling and snagging goals.
- [ ] For a flexible external antenna, try the GPS-end shoulder offset from the GNSS patch, with
  its active section beyond the case. Compare alignment with the case's long axis and an
  approximately 30-degree tilt toward the screen for the deck/riser attitude compromise.
- [ ] Check GNSS performance during LoRa TX, mechanical strain relief, case sealing, connector
  retention, and interaction with straps/lines. Keep force off the PCB's micro-coax receptacle.

Clearance recommendations are antenna-specific reference conditions, not guarantees of matching
or efficiency. A 50 mm diagnostic separation is not a required final case dimension.

## Controlled test sequence and evidence

1. Record a baseline with the current firmware and existing antennas before changing firmware.
   Identify both radio endpoints, hardware revisions, firmware commits, regional settings,
   antenna models, lead routing, mounting positions, battery state, and weather/site geometry.
2. Change firmware in the order above, one adjustment at a time, using the same antenna and
   geometry. Verify current-limit readback and actual output, then coding behavior and reception.
   Freeze a verified firmware candidate for subsequent antenna comparisons.
3. For antenna comparisons, keep the remote endpoint on a known-good external whip. Change only
   the test Leaf's antenna or placement. Test both directions; keep TX power, packet contents,
   cadence, height, range, polarization, and orientation consistent within each comparison.
4. At fixed clear-path distances, send at least 100 identified packets per condition and repeat
   runs. Record received/expected packet counts and delivery percentage, RSSI/SNR distributions,
   and heading-specific outages. Count direct reception separately from forwarded reception.
5. Sweep antenna clearance before concluding that a model is unsuitable. If available, measure
   matching across the intended band and at 920.8 MHz with the actual case closed, battery and
   LCD installed, and representative mounting/body proximity. Good return loss alone does not
   establish good radiation efficiency; correlate it with over-the-air measurements.
6. Compare antenna attitude and azimuth systematically: deck pitch 20-30 degrees, riser pitch
   near 90 degrees, facing toward/away/across the remote station, and representative body shielding.
   Perform elevated ground tests as repeatable comparisons, then confirm the results in flight.
7. Repeat the coastal air-to-air installation test with recorded separation, altitude, line of
   sight, headings, and delivery rates, and evaluate two completed internal-antenna Leafs together.
   Capture the external-whip reference under comparable conditions rather than comparing unrelated
   maximum-distance anecdotes.

Use repeatable delivery at 5 km / 3 miles in unobstructed air-to-air conditions as an initial
development target, followed by a 10 km / 6 mile evaluation. These are provisional engineering
targets, not measured Leaf capability or a guaranteed range. Agree and record the required delivery
percentage and update freshness for the intended product behavior before final qualification.

For context, Seeed specifies 2-5 km for its internal-antenna T1000 tracker and reports a separate
9.5 km gateway test with 100/99/83% reception for held-up/front-facing/back-facing conditions.
SkyBean reports Strato FANET tests around 15 km and over 40 km on another day. These demonstrate
possible performance in other installations; differences in radio settings, endpoint antennas,
device size, and packet-delivery criteria prevent treating them as Leaf acceptance results.

## Completion evidence

- [ ] Firmware changes reviewed and verified with the repository's normal hardware workflow.
  RF output, supply behavior, coding transitions, radio recovery, and independent FANET
  interoperability require physical evidence; a simulator/build pass does not establish range.
- [ ] Before/after firmware results and the controlled antenna/placement matrix saved with the
  exact commits, device revisions, configurations, and direct-link packet-delivery records.
- [ ] Final antenna, clearance volume, cable routing, strain relief, and deck/riser compromises
  documented with assembly photos or drawings and repeatable performance results.
- [ ] Remaining limitations, regional differences, and failed candidates retained in the results.
  If a firmware release is subsequently published, also follow the release/runtime requirements
  in `AGENTS.md`; this backlog entry does not satisfy those release requirements.

## Implementation and source references

- [Leaf FANET radio implementation](../../../src/vario/comms/fanet_radio.cpp)
  and [dependency versions](../../../platformio.ini).
- [FANET PHY and packet specification](https://github.com/3s1d/fanet-stm32/blob/master/Src/fanet/radio/protocol.txt).
- [Semtech SX1262 documentation](https://www.semtech.com/products/wireless-rf/lora-connect/sx1262)
  and [Seeed Wio-SX1262 module datasheet](https://files.seeedstudio.com/products/SenseCAP/Wio_SX1262/Wio-SX1262-N_Module_Datasheet.pdf).
- [RadioLib current-limit discussion and measured example](https://github.com/jgromes/RadioLib/discussions/829)
  and [maintainer's current-limit recommendation](https://github.com/jgromes/RadioLib/discussions/489).
- [Molex 2111400100 application guide, including placement tests](https://docs.rs-online.com/2867/A700000014531430.pdf).
- [TE L000549 datasheet](https://www.te.com/commerce/DocumentDelivery/DDEController?Action=srchrtrv&DocFormat=pdf&DocLang=English&DocNm=L000549_DS&DocType=Data+Sheet&PartCntxt=L000549-02)
  and [TE L000528 datasheet](https://www.mouser.com/pdfDocs/ENG_DS_L000528_A1.pdf).
- [Modtronix ANT-UFL-FLEX-915](https://modtronix.com/product/ant-ufl-flex-915/),
  [TrueRC BARDpole 915](https://truerc.com/8540-truerc-bardpole-915-mhz),
  [TrueRC BARDpole 868](https://truerc.com/10543-truerc-bardpole-868-mhz),
  and [Taoglas FXP890 datasheet](https://www.taoglas.com/datasheets/FXP890.07.0100C.pdf).
- [Seeed T1000 guide](https://files.seeedstudio.com/products/SenseCAP/SenseCAP_Tracker/SenseCAP_Tracker_T1000-AB_User_Guide.pdf),
  [Seeed T1000 field test](https://www.seeedstudio.com/blog/2023/10/12/open-manufacture-how-we-tested-the-long-range-communication-of-sensecap-t1000/),
  and [SkyBean Strato FAQ](https://skybean.eu/content/strato-faq).
