---
title: USB, Solar Input, and GPS Backup Power Backlog
description: Reduce USB divider losses while preserving battery-free GPS startup, backup retention, and operation through solar input voltage excursions.
---

# USB, Solar Input, and GPS Backup Power Backlog

## Status and scope

Recorded **2026-11-21** after the charge-mode sleep bench work on hardware v3.2.8.
These are hardware investigation candidates, not an approved schematic change or a
completed electrical qualification. No circuit or firmware change is included in this item.
Recheck component populations and net names for each earlier board revision before applying
the conclusions to it.

Goal: remove most of the continuous USB divider loss while preserving GPS operation without
a LiPo battery, battery-backed satellite-data retention when Leaf is off, and appropriate
behavior when a nominally 5V solar panel temporarily produces 7–9V at the USB input.

## Existing circuit and constraints

- R44/R43 are a 200Ω/200Ω divider from USB_VBUS to ground. Their nominal unloaded draw
  is 12.5mA at 5V (62.5mW), increasing with input voltage. The divider is outside the
  charger's input-current budget, although a USB input meter includes its consumption.
- The midpoint, USB_EN, both enables the main regulator through D3 and supplies the GPS
  backup regulator through D16. D14 provides a separate direct VBATT feed to that regulator.
- U6, XC6219B332MR, generates GPS_V_BCKP. Its existing 6V input rating rules out simply
  reconnecting D16 to an unregulated USB input that may reach 9V.
- The maintainer previously tried higher divider resistances: GPS backup startup inrush
  prevented reliable operation. Increasing R43/R44 alone is not a qualified solution.
- R20 connects GPS_V_BCKP to the backup regulator's enable input as a feedback pull-up.
  It keeps backup enabled after the processor turns off. It is **not a 10kΩ resistor to
  ground**; an earlier estimate of a continuous 0.33mA loss through it was incorrect.
- SYSPOWER is the BQ24073 OUT net, upstream of the main 3.3V regulator. It remains a
  battery-fed rail with USB absent. Leaf off disables the main regulator rather than
  disconnecting SYSPOWER. The main regulator and speaker amplifier still have shutdown
  leakage; the existing GPS backup feed bypasses SYSPOWER through D14.

