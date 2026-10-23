#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <chrono>
#include <cmath>
#include <vector>

#include "hardware/Leaf_SPI.h"
#include "ui/display/circle_geometry.h"
#include "ui/display/display.h"
#include "ui/display/utils.h"

namespace {
  template <uint8_t diameter>
  void checkClip() {
    constexpr circle_geometry::EvenCircleClip<diameter> clip(48, 124);
    for (int y = 124 - diameter / 2 - 2; y <= 124 + diameter / 2 + 2; ++y) {
      for (int x = 48 - diameter / 2 - 2; x <= 48 + diameter / 2 + 2; ++x) {
        const int32_t dx2 = 2 * x + 1 - 96, dy2 = 2 * y + 1 - 248;
        assert(clip.contains(x, y) == (dx2 * dx2 + dy2 * dy2 <= diameter * diameter));
      }
    }
  }

  void checkClippedGeometry() {
    checkClip<2>();
    checkClip<4>();
    checkClip<50>();
    checkClip<74>();
    checkClip<96>();
    checkClip<254>();
    // Check the incremental extents independently against a mathematical square root,
    // including the empty rows of the hollow interior and the zero-radius case.
    for (int r = 0; r <= 255; ++r) {
      for (const int32_t squared : {r * (r + 1), r ? r * (r - 1) : -1}) {
        circle_geometry::RowExtents rows(squared, -r);
        for (int y = -r; y <= r; ++y) {
          const int32_t remaining = squared - y * y;
          const int expected = remaining < 0 ? -1 : int(std::sqrt(double(remaining)));
          assert(rows.next() == expected);
        }
      }
    }
    constexpr circle_geometry::EvenCircleClip<96> clip(48, 124);
    const int16_t centers[][2] = {{48, 124}, {0, 124}, {95, 124}, {48, 76},  {48, 171},
                                  {14, 90},  {81, 90}, {14, 157}, {81, 157}, {-4, 124},
                                  {99, 124}, {48, 72}, {48, 175}, {48, 120}};
    size_t cases = 0;
    for (int r : {0, 1, 2, 3, 4, 6, 12, 48, 255}) {
      for (const auto& center : centers) {
        for (bool hollow : {false, true}) {
          std::vector<uint8_t> pixels(96 * 192);
          circle_geometry::RowExtents outer(r * (r + 1), -r);
          circle_geometry::RowExtents inner(r ? r * (r - 1) : -1, -r);
          const auto stroke = [&](int y, int16_t start, int16_t end) {
            if (!clip.clipSpan(y, start, end)) return;
            assert(y >= 0 && y < 192 && start >= 0 && end < 96);
            for (int x = start; x <= end; ++x) ++pixels[y * 96 + x];
          };
          for (int y = center[1] - r; y <= center[1] + r; ++y) {
            const int out = outer.next(), in = inner.next();
            if (!hollow || in < 0) {
              stroke(y, center[0] - out, center[0] + out);
            } else {
              stroke(y, center[0] - out, center[0] - in - 1);
              stroke(y, center[0] + in + 1, center[0] + out);
            }
          }
          for (int y = 0; y < 192; ++y) {
            for (int x = 0; x < 96; ++x) {
              const int dx = x - center[0], dy = y - center[1];
              const int distance4 = 4 * (dx * dx + dy * dy);
              const int mx2 = 2 * x + 1 - 96, my2 = 2 * y + 1 - 248;
              const bool expected = mx2 * mx2 + my2 * my2 <= 96 * 96 &&
                                    distance4 <= (2 * r + 1) * (2 * r + 1) &&
                                    (!hollow || !r || distance4 >= (2 * r - 1) * (2 * r - 1));
              assert(pixels[y * 96 + x] == expected);
            }
          }
          ++cases;
        }
      }
    }
    printf("Clipped circles and discs: %zu pixel-geometry cases passed\n", cases);
  }

  // Keep the previous renderer as a pixel-compatibility and performance reference.
  bool referenceSpan(int16_t cx, int16_t cy, uint8_t diameter, int16_t y, int16_t& start,
                     int16_t& end) {
    const int32_t dy2 = int32_t(y) * 2 + 1 - 2 * cy;
    const int32_t remaining = int32_t(diameter) * diameter - dy2 * dy2;
    if (remaining < 0) return false;
    int16_t root = 0;
    while (int32_t(root + 1) * (root + 1) <= remaining) ++root;
    if ((root & 1) == 0) --root;
    if (root < 0) return false;
    start = (2 * cx - root - 1) / 2;
    end = (2 * cx + root - 1) / 2;
    return end >= start;
  }

