---
title: LCD Preview Renderer Backlog
description: Future host-side pixel-exact renderer for Leaf LCD page layout iteration.
---

# LCD Preview Renderer Backlog

Display-only UI tweaks currently require building firmware, uploading, booting Leaf, and navigating menus before the layout can be reviewed. A host-side renderer would make iteration much faster, but it needs to be pixel-for-pixel identical to the Leaf LCD output to be useful for final layout decisions.

An initial approximate Python layout renderer was tried and then removed because it was not accurate enough: host-rendered text and placeholder QR codes did not match Leaf's u8g2 fonts, QR output, title rows, footer rows, or exact drawing behavior.

Future work:

- Build a pixel-exact host renderer that runs the actual Leaf page drawing code against a host-side u8g2 framebuffer.
- Link the real Leaf font arrays from `src/vario/ui/display/fonts.h`.
- Link the real QR implementation from `src/vario/utils/qrcodex.c`.
- Link the u8g2 core from `src/libraries/U8g2/src`.
- Provide small host mocks for Arduino APIs, WiFi state, settings, page stack globals, and any hardware/global objects touched by the page under test.
- Export the rendered `96x192` native framebuffer as PNG, plus an integer-scaled preview PNG for easier review.
- Start with `PageMenuSystemWifiWebApp` in Leaf-AP mode, then generalize to other pages once the harness is proven.
- Ensure the tool uses the same display rotation/orientation as firmware (`96x192` portrait for current Leaf hardware).

Notes from the first attempt:

- A non-exact renderer is not sufficient for design decisions.
- The Codex Windows environment at the time did not expose a native C/C++ compiler (`cl`, `gcc`, `clang`, `zig`) or a Python FFI/JIT compiler, so the exact u8g2 harness could not be built there.
- Revisit this when a host compiler is available, or consider a firmware-side debug endpoint that renders the real framebuffer and exports it for review.
