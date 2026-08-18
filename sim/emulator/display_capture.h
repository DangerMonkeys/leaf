// Reads the LCD's contents out of u8g2's frame buffer.
//
// The emulator does not redraw anything itself: what it shows is the buffer the device's own
// graphics code produced, so the pixels are the pixels.
#pragma once

#include <stdint.h>

#include <string>
#include <vector>

namespace sim {

  struct Frame {
    int width = 0;
    int height = 0;
    // One byte per pixel, 1 = lit.  Small enough (96x192) that packing buys nothing useful.
    std::vector<uint8_t> pixels;

    bool operator==(const Frame& other) const {
      return width == other.width && height == other.height && pixels == other.pixels;
    }
  };

  // Captures the current display contents.  The first call works out how u8g2's buffer maps to
  // logical pixels for this panel and rotation, and restores the buffer afterwards.
  Frame captureFrame();

  // 1-bit-per-pixel, row-major, MSB first: the wire format the browser panel decodes.
  std::string packFrame(const Frame& frame);

  // Minimal PNG (for screenshots and headless runs), scaled by an integer factor.
  std::string encodePng(const Frame& frame, int scale);

}  // namespace sim
