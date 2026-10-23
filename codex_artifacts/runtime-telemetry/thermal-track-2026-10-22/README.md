# Thermal Track circle optimization capture

Exploratory Leaf 3.2.8 display timing work. runtime_2.json matches the original
screenshot; runtime_6.json is the subsequent ten-marker test with optimized
circles, clipped markers and outlined distance text. Earlier captures remain
unchanged. See report.html, metadata.json and page-comparison.json.

The exact test firmware is identified by SHA256 in the metadata/build records.
Its compiled image is retained locally and excluded from Git. The dirty patch
and untracked-source snapshots identify the measured implementation, including
the temporary synthetic-marker environment that was subsequently removed.

This short manual sweep is not a release assessment or a matched comparison.
The production branch retains the rendering improvements and uses live thermals.
