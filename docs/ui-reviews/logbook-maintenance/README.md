# Logbook maintenance UI review

[Open the gallery](index.html). Only changed/new screens are included; native firmware captures
are 96 x 192 PNGs. Contact sheets enlarge them with nearest-neighbor scaling.

The capture regression uses real firmware rendering and disposable SD fixtures. It checks
400-flight filename-only browsing with zero JSON reads during scanning, more than 16 repair
candidates, conflict preservation, cancellation, missing/unreadable storage, busy storage,
active-flight exclusion, loading/paging and distinct read-failure/repair dialogs, undated filename
migration, and a minimum one-second visible interval despite extra redraws.

To recapture inside the simulator container:

```sh
make -C sim -j8 test-logbook-maintenance
```

Then package the native frames from the repository root (Python with Pillow):

```sh
python sim/scripts/build_logbook_maintenance_gallery.py --screenshots sim/build/logbook-maintenance-screens
```

`manifest.json` records the source base commit, relevant source hashes and native frame hashes.
The simulator save/upload regressions, embedded web JavaScript checks and first-boot checks were
also run. The `leaf_3_2_6_release` firmware build passed. On disposable copies of both blue
3.2.6 field snapshots, maintenance removed 19 superseded backups and renamed four undated
records while preserving all 23 primary JSON payloads byte-for-byte. No device was flashed;
physical save latency and power-loss behavior remain hardware verification work.
