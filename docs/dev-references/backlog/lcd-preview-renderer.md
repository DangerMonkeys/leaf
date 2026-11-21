---
title: LCD Preview Renderer Backlog
description: Implemented host LCD rendering and remaining preview workflow improvements.
---

# LCD Preview Renderer Backlog

Status reconciled **2026-10-22**: the host renderer is implemented by
[`leafsim`](../../../sim/README.md). Display iteration no longer requires flashing hardware.

## Implemented

- The simulator compiles the real firmware page/menu code, Leaf font arrays, QR implementation
  and u8g2 core against host hardware substitutes. It renders the actual 96x192 portrait framebuffer.
- The browser control panel provides working buttons, saved settings, a virtual SD card,
  recorded sensor playback and live thermal training.
- Script `screenshot` steps save PNGs; `GET /api/screenshot.png?scale=N` exports the screen at
  native or integer scale. See the simulator README for launch and script instructions.
- Focused tests drive page state directly. The charging-dialog work in `18fc69af` (#425)
  includes reference-art comparisons, live-input checks and 46 native captures under
  `docs/ui-reviews/charging-dialog-revamp/`.

The approximate Python renderer in the original proposal was removed. Its replacement uses the
firmware graphics code. Windows builds run through Docker's `gcc:13` toolchain.

## Remaining work

- Add reusable deterministic fixtures for more menu/page states where they save repeated setup.
  A dedicated Leaf-AP WiFi/QR fixture remains useful: the simulator substitutes the webserver,
  so its existence alone does not establish coverage of every WiFi page state.
- Extend screenshot regression coverage as layouts change, keeping references tied to actual
  framebuffer output rather than approximations.
- Check physical LCD contrast, readability and hardware behavior on the device when needed;
  host pixels do not qualify those properties.
