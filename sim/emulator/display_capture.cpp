#include "display_capture.h"

#include <U8g2lib.h>
#include <string.h>

#include "ui/display/display.h"

namespace sim {

  namespace {

    int g_nativeWidth = 0;
    int g_nativeHeight = 0;
    int g_transform = -1;

    bool nativePixel(const uint8_t* buffer, int nx, int ny) {
      if (nx < 0 || ny < 0 || nx >= g_nativeWidth || ny >= g_nativeHeight) return false;
      const size_t index = (size_t)(ny >> 3) * g_nativeWidth + nx;
      return (buffer[index] >> (ny & 7)) & 1;
    }

    // u8g2 keeps its buffer in the panel's native orientation, so reading logical pixels back
    // needs the inverse of the rotation the firmware set up.  Rather than hard-code one per
    // variant, the emulator draws a known asymmetric mark and works out which transform
    // reproduces it -- if the library's rotation handling ever changes, this fails loudly instead
    // of quietly showing a mirrored screen.
    void logicalToNative(int transform, int x, int y, int& nx, int& ny) {
      switch (transform) {
        case 0:
          nx = y;
          ny = x;
          break;
        case 1:
          nx = y;
          ny = g_nativeHeight - 1 - x;
          break;
        case 2:
          nx = g_nativeWidth - 1 - y;
          ny = x;
          break;
        default:
          nx = g_nativeWidth - 1 - y;
          ny = g_nativeHeight - 1 - x;
          break;
      }
    }

    bool calibrate(int width, int height) {
      g_nativeWidth = u8g2.getBufferTileWidth() * 8;
      g_nativeHeight = u8g2.getBufferTileHeight() * 8;
      if (g_nativeWidth <= 0 || g_nativeHeight <= 0) return false;

      const size_t bufferBytes = (size_t)g_nativeWidth * g_nativeHeight / 8;
      std::vector<uint8_t> saved(bufferBytes);
      memcpy(saved.data(), u8g2.getBufferPtr(), bufferBytes);

      // An L in the top-left corner: asymmetric in both axes, so only one transform matches.
      u8g2.clearBuffer();
      u8g2.drawBox(0, 0, 20, 4);
      u8g2.drawBox(0, 0, 4, 12);

      const uint8_t* buffer = u8g2.getBufferPtr();
      for (int candidate = 0; candidate < 4; candidate++) {
        bool matches = true;
        const auto probe = [&](int x, int y, bool expected) {
          int nx = 0;
          int ny = 0;
          logicalToNative(candidate, x, y, nx, ny);
          if (nativePixel(buffer, nx, ny) != expected) matches = false;
        };

        probe(1, 1, true);
        probe(18, 2, true);
        probe(2, 10, true);
        probe(18, 10, false);
        probe(30, 2, false);
        probe(2, 20, false);
        probe(width - 2, height - 2, false);
        probe(width - 2, 2, false);
        probe(2, height - 2, false);

        if (matches) {
          g_transform = candidate;
          break;
        }
      }

      memcpy(u8g2.getBufferPtr(), saved.data(), bufferBytes);
      return g_transform >= 0;
    }

  }  // namespace

  Frame captureFrame() {
    Frame frame;
    frame.width = (int)u8g2.getDisplayWidth();
    frame.height = (int)u8g2.getDisplayHeight();
    if (frame.width <= 0 || frame.height <= 0) return frame;

    if (g_transform < 0 && !calibrate(frame.width, frame.height)) return frame;

    const uint8_t* buffer = u8g2.getBufferPtr();
    frame.pixels.assign((size_t)frame.width * frame.height, 0);
    for (int y = 0; y < frame.height; y++) {
      for (int x = 0; x < frame.width; x++) {
        int nx = 0;
        int ny = 0;
        logicalToNative(g_transform, x, y, nx, ny);
        frame.pixels[(size_t)y * frame.width + x] = nativePixel(buffer, nx, ny) ? 1 : 0;
      }
    }
    return frame;
  }

  std::string packFrame(const Frame& frame) {
    std::string out;
    out.resize(((size_t)frame.width * frame.height + 7) / 8, 0);
    for (size_t i = 0; i < frame.pixels.size(); i++) {
      if (frame.pixels[i]) out[i >> 3] |= (char)(0x80 >> (i & 7));
    }
    return out;
  }

}  // namespace sim