  void referenceLine(int16_t start, int16_t y, int16_t end) {
    if (end >= start) u8g2.drawHLine(start, y, end - start + 1);
  }

  void referenceCircle(int16_t cx, int16_t cy, uint8_t diameter) {
    if (diameter == 0) return;
    if (diameter & 1) {
      u8g2.drawCircle(cx, cy, diameter / 2);
      return;
    }
    const int16_t top = cy - diameter / 2;
    for (int16_t y = top; y < top + diameter; ++y) {
      int16_t start, end, prevStart, prevEnd, nextStart, nextEnd;
      if (!referenceSpan(cx, cy, diameter, y, start, end)) continue;
      const bool prev = referenceSpan(cx, cy, diameter, y - 1, prevStart, prevEnd);
      const bool next = referenceSpan(cx, cy, diameter, y + 1, nextStart, nextEnd);
      u8g2.drawPixel(start, y);
      if (end != start) u8g2.drawPixel(end, y);
      if (!prev) {
        referenceLine(start, y, end);
      } else {
        referenceLine(start, y, prevStart - 1);
        referenceLine(prevEnd + 1, y, end);
      }
      if (!next) {
        referenceLine(start, y, end);
      } else {
        referenceLine(start, y, nextStart - 1);
        referenceLine(nextEnd + 1, y, end);
      }
    }
  }

  void checkPixels() {
    size_t cases = 0;
    for (const auto rotation : {U8G2_R0, U8G2_R1, U8G2_R2, U8G2_R3}) {
      u8g2.setDisplayRotation(rotation);
      u8g2.firstPage();
      const size_t size = u8g2.getBufferTileWidth() * u8g2.getBufferTileHeight() * 8;
      std::vector<uint8_t> seed(size), expected(size);
      for (size_t i = 0; i < size; ++i) seed[i] = (i * 37) ^ (i >> 3);
      const int16_t w = u8g2.getDisplayWidth(), h = u8g2.getDisplayHeight();
      const int16_t centers[][2] = {{-10, -12},
                                    {0, 0},
                                    {int16_t(w / 2), int16_t(h / 2)},
                                    {int16_t(w - 1), int16_t(h - 1)},
                                    {int16_t(w + 10), int16_t(h + 12)}};
      for (bool clipped : {false, true}) {
        if (clipped)
          u8g2.setClipWindow(11, 17, w - 14, h - 19);
        else
          u8g2.setMaxClipWindow();
        for (uint8_t color : {0, 1, 2}) {
          u8g2.setDrawColor(color);
          for (const auto& center : centers) {
            for (int diameter = 0; diameter <= 255; ++diameter) {
              memcpy(u8g2.getBufferPtr(), seed.data(), size);
              referenceCircle(center[0], center[1], diameter);
              memcpy(expected.data(), u8g2.getBufferPtr(), size);
              memcpy(u8g2.getBufferPtr(), seed.data(), size);
              drawCircleD(center[0], center[1], diameter);
              if (memcmp(expected.data(), u8g2.getBufferPtr(), size) != 0) {
                fprintf(stderr, "Circle mismatch: diameter=%d center=(%d,%d) color=%d clip=%d\n",
                        diameter, center[0], center[1], color, clipped);
                assert(false);
              }
              ++cases;
            }
          }
        }
      }
    }
    printf("Circle framebuffer equivalence: %zu cases passed\n", cases);
    u8g2.setDisplayRotation(U8G2_R1);
    u8g2.setMaxClipWindow();
    u8g2.setDrawColor(1);
    u8g2.clearBuffer();
  }

  double benchmark(void (*draw)(int16_t, int16_t, uint8_t)) {
    constexpr int frames = 2000;
    const auto start = std::chrono::steady_clock::now();
    for (int frame = 0; frame < frames; ++frame) {
      draw(48, 124, 50);
      draw(48, 124, 74);
      draw(48, 124, 96);
    }
    return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start)
               .count() /
           frames;
  }
}  // namespace

int main() {
  spi_init();
  display.init();
  checkClippedGeometry();
  checkPixels();
  // Draw into the real framebuffer without sending it: isolate circle rasterization.
  const double before = benchmark(referenceCircle);
  const double after = benchmark(drawCircleD);
  printf(
      "Three Thermal Track rings, host CPU only: previous %.2f us, incremental %.2f us (%.2fx)\n",
      before, after, before / after);
}
