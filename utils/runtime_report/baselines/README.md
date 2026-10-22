# Runtime comparison references

`index.json` identifies active references. Each versioned directory contains raw evidence,
metadata, an HTML report, the comparison report JSON and a checksum manifest.

The first active reference is
[Leaf 3.2.8 Navigate v1](leaf-3_2_8-navigate-v1/README.md), selected on 2026-10-21.
It is a provisional development baseline with unresolved attention items, not a release pass.
Use its `report.json` with `report.py --baseline` and follow its manual workload instructions.

Keep existing versions immutable. To promote a replacement, save a new directory and record
the reason, exact artifact/workload identity and reviewer in the index; preserve the old version.
Separate references are needed for other hardware, pages/workloads and simulator measurements.
