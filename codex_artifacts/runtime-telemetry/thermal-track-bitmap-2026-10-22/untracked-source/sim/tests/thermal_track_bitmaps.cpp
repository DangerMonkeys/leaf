#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <vector>

#include "hardware/Leaf_SPI.h"
#include "ui/display/display.h"
#include "ui/display/thermal_track_bitmaps.h"
#include "ui/display/utils.h"

namespace {
  namespace bitmap = thermal_track_bitmaps;

  void referencePixel(int x, int y, int color) {
    if (x < 0 || x >= u8g2.getDisplayWidth() || y < 0 || y >= u8g2.getDisplayHeight()) return;
    u8g2.setDrawColor(color);
    u8g2.drawPixel(x, y);
  }

  void referenceMarker(int cx, int cy, int quality, bool selection) {
    const int radius = selection ? 6 : 4;
    for (int dy = -radius; dy <= radius; ++dy) {
      for (int dx = -radius; dx <= radius; ++dx) {
        const int distance4 = 4 * (dx * dx + dy * dy);
        const auto ring = [&](int r) {
          return distance4 <= (2 * r + 1) * (2 * r + 1) && distance4 >= (2 * r - 1) * (2 * r - 1);
        };
        if (!selection && quality == 4 && !dx && !dy) {
          referencePixel(cx, cy, 0);
        } else if (selection ? ring(6)
                   : quality >= 4
                       ? distance4 <= 81
                       : ring(4) || (quality >= 2 && ring(3)) || (quality >= 3 && ring(2))) {
          referencePixel(cx + dx, cy + dy, 1);
        }
      }
    }
  }

  size_t checkRotation(bool reverse) {
    u8g2.setDisplayRotation(reverse ? U8G2_R3 : U8G2_R1);
    u8g2.firstPage();
    const int width = u8g2.getDisplayWidth(), height = u8g2.getDisplayHeight();
    const int stride = u8g2.getBufferTileWidth() * 8;
    const size_t size = stride * u8g2.getBufferTileHeight();
    std::vector<uint8_t> storage(size + 64, 0xA5), seed(size), expected(size);
    for (size_t i = 0; i < size; ++i) seed[i] = (i * 37) ^ (i >> 3);
    auto* original = u8g2.getBufferPtr();
    u8g2.getU8g2()->tile_buf_ptr = storage.data() + 32;
    const bitmap::Canvas canvas(u8g2.getBufferPtr(), stride, width, height, reverse);
    size_t cases = 0;
    for (int y : {-13, -1, 0, 76, 124, height - 1, height + 1}) {
      for (int x = -13; x <= width + 13; x += 3) {
        for (int quality = 1; quality <= 6; ++quality) {
          memcpy(u8g2.getBufferPtr(), seed.data(), size);
          referenceMarker(x, y, quality, quality == 6);
          memcpy(expected.data(), u8g2.getBufferPtr(), size);
          memcpy(u8g2.getBufferPtr(), seed.data(), size);
          if (quality == 6)
            canvas.draw(x - 6, y - 6, bitmap::selection);
          else
            canvas.draw(x - 4, y - 4, bitmap::markers[quality - 1]);
          assert(memcmp(expected.data(), u8g2.getBufferPtr(), size) == 0);
          for (size_t guard = 0; guard < 32; ++guard) {
            assert(storage[guard] == 0xA5);
            assert(storage[size + 32 + guard] == 0xA5);
          }
          ++cases;
        }
      }
    }

    memcpy(u8g2.getBufferPtr(), seed.data(), size);
    for (int y = 68; y < 180; ++y) {
      for (int x = 0; x < 104 && x < width; ++x) {
        const int dx = 2 * x + 1 - 96, dy = 2 * y + 1 - 248;
        if (dx * dx + dy * dy > 96 * 96) referencePixel(x, y, 0);
      }
    }
    u8g2.setDrawColor(1);
    for (int diameter : {50, 74, 96}) drawCircleD(48, 124, diameter);
    memcpy(expected.data(), u8g2.getBufferPtr(), size);
    memcpy(u8g2.getBufferPtr(), seed.data(), size);
    constexpr auto forward = bitmap::makeOverlay<false>();
    constexpr auto backward = bitmap::makeOverlay<true>();
    canvas.overlay(reverse ? backward : forward);
    assert(memcmp(expected.data(), u8g2.getBufferPtr(), size) == 0);
    for (size_t guard = 0; guard < 32; ++guard) {
      assert(storage[guard] == 0xA5);
      assert(storage[size + 32 + guard] == 0xA5);
    }
    u8g2.getU8g2()->tile_buf_ptr = original;
    return cases + 1;
  }
}  // namespace

int main() {
  spi_init();
  display.init();
  const size_t cases = checkRotation(false) + checkRotation(true);
  printf("Thermal map bitmaps: %zu sprite/overlay cases passed, R1/R3 and screen-edge guards\n",
         cases);
}