The firmware deliberately leaves backup enabled during normal shutdown. See
[`LC86G::setBackupPower()`](https://github.com/DangerMonkeys/leaf/blob/c4138b62/src/vario/hardware/lc86g.cpp)
and the [v3.2.8 schematic, sheets 1–2](https://github.com/DangerMonkeys/leaf/blob/c4138b62/vario/hardware/Schematic_Leaf-Vario-v3.2.8_2026-10-24.pdf).

## Candidate A: feed D16 from SYSPOWER

Move D16's input from USB_EN to SYSPOWER, retaining D14's direct VBATT feed and the
existing backup enable latch. With the startup load removed from USB_EN, investigate
higher R43/R44 values for the enable function, subject to the separate EN-pin issue below.

Expected charger behavior, based on its datasheet:

| Supply condition | SYSPOWER and GPS backup implications |
| --- | --- |
| Valid 5V USB, no battery | OUT starts without processor intervention and regulates near 4.4V. This should provide headroom for D16 and U6. Validate the actual startup waveform. |
| 9V USB, battery present | Input overvoltage disconnects USB and stops charging; OUT uses the battery. D14 retains the existing backup path. |
| 9V USB, no battery | No sustained charger-supplied OUT power. Backup retention through the excursion is not assured; measure discharge and recovery. |
| USB absent, battery present | OUT remains battery-fed. A small load does not select active USB charging mode. |

The BQ24073 overvoltage threshold is 6.6V typical (6.4–6.8V); it disconnects excessive
input voltage rather than converting 9V to 4.4V. Its published 4.3µA typical battery
power-down current is specified with no OUT load at 85°C. Measure overhead with a small
load rather than assuming that exact figure still applies. With valid USB, the chip already
operates with OUT unloaded. See [BQ24073 electrical characteristics and sections 9.3.1–9.3.4](https://www.ti.com/lit/ds/symlink/bq24073.pdf).

Retaining D14 means battery-only backup current may use either diode path, depending on
their forward voltages. Do not assume the entire backup load necessarily traverses the
charger. U6 and the GPS backup domain already consume battery power in the existing design.

### Startup sequencing

The main 3.3V rail does not depend on GPS backup, so the proposed source change introduces
no apparent supply dependency loop. The I/O expander defaults backup enable high; GPS
initialization enables it again and holds GPS reset low for 100ms before release.

However, Quectel requires V_BCKP to be powered simultaneously with or before GPS VCC.
Firmware reset handling alone does not demonstrate compliant rail ordering. Scope both
rails during a completely discharged, battery-free cold start and during recovery from
overvoltage. The specified V_BCKP operating range is 1.65–3.6V. See
[LC86G hardware design, sections 3.4 and 6.2](https://quectel.com/content/uploads/2024/02/Quectel_LC86G_Series_Hardware_Design_V1.1.pdf).

### Existing main-regulator enable issue

USB_EN also reaches LM3281 EN through D3. At 9V input the nominal divider midpoint is
4.5V, while SYSPOWER (LM3281 VIN) can be low or absent during input overvoltage.
The EN absolute maximum is the smaller of VIN + 0.2V and 6V. Check actual EN/VIN waveforms
and possible injection/back-powering; do not assume a half-voltage divider alone protects
this pin in every supply state. This concern exists independently of the D16 reroute.

If necessary, redesign USB-presence control so the EN high level is referenced to SYSPOWER,
using appropriate buffering or level shifting. Verify default-off behavior, button/latch
operation, leakage, and startup timing. See [LM3281 absolute maximum ratings](https://www.ti.com/lit/ds/symlink/lm3281.pdf).

## Alternative candidates

- **High-voltage, low-quiescent-current regulator:** power the USB backup branch from a
  regulator rated above the expected panel excursions, with sufficient startup current.
  Preserve D14, the backup enable latch, and valid low-battery operation. TPS709 (30V input,
  150mA output, 1µA typical IQ) is an example to evaluate, not a selected or pin-compatible
  replacement. Compare minimum input, dropout, reverse current, output discharge, and
  sequencing against U6 before choosing a part. See [TI TPS709](https://www.ti.com/product/TPS709).
- **Switched-capacitor or buck conversion:** consider only if linear losses under the
  measured backup load justify extra parts. LTC3255 is an example of a wide-input,
  50mA step-down charge pump; check startup and battery-path integration. No exact
  half-input output ratio is required if the backup supply remains within specification.
  See [Analog Devices LTC3255](https://www.analog.com/en/products/ltc3255.html).
- **Existing divider with startup energy storage:** investigate only with measured surge
  charge, latch behavior, and rail timing. A larger capacitor on a high-resistance divider
  can lengthen startup or prevent compliant sequencing; it is not an established fix.

## Bench qualification and acceptance

Record the schematic revision, modifications, component values, battery voltage, input
source, waveforms, and currents. Compare an unmodified board with each candidate separately.

- [ ] Measure baseline USB input, battery, SYSPOWER, and backup-branch currents in on,
  charging, and fully off states. Separate charger overhead from the load itself.
- [ ] Repeat battery-free cold starts with backup capacitors fully discharged; measure
  SYSPOWER, main 3.3V, GPS_V_BCKP, backup enable, main-regulator EN, and GPS reset.
- [ ] Verify GPS communication, fix acquisition, and backup retention across normal off/on
  and USB/battery insertion/removal, including a low battery and backup-enable reset.
- [ ] Exercise input ramps and excursions through 5–9V with and without a battery.
  Document charging interruption, rail sequencing, brownouts, GPS recovery, and any
  enable-pin injection. Use controlled sources before solar-panel trials.
- [ ] Test actual solar panels across illumination levels. Removing the divider's load
  could allow open-circuit voltage to remain above the charger's overvoltage threshold,
  preventing startup or recovery; a stiff bench supply alone does not qualify this behavior.
- [ ] Verify battery-only off-state drain and backup retention duration, including any
  leakage through unpowered I/O and both diode paths. Confirm no unexpected charger-mode
  or main-regulator activity appears.
- [ ] Confirm materially lower USB idle loss, required startup reliability, and datasheet
  voltage/sequence limits across the intended supply and temperature range.

Select a circuit only after recording these results. If battery-free GPS backup must persist
while USB exceeds the charger's overvoltage threshold, SYSPOWER alone cannot provide that
function; retain or replace the independent USB-derived backup source. Any decision to
accept loss of backup during that condition must be explicit.
