# User manual LCD captures

These fixtures render the actual firmware into its 96 × 192 framebuffer. Sensor
data, routes, and charging-server responses are synthetic; these are documentation
examples, not hardware or release runtime assessments. Developer mode is off in
the navigation fixture, so its pointers use production route optimization.

From the repository root, with the normal simulator dependencies present:

```powershell
docker run --rm -v "${PWD}:/leaf" -w /leaf gcc:13 sh -c "make -f sim/Makefile -f sim/manual/capture.mk -j8 manual-captures"
node sim/manual/package-captures.mjs
```

On this Windows workstation the website's bundled Node can be used instead:
`website/.local-node/node-v24.14.0-win-x64/node.exe`.

The package step uses the website's installed `sharp` dependency. It maps the two
LCD colors to pure black and white, crops exact integer rectangles, and retains
native pixel resolution. CSS uses nearest-neighbor scaling. It does not redraw,
retouch, smooth, or synthesize any LCD pixels. The 13 metric crops use the same
top-left field in enum/selection order. All examples share one navigation fix.

Raw frames and disposable card/settings fixtures are kept outside the manual.
The JSON manifest beside the packaged images records the source commit, firmware
base, executable hashes, dirty firmware status, crops, and image hashes.

The route diagrams in `RouteGuidanceDiagram.astro` are explanatory illustrations,
not simulator screenshots or measured solver results.

Keep the website/manual release separate from firmware release runtime assessment
evidence.
