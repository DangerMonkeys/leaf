# Charge mode sleep continuation

Continue this work on `codex/charge-mode-sleep-bench`. Its normal checkout contains
the cleaned single-conversion firmware and the default-ON change. The earlier bench
implementation is preserved in this branch's ancestry so it can be used in a separate
worktree when diagnostic captures are needed. This work remains separate from main
and has not been submitted as a PR or published as a release.

## Resume on another machine

```sh
git clone --branch codex/charge-mode-sleep-bench https://github.com/DangerMonkeys/leaf.git leaf-charge-sleep
cd leaf-charge-sleep
python test-data/charge-sleep/2026-11-20/verify_archive.py
```

Install PlatformIO and follow the repository's `AGENTS.md` for device verification.
Build the environment matching the unit. These targets were verified locally at
`8ce157c1a028cae7b0775b45ce3c02fc78d9b60e`; their original image hashes are in
[`verified-builds.json`](verified-builds.json).

```sh
pio run -e leaf_3_2_5_release
pio run -e leaf_3_2_6_release
pio run -e leaf_3_2_8_release
```

Run builds sequentially in each worktree: versioning temporarily changes a shared
header. Matching application images are under `.pio/build/<environment>/`.
Preserve the installed working bootloader and partition table when flashing the
bench PCB, which required a DIO bootloader. Both app slots use the same matching
image at `0x10000` and `0x340000`.

After a PlatformIO build has populated `.pio/libdeps`, simulator checks can run in
Docker. The following command uses the repository root as the working directory:

```sh
docker run --rm -v "$(pwd):/leaf" -w /leaf gcc:13 sh -c 'make -C sim -j8 test-developer-logging /leaf/sim/build/charging_dialogs_test && ./sim/build/charging_dialogs_test sim/build/charge-clean-dialogs && make -C sim -j8 test-shutdown-menu'
```

## Current firmware behavior

- Charge updates use a 500ms monotonic deadline. Hardware GPTimer counters stop
  during light sleep and must not be used as the charge-mode wall clock.
- On ISET-equipped boards, the ULP samples ADC1 once halfway through sleep and
  immediately powers the ADC down. The display averages eight recent calibrated
  readings and weights sleep and awake current by their durations. Failed or early
  acquisitions do not contribute. The ADC module accepts a pin and planned duration
  independently of operating state; ON-state sleep itself is not implemented here.
- IDF 5.3.2 forces SAR power off at light-sleep entry. The ULP restores clock and
  power for its conversion. Cleanup restores CPU ADC ownership and releases the
  reference-counted RTC peripheral power request. Keep these steps when iterating.
- Eco defaults ON for fresh/reset settings and missing `ECO_MODE` preferences.
  A saved OFF choice stays OFF. Developer mode exposes the toggle; disabling
  Developer mode does not reset Eco or prevent an enabled Eco setting from working.
- Hardware 3.2.6 and later defines ISET. Earlier boards still use charge light sleep,
  but skip ULP current sampling and retain the existing voltage-based battery
  estimate with an estimated charging correction. GPIO7 is not used as ISET on
  3.2.5, where it serves the radio. Older-board power savings need physical testing.
- USB host activity, active buttons, diagnostic-network work and Leaf Log work can
  hold the device awake. Use a simple charger for sleep measurements.

The normal firmware has no Chg test menu, alternating ADC modes, startup ADC test
or automatic ADC CSV writer. The implementation is in
[`sleep_adc.cpp`](../../../src/vario/hardware/sleep_adc.cpp),
[`power.cpp`](../../../src/vario/power.cpp) and
[`taskman.cpp`](../../../src/vario/taskman.cpp).

## Bench code and preserved history

| Commit | Purpose |
|---|---|
| `aa82cf9052a4ad3f43ec173b64823e617ee18083` | Former backup branch: initial sleep scheduling fix and charge capture controls |
| `46ff66ce5388d4c1b8415b44999d6c614ce24a04` | Complete validated bench snapshot before cleanup, including the formerly untracked ULP source files |
| `6768d4cc064a582cdea46865246af3f75d720562` | Cleaned single-conversion firmware, rebased onto main at `de834a12` |
| `8ce157c1a028cae7b0775b45ce3c02fc78d9b60e` | Eco defaults ON while preserving saved preferences |

The bench snapshot includes its original base and should be checked out separately:

```sh
git worktree add --detach ../leaf-charge-sleep-capture 46ff66ce5388d4c1b8415b44999d6c614ce24a04
```

That snapshot defaults Eco OFF. For another capture, insert the SD card before
booting, enable Developer mode and Eco, select **Chg test START**, then turn the
unit off while powered from a simple charger. After 60 seconds, hold centre to
turn it on and select **Chg test SAVE**. Turn it off on computer USB to retrieve
`/diagnostics/charge_sleep_N.csv`. Using the archived worktree does not change the
clean firmware on the continuation branch.

## Measurements and scope traces

The collection was copied on November 20 and archived on November 21, 2026.
[`source-manifest.json`](source-manifest.json) records byte counts and SHA-256
hashes. CSVs retain their original collection subdirectories and filenames;
duplicates preserve the context of each download. Build directories, dependencies
and generated firmware binaries are not required to retrieve the source history.

The final prototype run is
[`charge_sleep_9.csv`](captures/ulp-one-only-capture/charge_sleep_9.csv). On the bare
3.2.8 PCB with a battery connected and a simple charger, all 113 sleep attempts
succeeded with one sample and no sampling errors. After the initial USB grace
period, normal cycling spent 98.48% asleep. Mean ULP ISET was 1.2998V; the user
reported 1.3093V on the external meter. After the first eight seconds the LCD
averaged 471.1mA, with a logged range of 468-476mA. These results describe the
preserved bench firmware, not a full release assessment of later commits.

Useful earlier captures in the same final download:

| Capture | Experiment |
|---|---|
| `charge_sleep_4.csv` | Alternating ULP sampling enabled/disabled |
| `charge_sleep_5.csv` | Alternating ISET/battery input as an ADC reference |
| `charge_sleep_6.csv` | Input held selected through the burst |
| `charge_sleep_7.csv` | Short/long startup settling with individual conversions |
| `charge_sleep_8.csv` | One conversion versus a burst |

The files have different column layouts as instrumentation evolved. Startup
`sleep_adc_awake*.csv` files compare CPU and ULP while awake; some checks occurred
without a battery during flashing and must not be treated as charger bench results.
Longer settling and averaging repeated conversions lowered the ISET result.
Whether those dips reflect an ADC artifact or a physical ISET disturbance remains
unresolved; the single-conversion approach avoided most of the discrepancy.

`scope/` contains the original FNIRSI 2C53T BMPs. Images 1-3 used AC coupling and
a physical/software 1x probe, so they do not establish absolute ISET voltage.
Images 4-8 used DC coupling and 10x: 4-6 show ISET and 7-8 ground. Images 9-15
are additional time-base captures. Images 16-19 are ordered ADC+, ADC-, ADC+, ADC-;
similar fast spikes were present in both phases. The battery was connected during
the scope tests, although it was disconnected during some later flashing checks.
