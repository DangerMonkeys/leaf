# Thermal Track bitmap capture

runtime_7.json is the 72.261-second Leaf 3.2.8 page sweep with ten synthetic
markers. The operator reported flight logging and a valid wind estimate.
See report.html, metadata.json, analysis.json and page-comparison.json.
The bitmap implementation stamps rings, outside-map erasure, marker glyphs and
the pilot directly into the native framebuffer, with transparent map regions.

Thermal Track averaged 6.684 ms (32 normal renders), versus 7.377 ms in runtime_6.
Navigate averaged 7.121 ms. All measured pages had zero normal display-slot
deadline crossings. The 43 remaining normal crossings were in slots 75-84;
maximum completion delay was 0.290 ms. Buttons/recovery are reported separately.

Workload, settings, duration and GPS dispatch phase differ from runtime_6.
The observed 9.4% mean reduction does not isolate the bitmap speedup, and the
absence of display-slot crossings also reflects GPS moving away from slots
39/89. This exploratory capture is not a release assessment.

The exact firmware SHA256 is recorded in metadata and verified against the
device USB firmware volume. Its compiled image and position-bearing IGC remain
local and excluded from Git. IGC verification found 121 valid fixes at one-second
intervals with no gaps. Capture uptime has no exported UTC alignment to this log.

The dirty patch and untracked-source snapshots preserve the tested code,
including temporary synthetic thermals. The committed production page removes
that test path and the temporary build environment, retaining bitmap rendering
and outlined distance text with live tracker data.

Production cleanup/build/test/flash evidence is recorded separately in
production-validation.json and production-build_info.json. No runtime timing
claim is made for the final image after the synthetic data was removed.
