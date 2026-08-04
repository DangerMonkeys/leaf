# Leaf Display Layout Editor

This is a standalone browser tool for sketching Leaf display pages with U8g2-like drawing primitives.

Open `index.html` in a browser. The editor supports the current `96 x 192` display and candidate `122 x 250` and `168 x 384` display profiles.

Text is rendered from Leaf's actual U8g2 font byte arrays. Run this after changing `src/vario/ui/display/fonts.h`:

```powershell
node "tools and analysis\display_editor\generate_leaf_fonts.js"
```

Current capabilities:

- add, select, drag, duplicate, delete, and layer primitives
- move layers up/down to change draw order
- edit exact primitive coordinates and draw color
- use circle fill, non-destructive ring, thickness, and even-diameter options, backed by `drawCircleD` / `drawDiscD`
- use one rectangle primitive with fill, rounded-corner, and radius options
- render text with generated data from `src/vario/ui/display/fonts.h`
- export U8g2-style C++ calls, editable JSON layout data, a rough XBM bitmap, or a PNG mockup
- paste previously exported JSON back into the JSON export box and load it for continued editing
- import firmware-derived starter templates for current main pages

PNG export can save an exact LCD-resolution black/white image, or add a light grey border around the screen. The border thickness is 5% of the selected display width.

Some firmware fonts are intentionally partial subsets. For example, large numeric field fonts may include digits and only a few unit or compass letters. The editor falls back to `leaf_6x12` for missing glyph previews so draft text remains visible, but C++ export still uses the selected font token.

The primary page imports use constants and helper logic from the Leaf firmware drawing routines, with representative placeholder sensor values. Some runtime-dependent items, such as waypoint names, live nav arrows, and the built-in upstream `u8g2_font_12x6LED_tf` font used by Navigate, are still approximated.

The imported pages are design templates, not firmware-accurate renders. They are intended as fast starting points for layout exploration before the shared firmware/simulator renderer exists.

Use JSON for in-progress layout work. The C++ export is intended for moving finished drawing calls into firmware and is not parsed back into editable primitives.
